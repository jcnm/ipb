#include <ipb/common/rt_primitives.hpp>
#include <thread>
#include <chrono>

#ifdef __linux__
#include <sched.h>
#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace ipb {
namespace common {
namespace rt {

bool set_thread_priority(std::thread& thread, ThreadPriority priority) {
    #ifdef __linux__
    pthread_t native_handle = thread.native_handle();
    
    struct sched_param param;
    int policy;
    
    switch (priority) {
        case ThreadPriority::LOW:
            policy = SCHED_OTHER;
            param.sched_priority = 0;
            break;
        case ThreadPriority::NORMAL:
            policy = SCHED_OTHER;
            param.sched_priority = 0;
            break;
        case ThreadPriority::HIGH:
            policy = SCHED_FIFO;
            param.sched_priority = 50;
            break;
        case ThreadPriority::REALTIME:
            policy = SCHED_FIFO;
            param.sched_priority = 99;
            break;
        default:
            return false;
    }
    
    return pthread_setschedparam(native_handle, policy, &param) == 0;
    #else
    // Non-Linux platforms - basic implementation
    return true;
    #endif
}

bool set_thread_affinity(std::thread& thread, const std::vector<int>& cpu_cores) {
    #ifdef __linux__
    pthread_t native_handle = thread.native_handle();
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    
    for (int core : cpu_cores) {
        if (core >= 0 && core < CPU_SETSIZE) {
            CPU_SET(core, &cpuset);
        }
    }
    
    return pthread_setaffinity_np(native_handle, sizeof(cpu_set_t), &cpuset) == 0;
    #else
    // Non-Linux platforms
    return true;
    #endif
}

bool lock_memory() {
    #ifdef __linux__
    return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
    #else
    return true;
    #endif
}

bool unlock_memory() {
    #ifdef __linux__
    return munlockall() == 0;
    #else
    return true;
    #endif
}

void precise_sleep(std::chrono::nanoseconds duration) {
    auto start = std::chrono::high_resolution_clock::now();
    auto end = start + duration;
    
    // For very short durations, use busy wait
    if (duration < std::chrono::microseconds(100)) {
        while (std::chrono::high_resolution_clock::now() < end) {
            // Busy wait
        }
        return;
    }
    
    // For longer durations, use a combination of sleep and busy wait
    auto sleep_duration = duration - std::chrono::microseconds(50);
    if (sleep_duration > std::chrono::nanoseconds(0)) {
        std::this_thread::sleep_for(sleep_duration);
    }
    
    // Busy wait for the remaining time
    while (std::chrono::high_resolution_clock::now() < end) {
        // Busy wait
    }
}

uint64_t get_cpu_cycles() {
    #ifdef __x86_64__
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
    #else
    // Fallback to high resolution clock
    auto now = std::chrono::high_resolution_clock::now();
    return static_cast<uint64_t>(now.time_since_epoch().count());
    #endif
}

double get_cpu_frequency_ghz() {
    // Simple estimation - measure cycles over a known time period
    auto start_cycles = get_cpu_cycles();
    auto start_time = std::chrono::high_resolution_clock::now();
    
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto end_cycles = get_cpu_cycles();
    auto end_time = std::chrono::high_resolution_clock::now();
    
    auto duration_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time - start_time).count();
    auto cycles_diff = end_cycles - start_cycles;
    
    return static_cast<double>(cycles_diff) / duration_ns;
}

// Memory pool implementation
MemoryPool::MemoryPool(size_t block_size, size_t num_blocks)
    : block_size_(block_size)
    , num_blocks_(num_blocks)
    , memory_(nullptr)
    , free_blocks_()
{
    // Allocate aligned memory
    size_t total_size = block_size_ * num_blocks_;
    
    #ifdef __linux__
    if (posix_memalign(&memory_, 64, total_size) != 0) {
        memory_ = nullptr;
        return;
    }
    #else
    memory_ = std::aligned_alloc(64, total_size);
    #endif
    
    if (!memory_) {
        return;
    }
    
    // Initialize free block list
    char* ptr = static_cast<char*>(memory_);
    for (size_t i = 0; i < num_blocks_; ++i) {
        free_blocks_.push(ptr + i * block_size_);
    }
}

MemoryPool::~MemoryPool() {
    if (memory_) {
        std::free(memory_);
    }
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (free_blocks_.empty()) {
        return nullptr;
    }
    
    void* ptr = free_blocks_.top();
    free_blocks_.pop();
    return ptr;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(mutex_);
    free_blocks_.push(ptr);
}

bool MemoryPool::is_valid() const {
    return memory_ != nullptr;
}

size_t MemoryPool::available_blocks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return free_blocks_.size();
}

} // namespace rt
} // namespace common
} // namespace ipb

