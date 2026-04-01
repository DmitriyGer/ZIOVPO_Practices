#include "pch.h"
#include "TrayIconManager.h"

// Создает объект для управления иконкой и сообщениями системного трея.
TrayIconManager::TrayIconManager()
    : m_windowHandle(nullptr)
    , m_instanceHandle(nullptr)
    , m_callbackMessage(0)
    , m_iconResourceId(0)
    , m_taskbarCreatedMessage(0)
{
}

// Инициализирует настройки иконки и служебные сообщения.
void TrayIconManager::Initialize(HWND windowHandle, HINSTANCE instanceHandle, UINT callbackMessage, UINT iconResourceId, const std::wstring& tooltipText)
{
    m_windowHandle = windowHandle;
    m_instanceHandle = instanceHandle;
    m_callbackMessage = callbackMessage;
    m_iconResourceId = iconResourceId;
    m_tooltipText = tooltipText;
    m_taskbarCreatedMessage = RegisterWindowMessageW(L"TaskbarCreated");
}

// Добавляет иконку приложения в системный трей.
bool TrayIconManager::AddIconToTray()
{
    NOTIFYICONDATAW notifyData = {};
    FillNotifyIconData(notifyData);

    if (!Shell_NotifyIconW(NIM_ADD, &notifyData))
    {
        return false;
    }

    notifyData.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &notifyData);
    return true;
}

// Удаляет иконку приложения из системного трея.
void TrayIconManager::RemoveIconFromTray()
{
    NOTIFYICONDATAW notifyData = {};
    FillNotifyIconData(notifyData);
    Shell_NotifyIconW(NIM_DELETE, &notifyData);
}

// Показывает контекстное меню для иконки в трее.
void TrayIconManager::ShowTrayContextMenu(UINT openCommandId, UINT exitCommandId)
{
    HMENU trayMenu = CreatePopupMenu();
    if (trayMenu == nullptr)
    {
        return;
    }

    AppendMenuW(trayMenu, MF_STRING, openCommandId, L"\x041E\x0442\x043A\x0440\x044B\x0442\x044C");
    AppendMenuW(trayMenu, MF_STRING, exitCommandId, L"\x0412\x044B\x0445\x043E\x0434");

    POINT mousePoint = {};
    GetCursorPos(&mousePoint);

    SetForegroundWindow(m_windowHandle);
    TrackPopupMenu(trayMenu, TPM_RIGHTBUTTON, mousePoint.x, mousePoint.y, 0, m_windowHandle, nullptr);
    PostMessageW(m_windowHandle, WM_NULL, 0, 0);

    DestroyMenu(trayMenu);
}

// Возвращает зарегистрированное сообщение пересоздания панели задач.
UINT TrayIconManager::GetTaskbarCreatedMessage() const
{
    return m_taskbarCreatedMessage;
}

// Заполняет структуру с данными для управления иконкой трея.
void TrayIconManager::FillNotifyIconData(NOTIFYICONDATAW& notifyData) const
{
    notifyData.cbSize = sizeof(NOTIFYICONDATAW);
    notifyData.hWnd = m_windowHandle;
    notifyData.uID = 1;
    notifyData.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    notifyData.uCallbackMessage = m_callbackMessage;
    notifyData.hIcon = LoadIconW(m_instanceHandle, MAKEINTRESOURCEW(m_iconResourceId));

    lstrcpynW(notifyData.szTip, m_tooltipText.c_str(), ARRAYSIZE(notifyData.szTip));
}
