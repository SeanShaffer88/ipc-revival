#include <windows.h>
#include "../Examples/worker_protocol.h"
#include "ipc_bridge.h"
#include <iostream>
static_assert(sizeof(Response)==sizeof(ipc_result),"Bridge response ABI layout must match");
struct ControlClosed {};
void Transfer(HANDLE pipe,void* data,DWORD size,bool write) {
    auto bytes=static_cast<uint8_t*>(data);
    while(size) {DWORD count=0;BOOL ok=write?WriteFile(pipe,bytes,size,&count,nullptr):ReadFile(pipe,bytes,size,&count,nullptr);
        if(!ok||!count) throw ControlClosed{};size-=count;bytes+=count;}
}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 1;
    HANDLE pipe=CreateFileW(argv[1],GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
    if(pipe==INVALID_HANDLE_VALUE)return 2;
    try {
        std::wstring endpoint{argv[2]};std::string name(endpoint.begin(),endpoint.end());
        Transport transport;auto connector=transport.MakeClientConnector();
        auto pending=connector.Connect(name.c_str(),std::chrono::milliseconds{2000});
        if(pending.wait_for(std::chrono::seconds{3})!=std::future_status::ready) throw std::runtime_error("connect timeout");
        Transport::Client client{pending.get(),[]{}};
        auto memory=client.GetConnection().GetOutputChannel().GetMemory();
        auto blob=memory->MakeShared<Blob>(IPC::anonymous_instance,memory->GetAllocator<std::uint8_t>());
        uint32_t ready=0x49504331;Transfer(pipe,&ready,sizeof(ready),true);
        for(;;) {
            uint64_t id=0;uint32_t size=0;Transfer(pipe,&id,sizeof(id),false);Transfer(pipe,&size,sizeof(size),false);
            if(size>MaxBytes)throw std::runtime_error("oversized request");blob->resize(size);Transfer(pipe,blob->data(),size,false);
            auto future=client(Request{id,blob},std::chrono::seconds{30});
            auto response=future.get();if(response.id!=id||response.size!=size)throw std::runtime_error("response mismatch");
            Transfer(pipe,&response,sizeof(response),true);
        }
    }catch(ControlClosed){CloseHandle(pipe);return 0;}
    catch(...){CloseHandle(pipe);return 3;}
}
