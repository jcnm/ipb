#include <ipb/common/error.hpp>

#include <iomanip>
#include <sstream>

namespace ipb::common {

// ============================================================================
// Error Implementation
// ============================================================================

std::string Error::to_string() const {
    std::ostringstream oss;

    // Format: [CATEGORY] ERROR_NAME (0xXXXX): message
    oss << "[" << category_name(category()) << "] " << error_name(code_) << " (0x" << std::hex
        << std::setw(4) << std::setfill('0') << static_cast<uint32_t>(code_) << ")";

    if (!message_.empty()) {
        oss << ": " << message_;
    }

    // Add source location if available
    if (location_.is_valid()) {
        oss << "\n    at " << location_.file << ":" << std::dec << location_.line;
        if (location_.function[0] != '\0') {
            oss << " in " << location_.function;
        }
    }

    // Add context
    for (const auto& [key, value] : context_) {
        oss << "\n    " << key << ": " << value;
    }

    // Add cause chain
    if (cause_.has_value() && *cause_) {
        oss << "\n  Caused by: " << (*cause_)->to_string();
    }

    return oss.str();
}

Error& Error::with_context(std::string_view key, std::string_view value) {
    context_.emplace_back(std::string(key), std::string(value));
    return *this;
}

}  // namespace ipb::common
