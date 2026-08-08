package org.libsdl.app;

public final class HIDDeviceIdAllocatorTest {
    public static void main(String[] args) {
        HIDDeviceIdAllocator allocator = new HIDDeviceIdAllocator(0);
        int first = allocator.resolve(-1);
        int second = allocator.resolve(-1);
        int persisted = allocator.resolve(first);
        if (first != 1 || second != 2 || persisted != first ||
                allocator.getNextDeviceId() != 3) {
            throw new AssertionError("Device IDs are not positive and stable");
        }
        System.out.println("hid_device_id passed baseline_first=0 fixed_first=1 " +
                "fixed_second=2 persisted=1");
    }
}
