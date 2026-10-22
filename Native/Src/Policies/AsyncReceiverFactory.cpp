#include "stdafx.h"
#include "IPC/Policies/AsyncReceiverFactory.h"
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>




namespace IPC
{
namespace Policies
{
    class AsyncReceiverFactory::Scheduler::Impl : public std::enable_shared_from_this<Impl>
    {
    public:
        Impl(detail::Callback<void()> handler, boost::optional<ThreadPool> pool)
            : m_handler{ std::move(handler) },
              m_pool{ std::move(pool) },
              m_work{ ::CreateThreadpoolWork(
                  OnRun, this, m_pool ? static_cast<PTP_CALLBACK_ENVIRON>(m_pool->GetEnvironment()) : nullptr), ::CloseThreadpoolWork }
        {}

        void Stop()
        {
            // The ::WaitForThreadpoolWorkCallbacks cannot be used
            // to wait on pending tasks due to possible recursive calls.

            if (m_recursionDetector.IsExpectedThread())
            {
                m_pendingTasks.Signal();    // Signal extra count here instead of the handler.
            }

            m_pendingTasks.Signal();
            m_pendingTasks.Wait();
        }

        std::size_t operator()()
        {
            m_pendingTasks.AddCount();

            ::SubmitThreadpoolWork(m_work.get());

            return 1;
        }

    private:
        class CountdownEvent
        {
        public:
            void AddCount()
            {
                assert(m_count != 0);
                ++m_count;
            }

            void Signal()
            {
                // Serialize the zero transition with the wait predicate to
                // prevent notification between predicate evaluation and sleep.
                std::lock_guard<std::mutex> guard{ m_lock };
                if (--m_count == 0)
                {
                    m_zero.notify_all();
                }
            }

            auto AutoSignal()
            {
                auto deleter = [this](void*) { Signal(); };

                return std::unique_ptr<void, decltype(deleter)>{ (void*) true, std::move(deleter) };
            }

            void Wait()
            {
                std::unique_lock<decltype(m_lock)> guard{ m_lock };
                m_zero.wait(guard, [&] { return m_count == 0; });
            }

        private:
            std::atomic_size_t m_count{ 1 };
            std::mutex m_lock;
            std::condition_variable m_zero;
        };


        // Stack frames, rather than Boost.Thread TLS allocation, track nested
        // scheduler callbacks on each thread without another runtime library.
        class ThreadDetector
        {
            struct Frame { const ThreadDetector* owner; Frame* previous; };
            inline static thread_local Frame* s_current=nullptr;
        public:
            bool IsExpectedThread() const
            {
                for(auto frame=s_current; frame; frame=frame->previous)
                    if(frame->owner==this) return true;
                return false;
            }
            class Guard
            {
                Frame m_frame;
            public:
                explicit Guard(const ThreadDetector* owner) : m_frame{owner,s_current} { s_current=&m_frame; }
                Guard(const Guard&)=delete;
                Guard& operator=(const Guard&)=delete;
                ~Guard() { s_current=m_frame.previous; }
            };
            Guard Detect() const { return Guard{this}; }
        };

        static VOID CALLBACK OnRun(PTP_CALLBACK_INSTANCE /*instance*/, PVOID context, PTP_WORK /*work*/)
        {
            static_cast<Impl*>(context)->Run();
        }

        void Run()
        {
            auto keepAlive = shared_from_this();
            auto signaler = m_pendingTasks.AutoSignal();
            auto detector = m_recursionDetector.Detect();

            m_handler();
        }


        detail::Callback<void()> m_handler;
        boost::optional<ThreadPool> m_pool;
        std::unique_ptr<TP_WORK, decltype(&::CloseThreadpoolWork)> m_work;
        ThreadDetector m_recursionDetector;
        CountdownEvent m_pendingTasks;
    };


    AsyncReceiverFactory::AsyncReceiverFactory(boost::optional<ThreadPool> pool)
        : m_pool{ std::move(pool) }
    {}

    AsyncReceiverFactory::Scheduler::Scheduler(detail::Callback<void()> handler, boost::optional<ThreadPool> pool)
        : m_impl{ std::make_shared<Impl>(std::move(handler), std::move(pool)) }
    {}

    AsyncReceiverFactory::Scheduler::Scheduler(Scheduler&& other) = default;

    AsyncReceiverFactory::Scheduler::~Scheduler()
    {
        if (m_impl)
        {
            m_impl->Stop();
        }
    }

    std::size_t AsyncReceiverFactory::Scheduler::operator()() const
    {
        return m_impl->operator()();
    }

} // Policies
} // IPC
