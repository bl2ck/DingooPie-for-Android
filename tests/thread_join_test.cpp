#include "shared/execution/thread_join.h"

#include <chrono>
#include <pthread.h>
#include <stdio.h>
#include <thread>

struct ThreadCase
{
    RuntimeThreadCompletion* completion;
    uint32_t sleepMs;
};

static void* runThreadCase(void* data)
{
    ThreadCase* threadCase = (ThreadCase*)data;
    std::this_thread::sleep_for(std::chrono::milliseconds(threadCase->sleepMs));
    runtimeThreadCompletionSignal(threadCase->completion);
    return NULL;
}

static bool runJoinCase(uint32_t sleepMs, uint32_t timeoutMs, bool expectedJoined)
{
    RuntimeThreadCompletion completion = RUNTIME_THREAD_COMPLETION_INITIALIZER;
    ThreadCase threadCase = { &completion, sleepMs };
    pthread_t thread;
    if (pthread_create(&thread, NULL, runThreadCase, &threadCase) != 0)
    {
        return false;
    }

    const std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    int joinError = 0;
    const RuntimeThreadJoinResult joinResult = runtimeThreadJoinWithTimeout(
        thread, &completion, timeoutMs, &joinError);
    const bool joined = joinResult == RUNTIME_THREAD_JOINED;
    const long long elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();

    if (!joined)
    {
        pthread_join(thread, NULL);
    }
    printf("thread_join case sleep_ms=%u timeout_ms=%u result=%u error=%d elapsed_ms=%lld\n",
        sleepMs, timeoutMs, (unsigned int)joinResult, joinError, elapsedMs);
    const RuntimeThreadJoinResult expectedResult = expectedJoined
        ? RUNTIME_THREAD_JOINED
        : RUNTIME_THREAD_JOIN_TIMEOUT;
    return joinResult == expectedResult &&
        (expectedJoined ? elapsedMs < timeoutMs : elapsedMs >= timeoutMs);
}

int main()
{
    const bool quickJoin = runJoinCase(20, 500, true);
    const bool boundedTimeout = runJoinCase(250, 80, false);
    const bool passed = quickJoin && boundedTimeout;
    printf("thread_join_regression result=%s\n", passed ? "pass" : "fail");
    return passed ? 0 : 1;
}
