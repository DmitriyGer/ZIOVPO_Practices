#pragma once

#include <windows.h>

namespace ProcessProtection
{
    enum class AdministratorsTerminationPolicy
    {
        AllowTerminate = 0,
        DenyTerminate = 1
    };

    struct ProtectionPolicy
    {
        AdministratorsTerminationPolicy administratorsTerminationPolicy =
            AdministratorsTerminationPolicy::DenyTerminate;
        bool denyServiceStopForAdministrators = true;
        // Reserved for compatibility. Service DACL hardening must not deny WRITE_DAC/WRITE_OWNER
        // to Administrators because that breaks service maintenance and diagnostics flows.
        bool restrictServiceSecurityWriteForAdministrators = false;
    };

    // Updates runtime hardening policy used by process/service security helpers.
    void SetProtectionPolicy(const ProtectionPolicy& policy);
    // Returns currently active hardening policy.
    ProtectionPolicy GetProtectionPolicy();

    // Applies anti-termination DACL for the specified process handle.
    bool ProtectProcessFromTermination(HANDLE processHandle);
    // Hardens the current process object.
    bool HardenProcessSecurity();
    // Hardens TrayService service-object DACL in SCM.
    bool HardenServiceSecurity();
    // Writes current process-object DACL to OutputDebugStringW for diagnostics.
    bool DumpProcessDacl(HANDLE processHandle, const wchar_t* context);
}
