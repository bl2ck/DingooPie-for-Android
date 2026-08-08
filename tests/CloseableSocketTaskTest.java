package com.dingoopie.android;

import java.net.Socket;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.atomic.AtomicInteger;

public final class CloseableSocketTaskTest {
    public static void main(String[] args) throws Exception {
        CountDownLatch baselineStarted = new CountDownLatch(1);
        CountDownLatch baselineRelease = new CountDownLatch(1);
        ExecutorService baselineExecutor = Executors.newSingleThreadExecutor();
        baselineExecutor.execute(() -> {
            baselineStarted.countDown();
            try {
                baselineRelease.await();
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
        });
        baselineStarted.await();
        Socket baselineSocket = new Socket();
        baselineExecutor.execute(() -> {
            try {
                baselineSocket.close();
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        });
        List<Runnable> baselinePending = baselineExecutor.shutdownNow();
        baselineRelease.countDown();
        if (baselinePending.size() != 1 || baselineSocket.isClosed()) {
            throw new AssertionError("Baseline queue did not reproduce the open socket");
        }
        baselineSocket.close();

        AtomicInteger handled = new AtomicInteger();
        CountDownLatch fixedStarted = new CountDownLatch(1);
        CountDownLatch fixedRelease = new CountDownLatch(1);
        ExecutorService fixedExecutor = Executors.newSingleThreadExecutor();
        fixedExecutor.execute(() -> {
            fixedStarted.countDown();
            try {
                fixedRelease.await();
            } catch (InterruptedException exception) {
                Thread.currentThread().interrupt();
            }
        });
        fixedStarted.await();
        Socket pendingSocket = new Socket();
        fixedExecutor.execute(new CloseableSocketTask(
                pendingSocket, socket -> handled.incrementAndGet()));
        List<Runnable> fixedPending = fixedExecutor.shutdownNow();
        fixedRelease.countDown();
        for (Runnable pending : fixedPending) {
            if (pending instanceof CloseableSocketTask) {
                ((CloseableSocketTask)pending).close();
            }
        }
        if (fixedPending.size() != 1 || !pendingSocket.isClosed() || handled.get() != 0) {
            throw new AssertionError("Fixed queue did not close the pending socket");
        }

        Socket runningSocket = new Socket();
        CloseableSocketTask runningTask = new CloseableSocketTask(runningSocket, socket -> {
            handled.incrementAndGet();
            try {
                socket.close();
            } catch (Exception exception) {
                throw new RuntimeException(exception);
            }
        });
        runningTask.run();
        runningTask.close();
        if (!runningSocket.isClosed() || handled.get() != 1) {
            throw new AssertionError("Running socket task did not transfer ownership");
        }

        System.out.println("closeable_socket_task passed baseline_open=1 fixed_closed=1 handled=1");
    }
}
