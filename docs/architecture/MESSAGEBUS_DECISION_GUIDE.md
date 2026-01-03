# Guide de Décision: MessageBus vs Alternatives

## Pourquoi utiliser MessageBus? Quand le remplacer?

---

## 1. QU'EST-CE QUE LE MESSAGEBUS?

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                              MESSAGE BUS                                     │
│                                                                             │
│   ┌─────────┐     ┌─────────────────────────────────────┐     ┌─────────┐  │
│   │Producer1│────▶│                                     │────▶│Consumer1│  │
│   └─────────┘     │         TOPIC CHANNELS              │     └─────────┘  │
│   ┌─────────┐     │  ┌───────────────────────────────┐  │     ┌─────────┐  │
│   │Producer2│────▶│  │ sensors/temp    [████████░░]  │  │────▶│Consumer2│  │
│   └─────────┘     │  │ sensors/pressure[██████░░░░]  │  │     └─────────┘  │
│   ┌─────────┐     │  │ actuators/#     [███░░░░░░░]  │  │     ┌─────────┐  │
│   │Producer3│────▶│  │ control/cmd     [█████████░]  │  │────▶│Consumer3│  │
│   └─────────┘     │  └───────────────────────────────┘  │     └─────────┘  │
│                   │                                     │                   │
│                   │  • Lock-free MPMC queues            │                   │
│                   │  • Topic-based routing              │                   │
│                   │  • Priority dispatch                │                   │
│                   │  • Wildcard subscriptions           │                   │
│                   └─────────────────────────────────────┘                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Caractéristiques Clés

| Feature | Description | Impact |
|---------|-------------|--------|
| **Lock-free MPMC** | Files d'attente sans verrou | Latence prévisible |
| **Topic routing** | Routage par nom de topic | Découplage producteur/consommateur |
| **Wildcards** | `sensors/#`, `*/temp` | Souscriptions flexibles |
| **Priority dispatch** | 4 niveaux de priorité | QoS différenciée |
| **Zero-copy** | Passage par référence | Performance maximale |
| **Batching** | Envoi groupé | Throughput optimisé |

---

## 2. QUAND UTILISER LE MESSAGEBUS ✅

### 2.1 Cas d'Usage Idéaux

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    CAS D'USAGE IDÉAUX POUR MESSAGEBUS                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ✅ MULTIPLE PRODUCTEURS → MULTIPLE CONSOMMATEURS (MPMC)                    │
│     ┌────────┐                                           ┌────────┐        │
│     │Modbus  │──┐                                   ┌───▶│ Kafka  │        │
│     │ Scoop  │  │    ┌───────────────────────┐     │    └────────┘        │
│     └────────┘  ├───▶│     MESSAGE BUS       │─────┤    ┌────────┐        │
│     ┌────────┐  │    │                       │     ├───▶│InfluxDB│        │
│     │OPC UA  │──┤    │  Topic: sensors/#     │     │    └────────┘        │
│     │ Scoop  │  │    └───────────────────────┘     │    ┌────────┐        │
│     └────────┘  │                                   └───▶│Alerting│        │
│     ┌────────┐  │                                        └────────┘        │
│     │ MQTT   │──┘                                                          │
│     │ Scoop  │                                                             │
│     └────────┘                                                             │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ✅ DYNAMIC SUBSCRIPTIONS (composants qui arrivent/partent)                 │
│                                                                             │
│     Runtime:  t=0        t=1        t=2        t=3                         │
│               ┌───┐      ┌───┐      ┌───┐      ┌───┐                       │
│     Sinks:    │ 2 │ ───▶ │ 3 │ ───▶ │ 5 │ ───▶ │ 4 │                       │
│               └───┘      └───┘      └───┘      └───┘                       │
│                           +1         +2         -1                          │
│     Le MessageBus gère dynamiquement les abonnements                        │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ✅ TOPIC-BASED FILTERING (routage intelligent)                             │
│                                                                             │
│     Message: { topic: "factory/line1/robot3/joint2/temp", value: 85.5 }    │
│                                                                             │
│     Subscribers:                                                            │
│     ├── "factory/#"           → All factory data (historian)               │
│     ├── "factory/line1/#"     → Line 1 monitoring                          │
│     ├── "*/robot*/joint*/temp"→ All joint temperatures (analytics)         │
│     └── "factory/line1/robot3/joint2/temp" → Specific sensor (control)     │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ✅ PRIORITY-BASED PROCESSING                                               │
│                                                                             │
│     REALTIME (255) ═══════════════════════▶ Process immediately            │
│     HIGH (128)     ────────────────────────▶ Process next                  │
│     NORMAL (64)    ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─ ─▶ Queue for processing           │
│     LOW (0)        · · · · · · · · · · · · ▶ Background processing          │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 2.2 Métriques de Décision

| Critère | Seuil pour MessageBus | Exemple |
|---------|----------------------|---------|
| Producteurs | ≥ 2 | 3+ scoops actifs |
| Consommateurs | ≥ 2 | 2+ sinks ou processing |
| Topics distincts | ≥ 5 | Hiérarchie capteurs |
| Subscriptions dynamiques | Oui | Plugins hot-reload |
| Throughput cible | 100K - 10M msg/s | Industrial gateway |
| Latence acceptable | 1-50µs | Soft real-time |

---

## 3. QUAND NE PAS UTILISER LE MESSAGEBUS ❌

### 3.1 Overhead du MessageBus

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      OVERHEAD DU MESSAGEBUS                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Chemin direct (sans MessageBus):                                          │
│                                                                             │
│     Producer ──────────────────────────────────────▶ Consumer               │
│               │                                    │                        │
│               └──────────── ~50ns ─────────────────┘                        │
│                                                                             │
│  Avec MessageBus:                                                           │
│                                                                             │
│     Producer ──▶ [Serialize] ──▶ [Queue] ──▶ [Dispatch] ──▶ Consumer       │
│               │      │            │            │          │                 │
│               │   +100ns       +80ns        +200ns        │                 │
│               └────────────── ~500ns ────────────────────┘                  │
│                                                                             │
│  Overhead: ~450ns par message (10x plus lent)                               │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 3.2 Cas où le MessageBus est OVERKILL

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                 CAS OÙ MESSAGEBUS EST OVERKILL                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ❌ SINGLE PRODUCER → SINGLE CONSUMER (1:1)                                 │
│                                                                             │
│     MAUVAIS:                              MEILLEUR:                         │
│     ┌────────┐    ┌─────┐    ┌────────┐   ┌────────┐    ┌────────┐         │
│     │Producer│───▶│ Bus │───▶│Consumer│   │Producer│───▶│Consumer│         │
│     └────────┘    └─────┘    └────────┘   └────────┘    └────────┘         │
│         500ns overhead                     SPSCQueue: 50ns                  │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ❌ PIPELINE LINÉAIRE FIXE                                                  │
│                                                                             │
│     MAUVAIS:                                                                │
│     ┌───┐   ┌─────┐   ┌───┐   ┌─────┐   ┌───┐   ┌─────┐   ┌───┐           │
│     │ A │──▶│ Bus │──▶│ B │──▶│ Bus │──▶│ C │──▶│ Bus │──▶│ D │           │
│     └───┘   └─────┘   └───┘   └─────┘   └───┘   └─────┘   └───┘           │
│                     1500ns total overhead                                   │
│                                                                             │
│     MEILLEUR:                                                               │
│     ┌───┐   ┌───────┐   ┌───┐   ┌───────┐   ┌───┐   ┌───────┐   ┌───┐     │
│     │ A │──▶│ SPSC  │──▶│ B │──▶│ SPSC  │──▶│ C │──▶│ SPSC  │──▶│ D │     │
│     └───┘   └───────┘   └───┘   └───────┘   └───┘   └───────┘   └───┘     │
│                      150ns total overhead                                   │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ❌ HARD REAL-TIME (latence < 1µs requise)                                  │
│                                                                             │
│     MessageBus P99: 5-10µs (trop lent pour motion control)                 │
│     Direct queue P99: 100-500ns ✓                                          │
│                                                                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ❌ TRÈS FAIBLE VOLUME (< 1000 msg/s)                                       │
│                                                                             │
│     L'infrastructure du MessageBus consomme:                                │
│     • ~5MB RAM pour les buffers                                            │
│     • 2-4 threads dispatcher                                               │
│     • Cycles CPU pour le topic matching                                     │
│                                                                             │
│     Pour 1000 msg/s → appels directs suffisent                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. ALTERNATIVES AU MESSAGEBUS

### 4.1 Matrice de Décision

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    MATRICE DE DÉCISION: QUEL MÉCANISME?                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌─────────────────┬───────────────────────────────────────────────────┐   │
│  │                 │              NOMBRE DE CONSOMMATEURS               │   │
│  │                 ├─────────────┬─────────────┬───────────────────────┤   │
│  │                 │      1      │    2-10     │        10+            │   │
│  ├─────────────────┼─────────────┼─────────────┼───────────────────────┤   │
│  │                 │             │             │                       │   │
│  │  1 Producteur   │  SPSCQueue  │  Broadcast  │  MessageBus           │   │
│  │                 │  (50ns)     │  ou SPMC    │  (Topic routing)      │   │
│  │                 │             │  (100ns)    │                       │   │
│  ├─────────────────┼─────────────┼─────────────┼───────────────────────┤   │
│  │                 │             │             │                       │   │
│  │  2-10           │  MPSCQueue  │  MessageBus │  MessageBus           │   │
│  │  Producteurs    │  (100ns)    │  (500ns)    │  (Topic routing)      │   │
│  │                 │             │             │                       │   │
│  ├─────────────────┼─────────────┼─────────────┼───────────────────────┤   │
│  │                 │             │             │                       │   │
│  │  10+            │  MPSCQueue  │  MessageBus │  MessageBus +         │   │
│  │  Producteurs    │  ou Fan-in  │             │  Sharding             │   │
│  │                 │             │             │                       │   │
│  └─────────────────┴─────────────┴─────────────┴───────────────────────┘   │
│                                                                             │
│  Légende:                                                                   │
│  • (Xns) = latence typique P50                                             │
│  • SPSC/MPSC/MPMC = Lock-free queues directes                              │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.2 Alternative 1: Direct Queue (SPSC/MPSC/MPMC)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│              ALTERNATIVE 1: DIRECT QUEUE (SANS MESSAGEBUS)                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  USE CASE: Pipeline linéaire, faible latence critique                       │
│                                                                             │
│  Architecture:                                                              │
│  ┌────────────┐     ┌──────────────────┐     ┌────────────┐                │
│  │   Scoop    │────▶│   SPSCQueue<DP>  │────▶│  Processor │                │
│  │  (Thread1) │     │    Capacity=4096 │     │  (Thread2) │                │
│  └────────────┘     └──────────────────┘     └────────────┘                │
│                                                                             │
│  Code:                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  // Producer thread                                                  │   │
│  │  SPSCQueue<DataPoint, 4096> queue;                                   │   │
│  │                                                                      │   │
│  │  void producer() {                                                   │   │
│  │      while (running) {                                               │   │
│  │          auto dp = scoop.read();                                     │   │
│  │          queue.try_enqueue(std::move(dp));  // ~40ns                 │   │
│  │      }                                                               │   │
│  │  }                                                                   │   │
│  │                                                                      │   │
│  │  // Consumer thread                                                  │   │
│  │  void consumer() {                                                   │   │
│  │      while (running) {                                               │   │
│  │          if (auto dp = queue.try_dequeue()) {  // ~40ns              │   │
│  │              process(*dp);                                           │   │
│  │          }                                                           │   │
│  │      }                                                               │   │
│  │  }                                                                   │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Avantages:                          Inconvénients:                         │
│  ✓ Latence minimale (50-100ns)       ✗ Pas de topic routing                │
│  ✓ Zéro allocation                   ✗ Couplage fort                       │
│  ✓ Déterministe                      ✗ Pas de subscriptions dynamiques     │
│  ✓ Simple à debugger                 ✗ 1 queue par connexion               │
│                                                                             │
│  Performance:                                                               │
│  ┌────────────┬──────────┬──────────┬──────────────┐                       │
│  │ Queue Type │ P50      │ P99      │ Throughput   │                       │
│  ├────────────┼──────────┼──────────┼──────────────┤                       │
│  │ SPSC       │ 25ns     │ 100ns    │ 25M ops/s    │                       │
│  │ MPSC       │ 60ns     │ 200ns    │ 15M ops/s    │                       │
│  │ MPMC       │ 100ns    │ 500ns    │ 8M ops/s     │                       │
│  └────────────┴──────────┴──────────┴──────────────┘                       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.3 Alternative 2: Direct Function Calls

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                ALTERNATIVE 2: APPELS DIRECTS (SYNCHRONE)                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  USE CASE: Faible volume, simplicité maximale, même thread                  │
│                                                                             │
│  Architecture:                                                              │
│  ┌────────────┐     direct call      ┌────────────┐                        │
│  │   Scoop    │────────────────────▶│  Processor │                        │
│  │            │     processor(dp)    │            │                        │
│  └────────────┘                      └────────────┘                        │
│                                                                             │
│  Code:                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  class Pipeline {                                                    │   │
│  │      IProcessor* processor_;                                         │   │
│  │                                                                      │   │
│  │  public:                                                             │   │
│  │      void on_data(DataPoint dp) {                                    │   │
│  │          processor_->process(std::move(dp));  // ~10ns               │   │
│  │      }                                                               │   │
│  │  };                                                                  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Avantages:                          Inconvénients:                         │
│  ✓ Latence la plus basse (~10ns)     ✗ Bloque le producteur                │
│  ✓ Zéro overhead                     ✗ Pas de buffering                    │
│  ✓ Simple à comprendre               ✗ Couplage très fort                  │
│  ✓ Debugging trivial                 ✗ Pas thread-safe                     │
│                                                                             │
│  Quand utiliser:                                                            │
│  • < 1000 msg/s                                                            │
│  • Traitement rapide et garanti                                            │
│  • Single-threaded design                                                   │
│  • Prototypage rapide                                                       │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.4 Alternative 3: Fan-Out Pattern

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ALTERNATIVE 3: FAN-OUT EXPLICITE                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  USE CASE: 1 producteur, N consommateurs fixes                              │
│                                                                             │
│  Architecture:                                                              │
│                         ┌─────────────┐                                    │
│                    ┌───▶│  Consumer1  │                                    │
│                    │    └─────────────┘                                    │
│  ┌──────────┐      │    ┌─────────────┐                                    │
│  │ Producer │──────┼───▶│  Consumer2  │                                    │
│  └──────────┘      │    └─────────────┘                                    │
│                    │    ┌─────────────┐                                    │
│                    └───▶│  Consumer3  │                                    │
│                         └─────────────┘                                    │
│                                                                             │
│  Code:                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  template<size_t N>                                                  │   │
│  │  class FanOut {                                                      │   │
│  │      std::array<SPSCQueue<DataPoint, 4096>, N> queues_;              │   │
│  │                                                                      │   │
│  │  public:                                                             │   │
│  │      void broadcast(const DataPoint& dp) {                           │   │
│  │          for (auto& q : queues_) {                                   │   │
│  │              q.try_enqueue(dp);  // Copy to each consumer            │   │
│  │          }                                                           │   │
│  │      }                                                               │   │
│  │                                                                      │   │
│  │      SPSCQueue<DataPoint, 4096>& consumer_queue(size_t i) {          │   │
│  │          return queues_[i];                                          │   │
│  │      }                                                               │   │
│  │  };                                                                  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Avantages:                          Inconvénients:                         │
│  ✓ SPSC per consumer (optimal)       ✗ N copies du message                 │
│  ✓ Isolation des consommateurs       ✗ Consommateurs fixés compile-time    │
│  ✓ Pas de contention                 ✗ Mémoire proportionnelle à N         │
│  ✓ Backpressure par consumer         ✗ Pas de filtering                    │
│                                                                             │
│  Performance:                                                               │
│  • Broadcast 3 consumers: ~150ns (3×50ns)                                  │
│  • Broadcast 10 consumers: ~500ns (10×50ns)                                │
│  • Scales linearly with N                                                   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 4.5 Alternative 4: Observer Pattern

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    ALTERNATIVE 4: OBSERVER PATTERN                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  USE CASE: Subscriptions dynamiques, même thread, faible volume             │
│                                                                             │
│  Architecture:                                                              │
│  ┌──────────────────────────────────────────────────┐                      │
│  │                   Subject                         │                      │
│  │  ┌────────────────────────────────────────────┐  │                      │
│  │  │ observers_: vector<function<void(DP&)>>    │  │                      │
│  │  └────────────────────────────────────────────┘  │                      │
│  │       │              │              │            │                      │
│  │       ▼              ▼              ▼            │                      │
│  │  ┌────────┐    ┌────────┐    ┌────────┐         │                      │
│  │  │Observer│    │Observer│    │Observer│         │                      │
│  │  │   1    │    │   2    │    │   3    │         │                      │
│  │  └────────┘    └────────┘    └────────┘         │                      │
│  └──────────────────────────────────────────────────┘                      │
│                                                                             │
│  Code:                                                                      │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  class DataSubject {                                                 │   │
│  │      std::vector<std::function<void(const DataPoint&)>> observers_;  │   │
│  │      std::shared_mutex mutex_;                                       │   │
│  │                                                                      │   │
│  │  public:                                                             │   │
│  │      void subscribe(std::function<void(const DataPoint&)> cb) {      │   │
│  │          std::unique_lock lock(mutex_);                              │   │
│  │          observers_.push_back(std::move(cb));                        │   │
│  │      }                                                               │   │
│  │                                                                      │   │
│  │      void notify(const DataPoint& dp) {                              │   │
│  │          std::shared_lock lock(mutex_);                              │   │
│  │          for (auto& obs : observers_) {                              │   │
│  │              obs(dp);  // Synchronous call                           │   │
│  │          }                                                           │   │
│  │      }                                                               │   │
│  │  };                                                                  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
│  Avantages:                          Inconvénients:                         │
│  ✓ Subscriptions dynamiques          ✗ Lock sur notify() path              │
│  ✓ Simple à implémenter              ✗ Observateurs bloquants              │
│  ✓ Pas de buffers                    ✗ Pas thread-safe pour perf           │
│  ✓ Callbacks flexibles               ✗ Exception propagation issues        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. TABLEAU COMPARATIF COMPLET

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    COMPARAISON COMPLÈTE DES ALTERNATIVES                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌──────────────┬────────┬────────┬────────┬─────────┬───────────────────┐ │
│  │ Critère      │MessageBus│DirectQ│Direct │ FanOut │ Observer         │ │
│  │              │          │(SPSC) │ Call  │        │                  │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Latence P50  │ 500ns    │ 50ns  │ 10ns  │ 150ns  │ 100ns            │ │
│  │ Latence P99  │ 5µs      │ 200ns │ 50ns  │ 600ns  │ 1µs (lock)       │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Throughput   │ 5M/s     │ 25M/s │ 50M/s │ 15M/s  │ 10M/s            │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Memory (base)│ 5MB      │ 100KB │ 0     │ N×100KB│ 1KB              │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Producers    │ Any      │ 1     │ 1     │ 1      │ Any              │ │
│  │ Consumers    │ Any      │ 1     │ 1     │ N fixed│ Any              │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Topic filter │ ✅ Yes   │ ❌ No │ ❌ No │ ❌ No  │ ⚠️ Manual        │ │
│  │ Wildcards    │ ✅ Yes   │ ❌ No │ ❌ No │ ❌ No  │ ❌ No            │ │
│  │ Priority     │ ✅ Yes   │ ❌ No │ ❌ No │ ❌ No  │ ❌ No            │ │
│  │ Dynamic sub  │ ✅ Yes   │ ❌ No │ ❌ No │ ❌ No  │ ✅ Yes           │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Lock-free    │ ✅ Yes   │ ✅ Yes│ N/A   │ ✅ Yes │ ❌ No (rwlock)   │ │
│  │ Backpressure │ ✅ Yes   │ ✅ Yes│ ❌ No │ ✅ Yes │ ❌ No            │ │
│  │ Batching     │ ✅ Yes   │ ⚠️ Man│ ❌ No │ ⚠️ Man │ ❌ No            │ │
│  ├──────────────┼──────────┼───────┼───────┼────────┼──────────────────┤ │
│  │ Complexity   │ High     │ Low   │ Minimal│ Medium│ Low              │ │
│  │ Debug ease   │ Medium   │ High  │ Highest│ High  │ Medium           │ │
│  └──────────────┴──────────┴───────┴───────┴────────┴──────────────────┘ │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. ARBRE DE DÉCISION

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         ARBRE DE DÉCISION                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                        ┌─────────────────────┐                              │
│                        │ Latence critique?   │                              │
│                        │     (< 1µs P99)     │                              │
│                        └──────────┬──────────┘                              │
│                                   │                                         │
│                    ┌──────────────┴──────────────┐                         │
│                    ▼                             ▼                          │
│                  [OUI]                         [NON]                        │
│                    │                             │                          │
│         ┌─────────┴─────────┐         ┌─────────┴─────────┐                │
│         ▼                   ▼         ▼                   ▼                 │
│   ┌───────────┐      ┌───────────┐ ┌───────────┐   ┌───────────┐           │
│   │ 1 Prod?   │      │Multi-prod?│ │Topic rout?│   │ Dynamic   │           │
│   └─────┬─────┘      └─────┬─────┘ │ requis?   │   │ subs?     │           │
│         │                  │       └─────┬─────┘   └─────┬─────┘           │
│    ┌────┴────┐        ┌────┴────┐       │               │                  │
│    ▼         ▼        ▼         ▼       │               │                  │
│  [OUI]     [NON]    [OUI]     [NON]     │               │                  │
│    │         │        │         │   ┌───┴───┐       ┌───┴───┐              │
│    ▼         ▼        ▼         ▼   ▼       ▼       ▼       ▼              │
│ ┌──────┐ ┌──────┐ ┌──────┐ ┌──────┐[OUI]  [NON]   [OUI]   [NON]           │
│ │ SPSC │ │ MPSC │ │ MPSC │ │ MPMC │  │      │       │       │              │
│ │Queue │ │Queue │ │Queue │ │Queue │  │      │       │       │              │
│ └──────┘ └──────┘ └──────┘ └──────┘  │      │       │       │              │
│                                       ▼      ▼       ▼       ▼              │
│                                   ┌──────────────┐ ┌────────────┐          │
│                                   │  MESSAGE     │ │  FanOut /  │          │
│                                   │    BUS       │ │  Observer  │          │
│                                   └──────────────┘ └────────────┘          │
│                                                                             │
│  Résumé:                                                                    │
│  • Latence < 1µs + 1:1       → SPSCQueue                                   │
│  • Latence < 1µs + N:1       → MPSCQueue                                   │
│  • Topic routing requis      → MessageBus                                  │
│  • Dynamic + pas RT          → Observer                                    │
│  • 1:N fixe + low latency    → FanOut                                      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 7. EXEMPLES CONCRETS DE MIGRATION

### 7.1 Remplacer MessageBus par SPSCQueue

```cpp
// AVANT: MessageBus (overkill pour 1:1)
class ModbusToInflux_Before {
    MessageBus bus_;

    void setup() {
        bus_.subscribe("modbus/data", [this](const Message& m) {
            influx_.write(m.payload);
        });
    }

    void on_modbus_data(DataPoint dp) {
        bus_.publish("modbus/data", std::move(dp));  // ~500ns overhead
    }
};

// APRÈS: SPSCQueue (10x plus rapide)
class ModbusToInflux_After {
    SPSCQueue<DataPoint, 4096> queue_;
    std::thread consumer_;

    void setup() {
        consumer_ = std::thread([this]() {
            while (running_) {
                if (auto dp = queue_.try_dequeue()) {
                    influx_.write(*dp);
                }
            }
        });
    }

    void on_modbus_data(DataPoint dp) {
        queue_.try_enqueue(std::move(dp));  // ~50ns overhead
    }
};
```

### 7.2 Remplacer MessageBus par FanOut

```cpp
// AVANT: MessageBus (overhead topic matching)
class SensorBroadcast_Before {
    MessageBus bus_;

    void setup() {
        bus_.subscribe("sensors/#", kafka_handler);
        bus_.subscribe("sensors/#", influx_handler);
        bus_.subscribe("sensors/#", alert_handler);
    }

    void on_sensor(DataPoint dp) {
        bus_.publish("sensors/temp", std::move(dp));  // 1 publish, 3 deliveries
    }
};

// APRÈS: FanOut explicite (3x plus rapide)
class SensorBroadcast_After {
    FanOut<3> fanout_;  // 3 consumers

    void setup() {
        start_consumer(fanout_.queue(0), kafka_handler);
        start_consumer(fanout_.queue(1), influx_handler);
        start_consumer(fanout_.queue(2), alert_handler);
    }

    void on_sensor(DataPoint dp) {
        fanout_.broadcast(dp);  // 3 copies, ~150ns total
    }
};
```

---

## 8. RÉSUMÉ

| Situation | Solution Recommandée | Pourquoi |
|-----------|---------------------|----------|
| Pipeline 1:1 simple | `SPSCQueue` | Performance maximale |
| N sources → 1 traitement | `MPSCQueue` | Lock-free, efficace |
| 1 source → N fixes | `FanOut<N>` | SPSC par consommateur |
| Topics dynamiques | `MessageBus` | Routing flexible |
| Faible volume (<1K/s) | Direct call | Simplicité |
| Plugins hot-reload | `MessageBus` | Subscriptions dynamiques |
| Hard real-time (<1µs) | Direct queues | Déterminisme |
| Très haut throughput | `MessageBus` + sharding | Scalabilité |

**Règle d'or:** Utilisez le MessageBus seulement quand vous avez besoin de ses features (topic routing, wildcards, dynamic subscriptions). Sinon, utilisez des queues directes.

---

*Document généré le 2025-12-18*
