#pragma once

/**
 * @file security.hpp
 * @brief IPB Security Module - Main Header
 *
 * This header provides a unified interface to the IPB security subsystem:
 *
 * - Authentication: API keys, sessions, JWT tokens
 * - Authorization: Role-based access control (RBAC)
 * - Audit: Structured event logging with tamper evidence
 * - Utilities: Hashing, validation, sanitization
 *
 * Quick Start:
 * @code
 * #include <ipb/security/security.hpp>
 *
 * using namespace ipb::security;
 *
 * // Setup authentication
 * ApiKeyAuthenticator auth;
 * auth.add_key("admin_key", {"admin"}, "Admin access");
 *
 * // Authenticate request
 * auto result = auth.authenticate("admin_key");
 * if (result.success) {
 *     // Check authorization
 *     AuthorizationService authz;
 *     authz.setup_default_roles();
 *
 *     Resource resource{"datapoint", "sensor.temp", "production"};
 *     auto decision = authz.authorize(result.identity, resource, Action::READ);
 *
 *     if (decision.is_allowed()) {
 *         // Log access
 *         AUDIT_ACCESS_GRANTED(result.identity, resource.to_string(), "read");
 *         // Perform operation...
 *     }
 * }
 * @endcode
 */

#include "authentication.hpp"
#include "authorization.hpp"
#include "audit.hpp"
#include "security_utils.hpp"

namespace ipb::security {

//=============================================================================
// Security Context
//=============================================================================

/**
 * @brief Combined security context for a request/operation
 */
struct SecurityContext {
    // Authentication
    bool authenticated{false};
    Identity identity;
    std::string session_id;
    std::string correlation_id;

    // Source info
    std::string source_ip;
    std::string user_agent;

    // Request metadata
    std::chrono::system_clock::time_point request_time;

    /**
     * @brief Create context from authentication context
     */
    static SecurityContext from_auth(const AuthContext& result) {
        SecurityContext ctx;
        ctx.authenticated = (result.result == AuthResult::SUCCESS);
        if (result.identity) {
            ctx.identity = *result.identity;
        }
        ctx.request_time = std::chrono::system_clock::now();
        ctx.correlation_id = SecureRandom::uuid();
        return ctx;
    }

    /**
     * @brief Check if context has a specific role
     */
    bool has_role(std::string_view role) const {
        return std::find(identity.roles.begin(), identity.roles.end(), role)
               != identity.roles.end();
    }

    /**
     * @brief Check if context is an admin
     */
    bool is_admin() const {
        return has_role("admin");
    }
};

//=============================================================================
// Security Facade
//=============================================================================

/**
 * @brief Unified security facade
 *
 * Provides a single entry point for all security operations.
 */
class SecurityManager {
public:
    /**
     * @brief Initialize with default configuration
     */
    SecurityManager() {
        authz_.setup_default_roles();
    }

    // Authentication

    /**
     * @brief Register API key and return the full key string
     */
    std::string register_api_key(std::string_view owner_id,
                                 std::vector<std::string> roles = {},
                                 std::string_view description = "") {
        return api_auth_.register_key(std::string(owner_id), roles,
                                       std::chrono::hours(8760), std::string(description));
    }

    /**
     * @brief Revoke API key
     */
    bool revoke_api_key(std::string_view key_id) {
        return api_auth_.revoke_key(std::string(key_id));
    }

    /**
     * @brief Authenticate with API key
     */
    SecurityContext authenticate_api_key(std::string_view key,
                                         std::string_view source_ip = "") {
        auto result = api_auth_.authenticate(key);

        SecurityContext ctx = SecurityContext::from_auth(result);
        ctx.source_ip = std::string(source_ip);

        if (result.result == AuthResult::SUCCESS && result.identity) {
            AUDIT_AUTH_SUCCESS(*result.identity, "api_key");
        } else {
            std::string key_preview = key.length() > 8
                ? std::string(key.substr(0, 8)) + "..."
                : std::string(key);
            AUDIT_AUTH_FAILURE(key_preview, result.error_message);
        }

        return ctx;
    }

    /**
     * @brief Create session
     */
    std::string create_session(const Identity& identity,
                               std::chrono::seconds duration = std::chrono::seconds(86400)) {
        return sessions_.create_session(identity.id, identity.roles,
                                         std::chrono::duration_cast<std::chrono::hours>(duration));
    }

    /**
     * @brief Validate session
     */
    SecurityContext validate_session(std::string_view session_token,
                                     std::string_view source_ip = "") {
        auto result = sessions_.validate(session_token);

        SecurityContext ctx = SecurityContext::from_auth(result);
        ctx.source_ip = std::string(source_ip);
        if (result.identity) {
            ctx.session_id = result.identity->metadata.count("token_id")
                ? result.identity->metadata.at("token_id") : "";
        }

        return ctx;
    }

    // Authorization

    /**
     * @brief Register custom role
     */
    void register_role(Role role) {
        authz_.register_role(std::move(role));
    }

    /**
     * @brief Add authorization policy
     */
    void add_policy(Policy policy) {
        authz_.add_policy(std::move(policy));
    }

    /**
     * @brief Check authorization
     */
    AuthzDecision authorize(const SecurityContext& ctx,
                            const Resource& resource,
                            Action action) {
        if (!ctx.authenticated) {
            AuthzDecision decision;
            decision.result = AuthzResult::DENIED;
            decision.reason = "Not authenticated";
            return decision;
        }

        auto decision = authz_.authorize(ctx.identity, resource, action);

        // Audit
        if (decision.is_allowed()) {
            get_audit_logger().log_access_granted(
                ctx.identity, resource.to_string(), action_string(action));
        } else {
            get_audit_logger().log_access_denied(
                ctx.identity, resource.to_string(),
                action_string(action), decision.reason);
        }

        return decision;
    }

    /**
     * @brief Quick permission check
     */
    bool can(const SecurityContext& ctx,
             std::string_view resource_type,
             std::string_view resource_id,
             Action action) {
        if (!ctx.authenticated) return false;

        Resource resource;
        resource.type = std::string(resource_type);
        resource.id = std::string(resource_id);
        resource.scope = "*";

        return authorize(ctx, resource, action).is_allowed();
    }

    // Audit

    /**
     * @brief Get audit logger
     */
    AuditLogger& audit() {
        return get_audit_logger();
    }

    /**
     * @brief Configure audit logger
     */
    void configure_audit(AuditLogger::Config config) {
        // Note: Would need to recreate logger or add reconfigure method
        (void)config;
    }

    /**
     * @brief Add audit backend
     */
    void add_audit_backend(std::shared_ptr<IAuditBackend> backend) {
        get_audit_logger().add_backend(std::move(backend));
    }

    // Utilities

    /**
     * @brief Generate new API key
     */
    static std::string generate_api_key() {
        return TokenUtils::generate_api_key();
    }

    /**
     * @brief Validate input
     */
    static bool validate_email(std::string_view email) {
        return InputValidator::is_valid_email(email);
    }

    static bool validate_identifier(std::string_view id) {
        return InputValidator::is_valid_identifier(id);
    }

    /**
     * @brief Sanitize input
     */
    static std::string sanitize_html(std::string_view input) {
        return InputSanitizer::escape_html(input);
    }

    // Statistics

    size_t active_sessions() const {
        return sessions_.session_count();
    }

    size_t registered_keys() const {
        return api_auth_.key_count();
    }

    size_t registered_roles() const {
        return authz_.role_count();
    }

    size_t registered_policies() const {
        return authz_.policy_count();
    }

private:
    ApiKeyAuthenticator api_auth_;
    SessionManager sessions_;
    AuthorizationService authz_;
};

//=============================================================================
// Request Guard
//=============================================================================

/**
 * @brief RAII guard for securing a request
 */
class RequestGuard {
public:
    RequestGuard(SecurityManager& manager,
                 const SecurityContext& ctx,
                 const Resource& resource,
                 Action action)
        : manager_(manager)
        , ctx_(ctx)
        , resource_(resource)
        , action_(action)
        , allowed_(false) {

        auto decision = manager_.authorize(ctx_, resource_, action_);
        allowed_ = decision.is_allowed();
    }

    ~RequestGuard() {
        // Could log request completion here
    }

    bool allowed() const { return allowed_; }

    explicit operator bool() const { return allowed_; }

private:
    SecurityManager& manager_;
    const SecurityContext& ctx_;
    Resource resource_;
    Action action_;
    bool allowed_;
};

//=============================================================================
// Security Middleware Helper
//=============================================================================

/**
 * @brief Extract API key from authorization header
 */
inline std::optional<std::string> extract_api_key(std::string_view auth_header) {
    // Format: "Bearer <key>" or "ApiKey <key>"
    if (auth_header.starts_with("Bearer ")) {
        return std::string(auth_header.substr(7));
    }
    if (auth_header.starts_with("ApiKey ")) {
        return std::string(auth_header.substr(7));
    }
    return std::nullopt;
}

/**
 * @brief Extract bearer token
 */
inline std::optional<std::string> extract_bearer_token(std::string_view auth_header) {
    if (auth_header.starts_with("Bearer ")) {
        return std::string(auth_header.substr(7));
    }
    return std::nullopt;
}

} // namespace ipb::security
