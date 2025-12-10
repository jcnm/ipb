#include <ipb/router/edf_scheduler.hpp>
#include <algorithm>

namespace ipb {
namespace router {

EDFScheduler::EDFScheduler() 
    : running_(false)
{
}

EDFScheduler::~EDFScheduler() {
    stop();
}

bool EDFScheduler::start() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (running_) {
        return true;
    }
    
    running_ = true;
    scheduler_thread_ = std::thread(&EDFScheduler::scheduler_loop, this);
    
    return true;
}

void EDFScheduler::stop() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        running_ = false;
    }
    
    condition_.notify_all();
    
    if (scheduler_thread_.joinable()) {
        scheduler_thread_.join();
    }
}

void EDFScheduler::schedule_task(const ScheduledTask& task) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // Insert task in deadline order (EDF)
    auto it = std::upper_bound(task_queue_.begin(), task_queue_.end(), task,
        [](const ScheduledTask& a, const ScheduledTask& b) {
            return a.deadline < b.deadline;
        });
    
    task_queue_.insert(it, task);
    condition_.notify_one();
}

void EDFScheduler::scheduler_loop() {
    while (true) {
        ScheduledTask task;
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            // Wait for task or stop signal
            condition_.wait(lock, [this] { return !task_queue_.empty() || !running_; });
            
            if (!running_) {
                break;
            }
            
            if (!task_queue_.empty()) {
                auto now = std::chrono::steady_clock::now();
                
                // Check if the earliest deadline task is ready
                if (task_queue_.front().deadline <= now) {
                    task = task_queue_.front();
                    task_queue_.erase(task_queue_.begin());
                    has_task = true;
                } else {
                    // Wait until the next deadline
                    auto wait_time = task_queue_.front().deadline - now;
                    condition_.wait_for(lock, wait_time);
                    continue;
                }
            }
        }
        
        if (has_task) {
            execute_task(task);
        }
    }
}

void EDFScheduler::execute_task(const ScheduledTask& task) {
    auto start_time = std::chrono::steady_clock::now();
    
    try {
        if (task.callback) {
            task.callback();
        }
        
        // Update statistics
        auto end_time = std::chrono::steady_clock::now();
        auto execution_time = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
        
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.tasks_executed++;
        stats_.total_execution_time_us += execution_time.count();
        
        // Check for deadline miss
        if (end_time > task.deadline) {
            stats_.deadline_misses++;
        }
        
    } catch (const std::exception& e) {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.task_failures++;
    }
}

EDFStatistics EDFScheduler::get_statistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    
    EDFStatistics stats = stats_;
    
    if (stats_.tasks_executed > 0) {
        stats.average_execution_time_us = stats_.total_execution_time_us / stats_.tasks_executed;
    }
    
    return stats;
}

} // namespace router
} // namespace ipb

