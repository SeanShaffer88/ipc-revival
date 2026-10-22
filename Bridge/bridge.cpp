#include "ipc_bridge.h"
#include <windows.h>
#include <objbase.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>

namespace {
struct Failure { int status; };
struct Handle {
    HANDLE value=nullptr;
    Handle()=default;
    explicit Handle(HANDLE h):value{h} { if(!h||h==INVALID_HANDLE_VALUE) {value=nullptr;throw Failure{IPC_RESOURCE};} }
    ~Handle(){if(value) CloseHandle(value);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
struct Session {
    Handle pipe,process,job;
    std::timed_mutex mutex;
    uint64_t sequence=0;
    bool failed=true; // unpublished/partially opened sessions take the failure cleanup path
    void Stop() noexcept {
        failed=true;
        if(process.value && WaitForSingleObject(process.value,0)==WAIT_TIMEOUT) {
            TerminateProcess(process.value,IPC_CLOSED);
            WaitForSingleObject(process.value,1000);
        }
    }
    ~Session(){
        // A healthy broker should unwind mapped owners normally. If cleanup is
        // poisoned, keep disposal bounded and fall back to job termination.
        if(!failed && process.value && pipe.value) {
            DisconnectNamedPipe(pipe.value);
            WaitForSingleObject(process.value,250);
        }
        Stop();
    }
};
struct JobAttributes {
    HANDLE job;
    std::unique_ptr<unsigned char[]> storage;
    LPPROC_THREAD_ATTRIBUTE_LIST list=nullptr;
    explicit JobAttributes(HANDLE value):job{value} {
        SIZE_T size=0;InitializeProcThreadAttributeList(nullptr,1,0,&size);
        if(!size) throw Failure{IPC_RESOURCE};
        storage=std::make_unique<unsigned char[]>(size);
        auto candidate=reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.get());
        if(!InitializeProcThreadAttributeList(candidate,1,0,&size)) throw Failure{IPC_RESOURCE};
        if(!UpdateProcThreadAttribute(candidate,0,PROC_THREAD_ATTRIBUTE_JOB_LIST,&job,sizeof(job),nullptr,nullptr)) {
            DeleteProcThreadAttributeList(candidate);throw Failure{IPC_IO};
        }
        list=candidate;
    }
    ~JobAttributes(){if(list) DeleteProcThreadAttributeList(list);}
};
ULONGLONG Deadline(uint32_t ms) { if(ms<1||ms>30000) throw Failure{IPC_ARGUMENT};return GetTickCount64()+ms; }
DWORD Remaining(ULONGLONG deadline) {
    auto now=GetTickCount64(); if(now>=deadline) throw Failure{IPC_TIMEOUT};return static_cast<DWORD>(deadline-now);
}
void Complete(HANDLE pipe,OVERLAPPED& op,DWORD error,ULONGLONG deadline,DWORD& count) {
    if(error!=ERROR_IO_PENDING) throw Failure{IPC_PEER};
    auto now=GetTickCount64();
    DWORD wait=WaitForSingleObject(op.hEvent,deadline>now?static_cast<DWORD>(deadline-now):0);
    if(wait!=WAIT_OBJECT_0) {
        CancelIoEx(pipe,&op);GetOverlappedResult(pipe,&op,&count,TRUE);
        throw Failure{wait==WAIT_TIMEOUT?IPC_TIMEOUT:IPC_IO};
    }
    if(!GetOverlappedResult(pipe,&op,&count,FALSE)) throw Failure{IPC_PEER};
}
void Transfer(Session& session,void* data,DWORD size,bool write,ULONGLONG deadline) {
    auto bytes=static_cast<uint8_t*>(data);
    while(size) {
        Remaining(deadline);
        Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};OVERLAPPED op{};op.hEvent=event.value;DWORD count=0;
        BOOL ok=write?WriteFile(session.pipe.value,bytes,size,&count,&op):ReadFile(session.pipe.value,bytes,size,&count,&op);
        DWORD error=ok?ERROR_SUCCESS:GetLastError();if(!ok) Complete(session.pipe.value,op,error,deadline,count);
        if(count==0) throw Failure{IPC_PEER};bytes+=count;size-=count;
    }
}
std::wstring BrokerPath() {
    HMODULE module=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ipc_abi_version),&module)) throw Failure{IPC_IO};
    std::wstring path(32768,L'\0');DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));
    if(!length||length>=path.size()) throw Failure{IPC_IO};path.resize(length);
    auto slash=path.find_last_of(L"\\/");if(slash==std::wstring::npos) throw Failure{IPC_IO};
    return path.substr(0,slash+1)+L"ipc_broker.exe";
}
int Status() noexcept { try { throw; } catch(Failure f){return f.status;} catch(const std::bad_alloc&){return IPC_RESOURCE;} catch(...){return IPC_IO;} }
}
uint32_t __cdecl ipc_abi_version(void){return 1;}
uint32_t __cdecl ipc_broker_id(void* session){return session?GetProcessId(static_cast<Session*>(session)->process.value):0;}
int __cdecl ipc_open(const char* endpoint,uint32_t timeout_ms,void** output) {
    if(!output) return IPC_ARGUMENT;*output=nullptr;
    try {
        auto deadline=Deadline(timeout_ms);
        if(!endpoint) return IPC_ARGUMENT;
        std::string name{endpoint};if(name.empty()||name.size()>80||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos) return IPC_ARGUMENT;
        auto session=std::make_unique<Session>();
        GUID guid{};if(FAILED(CoCreateGuid(&guid))) throw Failure{IPC_RESOURCE};
        wchar_t id[40]{};if(!StringFromGUID2(guid,id,40)) throw Failure{IPC_IO};
        std::wstring pipeName=std::wstring{L"\\\\.\\pipe\\ipc-bridge-"}+id;
        session->pipe.value=CreateNamedPipeW(pipeName.c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
            PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr);
        if(session->pipe.value==INVALID_HANDLE_VALUE){session->pipe.value=nullptr;throw Failure{IPC_RESOURCE};}
        session->job.value=CreateJobObjectW(nullptr,nullptr);if(!session->job.value) throw Failure{IPC_RESOURCE};
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!SetInformationJobObject(session->job.value,JobObjectExtendedLimitInformation,&limits,sizeof(limits))) throw Failure{IPC_IO};
        auto path=BrokerPath();std::wstring command=L"\""+path+L"\" \""+pipeName+L"\" "+std::wstring{name.begin(),name.end()};
        JobAttributes attributes{session->job.value};
        STARTUPINFOEXW startup{};startup.StartupInfo.cb=sizeof(startup);startup.lpAttributeList=attributes.list;PROCESS_INFORMATION process{};
        // Assign at creation, closing the owner-death window between process
        // creation and a later AssignProcessToJobObject call.
        if(!CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW|CREATE_SUSPENDED|EXTENDED_STARTUPINFO_PRESENT,nullptr,nullptr,&startup.StartupInfo,&process)) throw Failure{IPC_RESOURCE};
        session->process.value=process.hProcess;Handle thread{process.hThread};
        if(ResumeThread(thread.value)==static_cast<DWORD>(-1)) throw Failure{IPC_IO};
        Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)};OVERLAPPED op{};op.hEvent=event.value;DWORD count=0;
        BOOL ok=ConnectNamedPipe(session->pipe.value,&op);DWORD error=ok?ERROR_SUCCESS:GetLastError();
        if(!ok&&error!=ERROR_PIPE_CONNECTED) Complete(session->pipe.value,op,error,deadline,count);
        ULONG peer=0;if(!GetNamedPipeClientProcessId(session->pipe.value,&peer)||peer!=process.dwProcessId) throw Failure{IPC_PEER};
        uint32_t ready=0;Transfer(*session,&ready,sizeof(ready),false,deadline);
        if(ready!=0x49504331) throw Failure{IPC_PEER};
        session->failed=false;*output=session.release();return IPC_OK;
    } catch(...){return Status();}
}
int __cdecl ipc_hash(void* handle,const uint8_t* bytes,uint32_t size,uint32_t timeout_ms,ipc_result* result) {
    if(!handle||!result||(!bytes&&size)||size>256*1024) return IPC_ARGUMENT;
    *result={};auto session=static_cast<Session*>(handle);
    try {
        auto deadline=Deadline(timeout_ms);
        std::unique_lock<std::timed_mutex> lock{session->mutex,std::defer_lock};
        if(!lock.try_lock_for(std::chrono::milliseconds{Remaining(deadline)})) return IPC_TIMEOUT;
        if(session->failed) return IPC_CLOSED;
        try {
            uint64_t id=++session->sequence;uint32_t length=size;
            Transfer(*session,&id,sizeof(id),true,deadline);Transfer(*session,&length,sizeof(length),true,deadline);
            Transfer(*session,const_cast<uint8_t*>(bytes),size,true,deadline);Transfer(*session,result,sizeof(*result),false,deadline);
            if(result->request_id!=id||result->size!=size) throw Failure{IPC_PEER};return IPC_OK;
        } catch(...) {session->Stop();*result={};return Status();}
    }catch(...){return Status();}
}
void __cdecl ipc_close(void* handle){delete static_cast<Session*>(handle);}
