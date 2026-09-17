#include "shared/diagnostics/runtime_resource_events.h"

void runtimeResourceMonitorReset(const char*, const char*)
{
}

void runtimeResourceMonitorSetActive(bool)
{
}

bool runtimeResourceMonitorIsCapturing(void)
{
    return false;
}

bool runtimeResourceMonitorMatchesApp(const char*, const char*)
{
    return false;
}

void runtimeResourceMonitorSetAppSha256(const char*)
{
}

void runtimeResourceMonitorSetAppResources(GuestPackage*)
{
}

void runtimeResourceMonitorSetGuestResources(GuestPackage*)
{
}

void runtimeResourceMonitorRecordGuestOpen(
    const char*, const GuestResourceEntry*, bool)
{
}

void runtimeResourceMonitorRecordGuestLoadContent(
    const GuestResourceEntry*, uint32_t, const void*, uint32_t, uint32_t)
{
}

void runtimeResourceMonitorRecordGuestClose(const GuestResourceEntry*)
{
}

void runtimeResourceMonitorRecordOpen(
    RuntimeResourceMonitorSource, const char*, const GuestResourceEntry*, bool)
{
}

void runtimeResourceMonitorRecordLoadContent(
    RuntimeResourceMonitorSource, const GuestResourceEntry*, uint32_t,
    const void*, uint32_t, uint32_t)
{
}

void runtimeResourceMonitorRecordPackageLoadContent(
    const char*, uint32_t, uint32_t, const void*, uint32_t, uint32_t)
{
}

void runtimeResourceMonitorRecordExternalLoadContent(
    const char*, uint32_t, uint32_t, const void*, uint32_t, uint32_t)
{
}

void runtimeResourceMonitorRecordPackageClose(const char*)
{
}

void runtimeResourceMonitorRecordExternalClose(const char*)
{
}

void runtimeResourceMonitorRecordSeek(
    RuntimeResourceMonitorSource, const GuestResourceEntry*, uint32_t)
{
}

void runtimeResourceMonitorRecordClose(
    RuntimeResourceMonitorSource, const GuestResourceEntry*)
{
}
