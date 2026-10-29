#include "stdafx.h"
#include "IPC/detail/KernelObject.h"
#include "IPC/Exception.h"
#include <system_error>
#include <limits>


namespace IPC
{
namespace detail
{
    namespace
    {
        HANDLE VerifyHandle(HANDLE handle, bool verifyValidHandle)
        {
            auto error = ::GetLastError();
            bool invalid = !handle || handle == INVALID_HANDLE_VALUE;
            if (invalid || (verifyValidHandle && error != ERROR_SUCCESS))
            {
                if (!invalid) ::CloseHandle(handle); // ownership was transferred even when verification rejects it
                if (error == ERROR_SUCCESS) error = ERROR_INVALID_HANDLE;
                {
                    try
                    {
                        throw std::system_error{ static_cast<int>(error), std::system_category() };
                    }
                    catch (const std::system_error&)
                    {
                        std::throw_with_nested(Exception{ "Invalid handle." });
                    }
                }
            }

            return handle;
        }
    }


    KernelObject::KernelObject(std::nullptr_t)
    {}

    KernelObject::KernelObject(void* handle, bool verifyValidHandle)
        : m_handle{ VerifyHandle(handle, verifyValidHandle), ::CloseHandle }
    {}

    void KernelObject::Wait() const
    {
        if (::WaitForSingleObject(m_handle.get(), INFINITE) != WAIT_OBJECT_0)
            throw std::system_error(static_cast<int>(::GetLastError()), std::system_category());
    }

    bool KernelObject::Wait(std::chrono::milliseconds timeout) const
    {
        if(timeout.count()<0 || static_cast<unsigned long long>(timeout.count())>=INFINITE)
            throw std::invalid_argument("Finite wait timeout must be in [0, INFINITE)");
        auto result=::WaitForSingleObject(m_handle.get(), static_cast<DWORD>(timeout.count()));
        if(result==WAIT_OBJECT_0) return true;
        if(result==WAIT_TIMEOUT) return false;
        throw std::system_error(static_cast<int>(::GetLastError()), std::system_category());
    }

    bool KernelObject::IsSignaled() const
    {
        return Wait(std::chrono::milliseconds::zero());
    }

    KernelObject::operator bool() const
    {
        return static_cast<bool>(m_handle);
    }

    KernelObject::operator void*() const
    {
        return m_handle.get();
    }
        
} // detail
} // IPC
