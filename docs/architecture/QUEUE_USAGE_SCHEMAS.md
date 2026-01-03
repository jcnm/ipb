# Guide Complet: Quand Utiliser Chaque Type de Queue Lock-Free

## Du Plus Simple au Plus Complexe

---

## 1. SPSC QUEUE (Single Producer Single Consumer)

### Caractéristiques
```
┌────────────────────────────────────────────────────────────────┐
│                        SPSC QUEUE                              │
├────────────────────────────────────────────────────────────────┤
│  Producteurs: 1 (EXACTEMENT UN)                                │
│  Consommateurs: 1 (EXACTEMENT UN)                              │
│  Latence: 25-50ns                                              │
│  Throughput: 20-30M ops/s                                      │
│  Complexité: O(1) wait-free                                    │
│  Contention: ZÉRO                                              │
└────────────────────────────────────────────────────────────────┘
```

### Schema 1.1: Pipeline I/O Simple
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Lecture capteur → Traitement                                      │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   Thread 1 (I/O)              Queue                    Thread 2 (CPU)      │
│  ┌─────────────┐         ┌──────────────┐           ┌─────────────┐        │
│  │   Modbus    │         │              │           │  Processor  │        │
│  │   Reader    │────────▶│  SPSC<DP>    │──────────▶│   (Filter,  │        │
│  │             │         │  Cap: 1024   │           │   Transform)│        │
│  └─────────────┘         └──────────────┘           └─────────────┘        │
│       │                        │                          │                │
│       │                        │                          │                │
│   read() ~1ms              enqueue ~40ns             process() ~10µs       │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  SPSCQueue<DataPoint, 1024> queue;                                 │    │
│  │                                                                    │    │
│  │  // Producer thread                                                │    │
│  │  void io_thread() {                                                │    │
│  │      while (running) {                                             │    │
│  │          DataPoint dp = modbus.read_register(addr);                │    │
│  │          queue.try_enqueue(std::move(dp));                         │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  │                                                                    │    │
│  │  // Consumer thread                                                │    │
│  │  void cpu_thread() {                                               │    │
│  │      while (running) {                                             │    │
│  │          if (auto dp = queue.try_dequeue()) {                      │    │
│  │              processor.process(*dp);                               │    │
│  │          }                                                         │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 1.2: Pipeline Multi-Étages
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Acquisition → Filtrage → Stockage                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ┌────────┐    SPSC     ┌────────┐    SPSC     ┌────────┐    SPSC    ┌────┐│
│  │ Sensor │───────────▶│ Filter │───────────▶│Transform│──────────▶│ DB ││
│  │ Reader │  Q1: 1024  │ Stage  │  Q2: 512   │ Stage   │  Q3: 256  │Write││
│  └────────┘            └────────┘            └────────┘            └────┘│
│   Thread 1              Thread 2              Thread 3            Thread 4 │
│                                                                             │
│  Latence totale: ~150ns (3 × 50ns)                                         │
│  Throughput: limité par l'étage le plus lent                               │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  SPSCQueue<RawData, 1024> q1;                                      │    │
│  │  SPSCQueue<FilteredData, 512> q2;                                  │    │
│  │  SPSCQueue<TransformedData, 256> q3;                               │    │
│  │                                                                    │    │
│  │  void stage1() { while(run) { q1.enqueue(sensor.read()); } }       │    │
│  │  void stage2() { while(run) { if(auto d=q1.dequeue())              │    │
│  │                                 q2.enqueue(filter(*d)); } }        │    │
│  │  void stage3() { while(run) { if(auto d=q2.dequeue())              │    │
│  │                                 q3.enqueue(transform(*d)); } }     │    │
│  │  void stage4() { while(run) { if(auto d=q3.dequeue())              │    │
│  │                                 db.write(*d); } }                  │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 1.3: Network I/O Découplé
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Réception TCP → Parsing → Application                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│       Network Thread                                   App Thread           │
│  ┌─────────────────────┐                        ┌─────────────────────┐    │
│  │  while(running) {   │                        │  while(running) {   │    │
│  │    recv(socket, buf)│      SPSC<Packet>      │    if(auto p=q.pop)│    │
│  │    parse(buf)       │     ┌──────────┐       │      handle(*p);   │    │
│  │    q.push(packet);  │────▶│ Cap:4096 │──────▶│  }                 │    │
│  │  }                  │     └──────────┘       │                    │    │
│  └─────────────────────┘                        └─────────────────────┘    │
│                                                                             │
│  Pourquoi SPSC?                                                             │
│  • 1 socket = 1 thread récepteur                                           │
│  • 1 handler applicatif par connexion                                       │
│  • Latence minimale critique pour trading/gaming                            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. MPSC QUEUE (Multiple Producers Single Consumer)

### Caractéristiques
```
┌────────────────────────────────────────────────────────────────┐
│                        MPSC QUEUE                              │
├────────────────────────────────────────────────────────────────┤
│  Producteurs: N (plusieurs)                                    │
│  Consommateurs: 1 (EXACTEMENT UN)                              │
│  Latence: 60-120ns                                             │
│  Throughput: 10-18M ops/s                                      │
│  Complexité: O(1) avec retry borné                             │
│  Contention: Faible (CAS sur head seulement)                   │
└────────────────────────────────────────────────────────────────┘
```

### Schema 2.1: Logging Centralisé
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Multi-threads → Logger unique                                     │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ┌─────────────┐                                                          │
│   │  Thread 1   │──┐                                                       │
│   │  (Modbus)   │  │                                                       │
│   └─────────────┘  │                                                       │
│   ┌─────────────┐  │       ┌──────────────┐        ┌─────────────┐         │
│   │  Thread 2   │──┼──────▶│  MPSC<Log>   │───────▶│   Logger    │         │
│   │  (OPC UA)   │  │       │  Cap: 8192   │        │  (File/Net) │         │
│   └─────────────┘  │       └──────────────┘        └─────────────┘         │
│   ┌─────────────┐  │            ▲                    Thread unique         │
│   │  Thread 3   │──┤            │                                          │
│   │  (MQTT)     │  │       Lock-free                                       │
│   └─────────────┘  │       enqueue                                         │
│   ┌─────────────┐  │                                                       │
│   │  Thread N   │──┘                                                       │
│   │  (HTTP)     │                                                          │
│   └─────────────┘                                                          │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  MPSCQueue<LogEntry, 8192> log_queue;                              │    │
│  │                                                                    │    │
│  │  // N'importe quel thread peut logger                              │    │
│  │  void log(Level lvl, string msg) {                                 │    │
│  │      log_queue.try_enqueue({lvl, msg, Timestamp::now()});          │    │
│  │  }                                                                 │    │
│  │                                                                    │    │
│  │  // Un seul thread écrit                                           │    │
│  │  void logger_thread() {                                            │    │
│  │      while (running) {                                             │    │
│  │          if (auto entry = log_queue.try_dequeue()) {               │    │
│  │              file << format(*entry) << "\n";                       │    │
│  │          }                                                         │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 2.2: Agrégation de Capteurs
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: 10 Scoops différents → 1 Router                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│     ┌──────────────┐                                                       │
│     │Modbus Scoop 1│──┐                                                    │
│     └──────────────┘  │                                                    │
│     ┌──────────────┐  │                                                    │
│     │Modbus Scoop 2│──┤                                                    │
│     └──────────────┘  │     ┌─────────────────┐      ┌──────────────┐      │
│     ┌──────────────┐  │     │                 │      │              │      │
│     │ OPC UA Scoop │──┼────▶│ MPSC<DataPoint> │─────▶│    Router    │      │
│     └──────────────┘  │     │   Cap: 16384    │      │   (single)   │      │
│     ┌──────────────┐  │     │                 │      │              │      │
│     │  MQTT Scoop  │──┤     └─────────────────┘      └──────────────┘      │
│     └──────────────┘  │                                                    │
│     ┌──────────────┐  │                                                    │
│     │Sparkplug Scoop──┘                                                    │
│     └──────────────┘                                                       │
│                                                                             │
│  Throughput: 10 scoops × 10K msg/s = 100K msg/s vers router                │
│  Latence ajoutée: ~100ns par message                                       │
│                                                                             │
│  Configuration:                                                             │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  struct ScoopAggregator {                                          │    │
│  │      MPSCQueue<DataPoint, 16384> queue_;                           │    │
│  │      std::vector<std::unique_ptr<IScoop>> scoops_;                 │    │
│  │                                                                    │    │
│  │      void start() {                                                │    │
│  │          for (auto& scoop : scoops_) {                             │    │
│  │              scoop->set_callback([this](DataPoint dp) {            │    │
│  │                  queue_.try_enqueue(std::move(dp));                │    │
│  │              });                                                   │    │
│  │              scoop->start();  // Chaque scoop dans son thread      │    │
│  │          }                                                         │    │
│  │          router_thread_ = std::thread([this]{ route_loop(); });    │    │
│  │      }                                                             │    │
│  │                                                                    │    │
│  │      void route_loop() {                                           │    │
│  │          while (running_) {                                        │    │
│  │              if (auto dp = queue_.try_dequeue()) {                 │    │
│  │                  router_.route(*dp);                               │    │
│  │              }                                                     │    │
│  │          }                                                         │    │
│  │      }                                                             │    │
│  │  };                                                                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 2.3: Event Sourcing
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Commandes distribuées → Event Store séquentiel                    │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   API Handlers (N threads)                                                  │
│   ┌─────────┐ ┌─────────┐ ┌─────────┐                                      │
│   │Handler 1│ │Handler 2│ │Handler N│                                      │
│   └────┬────┘ └────┬────┘ └────┬────┘                                      │
│        │           │           │                                            │
│        └───────────┼───────────┘                                            │
│                    ▼                                                        │
│           ┌────────────────┐                                               │
│           │  MPSC<Command> │                                               │
│           │   Cap: 32768   │                                               │
│           └───────┬────────┘                                               │
│                   │                                                         │
│                   ▼                                                         │
│           ┌────────────────┐        ┌────────────────┐                     │
│           │ Command        │───────▶│  Event Store   │                     │
│           │ Processor      │        │  (Sequential   │                     │
│           │ (1 thread)     │        │   writes)      │                     │
│           └────────────────┘        └────────────────┘                     │
│                                                                             │
│  Garanties:                                                                 │
│  • Ordre total des événements préservé                                     │
│  • Un seul writer → pas de conflits                                        │
│  • Throughput: 500K commands/s                                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 3. MPMC QUEUE (Multiple Producers Multiple Consumers)

### Caractéristiques
```
┌────────────────────────────────────────────────────────────────┐
│                        MPMC QUEUE                              │
├────────────────────────────────────────────────────────────────┤
│  Producteurs: N (plusieurs)                                    │
│  Consommateurs: M (plusieurs)                                  │
│  Latence: 80-200ns                                             │
│  Throughput: 5-12M ops/s                                       │
│  Complexité: O(1) avec retry borné                             │
│  Contention: Moyenne (CAS sur head ET tail)                    │
└────────────────────────────────────────────────────────────────┘
```

### Schema 3.1: Worker Pool Simple
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Distribution de tâches vers pool de workers                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│      Producers                  Queue                    Workers            │
│   ┌───────────┐                                      ┌───────────┐         │
│   │ Request 1 │──┐                              ┌───▶│ Worker 1  │         │
│   └───────────┘  │                              │    └───────────┘         │
│   ┌───────────┐  │     ┌──────────────────┐     │    ┌───────────┐         │
│   │ Request 2 │──┼────▶│   MPMC<Task>     │─────┼───▶│ Worker 2  │         │
│   └───────────┘  │     │   Cap: 4096      │     │    └───────────┘         │
│   ┌───────────┐  │     └──────────────────┘     │    ┌───────────┐         │
│   │ Request N │──┘                              └───▶│ Worker N  │         │
│   └───────────┘                                      └───────────┘         │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  MPMCQueue<Task, 4096> task_queue;                                 │    │
│  │                                                                    │    │
│  │  // Producers (any thread)                                         │    │
│  │  void submit_task(Task t) {                                        │    │
│  │      while (!task_queue.try_enqueue(std::move(t))) {               │    │
│  │          std::this_thread::yield();                                │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  │                                                                    │    │
│  │  // Workers                                                        │    │
│  │  void worker_thread() {                                            │    │
│  │      while (running) {                                             │    │
│  │          if (auto task = task_queue.try_dequeue()) {               │    │
│  │              task->execute();                                      │    │
│  │          } else {                                                  │    │
│  │              std::this_thread::yield();                            │    │
│  │          }                                                         │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  Load balancing automatique: premier worker libre prend la tâche            │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 3.2: Pipeline Parallèle (Fork-Join)
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Traitement parallèle avec merge                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                          FORK (1 → N)                                       │
│                                                                             │
│                       ┌──────────────────┐                                 │
│                       │   MPMC<Work>     │                                 │
│                       │   (distribute)   │                                 │
│                       └────────┬─────────┘                                 │
│                                │                                            │
│            ┌───────────────────┼───────────────────┐                       │
│            ▼                   ▼                   ▼                        │
│     ┌────────────┐      ┌────────────┐      ┌────────────┐                 │
│     │  Worker 1  │      │  Worker 2  │      │  Worker 3  │                 │
│     │ (process)  │      │ (process)  │      │ (process)  │                 │
│     └─────┬──────┘      └─────┬──────┘      └─────┬──────┘                 │
│           │                   │                   │                         │
│           └───────────────────┼───────────────────┘                         │
│                               ▼                                             │
│                       ┌──────────────────┐                                 │
│                       │   MPMC<Result>   │                                 │
│                       │   (collect)      │                                 │
│                       └────────┬─────────┘                                 │
│                                │                                            │
│                          JOIN (N → 1)                                       │
│                                ▼                                            │
│                       ┌──────────────────┐                                 │
│                       │    Aggregator    │                                 │
│                       └──────────────────┘                                 │
│                                                                             │
│  Exemple concret: Traitement d'images                                       │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  MPMCQueue<ImageChunk, 1024> work_queue;                           │    │
│  │  MPMCQueue<ProcessedChunk, 1024> result_queue;                     │    │
│  │                                                                    │    │
│  │  // Splitter                                                       │    │
│  │  void split_image(Image& img) {                                    │    │
│  │      for (auto chunk : img.split(NUM_CHUNKS)) {                    │    │
│  │          work_queue.try_enqueue(chunk);                            │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  │                                                                    │    │
│  │  // Workers (N threads)                                            │    │
│  │  void process_worker() {                                           │    │
│  │      while (running) {                                             │    │
│  │          if (auto chunk = work_queue.try_dequeue()) {              │    │
│  │              auto result = apply_filter(*chunk);                   │    │
│  │              result_queue.try_enqueue(result);                     │    │
│  │          }                                                         │    │
│  │      }                                                             │    │
│  │  }                                                                 │    │
│  │                                                                    │    │
│  │  // Merger                                                         │    │
│  │  Image merge_results() {                                           │    │
│  │      std::vector<ProcessedChunk> chunks;                           │    │
│  │      while (chunks.size() < NUM_CHUNKS) {                          │    │
│  │          if (auto c = result_queue.try_dequeue())                  │    │
│  │              chunks.push_back(*c);                                 │    │
│  │      }                                                             │    │
│  │      return Image::merge(chunks);                                  │    │
│  │  }                                                                 │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 3.3: Pub/Sub Sans Topics (Broadcast)
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Multiple publishers, Multiple subscribers (competing consumers)  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   Publishers                    Queue                    Subscribers        │
│   ┌─────────┐                                           ┌─────────┐        │
│   │Sensor 1 │──┐                                   ┌───▶│ Writer1 │        │
│   └─────────┘  │                                   │    │ (Kafka) │        │
│   ┌─────────┐  │     ┌─────────────────────┐      │    └─────────┘        │
│   │Sensor 2 │──┼────▶│    MPMC<Event>      │──────┤    ┌─────────┐        │
│   └─────────┘  │     │    Cap: 8192        │      ├───▶│ Writer2 │        │
│   ┌─────────┐  │     └─────────────────────┘      │    │ (InfluxDB│        │
│   │Sensor 3 │──┘                                   │    └─────────┘        │
│   └─────────┘                                      │    ┌─────────┐        │
│                                                    └───▶│ Writer3 │        │
│   NOTE: Chaque message va à UN SEUL subscriber!         │ (File)  │        │
│   (competing consumers, pas broadcast)                  └─────────┘        │
│                                                                             │
│   Use case: Load balancing entre writers de même type                      │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 4. BOUNDED MPMC QUEUE (Capacité Dynamique)

### Caractéristiques
```
┌────────────────────────────────────────────────────────────────┐
│                    BOUNDED MPMC QUEUE                          │
├────────────────────────────────────────────────────────────────┤
│  Producteurs: N (plusieurs)                                    │
│  Consommateurs: M (plusieurs)                                  │
│  Latence: 100-250ns (indirection)                              │
│  Throughput: 4-10M ops/s                                       │
│  Capacité: Runtime (power of 2)                                │
│  Mémoire: Heap allocated                                       │
└────────────────────────────────────────────────────────────────┘
```

### Schema 4.1: Queue Configurable Runtime
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Capacité définie par configuration YAML                           │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  Config YAML:                                                               │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  queue:                                                            │    │
│  │    capacity: 16384   # Runtime configurable!                       │    │
│  │    type: mpmc                                                      │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  // Capacity NOT known at compile time                             │    │
│  │  class DynamicRouter {                                             │    │
│  │      std::unique_ptr<BoundedMPMCQueue<DataPoint>> queue_;          │    │
│  │                                                                    │    │
│  │  public:                                                           │    │
│  │      void configure(const Config& cfg) {                           │    │
│  │          size_t cap = cfg.get<size_t>("queue.capacity", 4096);     │    │
│  │          queue_ = std::make_unique<BoundedMPMCQueue<DataPoint>>(   │    │
│  │              cap  // Runtime value!                                │    │
│  │          );                                                        │    │
│  │      }                                                             │    │
│  │  };                                                                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
│  Quand utiliser vs MPMC template:                                          │
│  • BoundedMPMC: config files, plugins, user-defined sizes                  │
│  • MPMC<N>: fixed architecture, compile-time optimization                  │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 4.2: Elastic Scaling
```
┌─────────────────────────────────────────────────────────────────────────────┐
│  EXEMPLE: Ajustement dynamique selon la charge                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│    Load Monitor                                                             │
│         │                                                                   │
│         ▼                                                                   │
│   ┌──────────────┐                                                         │
│   │ if load > 80%│──────▶ Create larger queue                              │
│   │ if load < 20%│──────▶ Shrink queue (memory)                            │
│   └──────────────┘                                                         │
│                                                                             │
│   Queues Pool:                                                              │
│   ┌─────────────────────────────────────────────────────────────────┐      │
│   │  Small:  BoundedMPMC(1024)   ← Low load                         │      │
│   │  Medium: BoundedMPMC(8192)   ← Normal load                      │      │
│   │  Large:  BoundedMPMC(65536)  ← High load                        │      │
│   │  XLarge: BoundedMPMC(262144) ← Peak load                        │      │
│   └─────────────────────────────────────────────────────────────────┘      │
│                                                                             │
│  Code:                                                                      │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │  class ElasticQueue {                                              │    │
│  │      std::atomic<BoundedMPMCQueue<T>*> active_;                    │    │
│  │      std::vector<std::unique_ptr<BoundedMPMCQueue<T>>> pool_;      │    │
│  │                                                                    │    │
│  │      void scale_up() {                                             │    │
│  │          size_t new_cap = active_.load()->capacity() * 2;          │    │
│  │          auto new_q = std::make_unique<BoundedMPMCQueue<T>>(       │    │
│  │              new_cap                                               │    │
│  │          );                                                        │    │
│  │          // Drain old → new, then swap                             │    │
│  │          drain_and_swap(new_q.get());                              │    │
│  │          pool_.push_back(std::move(new_q));                        │    │
│  │      }                                                             │    │
│  │  };                                                                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. COMPARAISON ET DÉCISION

### Tableau Récapitulatif
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    TABLEAU DE DÉCISION RAPIDE                               │
├───────────────┬──────────┬──────────┬──────────┬─────────────┬─────────────┤
│ Critère       │   SPSC   │   MPSC   │   MPMC   │BoundedMPMC  │ MessageBus  │
├───────────────┼──────────┼──────────┼──────────┼─────────────┼─────────────┤
│ Producers     │    1     │    N     │    N     │     N       │     N       │
│ Consumers     │    1     │    1     │    M     │     M       │     M       │
├───────────────┼──────────┼──────────┼──────────┼─────────────┼─────────────┤
│ Latency P50   │   25ns   │   60ns   │  100ns   │   120ns     │   500ns     │
│ Latency P99   │  100ns   │  200ns   │  400ns   │   500ns     │    5µs      │
├───────────────┼──────────┼──────────┼──────────┼─────────────┼─────────────┤
│ Throughput    │  25M/s   │  15M/s   │   8M/s   │    6M/s     │    5M/s     │
├───────────────┼──────────┼──────────┼──────────┼─────────────┼─────────────┤
│ Contention    │   None   │   Low    │  Medium  │   Medium    │    Low      │
│ Memory        │  Stack   │  Stack   │  Stack   │    Heap     │    Heap     │
│ Capacity      │ Compile  │ Compile  │ Compile  │   Runtime   │   Runtime   │
├───────────────┼──────────┼──────────┼──────────┼─────────────┼─────────────┤
│ Topic routing │    ❌    │    ❌    │    ❌    │     ❌      │     ✅      │
│ Wildcards     │    ❌    │    ❌    │    ❌    │     ❌      │     ✅      │
│ Priority      │    ❌    │    ❌    │    ❌    │     ❌      │     ✅      │
│ Dynamic sub   │    ❌    │    ❌    │    ❌    │     ❌      │     ✅      │
└───────────────┴──────────┴──────────┴──────────┴─────────────┴─────────────┘
```

### Arbre de Décision
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         ARBRE DE DÉCISION                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│                    Combien de PRODUCTEURS?                                  │
│                             │                                               │
│              ┌──────────────┴──────────────┐                               │
│              ▼                             ▼                                │
│         [UN SEUL]                    [PLUSIEURS]                           │
│              │                             │                                │
│              ▼                             ▼                                │
│     Combien de                    Combien de                               │
│     CONSOMMATEURS?                CONSOMMATEURS?                           │
│         │                              │                                    │
│    ┌────┴────┐                   ┌─────┴─────┐                             │
│    ▼         ▼                   ▼           ▼                              │
│ [UN]     [PLUSIEURS]          [UN]      [PLUSIEURS]                        │
│    │         │                   │           │                              │
│    ▼         ▼                   ▼           ▼                              │
│ ┌──────┐  ┌──────────┐      ┌──────┐    ┌─────────────┐                    │
│ │ SPSC │  │ FanOut + │      │ MPSC │    │Capacité fixe│                    │
│ │      │  │ N×SPSC   │      │      │    │compile-time?│                    │
│ └──────┘  └──────────┘      └──────┘    └──────┬──────┘                    │
│                                           ┌────┴────┐                       │
│                                           ▼         ▼                       │
│                                        [OUI]      [NON]                     │
│                                           │         │                       │
│                                           ▼         ▼                       │
│                                      ┌──────┐  ┌─────────┐                 │
│                                      │ MPMC │  │ Bounded │                 │
│                                      │ <N>  │  │  MPMC   │                 │
│                                      └──────┘  └─────────┘                 │
│                                                                             │
│  BONUS: Besoin de topic routing / wildcards / priorities?                  │
│         ──────────▶ MessageBus                                             │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 6. EXEMPLES INDUSTRIELS COMPLEXES

### Schema 6.1: Gateway IoT Complet
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      INDUSTRIAL IOT GATEWAY                                 │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  LAYER 1: ACQUISITION (SPSC per device)                                     │
│  ════════════════════════════════════════════════════════════════════════  │
│                                                                             │
│  ┌─────────┐    SPSC     ┌─────────┐    SPSC     ┌─────────┐               │
│  │ PLC 1   │───────────▶│ Parser1 │───────────▶│         │               │
│  │ (Modbus)│  Q:512      │         │  Q:512      │         │               │
│  └─────────┘             └─────────┘             │         │               │
│  ┌─────────┐    SPSC     ┌─────────┐    SPSC     │   A     │               │
│  │ PLC 2   │───────────▶│ Parser2 │───────────▶│   G     │               │
│  │ (Modbus)│  Q:512      │         │  Q:512      │   G     │               │
│  └─────────┘             └─────────┘             │   R     │               │
│  ┌─────────┐    SPSC     ┌─────────┐    SPSC     │   E     │               │
│  │ Robot   │───────────▶│ Parser3 │───────────▶│   G     │               │
│  │ (OPC UA)│  Q:1024     │         │  Q:1024     │   A     │               │
│  └─────────┘             └─────────┘             │   T     │               │
│  ┌─────────┐    SPSC     ┌─────────┐    SPSC     │   O     │               │
│  │ Sensors │───────────▶│ Parser4 │───────────▶│   R     │               │
│  │ (MQTT)  │  Q:2048     │         │  Q:2048     │         │               │
│  └─────────┘             └─────────┘             └────┬────┘               │
│                                                       │                     │
│  LAYER 2: AGGREGATION (MPSC)                         │                     │
│  ════════════════════════════════════════════════════│════════════════════ │
│                                                       │                     │
│                                              MPSC     ▼                     │
│                                         ┌──────────────────┐               │
│                                         │  Unified Queue   │               │
│                                         │    Cap: 16384    │               │
│                                         └────────┬─────────┘               │
│                                                  │                          │
│  LAYER 3: ROUTING (Single Router)                │                          │
│  ════════════════════════════════════════════════│═════════════════════════│
│                                                  ▼                          │
│                                         ┌──────────────────┐               │
│                                         │     ROUTER       │               │
│                                         │  (Rule Engine)   │               │
│                                         └────────┬─────────┘               │
│                                                  │                          │
│  LAYER 4: DISTRIBUTION (MPMC Work Distribution)  │                          │
│  ════════════════════════════════════════════════│═════════════════════════│
│                                                  ▼                          │
│                                         ┌──────────────────┐               │
│                                         │   MPMC Queue     │               │
│                                         │   (by priority)  │               │
│                                         └────────┬─────────┘               │
│                          ┌───────────────────────┼───────────────────┐     │
│                          ▼                       ▼                   ▼      │
│                   ┌────────────┐          ┌────────────┐      ┌──────────┐ │
│                   │  Sink Pool │          │  Sink Pool │      │Sink Pool │ │
│                   │   (Kafka)  │          │ (InfluxDB) │      │ (Alerts) │ │
│                   │  3 workers │          │  2 workers │      │ 1 worker │ │
│                   └────────────┘          └────────────┘      └──────────┘ │
│                                                                             │
│  Performance attendue:                                                      │
│  • Acquisition: 50K points/s par device                                    │
│  • Throughput total: 200K points/s                                         │
│  • Latence end-to-end P99: < 5ms                                           │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 6.2: Trading System (Ultra Low Latency)
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    HIGH-FREQUENCY TRADING PIPELINE                          │
│                        (Latence critique < 1µs)                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│   ⚠️ AUCUN MessageBus - SPSC PARTOUT pour latence minimale                 │
│                                                                             │
│   Market Data Feed                                                          │
│        │                                                                    │
│        ▼                                                                    │
│   ┌─────────────┐     SPSC      ┌─────────────┐     SPSC                   │
│   │   Network   │──────────────▶│   Decoder   │──────────────▶             │
│   │   Receiver  │   Cap:64K     │  (Protocol) │   Cap:32K                  │
│   └─────────────┘   ~30ns       └─────────────┘   ~30ns                    │
│       Core 1                        Core 2                                  │
│                                                                             │
│                    ┌─────────────┐     SPSC      ┌─────────────┐           │
│               ────▶│  Normalizer │──────────────▶│  Strategy   │           │
│                    │ (Transform) │   Cap:16K     │   Engine    │           │
│                    └─────────────┘   ~30ns       └─────────────┘           │
│                        Core 3                        Core 4                 │
│                                                                             │
│                    ┌─────────────┐     SPSC      ┌─────────────┐           │
│               ────▶│   Order     │──────────────▶│   Network   │           │
│                    │  Generator  │   Cap:8K      │   Sender    │           │
│                    └─────────────┘   ~30ns       └─────────────┘           │
│                        Core 5                        Core 6                 │
│                                                                             │
│   Latence totale: 5 × 30ns = 150ns (queue only)                            │
│   + Processing: ~500ns                                                      │
│   = Total: < 1µs tick-to-trade                                             │
│                                                                             │
│   Configuration CPU:                                                        │
│   ┌────────────────────────────────────────────────────────────────────┐   │
│   │  # Isolation des cores pour le pipeline                            │   │
│   │  isolcpus=1,2,3,4,5,6                                              │   │
│   │  # Affinité explicite                                              │   │
│   │  taskset -c 1 ./network_receiver                                   │   │
│   │  taskset -c 2 ./decoder                                            │   │
│   │  taskset -c 3 ./normalizer                                         │   │
│   │  taskset -c 4 ./strategy                                           │   │
│   │  taskset -c 5 ./order_gen                                          │   │
│   │  taskset -c 6 ./network_sender                                     │   │
│   └────────────────────────────────────────────────────────────────────┘   │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Schema 6.3: SCADA System (Mixte)
```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         SCADA ARCHITECTURE                                  │
│              (Mix de tous les types de queues)                              │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ╔═══════════════════════════════════════════════════════════════════════╗ │
│  ║  FIELD LEVEL - SPSC (1 thread par RTU)                                ║ │
│  ╠═══════════════════════════════════════════════════════════════════════╣ │
│  ║  ┌────────┐SPSC ┌────────┐  ┌────────┐SPSC ┌────────┐                ║ │
│  ║  │ RTU 1  │────▶│Poller 1│  │ RTU 2  │────▶│Poller 2│                ║ │
│  ║  └────────┘     └────┬───┘  └────────┘     └────┬───┘                ║ │
│  ║                      │                          │                     ║ │
│  ╚══════════════════════│══════════════════════════│═════════════════════╝ │
│                         │                          │                        │
│  ╔══════════════════════│══════════════════════════│═════════════════════╗ │
│  ║  COMMUNICATION LEVEL │- MPSC (N RTUs → 1 Front-End)                   ║ │
│  ╠══════════════════════│══════════════════════════│═════════════════════╣ │
│  ║                      └────────────┬─────────────┘                     ║ │
│  ║                                   ▼                                   ║ │
│  ║                          ┌────────────────┐                           ║ │
│  ║                          │  MPSC Queue    │                           ║ │
│  ║                          │  (all RTU data)│                           ║ │
│  ║                          └───────┬────────┘                           ║ │
│  ║                                  ▼                                    ║ │
│  ║                          ┌────────────────┐                           ║ │
│  ║                          │  Front-End     │                           ║ │
│  ║                          │  Processor     │                           ║ │
│  ║                          └───────┬────────┘                           ║ │
│  ╚══════════════════════════════════│════════════════════════════════════╝ │
│                                     │                                       │
│  ╔══════════════════════════════════│════════════════════════════════════╗ │
│  ║  DATA LEVEL - MPMC (distribution vers services)                       ║ │
│  ╠══════════════════════════════════│════════════════════════════════════╣ │
│  ║                                  ▼                                    ║ │
│  ║                         ┌─────────────────┐                           ║ │
│  ║                         │   MPMC Queue    │                           ║ │
│  ║                         │ (work distrib)  │                           ║ │
│  ║                         └────────┬────────┘                           ║ │
│  ║                ┌─────────────────┼─────────────────┐                  ║ │
│  ║                ▼                 ▼                 ▼                  ║ │
│  ║         ┌───────────┐    ┌───────────┐     ┌───────────┐             ║ │
│  ║         │ Historian │    │  Alarm    │     │   HMI     │             ║ │
│  ║         │ (Archive) │    │ Processor │     │  Server   │             ║ │
│  ║         └───────────┘    └───────────┘     └───────────┘             ║ │
│  ╚═══════════════════════════════════════════════════════════════════════╝ │
│                                                                             │
│  ╔═══════════════════════════════════════════════════════════════════════╗ │
│  ║  HMI LEVEL - BoundedMPMC (WebSocket connections dynamiques)           ║ │
│  ╠═══════════════════════════════════════════════════════════════════════╣ │
│  ║                                                                       ║ │
│  ║   HMI Server                                                          ║ │
│  ║       │                                                               ║ │
│  ║       ▼                                                               ║ │
│  ║  ┌──────────────────┐                                                 ║ │
│  ║  │  BoundedMPMC     │◀─── Capacité configurable per-client           ║ │
│  ║  │  (per client)    │                                                 ║ │
│  ║  └────────┬─────────┘                                                 ║ │
│  ║     ┌─────┴─────┬─────────┐                                           ║ │
│  ║     ▼           ▼         ▼                                           ║ │
│  ║  ┌──────┐   ┌──────┐  ┌──────┐                                       ║ │
│  ║  │WS 1  │   │WS 2  │  │WS N  │   (N clients dynamiques)              ║ │
│  ║  └──────┘   └──────┘  └──────┘                                       ║ │
│  ╚═══════════════════════════════════════════════════════════════════════╝ │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 7. ANTI-PATTERNS À ÉVITER

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         ANTI-PATTERNS                                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                             │
│  ❌ ANTI-PATTERN 1: MPMC pour 1:1                                          │
│  ────────────────────────────────────────                                   │
│  MAUVAIS:                          BON:                                     │
│  ┌───┐ MPMC  ┌───┐                 ┌───┐ SPSC  ┌───┐                       │
│  │ A │──────▶│ B │                 │ A │──────▶│ B │                       │
│  └───┘       └───┘                 └───┘       └───┘                       │
│  Overhead: 100ns                   Overhead: 40ns                          │
│                                                                             │
│  ❌ ANTI-PATTERN 2: Queue unique globale                                   │
│  ────────────────────────────────────────                                   │
│  MAUVAIS:                                                                   │
│  ┌───┐                                                                     │
│  │ A │──┐                          BON: Queues séparées                    │
│  └───┘  │    ┌────────────┐        ┌───┐ SPSC ┌───┐                        │
│  ┌───┐  ├───▶│ MEGA QUEUE │        │ A │─────▶│ X │                        │
│  │ B │──┤    │ (bottleneck│        └───┘      └───┘                        │
│  └───┘  │    └────────────┘        ┌───┐ SPSC ┌───┐                        │
│  ┌───┐  │                          │ B │─────▶│ Y │                        │
│  │ C │──┘                          └───┘      └───┘                        │
│  └───┘                                                                     │
│                                                                             │
│  ❌ ANTI-PATTERN 3: Queue trop petite                                      │
│  ────────────────────────────────────────                                   │
│  Queue<T, 64> avec producer à 100K/s et consumer à 80K/s                   │
│  → Overflow constant, perte de données                                     │
│                                                                             │
│  RÈGLE: Capacity >= (producer_rate - consumer_rate) × max_latency_burst    │
│                                                                             │
│  ❌ ANTI-PATTERN 4: Polling tight loop sans backoff                        │
│  ────────────────────────────────────────                                   │
│  MAUVAIS:                          BON:                                     │
│  while(running) {                  while(running) {                        │
│    if (auto x = q.pop()) {           if (auto x = q.pop()) {               │
│      process(x);                       process(x);                         │
│    }                                 } else {                               │
│    // 100% CPU spin!                   std::this_thread::yield();          │
│  }                                   }                                      │
│                                    }                                        │
│                                                                             │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 8. RÉSUMÉ FINAL

| Situation | Queue | Pourquoi |
|-----------|-------|----------|
| 1 device → 1 processor | **SPSC** | Latence minimale (25ns) |
| N devices → 1 aggregator | **MPSC** | Pas de contention consumer |
| Work pool (N workers) | **MPMC** | Load balancing auto |
| Config runtime | **BoundedMPMC** | Capacité dynamique |
| Pipeline stages | **Chaîne SPSC** | Latence totale basse |
| Logging centralisé | **MPSC** | N threads → 1 writer |
| Topic routing | **MessageBus** | Wildcards, priorities |

---

*Document généré le 2025-12-18 - IPB Framework v1.5.0*
