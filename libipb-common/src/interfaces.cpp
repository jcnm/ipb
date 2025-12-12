/**
 * @file interfaces.cpp
 * @brief IPB Common Interfaces Implementation
 *
 * All interfaces in interfaces.hpp are now pure virtual or fully inline.
 * This file serves as a placeholder for potential future explicit template
 * instantiations or non-inline utility functions.
 */

#include <ipb/common/interfaces.hpp>

namespace ipb::common {

// All interface methods are either:
// - Pure virtual (IIPBComponent, IIPBSinkBase, IProtocolSourceBase, etc.)
// - Inline implementations (Statistics, Result<T>)
// - Type-erased wrappers with inline forwarding (IIPBSink, IProtocolSource)
//
// No additional implementations needed here.

} // namespace ipb::common
