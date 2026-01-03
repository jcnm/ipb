# Analyse Architecture - Niveau Enterprise

**Date**: 2026-01-03
**Scope**: Architecture IPB pour déploiement enterprise-grade
**Criticité**: HAUTE

---

## 1. Lacunes Identifiées

### 1.1 Monolithisme du Router (CRITIQUE)

**Problème**: Le Router fait 744 lignes (header) + 1121 lignes (cpp) = 1865 lignes total.

**Impact Enterprise**:
- Difficulté de maintenance par équipes multiples
- Tests unitaires complexes et fragiles
- Couplage fort entre responsabilités
- Impossible de déployer des mises à jour partielles

**Violation du principe SRP**:
```
Router actuel gère:
├── Routing logic
├── Rule management
├── Sink orchestration
├── Statistics collection
├── Health monitoring
├── Failover handling
└── Configuration management
```

### 1.2 Absence de Service Discovery

**Problème**: Pas de mécanisme de découverte de services dynamique.

**Impact Enterprise**:
- Impossible de scale horizontalement
- Configuration statique = downtime lors des changements
- Pas de load balancing natif
- Intégration Kubernetes/Consul/etcd impossible

### 1.3 Pas de Multi-Tenancy

**Problème**: Architecture single-tenant implicite.

**Impact Enterprise**:
- Isolation des données client impossible
- Pas de quotas par tenant
- Audit trail global non segmenté
- Compliance GDPR/SOC2 compromise

### 1.4 Absence de Message Persistence

**Problème**: Message bus en mémoire uniquement.

**Impact Enterprise**:
- Perte de messages en cas de crash
- Pas de replay capability
- Audit impossible
- Recovery après incident = perte de données

### 1.5 Pas de Distributed Tracing

**Problème**: Pas de correlation ID ni propagation de contexte.

**Impact Enterprise**:
- Debug en production impossible
- Pas d'intégration Jaeger/Zipkin/OpenTelemetry
- SLA monitoring incomplet
- Root cause analysis manuelle

---

## 2. Solutions Enterprise-Grade

### 2.1 Décomposition en Microservices

```
Architecture Cible:
┌─────────────────────────────────────────────────────────────────┐
│                      API Gateway / Load Balancer                 │
├─────────────────────────────────────────────────────────────────┤
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Routing      │  │ Rule         │  │ Sink                 │  │
│  │ Service      │  │ Service      │  │ Orchestration        │  │
│  │              │  │              │  │ Service              │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────────────┐  │
│  │ Metrics      │  │ Config       │  │ Health               │  │
│  │ Service      │  │ Service      │  │ Service              │  │
│  └──────────────┘  └──────────────┘  └──────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                    Message Broker (Kafka/NATS)                   │
├─────────────────────────────────────────────────────────────────┤
│                    Service Mesh (Istio/Linkerd)                  │
└─────────────────────────────────────────────────────────────────┘
```

**Implémentation Proposée**:

```cpp
// Nouveau: RoutingService séparé
class IRoutingService {
public:
    virtual ~IRoutingService() = default;
    virtual Result<RouteDecision> route(const DataPoint& dp) = 0;
    virtual void register_rule(std::unique_ptr<IRule> rule) = 0;
};

// Nouveau: SinkOrchestrator séparé
class ISinkOrchestrator {
public:
    virtual ~ISinkOrchestrator() = default;
    virtual Result<void> deliver(const RouteDecision& decision, const DataPoint& dp) = 0;
    virtual void register_sink(std::unique_ptr<IIPBSink> sink) = 0;
};

// Nouveau: MetricsCollector séparé
class IMetricsCollector {
public:
    virtual ~IMetricsCollector() = default;
    virtual void record_routing(const RoutingMetrics& m) = 0;
    virtual void record_delivery(const DeliveryMetrics& m) = 0;
    virtual MetricsSnapshot snapshot() const = 0;
};
```

### 2.2 Service Discovery Integration

```cpp
// Interface pour Service Discovery
class IServiceDiscovery {
public:
    virtual ~IServiceDiscovery() = default;

    // Enregistrement du service
    virtual Result<void> register_service(const ServiceDescriptor& desc) = 0;
    virtual Result<void> deregister_service(const std::string& service_id) = 0;

    // Découverte
    virtual Result<std::vector<ServiceEndpoint>> discover(
        const std::string& service_name,
        const DiscoveryOptions& opts = {}) = 0;

    // Watch pour changements dynamiques
    virtual Result<WatchHandle> watch(
        const std::string& service_name,
        std::function<void(const std::vector<ServiceEndpoint>&)> callback) = 0;
};

// Implémentations
class ConsulServiceDiscovery : public IServiceDiscovery { /* ... */ };
class EtcdServiceDiscovery : public IServiceDiscovery { /* ... */ };
class KubernetesServiceDiscovery : public IServiceDiscovery { /* ... */ };

// Configuration
struct ServiceDescriptor {
    std::string id;
    std::string name;
    std::string address;
    uint16_t port;
    std::vector<std::string> tags;
    std::chrono::seconds health_check_interval{10};
    std::string health_check_endpoint;
};
```

### 2.3 Multi-Tenancy Support

```cpp
// Contexte tenant pour chaque requête
struct TenantContext {
    std::string tenant_id;
    std::string organization_id;
    QuotaLimits quotas;
    SecurityContext security;
    std::unordered_map<std::string, std::string> metadata;
};

// Middleware d'isolation
class TenantIsolationMiddleware {
public:
    Result<void> validate_access(
        const TenantContext& ctx,
        const DataPoint& dp) const;

    Result<void> enforce_quotas(
        const TenantContext& ctx,
        const QuotaMetrics& current) const;

    DataPoint tag_with_tenant(
        const TenantContext& ctx,
        DataPoint dp) const;
};

// Router tenant-aware
class TenantAwareRouter : public IRoutingService {
private:
    std::unordered_map<std::string, std::unique_ptr<IRoutingService>> tenant_routers_;
    TenantIsolationMiddleware isolation_;

public:
    Result<RouteDecision> route(
        const TenantContext& ctx,
        const DataPoint& dp) override;
};
```

### 2.4 Persistent Message Queue

```cpp
// Interface pour persistence des messages
class IMessageStore {
public:
    virtual ~IMessageStore() = default;

    // Stockage
    virtual Result<MessageId> store(const DataPoint& dp, const MessageMetadata& meta) = 0;
    virtual Result<void> acknowledge(const MessageId& id) = 0;
    virtual Result<void> reject(const MessageId& id, const RejectReason& reason) = 0;

    // Replay
    virtual Result<std::vector<StoredMessage>> replay(
        const ReplayOptions& opts) = 0;

    // Retention
    virtual Result<void> set_retention_policy(const RetentionPolicy& policy) = 0;
};

// Dead Letter Queue pour messages échoués
class IDeadLetterQueue {
public:
    virtual Result<void> enqueue(
        const DataPoint& dp,
        const FailureContext& ctx) = 0;

    virtual Result<std::vector<DeadLetter>> peek(size_t count) = 0;
    virtual Result<void> reprocess(const MessageId& id) = 0;
    virtual Result<void> discard(const MessageId& id) = 0;
};

// Implémentations
class KafkaMessageStore : public IMessageStore { /* ... */ };
class RocksDBMessageStore : public IMessageStore { /* ... */ };
class PostgresMessageStore : public IMessageStore { /* ... */ };
```

### 2.5 Distributed Tracing

```cpp
// Intégration OpenTelemetry
class TracingContext {
public:
    std::string trace_id;
    std::string span_id;
    std::string parent_span_id;
    std::unordered_map<std::string, std::string> baggage;

    // Propagation W3C Trace Context
    static Result<TracingContext> from_headers(
        const std::unordered_map<std::string, std::string>& headers);

    std::unordered_map<std::string, std::string> to_headers() const;
};

// Span pour instrumentation
class Span {
public:
    void set_attribute(std::string_view key, std::string_view value);
    void set_status(SpanStatus status, std::string_view message = {});
    void add_event(std::string_view name, const EventAttributes& attrs = {});
    void record_exception(const std::exception& ex);

    // RAII - finit le span automatiquement
    ~Span();
};

// Tracer factory
class ITracer {
public:
    virtual Span start_span(
        std::string_view name,
        const TracingContext& parent = {}) = 0;

    virtual TracingContext current_context() const = 0;
};

// Router instrumenté
class InstrumentedRouter : public IRoutingService {
private:
    std::unique_ptr<IRoutingService> inner_;
    std::shared_ptr<ITracer> tracer_;

public:
    Result<RouteDecision> route(const DataPoint& dp) override {
        auto span = tracer_->start_span("router.route");
        span.set_attribute("datapoint.address", dp.address());
        span.set_attribute("datapoint.type", to_string(dp.type()));

        auto result = inner_->route(dp);

        if (!result) {
            span.set_status(SpanStatus::ERROR, result.error().message());
        }

        return result;
    }
};
```

---

## 3. Roadmap d'Implémentation

### Phase 1: Fondations (4-6 semaines)
- [ ] Décomposer Router en 3 services internes
- [ ] Ajouter interface IServiceDiscovery
- [ ] Implémenter TracingContext de base
- [ ] Ajouter TenantContext dans DataPoint

### Phase 2: Persistence (4-6 semaines)
- [ ] Implémenter IMessageStore avec RocksDB
- [ ] Ajouter Dead Letter Queue
- [ ] Implémenter replay capability
- [ ] Tests de durabilité

### Phase 3: Scaling (6-8 semaines)
- [ ] Intégration Consul/etcd
- [ ] Load balancer interne
- [ ] Health checks distribués
- [ ] Horizontal pod autoscaling

### Phase 4: Observabilité (4 semaines)
- [ ] Intégration OpenTelemetry complète
- [ ] Dashboards Grafana
- [ ] Alerting PagerDuty/OpsGenie
- [ ] SLA monitoring

---

## 4. Métriques de Succès

| Métrique | Actuel | Cible Enterprise |
|----------|--------|------------------|
| Disponibilité | 99% | 99.99% |
| MTTR | Heures | < 5 minutes |
| Scalabilité | Verticale | Horizontale illimitée |
| Tenants supportés | 1 | 1000+ |
| Message durability | 0% | 100% |
| Trace coverage | 0% | 100% |

---

*Document généré pour IPB Enterprise Architecture Review*
