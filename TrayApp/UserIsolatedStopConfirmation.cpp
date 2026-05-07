#include "pch.h"

#include "UserIsolatedStopConfirmation.h"

#include "WinApiLog.h"

#include <atomic>
#include <cstdint>
#include <cwchar>
#include <string>

namespace
{
    constexpr wchar_t kConfirmationWindowClassName[] = L"TrayAppIsolatedStopConfirmWindow";
    constexpr int kApproveButtonId = 1001;
    constexpr int kRejectButtonId = 1002;
    constexpr DWORD kUiReadyTimeoutMs = 5000;
    constexpr DWORD kUserResponseTimeoutMs = 60000;
    constexpr DWORD kUiThreadExitTimeoutMs = 5000;

    constexpr int kWindowWidth = 600;
    constexpr int kWindowHeight = 260;
    constexpr int kMargin = 24;
    constexpr int kButtonWidth = 160;
    constexpr int kButtonHeight = 38;
    constexpr int kButtonGap = 16;

    constexpr ACCESS_MASK kIsolatedDesktopAccess =
        DESKTOP_CREATEWINDOW
        | DESKTOP_READOBJECTS
        | DESKTOP_WRITEOBJECTS
        | DESKTOP_SWITCHDESKTOP;

    constexpr ACCESS_MASK kDefaultDesktopAccess =
        DESKTOP_READOBJECTS
        | DESKTOP_WRITEOBJECTS
        | DESKTOP_SWITCHDESKTOP;

    class ScopedHandle final
    {
    public:
        ScopedHandle() = default;

        explicit ScopedHandle(HANDLE handle)
            : m_handle(handle)
        {
        }

        ~ScopedHandle()
        {
            Reset();
        }

        ScopedHandle(const ScopedHandle&) = delete;
        ScopedHandle& operator=(const ScopedHandle&) = delete;

        HANDLE Get() const
        {
            return m_handle;
        }

        void Reset(HANDLE handle = nullptr)
        {
            if (m_handle != nullptr)
            {
                CloseHandle(m_handle);
            }

            m_handle = handle;
        }

        bool IsValid() const
        {
            return m_handle != nullptr && m_handle != INVALID_HANDLE_VALUE;
        }

    private:
        HANDLE m_handle = nullptr;
    };

    class ScopedDesktop final
    {
    public:
        ScopedDesktop() = default;

        explicit ScopedDesktop(HDESK desktop)
            : m_desktop(desktop)
        {
        }

        ~ScopedDesktop()
        {
            Reset();
        }

        ScopedDesktop(const ScopedDesktop&) = delete;
        ScopedDesktop& operator=(const ScopedDesktop&) = delete;

        HDESK Get() const
        {
            return m_desktop;
        }

        void Reset(HDESK desktop = nullptr)
        {
            if (m_desktop != nullptr)
            {
                if (!CloseDesktop(m_desktop))
                {
                    WinApiLog::LogLastError(L"CloseDesktop");
                }
                else
                {
                    WinApiLog::LogInfo(L"CloseDesktop result: success.");
                }
            }

            m_desktop = desktop;
        }

        bool IsValid() const
        {
            return m_desktop != nullptr;
        }

    private:
        HDESK m_desktop = nullptr;
    };

    struct ConfirmationWindowState
    {
        StopConfirmationResult result = StopConfirmationResult::Rejected;
        bool childControlsCreated = false;
        bool completed = false;
    };

    struct UiThreadContext
    {
        HDESK isolatedDesktop = nullptr;
        HANDLE readyEvent = nullptr;
        HANDLE completedEvent = nullptr;
        HANDLE cancelEvent = nullptr;
        std::atomic<HWND> windowHandle = nullptr;
        std::wstring isolatedDesktopName;
        StopConfirmationResult result = StopConfirmationResult::Failed;
    };

    std::wstring BuildIsolatedDesktopName()
    {
        wchar_t buffer[128] = {};
        swprintf_s(
            buffer,
            ARRAYSIZE(buffer),
            L"TrayAppStopConfirm_%lu_%llu",
            GetCurrentProcessId(),
            static_cast<unsigned long long>(GetTickCount64()));
        return buffer;
    }

    void LogInfo(const wchar_t* message)
    {
        WinApiLog::LogInfo(message);
    }

    void LogBoolResult(const wchar_t* operation, BOOL result)
    {
        std::wstring text = operation != nullptr ? operation : L"(unknown operation)";
        text += L" result: ";
        text += result ? L"success." : L"failed.";
        WinApiLog::LogInfo(text.c_str());

        if (!result)
        {
            WinApiLog::LogLastError(operation);
        }
    }

    void LogHandleResult(const wchar_t* operation, const void* handle)
    {
        wchar_t buffer[256] = {};
        swprintf_s(
            buffer,
            ARRAYSIZE(buffer),
            L"%ls result: handle=0x%p.",
            operation != nullptr ? operation : L"(unknown operation)",
            handle);
        WinApiLog::LogInfo(buffer);

        if (handle == nullptr)
        {
            WinApiLog::LogLastError(operation);
        }
    }

    void LogAtomResult(const wchar_t* operation, ATOM atom)
    {
        wchar_t buffer[256] = {};
        swprintf_s(
            buffer,
            ARRAYSIZE(buffer),
            L"%ls result: atom=%u.",
            operation != nullptr ? operation : L"(unknown operation)",
            static_cast<unsigned int>(atom));
        WinApiLog::LogInfo(buffer);

        if (atom == 0)
        {
            WinApiLog::LogLastError(operation);
        }
    }

    void LogWindowResult(const wchar_t* operation, HWND windowHandle)
    {
        wchar_t buffer[256] = {};
        swprintf_s(
            buffer,
            ARRAYSIZE(buffer),
            L"%ls result: hwnd=0x%p.",
            operation != nullptr ? operation : L"CreateWindowExW",
            windowHandle);
        WinApiLog::LogInfo(buffer);

        if (windowHandle == nullptr)
        {
            WinApiLog::LogLastError(operation);
        }
    }

    bool LogThreadDesktopName(const std::wstring& expectedDesktopName)
    {
        HDESK threadDesktop = GetThreadDesktop(GetCurrentThreadId());
        LogHandleResult(L"GetThreadDesktop", threadDesktop);
        if (threadDesktop == nullptr)
        {
            return false;
        }

        DWORD requiredBytes = 0;
        SetLastError(ERROR_SUCCESS);
        GetUserObjectInformationW(threadDesktop, UOI_NAME, nullptr, 0, &requiredBytes);
        const DWORD probeError = GetLastError();
        if (requiredBytes == 0 && probeError != ERROR_INSUFFICIENT_BUFFER)
        {
            WinApiLog::LogLastError(L"GetUserObjectInformationW(UOI_NAME probe)");
            return false;
        }

        std::wstring desktopName;
        desktopName.resize(requiredBytes / sizeof(wchar_t));
        SetLastError(ERROR_SUCCESS);
        const BOOL gotName = GetUserObjectInformationW(
            threadDesktop,
            UOI_NAME,
            desktopName.data(),
            requiredBytes,
            &requiredBytes);
        LogBoolResult(L"GetUserObjectInformationW(UOI_NAME)", gotName);
        if (!gotName)
        {
            return false;
        }

        while (!desktopName.empty() && desktopName.back() == L'\0')
        {
            desktopName.pop_back();
        }

        std::wstring message = L"Thread desktop name: ";
        message += desktopName.empty() ? L"(empty)" : desktopName;
        message += L"; expected isolated desktop name: ";
        message += expectedDesktopName.empty() ? L"(empty)" : expectedDesktopName;
        message += L".";
        WinApiLog::LogInfo(message.c_str());

        if (desktopName != expectedDesktopName)
        {
            WinApiLog::LogInfo(L"Thread desktop verification failed: current desktop is not the isolated desktop.");
            return false;
        }

        return true;
    }

    bool RestoreDefaultDesktop(HDESK defaultDesktop)
    {
        if (defaultDesktop == nullptr)
        {
            WinApiLog::LogInfo(L"Default desktop restored: failed because handle is null.");
            return false;
        }

        const BOOL switched = SwitchDesktop(defaultDesktop);
        if (!switched)
        {
            WinApiLog::LogLastError(L"SwitchDesktop(Default)");
            WinApiLog::LogInfo(L"Default desktop restored: failed.");
            return false;
        }

        WinApiLog::LogInfo(L"Default desktop restored.");
        return true;
    }

    bool CompleteConfirmation(HWND windowHandle, StopConfirmationResult result)
    {
        auto* state = reinterpret_cast<ConfirmationWindowState*>(
            GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
        if (state == nullptr || state->completed)
        {
            return false;
        }

        state->completed = true;
        state->result = result;
        DestroyWindow(windowHandle);
        return true;
    }

    void ApplyDefaultFont(HWND controlHandle, HFONT font)
    {
        if (controlHandle != nullptr && font != nullptr)
        {
            SendMessageW(controlHandle, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
        }
    }

    bool CreateChildControls(HWND windowHandle)
    {
        RECT clientRect = {};
        GetClientRect(windowHandle, &clientRect);

        const int clientWidth = clientRect.right - clientRect.left;
        const int clientHeight = clientRect.bottom - clientRect.top;
        const int buttonTop = clientHeight - kMargin - kButtonHeight;
        const int buttonRowWidth = kButtonWidth * 2 + kButtonGap;
        const int approveLeft = (clientWidth - buttonRowWidth) / 2;
        const int rejectLeft = approveLeft + kButtonWidth + kButtonGap;
        const int textHeight = buttonTop - kMargin * 2;
        HFONT defaultFont = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
        HINSTANCE instance = GetModuleHandleW(nullptr);

        SetLastError(ERROR_SUCCESS);
        HWND textHandle = CreateWindowExW(
            0,
            L"STATIC",
            L"Confirm service stop?",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            kMargin,
            kMargin + 18,
            clientWidth - kMargin * 2,
            textHeight,
            windowHandle,
            nullptr,
            instance,
            nullptr);
        LogWindowResult(L"CreateWindowExW STATIC", textHandle);

        SetLastError(ERROR_SUCCESS);
        HWND approveButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Stop Service",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            approveLeft,
            buttonTop,
            kButtonWidth,
            kButtonHeight,
            windowHandle,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApproveButtonId)),
            instance,
            nullptr);
        LogWindowResult(L"CreateWindowExW BUTTON Stop", approveButton);

        SetLastError(ERROR_SUCCESS);
        HWND rejectButton = CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            rejectLeft,
            buttonTop,
            kButtonWidth,
            kButtonHeight,
            windowHandle,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectButtonId)),
            instance,
            nullptr);
        LogWindowResult(L"CreateWindowExW BUTTON Cancel", rejectButton);

        ApplyDefaultFont(textHandle, defaultFont);
        ApplyDefaultFont(approveButton, defaultFont);
        ApplyDefaultFont(rejectButton, defaultFont);

        const bool created = textHandle != nullptr && approveButton != nullptr && rejectButton != nullptr;
        WinApiLog::LogInfo(created ? L"child controls created." : L"child controls created: failed.");
        if (!created)
        {
            WinApiLog::LogInfo(L"Falling back to top-level WM_PAINT prompt because one or more child controls failed.");
        }

        if (approveButton != nullptr)
        {
            SetFocus(approveButton);
        }

        return created;
    }

    LRESULT CALLBACK ConfirmationWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* state = reinterpret_cast<ConfirmationWindowState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
            if (state != nullptr)
            {
                state->childControlsCreated = CreateChildControls(windowHandle);
            }
            return 0;
        }

        case WM_PAINT:
        {
            PAINTSTRUCT paint = {};
            HDC dc = BeginPaint(windowHandle, &paint);
            if (dc != nullptr)
            {
                RECT clientRect = {};
                GetClientRect(windowHandle, &clientRect);
                FillRect(dc, &clientRect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

                auto* state = reinterpret_cast<ConfirmationWindowState*>(
                    GetWindowLongPtrW(windowHandle, GWLP_USERDATA));
                if (state == nullptr || !state->childControlsCreated)
                {
                    HFONT font = reinterpret_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
                    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;
                    SetBkMode(dc, TRANSPARENT);
                    SetTextColor(dc, RGB(0, 0, 0));

                    RECT textRect = clientRect;
                    textRect.left += kMargin;
                    textRect.right -= kMargin;
                    textRect.top += 54;
                    textRect.bottom -= 54;
                    DrawTextW(
                        dc,
                        L"Confirm service stop?\r\n\r\nEnter = Stop Service    Esc = Cancel",
                        -1,
                        &textRect,
                        DT_CENTER | DT_VCENTER | DT_WORDBREAK);

                    if (oldFont != nullptr)
                    {
                        SelectObject(dc, oldFont);
                    }
                }
            }

            EndPaint(windowHandle, &paint);
            return 0;
        }

        case WM_COMMAND:
            if (LOWORD(wParam) == kApproveButtonId)
            {
                LogInfo(L"Confirm clicked.");
                CompleteConfirmation(windowHandle, StopConfirmationResult::Approved);
                return 0;
            }

            if (LOWORD(wParam) == kRejectButtonId)
            {
                LogInfo(L"Cancel clicked.");
                CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
                return 0;
            }
            return 0;

        case WM_KEYDOWN:
            if (wParam == VK_RETURN)
            {
                LogInfo(L"Confirm clicked.");
                CompleteConfirmation(windowHandle, StopConfirmationResult::Approved);
                return 0;
            }

            if (wParam == VK_ESCAPE)
            {
                LogInfo(L"Cancel clicked.");
                CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
                return 0;
            }
            break;

        case WM_CLOSE:
            LogInfo(L"WM_CLOSE.");
            CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        }

        return DefWindowProcW(windowHandle, message, wParam, lParam);
    }

    bool RegisterConfirmationWindowClass()
    {
        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = ConfirmationWindowProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        windowClass.lpszClassName = kConfirmationWindowClassName;
        windowClass.style = CS_HREDRAW | CS_VREDRAW;

        const ATOM atom = RegisterClassExW(&windowClass);
        if (atom != 0)
        {
            LogAtomResult(L"RegisterClassExW", atom);
            return true;
        }

        const DWORD error = GetLastError();
        if (error == ERROR_CLASS_ALREADY_EXISTS)
        {
            WinApiLog::LogInfo(L"RegisterClassExW result: class already exists.");
            return true;
        }

        LogAtomResult(L"RegisterClassExW", 0);
        return false;
    }

    HWND CreateConfirmationWindow(ConfirmationWindowState* state)
    {
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = screenWidth > kWindowWidth ? (screenWidth - kWindowWidth) / 2 : 0;
        const int y = screenHeight > kWindowHeight ? (screenHeight - kWindowHeight) / 2 : 0;

        SetLastError(ERROR_SUCCESS);
        HWND windowHandle = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_APPWINDOW,
            kConfirmationWindowClassName,
            L"TrayApp Stop Confirmation",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            x,
            y,
            kWindowWidth,
            kWindowHeight,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            state);
        LogWindowResult(L"CreateWindowExW top-level", windowHandle);
        return windowHandle;
    }

    void ShowConfirmationWindow(HWND windowHandle)
    {
        SetLastError(ERROR_SUCCESS);
        const BOOL wasVisible = ShowWindow(windowHandle, SW_SHOW);
        const DWORD showError = GetLastError();
        std::wstring showText = L"ShowWindow result: previousVisible=";
        showText += wasVisible ? L"true" : L"false";
        showText += L" visibleAfter=";
        showText += IsWindowVisible(windowHandle) ? L"true" : L"false";
        showText += L".";
        WinApiLog::LogInfo(showText.c_str());
        if (!IsWindowVisible(windowHandle))
        {
            WinApiLog::LogError(L"ShowWindow", showError);
        }

        RECT windowRect = {};
        GetWindowRect(windowHandle, &windowRect);
        const int width = windowRect.right - windowRect.left;
        const int height = windowRect.bottom - windowRect.top;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = screenWidth > width ? (screenWidth - width) / 2 : 0;
        const int y = screenHeight > height ? (screenHeight - height) / 2 : 0;

        const BOOL positioned = SetWindowPos(
            windowHandle,
            HWND_TOPMOST,
            x,
            y,
            width,
            height,
            SWP_SHOWWINDOW);
        LogBoolResult(L"SetWindowPos(HWND_TOPMOST, SWP_SHOWWINDOW)", positioned);

        const BOOL broughtToTop = BringWindowToTop(windowHandle);
        LogBoolResult(L"BringWindowToTop", broughtToTop);

        const BOOL foreground = SetForegroundWindow(windowHandle);
        LogBoolResult(L"SetForegroundWindow", foreground);
        SetActiveWindow(windowHandle);

        InvalidateRect(windowHandle, nullptr, TRUE);
        SetLastError(ERROR_SUCCESS);
        const BOOL updated = UpdateWindow(windowHandle);
        LogBoolResult(L"UpdateWindow", updated);
    }

    void HandleKeyboardShortcut(HWND windowHandle, const MSG& message, bool& handled)
    {
        handled = false;
        if (message.message != WM_KEYDOWN)
        {
            return;
        }

        if (message.wParam == VK_RETURN)
        {
            LogInfo(L"Confirm clicked.");
            CompleteConfirmation(windowHandle, StopConfirmationResult::Approved);
            handled = true;
            return;
        }

        if (message.wParam == VK_ESCAPE)
        {
            LogInfo(L"Cancel clicked.");
            CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
            handled = true;
        }
    }

    DWORD WINAPI ConfirmationUiThreadProc(LPVOID parameter)
    {
        WinApiLog::LogInfo(L"UI thread started.");

        auto* context = reinterpret_cast<UiThreadContext*>(parameter);
        if (context == nullptr
            || context->isolatedDesktop == nullptr
            || context->readyEvent == nullptr
            || context->completedEvent == nullptr
            || context->cancelEvent == nullptr)
        {
            WinApiLog::LogInfo(L"UI thread exited.");
            return 0;
        }

        const BOOL desktopSet = SetThreadDesktop(context->isolatedDesktop);
        LogBoolResult(L"SetThreadDesktop", desktopSet);
        if (!desktopSet)
        {
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            WinApiLog::LogInfo(L"UI thread exited.");
            return 0;
        }

        if (!LogThreadDesktopName(context->isolatedDesktopName))
        {
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            WinApiLog::LogInfo(L"UI thread exited.");
            return 0;
        }

        if (!RegisterConfirmationWindowClass())
        {
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            WinApiLog::LogInfo(L"UI thread exited.");
            return 0;
        }

        ConfirmationWindowState state = {};
        HWND windowHandle = CreateConfirmationWindow(&state);
        context->windowHandle.store(windowHandle, std::memory_order_release);
        if (windowHandle == nullptr)
        {
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            WinApiLog::LogInfo(L"UI thread exited.");
            return 0;
        }

        ShowConfirmationWindow(windowHandle);
        SetEvent(context->readyEvent);

        bool messageLoopFailed = false;
        bool quitReceived = false;
        const ULONGLONG deadline = GetTickCount64() + kUserResponseTimeoutMs;

        while (!quitReceived)
        {
            DWORD waitMs = 0;
            const ULONGLONG now = GetTickCount64();
            if (now >= deadline)
            {
                waitMs = 0;
            }
            else
            {
                waitMs = static_cast<DWORD>(deadline - now);
            }

            HANDLE waitHandles[] = { context->cancelEvent };
            const DWORD waitResult = MsgWaitForMultipleObjects(
                ARRAYSIZE(waitHandles),
                waitHandles,
                FALSE,
                waitMs,
                QS_ALLINPUT);

            if (waitResult == WAIT_OBJECT_0)
            {
                LogInfo(L"Cancel event received.");
                if (IsWindow(windowHandle))
                {
                    CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
                }
                break;
            }

            if (waitResult == WAIT_OBJECT_0 + ARRAYSIZE(waitHandles))
            {
                MSG message = {};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT)
                    {
                        quitReceived = true;
                        break;
                    }

                    bool handled = false;
                    HandleKeyboardShortcut(windowHandle, message, handled);
                    if (handled)
                    {
                        continue;
                    }

                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }
                continue;
            }

            if (waitResult == WAIT_TIMEOUT)
            {
                WinApiLog::LogInfo(L"timeout.");
                if (IsWindow(windowHandle))
                {
                    CompleteConfirmation(windowHandle, StopConfirmationResult::Rejected);
                }
                break;
            }

            messageLoopFailed = true;
            WinApiLog::LogLastError(L"MsgWaitForMultipleObjects");
            break;
        }

        if (messageLoopFailed)
        {
            context->result = StopConfirmationResult::Failed;
            if (IsWindow(windowHandle))
            {
                DestroyWindow(windowHandle);
            }
        }
        else
        {
            context->result = state.result;
        }

        context->windowHandle.store(nullptr, std::memory_order_release);
        SetEvent(context->completedEvent);
        WinApiLog::LogInfo(L"UI thread exited.");
        return 0;
    }

    bool CancelAndJoinUiThread(HANDLE cancelEvent, HANDLE threadHandle, HWND windowHandle)
    {
        if (cancelEvent != nullptr)
        {
            SetEvent(cancelEvent);
        }

        if (windowHandle != nullptr && IsWindow(windowHandle))
        {
            PostMessageW(windowHandle, WM_CLOSE, 0, 0);
        }

        if (threadHandle == nullptr)
        {
            return true;
        }

        const DWORD waitResult = WaitForSingleObject(threadHandle, kUiThreadExitTimeoutMs);
        if (waitResult == WAIT_OBJECT_0)
        {
            return true;
        }

        WinApiLog::LogError(
            L"WaitForSingleObject(UI thread exit)",
            waitResult == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
        WaitForSingleObject(threadHandle, INFINITE);
        return false;
    }
}

StopConfirmationResult ShowStopConfirmationOnIsolatedDesktop()
{
    ScopedDesktop defaultDesktop(OpenDesktopW(
        L"Default",
        0,
        FALSE,
        kDefaultDesktopAccess));
    LogHandleResult(L"OpenDesktopW(Default)", defaultDesktop.Get());
    if (!defaultDesktop.IsValid())
    {
        return StopConfirmationResult::Failed;
    }

    const std::wstring isolatedDesktopName = BuildIsolatedDesktopName();
    ScopedDesktop isolatedDesktop(CreateDesktopW(
        isolatedDesktopName.c_str(),
        nullptr,
        nullptr,
        0,
        kIsolatedDesktopAccess,
        nullptr));
    LogHandleResult(L"CreateDesktopW", isolatedDesktop.Get());
    if (!isolatedDesktop.IsValid())
    {
        RestoreDefaultDesktop(defaultDesktop.Get());
        return StopConfirmationResult::Failed;
    }

    ScopedHandle readyEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!readyEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(ready)");
        RestoreDefaultDesktop(defaultDesktop.Get());
        return StopConfirmationResult::Failed;
    }

    ScopedHandle completedEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completedEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(completed)");
        RestoreDefaultDesktop(defaultDesktop.Get());
        return StopConfirmationResult::Failed;
    }

    ScopedHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!cancelEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(cancel)");
        RestoreDefaultDesktop(defaultDesktop.Get());
        return StopConfirmationResult::Failed;
    }

    UiThreadContext context = {};
    context.isolatedDesktop = isolatedDesktop.Get();
    context.readyEvent = readyEvent.Get();
    context.completedEvent = completedEvent.Get();
    context.cancelEvent = cancelEvent.Get();
    context.isolatedDesktopName = isolatedDesktopName;
    context.result = StopConfirmationResult::Failed;

    ScopedHandle uiThread(CreateThread(
        nullptr,
        0,
        ConfirmationUiThreadProc,
        &context,
        0,
        nullptr));
    if (!uiThread.IsValid())
    {
        WinApiLog::LogLastError(L"CreateThread(confirmation UI)");
        RestoreDefaultDesktop(defaultDesktop.Get());
        return StopConfirmationResult::Failed;
    }

    const DWORD readyWait = WaitForSingleObject(readyEvent.Get(), kUiReadyTimeoutMs);
    if (readyWait != WAIT_OBJECT_0)
    {
        WinApiLog::LogInfo(L"timeout.");
        RestoreDefaultDesktop(defaultDesktop.Get());
        CancelAndJoinUiThread(
            cancelEvent.Get(),
            uiThread.Get(),
            context.windowHandle.load(std::memory_order_acquire));
        return StopConfirmationResult::Failed;
    }

    if (context.result == StopConfirmationResult::Failed
        && context.windowHandle.load(std::memory_order_acquire) == nullptr)
    {
        RestoreDefaultDesktop(defaultDesktop.Get());
        WaitForSingleObject(uiThread.Get(), INFINITE);
        return StopConfirmationResult::Failed;
    }

    const BOOL switchedToIsolated = SwitchDesktop(isolatedDesktop.Get());
    LogBoolResult(L"SwitchDesktop isolated", switchedToIsolated);
    if (!switchedToIsolated)
    {
        RestoreDefaultDesktop(defaultDesktop.Get());
        CancelAndJoinUiThread(
            cancelEvent.Get(),
            uiThread.Get(),
            context.windowHandle.load(std::memory_order_acquire));
        return StopConfirmationResult::Failed;
    }

    StopConfirmationResult result = StopConfirmationResult::Failed;
    const DWORD completedWait = WaitForSingleObject(completedEvent.Get(), kUserResponseTimeoutMs);
    if (completedWait == WAIT_OBJECT_0)
    {
        result = context.result;
    }
    else if (completedWait == WAIT_TIMEOUT)
    {
        WinApiLog::LogInfo(L"timeout.");
        result = StopConfirmationResult::Rejected;
        context.result = result;
        RestoreDefaultDesktop(defaultDesktop.Get());
        CancelAndJoinUiThread(
            cancelEvent.Get(),
            uiThread.Get(),
            context.windowHandle.load(std::memory_order_acquire));
        return result;
    }
    else
    {
        WinApiLog::LogLastError(L"WaitForSingleObject(completed)");
        result = StopConfirmationResult::Failed;
        context.result = result;
    }

    RestoreDefaultDesktop(defaultDesktop.Get());
    if (completedWait != WAIT_OBJECT_0)
    {
        CancelAndJoinUiThread(
            cancelEvent.Get(),
            uiThread.Get(),
            context.windowHandle.load(std::memory_order_acquire));
    }
    else
    {
        WaitForSingleObject(uiThread.Get(), INFINITE);
    }

    return result;
}
