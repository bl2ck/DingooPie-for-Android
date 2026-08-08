package org.libsdl.app;

final class HIDDeviceIdAllocator {
    private int nextDeviceId;

    HIDDeviceIdAllocator(int persistedNextDeviceId) {
        nextDeviceId = Math.max(1, persistedNextDeviceId);
    }

    int resolve(int persistedDeviceId) {
        return persistedDeviceId > 0 ? persistedDeviceId : nextDeviceId++;
    }

    int getNextDeviceId() {
        return nextDeviceId;
    }
}
