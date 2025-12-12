# Architecture Analysis Report - IPB (Industrial Protocol Bridge)

**Version**: 2.0
**Date**: 2025-12-12
**Language**: English

---

## 1. Executive Summary

**IPB (Industrial Protocol Bridge)** is a high-performance industrial communication middleware designed for real-time environments with microsecond latency requirements. The project is structured as a C++20 mono-repository with a modular architecture based on scoops (input data collectors) and sinks (outputs).

### Key Strengths
- Well-defined modular architecture
- Modern C++20 usage
- Lock-free data structures for real-time performance
- Mature CMake configuration with modular build options
- Separate transport layer abstraction
- Refactored components (message bus, rule engine, scheduler, registries)

### Areas for Improvement
- **Lack of modern dependency manager** (vcpkg/Conan)
- **Limited test coverage**
- **Router still monolithic** with multiple responsibilities
- **No CI/CD integration**
- **No static code analysis** (clang-tidy, cppcheck)

---

## 2. Mono-Repository Structure Analysis

### 2.1 Current Structure (v1.5.0)

```
ipb/
├── apps/                        # Applications
│   ├── ipb-gate/                # Main orchestrator application
│   │   ├── src/
│   │   │   ├── main.cpp         # Application entry point
│   │   │   ├── orchestrator.cpp # Main orchestration logic
│   │   │   ├── config_loader.cpp# YAML configuration loading
│   │   │   ├── daemon_utils.cpp # Daemon mode utilities
│   │   │   └── signal_handler.cpp
│   │   ├── include/ipb/gate/
│   │   └── config/
│   │
│   └── ipb-bridge/              # Lightweight bridge application
│       ├── src/
│       │   ├── main.cpp
│       │   ├── bridge.cpp       # Bridge logic
│       │   └── config.cpp
│       └── include/
│
├── core/                        # Core libraries
│   ├── common/                  # libipb-common - CRITICAL (100% coverage required)
│   │   ├── include/ipb/common/
│   │   │   ├── data_point.hpp   # Main data structure (~600 lines)
│   │   │   ├── dataset.hpp      # Collection of DataPoints
│   │   │   ├── endpoint.hpp     # Network abstractions + RT primitives
│   │   │   ├── error.hpp        # Comprehensive error handling
│   │   │   ├── interfaces.hpp   # Base interfaces (IIPBComponent, etc.)
│   │   │   ├── debug.hpp        # Debugging utilities
│   │   │   ├── platform.hpp     # Platform-specific abstractions
│   │   │   └── protocol_capabilities.hpp
│   │   ├── src/
│   │   ├── tests/
│   │   └── examples/
│   │
│   ├── components/              # Refactored modular components (NEW)
│   │   ├── include/ipb/core/
│   │   │   ├── config/          # Configuration loading
│   │   │   ├── message_bus/     # Pub/Sub pattern implementation
│   │   │   ├── rule_engine/     # Pattern matching & routing rules
│   │   │   ├── scheduler/       # EDF scheduling
│   │   │   ├── scoop_registry/  # Dynamic scoop management
│   │   │   └── sink_registry/   # Dynamic sink management + load balancing
│   │   └── src/
│   │
│   ├── router/                  # libipb-router - CRITICAL (100% coverage required)
│   │   └── include/ipb/router/
│   │       └── router.hpp       # ~485 lines - Still needs decomposition
│   │
│   └── security/                # Security components
│       └── include/ipb/security/
│
├── sinks/                       # Output adapters (6 modules)
│   ├── console/                 # Console output sink (6 format options)
│   ├── syslog/                  # Syslog sink (RFC compliant, remote support)
│   ├── mqtt/                    # MQTT sink (50K msg/s, 6 formats, 5 topic strategies)
│   ├── kafka/                   # Apache Kafka sink
│   ├── sparkplug/               # Sparkplug B sink
│   └── zmq/                     # ZeroMQ sink
│
├── scoops/                      # Data collectors (5 modules)
│   ├── console/                 # Console input scoop
│   ├── modbus/                  # Modbus protocol scoop
│   ├── mqtt/                    # MQTT subscriber scoop
│   ├── opcua/                   # OPC UA scoop
│   └── sparkplug/               # Sparkplug B scoop
│
├── transport/                   # Transport layers (NEW)
│   ├── mqtt/                    # MQTT transport
│   │   └── (Paho MQTT / CoreMQTT backend support)
│   └── http/                    # HTTP transport
│       └── (libcurl backend)
│
├── cmake/                       # CMake build system
│   ├── IPBDependencies.cmake    # Dependency management
│   ├── IPBOptions.cmake         # Build options
│   ├── IPBPrintConfig.cmake     # Configuration printing
│   ├── build_config.hpp.in      # Build configuration template
│   └── build_info.hpp.in        # Build info template
│
├── examples/                    # Example applications
│   ├── complete_industrial_setup.cpp
│   ├── mock_data_flow_test.cpp
│   └── gateway-config.yaml
│
├── scripts/                     # Build & installation scripts
│   ├── build.sh                 # Main build script
│   ├── install-deps-linux.sh
│   └── install-deps-macos.sh
│
├── tests/                       # Test suite
│   └── unit/
│
├── docs/                        # Documentation
│   └── MQTT_V5_NATIVE_PROPOSAL.md
│
├── CMakeLists.txt               # Root CMake configuration
├── README.md                    # Project README
├── ARCHITECTURE.md              # This document
└── CHANGELOG.md                 # Version history
```

### 2.2 Structure Evaluation

| Component | Status | Priority |
|-----------|--------|----------|
| `core/common` | Structure OK, needs more tests | CRITICAL |
| `core/components` | Well modularized (NEW) | OK |
| `core/router` | Still monolithic, needs decomposition | HIGH |
| `sinks/*` | Well modularized | OK |
| `scoops/*` | Well modularized | OK |
| `transport/*` | New abstraction layer | OK |
| `apps/ipb-gate` | Well structured | OK |
| `apps/ipb-bridge` | New lightweight option | OK |

---

## 3. Critical Analysis of Router

### 3.1 Identified Issues

The file `core/router/include/ipb/router/router.hpp` still has some architectural concerns:

#### 3.1.1 Single Responsibility Principle Violation
The `Router` class (~485 lines) manages:
- EDF (Earliest Deadline First) scheduling
- Sink management
- Routing rules
- Load balancing
- Statistics
- Hot-reload
- Memory pools
- Dead letter queue

**Status**: Partially addressed by extracting components to `core/components/`, but router still orchestrates all of these.

#### 3.1.2 Use of `std::regex` in Real-Time Context

```cpp
// router.hpp:18
#include <regex>
// In RoutingRule::address_pattern
std::string address_pattern;  // Regex pattern for addresses
```

**Critical Issue**: `std::regex` is not designed for real-time:
- Unpredictable dynamic allocations
- Non-deterministic performance
- Can cause deadline overruns

**Recommended Alternatives**:
1. **CTRE (Compile-Time Regular Expressions)** - Compile-time evaluation
2. **RE2** (Google) - Linear time guarantees
3. **Hyperscan** (Intel) - Optimized for high-throughput matching
4. **Trie/Radix Tree** - For simple address patterns

### 3.2 Proposed Refactored Architecture

The `core/components/` directory now contains extracted components:

```
┌─────────────────────────────────────────────────────────────┐
│                      MessageBus                              │
│  - Simplified publish/subscribe interface                    │
│  - Complete decoupling of producers/consumers               │
└─────────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
┌─────────────────┐  ┌────────────────┐  ┌─────────────────┐
│  RuleEngine     │  │  EDFScheduler  │  │  SinkRegistry   │
│  - Rule         │  │  - EDF         │  │  - Sink         │
│    evaluation   │  │    scheduling  │  │    registration │
│  - Pattern      │  │  - Deadlines   │  │  - Load balance │
│    matching     │  │  - Priorities  │  │  - Failover     │
└─────────────────┘  └────────────────┘  └─────────────────┘
```

### 3.3 Approach Comparison

| Criterion | Current Router | Modular Components |
|-----------|----------------|---------------------|
| Coupling | High | Low |
| Testability | Difficult | Easy (isolated components) |
| Extensibility | Central modification | Plugin addition |
| Performance | ~2M msg/s | Potential >5M msg/s |
| Determinism | Medium (regex, mutex) | High (lock-free, CTRE) |

---

## 4. Dependency Management - Modern Solution Proposal

### 4.1 Current State

```cmake
# Dependencies found manually or via pkg-config
find_package(jsoncpp QUIET)
find_package(yaml-cpp QUIET)
find_library(PAHO_MQTT_CPP_LIB paho-mqttpp3)
```

**Issues**:
- Manual installation required
- No dependency versioning
- Not reproducible across machines

### 4.2 Recommended Solution: vcpkg

Create a `vcpkg.json` file at the root:

```json
{
  "$schema": "https://raw.githubusercontent.com/microsoft/vcpkg-tool/main/docs/vcpkg.schema.json",
  "name": "ipb",
  "version": "1.5.0",
  "dependencies": [
    "jsoncpp",
    "yaml-cpp",
    "gtest",
    "benchmark",
    {
      "name": "paho-mqttpp3",
      "features": ["ssl"],
      "platform": "!windows"
    },
    {
      "name": "ctre",
      "version>=": "3.8"
    },
    {
      "name": "fmt",
      "version>=": "10.0"
    },
    {
      "name": "spdlog",
      "features": ["fmt-external"]
    }
  ],
  "overrides": [],
  "builtin-baseline": "2024.01.12"
}
```

---

## 5. Test Strategy - Plan for 100% Coverage

### 5.1 Current Test State

| File | Lines | Existing Tests | Coverage |
|------|-------|----------------|----------|
| `data_point.hpp` | ~600 | Minimal | <5% |
| `endpoint.hpp` | ~525 | 0 | 0% |
| `interfaces.hpp` | ~425 | 0 | 0% |
| `router.hpp` | ~485 | 0 | 0% |
| **Total** | ~2000 | Minimal | **<5%** |

### 5.2 Proposed Test Structure

```
tests/
├── unit/                         # Unit tests (fast, isolated)
│   ├── common/
│   │   ├── test_timestamp.cpp
│   │   ├── test_value.cpp
│   │   ├── test_data_point.cpp
│   │   ├── test_dataset.cpp
│   │   ├── test_endpoint.cpp
│   │   ├── test_spsc_ringbuffer.cpp
│   │   └── test_memory_pool.cpp
│   ├── components/
│   │   ├── test_message_bus.cpp
│   │   ├── test_rule_engine.cpp
│   │   ├── test_scheduler.cpp
│   │   ├── test_scoop_registry.cpp
│   │   └── test_sink_registry.cpp
│   ├── router/
│   │   ├── test_routing_rule.cpp
│   │   ├── test_value_condition.cpp
│   │   ├── test_edf_scheduler.cpp
│   │   └── test_router.cpp
│   └── CMakeLists.txt
│
├── integration/                  # Integration tests
│   ├── test_scoop_sink_flow.cpp
│   ├── test_router_with_sinks.cpp
│   ├── test_hot_reload.cpp
│   └── CMakeLists.txt
│
├── performance/                  # Benchmarks
│   ├── bench_data_point.cpp
│   ├── bench_router_throughput.cpp
│   ├── bench_latency.cpp
│   └── CMakeLists.txt
│
└── fuzzing/                      # Fuzzing tests
    ├── fuzz_data_point_deserialize.cpp
    └── fuzz_config_parser.cpp
```

---

## 6. Code Quality Tools

### 6.1 clang-tidy Configuration

Create `.clang-tidy`:

```yaml
---
Checks: >
  -*,
  bugprone-*,
  cert-*,
  clang-analyzer-*,
  cppcoreguidelines-*,
  modernize-*,
  performance-*,
  readability-*,
  -modernize-use-trailing-return-type,
  -readability-magic-numbers,
  -cppcoreguidelines-avoid-magic-numbers,
  -cppcoreguidelines-pro-type-reinterpret-cast

WarningsAsErrors: 'bugprone-*,cert-*,clang-analyzer-*'

CheckOptions:
  - key: readability-identifier-naming.ClassCase
    value: CamelCase
  - key: readability-identifier-naming.FunctionCase
    value: lower_case
  - key: readability-identifier-naming.VariableCase
    value: lower_case
  - key: readability-identifier-naming.PrivateMemberSuffix
    value: _
  - key: performance-unnecessary-value-param.AllowedTypes
    value: 'std::string_view;std::span'
```

### 6.2 clang-format Configuration

Create `.clang-format`:

```yaml
---
Language: Cpp
BasedOnStyle: LLVM
Standard: c++20
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Attach
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: Never
AllowShortLoopsOnASingleLine: false
AlwaysBreakTemplateDeclarations: Yes
PointerAlignment: Left
SpaceAfterTemplateKeyword: true
IncludeBlocks: Regroup
IncludeCategories:
  - Regex: '^<ipb/'
    Priority: 1
  - Regex: '^<.*\.hpp>'
    Priority: 2
  - Regex: '^<.*>'
    Priority: 3
  - Regex: '.*'
    Priority: 4
```

---

## 7. Proposed CI/CD Pipeline

### 7.1 GitHub Actions

Create `.github/workflows/ci.yml`:

```yaml
name: CI Pipeline

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-22.04, macos-13]
        compiler: [gcc-13, clang-17]
        build_type: [Debug, Release]
        exclude:
          - os: macos-13
            compiler: gcc-13

    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4

      - name: Setup vcpkg
        uses: lukka/run-vcpkg@v11
        with:
          vcpkgGitCommitId: '2024.01.12'

      - name: Configure CMake
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
            -DBUILD_TESTING=ON \
            -DENABLE_COVERAGE=${{ matrix.build_type == 'Debug' }}

      - name: Build
        run: cmake --build build --parallel

      - name: Run Tests
        run: ctest --test-dir build --output-on-failure

      - name: Generate Coverage
        if: matrix.build_type == 'Debug' && matrix.compiler == 'gcc-13'
        run: |
          lcov --capture --directory build --output-file coverage.info
          lcov --remove coverage.info '/usr/*' --output-file coverage.info

      - name: Upload Coverage
        if: matrix.build_type == 'Debug' && matrix.compiler == 'gcc-13'
        uses: codecov/codecov-action@v3
        with:
          files: coverage.info
          fail_ci_if_error: true

  static-analysis:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Run clang-tidy
        run: |
          cmake -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
          run-clang-tidy -p build

      - name: Run cppcheck
        run: cppcheck --project=build/compile_commands.json --error-exitcode=1

  sanitizers:
    runs-on: ubuntu-22.04
    strategy:
      matrix:
        sanitizer: [address, thread, undefined]

    steps:
      - uses: actions/checkout@v4

      - name: Build with Sanitizer
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=Debug \
            -DENABLE_SANITIZERS=ON
          cmake --build build

      - name: Run Tests
        run: ctest --test-dir build --output-on-failure
```

---

## 8. Priority Recommendations

### 8.1 Short Term (1-2 sprints)

1. **Add vcpkg** for dependency management
2. **Fix existing tests** (currently not compiling)
3. **Add clang-tidy and clang-format** with pre-commit hooks
4. **Create basic tests** for `Timestamp`, `Value`, `DataPoint`

### 8.2 Medium Term (3-6 sprints)

1. **Achieve 100% coverage** on `core/common`
2. **Continue Router decomposition** into separate components:
   - Complete integration with `core/components/`
   - Use extracted `RuleEngine`, `EDFScheduler`, `SinkRegistry`
3. **Replace `std::regex`** with CTRE or RE2
4. **Add integration tests**

### 8.3 Long Term

1. **Achieve 100% coverage** on `core/router`
2. **Add fuzzing** for parsers
3. **Implement MessageBus** fully decoupled
4. **Complete Doxygen documentation**

---

## 9. Conclusion

IPB is a well-architected project with solid foundations for real-time systems. The recent restructuring (v1.5.0) has improved the modularity significantly with:

- New `core/components/` for extracted, testable components
- New `transport/` layer for backend abstraction
- New `ipb-bridge` application for lightweight deployments
- Cleaner separation between sinks and scoops

However, major gaps remain:

1. **Insufficient tests** - Critical risk for industrial systems
2. **Router still partially monolithic** - Difficult to maintain and test
3. **No modern dependency management** - Not reproducible
4. **No CI/CD** - Quality not guaranteed

Adopting the recommendations in this report will transform IPB into a production-quality industrial project with reliability and determinism guarantees.

---

## Appendices

### A. Target Coverage Matrix

| Component | Current | Phase 1 Target | Phase 2 Target |
|-----------|---------|----------------|----------------|
| `core/common` | <5% | 80% | **100%** |
| `core/components` | 0% | 60% | **100%** |
| `core/router` | 0% | 60% | **100%** |
| `sinks/*` | 0% | 50% | 80% |
| `scoops/*` | 0% | 50% | 80% |
| `apps/*` | 0% | 40% | 70% |

### B. References

- [C++ Core Guidelines](https://isocpp.github.io/CppCoreGuidelines/)
- [CTRE - Compile Time Regular Expressions](https://github.com/hanickadot/compile-time-regular-expressions)
- [vcpkg Documentation](https://learn.microsoft.com/en-us/vcpkg/)
- [Google Test](https://google.github.io/googletest/)
- [Clang-Tidy Checks](https://clang.llvm.org/extra/clang-tidy/checks/list.html)
