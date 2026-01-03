# Analyse Testing - Niveau Enterprise

**Date**: 2026-01-03
**Scope**: Stratégie de test IPB pour niveau enterprise
**Criticité**: HAUTE

---

## 1. État Actuel des Tests

### 1.1 Couverture Existante

| Suite de Tests | Nombre | Status | Type |
|----------------|--------|--------|------|
| test_data_point | 45 | ✅ PASS | Unit |
| test_error | 46 | ✅ PASS | Unit |
| test_endpoint | 57 | ✅ PASS | Unit |
| test_scheduler | 31 | ✅ PASS | Unit |
| test_message_bus | 31 | ✅ PASS | Unit |
| test_rule_engine | 43 | ✅ PASS | Unit |
| test_sink_registry | 49 | ✅ PASS | Unit |
| test_scoop_registry | 58 | ✅ PASS | Unit |
| test_router | 52 | ✅ PASS | Unit |
| **Total** | **412** | **ALL PASS** | - |

### 1.2 Lacunes Identifiées

#### Tests Manquants Critiques

| Catégorie | Status | Impact |
|-----------|--------|--------|
| Tests de concurrence/stress | ❌ Absent | Race conditions non détectées |
| Tests ReDoS | ❌ Absent | Vulnérabilité non testée |
| Tests de pression mémoire | ❌ Absent | OOM non détecté |
| Tests deadline miss | ❌ Absent | SLA violations non testées |
| Tests network failure | ❌ Absent | Resilience non validée |
| Tests d'intégration | ❌ Absent | Interactions non validées |
| Tests E2E | ❌ Absent | Scénarios réels non couverts |
| Tests de performance | ❌ Absent | Régressions non détectées |
| Tests de sécurité | ❌ Absent | Vulnérabilités non testées |
| Property-based tests | ❌ Absent | Edge cases non explorés |
| Mutation testing | ❌ Absent | Qualité des tests non mesurée |
| Chaos engineering | ❌ Absent | Resilience non prouvée |

---

## 2. Solutions Enterprise-Grade

### 2.1 Tests de Concurrence

```cpp
// tests/stress/test_concurrent_router.cpp
#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <latch>

class ConcurrentRouterTest : public ::testing::Test {
protected:
    static constexpr size_t NUM_THREADS = 16;
    static constexpr size_t MESSAGES_PER_THREAD = 100000;

    std::unique_ptr<Router> router_;

    void SetUp() override {
        router_ = RouterFactory::create(RouterConfig{});

        // Add 1000 rules
        for (int i = 0; i < 1000; ++i) {
            router_->add_rule(RuleBuilder()
                .name(fmt::format("rule_{}", i))
                .pattern(fmt::format("sensors/{}/.*", i))
                .sink("test_sink")
                .build());
        }

        router_->start();
    }
};

TEST_F(ConcurrentRouterTest, HighConcurrencyRouting) {
    std::atomic<uint64_t> success_count{0};
    std::atomic<uint64_t> failure_count{0};
    std::latch start_latch(NUM_THREADS);

    std::vector<std::thread> threads;
# Analyse Testing - Enterprise Grade

**Projet**: IPB (Industrial Protocol Bridge)
**Date d'analyse**: 2024-12-12
**Focus**: Lacunes testing et solutions pour niveau entreprise

---

## 1. Résumé Exécutif

| Aspect | État Actuel | Niveau Enterprise Requis | Gap |
|--------|-------------|--------------------------|-----|
| Tests unitaires | 8/10 | 9/10 | Faible |
| Tests d'intégration | 4/10 | 9/10 | **Critique** |
| Tests de performance | 2/10 | 8/10 | **Critique** |
| Tests de sécurité | 1/10 | 9/10 | **Critique** |
| Tests de charge | 2/10 | 8/10 | **Critique** |
| Couverture code | ~70% | > 85% | Modéré |
| Tests de chaos | 0/10 | 7/10 | **Critique** |

**Score Testing Global: 8.0/10** (unitaires seulement)

---

## 2. État Actuel des Tests

### 2.1 Vue d'ensemble

| Suite de tests | Nombre | Status | Couverture estimée |
|----------------|--------|--------|-------------------|
| test_data_point | 45 | ✅ PASS | 85% |
| test_error | 46 | ✅ PASS | 90% |
| test_endpoint | 57 | ✅ PASS | 80% |
| test_scheduler | 31 | ✅ PASS | 75% |
| test_message_bus | 31 | ✅ PASS | 70% |
| test_rule_engine | 43 | ✅ PASS | 75% |
| test_sink_registry | 49 | ✅ PASS | 80% |
| test_scoop_registry | 58 | ✅ PASS | 80% |
| test_router | 52 | ✅ PASS | 65% |
| **Total** | **412** | **ALL PASS** | **~75%** |

### 2.2 Points Forts

- ✅ Bonne couverture des cas nominaux
- ✅ Tests de validation des règles de routing
- ✅ Tests des structures de données (DataPoint, Value)
- ✅ Framework Catch2 bien utilisé
- ✅ Tests paramétrés pour les types Value

### 2.3 Points Faibles Identifiés

- ❌ Pas de tests de concurrence/stress
- ❌ Pas de tests de sécurité (fuzzing, injection)
- ❌ Pas de tests de performance/benchmark
- ❌ Pas de tests d'intégration E2E
- ❌ Pas de tests de chaos engineering
- ❌ Pas de mutation testing
- ❌ Edge cases regex non testés (ReDoS)

---

## 3. Lacunes Critiques

### 3.1 CRITIQUE: Absence de Tests de Concurrence

**Constat:**
- Pas de tests multi-threadés
- Race conditions non détectées
- Thread-safety non validée

**Impact:**
- Bugs concurrence en production
- Data races non détectées
- Deadlocks potentiels

**Solution Recommandée:**

```cpp
// tests/stress/test_concurrent_router.cpp
#include <catch2/catch_all.hpp>
#include <thread>
#include <latch>

TEST_CASE("Router handles concurrent routing", "[stress][concurrency]") {
    Router router;
    setup_test_rules(router, 100);

    constexpr size_t NUM_THREADS = 16;
    constexpr size_t OPS_PER_THREAD = 10000;

    std::latch start_latch(NUM_THREADS);
    std::atomic<size_t> success_count{0};
    std::atomic<size_t> error_count{0};
    std::vector<std::jthread> threads;

    for (size_t t = 0; t < NUM_THREADS; ++t) {
        threads.emplace_back([&, t]() {
            start_latch.arrive_and_wait();  // Synchronize start

            for (size_t i = 0; i < MESSAGES_PER_THREAD; ++i) {
                auto dp = DataPoint::create(
                    fmt::format("sensors/{}/temperature", t % 1000),
                    Value::from(42.5));

                auto result = router_->route(dp);

                if (result) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    failure_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    EXPECT_EQ(success_count.load(), NUM_THREADS * MESSAGES_PER_THREAD);
    EXPECT_EQ(failure_count.load(), 0);
}

TEST_F(ConcurrentRouterTest, ConcurrentRuleModification) {
    std::atomic<bool> running{true};
    std::atomic<uint64_t> route_count{0};
    std::atomic<uint64_t> add_count{0};
    std::atomic<uint64_t> remove_count{0};

    // Reader threads
    std::vector<std::thread> readers;
    for (size_t t = 0; t < 8; ++t) {
        readers.emplace_back([&]() {
            while (running.load(std::memory_order_relaxed)) {
                auto dp = DataPoint::create("sensors/0/temp", Value::from(1));
                router_->route(dp);
                route_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Writer threads
    std::vector<std::thread> writers;
    for (size_t t = 0; t < 4; ++t) {
        writers.emplace_back([&, t]() {
            for (int i = 0; i < 1000; ++i) {
                auto rule_name = fmt::format("dynamic_rule_{}_{}", t, i);

                router_->add_rule(RuleBuilder()
                    .name(rule_name)
                    .pattern("dynamic/.*")
                    .sink("dynamic_sink")
                    .build());
                add_count.fetch_add(1, std::memory_order_relaxed);

                std::this_thread::yield();

                router_->remove_rule(rule_name);
                remove_count.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    for (auto& w : writers) {
        w.join();
    }

    running.store(false);

    for (auto& r : readers) {
        r.join();
    }

    EXPECT_GT(route_count.load(), 0);
    EXPECT_EQ(add_count.load(), 4 * 1000);
    EXPECT_EQ(remove_count.load(), 4 * 1000);
}

TEST_F(ConcurrentRouterTest, ThreadSanitizer) {
    // Ce test est conçu pour être exécuté avec -fsanitize=thread
    // Il expose les data races potentielles

    std::vector<std::thread> threads;

    for (size_t t = 0; t < 32; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10000; ++i) {
                auto dp = DataPoint::create("test/addr", Value::from(i));
                router_->route(dp);

                if (i % 100 == 0) {
                    auto stats = router_->statistics();
                    (void)stats;  // Just access stats
                }
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
}
```

### 2.2 Tests ReDoS

```cpp
// tests/security/test_redos.cpp
#include <gtest/gtest.h>
#include <chrono>

class ReDoSTest : public ::testing::Test {
protected:
    std::unique_ptr<PatternMatcher> matcher_;

    static constexpr auto MAX_MATCH_TIME = std::chrono::milliseconds{100};
};

TEST_F(ReDoSTest, ExponentialBacktrackingPattern) {
    // Pattern classique de ReDoS
    std::string evil_pattern = "(a+)+b";
    std::string evil_input(30, 'a');
    evil_input += '!';  // Non-match qui force backtracking

    auto start = std::chrono::steady_clock::now();

    // Ce test DOIT échouer rapidement (timeout ou rejet)
    auto result = matcher_->match(evil_pattern, evil_input);

    auto elapsed = std::chrono::steady_clock::now() - start;

    // DOIT terminer en moins de 100ms
    EXPECT_LT(elapsed, MAX_MATCH_TIME)
        << "Pattern matching took too long: potential ReDoS vulnerability";
}

TEST_F(ReDoSTest, NestedQuantifiersRejected) {
    std::vector<std::string> dangerous_patterns = {
        "(a+)+",
        "(a*)+",
        "(a+)*",
        "(a*)*",
        "((a+)|(b+))+",
        "(a|a)+",
        "([a-zA-Z]+)*",
    };

    for (const auto& pattern : dangerous_patterns) {
        // Pattern dangereux DOIT être rejeté ou timeout
        auto result = matcher_->validate_pattern(pattern);

        EXPECT_FALSE(result.is_safe())
            << "Dangerous pattern not rejected: " << pattern;
    }
}

TEST_F(ReDoSTest, LongPatternLimits) {
    // Pattern très long
    std::string long_pattern(10000, 'a');
    long_pattern += ".*";

    auto result = matcher_->validate_pattern(long_pattern);

    EXPECT_FALSE(result.is_valid())
        << "Excessively long pattern should be rejected";
}

TEST_F(ReDoSTest, MatchTimeoutEnforced) {
    // Même pour patterns "safe", le match doit avoir un timeout
    std::string pattern = ".*";
    std::string very_long_input(1000000, 'a');

    auto start = std::chrono::steady_clock::now();
    auto result = matcher_->match(pattern, very_long_input);
    auto elapsed = std::chrono::steady_clock::now() - start;

    // Doit terminer dans un délai raisonnable
    EXPECT_LT(elapsed, std::chrono::seconds{1});
}
```

### 2.3 Tests de Pression Mémoire

```cpp
// tests/stress/test_memory_pressure.cpp
#include <gtest/gtest.h>
#include <sys/resource.h>

class MemoryPressureTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Limiter la mémoire disponible pour le test
        struct rlimit limit;
        limit.rlim_cur = 100 * 1024 * 1024;  // 100MB
        limit.rlim_max = 100 * 1024 * 1024;
        setrlimit(RLIMIT_AS, &limit);
    }

    void TearDown() override {
        // Restaurer les limites
        struct rlimit limit;
        limit.rlim_cur = RLIM_INFINITY;
        limit.rlim_max = RLIM_INFINITY;
        setrlimit(RLIMIT_AS, &limit);
    }
};

TEST_F(MemoryPressureTest, GracefulDegradationUnderPressure) {
    Router router;

    size_t success_count = 0;
    size_t oom_count = 0;

    // Essayer de créer beaucoup de gros messages
    for (size_t i = 0; i < 100000; ++i) {
        try {
            // Message avec gros payload
            std::vector<uint8_t> large_payload(10 * 1024);  // 10KB
            std::fill(large_payload.begin(), large_payload.end(),
                      static_cast<uint8_t>(i));

            auto dp = DataPoint::create(
                fmt::format("test/{}", i),
                Value::from_bytes(large_payload));

            auto result = router.route(dp);

            if (result) {
                ++success_count;
            }
        } catch (const std::bad_alloc&) {
            ++oom_count;
            // Router doit rester fonctionnel après OOM
        }
    }

    // Le router doit avoir traité au moins quelques messages
    EXPECT_GT(success_count, 0);

    // Et doit toujours fonctionner après les OOM
    auto small_dp = DataPoint::create("test/small", Value::from(42));
    auto result = router.route(small_dp);
    EXPECT_TRUE(result) << "Router should recover after OOM";
}

TEST_F(MemoryPressureTest, MemoryLeakDetection) {
    // Ce test vérifie l'absence de memory leaks
    // Exécuter avec valgrind ou AddressSanitizer

    Router router;

    for (size_t iteration = 0; iteration < 10; ++iteration) {
        // Ajouter et supprimer beaucoup de règles
        for (size_t i = 0; i < 1000; ++i) {
            router.add_rule(RuleBuilder()
                .name(fmt::format("rule_{}", i))
                .pattern(fmt::format("test/{}/.*", i))
                .sink("sink")
                .build());
        }

        for (size_t i = 0; i < 1000; ++i) {
            router.remove_rule(fmt::format("rule_{}", i));
        }
    }

    // La mémoire utilisée devrait être stable
    // (vérifié par les outils externes)
}
```

### 2.4 Tests de Deadline Miss

```cpp
// tests/realtime/test_deadline.cpp
#include <gtest/gtest.h>

class DeadlineTest : public ::testing::Test {
protected:
    static constexpr auto DEADLINE_THRESHOLD = std::chrono::microseconds{45};
    static constexpr size_t SAMPLE_SIZE = 10000;
};

TEST_F(DeadlineTest, P99LatencyWithinBudget) {
    Router router;
    router.add_rule(RuleBuilder()
        .name("test")
        .pattern(".*")
        .sink("test_sink")
        .build());

    std::vector<std::chrono::nanoseconds> latencies;
    latencies.reserve(SAMPLE_SIZE);

    for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
        auto dp = DataPoint::create("test/addr", Value::from(i));

        auto start = std::chrono::steady_clock::now();
        auto result = router.route(dp);
        auto elapsed = std::chrono::steady_clock::now() - start;

        latencies.push_back(
            std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed));
    }

    // Calculer P99
    std::sort(latencies.begin(), latencies.end());
    size_t p99_index = static_cast<size_t>(SAMPLE_SIZE * 0.99);
    auto p99 = latencies[p99_index];

    EXPECT_LT(p99, DEADLINE_THRESHOLD)
        << "P99 latency " << p99.count() << "ns exceeds deadline "
        << DEADLINE_THRESHOLD.count() << "us";
}

TEST_F(DeadlineTest, NoLatencySpikes) {
    Router router;
    router.add_rule(RuleBuilder()
        .name("test")
        .pattern(".*")
        .sink("test_sink")
        .build());

    auto max_spike = std::chrono::nanoseconds{0};
    auto spike_threshold = std::chrono::microseconds{250};  // P99.9 target
    size_t spike_count = 0;

    for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
        auto dp = DataPoint::create("test/addr", Value::from(i));

        auto start = std::chrono::steady_clock::now();
        auto result = router.route(dp);
        auto elapsed = std::chrono::steady_clock::now() - start;

        if (elapsed > max_spike) {
            max_spike = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);
        }

        if (elapsed > spike_threshold) {
            ++spike_count;
        }
    }

    // Pas plus de 0.1% de spikes
    EXPECT_LT(spike_count, SAMPLE_SIZE / 1000)
        << "Too many latency spikes: " << spike_count;
}

TEST_F(DeadlineTest, EDFSchedulerDeadlineEnforcement) {
    EDFScheduler scheduler;

    std::atomic<size_t> deadline_misses{0};
    std::atomic<size_t> completed{0};

    // Soumettre des tâches avec deadlines stricts
    for (size_t i = 0; i < 1000; ++i) {
        auto deadline = std::chrono::steady_clock::now() +
                       std::chrono::microseconds{100};

        scheduler.schedule([&, deadline]() {
            auto now = std::chrono::steady_clock::now();
            if (now > deadline) {
                deadline_misses.fetch_add(1, std::memory_order_relaxed);
            }
            completed.fetch_add(1, std::memory_order_relaxed);
        }, deadline);
    }

    scheduler.run_until_empty();

    EXPECT_EQ(completed.load(), 1000);
    EXPECT_LT(deadline_misses.load(), 10)  // <1% miss rate
        << "Too many deadline misses: " << deadline_misses.load();
}
```

### 2.5 Tests de Network Failure

```cpp
// tests/resilience/test_network_failure.cpp
#include <gtest/gtest.h>

class NetworkFailureTest : public ::testing::Test {
protected:
    // Mock network avec injection de fautes
    class FaultyNetwork : public INetwork {
    public:
        void set_failure_mode(FailureMode mode) { mode_ = mode; }
        void set_failure_rate(double rate) { failure_rate_ = rate; }

        Result<void> send(const Message& msg) override {
            if (should_fail()) {
                switch (mode_) {
                    case FailureMode::TIMEOUT:
                        std::this_thread::sleep_for(std::chrono::seconds{30});
                        return err<void>(ErrorCode::TIMEOUT, "Network timeout");
                    case FailureMode::CONNECTION_RESET:
                        return err<void>(ErrorCode::CONNECTION_RESET, "Connection reset");
                    case FailureMode::PARTIAL_WRITE:
                        // Simulate partial write then failure
                        return err<void>(ErrorCode::PARTIAL_WRITE, "Partial write");
                    default:
                        return err<void>(ErrorCode::NETWORK_ERROR, "Network error");
                }
            }
            return inner_->send(msg);
        }

    private:
        bool should_fail() {
            return distribution_(rng_) < failure_rate_;
        }

        FailureMode mode_ = FailureMode::RANDOM;
        double failure_rate_ = 0.0;
        std::mt19937 rng_{std::random_device{}()};
        std::uniform_real_distribution<> distribution_{0.0, 1.0};
        std::unique_ptr<INetwork> inner_;
    };
};

TEST_F(NetworkFailureTest, RetryOnTransientFailure) {
    FaultyNetwork network;
    network.set_failure_mode(FailureMode::CONNECTION_RESET);
    network.set_failure_rate(0.5);  // 50% failure rate

    Router router(&network);
    router.set_retry_policy(RetryPolicy{
        .max_retries = 3,
        .backoff = ExponentialBackoff{
            .initial = std::chrono::milliseconds{10},
            .max = std::chrono::seconds{1},
            .multiplier = 2.0
        }
    });

    size_t success_count = 0;

    for (size_t i = 0; i < 100; ++i) {
        auto dp = DataPoint::create("test", Value::from(i));
        auto result = router.route(dp);

        if (result) {
            ++success_count;
        }
    }

    // Avec retries, on devrait avoir un bon taux de succès malgré 50% failures
    EXPECT_GT(success_count, 80);  // >80% should succeed with retries
}

TEST_F(NetworkFailureTest, CircuitBreakerOpens) {
    FaultyNetwork network;
    network.set_failure_mode(FailureMode::TIMEOUT);
    network.set_failure_rate(1.0);  // 100% failure

    Router router(&network);
    router.set_circuit_breaker(CircuitBreakerConfig{
        .failure_threshold = 5,
        .open_duration = std::chrono::seconds{30}
    });

    // First 5 calls should try and fail
    for (size_t i = 0; i < 5; ++i) {
        auto dp = DataPoint::create("test", Value::from(i));
        auto result = router.route(dp);
        EXPECT_FALSE(result);
        EXPECT_EQ(result.error().code(), ErrorCode::TIMEOUT);
    }

    // 6th call should fail fast (circuit open)
    auto dp = DataPoint::create("test", Value::from(99));
    auto start = std::chrono::steady_clock::now();
    auto result = router.route(dp);
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(result);
    EXPECT_EQ(result.error().code(), ErrorCode::CIRCUIT_OPEN);
    EXPECT_LT(elapsed, std::chrono::milliseconds{10});  // Fast fail
}

TEST_F(NetworkFailureTest, FailoverToSecondary) {
    FaultyNetwork primary;
    primary.set_failure_rate(1.0);  // Primary always fails

    MockNetwork secondary;  // Secondary works fine

    Router router;
    router.add_sink(SinkBuilder()
        .name("primary")
        .network(&primary)
        .priority(1)
        .build());
    router.add_sink(SinkBuilder()
        .name("secondary")
        .network(&secondary)
        .priority(2)  // Lower priority = fallback
        .build());

    auto dp = DataPoint::create("test", Value::from(42));
    auto result = router.route(dp);

    EXPECT_TRUE(result);
    EXPECT_EQ(secondary.received_count(), 1);
}
```

### 2.6 Property-Based Testing

```cpp
// tests/property/test_datapoint_properties.cpp
#include <rapidcheck.h>
#include <gtest/gtest.h>

RC_GTEST_PROP(DataPointProperties, SerializeDeserializeRoundtrip, ()) {
    // Génère des DataPoints aléatoires
    auto address = *rc::gen::arbitrary<std::string>();
    auto value = *rc::gen::oneOf(
        rc::gen::map(rc::gen::arbitrary<int32_t>(), [](int32_t v) {
            return Value::from(v);
        }),
        rc::gen::map(rc::gen::arbitrary<double>(), [](double v) {
            return Value::from(v);
        }),
        rc::gen::map(rc::gen::arbitrary<std::string>(), [](std::string v) {
            return Value::from(v);
        })
    );

    auto original = DataPoint::create(address, value);

    // Serialize
    auto serialized = original.serialize();

    // Deserialize
    auto result = DataPoint::deserialize(serialized);
    RC_ASSERT(result.has_value());

    // Should be equal
    RC_ASSERT(result->address() == original.address());
    RC_ASSERT(result->value() == original.value());
}

RC_GTEST_PROP(RouterProperties, RouteMatchesPatterns, ()) {
    Router router;

    // Génère des patterns et addresses
    auto pattern = *rc::gen::oneOf(
        rc::gen::just(std::string("*")),
        rc::gen::just(std::string("test/*")),
        rc::gen::map(rc::gen::arbitrary<std::string>(), [](std::string s) {
            return s + "/*";
        })
    );

    router.add_rule(RuleBuilder()
        .name("test")
        .pattern(pattern)
        .sink("sink")
        .build());

    // Génère une address qui devrait matcher
    auto matching_address = *rc::gen::suchThat(
        rc::gen::arbitrary<std::string>(),
        [&pattern](const std::string& addr) {
            return matches_pattern(pattern, addr);
        }
    );

    auto dp = DataPoint::create(matching_address, Value::from(42));
    auto result = router.route(dp);

    RC_ASSERT(result.has_value());
}

RC_GTEST_PROP(ValueProperties, ComparisonConsistent, ()) {
    auto a = *rc::gen::arbitrary<int32_t>();
    auto b = *rc::gen::arbitrary<int32_t>();

    Value va = Value::from(a);
    Value vb = Value::from(b);

    // Consistency: a < b iff !(a >= b)
    RC_ASSERT((va < vb) == !(va >= vb));

    // Antisymmetry: if a < b then !(b < a)
    if (va < vb) {
        RC_ASSERT(!(vb < va));
    }

    // Transitivity: if a < b and b < c then a < c
    auto c = *rc::gen::arbitrary<int32_t>();
    Value vc = Value::from(c);

    if (va < vb && vb < vc) {
        RC_ASSERT(va < vc);
    }
}
```

### 2.7 Configuration CI/CD Complète

```yaml
# .github/workflows/enterprise-testing.yml
name: Enterprise Test Suite

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]
  schedule:
    - cron: '0 2 * * *'  # Nightly full test

env:
  BUILD_TYPE: Release
            for (size_t i = 0; i < OPS_PER_THREAD; ++i) {
                auto dp = create_test_datapoint(t, i);
                auto result = router.route(dp);
                if (result) {
                    success_count.fetch_add(1, std::memory_order_relaxed);
                } else {
                    error_count.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    threads.clear();  // Join all

    CHECK(success_count.load() == NUM_THREADS * OPS_PER_THREAD);
    CHECK(error_count.load() == 0);
}

TEST_CASE("MessageBus handles concurrent pub/sub", "[stress][concurrency]") {
    MessageBus bus;
    constexpr size_t NUM_PUBLISHERS = 8;
    constexpr size_t NUM_SUBSCRIBERS = 8;
    constexpr size_t MESSAGES_PER_PUBLISHER = 1000;

    std::atomic<size_t> received_count{0};
    std::vector<SubscriptionId> sub_ids;

    // Setup subscribers
    for (size_t s = 0; s < NUM_SUBSCRIBERS; ++s) {
        sub_ids.push_back(bus.subscribe("test/*", [&](const DataPoint&) {
            received_count.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    // Run publishers
    std::latch start_latch(NUM_PUBLISHERS);
    std::vector<std::jthread> publishers;

    for (size_t p = 0; p < NUM_PUBLISHERS; ++p) {
        publishers.emplace_back([&, p]() {
            start_latch.arrive_and_wait();
            for (size_t i = 0; i < MESSAGES_PER_PUBLISHER; ++i) {
                bus.publish("test/topic", create_test_datapoint(p, i));
            }
        });
    }

    publishers.clear();

    // Wait for message processing
    std::this_thread::sleep_for(std::chrono::seconds(1));

    size_t expected = NUM_PUBLISHERS * MESSAGES_PER_PUBLISHER * NUM_SUBSCRIBERS;
    CHECK(received_count.load() == expected);
}

TEST_CASE("Scheduler handles concurrent deadline tasks", "[stress][concurrency]") {
    EDFScheduler scheduler;
    scheduler.start();

    constexpr size_t NUM_TASKS = 10000;
    std::atomic<size_t> completed{0};
    std::atomic<size_t> deadline_misses{0};

    std::vector<std::jthread> submitters;
    for (size_t t = 0; t < 4; ++t) {
        submitters.emplace_back([&]() {
            for (size_t i = 0; i < NUM_TASKS / 4; ++i) {
                auto deadline = std::chrono::steady_clock::now() +
                               std::chrono::milliseconds(100);
                scheduler.schedule({
                    .deadline = deadline,
                    .work = [&, deadline]() {
                        auto now = std::chrono::steady_clock::now();
                        if (now > deadline) {
                            deadline_misses.fetch_add(1);
                        }
                        completed.fetch_add(1);
                    }
                });
            }
        });
    }

    submitters.clear();
    scheduler.drain();

    CHECK(completed.load() == NUM_TASKS);
    CHECK(deadline_misses.load() < NUM_TASKS * 0.01);  // < 1% misses
}
```

**Effort estimé:** 1 semaine
**Priorité:** P0

---

### 3.2 CRITIQUE: Absence de Tests de Sécurité

**Constat:**
- Pas de fuzzing
- Pas de tests d'injection
- Pas de tests ReDoS
- Pas de tests buffer overflow

**Solution Recommandée:**

#### A. Fuzzing avec libFuzzer

```cpp
// tests/fuzz/fuzz_datapoint.cpp
#include <cstdint>
#include <cstddef>
#include "ipb/common/data_point.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    // Fuzz deserialization
    auto result = ipb::DataPoint::deserialize(
        std::span<const uint8_t>(data, size));

    // Si désérialisation réussit, vérifier invariants
    if (result) {
        auto& dp = *result;
        // Address should be valid UTF-8
        // Value should be consistent with type
        // Timestamp should be reasonable
        assert(dp.timestamp().time_since_epoch().count() >= 0);
    }

    return 0;
}
```

```cpp
// tests/fuzz/fuzz_pattern_matcher.cpp
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 2) return 0;

    // Split input: first byte = pattern length
    size_t pattern_len = data[0] % (size - 1);
    std::string pattern(reinterpret_cast<const char*>(data + 1), pattern_len);
    std::string input(reinterpret_cast<const char*>(data + 1 + pattern_len),
                      size - 1 - pattern_len);

    // Should not hang or crash
    try {
        auto matcher = ipb::PatternMatcher::create(pattern);
        if (matcher) {
            volatile bool result = matcher->matches(input);
            (void)result;
        }
    } catch (...) {
        // Expected for invalid patterns
    }

    return 0;
}
```

#### B. Tests ReDoS Spécifiques

```cpp
// tests/security/test_redos.cpp
TEST_CASE("Pattern matcher resistant to ReDoS", "[security][redos]") {
    const std::vector<std::string> malicious_patterns = {
        "(a+)+b",
        "(a|aa)+b",
        "(.*a){20}",
        "([a-zA-Z]+)*",
        "(a+)+",
        "^(a+)+$",
    };

    const std::string evil_input(30, 'a');  // Long input without 'b'

    for (const auto& pattern : malicious_patterns) {
        SECTION("Pattern: " + pattern) {
            auto start = std::chrono::steady_clock::now();

            // Should either reject pattern or complete quickly
            auto result = PatternMatcher::create(pattern);
            if (result) {
                result->matches(evil_input);
            }

            auto elapsed = std::chrono::steady_clock::now() - start;
            REQUIRE(elapsed < std::chrono::milliseconds(100));
        }
    }
}

TEST_CASE("Pattern validator rejects dangerous patterns", "[security]") {
    REQUIRE_FALSE(PatternValidator::is_safe("(a+)+b"));
    REQUIRE_FALSE(PatternValidator::is_safe("(.*)*"));
    REQUIRE(PatternValidator::is_safe("sensors/.*"));
    REQUIRE(PatternValidator::is_safe("devices/[0-9]+/status"));
}
```

#### C. Tests d'Injection

```cpp
// tests/security/test_injection.cpp
TEST_CASE("Router rejects injection attempts", "[security][injection]") {
    Router router;

    SECTION("SQL injection in address") {
        auto dp = DataPoint::create(
            "'; DROP TABLE rules; --",
            Value::from_int32(42));
        // Should handle safely
        auto result = router.route(dp);
        // No crash, no SQL execution
    }

    SECTION("Command injection in pattern") {
        // Should reject or escape
        auto result = router.add_rule({
            .pattern = "$(rm -rf /)",
            .sink_id = 1
        });
        // Pattern should be treated as literal or rejected
    }

    SECTION("Path traversal in address") {
        auto dp = DataPoint::create(
            "../../../etc/passwd",
            Value::from_string("test"));
        auto result = router.route(dp);
        // Should not access filesystem
    }
}
```

**Effort estimé:** 2 semaines
**Priorité:** P0

---

### 3.3 CRITIQUE: Absence de Tests de Performance

**Solution Recommandée:**

```cpp
// tests/performance/test_performance_regression.cpp
#include <catch2/catch_all.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

TEST_CASE("Router performance benchmarks", "[performance][!benchmark]") {
    Router router;
    setup_production_rules(router, 1000);
    auto dp = create_typical_datapoint();

    BENCHMARK("route single message") {
        return router.route(dp);
    };

    BENCHMARK_ADVANCED("route batch")(Catch::Benchmark::Chronometer meter) {
        std::vector<DataPoint> batch(1000);
        std::generate(batch.begin(), batch.end(), create_typical_datapoint);

        meter.measure([&] {
            for (auto& dp : batch) {
                router.route(dp);
            }
        });
    };
}

TEST_CASE("Performance SLO verification", "[performance][slo]") {
    Router router;
    setup_production_rules(router, 1000);

    constexpr size_t SAMPLE_SIZE = 10000;
    std::vector<double> latencies;
    latencies.reserve(SAMPLE_SIZE);

    for (size_t i = 0; i < SAMPLE_SIZE; ++i) {
        auto dp = create_random_datapoint();
        auto start = std::chrono::high_resolution_clock::now();
        router.route(dp);
        auto end = std::chrono::high_resolution_clock::now();

        auto us = std::chrono::duration<double, std::micro>(end - start).count();
        latencies.push_back(us);
    }

    std::sort(latencies.begin(), latencies.end());

    double p50 = latencies[SAMPLE_SIZE * 0.50];
    double p95 = latencies[SAMPLE_SIZE * 0.95];
    double p99 = latencies[SAMPLE_SIZE * 0.99];

    // SLO assertions
    CHECK(p50 < 50.0);   // P50 < 50μs
    CHECK(p95 < 150.0);  // P95 < 150μs
    CHECK(p99 < 250.0);  // P99 < 250μs

    CAPTURE(p50, p95, p99);
}
```

**Effort estimé:** 1 semaine
**Priorité:** P1

---

### 3.4 CRITIQUE: Absence de Tests d'Intégration E2E

**Solution Recommandée:**

```cpp
// tests/integration/test_e2e_pipeline.cpp
#include <catch2/catch_all.hpp>

class E2ETestFixture {
public:
    E2ETestFixture() {
        // Start full pipeline
        scoop_registry_ = std::make_unique<ScoopRegistry>();
        sink_registry_ = std::make_unique<SinkRegistry>();
        message_bus_ = std::make_unique<MessageBus>();
        scheduler_ = std::make_unique<EDFScheduler>();
        router_ = std::make_unique<Router>(/* deps */);

        // Setup test sinks
        test_sink_ = std::make_shared<TestSink>();
        sink_registry_->register_sink("test_sink", test_sink_);

        // Setup test source
        test_source_ = std::make_shared<TestSource>();
        scoop_registry_->register_source("test_source", test_source_);
    }

    void start() {
        scheduler_->start();
        router_->start();
    }

    void stop() {
        router_->stop();
        scheduler_->stop();
    }

protected:
    std::unique_ptr<ScoopRegistry> scoop_registry_;
    std::unique_ptr<SinkRegistry> sink_registry_;
    std::unique_ptr<MessageBus> message_bus_;
    std::unique_ptr<EDFScheduler> scheduler_;
    std::unique_ptr<Router> router_;
    std::shared_ptr<TestSink> test_sink_;
    std::shared_ptr<TestSource> test_source_;
};

TEST_CASE_METHOD(E2ETestFixture, "Full pipeline E2E", "[integration][e2e]") {
    start();

    SECTION("Message flows from source to sink") {
        // Configure routing
        router_->add_rule({
            .pattern = "sensors/*",
            .sink_id = "test_sink"
        });

        // Inject message at source
        test_source_->inject(DataPoint::create(
            "sensors/temperature",
            Value::from_float64(25.5)));

        // Verify message arrives at sink
        REQUIRE(test_sink_->wait_for_message(std::chrono::seconds(1)));
        auto received = test_sink_->last_message();
        CHECK(received.address() == "sensors/temperature");
        CHECK(received.value().as_float64() == Approx(25.5));
    }

    SECTION("Failover works correctly") {
        auto primary_sink = std::make_shared<FailableSink>();
        auto backup_sink = std::make_shared<TestSink>();

        sink_registry_->register_sink("primary", primary_sink);
        sink_registry_->register_sink("backup", backup_sink);

        router_->add_rule({
            .pattern = "critical/*",
            .sink_id = "primary",
            .failover_sink_id = "backup"
        });

        // First message goes to primary
        test_source_->inject(DataPoint::create("critical/data", Value::from_int32(1)));
        REQUIRE(primary_sink->wait_for_message(std::chrono::seconds(1)));

        // Fail primary
        primary_sink->set_failing(true);

        // Next message should go to backup
        test_source_->inject(DataPoint::create("critical/data", Value::from_int32(2)));
        REQUIRE(backup_sink->wait_for_message(std::chrono::seconds(1)));
    }

    stop();
}
```

**Effort estimé:** 2 semaines
**Priorité:** P1

---

### 3.5 HAUTE: Absence de Chaos Engineering

**Solution Recommandée:**

```cpp
// tests/chaos/test_resilience.cpp
#include "chaos/fault_injector.hpp"

TEST_CASE("System resilient to network failures", "[chaos][resilience]") {
    E2ETestFixture fixture;
    fixture.start();

    FaultInjector injector;

    SECTION("Handles temporary network partition") {
        // Start sending messages
        auto sender = std::jthread([&](std::stop_token st) {
            while (!st.stop_requested()) {
                fixture.inject_message("test/topic", random_value());
                std::this_thread::sleep_for(10ms);
            }
        });

        // Inject network partition after 100ms
        std::this_thread::sleep_for(100ms);
        injector.inject_network_partition(500ms);

        // Let it recover
        std::this_thread::sleep_for(1s);
        sender.request_stop();

        // Verify no data loss (with retries enabled)
        CHECK(fixture.test_sink_->message_count() > 0);
        CHECK(fixture.error_count() < fixture.total_messages() * 0.01);
    }

    SECTION("Handles sink crash and recovery") {
        auto crashable_sink = std::make_shared<CrashableSink>();
        fixture.register_sink("crashable", crashable_sink);

        // Send messages
        for (int i = 0; i < 100; ++i) {
            fixture.inject_message("crash/test", Value::from_int32(i));
            if (i == 50) {
                crashable_sink->simulate_crash();
            }
            if (i == 75) {
                crashable_sink->recover();
            }
        }

        std::this_thread::sleep_for(500ms);

        // Should have buffered or retried messages during crash
        CHECK(crashable_sink->received_count() >= 75);
    }

    SECTION("Handles memory pressure") {
        injector.limit_memory(100_MB);

        // Try to process large batch
        for (int i = 0; i < 10000; ++i) {
            fixture.inject_message("memory/test", large_value(10_KB));
        }

        // Should not crash, may drop some messages
        CHECK_NOTHROW(fixture.drain());

        injector.remove_memory_limit();
    }

    fixture.stop();
}
```

**Effort estimé:** 2 semaines
**Priorité:** P2

---

## 4. Infrastructure de Tests

### 4.1 Configuration CMake

```cmake
# tests/CMakeLists.txt
include(CTest)
include(Catch)

# Unit tests
add_executable(ipb_unit_tests
    unit/test_data_point.cpp
    unit/test_error.cpp
    unit/test_router.cpp
    # ... autres tests
)
target_link_libraries(ipb_unit_tests PRIVATE
    Catch2::Catch2WithMain
    ipb::core
)
catch_discover_tests(ipb_unit_tests)

# Stress tests (séparés car longs)
add_executable(ipb_stress_tests
    stress/test_concurrent_router.cpp
    stress/test_concurrent_bus.cpp
)
target_link_libraries(ipb_stress_tests PRIVATE
    Catch2::Catch2WithMain
    ipb::core
)
catch_discover_tests(ipb_stress_tests
    PROPERTIES LABELS "stress"
    TEST_DISCOVERY_TIMEOUT 300
)

# Fuzz tests
if(ENABLE_FUZZING)
    add_executable(fuzz_datapoint fuzz/fuzz_datapoint.cpp)
    target_compile_options(fuzz_datapoint PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_datapoint PRIVATE -fsanitize=fuzzer,address)
    target_link_libraries(fuzz_datapoint PRIVATE ipb::core)
endif()

# Performance tests
add_executable(ipb_perf_tests
    performance/test_performance_regression.cpp
)
target_link_libraries(ipb_perf_tests PRIVATE
    Catch2::Catch2WithMain
    ipb::core
)

# Integration tests
add_executable(ipb_integration_tests
    integration/test_e2e_pipeline.cpp
)
target_link_libraries(ipb_integration_tests PRIVATE
    Catch2::Catch2WithMain
    ipb::core
    ipb::test_utilities
)
```

### 4.2 CI/CD Pipeline

```yaml
# .github/workflows/tests.yml
name: Tests

on: [push, pull_request]

jobs:
  unit-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build and Test
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=$BUILD_TYPE -DENABLE_TESTING=ON
          cmake --build build -j$(nproc)
          cd build && ctest --output-on-failure

  sanitizer-tests:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, thread, undefined, memory]
    steps:
      - uses: actions/checkout@v4
      - name: Build with ${{ matrix.sanitizer }} sanitizer
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }}" \
            -DENABLE_TESTING=ON
          cmake --build build -j$(nproc)
          cd build && ctest --output-on-failure

  stress-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run stress tests
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_STRESS_TESTS=ON
          cmake --build build -j$(nproc)
          cd build && ctest -L stress --output-on-failure

  security-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run security tests
        run: |
          cmake -B build -DENABLE_SECURITY_TESTS=ON
          cmake --build build -j$(nproc)
          cd build && ctest -L security --output-on-failure

  performance-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Run benchmarks
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release -DENABLE_BENCHMARKS=ON
          cmake --build build -j$(nproc)
          ./build/benchmarks/ipb_benchmark --benchmark_format=json > benchmark.json
      - name: Check for regressions
        run: |
          python3 scripts/check_benchmark_regression.py benchmark.json

  coverage:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Generate coverage
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON
          cmake --build build -j$(nproc)
          cd build && ctest --output-on-failure
          lcov --capture --directory . --output-file coverage.info
          lcov --remove coverage.info '/usr/*' '*/test/*' --output-file coverage.info
      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: build/coverage.info
          fail_ci_if_error: true
          threshold: 80%

  mutation-testing:
    runs-on: ubuntu-latest
    if: github.event_name == 'schedule'
    steps:
      - uses: actions/checkout@v4
      - name: Run mutation tests
        run: |
          # Using mutmut or mull for C++
          ./scripts/run_mutation_tests.sh
```

---

## 3. Métriques de Qualité des Tests

| Métrique | Actuel | Cible Enterprise |
|----------|--------|------------------|
| Code coverage | ~75% | >90% |
| Branch coverage | N/A | >85% |
| Mutation score | N/A | >80% |
| Test/Code ratio | ~0.5 | >1.0 |
| Flaky test rate | N/A | <0.1% |
| Test execution time | N/A | <5 min (unit), <30 min (full) |

---

## 4. Roadmap Testing

### Phase 1: Critical (2 semaines)
- [ ] Tests ReDoS
- [ ] Tests concurrence avec sanitizers
- [ ] Tests de pression mémoire

### Phase 2: Resilience (3 semaines)
- [ ] Tests network failure
- [ ] Tests deadline miss
- [ ] Tests circuit breaker

### Phase 3: Advanced (4 semaines)
- [ ] Property-based testing
- [ ] Mutation testing
- [ ] Chaos engineering
- [ ] Performance regression tests

---

*Document généré pour IPB Enterprise Testing Review*
      - name: Build
        run: cmake -B build && cmake --build build
      - name: Run unit tests
        run: ctest --test-dir build --label-exclude stress -j4
      - name: Upload coverage
        uses: codecov/codecov-action@v3

  stress-tests:
    runs-on: ubuntu-latest
    timeout-minutes: 30
    steps:
      - uses: actions/checkout@v4
      - name: Build
        run: cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
      - name: Run stress tests
        run: ctest --test-dir build --label-regex stress -j2

  fuzz-tests:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Build with fuzzing
        run: |
          cmake -B build -DENABLE_FUZZING=ON \
            -DCMAKE_CXX_COMPILER=clang++ \
            -DCMAKE_BUILD_TYPE=Release
          cmake --build build
      - name: Run fuzzers
        run: |
          ./build/tests/fuzz_datapoint -max_total_time=300
          ./build/tests/fuzz_pattern_matcher -max_total_time=300

  sanitizers:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        sanitizer: [address, thread, undefined]
    steps:
      - uses: actions/checkout@v4
      - name: Build with ${{ matrix.sanitizer }} sanitizer
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DCMAKE_CXX_FLAGS="-fsanitize=${{ matrix.sanitizer }}"
          cmake --build build
      - name: Run tests
        run: ctest --test-dir build -j4
        env:
          ASAN_OPTIONS: detect_leaks=1
          TSAN_OPTIONS: second_deadlock_stack=1
```

---

## 5. Mutation Testing

```yaml
# .mutant.yml
mutant:
  source_dirs:
    - core/common/src
    - core/router/src
    - core/components/src

  test_command: ctest --test-dir build -j4

  mutations:
    - arithmetic      # +, -, *, /
    - relational     # <, >, <=, >=, ==, !=
    - logical        # &&, ||, !
    - assignment     # =, +=, -=
    - conditional    # if conditions
    - return_values  # return values

  thresholds:
    kill_rate: 80%   # Minimum mutation kill rate
    coverage: 85%    # Minimum line coverage
```

---

## 6. Plan d'Amélioration Tests

### Phase 1 - Fondations (Semaines 1-2)
- [ ] Ajouter tests de concurrence (3 suites)
- [ ] Configurer fuzzing (2 fuzzers)
- [ ] Ajouter tests ReDoS spécifiques

### Phase 2 - Sécurité (Semaines 3-4)
- [ ] Tests d'injection complets
- [ ] Tests sanitizers en CI
- [ ] Tests buffer overflow

### Phase 3 - Performance (Semaine 5)
- [ ] Benchmarks avec Catch2
- [ ] Tests SLO automatisés
- [ ] Performance regression CI

### Phase 4 - Intégration (Semaines 6-7)
- [ ] Tests E2E pipeline complet
- [ ] Tests failover
- [ ] Tests configuration dynamique

### Phase 5 - Chaos (Semaine 8)
- [ ] Framework fault injection
- [ ] Tests réseau
- [ ] Tests mémoire

---

## 7. Métriques de Tests Cibles

| Métrique | Actuel | Cible | Deadline |
|----------|--------|-------|----------|
| Couverture lignes | ~75% | > 85% | Phase 2 |
| Couverture branches | ~60% | > 80% | Phase 3 |
| Mutation score | N/A | > 80% | Phase 4 |
| Tests unitaires | 412 | 600+ | Phase 2 |
| Tests stress | 0 | 20+ | Phase 1 |
| Tests fuzz | 0 | 5+ | Phase 1 |
| Tests E2E | 0 | 30+ | Phase 4 |
| Temps CI total | ~5min | < 15min | Phase 5 |

---

## 8. Conclusion

Le testing d'IPB est **bon pour les tests unitaires** mais présente des **lacunes critiques** pour un environnement enterprise:

1. **Pas de tests concurrence** - Bugs race conditions non détectés
2. **Pas de fuzzing** - Vulnérabilités potentielles
3. **Pas de tests E2E** - Intégration non validée
4. **Pas de chaos testing** - Résilience non prouvée

L'investissement de **8 semaines** en testing permettrait d'atteindre un niveau de confiance enterprise avec:
- Couverture > 85%
- Mutation score > 80%
- Tests de sécurité automatisés
- Tests de charge/chaos

**Recommandation**: Prioriser tests concurrence et fuzzing (P0) avant déploiement production.
