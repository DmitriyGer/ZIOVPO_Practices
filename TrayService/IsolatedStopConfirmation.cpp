#include "IsolatedStopConfirmation.h"

#include "DesktopSession.h"
#include "WinApiLog.h"

#include <windows.h>

namespace
{
    constexpr wchar_t kConfirmationWindowClassName[] = L"TrayServiceIsolatedStopConfirmWindow";
    constexpr int kApproveButtonId = 1001;
    constexpr int kRejectButtonId = 1002;
    constexpr DWORD kUiReadyTimeoutMs = 5000;
    constexpr DWORD kConfirmationMaxWaitMs = 180000;

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

        ScopedHandle(ScopedHandle&& other) noexcept
            : m_handle(other.Release())
        {
        }

        ScopedHandle& operator=(ScopedHandle&& other) noexcept
        {
            if (this != &other)
            {
                Reset(other.Release());
            }

            return *this;
        }

        HANDLE Get() const
        {
            return m_handle;
        }

        HANDLE Release()
        {
            HANDLE detached = m_handle;
            m_handle = nullptr;
            return detached;
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

    struct ConfirmationWindowState
    {
        bool approved = false;
    };

    struct UiThreadContext
    {
        HDESK isolatedDesktop = nullptr;
        HANDLE readyEvent = nullptr;
        HANDLE completedEvent = nullptr;
        HANDLE cancelEvent = nullptr;
        StopConfirmationResult result = StopConfirmationResult::Failed;
    };

    void BuildConfirmationLayout(HWND windowHandle)
    {
        RECT windowRect = {};
        GetClientRect(windowHandle, &windowRect);

        const int width = windowRect.right - windowRect.left;
        const int height = windowRect.bottom - windowRect.top;

        constexpr int margin = 24;
        constexpr int buttonWidth = 160;
        constexpr int buttonHeight = 36;
        const int buttonTop = height - margin - buttonHeight;
        const int textHeight = buttonTop - margin * 2;

        CreateWindowExW(
            0,
            L"STATIC",
            // This is an isolated desktop prompt, not the real UAC secure desktop.
            L"TrayService stop was requested.\r\n\r\n"
            L"This confirmation is shown on an isolated desktop "
            L"(not the real UAC Secure Desktop).\r\n\r\n"
            L"Do you want to stop TrayService now?",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            margin,
            margin,
            width - margin * 2,
            textHeight,
            windowHandle,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);

        CreateWindowExW(
            0,
            L"BUTTON",
            L"Stop Service",
            WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON | WS_TABSTOP,
            width / 2 - buttonWidth - 10,
            buttonTop,
            buttonWidth,
            buttonHeight,
            windowHandle,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kApproveButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);

        CreateWindowExW(
            0,
            L"BUTTON",
            L"Cancel",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON | WS_TABSTOP,
            width / 2 + 10,
            buttonTop,
            buttonWidth,
            buttonHeight,
            windowHandle,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRejectButtonId)),
            GetModuleHandleW(nullptr),
            nullptr);
    }

    LRESULT CALLBACK ConfirmationWindowProc(HWND windowHandle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* state = reinterpret_cast<ConfirmationWindowState*>(GetWindowLongPtrW(windowHandle, GWLP_USERDATA));

        switch (message)
        {
        case WM_CREATE:
        {
            const auto* createStruct = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            auto* initialState = reinterpret_cast<ConfirmationWindowState*>(createStruct->lpCreateParams);
            SetWindowLongPtrW(windowHandle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(initialState));
            BuildConfirmationLayout(windowHandle);
            return 0;
        }

        case WM_COMMAND:
            if (state == nullptr)
            {
                return 0;
            }

            if (LOWORD(wParam) == kApproveButtonId)
            {
                state->approved = true;
                DestroyWindow(windowHandle);
                return 0;
            }

            if (LOWORD(wParam) == kRejectButtonId)
            {
                state->approved = false;
                DestroyWindow(windowHandle);
                return 0;
            }

            return 0;

        case WM_CLOSE:
            if (state != nullptr)
            {
                state->approved = false;
            }

            DestroyWindow(windowHandle);
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

        if (RegisterClassExW(&windowClass) != 0)
        {
            return true;
        }

        if (GetLastError() == ERROR_CLASS_ALREADY_EXISTS)
        {
            return true;
        }

        WinApiLog::LogLastError(L"RegisterClassExW");
        return false;
    }

    DWORD WINAPI ConfirmationUiThreadProc(LPVOID parameter)
    {
        auto* context = reinterpret_cast<UiThreadContext*>(parameter);
        if (context == nullptr
            || context->readyEvent == nullptr
            || context->completedEvent == nullptr
            || context->cancelEvent == nullptr)
        {
            return 0;
        }

        if (!SetThreadDesktop(context->isolatedDesktop))
        {
            WinApiLog::LogLastError(L"SetThreadDesktop(isolated)");
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            return 0;
        }

        if (!RegisterConfirmationWindowClass())
        {
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            return 0;
        }

        ConfirmationWindowState state = {};
        const int width = 640;
        const int height = 320;
        const int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
        const int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;

        HWND windowHandle = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_APPWINDOW,
            kConfirmationWindowClassName,
            L"TrayService Stop Confirmation",
            WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
            x < 0 ? 0 : x,
            y < 0 ? 0 : y,
            width,
            height,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            &state);
        if (windowHandle == nullptr)
        {
            WinApiLog::LogLastError(L"CreateWindowExW(confirmation)");
            context->result = StopConfirmationResult::Failed;
            SetEvent(context->readyEvent);
            SetEvent(context->completedEvent);
            return 0;
        }

        ShowWindow(windowHandle, SW_SHOW);
        UpdateWindow(windowHandle);
        SetForegroundWindow(windowHandle);
        SetFocus(windowHandle);

        SetEvent(context->readyEvent);

        bool messageLoopFailed = false;
        for (;;)
        {
            HANDLE waitHandle = context->cancelEvent;
            const DWORD waitResult = MsgWaitForMultipleObjects(
                1,
                &waitHandle,
                FALSE,
                INFINITE,
                QS_ALLINPUT);

            if (waitResult == WAIT_OBJECT_0)
            {
                state.approved = false;
                if (IsWindow(windowHandle))
                {
                    DestroyWindow(windowHandle);
                }

                break;
            }

            if (waitResult == WAIT_OBJECT_0 + 1)
            {
                MSG message = {};
                while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
                {
                    if (message.message == WM_QUIT)
                    {
                        goto MessageLoopDone;
                    }

                    TranslateMessage(&message);
                    DispatchMessageW(&message);
                }

                continue;
            }

            messageLoopFailed = true;
            WinApiLog::LogError(L"MsgWaitForMultipleObjects(confirmation)", GetLastError());
            break;
        }

MessageLoopDone:
        if (messageLoopFailed)
        {
            context->result = StopConfirmationResult::Failed;
        }
        else
        {
            context->result = state.approved ? StopConfirmationResult::Approved : StopConfirmationResult::Rejected;
        }

        SetEvent(context->completedEvent);
        return 0;
    }
}

StopConfirmationResult ShowStopConfirmationOnIsolatedDesktop()
{
    DesktopSession desktopSession;
    if (!desktopSession.Initialize())
    {
        return StopConfirmationResult::Failed;
    }

    ScopedHandle readyEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!readyEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(ready)");
        return StopConfirmationResult::Failed;
    }

    ScopedHandle completedEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!completedEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(completed)");
        return StopConfirmationResult::Failed;
    }

    ScopedHandle cancelEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    if (!cancelEvent.IsValid())
    {
        WinApiLog::LogLastError(L"CreateEventW(cancel)");
        return StopConfirmationResult::Failed;
    }

    UiThreadContext context = {};
    context.isolatedDesktop = desktopSession.IsolatedDesktop();
    context.readyEvent = readyEvent.Get();
    context.completedEvent = completedEvent.Get();
    context.cancelEvent = cancelEvent.Get();
    context.result = StopConfirmationResult::Failed;

    ScopedHandle threadHandle(CreateThread(
        nullptr,
        0,
        ConfirmationUiThreadProc,
        &context,
        0,
        nullptr));
    if (!threadHandle.IsValid())
    {
        WinApiLog::LogLastError(L"CreateThread(confirmation)");
        return StopConfirmationResult::Failed;
    }

    const DWORD readyWait = WaitForSingleObject(readyEvent.Get(), kUiReadyTimeoutMs);
    if (readyWait != WAIT_OBJECT_0)
    {
        WinApiLog::LogError(
            L"WaitForSingleObject(ready)",
            readyWait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError());
        SetEvent(cancelEvent.Get());
        WaitForSingleObject(threadHandle.Get(), INFINITE);
        return StopConfirmationResult::Failed;
    }

    if (!desktopSession.SwitchToIsolatedDesktop())
    {
        SetEvent(cancelEvent.Get());
        WaitForSingleObject(threadHandle.Get(), INFINITE);
        return StopConfirmationResult::Failed;
    }

    // Ensures automatic switch back to the original desktop on every exit path.
    struct DesktopRestoreGuard
    {
        DesktopSession* session = nullptr;
        ~DesktopRestoreGuard()
        {
            if (session != nullptr)
            {
                session->RestoreDefaultDesktop();
            }
        }
    } restoreGuard{ &desktopSession };

    const DWORD completedWait = WaitForSingleObject(completedEvent.Get(), kConfirmationMaxWaitMs);
    if (completedWait != WAIT_OBJECT_0)
    {
        const DWORD waitError = completedWait == WAIT_TIMEOUT ? ERROR_TIMEOUT : GetLastError();
        WinApiLog::LogError(L"WaitForSingleObject(completed)", waitError);
        SetEvent(cancelEvent.Get());
        WaitForSingleObject(threadHandle.Get(), INFINITE);
        context.result = StopConfirmationResult::Failed;
    }

    WaitForSingleObject(threadHandle.Get(), INFINITE);
    return context.result;
}
