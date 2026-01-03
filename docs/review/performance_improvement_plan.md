# Plan d'Amélioration des Performances IPB

## Objectifs Cibles

| Objectif | Valeur Actuelle | Valeur Cible | Écart |
|----------|-----------------|--------------|-------|
| **Latence P99** | ~1ms pire cas | <250μs garanti | 4x réduction |
| **Empreinte Mémoire** | ~5GB max | <500MB | 10x réduction |
| **Déterminisme** | Partiel (70%) | 100% lock-free hot paths | +30% |
| **Débit** | ~2-3M msg/s | >5M msg/s | 2x amélioration |

---

## 1. Tableau des Problèmes et Solutions

### 1.1 Problèmes Critiques (Bloquants pour Hard Real-Time)

| ID | Problème | Localisation | Impact | Solution | Complexité | Gain Attendu |
|----|----------|--------------|--------|----------|------------|--------------|
| **P1** | TaskQueue utilise mutex | `task_queue.cpp:9-79` | Latence non bornée, inversion de priorité | Remplacer par skip list lock-free | HAUTE | 25x sur remove |
| **P2** | TaskQueue::remove() en O(n log n) | `task_queue.cpp:55-79` | Bloque toutes opérations pendant ms | Utiliser pairing heap ou lazy deletion | HAUTE | 50μs → 2μs |
| **P3** | MemoryPool utilise mutex | `memory_pool.cpp:103-132` | 50-100ns overhead, inversion priorité | Free list lock-free avec CAS | MOYENNE | 5x plus rapide |
| **P4** | Channel iteration sous shared_lock | `message_bus.cpp:280-285` | Lock proportionnel au nombre de canaux | Dispatcher threads par canal | MOYENNE | Scalabilité linéaire |
| **P5** | Pattern Matcher créé à chaque appel | `router.cpp:256, 352` | Hash lookup + construction objet | Cache dans ValueCondition | BASSE | 10x sur regex |

### 1.2 Problèmes Mémoire (Bloquants pour <500MB)

| ID | Problème | Localisation | Consommation | Solution | Complexité | Gain Attendu |
|----|----------|--------------|--------------|----------|------------|--------------|
| **M1** | TaskQueue: 100K tâches par défaut | `edf_scheduler.hpp:177` | 20MB | Réduire à 10K | TRIVIALE | 18MB libérés |
| **M2** | Channel buffer: 64K messages | `channel.hpp:152` | 4.9GB (256 canaux) | Réduire à 4K messages | BASSE | 4.6GB libérés |
| **M3** | Message bus: 256 canaux max | `message_bus.hpp:167` | Multiplicateur M2 | Réduire à 64 canaux | BASSE | 75% réduction |
| **M4** | Slot aligné 64 bytes gaspillé | `channel.hpp:116-123` | ~50% overhead | Packing optimisé | MOYENNE | 30% réduction |
| **M5** | std::string heap allocation | `message_bus.hpp:59-60` | Variable | std::array<char, N> fixe | BASSE | Éliminer allocations |
| **M6** | std::function heap allocation | `edf_scheduler.hpp:63-101` | 32 bytes + heap | function_ref ou template | MOYENNE | Éliminer allocations |

### 1.3 Problèmes de Jitter (Secondaires)

| ID | Problème | Localisation | Jitter | Solution | Complexité | Gain Attendu |
|----|----------|--------------|--------|----------|------------|--------------|
| **J1** | CAS loop peut spin longtemps | `lockfree_queue.hpp:58-60` | ±20μs | Exponential backoff + yield | BASSE | Réduire contention |
| **J2** | Rate limiter utilise double | `rate_limiter.hpp:107-108` | Précision | Utiliser int64_t avec fixed-point | BASSE | Déterminisme |
| **J3** | Subscriber list utilise mutex | `channel.cpp:31-38` | ±100μs | RCU (Read-Copy-Update) | HAUTE | Lock-free reads |
| **J4** | std::vector growth | Divers | ±10-100μs | reserve() + fixed capacity | BASSE | Éliminer reallocations |

---

## 2. Tableau des Trade-offs et Conflits

### 2.1 Conflits Directs (Solutions Mutuellement Exclusives)

| Conflit | Option A | Option B | Incompatibilité | Recommandation |
|---------|----------|----------|-----------------|----------------|
| **C1** | Lock-free TaskQueue (skip list) | Simplicité du code (std::priority_queue) | Skip list = 500+ lignes complexes vs 50 lignes simples | **Option A** pour hard RT |
| **C2** | Lazy deletion (marquer cancelled) | Suppression immédiate | Lazy = mémoire retenue + overhead check; Immédiat = O(n log n) | **Lazy** pour latence bornée |
| **C3** | Channels pré-alloués (mémoire fixe) | Channels dynamiques (flexibilité) | Pré-alloc = mémoire fixe gaspillée; Dynamique = allocations runtime | **Pré-alloc** limité pour RT |
| **C4** | CTRE compile-time regex | std::regex runtime | CTRE = patterns fixés à compilation; std::regex = patterns dynamiques | **Hybride** selon use case |

### 2.2 Trade-offs Performance vs Mémoire

| Trade-off | Optimisation Performance | Coût Mémoire | Optimisation Mémoire | Coût Performance | Équilibre Recommandé |
|-----------|-------------------------|--------------|---------------------|------------------|---------------------|
| **T1** | Cache line padding (64B) | +50% overhead structures | Packing dense | +Cache misses, false sharing | **Padding** sur hot data uniquement |
| **T2** | Pré-allocation pools larges | +Mémoire inutilisée | Pools petits + growth | +Latence allocation | **Pools moyens** avec reserve |
| **T3** | Buffers circulaires grands | +Mémoire par canal | Buffers petits | +Risque overflow/backpressure | **4K-8K** messages suffisant |
| **T4** | Thread pools surdimensionnés | +Mémoire stack threads | Moins de threads | +Latence scheduling | **CPU cores × 2** threads |
| **T5** | Inline functions partout | +Taille binaire, I-cache | Appels normaux | +Overhead appel fonction | **Inline** hot paths uniquement |

### 2.3 Trade-offs Latence vs Débit

| Trade-off | Faible Latence | Impact Débit | Haut Débit | Impact Latence | Recommandation |
|-----------|----------------|--------------|------------|----------------|----------------|
| **L1** | Traitement message par message | -Overhead par message | Batching agressif | +Latence accumulation | **Micro-batches** (8-16 msg) |
| **L2** | Spin-wait (busy loop) | -CPU utilisé en attente | Sleep/yield | +Latence réveil thread | **Spin brief** + yield |
| **L3** | Lock-free partout | -Complexité code | Locks simples | +Latence pire cas | **Lock-free** hot paths |
| **L4** | Copy-on-write | -Copies inutiles si pas modifié | Zero-copy strict | +Complexité ownership | **Zero-copy** avec move |

### 2.4 Trade-offs Déterminisme vs Flexibilité

| Trade-off | Déterminisme | Perte Flexibilité | Flexibilité | Perte Déterminisme | Recommandation |
|-----------|--------------|-------------------|-------------|-------------------|----------------|
| **D1** | Capacités fixes (compile-time) | Pas de redimensionnement | Capacités dynamiques | Allocations imprévisibles | **Fixe** avec config build |
| **D2** | Patterns CTRE uniquement | Pas de regex runtime | std::regex runtime | Temps matching variable | **CTRE** + whitelist runtime |
| **D3** | Nombre threads fixe | Pas d'adaptation charge | Thread pool élastique | Création threads imprévisible | **Fixe** avec scaling manuel |
| **D4** | Timeouts stricts (drop) | Perte messages | Retry infini | Accumulation latence | **Timeout** + dead letter queue |

---

## 3. Plan d'Opérations pour Hard Real-Time (<250μs P99)

### Phase 1: Élimination des Mutex dans Hot Paths (Semaines 1-3)

#### Opération 1.1: Lock-Free TaskQueue
```
Fichiers à modifier:
  - core/components/include/ipb/scheduler/task_queue.hpp
  - core/components/src/scheduler/task_queue.cpp

Actions:
  1. Implémenter LockFreeSkipList<ScheduledTask>
  2. Comparateur par deadline (EDF)
  3. Insert: O(log n) lock-free avec CAS
  4. Remove: O(log n) lock-free avec marquage
  5. Pop min: O(1) lock-free

Tests requis:
  - Stress test multi-thread (16 threads, 1M ops)
  - Vérification ordre EDF
  - Mesure latence P99 < 5μs
```

#### Opération 1.2: Lock-Free Memory Pool
```
Fichiers à modifier:
  - core/common/include/ipb/common/memory_pool.hpp
  - core/common/src/memory_pool.cpp

Actions:
  1. Remplacer mutex + vector par atomic stack
  2. Structure Node avec next pointer
  3. allocate(): CAS sur head
  4. deallocate(): CAS push sur head
  5. ABA prevention avec tagged pointers

Tests requis:
  - Allocation/deallocation concurrent (16 threads)
  - Vérification pas de leaks
  - Latence allocation < 50ns P99
```

#### Opération 1.3: Per-Channel Dispatching
```
Fichiers à modifier:
  - core/components/include/ipb/message_bus/message_bus.hpp
  - core/components/src/message_bus/message_bus.cpp

Actions:
  1. Hash channels vers dispatcher threads
  2. Chaque thread possède subset de canaux
  3. Supprimer shared_lock global
  4. Channel creation: atomic registration

Tests requis:
  - Dispatch latency < 10μs
  - Scalabilité linéaire avec canaux
```

### Phase 2: Optimisation Pattern Matching (Semaine 4)

#### Opération 2.1: Cache Pattern Matchers
```
Fichiers à modifier:
  - core/router/include/ipb/router.hpp
  - core/router/src/router.cpp

Actions:
  1. Ajouter mutable cached_matcher_ dans ValueCondition
  2. Lazy initialization au premier match
  3. Thread-safe avec std::call_once ou atomic

Gain: 5μs → 0.5μs par match regex
```

#### Opération 2.2: CTRE pour Patterns Statiques
```
Fichiers à modifier:
  - core/components/src/rule_engine/pattern_matcher.cpp

Actions:
  1. Identifier patterns connus à compile-time
  2. Utiliser ctre::match<"pattern">()
  3. Fallback std::regex pour patterns dynamiques

Gain: 10x sur patterns statiques
```

### Phase 3: Élimination Allocations Hot Path (Semaine 5)

#### Opération 3.1: Fixed-Size Strings
```
Fichiers à modifier:
  - core/components/include/ipb/message_bus/message_bus.hpp
  - core/common/include/ipb/common/data_point.hpp

Actions:
  1. Remplacer std::string topic par std::array<char, 64>
  2. Remplacer std::string source_id par std::array<char, 32>
  3. Helper functions pour conversion

Gain: Éliminer heap allocations
```

#### Opération 3.2: Function Pointers ou Templates
```
Fichiers à modifier:
  - core/components/include/ipb/scheduler/edf_scheduler.hpp

Actions:
  1. Option A: function_ref (non-owning)
  2. Option B: Template sur callable type
  3. Éviter std::function dans ScheduledTask

Gain: Éliminer type erasure overhead
```

---

## 4. Plan d'Opérations pour Mémoire <500MB

### Phase M1: Réduction Capacités Par Défaut (Jour 1)

| Paramètre | Valeur Actuelle | Nouvelle Valeur | Économie |
|-----------|-----------------|-----------------|----------|
| `TaskQueue::max_size` | 100,000 | 10,000 | 18 MB |
| `Channel::buffer_size` | 65,536 | 4,096 | ~300 MB/canal |
| `MessageBus::max_channels` | 256 | 64 | 75% |
| **Empreinte totale estimée** | ~5 GB | ~400 MB | **92% réduction** |

```cpp
// edf_scheduler.hpp:177
static constexpr size_t DEFAULT_MAX_QUEUE_SIZE = 10'000;  // Was 100'000

// channel.hpp:152
static constexpr size_t DEFAULT_BUFFER_CAPACITY = 4'096;  // Was 65'536

// message_bus.hpp:167
static constexpr size_t DEFAULT_MAX_CHANNELS = 64;  // Was 256
```

### Phase M2: Lazy Allocation (Semaine 2)

```
Fichiers à modifier:
  - core/components/src/message_bus/channel.cpp

Actions:
  1. Ne pas pré-allouer tous les slots
  2. Allocation par chunks de 1024
  3. High watermark pour libération

Gain: Mémoire utilisée = mémoire nécessaire
```

### Phase M3: Structure Packing (Semaine 3)

```cpp
// Avant: ~300 bytes avec padding
struct Message {
    Type type;                       // 1 byte + 7 padding
    Priority priority;               // 1 byte + 7 padding
    std::string topic;               // 32 bytes
    ...
};

// Après: ~200 bytes packed
struct Message {
    // Grouper petits champs
    Type type;                       // 1 byte
    Priority priority;               // 1 byte
    uint16_t flags;                  // 2 bytes
    uint32_t reserved;               // 4 bytes (total 8, aligned)

    std::array<char, 64> topic;      // 64 bytes (no heap)
    ...
} __attribute__((packed));
```

---

## 5. Matrice de Priorisation

| Opération | Impact RT | Impact Mémoire | Complexité | Priorité | Sprint |
|-----------|-----------|----------------|------------|----------|--------|
| Réduire capacités défaut | Aucun | ⬆️⬆️⬆️ | Triviale | **P0** | 1 |
| Lock-Free TaskQueue | ⬆️⬆️⬆️ | Neutre | Haute | **P1** | 1-2 |
| Lock-Free MemoryPool | ⬆️⬆️ | Neutre | Moyenne | **P1** | 2 |
| Cache Pattern Matchers | ⬆️⬆️ | ⬇️ légère | Basse | **P2** | 3 |
| Per-Channel Dispatch | ⬆️⬆️ | Neutre | Moyenne | **P2** | 3 |
| Fixed-Size Strings | ⬆️ | ⬆️ | Basse | **P3** | 4 |
| Lazy Channel Allocation | Neutre | ⬆️⬆️ | Moyenne | **P3** | 4 |
| Structure Packing | ⬆️ cache | ⬆️⬆️ | Moyenne | **P4** | 5 |

---

## 6. Métriques de Validation

### Tests de Performance Requis

```cpp
// Benchmark latence
TEST(HardRealTime, P99LatencyUnder250us) {
    constexpr int ITERATIONS = 1'000'000;
    std::vector<int64_t> latencies;
    latencies.reserve(ITERATIONS);

    for (int i = 0; i < ITERATIONS; ++i) {
        auto start = std::chrono::steady_clock::now();
        router.route(test_data_point);
        auto end = std::chrono::steady_clock::now();
        latencies.push_back((end - start).count());
    }

    std::sort(latencies.begin(), latencies.end());
    int64_t p99 = latencies[ITERATIONS * 99 / 100];

    EXPECT_LT(p99, 250'000);  // 250μs en nanosecondes
}

// Benchmark mémoire
TEST(MemoryFootprint, Under500MB) {
    // Créer configuration maximale
    MessageBus bus(64);  // 64 canaux
    for (int i = 0; i < 64; ++i) {
        bus.create_channel("channel_" + std::to_string(i));
    }

    size_t rss = get_resident_set_size();
    EXPECT_LT(rss, 500 * 1024 * 1024);  // 500 MB
}
```

### Critères de Succès

| Critère | Seuil | Méthode de Mesure |
|---------|-------|-------------------|
| Latence P99 | <250μs | Benchmark 1M opérations |
| Latence P999 | <500μs | Benchmark 1M opérations |
| Latence Max | <1ms | Stress test 10M opérations |
| Mémoire RSS | <500MB | /proc/self/status après init |
| Mémoire Peak | <600MB | valgrind massif |
| Throughput | >5M msg/s | Benchmark sustained load |

---

## 7. Risques et Mitigations

| Risque | Probabilité | Impact | Mitigation |
|--------|-------------|--------|------------|
| Lock-free bugs (ABA, etc.) | Moyenne | Critique | Tests exhaustifs, TSAN, model checking |
| Régression performance | Moyenne | Haute | Benchmarks CI, comparaison avant/après |
| Capacités trop réduites | Basse | Moyenne | Configuration runtime, monitoring |
| CTRE incompatibilité patterns | Basse | Basse | Fallback std::regex |

---

## 8. Résumé Exécutif

### Actions Immédiates (Cette Semaine)
1. ✅ Réduire capacités par défaut → **Mémoire <500MB atteint**
2. Commencer Lock-Free TaskQueue

### Actions Court Terme (2-4 Semaines)
3. Lock-Free MemoryPool
4. Cache Pattern Matchers
5. Per-Channel Dispatching

### Résultat Attendu
- **Hard Real-Time**: <250μs P99 garanti
- **Mémoire**: <400MB en configuration standard
- **Débit**: >5M messages/seconde

---

**Document créé**: 2025-12-13
**Basé sur**: performance_analysis.md
**Objectif**: Hard Real-Time + Mémoire <500MB
