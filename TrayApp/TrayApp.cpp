// TrayApp.cpp : Defines the entry point for the application.

#include "pch.h"
#include "framework.h"
#include "TrayApp.h"

#include "SingleInstanceGuard.h"
#include "TrayIconManager.h"
#include "UserIsolatedStopConfirmation.h"
#include "../Shared/ProcessUtils.h"
#include "../Shared/RpcClient.h"
#include "../Shared/ServiceUtils.h"

#include <commdlg.h>
#include <iphlpapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <ctime>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "Iphlpapi.lib")

#define MAX_LOADSTRING 100

namespace
{
    constexpr UINT kTrayIconCallbackMessage = WM_APP + 1;
    constexpr UINT kAvDatabaseRefreshCompletedMessage = WM_APP + 2;
    constexpr UINT kAvScanCompletedMessage = WM_APP + 3;
    constexpr UINT kTrayOpenCommandId = IDM_TRAY_OPEN;
    constexpr UINT_PTR kLicensePollTimerId = 1;
    constexpr UINT kLicensePollIntervalMs = 10000;
    constexpr DWORD kServiceStartTimeoutMs = 30000;

    constexpr long long kDefaultProductId = 1;
    constexpr wchar_t kFallbackDeviceName[] = L"TrayDevice";
    constexpr wchar_t kFallbackDeviceMac[] = L"00-00-00-00-00-00";

    constexpr int kControlAuthTitle = 4001;
    constexpr int kControlUserNameEdit = 4002;
    constexpr int kControlPasswordEdit = 4003;
    constexpr int kControlLoginButton = 4004;
    constexpr int kControlLogoutButton = 4005;
    constexpr int kControlActivationTitle = 4006;
    constexpr int kControlActivationEdit = 4007;
    constexpr int kControlActivationButton = 4008;
    constexpr int kControlUserStatus = 4009;
    constexpr int kControlLicenseStatus = 4010;
    constexpr int kControlAntivirusStatus = 4011;
    constexpr int kControlRefreshAvDatabase = 4012;
    constexpr int kControlInfoStatus = 4013;
    constexpr int kControlAvDatabaseStatus = 4014;
    constexpr int kControlScanFile = 4015;
    constexpr int kControlScanDirectory = 4016;
    constexpr int kControlScanResults = 4017;

    // Converts integer control id to HMENU for CreateWindowEx.
    HMENU ControlIdToMenu(int controlId)
    {
        return reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId));
    }

    struct UiControls
    {
        HWND userStatusLabel = nullptr;
        HWND licenseStatusLabel = nullptr;
        HWND antivirusStatusLabel = nullptr;
        HWND infoStatusLabel = nullptr;

        HWND authTitleLabel = nullptr;
        HWND usernameEdit = nullptr;
        HWND passwordEdit = nullptr;
        HWND loginButton = nullptr;
        HWND logoutButton = nullptr;

        HWND activationTitleLabel = nullptr;
        HWND activationEdit = nullptr;
        HWND activationButton = nullptr;

        HWND avDatabaseStatusLabel = nullptr;
        HWND refreshAvDatabaseButton = nullptr;
        HWND scanFileButton = nullptr;
        HWND scanDirectoryButton = nullptr;
        HWND scanResultsEdit = nullptr;
    };

    struct AppUiState
    {
        bool authenticated = false;
        RpcClient::AuthInfo authInfo = {};
        bool hasLicenseState = false;
        RpcClient::LicenseInfo licenseInfo = {};
        std::wstring deviceName = kFallbackDeviceName;
        std::wstring deviceMac = kFallbackDeviceMac;
        RpcClient::AvDatabaseInfo avDatabaseInfo = {};
        std::wstring scanResultText;
        std::wstring infoMessage;
        bool avOperationInProgress = false;
    };

    enum class AvScanOperation
    {
        File = 0,
        Directory = 1
    };

    struct AvDatabaseRefreshAsyncResult
    {
        RpcClient::RpcStatusCode status = RpcClient::RpcStatusCode::ServerError;
        RpcClient::AvDatabaseInfo databaseInfo = {};
        bool showErrors = false;
    };

    struct AvScanAsyncResult
    {
        AvScanOperation operation = AvScanOperation::File;
        RpcClient::RpcStatusCode scanStatus = RpcClient::RpcStatusCode::ServerError;
        RpcClient::AvFileScanResult fileResult = {};
        RpcClient::AvDirectoryScanResult directoryResult = {};
        RpcClient::RpcStatusCode databaseStatus = RpcClient::RpcStatusCode::ServerError;
        RpcClient::AvDatabaseInfo databaseInfo = {};
    };

    UiControls g_controls = {};
    AppUiState g_appState = {};

    // Returns the full path to a sibling binary near the current executable.
    std::wstring GetSiblingBinaryPath(const wchar_t* binaryName)
    {
        if (binaryName == nullptr || binaryName[0] == L'\0')
        {
            return {};
        }

        wchar_t modulePath[MAX_PATH] = {};
        if (GetModuleFileNameW(nullptr, modulePath, ARRAYSIZE(modulePath)) == 0)
        {
            return {};
        }

        std::wstring siblingPath = modulePath;
        const size_t separatorPos = siblingPath.find_last_of(L"\\/");
        if (separatorPos == std::wstring::npos)
        {
            return {};
        }

        siblingPath.erase(separatorPos + 1);
        siblingPath += binaryName;
        return siblingPath;
    }

    // Checks whether the current process was launched by the service process.
    bool IsCurrentProcessChildOfService(const SERVICE_STATUS_PROCESS& serviceStatus)
    {
        if (serviceStatus.dwCurrentState != SERVICE_RUNNING || serviceStatus.dwProcessId == 0)
        {
            return false;
        }

        DWORD parentProcessId = 0;
        if (!ProcessUtils::GetParentProcessId(GetCurrentProcessId(), parentProcessId))
        {
            return false;
        }

        return parentProcessId == serviceStatus.dwProcessId;
    }

    // Checks whether the service is currently running.
    bool IsServiceRunning()
    {
        SERVICE_STATUS_PROCESS serviceStatus = {};
        return ServiceUtils::QueryServiceStatus(ServiceUtils::kTrayServiceName, serviceStatus, nullptr)
            && serviceStatus.dwCurrentState == SERVICE_RUNNING;
    }

    // Reads best-effort MAC address from local network adapters.
    std::wstring GetDeviceMac()
    {
        ULONG bufferLength = 0;
        if (GetAdaptersInfo(nullptr, &bufferLength) != ERROR_BUFFER_OVERFLOW || bufferLength == 0)
        {
            return kFallbackDeviceMac;
        }

        std::vector<BYTE> buffer(bufferLength);
        auto* adapters = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
        if (GetAdaptersInfo(adapters, &bufferLength) != ERROR_SUCCESS)
        {
            return kFallbackDeviceMac;
        }

        std::wstring fallbackMac;
        for (PIP_ADAPTER_INFO adapter = adapters; adapter != nullptr; adapter = adapter->Next)
        {
            if (adapter->AddressLength < 6)
            {
                continue;
            }

            bool allZero = true;
            for (ULONG index = 0; index < adapter->AddressLength; ++index)
            {
                if (adapter->Address[index] != 0)
                {
                    allZero = false;
                    break;
                }
            }
            if (allZero)
            {
                continue;
            }

            wchar_t macBuffer[32] = {};
            swprintf_s(
                macBuffer,
                ARRAYSIZE(macBuffer),
                L"%02X-%02X-%02X-%02X-%02X-%02X",
                static_cast<unsigned int>(adapter->Address[0]),
                static_cast<unsigned int>(adapter->Address[1]),
                static_cast<unsigned int>(adapter->Address[2]),
                static_cast<unsigned int>(adapter->Address[3]),
                static_cast<unsigned int>(adapter->Address[4]),
                static_cast<unsigned int>(adapter->Address[5]));

            if (fallbackMac.empty())
            {
                fallbackMac = macBuffer;
            }

            if (adapter->Type != MIB_IF_TYPE_LOOPBACK)
            {
                return macBuffer;
            }
        }

        return fallbackMac.empty() ? std::wstring(kFallbackDeviceMac) : fallbackMac;
    }

    // Retrieves a best-effort machine name for license activation payload.
    std::wstring GetDeviceName()
    {
        wchar_t nameBuffer[256] = {};
        DWORD nameLength = ARRAYSIZE(nameBuffer);
        if (GetComputerNameExW(ComputerNamePhysicalDnsHostname, nameBuffer, &nameLength) && nameLength > 0)
        {
            return nameBuffer;
        }

        nameLength = ARRAYSIZE(nameBuffer);
        if (GetComputerNameW(nameBuffer, &nameLength) && nameLength > 0)
        {
            return nameBuffer;
        }

        return kFallbackDeviceName;
    }

    // Reads text from an edit control.
    std::wstring ReadControlText(HWND controlHandle)
    {
        if (controlHandle == nullptr)
        {
            return {};
        }

        const int textLength = GetWindowTextLengthW(controlHandle);
        if (textLength <= 0)
        {
            return {};
        }

        std::wstring text(static_cast<size_t>(textLength) + 1, L'\0');
        const int copiedLength = GetWindowTextW(controlHandle, text.data(), textLength + 1);
        if (copiedLength <= 0)
        {
            return {};
        }

        text.resize(static_cast<size_t>(copiedLength));
        return text;
    }

    // Converts RPC status to a short human-readable text.
    std::wstring RpcStatusToText(RpcClient::RpcStatusCode code)
    {
        switch (code)
        {
        case RpcClient::RpcStatusCode::Ok:
            return L"OK";
        case RpcClient::RpcStatusCode::InvalidArgument:
            return L"INVALID_ARGUMENT";
        case RpcClient::RpcStatusCode::NotAuthenticated:
            return L"NOT_AUTHENTICATED";
        case RpcClient::RpcStatusCode::AuthFailed:
            return L"AUTH_FAILED";
        case RpcClient::RpcStatusCode::NetworkError:
            return L"NETWORK_ERROR";
        case RpcClient::RpcStatusCode::ServerError:
            return L"SERVER_ERROR";
        case RpcClient::RpcStatusCode::NoLicense:
            return L"NO_LICENSE";
        case RpcClient::RpcStatusCode::LicenseExpired:
            return L"LICENSE_EXPIRED";
        case RpcClient::RpcStatusCode::LicenseBlocked:
            return L"LICENSE_BLOCKED";
        case RpcClient::RpcStatusCode::TransportError:
            return L"TRANSPORT_ERROR";
        default:
            return L"UNKNOWN_ERROR";
        }
    }

    // Formats optional license expiration date in UTC.
    std::wstring FormatExpirationDateUtc(
        const std::optional<std::chrono::system_clock::time_point>& expirationDateUtc)
    {
        if (!expirationDateUtc.has_value())
        {
            return L"n/a";
        }

        const std::time_t rawTime = std::chrono::system_clock::to_time_t(expirationDateUtc.value());
        std::tm utcTime = {};
        if (gmtime_s(&utcTime, &rawTime) != 0)
        {
            return L"n/a";
        }

        wchar_t text[64] = {};
        if (wcsftime(text, ARRAYSIZE(text), L"%Y-%m-%d %H:%M:%S UTC", &utcTime) == 0)
        {
            return L"n/a";
        }

        return text;
    }

    // Converts antivirus scan verdict to display text.
    std::wstring AvVerdictToText(RpcClient::AvScanVerdict verdict)
    {
        switch (verdict)
        {
        case RpcClient::AvScanVerdict::Infected:
            return L"infected";
        case RpcClient::AvScanVerdict::Error:
            return L"error";
        case RpcClient::AvScanVerdict::Clean:
        default:
            return L"clean";
        }
    }

    // Converts antivirus object type to display text.
    std::wstring AvObjectTypeToText(RpcClient::AvObjectType objectType)
    {
        switch (objectType)
        {
        case RpcClient::AvObjectType::Pe:
            return L"PE";
        case RpcClient::AvObjectType::ScriptText:
            return L"Script/Text";
        case RpcClient::AvObjectType::Unknown:
        default:
            return L"Unknown";
        }
    }

    // Converts antivirus database load status to display text.
    std::wstring AvDatabaseLoadStatusToText(RpcClient::AvDatabaseLoadStatus status)
    {
        return status == RpcClient::AvDatabaseLoadStatus::Loaded ? L"loaded" : L"not loaded";
    }

    // Formats one file scan result for the multiline result view.
    std::wstring FormatFileScanResult(const RpcClient::AvFileScanResult& result)
    {
        std::wstringstream text;
        text << L"Result: " << AvVerdictToText(result.verdict) << L"\r\n";
        text << L"Path: " << result.path << L"\r\n";
        text << L"Object type: " << AvObjectTypeToText(result.objectType) << L"\r\n";
        if (result.verdict == RpcClient::AvScanVerdict::Infected)
        {
            text << L"Threat/signature: " << (result.recordId.empty() ? L"<unknown>" : result.recordId) << L"\r\n";
            text << L"Signature hash: "
                << (result.objectSignatureHex.empty() ? L"<empty>" : result.objectSignatureHex) << L"\r\n";
            text << L"Detection offset: " << result.detectionOffset << L"\r\n";
        }
        if (!result.message.empty())
        {
            text << L"Message: " << result.message << L"\r\n";
        }

        return text.str();
    }

    // Formats directory scan summary and per-file results.
    std::wstring FormatDirectoryScanResult(const RpcClient::AvDirectoryScanResult& result)
    {
        std::wstringstream text;
        text << L"Directory: " << result.path << L"\r\n";
        text << L"Total scanned: " << result.totalScanned << L"\r\n";
        text << L"Infected: " << result.infectedCount << L"\r\n";
        text << L"Errors: " << result.errorCount << L"\r\n";
        if (result.truncated)
        {
            text << L"Displayed file results are truncated by RPC response size.\r\n";
        }
        if (!result.message.empty())
        {
            text << L"Message: " << result.message << L"\r\n";
        }

        text << L"\r\nFiles:\r\n";
        for (const RpcClient::AvFileScanResult& fileResult : result.results)
        {
            text << L"- " << AvVerdictToText(fileResult.verdict)
                << L" | " << AvObjectTypeToText(fileResult.objectType)
                << L" | " << fileResult.path;
            if (!fileResult.recordId.empty())
            {
                text << L" | " << fileResult.recordId;
            }
            if (!fileResult.message.empty())
            {
                text << L" | " << fileResult.message;
            }
            text << L"\r\n";
        }

        return text.str();
    }

    // Opens a file picker and returns the selected path.
    bool SelectFileForScan(HWND ownerWindow, std::wstring& selectedPath)
    {
        wchar_t fileName[MAX_PATH] = {};
        OPENFILENAMEW openFileName = {};
        openFileName.lStructSize = sizeof(openFileName);
        openFileName.hwndOwner = ownerWindow;
        openFileName.lpstrFile = fileName;
        openFileName.nMaxFile = ARRAYSIZE(fileName);
        openFileName.lpstrTitle = L"Select file to scan";
        openFileName.lpstrFilter = L"All files\0*.*\0";
        openFileName.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER;

        if (!GetOpenFileNameW(&openFileName))
        {
            return false;
        }

        selectedPath = fileName;
        return true;
    }

    // Opens a folder picker and returns the selected path.
    bool SelectDirectoryForScan(HWND ownerWindow, std::wstring& selectedPath)
    {
        BROWSEINFOW browseInfo = {};
        browseInfo.hwndOwner = ownerWindow;
        browseInfo.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
        browseInfo.lpszTitle = L"Select directory to scan";

        PIDLIST_ABSOLUTE itemList = SHBrowseForFolderW(&browseInfo);
        if (itemList == nullptr)
        {
            return false;
        }

        wchar_t path[MAX_PATH] = {};
        const BOOL converted = SHGetPathFromIDListW(itemList, path);
        CoTaskMemFree(itemList);
        if (!converted)
        {
            return false;
        }

        selectedPath = path;
        return true;
    }

    // Clears local user and license state in GUI memory.
    void ClearUiAuthAndLicenseState()
    {
        g_appState.authenticated = false;
        g_appState.authInfo = {};
        g_appState.hasLicenseState = false;
        g_appState.licenseInfo = {};
    }

    // Returns true when antivirus functionality must be enabled.
    bool IsAntivirusEnabledByState()
    {
        if (!g_appState.authenticated || !g_appState.hasLicenseState)
        {
            return false;
        }

        const RpcClient::LicenseInfo& license = g_appState.licenseInfo;
        return license.hasLicense && !license.blocked && !license.expired;
    }

    // Starts an async RPC request for antivirus database metadata.
    void StartAvDatabaseRefreshAsync(HWND hWnd, bool showErrors)
    {
        std::thread([hWnd, showErrors]()
        {
            auto* result = new AvDatabaseRefreshAsyncResult();
            result->showErrors = showErrors;
            result->status = RpcClient::GetAvDatabaseInfo(result->databaseInfo);
            if (!PostMessageW(hWnd, kAvDatabaseRefreshCompletedMessage, 0, reinterpret_cast<LPARAM>(result)))
            {
                delete result;
            }
        }).detach();
    }

    // Starts an async RPC scan request for one selected file.
    void StartFileScanAsync(HWND hWnd, const std::wstring& filePath)
    {
        std::thread([hWnd, filePath]()
        {
            auto* result = new AvScanAsyncResult();
            result->operation = AvScanOperation::File;
            result->scanStatus = RpcClient::ScanFile(filePath, result->fileResult);
            result->databaseStatus = RpcClient::GetAvDatabaseInfo(result->databaseInfo);
            if (!PostMessageW(hWnd, kAvScanCompletedMessage, 0, reinterpret_cast<LPARAM>(result)))
            {
                delete result;
            }
        }).detach();
    }

    // Starts an async RPC scan request for one selected directory.
    void StartDirectoryScanAsync(HWND hWnd, const std::wstring& directoryPath)
    {
        std::thread([hWnd, directoryPath]()
        {
            auto* result = new AvScanAsyncResult();
            result->operation = AvScanOperation::Directory;
            result->scanStatus = RpcClient::ScanDirectory(directoryPath, result->directoryResult);
            result->databaseStatus = RpcClient::GetAvDatabaseInfo(result->databaseInfo);
            if (!PostMessageW(hWnd, kAvScanCompletedMessage, 0, reinterpret_cast<LPARAM>(result)))
            {
                delete result;
            }
        }).detach();
    }
}

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
bool g_isExitRequested = false;
TrayIconManager g_trayIconManager;
SingleInstanceGuard g_singleInstanceGuard;
HWND g_mainWindowHandle = nullptr;

// Registers the main window class.
ATOM RegisterMainWindowClass(HINSTANCE hInstance);

// Creates the main window and tray icon.
BOOL CreateMainWindowAndTray(HINSTANCE hInstance, int nCmdShow, bool startInBackgroundMode);

// Creates all runtime controls for auth/license UI.
void CreateRuntimeControls(HWND hWnd);

// Arranges runtime controls according to current client size.
void LayoutRuntimeControls(HWND hWnd);

// Applies current auth/license state to visible controls.
void ApplyUiState(HWND hWnd);

// Refreshes current user state from RPC at startup.
void InitializeAuthStateFromRpc(HWND hWnd);

// Refreshes license state from RPC.
RpcClient::RpcStatusCode RefreshLicenseStateFromRpc(HWND hWnd, bool showNetworkErrors);

// Refreshes antivirus database state from RPC.
void RefreshAvDatabaseInfoFromRpc(HWND hWnd, bool showErrors);

// Handles refresh-database button action.
void HandleRefreshAvDatabaseAction(HWND hWnd);

// Handles login button action.
void HandleLoginAction(HWND hWnd);

// Handles logout button action.
void HandleLogoutAction(HWND hWnd);

// Handles activation button action.
void HandleActivationAction(HWND hWnd);

// Handles scan-file button action.
void HandleScanFileAction(HWND hWnd);

// Handles scan-directory button action.
void HandleScanDirectoryAction(HWND hWnd);

// Applies async database refresh result to GUI state.
void HandleAvDatabaseRefreshCompleted(HWND hWnd, LPARAM lParam);

// Applies async scan result to GUI state.
void HandleAvScanCompleted(HWND hWnd, LPARAM lParam);

// Processes all main window messages.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Processes tray icon callback message.
LRESULT HandleTrayIconMessage(HWND hWnd, LPARAM lParam);

// Handles about dialog messages.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

// Shows and activates the main window.
void ShowMainWindowFromTray(HWND hWnd);

// Hides the main window and keeps app in tray mode.
void HideMainWindowToTray(HWND hWnd);

// Parses startup arguments and checks background mode.
bool ShouldStartInBackground(const wchar_t* commandLine);

// Ensures startup policy related to Windows service.
bool ShouldTerminateAtStartup();

// Requests service stop over RPC.
bool TryStopServiceByRpc(HWND ownerWindow);

// Entry point that starts Win32 loop.
int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR lpCmdLine,
                     _In_ int nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);

    if (ShouldTerminateAtStartup())
    {
        return 0;
    }

    if (!g_singleInstanceGuard.TryLockForCurrentUser(L"TrayAppSingleInstanceMutex"))
    {
        return 0;
    }

    const bool startInBackgroundMode = ShouldStartInBackground(lpCmdLine);

    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_TRAYAPP, szWindowClass, MAX_LOADSTRING);
    RegisterMainWindowClass(hInstance);

    if (!CreateMainWindowAndTray(hInstance, nCmdShow, startInBackgroundMode))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_TRAYAPP));
    MSG msg;

    while (GetMessage(&msg, nullptr, 0, 0))
    {
        if (g_mainWindowHandle != nullptr && IsDialogMessageW(g_mainWindowHandle, &msg))
        {
            continue;
        }

        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return static_cast<int>(msg.wParam);
}

// Registers the main window class.
ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYAPP));
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(IDC_TRAYAPP);
    windowClass.lpszClassName = szWindowClass;
    windowClass.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&windowClass);
}

// Creates the main window and tray icon.
BOOL CreateMainWindowAndTray(HINSTANCE hInstance, int nCmdShow, bool startInBackgroundMode)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        0,
        920,
        700,
        nullptr,
        nullptr,
        hInstance,
        nullptr);
    if (hWnd == nullptr)
    {
        return FALSE;
    }

    g_mainWindowHandle = hWnd;

    g_trayIconManager.Initialize(hWnd, hInstance, kTrayIconCallbackMessage, IDI_TRAYAPP, L"TrayApp");
    if (!g_trayIconManager.AddIconToTray())
    {
        DestroyWindow(hWnd);
        return FALSE;
    }

    if (!startInBackgroundMode)
    {
        ShowWindow(hWnd, nCmdShow);
        UpdateWindow(hWnd);
    }

    return TRUE;
}

// Creates all runtime controls for auth/license UI.
void CreateRuntimeControls(HWND hWnd)
{
    const DWORD staticStyle = WS_CHILD | WS_VISIBLE;
    const DWORD editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP;
    const DWORD buttonStyle = WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP;
    const DWORD multilineReadOnlyEditStyle =
        WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY;

    g_controls.userStatusLabel = CreateWindowExW(
        0, L"STATIC", L"User: not authenticated", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlUserStatus), hInst, nullptr);
    g_controls.licenseStatusLabel = CreateWindowExW(
        0, L"STATIC", L"License: unknown", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlLicenseStatus), hInst, nullptr);
    g_controls.antivirusStatusLabel = CreateWindowExW(
        0, L"STATIC", L"Antivirus: blocked", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlAntivirusStatus), hInst, nullptr);
    g_controls.infoStatusLabel = CreateWindowExW(
        0, L"STATIC", L"", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlInfoStatus), hInst, nullptr);

    g_controls.authTitleLabel = CreateWindowExW(
        0, L"STATIC", L"Authentication", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlAuthTitle), hInst, nullptr);
    g_controls.usernameEdit = CreateWindowExW(
        0, L"EDIT", L"", editStyle | WS_GROUP, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlUserNameEdit), hInst, nullptr);
    g_controls.passwordEdit = CreateWindowExW(
        0, L"EDIT", L"", editStyle | ES_PASSWORD, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlPasswordEdit), hInst, nullptr);
    g_controls.loginButton = CreateWindowExW(
        0, L"BUTTON", L"Login", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlLoginButton), hInst, nullptr);
    g_controls.logoutButton = CreateWindowExW(
        0, L"BUTTON", L"Logout", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlLogoutButton), hInst, nullptr);

    g_controls.activationTitleLabel = CreateWindowExW(
        0, L"STATIC", L"Activation", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlActivationTitle), hInst, nullptr);
    g_controls.activationEdit = CreateWindowExW(
        0, L"EDIT", L"", editStyle | WS_GROUP, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlActivationEdit), hInst, nullptr);
    g_controls.activationButton = CreateWindowExW(
        0, L"BUTTON", L"Activate", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlActivationButton), hInst, nullptr);

    g_controls.avDatabaseStatusLabel = CreateWindowExW(
        0, L"STATIC", L"AV database: unknown", staticStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlAvDatabaseStatus), hInst, nullptr);
    g_controls.refreshAvDatabaseButton = CreateWindowExW(
        0, L"BUTTON", L"Refresh database info", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlRefreshAvDatabase), hInst, nullptr);
    g_controls.scanFileButton = CreateWindowExW(
        0, L"BUTTON", L"Scan file", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlScanFile), hInst, nullptr);
    g_controls.scanDirectoryButton = CreateWindowExW(
        0, L"BUTTON", L"Scan directory", buttonStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlScanDirectory), hInst, nullptr);
    g_controls.scanResultsEdit = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"EDIT", L"", multilineReadOnlyEditStyle, 0, 0, 0, 0, hWnd,
        ControlIdToMenu(kControlScanResults), hInst, nullptr);
}

// Arranges runtime controls according to current client size.
void LayoutRuntimeControls(HWND hWnd)
{
    RECT clientRect = {};
    GetClientRect(hWnd, &clientRect);

    const int left = 16;
    const int top = 16;
    const int right = clientRect.right - 16;
    const int width = right - left;
    const int rowHeight = 24;
    const int blockSpacing = 10;

    int y = top;
    MoveWindow(g_controls.userStatusLabel, left, y, width, rowHeight, TRUE);
    y += rowHeight + 4;
    MoveWindow(g_controls.licenseStatusLabel, left, y, width, rowHeight, TRUE);
    y += rowHeight + 4;
    MoveWindow(g_controls.antivirusStatusLabel, left, y, width, rowHeight, TRUE);
    y += rowHeight + blockSpacing;
    MoveWindow(g_controls.infoStatusLabel, left, y, width, rowHeight * 2, TRUE);
    y += rowHeight * 2 + blockSpacing;

    MoveWindow(g_controls.authTitleLabel, left, y, width, rowHeight, TRUE);
    y += rowHeight + 4;
    MoveWindow(g_controls.usernameEdit, left, y, width / 2, rowHeight, TRUE);
    y += rowHeight + 4;
    MoveWindow(g_controls.passwordEdit, left, y, width / 2, rowHeight, TRUE);
    MoveWindow(g_controls.loginButton, left + width / 2 + 8, y, 140, rowHeight, TRUE);
    MoveWindow(g_controls.logoutButton, left + width / 2 + 160, y, 140, rowHeight, TRUE);
    y += rowHeight + blockSpacing;

    MoveWindow(g_controls.activationTitleLabel, left, y, width, rowHeight, TRUE);
    y += rowHeight + 4;
    MoveWindow(g_controls.activationEdit, left, y, width / 2, rowHeight, TRUE);
    MoveWindow(g_controls.activationButton, left + width / 2 + 8, y, 140, rowHeight, TRUE);
    y += rowHeight + blockSpacing;

    MoveWindow(g_controls.avDatabaseStatusLabel, left, y, width, rowHeight * 2, TRUE);
    y += rowHeight * 2 + 4;
    MoveWindow(g_controls.refreshAvDatabaseButton, left, y, 180, rowHeight, TRUE);
    MoveWindow(g_controls.scanFileButton, left + 188, y, 120, rowHeight, TRUE);
    MoveWindow(g_controls.scanDirectoryButton, left + 316, y, 150, rowHeight, TRUE);
    y += rowHeight + blockSpacing;

    const int resultHeight = (std::max)(rowHeight * 5, static_cast<int>(clientRect.bottom) - y - 16);
    MoveWindow(g_controls.scanResultsEdit, left, y, width, resultHeight, TRUE);
}

// Applies current auth/license state to visible controls.
void ApplyUiState(HWND hWnd)
{
    UNREFERENCED_PARAMETER(hWnd);

    if (g_appState.authenticated)
    {
        std::wstring userLine = L"User: ";
        if (!g_appState.authInfo.username.empty())
        {
            userLine += g_appState.authInfo.username;
        }
        else
        {
            userLine += L"<unknown>";
        }
        if (g_appState.authInfo.userId.has_value())
        {
            userLine += L" (id=" + std::to_wstring(g_appState.authInfo.userId.value()) + L")";
        }
        SetWindowTextW(g_controls.userStatusLabel, userLine.c_str());
    }
    else
    {
        SetWindowTextW(g_controls.userStatusLabel, L"User: not authenticated");
    }

    std::wstring licenseLine = L"License: ";
    if (!g_appState.authenticated)
    {
        licenseLine += L"authentication required";
    }
    else if (!g_appState.hasLicenseState || !g_appState.licenseInfo.hasLicense)
    {
        licenseLine += L"not found";
    }
    else if (g_appState.licenseInfo.blocked)
    {
        licenseLine += L"blocked";
    }
    else if (g_appState.licenseInfo.expired)
    {
        licenseLine += L"expired";
    }
    else
    {
        licenseLine += L"active";
    }

    if (g_appState.hasLicenseState && g_appState.licenseInfo.expirationDateUtc.has_value())
    {
        licenseLine += L", expiration: ";
        licenseLine += FormatExpirationDateUtc(g_appState.licenseInfo.expirationDateUtc);
    }
    SetWindowTextW(g_controls.licenseStatusLabel, licenseLine.c_str());

    const bool antivirusEnabled = IsAntivirusEnabledByState();
    SetWindowTextW(
        g_controls.antivirusStatusLabel,
        antivirusEnabled ? L"Antivirus: enabled" : L"Antivirus: blocked");

    std::wstring databaseLine = L"AV database: ";
    databaseLine += AvDatabaseLoadStatusToText(g_appState.avDatabaseInfo.loadStatus);
    databaseLine += L", records: ";
    databaseLine += std::to_wstring(g_appState.avDatabaseInfo.recordCount);
    databaseLine += L", released: ";
    databaseLine += FormatExpirationDateUtc(g_appState.avDatabaseInfo.releaseDateUtc);
    databaseLine += L", source: ";
    databaseLine += g_appState.avDatabaseInfo.source.empty() ? L"unknown" : g_appState.avDatabaseInfo.source;
    databaseLine += L", loaded: ";
    databaseLine += FormatExpirationDateUtc(g_appState.avDatabaseInfo.lastSuccessfulLoadUtc);
    if (!g_appState.avDatabaseInfo.lastUpdateStatus.empty())
    {
        databaseLine += L", update: ";
        databaseLine += g_appState.avDatabaseInfo.lastUpdateStatus;
    }
    SetWindowTextW(g_controls.avDatabaseStatusLabel, databaseLine.c_str());

    SetWindowTextW(g_controls.scanResultsEdit, g_appState.scanResultText.c_str());

    SetWindowTextW(g_controls.infoStatusLabel, g_appState.infoMessage.c_str());

    const BOOL avButtonEnabled = antivirusEnabled && !g_appState.avOperationInProgress ? TRUE : FALSE;
    EnableWindow(g_controls.refreshAvDatabaseButton, avButtonEnabled);
    EnableWindow(g_controls.scanFileButton, avButtonEnabled);
    EnableWindow(g_controls.scanDirectoryButton, avButtonEnabled);

    const bool showLoginSection = !g_appState.authenticated;
    ShowWindow(g_controls.authTitleLabel, showLoginSection ? SW_SHOW : SW_HIDE);
    ShowWindow(g_controls.usernameEdit, showLoginSection ? SW_SHOW : SW_HIDE);
    ShowWindow(g_controls.passwordEdit, showLoginSection ? SW_SHOW : SW_HIDE);
    ShowWindow(g_controls.loginButton, showLoginSection ? SW_SHOW : SW_HIDE);

    ShowWindow(g_controls.logoutButton, g_appState.authenticated ? SW_SHOW : SW_HIDE);

    const bool showActivationSection =
        g_appState.authenticated
        && (!g_appState.hasLicenseState || !g_appState.licenseInfo.hasLicense);
    ShowWindow(g_controls.activationTitleLabel, showActivationSection ? SW_SHOW : SW_HIDE);
    ShowWindow(g_controls.activationEdit, showActivationSection ? SW_SHOW : SW_HIDE);
    ShowWindow(g_controls.activationButton, showActivationSection ? SW_SHOW : SW_HIDE);
}
// Refreshes current user state from RPC at startup.
void InitializeAuthStateFromRpc(HWND hWnd)
{
    g_appState.deviceName = GetDeviceName();
    g_appState.deviceMac = GetDeviceMac();
    g_appState.infoMessage = L"Device: " + g_appState.deviceName + L", mac: " + g_appState.deviceMac;

    RpcClient::AuthInfo authInfo = {};
    const RpcClient::RpcStatusCode authStatus = RpcClient::GetCurrentAuthInfo(authInfo);
    if (authStatus == RpcClient::RpcStatusCode::Ok && authInfo.authenticated)
    {
        g_appState.authenticated = true;
        g_appState.authInfo = authInfo;
        RefreshLicenseStateFromRpc(hWnd, false);
    }
    else
    {
        ClearUiAuthAndLicenseState();
        if (authStatus != RpcClient::RpcStatusCode::Ok
            && authStatus != RpcClient::RpcStatusCode::NotAuthenticated)
        {
            g_appState.infoMessage = L"Auth state request error: " + RpcStatusToText(authStatus)
                + L". Device: " + g_appState.deviceName + L", mac: " + g_appState.deviceMac;
        }
    }

    ApplyUiState(hWnd);
    RefreshAvDatabaseInfoFromRpc(hWnd, false);
}

// Refreshes license state from RPC.
RpcClient::RpcStatusCode RefreshLicenseStateFromRpc(HWND hWnd, bool showNetworkErrors)
{
    if (!g_appState.authenticated)
    {
        g_appState.hasLicenseState = false;
        g_appState.licenseInfo = {};
        ApplyUiState(hWnd);
        return RpcClient::RpcStatusCode::NotAuthenticated;
    }

    RpcClient::LicenseInfo licenseInfo = {};
    const RpcClient::RpcStatusCode status = RpcClient::GetLicenseState(
        kDefaultProductId,
        g_appState.deviceMac,
        licenseInfo);

    g_appState.hasLicenseState = true;
    g_appState.licenseInfo = licenseInfo;

    if (status == RpcClient::RpcStatusCode::NotAuthenticated)
    {
        ClearUiAuthAndLicenseState();
        g_appState.infoMessage = L"Session expired. Login is required.";
    }
    else if (status == RpcClient::RpcStatusCode::NoLicense)
    {
        g_appState.infoMessage = L"No license for current user/device.";
    }
    else if (status == RpcClient::RpcStatusCode::LicenseExpired)
    {
        g_appState.infoMessage = L"License is expired.";
    }
    else if (status == RpcClient::RpcStatusCode::LicenseBlocked)
    {
        g_appState.infoMessage = L"License is blocked.";
    }
    else if (status == RpcClient::RpcStatusCode::Ok)
    {
        g_appState.infoMessage.clear();
    }
    else if (showNetworkErrors)
    {
        g_appState.infoMessage = L"License request error: " + RpcStatusToText(status);
    }

    ApplyUiState(hWnd);
    return status;
}

// Refreshes antivirus database state from RPC.
void RefreshAvDatabaseInfoFromRpc(HWND hWnd, bool showErrors)
{
    RpcClient::AvDatabaseInfo databaseInfo = {};
    const RpcClient::RpcStatusCode status = RpcClient::GetAvDatabaseInfo(databaseInfo);
    if (status == RpcClient::RpcStatusCode::Ok)
    {
        g_appState.avDatabaseInfo = databaseInfo;
    }
    else if (showErrors)
    {
        g_appState.infoMessage = L"AV database info request error: " + RpcStatusToText(status);
        MessageBoxW(hWnd, g_appState.infoMessage.c_str(), L"TrayApp", MB_OK | MB_ICONERROR);
    }

    ApplyUiState(hWnd);
}

// Handles refresh-database button action.
void HandleRefreshAvDatabaseAction(HWND hWnd)
{
    if (!IsAntivirusEnabledByState())
    {
        g_appState.infoMessage = L"Authentication and an active license are required.";
        ApplyUiState(hWnd);
        return;
    }

    if (g_appState.avOperationInProgress)
    {
        return;
    }

    g_appState.avOperationInProgress = true;
    g_appState.infoMessage = L"Refreshing AV database info...";
    ApplyUiState(hWnd);
    StartAvDatabaseRefreshAsync(hWnd, true);
}

// Handles login button action.
void HandleLoginAction(HWND hWnd)
{
    const std::wstring username = ReadControlText(g_controls.usernameEdit);
    const std::wstring password = ReadControlText(g_controls.passwordEdit);
    if (username.empty() || password.empty())
    {
        g_appState.infoMessage = L"Enter login and password.";
        ApplyUiState(hWnd);
        return;
    }

    RpcClient::AuthInfo authInfo = {};
    const RpcClient::RpcStatusCode status = RpcClient::Login(username, password, authInfo);
    if (status == RpcClient::RpcStatusCode::Ok && authInfo.authenticated)
    {
        g_appState.authenticated = true;
        g_appState.authInfo = authInfo;
        g_appState.infoMessage.clear();
        SetWindowTextW(g_controls.passwordEdit, L"");
        RefreshLicenseStateFromRpc(hWnd, true);
        return;
    }

    ClearUiAuthAndLicenseState();
    g_appState.infoMessage = L"Authentication failed: " + RpcStatusToText(status);
    ApplyUiState(hWnd);
    SetFocus(g_controls.usernameEdit);
    MessageBoxW(hWnd, g_appState.infoMessage.c_str(), L"TrayApp", MB_OK | MB_ICONERROR);
}

// Handles logout button action.
void HandleLogoutAction(HWND hWnd)
{
    const RpcClient::RpcStatusCode status = RpcClient::Logout();
    ClearUiAuthAndLicenseState();
    SetWindowTextW(g_controls.passwordEdit, L"");
    SetWindowTextW(g_controls.activationEdit, L"");
    g_appState.infoMessage.clear();

    if (status != RpcClient::RpcStatusCode::Ok)
    {
        g_appState.infoMessage = L"Logout RPC returned: " + RpcStatusToText(status);
    }

    ApplyUiState(hWnd);
    SetFocus(g_controls.usernameEdit);
}

// Handles activation button action.
void HandleActivationAction(HWND hWnd)
{
    if (!g_appState.authenticated)
    {
        g_appState.infoMessage = L"Authentication is required.";
        ApplyUiState(hWnd);
        return;
    }

    const std::wstring activationKey = ReadControlText(g_controls.activationEdit);
    if (activationKey.empty())
    {
        g_appState.infoMessage = L"Enter activation code.";
        ApplyUiState(hWnd);
        return;
    }

    RpcClient::LicenseInfo licenseInfo = {};
    const RpcClient::RpcStatusCode status = RpcClient::ActivateProduct(
        activationKey,
        kDefaultProductId,
        g_appState.deviceName,
        g_appState.deviceMac,
        licenseInfo);

    g_appState.hasLicenseState = true;
    g_appState.licenseInfo = licenseInfo;

    if (status == RpcClient::RpcStatusCode::Ok && licenseInfo.hasLicense
        && !licenseInfo.expired && !licenseInfo.blocked)
    {
        g_appState.infoMessage = L"License activated.";
        SetWindowTextW(g_controls.activationEdit, L"");
        RefreshAvDatabaseInfoFromRpc(hWnd, false);
    }
    else
    {
        g_appState.infoMessage = L"Activation failed: " + RpcStatusToText(status)
            + L". Device: " + g_appState.deviceName + L", mac: " + g_appState.deviceMac;
        SetFocus(g_controls.activationEdit);
        MessageBoxW(hWnd, g_appState.infoMessage.c_str(), L"TrayApp", MB_OK | MB_ICONERROR);
    }

    ApplyUiState(hWnd);
}

// Handles scan-file button action.
void HandleScanFileAction(HWND hWnd)
{
    if (!IsAntivirusEnabledByState())
    {
        g_appState.infoMessage = L"Authentication and an active license are required.";
        ApplyUiState(hWnd);
        return;
    }

    if (g_appState.avOperationInProgress)
    {
        return;
    }

    std::wstring filePath;
    if (!SelectFileForScan(hWnd, filePath))
    {
        return;
    }

    g_appState.avOperationInProgress = true;
    g_appState.infoMessage = L"Scanning file...";
    g_appState.scanResultText = L"Scanning file:\r\n" + filePath;
    ApplyUiState(hWnd);
    StartFileScanAsync(hWnd, filePath);
}

// Handles scan-directory button action.
void HandleScanDirectoryAction(HWND hWnd)
{
    if (!IsAntivirusEnabledByState())
    {
        g_appState.infoMessage = L"Authentication and an active license are required.";
        ApplyUiState(hWnd);
        return;
    }

    if (g_appState.avOperationInProgress)
    {
        return;
    }

    std::wstring directoryPath;
    if (!SelectDirectoryForScan(hWnd, directoryPath))
    {
        return;
    }

    g_appState.avOperationInProgress = true;
    g_appState.infoMessage = L"Scanning directory...";
    g_appState.scanResultText = L"Scanning directory:\r\n" + directoryPath;
    ApplyUiState(hWnd);
    StartDirectoryScanAsync(hWnd, directoryPath);
}

// Applies async database refresh result to GUI state.
void HandleAvDatabaseRefreshCompleted(HWND hWnd, LPARAM lParam)
{
    auto* rawResult = reinterpret_cast<AvDatabaseRefreshAsyncResult*>(lParam);
    if (rawResult == nullptr)
    {
        return;
    }

    const AvDatabaseRefreshAsyncResult result = *rawResult;
    delete rawResult;

    g_appState.avOperationInProgress = false;
    if (result.status == RpcClient::RpcStatusCode::Ok)
    {
        g_appState.avDatabaseInfo = result.databaseInfo;
        g_appState.infoMessage = L"AV database info refreshed.";
    }
    else if (result.showErrors)
    {
        g_appState.infoMessage = L"AV database info request error: " + RpcStatusToText(result.status);
        MessageBoxW(hWnd, g_appState.infoMessage.c_str(), L"TrayApp", MB_OK | MB_ICONERROR);
    }

    ApplyUiState(hWnd);
}

// Applies async scan result to GUI state.
void HandleAvScanCompleted(HWND hWnd, LPARAM lParam)
{
    auto* rawResult = reinterpret_cast<AvScanAsyncResult*>(lParam);
    if (rawResult == nullptr)
    {
        return;
    }

    const AvScanAsyncResult result = *rawResult;
    delete rawResult;

    g_appState.avOperationInProgress = false;
    if (result.databaseStatus == RpcClient::RpcStatusCode::Ok)
    {
        g_appState.avDatabaseInfo = result.databaseInfo;
    }

    if (result.scanStatus != RpcClient::RpcStatusCode::Ok)
    {
        g_appState.infoMessage = result.operation == AvScanOperation::File
            ? L"ScanFile RPC error: "
            : L"ScanDirectory RPC error: ";
        g_appState.infoMessage += RpcStatusToText(result.scanStatus);
        g_appState.scanResultText = g_appState.infoMessage;
        MessageBoxW(hWnd, g_appState.infoMessage.c_str(), L"TrayApp", MB_OK | MB_ICONERROR);
    }
    else if (result.operation == AvScanOperation::File)
    {
        g_appState.infoMessage = L"File scan completed.";
        g_appState.scanResultText = FormatFileScanResult(result.fileResult);
    }
    else
    {
        g_appState.infoMessage = L"Directory scan completed.";
        g_appState.scanResultText = FormatDirectoryScanResult(result.directoryResult);
    }

    ApplyUiState(hWnd);
}

// Processes all main window messages.
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == g_trayIconManager.GetTaskbarCreatedMessage())
    {
        g_trayIconManager.AddIconToTray();
        return 0;
    }

    if (message == kTrayIconCallbackMessage)
    {
        return HandleTrayIconMessage(hWnd, lParam);
    }

    if (message == kAvDatabaseRefreshCompletedMessage)
    {
        HandleAvDatabaseRefreshCompleted(hWnd, lParam);
        return 0;
    }

    if (message == kAvScanCompletedMessage)
    {
        HandleAvScanCompleted(hWnd, lParam);
        return 0;
    }

    switch (message)
    {
    case WM_CREATE:
        CreateRuntimeControls(hWnd);
        LayoutRuntimeControls(hWnd);
        InitializeAuthStateFromRpc(hWnd);
        if (!g_appState.authenticated)
        {
            SetFocus(g_controls.usernameEdit);
        }
        SetTimer(hWnd, kLicensePollTimerId, kLicensePollIntervalMs, nullptr);
        return 0;

    case WM_SIZE:
        LayoutRuntimeControls(hWnd);
        return 0;

    case WM_TIMER:
        if (wParam == kLicensePollTimerId && g_appState.authenticated)
        {
            RefreshLicenseStateFromRpc(hWnd, false);
        }
        return 0;

    case WM_COMMAND:
    {
        const int commandId = LOWORD(wParam);
        switch (commandId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            return 0;
        case kTrayOpenCommandId:
            ShowMainWindowFromTray(hWnd);
            return 0;
        case IDM_EXIT:
            if (TryStopServiceByRpc(hWnd) || !IsServiceRunning())
            {
                g_isExitRequested = true;
                DestroyWindow(hWnd);
            }
            return 0;
        case kControlLoginButton:
            HandleLoginAction(hWnd);
            return 0;
        case kControlLogoutButton:
            HandleLogoutAction(hWnd);
            return 0;
        case kControlActivationButton:
            HandleActivationAction(hWnd);
            return 0;
        case kControlRefreshAvDatabase:
            HandleRefreshAvDatabaseAction(hWnd);
            return 0;
        case kControlScanFile:
            HandleScanFileAction(hWnd);
            return 0;
        case kControlScanDirectory:
            HandleScanDirectoryAction(hWnd);
            return 0;
        default:
            return DefWindowProc(hWnd, message, wParam, lParam);
        }
    }

    case WM_CLOSE:
        if (!g_isExitRequested)
        {
            HideMainWindowToTray(hWnd);
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hWnd, kLicensePollTimerId);
        g_trayIconManager.RemoveIconFromTray();
        g_mainWindowHandle = nullptr;
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Processes tray icon callback message.
LRESULT HandleTrayIconMessage(HWND hWnd, LPARAM lParam)
{
    const UINT trayEventCode = LOWORD(static_cast<DWORD_PTR>(lParam));
    switch (trayEventCode)
    {
    case NIN_SELECT:
    case NIN_KEYSELECT:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
        ShowMainWindowFromTray(hWnd);
        return 0;
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_CONTEXTMENU:
        g_trayIconManager.ShowTrayContextMenu(kTrayOpenCommandId, IDM_EXIT);
        return 0;
    default:
        return 0;
    }
}

// Shows and activates the main window.
void ShowMainWindowFromTray(HWND hWnd)
{
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
}

// Hides the main window and keeps app in tray mode.
void HideMainWindowToTray(HWND hWnd)
{
    ShowWindow(hWnd, SW_HIDE);
}

// Parses startup arguments and checks background mode.
bool ShouldStartInBackground(const wchar_t* commandLine)
{
    if (commandLine == nullptr)
    {
        return false;
    }

    return wcsstr(commandLine, L"/background") != nullptr
        || wcsstr(commandLine, L"-background") != nullptr
        || wcsstr(commandLine, L"/tray") != nullptr
        || wcsstr(commandLine, L"-tray") != nullptr;
}

// Ensures startup policy related to Windows service.
bool ShouldTerminateAtStartup()
{
    SERVICE_STATUS_PROCESS serviceStatus = {};
    DWORD queryError = ERROR_SUCCESS;
    if (!ServiceUtils::QueryServiceStatus(ServiceUtils::kTrayServiceName, serviceStatus, &queryError))
    {
        if (queryError == ERROR_SERVICE_DOES_NOT_EXIST)
        {
            const std::wstring serviceBinaryPath = GetSiblingBinaryPath(L"TrayService.exe");
            if (!serviceBinaryPath.empty())
            {
                ServiceUtils::EnsureServiceInstalled(
                    ServiceUtils::kTrayServiceName,
                    serviceBinaryPath.c_str(),
                    nullptr);
            }
        }

        ServiceUtils::StartServiceAndWaitRunning(ServiceUtils::kTrayServiceName, kServiceStartTimeoutMs);
        return true;
    }

    if (serviceStatus.dwCurrentState != SERVICE_RUNNING)
    {
        ServiceUtils::StartServiceAndWaitRunning(ServiceUtils::kTrayServiceName, kServiceStartTimeoutMs);
        return true;
    }

    return !IsCurrentProcessChildOfService(serviceStatus);
}

// Requests service stop over RPC.
bool TryStopServiceByRpc(HWND ownerWindow)
{
    const RpcClient::StopRequestResult stopResult = RpcClient::RequestServiceStop();
    if (stopResult == RpcClient::StopRequestResult::Approved)
    {
        return true;
    }

    if (stopResult == RpcClient::StopRequestResult::Rejected)
    {
        MessageBoxW(
            ownerWindow,
            L"Service stop request was rejected in isolated desktop confirmation.",
            L"TrayApp",
            MB_OK | MB_ICONINFORMATION);
        return false;
    }

    if (stopResult == RpcClient::StopRequestResult::ConfirmationRequired)
    {
        const StopConfirmationResult confirmationResult = ShowStopConfirmationOnIsolatedDesktop();
        if (confirmationResult == StopConfirmationResult::Rejected)
        {
            return false;
        }

        if (confirmationResult == StopConfirmationResult::Failed)
        {
            MessageBoxW(
                ownerWindow,
                L"Could not start isolated desktop confirmation in the current user session.",
                L"TrayApp",
                MB_OK | MB_ICONERROR);
            return false;
        }

        const RpcClient::StopRequestResult confirmResult = RpcClient::ConfirmServiceStop();
        if (confirmResult == RpcClient::StopRequestResult::Approved)
        {
            return true;
        }

        if (!IsServiceRunning())
        {
            return true;
        }

        MessageBoxW(
            ownerWindow,
            L"Service stop was approved locally, but TrayApp could not send the final stop confirmation to TrayService.",
            L"TrayApp",
            MB_OK | MB_ICONERROR);
        return false;
    }

    if (stopResult == RpcClient::StopRequestResult::Failed)
    {
        MessageBoxW(
            ownerWindow,
            L"TrayService received the stop request, but confirmation could not be started.",
            L"TrayApp",
            MB_OK | MB_ICONERROR);
        return false;
    }

    if (!IsServiceRunning())
    {
        return false;
    }

    MessageBoxW(
        ownerWindow,
        L"Could not connect to the TrayService RPC endpoint.",
        L"TrayApp",
        MB_OK | MB_ICONERROR);
    return false;
}

// Handles about dialog messages.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return static_cast<INT_PTR>(TRUE);
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return static_cast<INT_PTR>(TRUE);
        }
        break;
    }

    return static_cast<INT_PTR>(FALSE);
}

