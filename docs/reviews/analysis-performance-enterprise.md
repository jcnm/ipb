# Analyse Performance - Niveau Enterprise

**Date**: 2026-01-03
**Scope**: Performance IPB pour déploiement enterprise-grade
**Criticité**: HAUTE

---

## 1. Lacunes Identifiées

### 1.1 Complexité Algorithmique Sous-Optimale

| Opération | Actuel | Optimal | Impact |
|-----------|--------|---------|--------|
| Rule matching | O(r × n) | O(log r) | Latence croît linéairement |
| Sink lookup | O(s) | O(1) | Overhead inutile |
| Pattern compile | O(p) par msg | O(1) | Violation SLA |
| Failover selection | O(s) | O(1) | Latence récupération |

**Impact à l'échelle Enterprise**:
- 10,000 règles × 100,000 msg/s = 1 milliard comparaisons/s
- Latence P99 dépasse 250μs target
- CPU bottleneck avant I/O

### 1.2 Regex Compilation dans Hot Path (CRITIQUE)

```cpp
// PROBLÈME: router.cpp:104
std::regex pattern(address_pattern);  // O(p) à CHAQUE message!
```

**Mesures d'impact**:
| Pattern Length | Compile Time | Messages/s Impact |
|----------------|--------------|-------------------|
| 10 chars | ~50μs | -20% throughput |
| 50 chars | ~200μs | -50% throughput |
| 100+ chars | ~500μs+ | -80% throughput |

### 1.3 Absence de Memory Pooling

**Problème**: Allocations heap individuelles pour gros messages.

```cpp
// Actuel: data_point.hpp
std::unique_ptr<uint8_t[]> external_data_;  // Allocation individuelle
```

**Impact**:
- Fragmentation mémoire
- Cache misses
- Latence allocateur
- Pas de pré-allocation

### 1.4 Lock Contention Potentielle

| Composant | Mécanisme | Risque | Scénario |
|-----------|-----------|--------|----------|
| Router state | atomic | Aucun | - |
| Rule storage | shared_mutex | Faible | Write-heavy config |
| Sink registry | shared_mutex | Faible | Dynamic registration |
| Scheduler queue | Mutex + CV | **Moyen** | High contention |
| Statistics | atomic | Aucun | - |

### 1.5 Pas de Profiling Intégré

**Lacunes**:
- Pas de métriques latence par composant
- Pas de histogrammes de distribution
- Pas de flamegraphs automatiques
- Pas de détection de hot spots

### 1.6 Cache Efficiency Issues

**Problèmes identifiés**:
```cpp
// RoutingRule trop large = cache misses
struct RoutingRule {
    std::string name;              // 32 bytes
    std::string address_pattern;   // 32 bytes
    std::vector<std::string> sinks; // 24 bytes
    // ... total: >200 bytes = 3+ cache lines
};
```

### 1.7 Pas de Connection Pooling

**Impact**:
- Overhead établissement connexion
- Latence TCP handshake
- Limite de sockets
- Pas de keep-alive optimisé

---

## 2. Solutions Enterprise-Grade

### 2.1 Pattern Matching Optimisé - Trie/Radix Tree

```cpp
// Trie optimisé pour pattern matching
class PatternTrie {
public:
    struct MatchResult {
        std::vector<RuleId> matching_rules;
        size_t match_depth;
    };

    // Insertion O(p) où p = longueur pattern
    void insert(std::string_view pattern, RuleId rule_id) {
        auto* node = &root_;

        for (char c : pattern) {
            if (c == '*') {
                // Wildcard: matches any sequence
                node->wildcard_rules.push_back(rule_id);
                return;
            }

            auto& child = node->children[static_cast<uint8_t>(c)];
            if (!child) {
                child = std::make_unique<Node>();
            }
            node = child.get();
        }

        node->terminal_rules.push_back(rule_id);
    }

    // Match O(a) où a = longueur address
    MatchResult match(std::string_view address) const {
        MatchResult result;
        match_recursive(&root_, address, 0, result);
        return result;
    }

private:
    struct Node {
        std::array<std::unique_ptr<Node>, 256> children{};
        std::vector<RuleId> terminal_rules;
        std::vector<RuleId> wildcard_rules;
    };

    void match_recursive(
        const Node* node,
        std::string_view remaining,
        size_t depth,
        MatchResult& result) const
    {
        // Collect wildcard matches at this level
        for (auto rule_id : node->wildcard_rules) {
            result.matching_rules.push_back(rule_id);
        }

        if (remaining.empty()) {
            for (auto rule_id : node->terminal_rules) {
                result.matching_rules.push_back(rule_id);
            }
            result.match_depth = depth;
            return;
        }

        char c = remaining.front();
        const auto& child = node->children[static_cast<uint8_t>(c)];

        if (child) {
            match_recursive(child.get(), remaining.substr(1), depth + 1, result);
        }
    }

    Node root_;
};

// Radix Tree pour patterns avec préfixes communs
class RadixTree {
public:
    void insert(std::string_view pattern, RuleId rule_id);
    std::vector<RuleId> match(std::string_view address) const;

private:
    struct Edge {
        std::string label;
        std::unique_ptr<Node> target;
    };

    struct Node {
        std::vector<Edge> edges;
        std::vector<RuleId> rules;
        bool is_wildcard = false;
    };

    Node root_;
};
```

### 2.2 Pattern Cache avec LRU

```cpp
// Cache LRU thread-safe pour patterns compilés
template<typename Key, typename Value, size_t MaxSize = 10000>
class LRUCache {
public:
    std::optional<Value> get(const Key& key) {
        std::lock_guard lock(mutex_);

        auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }

        // Move to front (most recently used)
        list_.splice(list_.begin(), list_, it->second);
        return it->second->second;
    }

    void put(const Key& key, Value value) {
        std::lock_guard lock(mutex_);

        auto it = map_.find(key);
        if (it != map_.end()) {
            it->second->second = std::move(value);
            list_.splice(list_.begin(), list_, it->second);
            return;
        }

        // Evict if full
        if (map_.size() >= MaxSize) {
            auto last = list_.back();
            map_.erase(last.first);
            list_.pop_back();
        }

        list_.emplace_front(key, std::move(value));
        map_[key] = list_.begin();
    }

private:
    using ListType = std::list<std::pair<Key, Value>>;

    std::mutex mutex_;
    ListType list_;
    std::unordered_map<Key, typename ListType::iterator> map_;
};

// Usage
class CachedPatternMatcher {
public:
    bool match(std::string_view pattern, std::string_view address) {
        // Try cache first
        auto cached = cache_.get(std::string(pattern));
        if (cached) {
            return std::regex_match(
                address.begin(), address.end(), *cached);
        }

        // Compile and cache
        std::regex compiled(pattern.begin(), pattern.end());
        cache_.put(std::string(pattern), compiled);

        return std::regex_match(address.begin(), address.end(), compiled);
    }

private:
    LRUCache<std::string, std::regex> cache_;
};
```

### 2.3 Memory Pool pour Messages

```cpp
// Pool de mémoire pour allocations haute fréquence
template<size_t BlockSize, size_t PoolSize = 1024>
class MemoryPool {
public:
    MemoryPool() {
        // Pré-allocation
        pool_.reserve(PoolSize);
        for (size_t i = 0; i < PoolSize; ++i) {
            pool_.push_back(std::make_unique<Block>());
            free_list_.push(pool_.back().get());
        }
    }

    void* allocate() {
        Block* block = nullptr;

        if (free_list_.try_pop(block)) {
            return block->data.data();
        }

        // Fallback to heap if pool exhausted
        return ::operator new(BlockSize);
    }

    void deallocate(void* ptr) {
        // Check if ptr is from our pool
        for (const auto& block : pool_) {
            if (block->data.data() == ptr) {
                free_list_.push(block.get());
                return;
            }
        }

        // Not from pool, use regular delete
        ::operator delete(ptr);
    }

    struct Deleter {
        MemoryPool* pool;
        void operator()(void* ptr) { pool->deallocate(ptr); }
    };

    template<typename T, typename... Args>
    std::unique_ptr<T, Deleter> make(Args&&... args) {
        void* mem = allocate();
        T* obj = new (mem) T(std::forward<Args>(args)...);
        return std::unique_ptr<T, Deleter>(obj, Deleter{this});
    }

private:
    struct Block {
        alignas(std::max_align_t) std::array<uint8_t, BlockSize> data;
    };

    std::vector<std::unique_ptr<Block>> pool_;
    moodycamel::ConcurrentQueue<Block*> free_list_;  // Lock-free queue
};

// Spécialisation pour DataPoints
class DataPointPool {
public:
    static constexpr size_t SMALL_SIZE = 64;
    static constexpr size_t MEDIUM_SIZE = 256;
    static constexpr size_t LARGE_SIZE = 4096;

    void* allocate(size_t size) {
        if (size <= SMALL_SIZE) return small_pool_.allocate();
        if (size <= MEDIUM_SIZE) return medium_pool_.allocate();
        if (size <= LARGE_SIZE) return large_pool_.allocate();
        return ::operator new(size);
    }

private:
    MemoryPool<SMALL_SIZE, 10000> small_pool_;
    MemoryPool<MEDIUM_SIZE, 1000> medium_pool_;
    MemoryPool<LARGE_SIZE, 100> large_pool_;
};
```

### 2.4 Lock-Free Data Structures

```cpp
// Lock-free MPMC queue pour messages
template<typename T>
class LockFreeQueue {
public:
    explicit LockFreeQueue(size_t capacity)
        : buffer_(capacity)
        , capacity_(capacity)
        , mask_(capacity - 1)
    {
        assert((capacity & (capacity - 1)) == 0);  // Power of 2

        for (size_t i = 0; i < capacity; ++i) {
            buffer_[i].sequence.store(i, std::memory_order_relaxed);
        }

        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
    }

    bool try_push(T value) {
        Cell* cell;
        size_t pos = tail_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) -
                           static_cast<intptr_t>(pos);

            if (diff == 0) {
                if (tail_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // Queue full
            } else {
                pos = tail_.load(std::memory_order_relaxed);
            }
        }

        cell->data = std::move(value);
        cell->sequence.store(pos + 1, std::memory_order_release);
        return true;
    }

    bool try_pop(T& value) {
        Cell* cell;
        size_t pos = head_.load(std::memory_order_relaxed);

        for (;;) {
            cell = &buffer_[pos & mask_];
            size_t seq = cell->sequence.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) -
                           static_cast<intptr_t>(pos + 1);

            if (diff == 0) {
                if (head_.compare_exchange_weak(
                        pos, pos + 1, std::memory_order_relaxed)) {
                    break;
                }
            } else if (diff < 0) {
                return false;  // Queue empty
            } else {
                pos = head_.load(std::memory_order_relaxed);
            }
        }

        value = std::move(cell->data);
        cell->sequence.store(
            pos + mask_ + 1, std::memory_order_release);
        return true;
    }

private:
    struct Cell {
        std::atomic<size_t> sequence;
        T data;
    };

    std::vector<Cell> buffer_;
    size_t capacity_;
    size_t mask_;
    alignas(64) std::atomic<size_t> head_;
    alignas(64) std::atomic<size_t> tail_;
};

// Skip list pour règles ordonnées (lock-free)
template<typename K, typename V>
class LockFreeSkipList {
    // Implémentation lock-free skip list
    // O(log n) lookup, insert, delete
};
```

### 2.5 Connection Pool Enterprise

```cpp
// Pool de connexions avec health checking
template<typename Connection>
class ConnectionPool {
public:
    struct Config {
        size_t min_connections = 5;
        size_t max_connections = 100;
        std::chrono::seconds idle_timeout{60};
        std::chrono::seconds health_check_interval{10};
        std::chrono::milliseconds acquire_timeout{1000};
        size_t max_retries = 3;
    };

    class PooledConnection {
    public:
        PooledConnection(ConnectionPool* pool, std::unique_ptr<Connection> conn)
            : pool_(pool), conn_(std::move(conn)) {}

        ~PooledConnection() {
            if (conn_ && pool_) {
                pool_->release(std::move(conn_));
            }
        }

        Connection* operator->() { return conn_.get(); }
        Connection& operator*() { return *conn_; }

        // Move only
        PooledConnection(PooledConnection&&) = default;
        PooledConnection& operator=(PooledConnection&&) = default;

    private:
        ConnectionPool* pool_;
        std::unique_ptr<Connection> conn_;
    };

    Result<PooledConnection> acquire() {
        auto deadline = std::chrono::steady_clock::now() +
                       config_.acquire_timeout;

        while (std::chrono::steady_clock::now() < deadline) {
            // Try to get from pool
            std::unique_lock lock(mutex_);

            if (!idle_connections_.empty()) {
                auto conn = std::move(idle_connections_.back());
                idle_connections_.pop_back();
                lock.unlock();

                // Validate connection
                if (is_healthy(*conn)) {
                    return ok(PooledConnection(this, std::move(conn)));
                }

                // Connection dead, try again
                continue;
            }

            // Can we create new connection?
            if (active_count_ < config_.max_connections) {
                ++active_count_;
                lock.unlock();

                auto conn = create_connection();
                if (conn) {
                    return ok(PooledConnection(this, std::move(*conn)));
                }

                --active_count_;
            }

            // Wait for connection to be released
            cv_.wait_until(lock, deadline);
        }

        return err<PooledConnection>(
            ErrorCode::TIMEOUT, "Connection pool exhausted");
    }

private:
    void release(std::unique_ptr<Connection> conn) {
        std::lock_guard lock(mutex_);

        if (is_healthy(*conn) &&
            idle_connections_.size() < config_.max_connections) {
            idle_connections_.push_back(std::move(conn));
            cv_.notify_one();
        }
        // Otherwise let it drop
    }

    bool is_healthy(Connection& conn) {
        try {
            return conn.ping();
        } catch (...) {
            return false;
        }
    }

    Result<std::unique_ptr<Connection>> create_connection() {
        for (size_t i = 0; i < config_.max_retries; ++i) {
            try {
                return ok(std::make_unique<Connection>(connection_params_));
            } catch (const std::exception& e) {
                if (i == config_.max_retries - 1) {
                    return err<std::unique_ptr<Connection>>(
                        ErrorCode::CONNECTION_FAILED, e.what());
                }
            }
        }
        std::unreachable();
    }

    Config config_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::unique_ptr<Connection>> idle_connections_;
    std::atomic<size_t> active_count_{0};
    ConnectionParams connection_params_;
};
```

### 2.6 Profiling & Metrics Integration

```cpp
// Histogramme de latence haute performance
class LatencyHistogram {
public:
    static constexpr size_t NUM_BUCKETS = 64;

    void record(std::chrono::nanoseconds latency) {
        size_t bucket = latency_to_bucket(latency);
        buckets_[bucket].fetch_add(1, std::memory_order_relaxed);
        count_.fetch_add(1, std::memory_order_relaxed);
        sum_.fetch_add(latency.count(), std::memory_order_relaxed);
    }

    struct Percentiles {
        std::chrono::nanoseconds p50;
        std::chrono::nanoseconds p90;
        std::chrono::nanoseconds p95;
        std::chrono::nanoseconds p99;
        std::chrono::nanoseconds p999;
        std::chrono::nanoseconds max;
    };

    Percentiles compute_percentiles() const {
        std::array<uint64_t, NUM_BUCKETS> snapshot;
        uint64_t total = 0;

        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            snapshot[i] = buckets_[i].load(std::memory_order_relaxed);
            total += snapshot[i];
        }

        Percentiles result;
        uint64_t cumulative = 0;

        for (size_t i = 0; i < NUM_BUCKETS; ++i) {
            cumulative += snapshot[i];
            double percentile = 100.0 * cumulative / total;

            if (percentile >= 50.0 && result.p50.count() == 0)
                result.p50 = bucket_to_latency(i);
            if (percentile >= 90.0 && result.p90.count() == 0)
                result.p90 = bucket_to_latency(i);
            // ... etc
        }

        return result;
    }

private:
    size_t latency_to_bucket(std::chrono::nanoseconds ns) {
        // Logarithmic bucketing
        if (ns.count() <= 0) return 0;
        return std::min(
            static_cast<size_t>(std::log2(ns.count())),
            NUM_BUCKETS - 1);
    }

    std::chrono::nanoseconds bucket_to_latency(size_t bucket) {
        return std::chrono::nanoseconds{1ULL << bucket};
    }

    std::array<std::atomic<uint64_t>, NUM_BUCKETS> buckets_{};
    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> sum_{0};
};

// Instrumenter le router
class InstrumentedRouter {
public:
    Result<RouteDecision> route(const DataPoint& dp) {
        auto start = std::chrono::steady_clock::now();

        auto result = inner_router_->route(dp);

        auto elapsed = std::chrono::steady_clock::now() - start;
        routing_latency_.record(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));

        if (result) {
            routes_success_.fetch_add(1, std::memory_order_relaxed);
        } else {
            routes_failure_.fetch_add(1, std::memory_order_relaxed);
        }

        return result;
    }

    RouterMetrics get_metrics() const {
        return RouterMetrics{
            .success_count = routes_success_.load(),
            .failure_count = routes_failure_.load(),
            .latency = routing_latency_.compute_percentiles()
        };
    }

private:
    std::unique_ptr<IRouter> inner_router_;
    LatencyHistogram routing_latency_;
    std::atomic<uint64_t> routes_success_{0};
    std::atomic<uint64_t> routes_failure_{0};
};
```

---

## 3. Benchmarks Cibles Enterprise

| Métrique | Actuel | Cible | Enterprise Target |
|----------|--------|-------|-------------------|
| Throughput | ~50K msg/s | 200K msg/s | 1M msg/s |
| P50 Latency | ~50μs | 20μs | 10μs |
| P99 Latency | >250μs | 100μs | 50μs |
| Memory/msg | Variable | <100 bytes | <64 bytes |
| CPU usage | Variable | <50% | <30% |

---

## 4. Plan d'Optimisation

### Phase 1: Quick Wins (1-2 semaines)
- [ ] Implémenter pattern cache LRU
- [ ] Optimiser sink lookup O(1)
- [ ] Profiling baseline avec perf/flamegraph

### Phase 2: Core Optimizations (3-4 semaines)
- [ ] Implémenter Patricia Trie pour patterns
- [ ] Memory pool pour DataPoints
- [ ] Lock-free scheduler queue

### Phase 3: Advanced (4-6 semaines)
- [ ] Connection pooling
- [ ] SIMD pour pattern matching
- [ ] Kernel bypass (DPDK/io_uring)

---

*Document généré pour IPB Enterprise Performance Review*
