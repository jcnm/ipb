# Analyse Sécurité - Enterprise Grade

**Projet**: IPB (Industrial Protocol Bridge)
**Date d'analyse**: 2024-12-12
**Focus**: Lacunes sécurité et solutions pour niveau entreprise

---

## 1. Résumé Exécutif

| Aspect | État Actuel | Niveau Enterprise Requis | Gap |
|--------|-------------|--------------------------|-----|
| Vulnérabilités connues | 6/10 | 9/10 | **Critique** |
| Authentification/Autorisation | 2/10 | 9/10 | **Critique** |
| Chiffrement | 3/10 | 9/10 | **Critique** |
| Audit & Logging | 3/10 | 9/10 | **Critique** |
| Input Validation | 7/10 | 9/10 | Modéré |
| Memory Safety | 8/10 | 9/10 | Faible |

**Score Sécurité Global: 6.5/10** - Non conforme aux standards enterprise

---

## 2. Vulnérabilités Critiques Identifiées

### 2.1 CVE-POTENTIAL: ReDoS (Regular Expression Denial of Service)

**Sévérité**: CRITIQUE (CVSS 8.6)
**Localisation**: `core/router/src/router.cpp:104-106`

**Code Vulnérable:**
```cpp
// Compilation regex à CHAQUE message - catastrophique
try {
    std::regex pattern(address_pattern);  // O(2^n) possible!
    auto addr = data_point.address();
    return std::regex_match(addr.begin(), addr.end(), pattern);
} catch (...) {
    return false;  // Échec silencieux
}
```

**Vecteur d'attaque:**
```
Pattern malicieux: (a+)+b
Input: "aaaaaaaaaaaaaaaaaaaaaaaaaaaa!"
Résultat: CPU 100% pendant plusieurs secondes/minutes
```

**Impact:**
- Denial of Service complet du router
- Violation des garanties temps-réel
- Exploitation facile si patterns contrôlés par utilisateur

**Solutions (par ordre de préférence):**

#### Solution A: RE2 (Garantie temps linéaire)

```cpp
// Utiliser Google RE2 au lieu de std::regex
#include <re2/re2.h>

class SafePatternMatcher {
public:
    static Result<SafePatternMatcher> create(std::string_view pattern) {
        RE2::Options options;
        options.set_max_mem(1 << 20);  // 1MB max
        options.set_log_errors(false);

        auto re = std::make_unique<RE2>(pattern, options);
        if (!re->ok()) {
            return err<SafePatternMatcher>(
                ErrorCode::INVALID_PATTERN,
                "Invalid regex: {}", re->error());
        }
        return ok(SafePatternMatcher{std::move(re)});
    }

    bool matches(std::string_view input) const {
        return RE2::FullMatch(input, *regex_);
    }

private:
    explicit SafePatternMatcher(std::unique_ptr<RE2> re)
        : regex_(std::move(re)) {}
    std::unique_ptr<RE2> regex_;
};
```

#### Solution B: Pattern Pre-compilation + Cache

```cpp
class CompiledPatternCache {
public:
    static CompiledPatternCache& instance();

    Result<const std::regex*> get_or_compile(
        const std::string& pattern,
        std::chrono::milliseconds timeout = 100ms) {

        // Check cache first
        {
            std::shared_lock lock(mutex_);
            if (auto it = cache_.find(pattern); it != cache_.end()) {
                return ok(&it->second);
            }
        }

        // Compile with timeout protection
        std::unique_lock lock(mutex_);

        // Double-check after acquiring write lock
        if (auto it = cache_.find(pattern); it != cache_.end()) {
            return ok(&it->second);
        }

        // Compile in separate thread with timeout
        auto future = std::async(std::launch::async, [&pattern]() {
            return std::regex(pattern, std::regex::optimize);
        });

        if (future.wait_for(timeout) == std::future_status::timeout) {
            return err<const std::regex*>(
                ErrorCode::PATTERN_COMPILE_TIMEOUT,
                "Pattern compilation exceeded {}ms", timeout.count());
        }

        auto [it, _] = cache_.emplace(pattern, future.get());
        return ok(&it->second);
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, std::regex> cache_;
};
```

#### Solution C: Validation de Pattern (Defense in Depth)

```cpp
class PatternValidator {
public:
    struct ValidationResult {
        bool is_safe;
        std::string reason;
        size_t estimated_complexity;
    };

    static ValidationResult validate(std::string_view pattern) {
        // Reject known dangerous patterns
        static const std::vector<std::regex> dangerous_patterns = {
            std::regex(R"(\([^)]*[+*]\)[+*])"),  // (a+)+
            std::regex(R"(\([^)]*\|[^)]*\)[+*])"), // (a|b)+
            std::regex(R"(\.{2,}\*)"),  // ...*
        };

        for (const auto& dp : dangerous_patterns) {
            if (std::regex_search(pattern.begin(), pattern.end(), dp)) {
                return {false, "Contains potentially catastrophic backtracking", 0};
            }
        }

        // Estimate complexity
        size_t stars = std::count(pattern.begin(), pattern.end(), '*');
        size_t plus = std::count(pattern.begin(), pattern.end(), '+');
        size_t groups = std::count(pattern.begin(), pattern.end(), '(');

        size_t complexity = stars + plus + (groups * 2);
        if (complexity > 10) {
            return {false, "Pattern too complex", complexity};
        }

        return {true, "OK", complexity};
    }
};
```

**Effort estimé:** 1-2 semaines
**Priorité:** P0 (IMMÉDIAT)

---

### 2.2 CRITIQUE: Absence d'Authentification/Autorisation

**Sévérité**: CRITIQUE (CVSS 9.1)
**Constat**: Aucun mécanisme d'authentification ou d'autorisation

**Impact:**
- Tout client peut accéder à toutes les données
- Pas de contrôle d'accès aux routes
- Pas de multi-tenancy sécurisé
- Non-conformité réglementaire (GDPR, SOC2, etc.)

**Solution Recommandée:**

#### A. Framework d'Authentification

```cpp
// core/security/include/ipb/security/auth.hpp
namespace ipb::security {

// Types de credentials supportés
enum class AuthMethod {
    NONE,           // Legacy/test only
    API_KEY,        // Simple API key
    MTLS,           // Mutual TLS
    JWT,            // JSON Web Token
    OAUTH2,         // OAuth 2.0
};

struct Identity {
    std::string principal;          // User/service identifier
    std::string tenant_id;          // Multi-tenant isolation
    std::vector<std::string> roles; // RBAC roles
    std::unordered_map<std::string, std::string> claims;
    std::chrono::system_clock::time_point expires_at;

    bool has_role(std::string_view role) const;
    bool has_claim(std::string_view key, std::string_view value) const;
};

class IAuthenticator {
public:
    virtual ~IAuthenticator() = default;
    virtual Result<Identity> authenticate(const AuthContext& ctx) = 0;
    virtual AuthMethod method() const = 0;
};

class IAuthorizer {
public:
    virtual ~IAuthorizer() = default;

    // Vérifie si identity peut effectuer action sur resource
    virtual Result<bool> authorize(
        const Identity& identity,
        std::string_view action,
        std::string_view resource) = 0;
};

// Implémentations
class JWTAuthenticator : public IAuthenticator {
public:
    explicit JWTAuthenticator(JWTConfig config);
    Result<Identity> authenticate(const AuthContext& ctx) override;

private:
    Result<Identity> verify_token(std::string_view token);
    JWTConfig config_;
    std::unique_ptr<jwt::verifier<jwt::default_clock>> verifier_;
};

class RBACAuthorizer : public IAuthorizer {
public:
    void add_policy(const Policy& policy);
    Result<bool> authorize(
        const Identity& identity,
        std::string_view action,
        std::string_view resource) override;

private:
    std::vector<Policy> policies_;
};

} // namespace ipb::security
```

#### B. Intégration Router avec Autorisation

```cpp
class SecureRouter : public IRouter {
public:
    SecureRouter(std::unique_ptr<IRouter> inner,
                 std::shared_ptr<IAuthorizer> authorizer)
        : inner_(std::move(inner))
        , authorizer_(std::move(authorizer)) {}

    Result<void> route(DataPoint dp, const Identity& identity) {
        // Check authorization before routing
        auto auth_result = authorizer_->authorize(
            identity,
            "route",
            dp.address());

        if (!auth_result) {
            audit_log_.log(AuditEvent::ACCESS_DENIED, identity, dp.address());
            return err(ErrorCode::UNAUTHORIZED,
                      "Access denied for {} to {}",
                      identity.principal, dp.address());
        }

        if (!*auth_result) {
            audit_log_.log(AuditEvent::ACCESS_DENIED, identity, dp.address());
            return err(ErrorCode::FORBIDDEN,
                      "Insufficient permissions for {} on {}",
                      identity.principal, dp.address());
        }

        audit_log_.log(AuditEvent::ACCESS_GRANTED, identity, dp.address());
        return inner_->route(std::move(dp));
    }

private:
    std::unique_ptr<IRouter> inner_;
    std::shared_ptr<IAuthorizer> authorizer_;
    AuditLog audit_log_;
};
```

**Effort estimé:** 3-4 semaines
**Priorité:** P0

---

### 2.3 CRITIQUE: Absence de Chiffrement

**Sévérité**: HAUTE (CVSS 7.5)
**Constat**:
- Pas de TLS pour communications
- Pas de chiffrement des données au repos
- Pas de gestion des secrets

**Solution Recommandée:**

#### A. TLS Configuration

```cpp
// core/security/include/ipb/security/tls.hpp
namespace ipb::security {

struct TLSConfig {
    std::string cert_file;
    std::string key_file;
    std::string ca_file;
    bool verify_peer = true;
    bool require_client_cert = false;  // Pour mTLS
    std::vector<std::string> allowed_ciphers = {
        "TLS_AES_256_GCM_SHA384",
        "TLS_CHACHA20_POLY1305_SHA256",
        "TLS_AES_128_GCM_SHA256"
    };
    int min_version = TLS1_3_VERSION;
};

class TLSContext {
public:
    static Result<TLSContext> create(const TLSConfig& config);

    // Pour serveur
    Result<std::unique_ptr<TLSSocket>> accept(int fd);

    // Pour client
    Result<std::unique_ptr<TLSSocket>> connect(
        const std::string& host, int port);

private:
    SSL_CTX* ctx_;
};

} // namespace ipb::security
```

#### B. Secret Management (Vault Integration)

```cpp
// core/security/include/ipb/security/secrets.hpp
namespace ipb::security {

class ISecretProvider {
public:
    virtual ~ISecretProvider() = default;
    virtual Result<std::string> get_secret(std::string_view path) = 0;
    virtual Result<void> rotate_secret(std::string_view path) = 0;
};

class VaultSecretProvider : public ISecretProvider {
public:
    struct Config {
        std::string vault_addr;
        std::string auth_method;  // "kubernetes", "approle", "token"
        std::string role;
        std::chrono::seconds lease_duration{3600};
    };

    static Result<VaultSecretProvider> create(const Config& config);

    Result<std::string> get_secret(std::string_view path) override;
    Result<void> rotate_secret(std::string_view path) override;

private:
    // Auto-renewal of token
    void start_renewal_loop();

    Config config_;
    std::string token_;
    std::jthread renewal_thread_;
};

// Usage
auto provider = VaultSecretProvider::create({
    .vault_addr = "https://vault.internal:8200",
    .auth_method = "kubernetes",
    .role = "ipb-router"
}).value();

auto db_password = provider.get_secret("secret/data/ipb/database");
```

**Effort estimé:** 2-3 semaines
**Priorité:** P0

---

### 2.4 CRITIQUE: Audit Logging Insuffisant

**Sévérité**: HAUTE (CVSS 6.5)
**Constat**:
- Pas d'audit trail
- Logging non structuré
- Pas de tamper-proof logging
- Non-conformité réglementaire

**Solution Recommandée:**

```cpp
// core/security/include/ipb/security/audit.hpp
namespace ipb::security {

enum class AuditEventType {
    // Authentication
    AUTH_SUCCESS,
    AUTH_FAILURE,
    AUTH_LOGOUT,
    TOKEN_REFRESH,

    // Authorization
    ACCESS_GRANTED,
    ACCESS_DENIED,

    // Data operations
    DATA_READ,
    DATA_WRITE,
    DATA_DELETE,

    // Configuration
    CONFIG_CHANGE,
    RULE_CREATED,
    RULE_MODIFIED,
    RULE_DELETED,

    // System
    SERVICE_START,
    SERVICE_STOP,
    ERROR_CRITICAL,
};

struct AuditEvent {
    std::string event_id;           // UUID
    AuditEventType type;
    std::chrono::system_clock::time_point timestamp;
    std::string principal;          // Who
    std::string action;             // What
    std::string resource;           // On what
    std::string source_ip;          // From where
    std::string result;             // Success/Failure
    std::unordered_map<std::string, std::string> metadata;

    // Tamper detection
    std::string previous_hash;
    std::string hash;
};

class AuditLogger {
public:
    struct Config {
        std::vector<std::unique_ptr<IAuditSink>> sinks;
        bool async = true;
        size_t buffer_size = 10000;
        std::chrono::milliseconds flush_interval{1000};
    };

    explicit AuditLogger(Config config);

    void log(AuditEventType type,
             const Identity& identity,
             std::string_view action,
             std::string_view resource,
             std::string_view result = "success",
             const std::unordered_map<std::string, std::string>& meta = {});

    // Verification
    Result<bool> verify_chain(
        std::chrono::system_clock::time_point from,
        std::chrono::system_clock::time_point to);

private:
    void compute_hash(AuditEvent& event);
    void flush_buffer();

    Config config_;
    std::string last_hash_;
    mutable std::mutex mutex_;
    std::vector<AuditEvent> buffer_;
    std::jthread flush_thread_;
};

// Audit sinks
class FileAuditSink : public IAuditSink { /*...*/ };
class SyslogAuditSink : public IAuditSink { /*...*/ };
class SplunkAuditSink : public IAuditSink { /*...*/ };
class ElasticsearchAuditSink : public IAuditSink { /*...*/ };

} // namespace ipb::security
```

**Effort estimé:** 1-2 semaines
**Priorité:** P1

---

### 2.5 HAUTE: Opérateurs ValueCondition Incomplets

**Sévérité**: HAUTE (CVSS 6.0)
**Localisation**: `core/router/src/router.cpp:16-28`

**Code Vulnérable:**
```cpp
bool ValueCondition::evaluate(const Value& value) const {
    switch (op) {
        case ValueOperator::EQUAL:
            return value == reference_value;
        // MANQUANT: LESS_THAN, GREATER_THAN, etc.
        default:
            return false;  // Échec silencieux!
    }
}
```

**Impact:**
- Règles de routage basées sur valeurs ignorées silencieusement
- Bypass potentiel de règles de sécurité
- Comportement imprévisible

**Solution:** Voir section Recommendations du code review original.

**Effort estimé:** 2 jours
**Priorité:** P0

---

### 2.6 MODÉRÉ: Rate Limiting Absent

**Sévérité**: MOYENNE (CVSS 5.3)
**Constat**: Message bus sans limitation de débit

**Impact:**
- Épuisement ressources
- DoS par flooding
- Instabilité système

**Solution Recommandée:**

```cpp
// core/components/include/ipb/components/rate_limiter.hpp
namespace ipb::components {

class TokenBucketRateLimiter {
public:
    struct Config {
        size_t bucket_size;           // Burst capacity
        size_t refill_rate;           // Tokens per second
        std::chrono::milliseconds refill_interval{100};
    };

    explicit TokenBucketRateLimiter(Config config);

    // Returns true if allowed, false if rate limited
    bool try_acquire(size_t tokens = 1);

    // Blocking version
    void acquire(size_t tokens = 1);

    // Stats
    size_t available_tokens() const;
    size_t rejected_count() const;

private:
    void refill();

    Config config_;
    std::atomic<size_t> tokens_;
    std::atomic<size_t> rejected_;
    std::chrono::steady_clock::time_point last_refill_;
    mutable std::mutex refill_mutex_;
};

// Per-client rate limiting
class ClientRateLimiter {
public:
    struct Policy {
        size_t requests_per_second;
        size_t burst_size;
        std::chrono::seconds window{1};
    };

    void set_default_policy(const Policy& policy);
    void set_client_policy(const std::string& client_id, const Policy& policy);

    bool is_allowed(const std::string& client_id);
    RateLimitInfo get_info(const std::string& client_id) const;

private:
    Policy default_policy_;
    std::unordered_map<std::string, Policy> client_policies_;
    std::unordered_map<std::string, TokenBucketRateLimiter> limiters_;
    mutable std::shared_mutex mutex_;
};

} // namespace ipb::components
```

**Effort estimé:** 1 semaine
**Priorité:** P1

---

## 3. Matrice de Conformité Sécurité

| Standard | Exigence | État | Gap |
|----------|----------|------|-----|
| **OWASP Top 10** | | | |
| A01 Broken Access Control | Auth/Authz | Non | Critique |
| A02 Cryptographic Failures | TLS/Encryption | Non | Critique |
| A03 Injection | Input validation | Partiel | Modéré |
| A05 Security Misconfiguration | Hardening | Non | Haute |
| A09 Logging & Monitoring | Audit | Non | Critique |
| **SOC 2** | | | |
| CC6.1 Logical Access | RBAC | Non | Critique |
| CC6.6 System Operations | Audit trail | Non | Critique |
| CC6.7 Change Management | Config audit | Non | Haute |
| **IEC 62443** (Industrial) | | | |
| SR 1.1 Human User ID | Authentication | Non | Critique |
| SR 1.2 Software Process ID | Service auth | Non | Critique |
| SR 3.1 Communication Integrity | TLS | Non | Critique |
| SR 7.1 DoS Protection | Rate limiting | Non | Haute |

---

## 4. Plan de Remédiation Sécurité

### Phase 1 - Critique (Semaines 1-4)
- [ ] **Semaine 1**: Corriger ReDoS (RE2 ou cache)
- [ ] **Semaine 1**: Compléter ValueCondition operators
- [ ] **Semaine 2-3**: Implémenter framework authentication
- [ ] **Semaine 3-4**: Ajouter TLS support

### Phase 2 - Haute (Semaines 5-8)
- [ ] **Semaine 5**: Implémenter authorization (RBAC)
- [ ] **Semaine 6**: Ajouter audit logging
- [ ] **Semaine 7**: Implémenter rate limiting
- [ ] **Semaine 8**: Intégrer secret management

### Phase 3 - Hardening (Semaines 9-12)
- [ ] **Semaine 9**: Security headers & hardening
- [ ] **Semaine 10**: Penetration testing
- [ ] **Semaine 11**: Security documentation
- [ ] **Semaine 12**: Compliance audit preparation

---

## 5. Checklist Sécurité Enterprise

### Pré-Production
- [ ] Toutes vulnérabilités critiques corrigées
- [ ] Authentification obligatoire activée
- [ ] TLS 1.3 enforced
- [ ] Audit logging fonctionnel
- [ ] Rate limiting configuré
- [ ] Secrets externalisés (Vault)
- [ ] SAST intégré CI/CD
- [ ] DAST/pentest réalisé

### Production
- [ ] Monitoring sécurité actif
- [ ] Alerting configuré
- [ ] Incident response plan
- [ ] Backup/restore testé
- [ ] DR plan documenté

---

## 6. Conclusion

L'état sécurité actuel d'IPB est **insuffisant pour un déploiement enterprise**:

1. **ReDoS** - Vulnérabilité exploitable immédiatement
2. **Pas d'authentification** - Accès ouvert à toutes les données
3. **Pas de chiffrement** - Données en clair
4. **Pas d'audit** - Non-conformité réglementaire

Un investissement de **8-12 semaines** est nécessaire pour atteindre un niveau de sécurité enterprise acceptable.

**Recommandation**: Ne PAS déployer en production avant correction des vulnérabilités P0.
