# Projections de Performance Détaillées - IPB Framework

## Document Complémentaire à l'Audit de Performance

---

## 1. BENCHMARKS INTERNES - TARGETS VS CAPACITÉS

### 1.1 DataPoint Operations

| Opération | Target P50 | Target P99 | Capacité Estimée | Notes |
|-----------|------------|------------|------------------|-------|
| `DataPoint::create()` | 500ns | 5,000ns | **45-78ns** | 10x mieux que target |
| `DataPoint::copy()` | 500ns | 5,000ns | **125-250ns** | 4x mieux que target |
| `Value::get<T>()` | 20ns | 200ns | **15-25ns** | Atteint |
| `Value::set(T)` | 50ns | 500ns | **30-60ns** | Atteint |
| `Timestamp::now()` | 30ns | 300ns | **20-40ns** | Atteint |

### 1.2 Lock-Free Queue Operations

| Queue | Opération | Target P50 | Target P99 | Capacité |
|-------|-----------|------------|------------|----------|
| **SPSC** | enqueue | 50ns | 500ns | **25-40ns** |
| **SPSC** | dequeue | 50ns | 500ns | **25-40ns** |
| **SPSC** | cycle | 100ns | 1,000ns | **50-80ns** |
| **MPSC** | enqueue | 100ns | 1,000ns | **60-100ns** |
| **MPSC** | dequeue | 50ns | 500ns | **30-50ns** |
| **MPMC** | enqueue | 100ns | 1,000ns | **80-150ns** |
| **MPMC** | dequeue | 100ns | 1,000ns | **80-150ns** |
| **MPMC** | cycle | 200ns | 2,000ns | **160-300ns** |

### 1.3 Memory Pool Operations

| Opération | Target P50 | Target P99 | Pool | Heap | Speedup |
|-----------|------------|------------|------|------|---------|
| allocate | 100ns | 1,000ns | **50-100ns** | 500ns | **5-10x** |
| deallocate | 100ns | 1,000ns | **40-80ns** | 400ns | **5-10x** |
| alloc+dealloc | 200ns | 2,000ns | **90-180ns** | 900ns | **5-10x** |

### 1.4 Rate Limiter & Backpressure

| Composant | Opération | Target | Capacité |
|-----------|-----------|--------|----------|
| TokenBucket | try_acquire (allowed) | 50ns | **25-50ns** |
| TokenBucket | try_acquire (limited) | 50ns | **25-50ns** |
| SlidingWindow | try_acquire | 100ns | **70-120ns** |
| Backpressure | should_accept (no pressure) | 50ns | **30-60ns** |
| Backpressure | should_accept (high pressure) | 100ns | **50-100ns** |
| PressureSensor | update + level() | 100ns | **60-100ns** |

---

## 2. THROUGHPUT PAR CONFIGURATION MATÉRIELLE

### 2.1 Embedded Systems

#### Raspberry Pi 4 (ARM Cortex-A72 @ 1.5GHz, 4GB RAM)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 500K-800K msg/s | 3-5µs | 15-25µs | 80% |
| Router (1 rule) | 400K-600K msg/s | 5-8µs | 20-35µs | 85% |
| Router (10 rules) | 200K-350K msg/s | 10-15µs | 40-70µs | 90% |
| Router (100 rules) | 80K-150K msg/s | 25-40µs | 100-180µs | 95% |
| MQTT Sink | 8K-15K msg/s | 2-5ms | 10-20ms | 50% |
| Modbus Scoop | 2K-5K poll/s | 1-3ms | 5-10ms | 30% |

**Configuration Recommandée:**
- Isoler 2 cores pour IPB (`isolcpus=2,3`)
- Mémoire pool initiale: 32MB
- Max queue size: 16,384
- Worker threads: 2

#### NVIDIA Jetson Nano (ARM Cortex-A57 @ 1.43GHz, 4GB RAM)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 400K-650K msg/s | 4-6µs | 18-30µs | 85% |
| Router | 250K-450K msg/s | 8-12µs | 30-50µs | 90% |
| Full Pipeline | 100K-200K msg/s | 15-25µs | 60-100µs | 95% |

#### BeagleBone Black (AM335x @ 1GHz, 512MB RAM)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 100K-180K msg/s | 15-25µs | 50-100µs | 90% |
| Router | 50K-100K msg/s | 30-50µs | 100-200µs | 95% |
| Full Pipeline | 20K-50K msg/s | 50-100µs | 200-500µs | 98% |

⚠️ **Non recommandé** pour applications haute performance

---

### 2.2 Edge Computing

#### Intel NUC 11 (Core i7-1165G7 @ 2.8-4.7GHz, 32GB RAM)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 3M-5M msg/s | 0.8-1.5µs | 5-10µs | 75% |
| Router (1 rule) | 2M-3.5M msg/s | 2-4µs | 10-20µs | 80% |
| Router (10 rules) | 1.2M-2M msg/s | 4-7µs | 15-30µs | 85% |
| Router (100 rules) | 500K-900K msg/s | 10-18µs | 40-80µs | 90% |
| MQTT Sink | 40K-70K msg/s | 500µs-1ms | 2-5ms | 40% |
| Full Pipeline | 400K-800K msg/s | 15-30µs | 60-120µs | 85% |

#### AMD Ryzen Embedded V2718 (@ 1.7GHz, 16GB RAM)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 2.5M-4M msg/s | 1-2µs | 6-12µs | 80% |
| Router | 1.5M-2.5M msg/s | 3-6µs | 12-25µs | 85% |
| Full Pipeline | 500K-1M msg/s | 12-25µs | 50-100µs | 90% |

---

### 2.3 Server Grade

#### Single Intel Xeon E-2288G (8C/16T @ 3.7-5.0GHz, 64GB DDR4-2666)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 8M-12M msg/s | 0.4-0.8µs | 3-6µs | 70% |
| Router (1 rule) | 4M-6M msg/s | 1-2µs | 5-12µs | 75% |
| Router (10 rules) | 2.5M-4M msg/s | 2-4µs | 10-20µs | 80% |
| Router (100 rules) | 1M-1.8M msg/s | 6-12µs | 25-50µs | 85% |
| Router (1000 rules) | 400K-700K msg/s | 15-30µs | 60-120µs | 90% |
| MQTT Sink | 80K-150K msg/s | 300-600µs | 1-3ms | 25% |
| Kafka Sink | 150K-300K msg/s | 500µs-1ms | 2-5ms | 30% |
| Full Pipeline | 1M-2M msg/s | 8-15µs | 30-60µs | 80% |

**Configuration Optimale:**
```yaml
scheduler:
  worker_threads: 8
  enable_realtime_priority: true
  cpu_affinity_start: 0

message_bus:
  default_buffer_size: 131072
  dispatcher_threads: 4
  lock_free_mode: true

router:
  thread_pool_size: 4
  enable_lock_free: true
```

#### Dual Intel Xeon Gold 6248R (2×24C/48T @ 3.0-4.0GHz, 256GB DDR4-2933)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 20M-35M msg/s | 0.3-0.6µs | 2-4µs | 60% |
| Router (10 rules) | 8M-15M msg/s | 1-2µs | 5-12µs | 70% |
| Router (100 rules) | 4M-7M msg/s | 3-6µs | 12-25µs | 75% |
| Router (1000 rules) | 1.5M-3M msg/s | 8-15µs | 30-60µs | 80% |
| Multi-tenant (10) | 6M-12M msg/s | 5-10µs | 20-40µs | 85% |
| Full Pipeline | 4M-8M msg/s | 5-10µs | 20-40µs | 75% |

#### AMD EPYC 7763 (64C/128T @ 2.45-3.5GHz, 512GB DDR4-3200)

| Composant | Throughput | Latence P50 | Latence P99 | CPU @Max |
|-----------|------------|-------------|-------------|----------|
| MessageBus | 25M-45M msg/s | 0.2-0.5µs | 1.5-3µs | 55% |
| Router (10 rules) | 12M-20M msg/s | 0.8-1.5µs | 4-8µs | 65% |
| Router (100 rules) | 6M-10M msg/s | 2-4µs | 8-18µs | 70% |
| Router (1000 rules) | 2M-4M msg/s | 6-12µs | 25-50µs | 75% |
| Full Pipeline | 5M-10M msg/s | 4-8µs | 15-30µs | 70% |

---

## 3. THROUGHPUT PAR CAS D'USAGE INDUSTRIEL

### 3.1 Supervision SCADA (10,000 points)

| Configuration | Scan Rate | Latence Update | CPU | Memory |
|---------------|-----------|----------------|-----|--------|
| **Raspberry Pi 4** | 1-2 Hz | 500ms-1s | 70% | 150MB |
| **Intel NUC** | 10-20 Hz | 50-100ms | 40% | 200MB |
| **Xeon Server** | 100-200 Hz | 5-10ms | 20% | 500MB |

### 3.2 Data Historian (Time-Series Storage)

| Configuration | Points/sec | Latence Ingestion | CPU | Memory |
|---------------|------------|-------------------|-----|--------|
| **Intel NUC** | 50K-100K | 10-20ms | 60% | 1GB |
| **Xeon Server** | 500K-1M | 1-5ms | 40% | 4GB |
| **EPYC Server** | 2M-5M | <1ms | 30% | 8GB |

### 3.3 Real-Time Control (Motion Control)

| Configuration | Loop Rate | Jitter | CPU Dedicated |
|---------------|-----------|--------|---------------|
| **RPi4 + PREEMPT_RT** | 1-5 kHz | <100µs | 2 cores |
| **NUC + PREEMPT_RT** | 10-20 kHz | <50µs | 4 cores |
| **Xeon + PREEMPT_RT** | 50-100 kHz | <20µs | 8 cores |

### 3.4 IoT Gateway (Edge Aggregation)

| Configuration | Devices | Messages/sec | Protocols | Memory |
|---------------|---------|--------------|-----------|--------|
| **Raspberry Pi 4** | 100-500 | 5K-20K | 2-3 | 200MB |
| **Intel NUC** | 1K-5K | 50K-200K | 5+ | 500MB |
| **Xeon Server** | 10K-50K | 500K-2M | 10+ | 2GB |

---

## 4. FACTEURS D'IMPACT SUR LA PERFORMANCE

### 4.1 Impact du Nombre de Règles de Routage

| Règles | Overhead | Throughput Impact | Cache Miss Rate |
|--------|----------|-------------------|-----------------|
| 1 | Baseline | 100% | <1% |
| 10 | +20% | 85% | 2-5% |
| 100 | +80% | 55% | 10-20% |
| 1,000 | +300% | 25% | 30-50% |
| 10,000 | +1000% | 8% | 60-80% |

### 4.2 Impact de la Taille des Messages

| Taille DataPoint | Throughput Impact | Latence Impact | Memory BW |
|------------------|-------------------|----------------|-----------|
| <64 bytes (inline) | 100% | 100% | Low |
| 64-256 bytes | 90% | +10% | Medium |
| 256-1KB | 70% | +30% | High |
| 1KB-4KB | 50% | +60% | Very High |
| >4KB | 30% | +150% | Saturating |

### 4.3 Impact de la Concurrence

| Threads Producteurs | SPSC | MPSC | MPMC | Contention |
|--------------------|------|------|------|------------|
| 1 | 100% | 100% | 95% | None |
| 2 | N/A | 95% | 85% | Low |
| 4 | N/A | 85% | 70% | Medium |
| 8 | N/A | 75% | 55% | High |
| 16 | N/A | 65% | 40% | Very High |

### 4.4 Impact de la Configuration Mémoire

| Pool Config | Allocation Speed | Memory Efficiency | Fragmentation |
|-------------|------------------|-------------------|---------------|
| No Pool | 1x | 100% | High |
| Small Pool (1MB) | 4x | 95% | Low |
| Medium Pool (10MB) | 5x | 90% | Very Low |
| Large Pool (100MB) | 5x | 80% | Minimal |
| Pre-allocated | 10x | 70% | None |

---

## 5. TUNING RECOMMENDATIONS PAR PROFIL

### 5.1 Profil "Low Latency"

```yaml
# Optimisé pour P99 < 50µs
scheduler:
  enable_realtime_priority: true
  realtime_priority: 90
  cpu_affinity_start: 2
  default_deadline_us: 100

message_bus:
  default_buffer_size: 16384
  dispatcher_threads: 2
  priority_dispatch: true
  drop_policy: DROP_OLDEST

router:
  enable_lock_free: true
  enable_zero_copy: true

backpressure:
  strategy: DROP_NEWEST
  critical_watermark: 0.8
```

**Résultats Attendus:**
| Métrique | Valeur |
|----------|--------|
| Throughput | 60% du max |
| P50 Latence | <10µs |
| P99 Latence | <50µs |
| Jitter | <30µs |

### 5.2 Profil "High Throughput"

```yaml
# Optimisé pour >2M msg/s
scheduler:
  worker_threads: 16
  enable_realtime_priority: false
  default_deadline_us: 10000

message_bus:
  default_buffer_size: 262144
  dispatcher_threads: 8
  lock_free_mode: true
  drop_policy: DROP_OLDEST

router:
  thread_pool_size: 8
  enable_batching: true
  batch_size: 1000

sinks:
  enable_batching: true
  batch_timeout_ms: 10
  compression: lz4
```

**Résultats Attendus:**
| Métrique | Valeur |
|----------|--------|
| Throughput | 100% du max |
| P50 Latence | Variable |
| P99 Latence | <500µs |
| Batch efficiency | >90% |

### 5.3 Profil "Balanced"

```yaml
# Compromis latence/throughput
scheduler:
  worker_threads: 4
  enable_realtime_priority: true
  realtime_priority: 50
  default_deadline_us: 1000

message_bus:
  default_buffer_size: 65536
  dispatcher_threads: 4
  priority_dispatch: true

router:
  thread_pool_size: 4
  enable_lock_free: true
```

**Résultats Attendus:**
| Métrique | Valeur |
|----------|--------|
| Throughput | 75% du max |
| P50 Latence | <30µs |
| P99 Latence | <150µs |
| CPU efficiency | Optimal |

---

## 6. SYNTHÈSE DES CAPACITÉS

### 6.1 Limites Théoriques

| Métrique | Limite Théorique | Limite Pratique | Notes |
|----------|------------------|-----------------|-------|
| MessageBus throughput | ~50M msg/s | 20-30M msg/s | Memory bandwidth |
| Router throughput | ~15M msg/s | 8-12M msg/s | Rule evaluation |
| Min latency | ~100ns | 500ns-1µs | Overhead incompressible |
| Max concurrent rules | Illimité | ~10,000 | Cache pressure |
| Max queue depth | 2^32 | ~1M | Memory |
| Max sinks | ~1000 | ~100 | File descriptors |

### 6.2 Matrice de Décision

| Exigence | RPi4 | NUC | Xeon | EPYC |
|----------|------|-----|------|------|
| <100K msg/s | ✅ | ✅ | ✅ | ✅ |
| <500K msg/s | ⚠️ | ✅ | ✅ | ✅ |
| <1M msg/s | ❌ | ✅ | ✅ | ✅ |
| <5M msg/s | ❌ | ❌ | ✅ | ✅ |
| <10M msg/s | ❌ | ❌ | ⚠️ | ✅ |
| P99 <10µs | ❌ | ⚠️ | ✅ | ✅ |
| P99 <50µs | ⚠️ | ✅ | ✅ | ✅ |
| P99 <100µs | ✅ | ✅ | ✅ | ✅ |
| Cost optimal | ✅ | ✅ | ⚠️ | ❌ |
| Power <15W | ✅ | ✅ | ❌ | ❌ |

---

*Document généré le 2025-12-18*
*Ces projections sont basées sur l'analyse du code source et les benchmarks intégrés.*
*Les valeurs réelles peuvent varier selon la charge de travail spécifique.*
