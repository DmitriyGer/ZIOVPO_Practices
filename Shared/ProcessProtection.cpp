#include "ProcessProtection.h"

#include "ServiceUtils.h"

#include <Aclapi.h>
#include <accctrl.h>
#include <sddl.h>

#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace
{
    struct LocalFreeDeleter
    {
        void operator()(void* memory) const noexcept
        {
            if (memory != nullptr)
            {
                LocalFree(memory);
            }
        }
    };

    using ScopedLocalMemory = std::unique_ptr<void, LocalFreeDeleter>;

    struct HandleCloser
    {
        void operator()(HANDLE handle) const noexcept
        {
            if (handle != nullptr && handle != INVALID_HANDLE_VALUE)
            {
                CloseHandle(handle);
            }
        }
    };

    using ScopedHandle = std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

    struct ScHandleCloser
    {
        void operator()(SC_HANDLE handle) const noexcept
        {
            if (handle != nullptr)
            {
                CloseServiceHandle(handle);
            }
        }
    };

    using ScopedScHandle = std::unique_ptr<std::remove_pointer_t<SC_HANDLE>, ScHandleCloser>;

    class WellKnownSid
    {
    public:
        explicit WellKnownSid(WELL_KNOWN_SID_TYPE sidType)
        {
            m_buffer.resize(SECURITY_MAX_SID_SIZE);
            DWORD sidSize = static_cast<DWORD>(m_buffer.size());
            if (!CreateWellKnownSid(sidType, nullptr, m_buffer.data(), &sidSize))
            {
                m_lastError = GetLastError();
                m_buffer.clear();
                return;
            }

            m_buffer.resize(sidSize);
        }

        PSID Get() const
        {
            if (m_buffer.empty())
            {
                return nullptr;
            }

            return const_cast<BYTE*>(m_buffer.data());
        }

        bool IsValid() const
        {
            return Get() != nullptr && IsValidSid(Get()) != FALSE;
        }

        DWORD LastError() const
        {
            return m_lastError;
        }

    private:
        std::vector<BYTE> m_buffer;
        DWORD m_lastError = ERROR_SUCCESS;
    };

    struct AceSpec
    {
        PSID sid = nullptr;
        ACCESS_MODE accessMode = NOT_USED_ACCESS;
        DWORD accessMask = 0;
        DWORD inheritance = NO_INHERITANCE;
    };

    ProcessProtection::ProtectionPolicy g_policy = {};

    std::wstring FormatErrorMessage(DWORD errorCode)
    {
        wchar_t* rawMessage = nullptr;
        const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER
            | FORMAT_MESSAGE_FROM_SYSTEM
            | FORMAT_MESSAGE_IGNORE_INSERTS;
        const DWORD result = FormatMessageW(
            flags,
            nullptr,
            errorCode,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            reinterpret_cast<LPWSTR>(&rawMessage),
            0,
            nullptr);
        if (result == 0 || rawMessage == nullptr)
        {
            return L"(no text)";
        }

        std::wstring message = rawMessage;
        LocalFree(rawMessage);
        return message;
    }

    void LogHardeningWarning(
        const wchar_t* stepName,
        const wchar_t* functionName,
        DWORD errorCode)
    {
        std::wstring text = L"[TrayService][WARNING] ";
        text += stepName != nullptr ? stepName : L"(unknown hardening step)";
        text += L" failed at ";
        text += functionName != nullptr ? functionName : L"(unknown function)";
        text += L". error=";
        text += std::to_wstring(errorCode);
        text += L" message=";
        text += FormatErrorMessage(errorCode);
        text += L" TrayService will continue where the caller allows warning-only hardening.\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogHardeningWarningMessage(const wchar_t* stepName, const wchar_t* message)
    {
        std::wstring text = L"[TrayService][WARNING] ";
        text += stepName != nullptr ? stepName : L"(unknown hardening step)";
        text += L": ";
        text += message != nullptr ? message : L"(no details)";
        text += L" TrayService will continue where the caller allows warning-only hardening.\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogHardeningInfo(const wchar_t* stepName, const wchar_t* message)
    {
        std::wstring text = L"[TrayService][INFO] ";
        text += stepName != nullptr ? stepName : L"(unknown hardening step)";
        text += L": ";
        text += message != nullptr ? message : L"(no details)";
        text += L"\r\n";
        OutputDebugStringW(text.c_str());
    }

    void LogPidInfo(const wchar_t* stepName, const wchar_t* prefix, DWORD processId)
    {
        std::wstring text = prefix != nullptr ? prefix : L"pid=";
        text += std::to_wstring(processId);
        LogHardeningInfo(stepName, text.c_str());
    }

    bool LogAndReturnFailure(
        const wchar_t* stepName,
        const wchar_t* functionName,
        DWORD errorCode)
    {
        LogHardeningWarning(stepName, functionName, errorCode);
        return false;
    }

    bool ValidateWellKnownSids(
        const wchar_t* stepName,
        const WellKnownSid& systemSid,
        const WellKnownSid& administratorsSid,
        const WellKnownSid& usersSid)
    {
        if (!systemSid.IsValid())
        {
            return LogAndReturnFailure(
                stepName,
                L"CreateWellKnownSid(WinLocalSystemSid)",
                systemSid.LastError() != ERROR_SUCCESS ? systemSid.LastError() : ERROR_INVALID_SID);
        }

        if (!administratorsSid.IsValid())
        {
            return LogAndReturnFailure(
                stepName,
                L"CreateWellKnownSid(WinBuiltinAdministratorsSid)",
                administratorsSid.LastError() != ERROR_SUCCESS ? administratorsSid.LastError() : ERROR_INVALID_SID);
        }

        if (!usersSid.IsValid())
        {
            return LogAndReturnFailure(
                stepName,
                L"CreateWellKnownSid(WinBuiltinUsersSid)",
                usersSid.LastError() != ERROR_SUCCESS ? usersSid.LastError() : ERROR_INVALID_SID);
        }

        return true;
    }

    bool AcquireProcessSecurityHandle(
        HANDLE processHandle,
        const wchar_t* stepName,
        ScopedHandle& openedHandle,
        HANDLE& effectiveHandle,
        DWORD& processId)
    {
        processId = GetProcessId(processHandle);
        if (processId == 0)
        {
            return LogAndReturnFailure(stepName, L"GetProcessId", GetLastError());
        }

        LogPidInfo(stepName, L"ProtectProcessFromTermination start. pid=", processId);

        openedHandle.reset(OpenProcess(
            READ_CONTROL | WRITE_DAC | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId));
        if (!openedHandle)
        {
            LogHardeningWarning(stepName, L"OpenProcess(READ_CONTROL|WRITE_DAC)", GetLastError());
            effectiveHandle = processHandle;
            LogHardeningInfo(
                stepName,
                L"Falling back to the original process handle for DACL operations.");
            return true;
        }

        effectiveHandle = openedHandle.get();
        LogHardeningInfo(
            stepName,
            L"OpenProcess(READ_CONTROL|WRITE_DAC|PROCESS_QUERY_LIMITED_INFORMATION) succeeded.");
        return true;
    }

    bool ContainsAceForSid(PACL dacl, PSID sid, bool denyAce, DWORD requiredAccessMask)
    {
        if (dacl == nullptr || sid == nullptr || !IsValidSid(sid))
        {
            return false;
        }

        ACL_SIZE_INFORMATION aclInfo = {};
        if (!GetAclInformation(dacl, &aclInfo, sizeof(aclInfo), AclSizeInformation))
        {
            return false;
        }

        for (DWORD aceIndex = 0; aceIndex < aclInfo.AceCount; ++aceIndex)
        {
            LPVOID aceEntry = nullptr;
            if (!GetAce(dacl, aceIndex, &aceEntry) || aceEntry == nullptr)
            {
                continue;
            }

            const auto* header = static_cast<const ACE_HEADER*>(aceEntry);
            const bool isDeny = header->AceType == ACCESS_DENIED_ACE_TYPE;
            const bool isAllow = header->AceType == ACCESS_ALLOWED_ACE_TYPE;
            if ((!denyAce && !isAllow) || (denyAce && !isDeny))
            {
                continue;
            }

            const auto* ace = static_cast<const ACCESS_ALLOWED_ACE*>(aceEntry);
            const PSID aceSid = reinterpret_cast<PSID>(const_cast<DWORD*>(&ace->SidStart));
            if (!IsValidSid(aceSid) || !EqualSid(aceSid, sid))
            {
                continue;
            }

            if ((ace->Mask & requiredAccessMask) == requiredAccessMask)
            {
                return true;
            }
        }

        return false;
    }

    bool DumpProcessDaclInternal(HANDLE processHandle, const wchar_t* stepName, DWORD processId)
    {
        if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE)
        {
            return LogAndReturnFailure(stepName, L"DumpProcessDacl(handle validation)", ERROR_INVALID_HANDLE);
        }

        PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
        PACL existingDacl = nullptr;
        const DWORD getSecurityError = GetSecurityInfo(
            processHandle,
            SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &existingDacl,
            nullptr,
            &securityDescriptor);
        ScopedLocalMemory securityDescriptorHolder(securityDescriptor);
        if (getSecurityError != ERROR_SUCCESS)
        {
            return LogAndReturnFailure(stepName, L"GetSecurityInfo(DumpProcessDacl)", getSecurityError);
        }

        LPWSTR sddlText = nullptr;
        if (!ConvertSecurityDescriptorToStringSecurityDescriptorW(
            securityDescriptor,
            SDDL_REVISION_1,
            DACL_SECURITY_INFORMATION,
            &sddlText,
            nullptr))
        {
            return LogAndReturnFailure(
                stepName,
                L"ConvertSecurityDescriptorToStringSecurityDescriptorW",
                GetLastError());
        }

        ScopedLocalMemory sddlHolder(sddlText);

        std::wstring text = L"pid=";
        text += std::to_wstring(processId);
        text += L" dacl=";
        text += sddlText != nullptr ? sddlText : L"(null)";
        LogHardeningInfo(stepName, text.c_str());
        return true;
    }

    bool VerifyProcessProtection(
        HANDLE processHandle,
        const ProcessProtection::ProtectionPolicy& policy,
        const wchar_t* stepName)
    {
        PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
        PACL existingDacl = nullptr;
        const DWORD getSecurityError = GetSecurityInfo(
            processHandle,
            SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &existingDacl,
            nullptr,
            &securityDescriptor);
        ScopedLocalMemory securityDescriptorHolder(securityDescriptor);
        if (getSecurityError != ERROR_SUCCESS)
        {
            return LogAndReturnFailure(stepName, L"GetSecurityInfo(verify)", getSecurityError);
        }

        WellKnownSid administratorsSid(WinBuiltinAdministratorsSid);
        WellKnownSid usersSid(WinBuiltinUsersSid);
        if (!administratorsSid.IsValid())
        {
            return LogAndReturnFailure(
                stepName,
                L"CreateWellKnownSid(WinBuiltinAdministratorsSid)",
                administratorsSid.LastError() != ERROR_SUCCESS ? administratorsSid.LastError() : ERROR_INVALID_SID);
        }

        if (!usersSid.IsValid())
        {
            return LogAndReturnFailure(
                stepName,
                L"CreateWellKnownSid(WinBuiltinUsersSid)",
                usersSid.LastError() != ERROR_SUCCESS ? usersSid.LastError() : ERROR_INVALID_SID);
        }

        if (!ContainsAceForSid(existingDacl, usersSid.Get(), true, PROCESS_TERMINATE))
        {
            LogHardeningWarningMessage(
                stepName,
                L"Verification failed: BUILTIN\\Users deny ACE for PROCESS_TERMINATE is missing.");
            return false;
        }

        if (policy.administratorsTerminationPolicy == ProcessProtection::AdministratorsTerminationPolicy::DenyTerminate
            && !ContainsAceForSid(existingDacl, administratorsSid.Get(), true, PROCESS_TERMINATE))
        {
            LogHardeningWarningMessage(
                stepName,
                L"Verification failed: BUILTIN\\Administrators deny ACE for PROCESS_TERMINATE is missing.");
            return false;
        }

        return true;
    }

    void LogTerminateProbeResult(DWORD processId, HANDLE probeHandle, const wchar_t* stepName)
    {
        if (probeHandle != nullptr)
        {
            CloseHandle(probeHandle);
            std::wstring text =
                L"OpenProcess(PROCESS_TERMINATE) from the current caller context succeeded for pid=";
            text += std::to_wstring(processId);
            text +=
                L". This probe runs in the current service context; success can be expected for SYSTEM "
                L"or any caller with SeDebugPrivilege and does not by itself prove that external admin "
                L"termination will be denied by DACL checks.";
            LogHardeningWarningMessage(stepName, text.c_str());
            return;
        }

        const DWORD errorCode = GetLastError();
        std::wstring text = L"OpenProcess(PROCESS_TERMINATE) from the current caller context was denied for pid=";
        text += std::to_wstring(processId);
        text += L". error=";
        text += std::to_wstring(errorCode);
        text += L" message=";
        text += FormatErrorMessage(errorCode);
        LogHardeningInfo(stepName, text.c_str());
    }

    bool BuildUpdatedDacl(
        const wchar_t* stepName,
        PACL existingDacl,
        const std::vector<AceSpec>& requiredAces,
        PACL& updatedDacl)
    {
        updatedDacl = nullptr;

        std::vector<EXPLICIT_ACCESSW> entriesToAdd;
        entriesToAdd.reserve(requiredAces.size());

        for (const AceSpec& aceSpec : requiredAces)
        {
            if (aceSpec.sid == nullptr || !IsValidSid(aceSpec.sid))
            {
                return LogAndReturnFailure(stepName, L"IsValidSid", ERROR_INVALID_SID);
            }

            const bool isDeny = aceSpec.accessMode == DENY_ACCESS;
            if (ContainsAceForSid(existingDacl, aceSpec.sid, isDeny, aceSpec.accessMask))
            {
                continue;
            }

            EXPLICIT_ACCESSW entry = {};
            entry.grfAccessPermissions = aceSpec.accessMask;
            entry.grfAccessMode = aceSpec.accessMode;
            entry.grfInheritance = aceSpec.inheritance;
            BuildTrusteeWithSidW(&entry.Trustee, aceSpec.sid);
            entriesToAdd.push_back(entry);
        }

        if (entriesToAdd.empty())
        {
            LogHardeningInfo(stepName, L"Required deny/allow ACEs are already present in the process DACL.");
            return true;
        }

        const DWORD setEntriesError = SetEntriesInAclW(
            static_cast<ULONG>(entriesToAdd.size()),
            entriesToAdd.data(),
            existingDacl,
            &updatedDacl);
        if (setEntriesError != ERROR_SUCCESS)
        {
            return LogAndReturnFailure(stepName, L"SetEntriesInAclW", setEntriesError);
        }

        std::wstring text = L"SetEntriesInAclW succeeded. aceCount=";
        text += std::to_wstring(entriesToAdd.size());
        LogHardeningInfo(stepName, text.c_str());
        return true;
    }

    bool ProtectProcessHandleWithPolicy(
        HANDLE processHandle,
        const ProcessProtection::ProtectionPolicy& policy,
        const wchar_t* stepName)
    {
        if (processHandle == nullptr || processHandle == INVALID_HANDLE_VALUE)
        {
            return LogAndReturnFailure(stepName, L"process handle validation", ERROR_INVALID_HANDLE);
        }

        ScopedHandle openedHandle;
        HANDLE securityHandle = nullptr;
        DWORD processId = 0;
        if (!AcquireProcessSecurityHandle(processHandle, stepName, openedHandle, securityHandle, processId))
        {
            return false;
        }

        PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
        PACL existingDacl = nullptr;
        const DWORD getSecurityError = GetSecurityInfo(
            securityHandle,
            SE_KERNEL_OBJECT,
            DACL_SECURITY_INFORMATION,
            nullptr,
            nullptr,
            &existingDacl,
            nullptr,
            &securityDescriptor);
        ScopedLocalMemory securityDescriptorHolder(securityDescriptor);
        if (getSecurityError != ERROR_SUCCESS)
        {
            return LogAndReturnFailure(stepName, L"GetSecurityInfo", getSecurityError);
        }
        LogHardeningInfo(stepName, L"GetSecurityInfo succeeded for the process object.");

        WellKnownSid systemSid(WinLocalSystemSid);
        WellKnownSid administratorsSid(WinBuiltinAdministratorsSid);
        WellKnownSid usersSid(WinBuiltinUsersSid);
        if (!ValidateWellKnownSids(stepName, systemSid, administratorsSid, usersSid))
        {
            return false;
        }

        std::vector<AceSpec> requiredAces;
        requiredAces.push_back(
            AceSpec{ systemSid.Get(), SET_ACCESS, PROCESS_ALL_ACCESS, NO_INHERITANCE });
        requiredAces.push_back(
            AceSpec{ usersSid.Get(), DENY_ACCESS, PROCESS_TERMINATE, NO_INHERITANCE });

        if (policy.administratorsTerminationPolicy
            == ProcessProtection::AdministratorsTerminationPolicy::DenyTerminate)
        {
            requiredAces.push_back(
                AceSpec{ administratorsSid.Get(), DENY_ACCESS, PROCESS_TERMINATE, NO_INHERITANCE });
        }

        PACL updatedDacl = nullptr;
        if (!BuildUpdatedDacl(stepName, existingDacl, requiredAces, updatedDacl))
        {
            return false;
        }

        if (updatedDacl != nullptr)
        {
            ScopedLocalMemory updatedDaclHolder(updatedDacl);
            const DWORD setSecurityError = SetSecurityInfo(
                securityHandle,
                SE_KERNEL_OBJECT,
                DACL_SECURITY_INFORMATION,
                nullptr,
                nullptr,
                static_cast<PACL>(updatedDaclHolder.get()),
                nullptr);
            if (setSecurityError != ERROR_SUCCESS)
            {
                return LogAndReturnFailure(stepName, L"SetSecurityInfo", setSecurityError);
            }

            LogHardeningInfo(stepName, L"SetSecurityInfo succeeded for the process object.");
        }

        if (!VerifyProcessProtection(securityHandle, policy, stepName))
        {
            return false;
        }

        if (!DumpProcessDaclInternal(securityHandle, stepName, processId))
        {
            return false;
        }

        std::wstring successMessage = L"Process hardening applied successfully. pid=";
        successMessage += std::to_wstring(processId);
        LogHardeningInfo(stepName, successMessage.c_str());

        // This probe is diagnostic only. Per OpenProcess documentation, callers with
        // SeDebugPrivilege can be granted access regardless of the process security descriptor.
        HANDLE terminateProbe = OpenProcess(PROCESS_TERMINATE, FALSE, processId);
        LogTerminateProbeResult(processId, terminateProbe, stepName);
        return true;
    }
}

void ProcessProtection::SetProtectionPolicy(const ProtectionPolicy& policy)
{
    g_policy = policy;
}

ProcessProtection::ProtectionPolicy ProcessProtection::GetProtectionPolicy()
{
    return g_policy;
}

bool ProcessProtection::ProtectProcessFromTermination(HANDLE processHandle)
{
    return ProtectProcessHandleWithPolicy(processHandle, g_policy, L"ProtectProcessFromTermination");
}

bool ProcessProtection::HardenProcessSecurity()
{
    ScopedHandle selfProcessHandle(OpenProcess(
        READ_CONTROL | WRITE_DAC | PROCESS_QUERY_LIMITED_INFORMATION,
        FALSE,
        GetCurrentProcessId()));
    if (!selfProcessHandle)
    {
        return LogAndReturnFailure(L"HardenProcessSecurity", L"OpenProcess(self)", GetLastError());
    }

    return ProtectProcessHandleWithPolicy(selfProcessHandle.get(), g_policy, L"HardenProcessSecurity");
}

bool ProcessProtection::HardenServiceSecurity()
{
    const ProtectionPolicy policy = g_policy;

    ScopedScHandle scmHandle(OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT));
    if (!scmHandle)
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"OpenSCManagerW", GetLastError());
    }

    ScopedScHandle serviceHandle(OpenServiceW(
        scmHandle.get(),
        ServiceUtils::kTrayServiceName,
        READ_CONTROL | WRITE_DAC));
    if (!serviceHandle)
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"OpenServiceW", GetLastError());
    }

    DWORD descriptorSize = 0;
    if (QueryServiceObjectSecurity(
        serviceHandle.get(),
        DACL_SECURITY_INFORMATION,
        nullptr,
        0,
        &descriptorSize))
    {
        LogHardeningWarningMessage(
            L"HardenServiceSecurity",
            L"QueryServiceObjectSecurity returned success for a size probe unexpectedly.");
        return false;
    }

    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || descriptorSize == 0)
    {
        return LogAndReturnFailure(
            L"HardenServiceSecurity",
            L"QueryServiceObjectSecurity(size probe)",
            GetLastError());
    }

    std::vector<BYTE> descriptorBuffer(descriptorSize, 0);
    if (!QueryServiceObjectSecurity(
        serviceHandle.get(),
        DACL_SECURITY_INFORMATION,
        reinterpret_cast<PSECURITY_DESCRIPTOR>(descriptorBuffer.data()),
        descriptorSize,
        &descriptorSize))
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"QueryServiceObjectSecurity", GetLastError());
    }

    PACL existingDacl = nullptr;
    BOOL daclPresent = FALSE;
    BOOL daclDefaulted = FALSE;
    if (!GetSecurityDescriptorDacl(
        reinterpret_cast<PSECURITY_DESCRIPTOR>(descriptorBuffer.data()),
        &daclPresent,
        &existingDacl,
        &daclDefaulted))
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"GetSecurityDescriptorDacl", GetLastError());
    }

    if (!daclPresent)
    {
        existingDacl = nullptr;
    }

    WellKnownSid systemSid(WinLocalSystemSid);
    WellKnownSid administratorsSid(WinBuiltinAdministratorsSid);
    WellKnownSid usersSid(WinBuiltinUsersSid);
    if (!ValidateWellKnownSids(L"HardenServiceSecurity", systemSid, administratorsSid, usersSid))
    {
        return false;
    }

    std::vector<AceSpec> requiredAces;
    requiredAces.push_back(
        AceSpec{ systemSid.Get(), SET_ACCESS, SERVICE_ALL_ACCESS, NO_INHERITANCE });
    requiredAces.push_back(
        AceSpec{ usersSid.Get(), DENY_ACCESS, SERVICE_STOP, NO_INHERITANCE });

    DWORD administratorsDenyMask = 0;
    if (policy.denyServiceStopForAdministrators)
    {
        administratorsDenyMask |= SERVICE_STOP;
    }

    if (administratorsDenyMask != 0)
    {
        requiredAces.push_back(
            AceSpec{ administratorsSid.Get(), DENY_ACCESS, administratorsDenyMask, NO_INHERITANCE });
    }

    PACL updatedDacl = nullptr;
    if (!BuildUpdatedDacl(L"HardenServiceSecurity", existingDacl, requiredAces, updatedDacl))
    {
        return false;
    }

    if (updatedDacl == nullptr)
    {
        return true;
    }

    ScopedLocalMemory updatedDaclHolder(updatedDacl);

    SECURITY_DESCRIPTOR updatedSecurityDescriptor = {};
    if (!InitializeSecurityDescriptor(&updatedSecurityDescriptor, SECURITY_DESCRIPTOR_REVISION))
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"InitializeSecurityDescriptor", GetLastError());
    }

    if (!SetSecurityDescriptorDacl(
        &updatedSecurityDescriptor,
        TRUE,
        static_cast<PACL>(updatedDaclHolder.get()),
        FALSE))
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"SetSecurityDescriptorDacl", GetLastError());
    }

    if (!SetServiceObjectSecurity(
        serviceHandle.get(),
        DACL_SECURITY_INFORMATION,
        &updatedSecurityDescriptor))
    {
        return LogAndReturnFailure(L"HardenServiceSecurity", L"SetServiceObjectSecurity", GetLastError());
    }

    return true;
}

bool ProcessProtection::DumpProcessDacl(HANDLE processHandle, const wchar_t* context)
{
    ScopedHandle openedHandle;
    HANDLE securityHandle = nullptr;
    DWORD processId = 0;
    if (!AcquireProcessSecurityHandle(processHandle, context != nullptr ? context : L"DumpProcessDacl", openedHandle, securityHandle, processId))
    {
        return false;
    }

    return DumpProcessDaclInternal(securityHandle, context != nullptr ? context : L"DumpProcessDacl", processId);
}
