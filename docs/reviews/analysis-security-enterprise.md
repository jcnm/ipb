# Analyse Security - Niveau Enterprise

**Date**: 2026-01-03
**Scope**: Sécurité IPB pour déploiement enterprise-grade
**Criticité**: CRITIQUE

---

## 1. Vulnérabilités Identifiées

### 1.1 ReDoS - Regular Expression Denial of Service (CRITIQUE)

**Localisation**: `core/router/src/router.cpp:104-106`

```cpp
// CODE VULNÉRABLE
try {
    std::regex pattern(address_pattern);  // Compilation à CHAQUE appel!
    auto addr = data_point.address();
    return std::regex_match(addr.begin(), addr.end(), pattern);
} catch (...) {
    return false;
}
```

**Vecteur d'Attaque**:
```
Pattern malveillant: (a+)+b
Input malveillant:   aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa!

Résultat: Backtracking exponentiel O(2^n)
Impact: CPU 100% pendant plusieurs minutes
```

**Exploitation Enterprise**:
- Un attaquant injecte un pattern malveillant via configuration
- Ou crafts un address malveillant si pattern utilise capture groups
- Service devient indisponible = SLA violé
- Pas de timeout = impact prolongé

**CVSS Score Estimé**: 7.5 (High)

### 1.2 Value Operators Incomplets (HAUTE)

**Localisation**: `core/router/src/router.cpp:16-28`

```cpp
bool ValueCondition::evaluate(const Value& value) const {
    switch (op) {
        case ValueOperator::EQUAL:
            return value == reference_value;
        // MANQUANTS: LESS_THAN, GREATER_THAN, CONTAINS, etc.
        default:
            return false;  // Échec silencieux!
    }
}
```

**Impact Sécurité**:
- Règles de filtrage bypassées silencieusement
- Données sensibles routées vers mauvaises destinations
- Alertes sécurité basées sur valeurs non déclenchées
- Audit trail compromis

### 1.3 Absence de Rate Limiting

**Problème**: Message bus sans protection contre flood.

**Impact**:
- Resource exhaustion (mémoire, CPU)
- Denial of Service interne
- Amplification d'attaques
- Pas de protection fair-use entre tenants

### 1.4 Absence d'Authentification/Autorisation

**Problème**: Pas de mécanisme de contrôle d'accès natif.

**Lacunes**:
- Pas d'authentification des sources de messages
- Pas d'autorisation pour opérations de routing
- Pas de signature des messages
- Pas d'audit des accès

### 1.5 Secrets en Configuration

**Risque**: Potentiel de secrets en clair dans configs.

**Vérification Nécessaire**:
- Credentials de connexion aux sinks
- API keys
- Certificates
- Tokens

### 1.6 Absence de Chiffrement des Données

**Problème**: Pas de chiffrement at-rest ni in-transit natif.

**Impact GDPR/SOC2/HIPAA**:
- Non-conformité réglementaire
- Données exposées en cas de breach
- Pas de perfect forward secrecy

---

## 2. Solutions Enterprise-Grade

### 2.1 Fix ReDoS - Solution Immédiate

```cpp
// Solution 1: Cache de patterns compilés
class PatternCache {
public:
    static constexpr size_t MAX_CACHE_SIZE = 1000;
    static constexpr auto PATTERN_TIMEOUT = std::chrono::milliseconds{100};

    Result<bool> match(std::string_view pattern, std::string_view input) {
        auto compiled = get_or_compile(pattern);
        if (!compiled) {
            return compiled.error();
        }

        // Exécution avec timeout
        auto future = std::async(std::launch::async, [&]() {
            return std::regex_match(input.begin(), input.end(), *compiled);
        });

        if (future.wait_for(PATTERN_TIMEOUT) == std::future_status::timeout) {
            return err<bool>(ErrorCode::TIMEOUT, "Pattern matching timeout");
        }

        return ok(future.get());
    }

private:
    Result<const std::regex*> get_or_compile(std::string_view pattern) {
        std::shared_lock lock(mutex_);

        auto it = cache_.find(std::string(pattern));
        if (it != cache_.end()) {
            return ok(&it->second);
        }

        lock.unlock();
        std::unique_lock write_lock(mutex_);

        // Double-check
        it = cache_.find(std::string(pattern));
        if (it != cache_.end()) {
            return ok(&it->second);
        }

        // Validation anti-ReDoS
        if (!validate_pattern_safety(pattern)) {
            return err<const std::regex*>(
                ErrorCode::SECURITY_ERROR,
                "Pattern rejected: potential ReDoS");
        }

        try {
            auto [inserted, _] = cache_.emplace(
                std::string(pattern),
                std::regex(pattern.begin(), pattern.end())
            );
            return ok(&inserted->second);
        } catch (const std::regex_error& e) {
            return err<const std::regex*>(
                ErrorCode::INVALID_PATTERN,
                e.what());
        }
    }

    bool validate_pattern_safety(std::string_view pattern) {
        // Détection patterns dangereux
        // (a+)+, (a*)+, (a+)*, etc.
        static const std::regex dangerous_patterns[] = {
            std::regex(R"(\([^)]*[+*]\)[+*])"),  // Quantificateurs imbriqués
            std::regex(R"(\([^)]*\|[^)]*\)[+*])"), // Alternation avec quantificateur
        };

        for (const auto& dp : dangerous_patterns) {
            if (std::regex_search(pattern.begin(), pattern.end(), dp)) {
                return false;
            }
        }

        // Limite de longueur
        if (pattern.size() > 1000) {
            return false;
        }

        return true;
    }

    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::regex> cache_;
};
```

```cpp
// Solution 2: Utiliser RE2 (garantie temps linéaire)
#include <re2/re2.h>

class SafePatternMatcher {
public:
    Result<bool> match(std::string_view pattern, std::string_view input) {
        RE2::Options opts;
        opts.set_max_mem(1 << 20);  // 1MB max
        opts.set_log_errors(false);

        RE2 re(pattern, opts);
        if (!re.ok()) {
            return err<bool>(ErrorCode::INVALID_PATTERN, re.error());
        }

        return ok(RE2::FullMatch(input, re));
    }
};
```

### 2.2 Complétion Value Operators

```cpp
bool ValueCondition::evaluate(const Value& value) const {
    // Validation de type d'abord
    if (!are_types_comparable(value.type(), reference_value.type())) {
        IPB_LOG_WARN("Type mismatch in value comparison: {} vs {}",
            to_string(value.type()), to_string(reference_value.type()));
        return false;
    }

    switch (op) {
        case ValueOperator::EQUAL:
            return value == reference_value;

        case ValueOperator::NOT_EQUAL:
            return value != reference_value;

        case ValueOperator::LESS_THAN:
            return compare_values(value, reference_value) < 0;

        case ValueOperator::LESS_EQUAL:
            return compare_values(value, reference_value) <= 0;

        case ValueOperator::GREATER_THAN:
            return compare_values(value, reference_value) > 0;

        case ValueOperator::GREATER_EQUAL:
            return compare_values(value, reference_value) >= 0;

        case ValueOperator::CONTAINS:
            return evaluate_contains(value, reference_value);

        case ValueOperator::STARTS_WITH:
            return evaluate_starts_with(value, reference_value);

        case ValueOperator::ENDS_WITH:
            return evaluate_ends_with(value, reference_value);

        case ValueOperator::MATCHES:
            return evaluate_regex_match(value, reference_value);

        case ValueOperator::IN_RANGE:
            return evaluate_in_range(value);

        case ValueOperator::IN_SET:
            return evaluate_in_set(value);

        default:
            IPB_LOG_ERROR("Unknown operator: {}", static_cast<int>(op));
            // Fail-secure: retourne false plutôt que de laisser passer
            return false;
    }
}

// Helper pour comparaison numérique type-safe
int compare_values(const Value& a, const Value& b) {
    return std::visit([](auto&& lhs, auto&& rhs) -> int {
        using L = std::decay_t<decltype(lhs)>;
        using R = std::decay_t<decltype(rhs)>;

        if constexpr (std::is_arithmetic_v<L> && std::is_arithmetic_v<R>) {
            auto l = static_cast<double>(lhs);
            auto r = static_cast<double>(rhs);
            return (l < r) ? -1 : (l > r) ? 1 : 0;
        } else if constexpr (std::is_same_v<L, std::string> &&
                             std::is_same_v<R, std::string>) {
            return lhs.compare(rhs);
        } else {
            throw std::invalid_argument("Incompatible types for comparison");
        }
    }, a.variant(), b.variant());
}
```

### 2.3 Rate Limiting Enterprise

```cpp
// Token Bucket avec support multi-tenant
class RateLimiter {
public:
    struct Config {
        size_t bucket_size = 1000;           // Capacité max
        double refill_rate = 100.0;          // Tokens par seconde
        bool per_tenant = true;              // Isolation par tenant
        bool per_source = true;              // Isolation par source
    };

    Result<void> acquire(const RateLimitContext& ctx, size_t tokens = 1) {
        auto key = make_key(ctx);

        std::unique_lock lock(mutex_);
        auto& bucket = buckets_[key];

        refill(bucket);

        if (bucket.tokens < tokens) {
            record_rejection(ctx);
            return err<void>(
                ErrorCode::RATE_LIMITED,
                fmt::format("Rate limit exceeded. Retry after {}ms",
                    calculate_wait_time(bucket, tokens).count()));
        }

        bucket.tokens -= tokens;
        return ok();
    }

    // Adaptive rate limiting basé sur la charge système
    void adjust_limits(double system_load) {
        if (system_load > 0.9) {
            // Réduction agressive
            current_multiplier_ = 0.5;
        } else if (system_load > 0.7) {
            // Réduction modérée
            current_multiplier_ = 0.75;
        } else {
            current_multiplier_ = 1.0;
        }
    }

private:
    struct Bucket {
        double tokens;
        std::chrono::steady_clock::time_point last_refill;
    };

    void refill(Bucket& bucket) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration<double>(
            now - bucket.last_refill).count();

        bucket.tokens = std::min(
            static_cast<double>(config_.bucket_size),
            bucket.tokens + elapsed * config_.refill_rate * current_multiplier_
        );
        bucket.last_refill = now;
    }

    Config config_;
    std::shared_mutex mutex_;
    std::unordered_map<std::string, Bucket> buckets_;
    std::atomic<double> current_multiplier_{1.0};
};

// Circuit Breaker pour protection cascade
class CircuitBreaker {
public:
    enum class State { CLOSED, OPEN, HALF_OPEN };

    struct Config {
        size_t failure_threshold = 5;
        std::chrono::seconds open_duration{30};
        size_t half_open_max_calls = 3;
    };

    template<typename F>
    Result<std::invoke_result_t<F>> execute(F&& func) {
        if (!allow_request()) {
            return err<std::invoke_result_t<F>>(
                ErrorCode::CIRCUIT_OPEN,
                "Circuit breaker is open");
        }

        try {
            auto result = std::forward<F>(func)();
            record_success();
            return ok(std::move(result));
        } catch (...) {
            record_failure();
            throw;
        }
    }

private:
    bool allow_request() {
        auto state = state_.load();

        if (state == State::CLOSED) {
            return true;
        }

        if (state == State::OPEN) {
            auto now = std::chrono::steady_clock::now();
            if (now >= open_until_) {
                state_.store(State::HALF_OPEN);
                half_open_calls_ = 0;
                return true;
            }
            return false;
        }

        // HALF_OPEN
        return half_open_calls_++ < config_.half_open_max_calls;
    }

    void record_success() {
        failure_count_ = 0;
        if (state_.load() == State::HALF_OPEN) {
            state_.store(State::CLOSED);
        }
    }

    void record_failure() {
        if (++failure_count_ >= config_.failure_threshold) {
            state_.store(State::OPEN);
            open_until_ = std::chrono::steady_clock::now() +
                          config_.open_duration;
        }
    }

    Config config_;
    std::atomic<State> state_{State::CLOSED};
    std::atomic<size_t> failure_count_{0};
    std::atomic<size_t> half_open_calls_{0};
    std::chrono::steady_clock::time_point open_until_;
};
```

### 2.4 Authentication & Authorization Framework

```cpp
// Interface d'authentification
class IAuthenticator {
public:
    virtual ~IAuthenticator() = default;

    virtual Result<Identity> authenticate(
        const AuthenticationContext& ctx) = 0;

    virtual Result<void> validate_token(
        const std::string& token) = 0;
};

// Identité authentifiée
struct Identity {
    std::string principal_id;
    std::string tenant_id;
    std::vector<std::string> roles;
    std::vector<std::string> permissions;
    std::chrono::system_clock::time_point expires_at;
    std::unordered_map<std::string, std::string> claims;
};

// Interface d'autorisation
class IAuthorizer {
public:
    virtual ~IAuthorizer() = default;

    virtual Result<void> authorize(
        const Identity& identity,
        const Resource& resource,
        const Action& action) = 0;
};

// Policy-based authorization
class PolicyAuthorizer : public IAuthorizer {
public:
    Result<void> authorize(
        const Identity& identity,
        const Resource& resource,
        const Action& action) override
    {
        for (const auto& policy : policies_) {
            auto result = policy->evaluate(identity, resource, action);
            if (result == PolicyResult::DENY) {
                return err<void>(ErrorCode::FORBIDDEN,
                    "Access denied by policy");
            }
            if (result == PolicyResult::ALLOW) {
                return ok();
            }
        }

        // Default deny
        return err<void>(ErrorCode::FORBIDDEN, "No policy allows access");
    }

private:
    std::vector<std::unique_ptr<IPolicy>> policies_;
};

// Message signing
class MessageSigner {
public:
    Result<SignedMessage> sign(
        const DataPoint& dp,
        const SigningKey& key)
    {
        auto payload = serialize(dp);
        auto signature = crypto::hmac_sha256(key.secret(), payload);

        return ok(SignedMessage{
            .payload = std::move(payload),
            .signature = std::move(signature),
            .key_id = key.id(),
            .algorithm = "HMAC-SHA256",
            .timestamp = std::chrono::system_clock::now()
        });
    }

    Result<DataPoint> verify_and_extract(
        const SignedMessage& msg,
        const KeyStore& keys)
    {
        auto key = keys.get(msg.key_id);
        if (!key) {
            return err<DataPoint>(ErrorCode::INVALID_SIGNATURE,
                "Unknown key ID");
        }

        // Verify timestamp (anti-replay)
        auto age = std::chrono::system_clock::now() - msg.timestamp;
        if (age > MAX_MESSAGE_AGE) {
            return err<DataPoint>(ErrorCode::EXPIRED,
                "Message too old");
        }

        auto expected = crypto::hmac_sha256(key->secret(), msg.payload);
        if (!crypto::constant_time_compare(expected, msg.signature)) {
            return err<DataPoint>(ErrorCode::INVALID_SIGNATURE,
                "Signature verification failed");
        }

        return deserialize<DataPoint>(msg.payload);
    }
};
```

### 2.5 Encryption at Rest & In Transit

```cpp
// Encryption service
class EncryptionService {
public:
    struct Config {
        std::string kms_endpoint;
        std::string master_key_id;
        EncryptionAlgorithm algorithm = EncryptionAlgorithm::AES_256_GCM;
        bool enable_key_rotation = true;
        std::chrono::hours key_rotation_interval{24 * 30}; // 30 days
    };

    // Encryption avec envelope encryption
    Result<EncryptedData> encrypt(std::span<const uint8_t> plaintext) {
        // Générer DEK (Data Encryption Key)
        auto dek = crypto::generate_random_key(32);

        // Chiffrer le plaintext avec DEK
        auto iv = crypto::generate_random_iv(12);
        auto ciphertext = crypto::aes_gcm_encrypt(plaintext, dek, iv);

        // Chiffrer le DEK avec KEK (via KMS)
        auto encrypted_dek = kms_client_->encrypt(
            config_.master_key_id, dek);

        return ok(EncryptedData{
            .ciphertext = std::move(ciphertext),
            .iv = std::move(iv),
            .encrypted_dek = std::move(encrypted_dek),
            .key_id = config_.master_key_id,
            .algorithm = config_.algorithm
        });
    }

    Result<std::vector<uint8_t>> decrypt(const EncryptedData& data) {
        // Déchiffrer le DEK via KMS
        auto dek = kms_client_->decrypt(data.key_id, data.encrypted_dek);
        if (!dek) {
            return dek.error();
        }

        // Déchiffrer le ciphertext
        return crypto::aes_gcm_decrypt(data.ciphertext, *dek, data.iv);
    }

private:
    Config config_;
    std::unique_ptr<IKMSClient> kms_client_;
};

// TLS Configuration
struct TLSConfig {
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    TLSVersion min_version = TLSVersion::TLS_1_3;
    std::vector<std::string> cipher_suites = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };
    bool verify_peer = true;
    bool require_client_cert = false;
};
```

### 2.6 Security Audit Logging

```cpp
// Audit logger pour conformité
class SecurityAuditLogger {
public:
    struct AuditEvent {
        std::string event_id;
        std::chrono::system_clock::time_point timestamp;
        std::string event_type;
        std::string principal;
        std::string resource;
        std::string action;
        std::string outcome;  // SUCCESS, FAILURE, DENIED
        std::string source_ip;
        std::unordered_map<std::string, std::string> details;
    };

    void log_authentication(
        const AuthenticationContext& ctx,
        const Result<Identity>& result)
    {
        AuditEvent event{
            .event_id = generate_uuid(),
            .timestamp = std::chrono::system_clock::now(),
            .event_type = "AUTHENTICATION",
            .principal = ctx.username,
            .action = "LOGIN",
            .outcome = result ? "SUCCESS" : "FAILURE",
            .source_ip = ctx.source_ip
        };

        if (!result) {
            event.details["error"] = result.error().message();
        }

        emit(event);
    }

    void log_authorization(
        const Identity& identity,
        const Resource& resource,
        const Action& action,
        bool allowed)
    {
        AuditEvent event{
            .event_id = generate_uuid(),
            .timestamp = std::chrono::system_clock::now(),
            .event_type = "AUTHORIZATION",
            .principal = identity.principal_id,
            .resource = resource.to_string(),
            .action = action.to_string(),
            .outcome = allowed ? "SUCCESS" : "DENIED"
        };

        emit(event);
    }

    void log_data_access(
        const Identity& identity,
        const DataPoint& dp,
        DataAccessType type)
    {
        AuditEvent event{
            .event_id = generate_uuid(),
            .timestamp = std::chrono::system_clock::now(),
            .event_type = "DATA_ACCESS",
            .principal = identity.principal_id,
            .resource = dp.address(),
            .action = to_string(type),
            .outcome = "SUCCESS"
        };

        // PII masking
        if (is_sensitive(dp)) {
            event.details["data_classification"] = "SENSITIVE";
        }

        emit(event);
    }

private:
    void emit(const AuditEvent& event) {
        // Write to immutable audit log
        // Typically: append-only storage, SIEM integration
        audit_sink_->write(serialize(event));
    }

    std::unique_ptr<IAuditSink> audit_sink_;
};
```

---

## 3. Checklist de Conformité Enterprise

### SOC 2 Type II

| Control | Status | Action Required |
|---------|--------|-----------------|
| CC6.1 - Logical Access | ❌ | Implémenter AuthN/AuthZ |
| CC6.2 - Access Removal | ❌ | Token revocation |
| CC6.3 - Role-Based Access | ❌ | RBAC framework |
| CC6.6 - Encryption | ❌ | TLS + at-rest encryption |
| CC6.7 - Transmission Security | ❌ | mTLS |
| CC7.1 - Intrusion Detection | ❌ | Rate limiting + anomaly detection |
| CC7.2 - Security Monitoring | ❌ | Audit logging |

### GDPR

| Requirement | Status | Action Required |
|-------------|--------|-----------------|
| Data Encryption | ❌ | Encryption service |
| Access Controls | ❌ | AuthZ framework |
| Audit Trail | ❌ | Audit logger |
| Data Isolation | ❌ | Multi-tenancy |
| Right to Erasure | ❌ | Data lifecycle management |

---

## 4. Priorité des Corrections

| Priorité | Vulnérabilité | Effort | Impact |
|----------|---------------|--------|--------|
| **P0** | Fix ReDoS | 2-3 jours | Critique |
| **P0** | Compléter Value operators | 1 jour | Haute |
| **P1** | Rate limiting | 1 semaine | Haute |
| **P1** | Audit logging | 1 semaine | Compliance |
| **P2** | AuthN/AuthZ | 2-3 semaines | Haute |
| **P2** | Encryption | 2 semaines | Compliance |
| **P3** | mTLS | 1 semaine | Moyenne |

---

*Document généré pour IPB Enterprise Security Review*
