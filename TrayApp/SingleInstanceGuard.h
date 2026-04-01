#pragma once

#include <Windows.h>
#include <string>

class SingleInstanceGuard
{
public:
    // Создает объект, который контролирует запуск единственного экземпляра.
    SingleInstanceGuard();

    // Освобождает ресурсы именованного мьютекса.
    ~SingleInstanceGuard();

    // Пытается занять именованный мьютекс для текущего пользователя.
    bool TryLockForCurrentUser(const wchar_t* appName);

private:
    // Формирует имя мьютекса с учетом имени текущего пользователя.
    std::wstring BuildMutexNameForCurrentUser(const wchar_t* appName) const;

private:
    HANDLE m_mutexHandle;
    bool m_isOwner;
};
