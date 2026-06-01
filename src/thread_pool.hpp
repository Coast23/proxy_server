#pragma once
#include <mutex>
#include <queue>
#include <future>
#include <thread>
#include <vector>
#include <functional>

inline std::size_t default_thread_count() noexcept {
    auto num = std::thread::hardware_concurrency();
    return std::max(num, 2u);
}

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t threads = default_thread_count()) : stop_(false) {
        workers_.reserve(threads);
        for(size_t i = 0; i < threads; ++i){
            workers_.emplace_back(&ThreadPool::worker_loop, this);
        }
    }
    ~ThreadPool(){
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
        }
        cv_.notify_all();
        for(auto& w : workers_) if(w.joinable()) w.join();
    }

    template<typename F, typename... Args>
    auto enqueue(F&& f, Args&&... args) -> std::future<typename std::invoke_result<F, Args...>::type> {
        using RetType = std::invoke_result_t<F, Args...>;
    
        auto task = std::make_shared<std::packaged_task<RetType()>>(
            [f = std::forward<F>(f),
                args = std::make_tuple(std::forward<Args>(args)...)]() mutable -> RetType {
                   return std::apply(
                       [&](auto &&...args) -> RetType {
                           return std::invoke(std::move(f), std::forward<decltype(args)>(args)...);
                       },
                       std::move(args)
                   );
               }
        );
        auto future = task->get_future();
       
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stop_) throw std::runtime_error("enqueue on stopped ThreadPool");
            tasks_.emplace([task]{(*task)();});
        }
        cv_.notify_one();
        return future;
    }

    size_t size() const noexcept {return workers_.size();}

private:
    std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Task> tasks_;
    std::vector<std::thread> workers_;
    bool stop_;

    void worker_loop(){
        while(true){
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                cv_.wait(lock, [this]{return stop_ or !tasks_.empty();});
                if(stop_ and tasks_.empty()) return;
                task = std::move(tasks_.front());
                tasks_.pop();
            }
            task();
        }
    }
};