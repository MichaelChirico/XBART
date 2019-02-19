#pragma once

#ifndef GUARD_thread_pool_h
#define GUARD_thread_pool_h

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <memory>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

class ThreadPoolTaskDone 
{
public:
    ThreadPoolTaskDone() : done(false) { };
    std::atomic<bool> done;
    std::condition_variable wake;

    ThreadPoolTaskDone(const ThreadPoolTaskDone&) = delete; // no copy constructor
};

class ThreadPool 
{
public:
    inline ThreadPool() : stopping(false) { };
    inline ~ThreadPool() { stop(); }

    void start(size_t nthreads = 0); // if nthreads = 0, start 1 thread per hardware core
    void stop();

    // add new work item to the pool
    template<class F, class... Args>
    auto add_task(F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;

        auto sharedf = std::make_shared< std::packaged_task<return_type()> >(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
            );

        auto done = new ThreadPoolTaskDone;

        dones_mutex.lock();
        dones.push(std::shared_ptr<ThreadPoolTaskDone>(done));
        dones_mutex.unlock();

        std::future<return_type> res = sharedf->get_future();

        if (threads.size() == 0)
            throw std::runtime_error("add_task() called on inactive ThreadPool");

        if (stopping)
            throw std::runtime_error("add_task() called on stopping ThreadPool");

        tasks_mutex.lock();
        tasks.emplace(
            [this, sharedf, done]() // lambda callback to execute the task, called by a worker thread
        {
            (*sharedf)();
            dones_mutex.lock();
            done->done = true;
            done->wake.notify_all();
            dones_mutex.unlock();
        });
        tasks_mutex.unlock();

        condition.notify_one();
        return res;
    }

    void wait();

    inline bool is_active() { return threads.size() > 0; }

private:
    std::vector< std::thread > threads;
    std::queue< std::shared_ptr<ThreadPoolTaskDone> > dones;
    std::queue< std::function<void()> > tasks;

    // synchronization
    std::mutex tasks_mutex;
    std::mutex dones_mutex;
    std::condition_variable condition;
    std::atomic<bool> stopping;
};


#endif
