#pragma once

/**
 * @file authorization.hpp
 * @brief Role-Based Access Control (RBAC) authorization system
 *
 * Features:
 * - Hierarchical roles
 * - Fine-grained permissions
 * - Resource-based access control
 * - Policy evaluation engine
 * - Caching for performance
 */

#include <algorithm>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "authentication.hpp"

namespace ipb::security {

//=============================================================================
// Permission and Resource Types
//=============================================================================

/**
 * @brief Permission actions
 */
enum class Action { READ, WRITE, DELETE, EXECUTE, ADMIN, ALL };

inline std::string action_string(Action action) {
    switch (action) {
        case Action::READ:
            return "read";
        case Action::WRITE:
            return "write";
        case Action::DELETE:
            return "delete";
        case Action::EXECUTE:
            return "execute";
        case Action::ADMIN:
            return "admin";
        case Action::ALL:
            return "*";
    }
    return "unknown";
}

inline std::optional<Action> parse_action(std::string_view str) {
    if (str == "read")
        return Action::READ;
    if (str == "write")
        return Action::WRITE;
    if (str == "delete")
        return Action::DELETE;
    if (str == "execute")
        return Action::EXECUTE;
    if (str == "admin")
        return Action::ADMIN;
    if (str == "*")
        return Action::ALL;
    return std::nullopt;
}

/**
 * @brief Resource type
 */
struct Resource {
    std::string type;   // e.g., "datapoint", "route", "sink"
    std::string id;     // e.g., "sensor.temperature", "*"
    std::string scope;  // e.g., "namespace:production", "*"

    bool matches(const Resource& other) const {
        return (type == "*" || other.type == "*" || type == other.type) &&
               (id == "*" || other.id == "*" || id == other.id) &&
               (scope == "*" || other.scope == "*" || scope == other.scope);
    }

    std::string to_string() const { return type + ":" + id + "@" + scope; }

    static Resource parse(std::string_view str) {
        Resource res;
        auto at_pos    = str.find('@');
        auto colon_pos = str.find(':');

        if (colon_pos != std::string_view::npos) {
            res.type = std::string(str.substr(0, colon_pos));
            if (at_pos != std::string_view::npos) {
                res.id    = std::string(str.substr(colon_pos + 1, at_pos - colon_pos - 1));
                res.scope = std::string(str.substr(at_pos + 1));
            } else {
                res.id    = std::string(str.substr(colon_pos + 1));
                res.scope = "*";
            }
        } else {
            res.type  = std::string(str);
            res.id    = "*";
            res.scope = "*";
        }
        return res;
    }
};

/**
 * @brief Permission definition
 */
struct Permission {
    Resource resource;
    std::set<Action> actions;

    bool allows(const Resource& res, Action action) const {
        if (!resource.matches(res))
            return false;
        return actions.count(Action::ALL) > 0 || actions.count(action) > 0;
    }
};

//=============================================================================
// Role Definition
//=============================================================================

/**
 * @brief Role with permissions and inheritance
 */
struct Role {
    std::string name;
    std::string description;
    std::vector<Permission> permissions;
    std::vector<std::string> inherits;  // Parent roles

    bool has_permission(const Resource& resource, Action action,
                        const std::unordered_map<std::string, Role>& all_roles,
                        std::unordered_set<std::string>& visited) const {
        // Prevent circular inheritance
        if (visited.count(name))
            return false;
        visited.insert(name);

        // Check direct permissions
        for (const auto& perm : permissions) {
            if (perm.allows(resource, action)) {
                return true;
            }
        }

        // Check inherited roles
        for (const auto& parent_name : inherits) {
            auto it = all_roles.find(parent_name);
            if (it != all_roles.end()) {
                if (it->second.has_permission(resource, action, all_roles, visited)) {
                    return true;
                }
            }
        }

        return false;
    }
};

//=============================================================================
// Policy
//=============================================================================

/**
 * @brief Access policy effect
 */
enum class PolicyEffect { ALLOW, DENY };

/**
 * @brief Access policy
 */
struct Policy {
    std::string name;
    PolicyEffect effect{PolicyEffect::ALLOW};
    std::vector<std::string> principals;  // Roles or identity IDs
    std::vector<Resource> resources;
    std::vector<Action> actions;
    std::unordered_map<std::string, std::string> conditions;

    bool applies_to(const std::string& principal, const Resource& resource, Action action) const {
        // Check principal
        bool principal_match = false;
        for (const auto& p : principals) {
            if (p == "*" || p == principal) {
                principal_match = true;
                break;
            }
        }
        if (!principal_match)
            return false;

        // Check resource
        bool resource_match = false;
        for (const auto& r : resources) {
            if (r.matches(resource)) {
                resource_match = true;
                break;
            }
        }
        if (!resource_match)
            return false;

        // Check action
        bool action_match = false;
        for (const auto& a : actions) {
            if (a == Action::ALL || a == action) {
                action_match = true;
                break;
            }
        }

        return action_match;
    }
};

//=============================================================================
// Authorization Decision
//=============================================================================

/**
 * @brief Authorization result
 */
enum class AuthzResult { ALLOWED, DENIED, NOT_APPLICABLE };

/**
 * @brief Authorization decision with context
 */
struct AuthzDecision {
    AuthzResult result{AuthzResult::DENIED};
    std::string reason;
    std::string matched_policy;
    std::chrono::microseconds latency{0};

    bool is_allowed() const { return result == AuthzResult::ALLOWED; }
};

//=============================================================================
// Authorization Service
//=============================================================================

/**
 * @brief RBAC Authorization service
 */
class AuthorizationService {
public:
    /**
     * @brief Register role
     */
    void register_role(Role role) {
        std::unique_lock lock(mutex_);
        roles_[role.name] = std::move(role);
    }

    /**
     * @brief Remove role
     */
    bool remove_role(const std::string& name) {
        std::unique_lock lock(mutex_);
        return roles_.erase(name) > 0;
    }

    /**
     * @brief Add policy
     */
    void add_policy(Policy policy) {
        std::unique_lock lock(mutex_);
        policies_.push_back(std::move(policy));
    }

    /**
     * @brief Check authorization
     */
    AuthzDecision authorize(const Identity& identity, const Resource& resource,
                            Action action) const {
        auto start = std::chrono::high_resolution_clock::now();
        AuthzDecision decision;

        std::shared_lock lock(mutex_);

        // Check explicit deny policies first
        for (const auto& policy : policies_) {
            if (policy.effect == PolicyEffect::DENY) {
                for (const auto& role : identity.roles) {
                    if (policy.applies_to(role, resource, action)) {
                        decision.result         = AuthzResult::DENIED;
                        decision.reason         = "Denied by policy";
                        decision.matched_policy = policy.name;
                        goto done;
                    }
                }
                if (policy.applies_to(identity.id, resource, action)) {
                    decision.result         = AuthzResult::DENIED;
                    decision.reason         = "Denied by policy";
                    decision.matched_policy = policy.name;
                    goto done;
                }
            }
        }

        // Check role-based permissions
        for (const auto& role_name : identity.roles) {
            auto it = roles_.find(role_name);
            if (it != roles_.end()) {
                std::unordered_set<std::string> visited;
                if (it->second.has_permission(resource, action, roles_, visited)) {
                    decision.result = AuthzResult::ALLOWED;
                    decision.reason = "Allowed by role: " + role_name;
                    goto done;
                }
            }
        }

        // Check allow policies
        for (const auto& policy : policies_) {
            if (policy.effect == PolicyEffect::ALLOW) {
                for (const auto& role : identity.roles) {
                    if (policy.applies_to(role, resource, action)) {
                        decision.result         = AuthzResult::ALLOWED;
                        decision.reason         = "Allowed by policy";
                        decision.matched_policy = policy.name;
                        goto done;
                    }
                }
                if (policy.applies_to(identity.id, resource, action)) {
                    decision.result         = AuthzResult::ALLOWED;
                    decision.reason         = "Allowed by policy";
                    decision.matched_policy = policy.name;
                    goto done;
                }
            }
        }

        // Default deny
        decision.result = AuthzResult::DENIED;
        decision.reason = "No matching permission";

    done:
        auto end         = std::chrono::high_resolution_clock::now();
        decision.latency = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
        return decision;
    }

    /**
     * @brief Check if identity has specific role
     */
    bool has_role(const Identity& identity, std::string_view role) const {
        return std::find(identity.roles.begin(), identity.roles.end(), role) !=
               identity.roles.end();
    }

    /**
     * @brief Get all permissions for identity
     */
    std::vector<Permission> get_permissions(const Identity& identity) const {
        std::vector<Permission> result;
        std::shared_lock lock(mutex_);

        for (const auto& role_name : identity.roles) {
            auto it = roles_.find(role_name);
            if (it != roles_.end()) {
                result.insert(result.end(), it->second.permissions.begin(),
                              it->second.permissions.end());
            }
        }

        return result;
    }

    /**
     * @brief Setup default roles
     */
    void setup_default_roles() {
        // Admin role - full access
        Role admin;
        admin.name        = "admin";
        admin.description = "Administrator with full access";
        admin.permissions.push_back({
            Resource{"*", "*", "*"},
            {Action::ALL}
        });
        register_role(std::move(admin));

        // Operator role - read/write data
        Role ops;
        ops.name        = "operator";
        ops.description = "Operator with read/write access to data";
        ops.permissions.push_back({
            Resource{"datapoint", "*", "*"},
            {Action::READ, Action::WRITE}
        });
        ops.permissions.push_back({
            Resource{"route", "*", "*"},
            {Action::READ}
        });
        register_role(std::move(ops));

        // Viewer role - read only
        Role viewer;
        viewer.name        = "viewer";
        viewer.description = "Read-only access";
        viewer.permissions.push_back({
            Resource{"*", "*", "*"},
            {Action::READ}
        });
        register_role(std::move(viewer));

        // Service role - for internal services
        Role service;
        service.name        = "service";
        service.description = "Internal service access";
        service.permissions.push_back({
            Resource{"datapoint",  "*",           "*"           },
            {Action::READ, Action::WRITE, Action::DELETE}
        });
        service.permissions.push_back({
            Resource{"internal", "*", "*"},
            {Action::ALL}
        });
        register_role(std::move(service));
    }

    size_t role_count() const {
        std::shared_lock lock(mutex_);
        return roles_.size();
    }

    size_t policy_count() const {
        std::shared_lock lock(mutex_);
        return policies_.size();
    }

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, Role> roles_;
    std::vector<Policy> policies_;
};

}  // namespace ipb::security
