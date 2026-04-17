// TrayApp.cpp : Defines the entry point for the application.

#include "pch.h"
#include "framework.h"
#include "TrayApp.h"

#include "SingleInstanceGuard.h"
#include "TrayIconManager.h"
#include "../Shared/ProcessUtils.h"
#include "../Shared/RpcClient.h"
#include "../Shared/ServiceUtils.h"

#include <string>

#define MAX_LOADSTRING 100

namespace
{
    constexpr UINT kTrayIconCallbackMessage = WM_APP + 1;
    constexpr UINT kTrayOpenCommandId = IDM_TRAY_OPEN;
    constexpr DWORD kServiceStartTimeoutMs = 30000;
    constexpr wchar_t kServiceParentProcessName[] = L"TrayService.exe";

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
}

HINSTANCE hInst;
WCHAR szTitle[MAX_LOADSTRING];
WCHAR szWindowClass[MAX_LOADSTRING];
bool g_isExitRequested = false;
TrayIconManager g_trayIconManager;
SingleInstanceGuard g_singleInstanceGuard;

// Регистрирует класс главного окна приложения.
ATOM RegisterMainWindowClass(HINSTANCE hInstance);

// Создает главное окно и добавляет иконку приложения в трей.
BOOL CreateMainWindowAndTray(HINSTANCE hInstance, int nCmdShow, bool startInBackgroundMode);

// Обрабатывает все сообщения главного окна.
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

// Обрабатывает сообщения от иконки в системном трее.
LRESULT HandleTrayIconMessage(HWND hWnd, LPARAM lParam);

// Обрабатывает диалоговое окно "О программе".
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

// Показывает главное окно и переносит его на передний план.
void ShowMainWindowFromTray(HWND hWnd);

// Скрывает главное окно, оставляя приложение работать в фоне.
void HideMainWindowToTray(HWND hWnd);

// Проверяет аргументы запуска и определяет режим фонового старта.
bool ShouldStartInBackground(const wchar_t* commandLine);

bool ShouldTerminateAtStartup();

bool TryStopServiceByRpc(HWND ownerWindow);

// Является точкой входа приложения и запускает главный цикл сообщений.
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

    bool startInBackgroundMode = ShouldStartInBackground(lpCmdLine);

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
        if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    return (int)msg.wParam;
}

// Регистрирует класс главного окна приложения.
ATOM RegisterMainWindowClass(HINSTANCE hInstance)
{
    WNDCLASSEXW windowClass = {};
    windowClass.cbSize = sizeof(WNDCLASSEXW);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = hInstance;
    windowClass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_TRAYAPP));
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    windowClass.lpszMenuName = MAKEINTRESOURCEW(IDC_TRAYAPP);
    windowClass.lpszClassName = szWindowClass;
    windowClass.hIconSm = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&windowClass);
}

// Создает главное окно и добавляет иконку приложения в трей.
BOOL CreateMainWindowAndTray(HINSTANCE hInstance, int nCmdShow, bool startInBackgroundMode)
{
    hInst = hInstance;

    HWND hWnd = CreateWindowW(
        szWindowClass,
        szTitle,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        0,
        CW_USEDEFAULT,
        0,
        nullptr,
        nullptr,
        hInstance,
        nullptr);

    if (hWnd == nullptr)
    {
        return FALSE;
    }

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

// Обрабатывает все сообщения главного окна.
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

    switch (message)
    {
    case WM_COMMAND:
    {
        int commandId = LOWORD(wParam);
        switch (commandId)
        {
        case IDM_ABOUT:
            DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
            return 0;
        case kTrayOpenCommandId:
            ShowMainWindowFromTray(hWnd);
            return 0;
        case IDM_EXIT:
            if (TryStopServiceByRpc(hWnd))
            {
                g_isExitRequested = true;
                DestroyWindow(hWnd);
            }
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
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);
        UNREFERENCED_PARAMETER(hdc);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        g_trayIconManager.RemoveIconFromTray();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProc(hWnd, message, wParam, lParam);
}

// Обрабатывает сообщения от иконки в системном трее.
LRESULT HandleTrayIconMessage(HWND hWnd, LPARAM lParam)
{
    UINT trayEventCode = LOWORD(static_cast<DWORD_PTR>(lParam));

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

// Показывает главное окно и переносит его на передний план.
void ShowMainWindowFromTray(HWND hWnd)
{
    ShowWindow(hWnd, SW_SHOW);
    ShowWindow(hWnd, SW_RESTORE);
    SetForegroundWindow(hWnd);
}

// Скрывает главное окно, оставляя приложение работать в фоне.
void HideMainWindowToTray(HWND hWnd)
{
    ShowWindow(hWnd, SW_HIDE);
}

// Проверяет аргументы запуска и определяет режим фонового старта.
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

    return !ProcessUtils::IsCurrentProcessParentNamed(kServiceParentProcessName);
}

bool TryStopServiceByRpc(HWND ownerWindow)
{
    if (RpcClient::RequestServiceStop())
    {
        return true;
    }

    MessageBoxW(
        ownerWindow,
        L"Не удалось отправить запрос остановки службы через RPC.",
        L"TrayApp",
        MB_OK | MB_ICONERROR);
    return false;
}

// Показывает окно "О программе".
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);

    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }

    return (INT_PTR)FALSE;
}
