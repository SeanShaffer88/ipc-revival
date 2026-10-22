#pragma once
#include <stdint.h>
#ifdef IPC_BRIDGE_BUILD
#define IPC_API __declspec(dllexport)
#else
#define IPC_API __declspec(dllimport)
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ipc_result { uint64_t request_id; uint64_t size; uint64_t checksum; } ipc_result;
enum ipc_status { IPC_OK=0, IPC_ARGUMENT=1, IPC_TIMEOUT=2, IPC_PEER=3, IPC_RESOURCE=4, IPC_IO=5, IPC_CLOSED=6 };
IPC_API uint32_t __cdecl ipc_abi_version(void);
IPC_API uint32_t __cdecl ipc_broker_id(void* session);
IPC_API int __cdecl ipc_open(const char* endpoint,uint32_t timeout_ms,void** session);
IPC_API int __cdecl ipc_hash(void* session,const uint8_t* bytes,uint32_t size,uint32_t timeout_ms,ipc_result* result);
IPC_API void __cdecl ipc_close(void* session);
#ifdef __cplusplus
}
#endif
