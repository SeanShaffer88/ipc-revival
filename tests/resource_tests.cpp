#include <windows.h>
#include <IPC/detail/Resources.h>
#include <IPC/detail/KernelEvent.h>
#include <IPC/SharedMemory.h>
#include <IPC/Policies/ThreadPool.h>
#include <IPC/Policies/AsyncReceiverFactory.h>
#include <IPC/Policies/WaitHandleFactory.h>
#include <IPC/Policies/TimeoutFactory.h>
#include <functional>
#include <iostream>
#include <vector>
#include <future>
using namespace IPC;
void Check(bool ok){if(!ok)throw std::runtime_error("resource check failed");}
void Fails(detail::Resource resource,const std::function<void()>& call) {
    detail::FailNext(resource);bool threw=false;try{call();}catch(const std::exception&){threw=true;}
    Check(threw);call();
}
int main(){try{
    DWORD before=0,after=0;GetProcessHandleCount(GetCurrentProcess(),&before);
    auto name="resource-"+std::to_string(GetCurrentProcessId());
    Fails(detail::Resource::event,[&]{detail::KernelEvent event{create_only,false,false,name.c_str()};});
    Fails(detail::Resource::memory,[&]{SharedMemory memory{create_only,name.c_str(),65536};});
    Fails(detail::Resource::pool,[]{Policies::ThreadPool pool;});
    bool zeroPool=false;try{Policies::ThreadPool pool{0,0};}catch(...){zeroPool=true;}Check(zeroPool);
    struct Queue{boost::optional<int> Pop(){return boost::none;}} queue;
    Fails(detail::Resource::work,[&]{auto scheduler=Policies::AsyncReceiverFactory{}(queue,[](int){});});
    Fails(detail::Resource::wait,[]{detail::KernelEvent event{create_only,false};auto waiter=Policies::WaitHandleFactory{}(event,[]{return false;});});
    Fails(detail::Resource::timer,[]{auto timer=Policies::TimeoutFactory{}([]{});});
    {detail::KernelEvent first{create_only,false,false,name.c_str()};
     for(int i=0;i<500;++i){bool threw=false;try{detail::KernelEvent duplicate{create_only,false,false,name.c_str()};}catch(...){threw=true;}Check(threw);}}
    SetLastError(ERROR_SUCCESS);bool invalid=false;try{detail::KernelObject object{INVALID_HANDLE_VALUE};}catch(...){invalid=true;}Check(invalid);
    invalid=false;try{detail::KernelObject{nullptr}.Wait(std::chrono::milliseconds{1});}catch(...){invalid=true;}Check(invalid);
    {detail::KernelEvent event{create_only,false};invalid=false;try{event.Wait(std::chrono::milliseconds{-1});}catch(...){invalid=true;}Check(invalid);}
    {SharedMemory memory{create_only,name.c_str(),65536};auto allocator=memory.GetAllocator<char>();
     std::vector<decltype(allocator)::pointer> blocks;auto free=memory.GetFreeSize();bool exhausted=false;
     try{for(int i=0;i<1000;++i)blocks.push_back(allocator.allocate(1024));}catch(const std::exception&){exhausted=true;}
     Check(exhausted);for(auto ptr:blocks)allocator.deallocate(ptr,1024);Check(memory.GetFreeSize()==free);
     auto ptr=allocator.allocate(8192);allocator.deallocate(ptr,8192);}
    GetProcessHandleCount(GetCurrentProcess(),&after);Check(after<=before+2);
    {struct One{boost::optional<int> Pop(){return 1;}} one;
     auto scheduler=Policies::AsyncReceiverFactory{}(one,[](int){throw std::runtime_error("async callback");});scheduler();}
    {detail::KernelEvent event{create_only,false};std::promise<void> ran;auto ready=ran.get_future();
     auto waiter=Policies::WaitHandleFactory{}(event,[&]()->bool{ran.set_value();throw std::runtime_error("wait callback");});
     event.Signal();Check(ready.wait_for(std::chrono::seconds{2})==std::future_status::ready);}
    {std::promise<void> ran;auto ready=ran.get_future();auto timer=Policies::TimeoutFactory{}([&]{ran.set_value();throw std::runtime_error("timer callback");});
     timer(std::chrono::milliseconds{1});Check(ready.wait_for(std::chrono::seconds{2})==std::future_status::ready);}
    std::cout<<"PASS six injected resources, 500 duplicate-event cleanups, invalid handles/waits and real allocator exhaustion/recovery\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
