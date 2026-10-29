#include <windows.h>
#include <IPC/Transport.h>
#include <boost/container/vector.hpp>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Clock=std::chrono::steady_clock;
using namespace std::chrono_literals;
#include "worker_protocol.h"
unsigned Number(const char* text,unsigned maximum) {
    std::string s{text}; if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos) throw std::runtime_error("invalid number");
    auto n=std::stoull(s); if(n>maximum) throw std::runtime_error("number out of range"); return static_cast<unsigned>(n);
}
void ShmServer(const char* name,unsigned delay) {
    Transport transport;
    auto servers=transport.AcceptServers(name,[delay](auto&) {
        return [delay](Request request,auto&& reply) {
            // Native callbacks must contain exceptions, including a peer closing
            // while the application is still computing its response.
            try {
                if(!request.blob || request.blob->size()>MaxBytes) throw std::runtime_error("invalid blob");
                if(delay) { std::cout << "REQUEST" << std::endl; std::this_thread::sleep_for(std::chrono::milliseconds{delay}); }
                reply(Response{request.id,request.blob->size(),Hash(request.blob->data(),request.blob->size())});
            } catch(const std::exception& error) { std::cerr << "REQUEST_FAILED " << error.what() << std::endl; }
            catch(...) { std::cerr << "REQUEST_FAILED unknown exception" << std::endl; }
        };
    });
    std::cout << "READY" << std::endl;
    std::string line; std::getline(std::cin,line); // harness/application owns graceful shutdown
}
void Report(const char* transport,unsigned size,unsigned count,Clock::duration elapsed) {
    auto us=std::chrono::duration<double,std::micro>(elapsed).count();
    std::cout << "{\"transport\":\"" << transport << "\",\"bytes\":" << size << ",\"iterations\":" << count
              << ",\"meanRoundTripUs\":" << us/count << ",\"payloadMiBPerSecond\":"
              << (static_cast<double>(size)*count/(1024*1024))/(us/1000000) << "}" << std::endl;
}
void ShmClient(const char* name,unsigned size,unsigned count,unsigned timeout) {
    Transport transport;
    auto connector=transport.MakeClientConnector();
    auto pending=connector.Connect(name,std::chrono::milliseconds{timeout});
    if(pending.wait_for(std::chrono::milliseconds{timeout+500})!=std::future_status::ready) throw std::runtime_error("connect timeout");
    Transport::Client client{pending.get(),[]{}};
    auto memory=client.GetConnection().GetOutputChannel().GetMemory();
    auto blob=memory->MakeShared<Blob>(IPC::anonymous_instance,memory->GetAllocator<std::uint8_t>());
    blob->resize(size); for(unsigned i=0;i<size;++i) (*blob)[i]=static_cast<std::uint8_t>(i*31u+7u);
    auto expected=Hash(blob->data(),size);
    auto begin=Clock::now();
    for(unsigned i=0;i<count;++i) {
        auto future=client(Request{i,blob},std::chrono::milliseconds{timeout});
        if(future.wait_for(std::chrono::milliseconds{timeout+500})!=std::future_status::ready) throw std::runtime_error("response timeout");
        auto response=future.get();
        if(response.id!=i||response.size!=size||response.checksum!=expected) throw std::runtime_error("response mismatch");
    }
    Report("shared-memory",size,count,Clock::now()-begin);
}
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value{h} { if(h==INVALID_HANDLE_VALUE||h==nullptr) throw std::runtime_error("Win32 handle creation failed"); }
    ~Handle(){CloseHandle(value);}
    Handle(const Handle&)=delete; Handle& operator=(const Handle&)=delete;
};
void Complete(HANDLE handle,OVERLAPPED& op,DWORD error,unsigned timeout,DWORD& transferred) {
    if(error!=ERROR_IO_PENDING) throw std::runtime_error("pipe I/O failed");
    auto wait=WaitForSingleObject(op.hEvent,timeout);
    if(wait!=WAIT_OBJECT_0) {
        CancelIoEx(handle,&op); GetOverlappedResult(handle,&op,&transferred,TRUE);
        throw std::runtime_error("pipe I/O timeout");
    }
    if(!GetOverlappedResult(handle,&op,&transferred,FALSE)) throw std::runtime_error("pipe completion failed");
}
void Transfer(HANDLE pipe,void* bytes,DWORD size,bool write) {
    auto data=static_cast<std::uint8_t*>(bytes);
    auto deadline=Clock::now()+2s;
    while(size) {
        auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-Clock::now()).count();
        if(remaining<=0) throw std::runtime_error("pipe transfer timeout");
        Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)}; OVERLAPPED op{};op.hEvent=event.value;
        DWORD transferred=0;
        BOOL success=write?WriteFile(pipe,data,size,&transferred,&op):ReadFile(pipe,data,size,&transferred,&op);
        DWORD error=success?ERROR_SUCCESS:GetLastError();
        if(!success) Complete(pipe,op,error,static_cast<unsigned>(remaining),transferred);
        if(!transferred) throw std::runtime_error("pipe closed");
        size-=transferred; data+=transferred;
    }
}
std::string PipeName(const char* name){return std::string{"\\\\.\\pipe\\wr001-"}+name;}
void PipeServer(const char* name,unsigned count) {
    Handle pipe{CreateNamedPipeA(PipeName(name).c_str(),PIPE_ACCESS_DUPLEX|FILE_FLAG_OVERLAPPED|FILE_FLAG_FIRST_PIPE_INSTANCE,
        PIPE_TYPE_BYTE|PIPE_READMODE_BYTE|PIPE_WAIT|PIPE_REJECT_REMOTE_CLIENTS,1,65536,65536,0,nullptr)};
    Handle event{CreateEventW(nullptr,TRUE,FALSE,nullptr)}; OVERLAPPED op{};op.hEvent=event.value;
    std::cout << "READY" << std::endl;
    BOOL success=ConnectNamedPipe(pipe.value,&op); DWORD error=success?ERROR_SUCCESS:GetLastError(); DWORD ignored=0;
    if(!success&&error!=ERROR_PIPE_CONNECTED) Complete(pipe.value,op,error,10000,ignored);
    for(unsigned i=0;i<count;++i) {
        std::uint32_t size=0; Transfer(pipe.value,&size,sizeof(size),false);
        if(size>MaxBytes) throw std::runtime_error("oversized pipe request");
        std::vector<std::uint8_t> bytes(size); Transfer(pipe.value,bytes.data(),size,false);
        Response response{i,size,Hash(bytes.data(),size)}; Transfer(pipe.value,&response,sizeof(response),true);
        std::uint8_t ack=0; Transfer(pipe.value,&ack,1,false); if(ack!=1) throw std::runtime_error("invalid ack");
    }
}
void PipeClient(const char* name,unsigned size,unsigned count) {
    Handle pipe{CreateFileA(PipeName(name).c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_FLAG_OVERLAPPED,nullptr)};
    std::vector<std::uint8_t> bytes(size);for(unsigned i=0;i<size;++i) bytes[i]=static_cast<std::uint8_t>(i*31u+7u);
    auto expected=Hash(bytes.data(),size); auto begin=Clock::now();
    for(unsigned i=0;i<count;++i) {
        std::uint32_t length=size; Transfer(pipe.value,&length,sizeof(length),true); Transfer(pipe.value,bytes.data(),size,true);
        Response response{};Transfer(pipe.value,&response,sizeof(response),false);
        if(response.id!=i||response.size!=size||response.checksum!=expected) throw std::runtime_error("pipe response mismatch");
        std::uint8_t ack=1;Transfer(pipe.value,&ack,1,true);
    }
    Report("named-pipe",size,count,Clock::now()-begin);
}
int main(int argc,char** argv) {
    try {
        if(argc<3) throw std::runtime_error("usage: ipc_worker shm-server NAME [delay-ms] | shm-client NAME BYTES COUNT [timeout-ms] | pipe-server NAME COUNT | pipe-client NAME BYTES COUNT");
        std::string mode=argv[1];std::string name=argv[2];
        if(name.empty()||name.size()>80||name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_")!=std::string::npos) throw std::runtime_error("invalid endpoint name");
        if(mode=="shm-server"&&(argc==3||argc==4)) ShmServer(argv[2],argc==4?Number(argv[3],10000):0);
        else if(mode=="pipe-server"&&argc==4) { auto count=Number(argv[3],100000); if(!count) throw std::runtime_error("count must be positive"); PipeServer(argv[2],count); }
        else if((mode=="shm-client"||mode=="pipe-client")&&(argc==5||argc==6)) {
            auto size=Number(argv[3],MaxBytes),count=Number(argv[4],100000),timeout=argc==6?Number(argv[5],10000):1500;
            if(!count||!timeout) throw std::runtime_error("count/timeout must be positive");
            if(mode=="shm-client") ShmClient(argv[2],size,count,timeout); else PipeClient(argv[2],size,count);
        } else throw std::runtime_error("invalid arguments");
        return 0;
    } catch(const std::exception& e){std::cerr << "ERROR " << e.what() << std::endl;return 1;}
}
