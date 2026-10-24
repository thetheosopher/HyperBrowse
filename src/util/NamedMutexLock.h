#pragma once

#include <windows.h>

namespace hyperbrowse::util
{
    enum class NamedMutexLockResult
    {
        Acquired,
        AbandonedAcquired,
        TimedOut,
        Unavailable
    };

    // Bounded cross-process lock. Callers must treat TimedOut and Unavailable as
    // "proceed without the lock" so a hung peer cannot block the calling thread.
    class NamedMutexLock
    {
    public:
        NamedMutexLock(const wchar_t* name, DWORD timeoutMilliseconds) noexcept
        {
            mutex_ = CreateMutexW(nullptr, FALSE, name);
            if (!mutex_)
            {
                result_ = NamedMutexLockResult::Unavailable;
                return;
            }

            const DWORD waitResult = WaitForSingleObject(mutex_, timeoutMilliseconds);
            if (waitResult == WAIT_OBJECT_0)
            {
                result_ = NamedMutexLockResult::Acquired;
                held_ = true;
            }
            else if (waitResult == WAIT_ABANDONED)
            {
                result_ = NamedMutexLockResult::AbandonedAcquired;
                held_ = true;
            }
            else
            {
                result_ = waitResult == WAIT_TIMEOUT
                    ? NamedMutexLockResult::TimedOut
                    : NamedMutexLockResult::Unavailable;
            }
        }

        ~NamedMutexLock()
        {
            if (held_)
            {
                ReleaseMutex(mutex_);
            }
            if (mutex_)
            {
                CloseHandle(mutex_);
            }
        }

        NamedMutexLock(const NamedMutexLock&) = delete;
        NamedMutexLock& operator=(const NamedMutexLock&) = delete;

        NamedMutexLockResult Result() const noexcept
        {
            return result_;
        }

        bool Held() const noexcept
        {
            return held_;
        }

    private:
        HANDLE mutex_{};
        NamedMutexLockResult result_{NamedMutexLockResult::Unavailable};
        bool held_{};
    };
}
