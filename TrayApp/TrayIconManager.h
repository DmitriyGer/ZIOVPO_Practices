#pragma once

#include <Windows.h>
#include <shellapi.h>
#include <string>

class TrayIconManager
{
public:
    // Создает объект для работы с иконкой приложения в системном трее.
    TrayIconManager();

    // Инициализирует настройки иконки и служебные сообщения.
    void Initialize(HWND windowHandle, HINSTANCE instanceHandle, UINT callbackMessage, UINT iconResourceId, const std::wstring& tooltipText);

    // Добавляет иконку приложения в системный трей.
    bool AddIconToTray();

    // Удаляет иконку приложения из системного трея.
    void RemoveIconFromTray();

    // Показывает контекстное меню для иконки в трее.
    void ShowTrayContextMenu(UINT openCommandId, UINT exitCommandId);

    // Возвращает зарегистрированное сообщение пересоздания панели задач.
    UINT GetTaskbarCreatedMessage() const;

private:
    // Заполняет структуру с данными для управления иконкой трея.
    void FillNotifyIconData(NOTIFYICONDATAW& notifyData) const;

private:
    HWND m_windowHandle;
    HINSTANCE m_instanceHandle;
    UINT m_callbackMessage;
    UINT m_iconResourceId;
    std::wstring m_tooltipText;
    UINT m_taskbarCreatedMessage;
};
