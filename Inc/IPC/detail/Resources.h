#pragma once
#include <windows.h>
#include <system_error>
namespace IPC { namespace detail {
enum class Resource { none, event, work, wait, timer, pool, memory };
#ifdef IPC_ENABLE_TEST_FAILURES
inline thread_local Resource nextFailure=Resource::none;
inline void FailNext(Resource resource) { nextFailure=resource; }
inline void CheckResource(Resource resource) {
    if(nextFailure==resource) { nextFailure=Resource::none; throw std::system_error(ERROR_NOT_ENOUGH_MEMORY,std::system_category()); }
}
#else
inline void CheckResource(Resource) {}
#endif
template<class Factory> auto Acquire(Resource resource,Factory&& factory) {
    CheckResource(resource);
    auto handle=factory();
    if(!handle || handle==reinterpret_cast<decltype(handle)>(INVALID_HANDLE_VALUE)) {
        auto error=GetLastError();
        throw std::system_error(error?static_cast<int>(error):ERROR_NOT_ENOUGH_MEMORY,std::system_category());
    }
    return handle;
}
} }
