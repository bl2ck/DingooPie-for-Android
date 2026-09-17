#include "app/hle/app_task_lifecycle.h"
#include "shared/diagnostics/profile_counter.h"
#include "shared/execution/thread_safe_text.h"
#include "shared/execution/runtime_tick_clock.h"

#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_completed = 0;
static RuntimeProfileCounter g_profileCounter;
static ThreadSafeText<96> g_sharedText;
static RuntimeTickClock g_tickClock;
static volatile int g_concurrencyStart = 0;
static volatile int g_sharedTextErrors = 0;
static volatile int g_tickClockErrors = 0;

struct ConcurrencyTask
{
    int index;
    int iterations;
};

static void* runConcurrencyTask(void* data)
{
    ConcurrencyTask* task = (ConcurrencyTask*)data;
    while (!__atomic_load_n(&g_concurrencyStart, __ATOMIC_ACQUIRE))
    {
        sched_yield();
    }
    for (int sequence = 0; sequence < task->iterations; ++sequence)
    {
        unsigned int checksum = (unsigned int)task->index ^
            ((unsigned int)sequence * 2654435761u);
        char value[96];
        snprintf(value, sizeof(value), "writer=%d sequence=%d checksum=%08x",
            task->index, sequence, checksum);
        g_sharedText.set(value);

        char snapshot[96];
        g_sharedText.copy(snapshot, sizeof(snapshot));
        int writer = -1;
        int observedSequence = -1;
        unsigned int observedChecksum = 0;
        if (sscanf(snapshot, "writer=%d sequence=%d checksum=%x",
                &writer, &observedSequence, &observedChecksum) != 3 ||
            observedChecksum != ((unsigned int)writer ^
                ((unsigned int)observedSequence * 2654435761u)))
        {
            __atomic_add_fetch(&g_sharedTextErrors, 1, __ATOMIC_RELAXED);
        }
        if (g_tickClock.elapsed(5000000) != 0)
        {
            __atomic_add_fetch(&g_tickClockErrors, 1, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static bool runConcurrencyRegression(void)
{
    const int threadCount = 16;
    const int iterations = 2000;
    pthread_t threads[threadCount];
    ConcurrencyTask tasks[threadCount];
    g_tickClock.reset();
    for (int index = 0; index < threadCount; ++index)
    {
        tasks[index].index = index;
        tasks[index].iterations = iterations;
        if (pthread_create(&threads[index], NULL, runConcurrencyTask, &tasks[index]) != 0)
        {
            return false;
        }
    }
    __atomic_store_n(&g_concurrencyStart, 1, __ATOMIC_RELEASE);
    for (int index = 0; index < threadCount; ++index)
    {
        pthread_join(threads[index], NULL);
    }
    g_tickClock.restoreElapsed(6000000, 250);
    return __atomic_load_n(&g_sharedTextErrors, __ATOMIC_RELAXED) == 0 &&
        __atomic_load_n(&g_tickClockErrors, __ATOMIC_RELAXED) == 0 &&
        g_tickClock.elapsed(6000000) == 250;
}

static void* runShortTask(void*)
{
    taskThreadLifecycleDetachCurrent();
    g_profileCounter.increment();
    __atomic_add_fetch(&g_completed, 1, __ATOMIC_RELEASE);
    taskThreadLifecycleFinish();
    return NULL;
}

static long readStatusKb(const char* key)
{
    FILE* file = fopen("/proc/self/status", "r");
    if (!file) return -1;
    char line[256];
    long value = -1;
    while (fgets(line, sizeof(line), file))
    {
        if (!strncmp(line, key, strlen(key)))
        {
            sscanf(line + strlen(key), "%ld", &value);
            break;
        }
    }
    fclose(file);
    return value;
}

int main(int argc, char** argv)
{
    if (!runConcurrencyRegression())
    {
        printf("runtime_concurrency failed text_errors=%d clock_errors=%d\n",
            (int)__atomic_load_n(&g_sharedTextErrors, __ATOMIC_RELAXED),
            (int)__atomic_load_n(&g_tickClockErrors, __ATOMIC_RELAXED));
        return 1;
    }
    int taskCount = argc > 1 ? atoi(argv[1]) : 4000;
    long vmSizeBefore = readStatusKb("VmSize:");
    for (int index = 0; index < taskCount; ++index)
    {
        taskThreadLifecycleBegin();
        pthread_t thread;
        int result = pthread_create(&thread, NULL, runShortTask, NULL);
        if (result != 0)
        {
            taskThreadLifecycleCancelBegin();
            printf("create_failed index=%d error=%d\n", index, result);
            return 1;
        }
        while (__atomic_load_n(&g_completed, __ATOMIC_ACQUIRE) <= index)
        {
            sched_yield();
        }
    }
    taskThreadLifecycleWaitForAll();
    long vmSizeAfter = readStatusKb("VmSize:");
    long delta = vmSizeAfter - vmSizeBefore;
    size_t active = taskThreadLifecycleActiveCount();
    uint64_t profileCount = g_profileCounter.take();
    printf("task_thread_lifecycle passed tasks=%d completed=%d profile=%llu active=%zu "
        "shared_text_errors=0 clock_errors=0 vmsize_delta_kb=%ld\n",
        taskCount, (int)__atomic_load_n(&g_completed, __ATOMIC_ACQUIRE),
        (unsigned long long)profileCount, active, delta);
    return __atomic_load_n(&g_completed, __ATOMIC_ACQUIRE) == taskCount &&
        profileCount == (uint64_t)taskCount && active == 0 && delta < 16384 ? 0 : 1;
}
