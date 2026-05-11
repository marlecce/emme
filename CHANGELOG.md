# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- **Request Timeout Enforcement**
  - Configurable request timeout (default 30s) via YAML and `EMME_REQUEST_TIMEOUT` env var
  - Configurable TLS handshake timeout (default 10s) via YAML and `EMME_TLS_HANDSHAKE_TIMEOUT` env var
  - HTTP/1.1 timeout enforcement with 408 Request Timeout response
  - HTTP/2 timeout enforcement in event loop
  - TLS handshake timeout with elapsed time validation
  - UUID v4 request correlation IDs (RFC 4122) for distributed tracing
  - Request timeout metrics: `emme_request_timeouts_total` counter
  - 7 unit tests for timeout logic and UUID format validation

- **Auto-Docs Skill**
  - Created reusable skill at `skills/auto-docs/` for automatic documentation updates
  - Workflow: 7-phase process (Impact Analysis → ROADMAP → CHANGELOG → Feature Docs → README → Verification)
  - Documentation priority matrix (P0/P1/P2 features)
  - Templates for ROADMAP, CHANGELOG, and feature documentation
  - Quality checks checklist for documentation reviews
  - Integrated into AGENTS.md alongside c-code-quality skill

- **C Code Quality Improvement Skill**
  - Created reusable skill at `skills/c-code-quality/` with 7 documentation files (1,627 lines total)
  - Workflow: 4-phase process (Analysis → Prioritization → Implementation → Verification)
  - 7 refactoring patterns with C-specific examples
  - Quality gates checklist with HIGH/MEDIUM/LOW priority categorization
  - Quick reference card with one-liner commands
  - Before/after examples from actual codebase refactoring
  - Applied to `src/metrics.c` (126 → 40 lines via helper extraction)
  - Applied to `src/server.c` (161 → 109 lines via helper extraction)

### Code Quality Improvements
- **src/metrics.c Refactoring**
  - Extracted 3 helper functions: `metrics_format_counter()`, `metrics_format_gauge()`, `metrics_format_histogram()`
  - Reduced `metrics_format_prometheus()` from 126 to 40 lines
  - Added named constants: `METRICS_BUFFER_SIZE`, `DEFAULT_METRICS_PORT`, `MAX_PORT_NUMBER`
  - Added snprintf return value validation for buffer truncation detection
  - Removed duplicate includes

- **src/server.c Refactoring**
  - Extracted `h2_session_init()` helper (69 lines) for HTTP/2 session setup
  - Extracted `h2_session_send_initial_settings()` helper (15 lines)
  - Reduced `handle_http2_connection()` from 161 to 109 lines (-32%)
  - Added named constants: `H2_POLL_ERROR_EVENTS`, `NS_PER_MS`, `US_PER_MS`
  - Replaced 6 magic numbers with named constants
  - Improved unclear comment ("legacy methods" → "synchronous SSL I/O")
  - All changes verified: zero warnings, 76/76 tests passing

- **src/main.c Improvements**
  - Added named constants: `DEFAULT_METRICS_PORT`, `MAX_PORT_NUMBER`, `METRICS_SERVER_BACKLOG`
  - Consistent logging format for all error paths
  - Clear error messages for metrics server initialization failures

- **src/router.c Cleanup**
  - Removed duplicate includes
  - Added request metrics integration point

### Prometheus Metrics Endpoint
  - New metrics server on port 9090 (configurable via `EMME_METRICS_PORT`)
  - `/metrics` endpoint returning Prometheus text format
  - Request metrics: `emme_requests_total` counter, `emme_request_duration_seconds` histogram
  - Connection metrics: `emme_active_connections` gauge
  - Thread pool metrics: active threads, queue depth gauges
  - TLS metrics: `emme_tls_handshakes_total` counter, `emme_tls_handshake_duration_seconds` histogram
  - io_uring metrics: SQE/CQE depth gauges
  - Shutdown metrics: `emme_shutdown_drain_active` gauge
  - Lock-free implementation using atomic operations (<1% performance overhead)
  - 8 new unit tests for metrics module

- **Environment Variable Overrides**
  - `EMME_MAX_CONNECTIONS` - override max connections (validation: 1-1000000)
  - `EMME_SSL_CERT_PATH` - override SSL certificate path (supports spaces in paths)
  - `EMME_SSL_KEY_PATH` - override SSL private key path (supports spaces in paths)
  - Refactored env override logic into `apply_env_overrides()` function for testability
  - 7 new unit tests covering valid, invalid, and out-of-range environment values
  - Comprehensive validation with fallback to config.yaml values on error

- **Graceful Shutdown Improvements**
  - Configurable shutdown timeout via `EMME_SHUTDOWN_TIMEOUT` environment variable
  - Dual-mode signal handling: SIGTERM (graceful) vs SIGINT (immediate)
  - Health endpoint returns 503 Service Unavailable during drain phase with `Retry-After: 5` header
  - Shutdown metrics logging: duration, completed requests, forced closures, peak in-flight
  - Lock-free shutdown state machine with atomic operations
  - 9 new unit tests for shutdown context and state transitions

- **SSL Performance Optimizations**
  - Configurable SSL read buffer size (4KB-64KB, default 32KB)
  - SSL partial write support (`SSL_MODE_ENABLE_PARTIAL_WRITE`)
  - SSL buffer release on idle (`SSL_MODE_RELEASE_BUFFERS`)
  - TLS session cache configuration (size, timeout)
  - TLS session ticket key support

- **HTTP/2 Configuration**
  - Keepalive timeout (10-300 seconds)
  - Max requests per connection (1-100,000)
  - Max concurrent streams (1-1,000)

- **Documentation**
  - Performance Tuning Guide (`docs/PERFORMANCE.md`)
  - Configuration Improvements documentation (`docs/CONFIG_IMPROVEMENTS.md`)
  - Updated Deployment Guide with SSL tuning section
  - Updated Health Check documentation

### Changed
- **Server Architecture Refactoring**
  - Split monolithic `start_server()` (213 lines) into 5 focused functions
  - Added `initialize_server()` for setup with unified error handling
  - Added `accept_and_dispatch_client()` for accept loop logic
  - Added `drain_in_flight_requests()` for graceful shutdown drain
  - Added `perform_shutdown()` for cleanup and metrics logging
  - Added `cleanup_server_resources()` for centralized resource deallocation
  - Introduced named constants: `SERVER_BACKLOG`, `THREAD_POOL_MIN_THREADS`, `SESSION_STATS_INTERVAL_SEC`
  - Consistent logging: all `fprintf`/`perror`/`printf` → `log_message()`
  - Improved code readability and maintainability

- **Configuration System Refactoring**
  - Split monolithic `load_config()` (300 lines) into 7 focused parser functions
  - Added line number tracking to all YAML error messages
  - Implemented macro-based field parsing (`PARSE_FIELD`, `PARSE_STRING`, `PARSE_BOOL`)
  - Reduced cyclomatic complexity from ~50 to ~8 per function
  - Improved config.c branch coverage from ~40% to **67%**

- **Testing Improvements**
  - Added 15 new unit tests for configuration parsing
  - Added 9 new unit tests for shutdown context and state machine
  - Increased total tests from 37 to **61** (+65%)
  - Added tests for SSL performance settings
  - Added tests for HTTP/2 configuration
  - Added tests for boolean parsing variations (true/false/yes/no/0/1)
  - Improved overall project coverage from ~45% to **54%**

- **Performance**
  - SSL buffer size increased from 8KB to 32KB (4x)
  - **Throughput improvement: 460 → 2,236 req/s (4.9x)**
  - Memory per connection reduced by 40% (~84KB → ~50KB)
  - Syscall overhead reduced by 75%
  - Graceful shutdown overhead: <1% (lock-free atomics only)

### Technical Debt
- Thread pool still uses mutex-based synchronization (lock-free implementation planned for Phase 3)
- Work-stealing algorithm not yet implemented (Phase 3)
- Prometheus metrics endpoint not yet implemented (Phase 1 - P1 priority)

### Completed
- ✅ **Graceful shutdown with drain logic** (P0 - Phase 1)
  - 30s configurable drain timeout
  - 503 health endpoint during drain
  - SIGTERM graceful vs SIGINT immediate
  - Shutdown metrics logging
- ✅ **Server code refactoring** for improved maintainability

### Fixed
- Fixed SSL private key path typo in README.md (removed trailing quote)
- Fixed incorrect TLS section name in deployment guide (`tls:` → `ssl:`)

### Documentation
- Updated README.md with SSL performance settings
- Updated README.md with HTTP/2 configuration options
- Updated README.md with benchmark results
- Updated README.md with detailed testing coverage breakdown
- Updated AGENTS.md with architecture improvements
- Updated AGENTS.md with current test coverage status
- Updated DEPLOYMENT.md with SSL performance tuning section
- Updated DEPLOYMENT.md with memory troubleshooting guide
- Updated HEALTH_CHECK.md with related documentation links

---

## [0.2.0] - 2026-05-03

### Added
- HTTP/2 support via nghttp2 with ALPN negotiation
- TLS 1.3 support
- TLS session resumption (cache and tickets)
- YAML configuration validation
- Graceful shutdown on SIGTERM
- Health check endpoint at `/health`
- Environment variable overrides (`EMME_CONFIG_PATH`, `EMME_PORT`, `EMME_LOG_LEVEL`)

### Changed
- SSL buffer optimization (32KB default)
- Thread pool with dynamic scaling (min/max threads)
- Async logging with lock-free ring buffer

### Fixed
- Various bug fixes and stability improvements

---

## [0.1.0] - 2026-04-15

### Added
- Initial release
- io_uring-based async I/O
- Custom HTTP parser
- Thread pool implementation
- TLS/HTTPS support with OpenSSL
- YAML configuration loading
- Structured logging (JSON and plain text formats)
- Basic routing (static files, reverse proxy)
- Unit and integration tests with Criterion

---

## Version History

| Version | Date | Key Features |
|---------|------|--------------|
| 0.2.0 | 2026-05-03 | HTTP/2, TLS 1.3, session resumption |
| 0.1.0 | 2026-04-15 | Initial release with io_uring, TLS, HTTP/1.1 |

---

## Upcoming (Roadmap)

See [ROADMAP.md](ROADMAP.md) for detailed implementation plan with priorities, timelines, and acceptance criteria.

**Phase 1 (Weeks 1-2) - Production Readiness (P0/P1):**
- [x] Graceful shutdown with 30s drain timeout (configurable via env var)
- [x] Full environment variable overrides (port, log_level, shutdown_timeout, max_connections, ssl paths)
- [ ] Prometheus metrics endpoint
- [ ] HTTP/2 reverse proxy
- [ ] Request timeout enforcement

**Phase 2 (Weeks 3-4) - Security Hardening (P2):**
- [ ] Security headers
- [ ] Per-IP connection limits
- [ ] TLS session cache size limits
- [ ] Backend certificate validation
- [ ] Log injection prevention
- [ ] Request ID / correlation

**Phase 3 (Weeks 5-6) - Performance Optimization:**
- [ ] Lock-free thread pool with work-stealing
- [ ] Zero-copy static files
- [ ] Batch accept
- [ ] io_uring for TLS I/O

**Phase 4 (Weeks 7-8) - Advanced Features:**
- [ ] HTTP/2 push hints
- [ ] TLS 1.3 early data
- [ ] Adaptive concurrency

**Phase 5 (Weeks 9-10) - Quality & Operations:**
- [ ] Coverage thresholds in CI
- [ ] Rate limiting
- [ ] Documentation & runbooks
