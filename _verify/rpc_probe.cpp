#include <iostream>
#include <string>

#include "../Shared/RpcClient.h"

int wmain(int argc, wchar_t** argv)
{
    std::wstring key = argc > 1 ? argv[1] : L"";
    std::wstring deviceName = argc > 2 ? argv[2] : L"PE8D";
    std::wstring deviceMac = argc > 3 ? argv[3] : L"00-1C-42-A5-5F-D9";

    RpcClient::AuthInfo auth = {};
    const auto authCode = RpcClient::GetCurrentAuthInfo(auth);
    std::wcout << L"GetCurrentAuthInfo status=" << static_cast<int>(authCode)
               << L" authenticated=" << (auth.authenticated ? 1 : 0)
               << L" username=" << auth.username << std::endl;

    RpcClient::LicenseInfo stateBefore = {};
    const auto beforeCode = RpcClient::GetLicenseState(1, deviceMac, stateBefore);
    std::wcout << L"GetLicenseState(before) status=" << static_cast<int>(beforeCode)
               << L" hasLicense=" << (stateBefore.hasLicense ? 1 : 0)
               << L" blocked=" << (stateBefore.blocked ? 1 : 0)
               << L" expired=" << (stateBefore.expired ? 1 : 0)
               << std::endl;

    if (!key.empty())
    {
        RpcClient::LicenseInfo activated = {};
        const auto activateCode = RpcClient::ActivateProduct(key, 1, deviceName, deviceMac, activated);
        std::wcout << L"ActivateProduct status=" << static_cast<int>(activateCode)
                   << L" hasLicense=" << (activated.hasLicense ? 1 : 0)
                   << L" blocked=" << (activated.blocked ? 1 : 0)
                   << L" expired=" << (activated.expired ? 1 : 0)
                   << std::endl;
    }

    RpcClient::LicenseInfo stateAfter = {};
    const auto afterCode = RpcClient::GetLicenseState(1, deviceMac, stateAfter);
    std::wcout << L"GetLicenseState(after) status=" << static_cast<int>(afterCode)
               << L" hasLicense=" << (stateAfter.hasLicense ? 1 : 0)
               << L" blocked=" << (stateAfter.blocked ? 1 : 0)
               << L" expired=" << (stateAfter.expired ? 1 : 0)
               << std::endl;

    return 0;
}
