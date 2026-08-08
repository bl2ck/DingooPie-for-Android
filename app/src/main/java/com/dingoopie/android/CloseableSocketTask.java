package com.dingoopie.android;

import java.io.Closeable;
import java.io.IOException;
import java.net.Socket;

final class CloseableSocketTask implements Runnable, Closeable {
    interface SocketHandler {
        void handle(Socket socket);
    }

    private Socket pendingSocket;
    private final SocketHandler handler;

    CloseableSocketTask(Socket socket, SocketHandler handler) {
        pendingSocket = socket;
        this.handler = handler;
    }

    @Override
    public void run() {
        Socket socket;
        synchronized (this) {
            socket = pendingSocket;
            pendingSocket = null;
        }
        if (socket != null) {
            handler.handle(socket);
        }
    }

    @Override
    public synchronized void close() throws IOException {
        if (pendingSocket == null) {
            return;
        }
        pendingSocket.close();
        pendingSocket = null;
    }
}
