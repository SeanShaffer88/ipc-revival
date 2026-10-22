#include <windows.h>
#include <IPC/SharedMemory.h>
#include <IPC/detail/LockFree/FixedQueue.h>
#include <IPC/detail/KernelEvent.h>
#include <IPC/Policies/AsyncReceiverFactory.h>
#include <IPC/Managed/detail/Interop/Transport.h>
#include <atomic>
#include <stdexcept>
#include <string>
#include <iostream>

void Check(bool value) { if (!value) throw std::runtime_error("check failed"); }
using LegacyTransport=IPC::Managed::detail::Interop::Transport<int,int>;
using LegacyTraits=IPC::Managed::detail::Interop::Traits;
static_assert(std::is_convertible<std::unique_ptr<IPC::ClientConnection<int,int,LegacyTraits>>,LegacyTransport::Client::ConnectionPtr>::value,"Client connection wrapper must accept ownership from its base pointer");
static_assert(std::is_convertible<std::unique_ptr<IPC::ServerConnection<int,int,LegacyTraits>>,LegacyTransport::Server::ConnectionPtr>::value,"Server connection wrapper must accept ownership from its base pointer");
struct Tracked {
    static int live;
    static bool failMove;
    Tracked() { ++live; }
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&&) { if (failMove) throw std::runtime_error("injected move failure"); ++live; }
    Tracked& operator=(Tracked&&) { if (failMove) throw std::runtime_error("injected assign failure"); return *this; }
    ~Tracked() { --live; }
};
int Tracked::live=0;
bool Tracked::failMove=false;
int main() {
    try {
        {
            IPC::detail::LockFree::FixedQueue<int, 2> queue;
            Check(queue.Push(1)); Check(queue.Push(2)); Check(!queue.Push(3));
            Check(*queue.Pop()==1); Check(*queue.Pop()==2); Check(!queue.Pop());
        }
        {
            IPC::detail::LockFree::FixedQueue<Tracked, 2> queue;
            queue.Push(Tracked{});
            Tracked::failMove=true;
            bool threw=false; try { queue.Pop(); } catch (...) { threw=true; }
            Check(threw); Check(Tracked::live==0);
            Tracked::failMove=false;
            queue.Push(Tracked{});
            Tracked::failMove=true; threw=false; try { queue.ConsumeAll([](Tracked&&) { if(Tracked::failMove) throw std::runtime_error("handler"); }); } catch (...) { threw=true; }
            Check(threw); Check(Tracked::live==0);
            Tracked::failMove=false; Check(queue.Push(Tracked{})); Check(queue.Push(Tracked{})); Check(!queue.Push(Tracked{}));
            Check(queue.ConsumeAll([](Tracked&&){})==2); Check(Tracked::live==0);
        }
        auto name="wr001-core-"+std::to_string(GetCurrentProcessId());
        {
            IPC::SharedMemory first{IPC::create_only, name.c_str(), 65536};
            auto& value=first.Construct<int>("value", 42);
            IPC::SharedMemory second{IPC::open_only, name.c_str()};
            Check(second.Find<int>("value")==42); second.Find<int>("value")=99; Check(value==99);
            bool threw=false; try { IPC::SharedMemory duplicate{IPC::create_only,name.c_str(),65536}; } catch(...) { threw=true; }
            Check(threw);
            auto free=first.GetFreeSize(); auto allocator=first.GetAllocator<char>();
            auto ptr=allocator.allocate(1024); allocator.deallocate(ptr,1024); Check(first.GetFreeSize()==free);
        }
        { IPC::SharedMemory reused{IPC::create_only,name.c_str(),65536}; }
        struct WorkQueue {
            std::atomic<int> remaining{64};
            boost::optional<int> Pop() { auto n=remaining.fetch_sub(1); return n>0?boost::optional<int>{n}:boost::none; }
        };
        for(int round=0;round<50;++round) {
            WorkQueue work; std::atomic<int> processed{0};
            { auto scheduler=IPC::Policies::AsyncReceiverFactory{}(work,[&](int){++processed;});
              for(int i=0;i<64;++i) scheduler(); }
            Check(processed==64);
        }
        std::cout << "PASS queue capacity, throwing move/callback cleanup, reuse, shared allocator and mapping lifetime\n";
        std::cout << "PASS async scheduler drain: 50 rounds / 3200 callbacks\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
