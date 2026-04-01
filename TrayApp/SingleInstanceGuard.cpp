#include "pch.h"
#include "SingleInstanceGuard.h"

#include <Lmcons.h>

// Создает объект, который будет удерживать мьютекс единственного экземпляра.
SingleInstanceGuard::SingleInstanceGuard()
    : m_mutexHandle(nullptr)
    , m_isOwner(false)
{
}

// Освобождает мьютекс и закрывает дескриптор при завершении приложения.
SingleInstanceGuard::~SingleInstanceGuard()
{
    if (m_isOwner)
    {
        ReleaseMutex(m_mutexHandle);
    }

    if (m_mutexHandle != nullptr)
    {
        CloseHandle(m_mutexHandle);
    }
}

// Пытается занять именованный мьютекс для текущего пользователя.
bool SingleInstanceGuard::TryLockForCurrentUser(const wchar_t* appName)
{
    std::wstring mutexName = BuildMutexNameForCurrentUser(appName);

    m_mutexHandle = CreateMutexW(nullptr, TRUE, mutexName.c_str());
    if (m_mutexHandle == nullptr)
    {
        return false;
    }

    if (GetLastError() == ERROR_ALREADY_EXISTS)
    {
        return false;
    }

    m_isOwner = true;
    return true;
}

// Формирует имя мьютекса с учетом имени текущего пользователя.
std::wstring SingleInstanceGuard::BuildMutexNameForCurrentUser(const wchar_t* appName) const
{
    wchar_t userName[UNLEN + 1] = {};
    DWORD userNameSize = UNLEN + 1;
    if (!GetUserNameW(userName, &userNameSize))
    {
        lstrcpyW(userName, L"UnknownUser");
    }

    std::wstring mutexName = L"Local\\";
    mutexName += appName;
    mutexName += L"_";
    mutexName += userName;

    return mutexName;
}
