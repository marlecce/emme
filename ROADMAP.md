# Emme Roadmap

This document outlines the development roadmap for Emme, prioritized by production readiness impact and competitive differentiation.

## Prioritization Framework

- **P0 (CRITICAL)**: Must-have for production deployment. Blocks adoption in enterprise environments.
- **P1 (HIGH)**: Required for competitive parity with nginx, Envoy. Expected by operators.
- **P2 (MEDIUM)**: Security hardening and operational excellence. Differentiates from hobby projects.
- **P3 (LOW)**: Nice-to-have features. Incremental improvements.

---

## Phase 1: Production Readiness Foundation (Weeks 1-2)

### P0: Graceful Shutdown & Drain Logic ✅ COMPLETED
**Severity**: CRITICAL | **Impact**: Request drops during deployments, data corruption risk

**Implementation** (Completed 2026-05-10):
- [x] Added connection tracking with atomic reference counting (`g_shutdown_ctx.in_flight_requests`)
- [x] Implemented 30s drain timeout (configurable via `EMME_SHUTDOWN_TIMEOUT` env var)
- [x] Updated SIGTERM handler to:
  - Stop accepting new connections
  - Wait for active connections to complete (max 30s)
  - Force-close remaining connections after timeout
  - Log shutdown statistics (completed vs. forced)
- [x] Health endpoint returns 503 with `Retry-After: 5` header during drain
- [x] SIGINT (Ctrl+C) triggers immediate shutdown for development convenience
- [x] Lock-free shutdown state machine (`SHUTDOWN_STATE_RUNNING` → `DRAINING` → `FORCED`)
- [x] Shutdown metrics: duration, completed, forced, peak in-flight requests

**Files modified**: `src/server.c`, `src/router.c`, `include/server.h`, `tests/unit/test_shutdown.c`

**Acceptance criteria** (All Met):
- [x] Zero request drops during rolling deployments
- [x] Shutdown completes within 35s max (30s drain + 5s cleanup)
- [x] Metrics logged: `Duration: Xms | Completed: Y | Forced: Z | Peak: W`
- [x] 9 unit tests added, all passing (61/61 total tests)
- [x] Zero compiler warnings, zero memory leaks
- [x] Performance overhead <1% (lock-free atomics only)

---

### P0: Environment Variable Overrides ✅ COMPLETED
**Severity**: CRITICAL | **Impact**: Cannot deploy in Kubernetes without ConfigMap mounts

**Implementation** (Completed 2026-05-10):
- [x] `EMME_CONFIG_PATH` - supported
- [x] `EMME_PORT` - supported (validation: 1-65535)
- [x] `EMME_LOG_LEVEL` - supported
- [x] `EMME_SHUTDOWN_TIMEOUT` - supported (validation: 1-300s, default 30s)
- [x] `EMME_MAX_CONNECTIONS` - supported (validation: 1-1000000)
- [x] `EMME_SSL_CERT_PATH` - supported (full path, spaces allowed)
- [x] `EMME_SSL_KEY_PATH` - supported (full path, spaces allowed)
- [ ] YAML interpolation: `${ENV_VAR}` syntax - deferred to P2

**Files modified**: `src/main.c` (bootstrap), `src/config.c` (extracted `apply_env_overrides()`), `include/config.h` (new function declaration), `tests/unit/test_env_vars.c` (7 new tests)

**Acceptance criteria** (All Met):
- [x] Can override port, log level, shutdown timeout via environment
- [x] Can override SSL certificate and key paths for multi-cloud deployments
- [x] Can override max_connections for capacity tuning
- [x] All env vars have proper validation with fallback to config.yaml values
- [x] 7 unit tests added covering valid, invalid, and out-of-range values
- [x] All 68 tests passing (100%)
- [x] Zero compiler warnings, zero memory leaks

---

### P1: Prometheus Metrics Endpoint ✅ COMPLETED
**Severity**: HIGH | **Impact**: No observability for alerting, dashboards, capacity planning

**Implementation** (Completed 2026-05-10):
- [x] Open port 9090 for metrics (configurable via `EMME_METRICS_PORT`)
- [x] Expose metrics in Prometheus text format at `/metrics`:
  - `emme_requests_total` - counter (total HTTP requests)
  - `emme_request_duration_seconds` - histogram (request latency with 14 buckets: 1ms-50s)
  - `emme_active_connections` - gauge (current active connections)
  - `emme_thread_pool_active_threads` - gauge (active threads in pool)
  - `emme_thread_pool_queue_depth` - gauge (thread pool queue depth)
  - `emme_tls_handshakes_total` - counter (total TLS handshakes)
  - `emme_tls_handshake_duration_seconds` - histogram (TLS handshake latency)
  - `emme_io_uring_sqe_depth` - gauge (io_uring submission queue depth)
  - `emme_io_uring_cqe_depth` - gauge (io_uring completion queue depth)
  - `emme_shutdown_drain_active` - gauge (1 during graceful shutdown drain)
- [x] Thread-safe metric updates using atomic operations
- [x] Lock-free counters and gauges for hot path performance
- [x] Histogram with configurable bucket boundaries

**Files modified**: `src/metrics.c` (new - 425 lines), `include/metrics.h` (new), `src/main.c` (metrics server bootstrap), `src/server.c` (metrics integration), `src/router.c` (request metrics), `tests/unit/test_metrics.c` (8 new tests)

**Acceptance criteria** (All Met):
- [x] `curl http://localhost:9090/metrics` returns valid Prometheus text format
- [x] All metrics update in real-time
- [x] Zero allocation in hot path for metric increments (atomic operations only)
- [x] 8 unit tests added, all passing (76/76 total tests)
- [x] Zero compiler warnings, zero memory leaks
- [x] Performance overhead <1% (lock-free atomics, no mutexes in hot path)

---

### P1: Code Quality Improvement Skill ✅ COMPLETED
**Severity**: HIGH | **Impact**: Inconsistent code quality, hard to maintain, knowledge loss

**Implementation** (Completed 2026-05-10):
- [x] Created reusable skill at `skills/c-code-quality/` with 7 documentation files (1,627 lines)
- [x] Defined 4-phase workflow: Analysis → Prioritization → Implementation → Verification
- [x] Documented 7 refactoring patterns with C-specific examples
- [x] Created quality gates checklist with HIGH/MEDIUM/LOW priority
- [x] Added quick reference card with one-liner commands
- [x] Included before/after examples from actual codebase
- [x] Applied to `src/metrics.c`: 126 → 40 lines (68% reduction)
- [x] Applied to `src/server.c`: 161 → 109 lines (32% reduction)
- [x] Eliminated 12+ magic numbers across codebase
- [x] All changes verified: zero warnings, 76/76 tests passing

**Files created**: 
- `skills/c-code-quality/README.md` (166 lines) - Quick start guide
- `skills/c-code-quality/skill.md` (314 lines) - Complete workflow
- `skills/c-code-quality/checklist.md` (180 lines) - Quality gates
- `skills/c-code-quality/patterns.md` (438 lines) - Refactoring patterns
- `skills/c-code-quality/QUICKREF.md` - One-page reference
- `skills/c-code-quality/examples/` - Before/after examples

**Acceptance criteria** (All Met):
- [x] Skill can be invoked on any C file for quality improvement
- [x] Workflow is repeatable and produces consistent results
- [x] Patterns are C-specific (compiler flags, memory safety, thread safety)
- [x] Examples demonstrate real improvements from this codebase
- [x] Quality gates ensure zero warnings and all tests pass

---

### P1: HTTP/2 Reverse Proxy ✅ COMPLETED
**Severity**: HIGH | **Impact**: Multi-cloud backend routing only works for HTTP/1.1

**Current State**: ✅ Fully implemented with connection pooling, health checks, and circuit breaker

**Implementation**:
- [x] Add backend connection pooling (keep-alive connections to backends)
- [x] Implement HTTP/2 client via nghttp2 for upstream connections
- [x] Support TLS to backends with certificate validation
- [x] Configure backend health checks (interval, timeout, unhealthy threshold)
- [x] Add circuit breaker pattern (max failures, recovery timeout)
- [x] Metrics integration (12 new Prometheus metrics)
- [x] Unit tests (14 tests, 100% passing)

**Files created/modified**: 
- New: `src/http2_client.c` (534 lines), `include/http2_client.h` (72 lines)
- New: `src/backend_pool.c` (~682 lines), `include/backend_pool.h` (141 lines)
- New: `tests/unit/test_backend_pool.c` (~380 lines)
- Modified: `src/config.c`, `src/router.c`, `src/main.c`, `include/metrics.h`, `src/metrics.c`

**Acceptance criteria**:
- [x] HTTP/2 requests to `/api/*` proxy correctly to upstream
- [x] Backend TLS certificates validated (configurable to skip for dev)
- [x] Connection reuse across multiple requests (no new TCP per request)
- [x] Failed backends detected and removed from pool within 5s
- [x] Circuit breaker prevents cascade failures
- [x] Health checker runs in background thread per pool
- [x] Metrics exported for pool/health/circuit breaker state

---

### P1: Request Timeout Enforcement ✅ COMPLETED
**Severity**: HIGH | **Impact**: DoS protection via slowloris mitigation

**Implementation** (Completed 2026-05-11):
- [x] Configuration with `request_timeout_ms` (30s default) and `tls_handshake_timeout_ms` (10s default)
- [x] Environment variable overrides: `EMME_REQUEST_TIMEOUT`, `EMME_TLS_HANDSHAKE_TIMEOUT`
- [x] Track request start time in `HttpRequest` struct (HTTP/1.1) and `H2IO` struct (HTTP/2)
- [x] Timeout enforcement:
  - HTTP/1.1: Checked on every SSL_read() call
  - HTTP/2: Checked in main event loop
  - TLS handshake: Checked before each io_uring poll wait
- [x] Send 408 Request Timeout with `Retry-After: 5` header
- [x] UUID v4 request correlation IDs logged on timeout
- [x] Metrics counter: `emme_request_timeouts_total`

**Files modified**: 
- `src/server.c` - HTTP/1.1 timeout (line 896+), HTTP/2 timeout (line 818+), TLS handshake timeout (line 660+)
- `src/config.c` - Timeout config parsing and env overrides
- `src/metrics.c` - Timeout counter implementation
- `src/http_parser.c` - UUID generation for HTTP/1.1 requests
- `src/uuid.c` - Fixed RFC 4122 version 4 UUID generation
- `include/config.h` - `request_timeout_ms`, `tls_handshake_timeout_ms` fields
- `include/http_parser.h` - `request_id[37]` field in HttpRequest
- `include/http_status.h` - `HTTP_STATUS_REQUEST_TIMEOUT 408`
- `tests/unit/test_timeout.c` - 7 unit tests for timeout logic and UUID format

**Acceptance criteria** (All Met):
- [x] Zero compiler warnings (`-Wall -Wextra -std=c11`)
- [x] 97/97 tests passing (100%)
- [x] Lock-free implementation (atomic counters for metrics)
- [x] <1% performance overhead target
- [x] Configurable via YAML and environment variables
- [x] UUID format: RFC 4122 version 4, 36 characters, unique per request
- [x] 408 response includes `Retry-After: 5` header

**Test results**: 97/97 tests passing (100%)

---

## Phase 2: Security Hardening (Weeks 3-4)

### P2: Security Headers ✅ COMPLETED
**Severity**: MEDIUM | **Impact**: Clickjacking, MIME sniffing, downgrade attacks

**Implementation** (Completed 2026-05-11):
- [x] SecurityHeadersConfig struct with enabled flag and header array (max 16 headers)
- [x] CORSConfig struct with origin, methods, headers, credentials, max-age
- [x] YAML parsing for global `security_headers` section
- [x] Per-route `security_headers` and `cors` sections with inheritance model
- [x] HTTP/1.1 integration: `add_security_headers_to_buffer()`, `add_cors_headers_to_buffer()`
- [x] HTTP/2 integration: `h2_response_add_security_headers()`
- [x] Metrics counters: `emme_security_headers_sent_total`, `emme_cors_headers_sent_total`
- [x] 6 default security headers: HSTS, X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, CSP, Referrer-Policy
- [x] CORS support for API endpoints with configurable origin, methods, headers
- [x] Pre-computed headers at startup (zero runtime string formatting)

**Files modified**: 
- `include/config.h` - SecurityHeadersConfig, CORSConfig, SecurityHeader structs
- `src/config.c` - YAML parsing with validation and line number error reporting
- `src/router.c` - HTTP/1.1 security headers and CORS integration
- `src/http2_response.c` - HTTP/2 security headers and CORS integration
- `include/metrics.h` - Security headers and CORS metrics counters
- `src/metrics.c` - Metrics implementation and Prometheus export
- `tests/unit/test_security_headers.c` - 11 unit tests for security headers

**Acceptance criteria** (All Met):
- [x] All responses include security headers by default
- [x] Headers can be overridden for specific routes via inheritance model
- [x] No performance regression (headers pre-computed at startup)
- [x] CORS headers automatically added to configured routes
- [x] Metrics track security headers and CORS headers sent
- [x] 11 unit tests added, all passing (108/108 total tests)
- [x] Zero compiler warnings, zero memory leaks

**Test results**: 108/108 tests passing (100%)

---

### P2: Per-IP Connection Limits
**Severity**: MEDIUM | **Impact**: Single client can exhaust connection pool

**Current State**: Global `max_connections=100` but no per-IP limiting

**Implementation**:
- [ ] Track connections per IP using hash table (IP → count)
- [ ] Default limit: 10 connections per IP (configurable)
- [ ] Use atomic operations for count increments/decrements
- [ ] Reject new connections from IP with 429 Too Many Requests
- [ ] Add `Retry-After` header to rejection responses
- [ ] Clean up stale entries (IPs with 0 connections) periodically

**Files to modify**: `src/server.c`, `src/ip_limiter.c` (new), `include/ip_limiter.h` (new)

**Acceptance criteria**:
- Single IP cannot open >10 concurrent connections
- Connection limit enforced before TLS handshake (save resources)
- Hash table memory usage bounded (max 10K IPs tracked)

---

### P2: TLS Session Cache Size Limit
**Severity**: MEDIUM | **Impact**: Memory exhaustion via session cache growth

**Current State**: Session caching enabled but no max size configuration

**Implementation**:
- [ ] Add `ssl.session_cache_size` to config (default: 10K sessions)
- [ ] Add `ssl.session_cache_timeout` to config (default: 10h)
- [ ] Use OpenSSL's built-in session cache with size limits
- [ ] Monitor cache hit rate, log if <50% (misconfiguration signal)

**Files to modify**: `src/tls.c`, `src/config.c`

**Acceptance criteria**:
- Session cache memory bounded to ~5MB max (10K * 512 bytes)
- Cache hit rate logged every 5 minutes
- Sessions expire after timeout (no infinite growth)

---

### P2: Backend Certificate Validation
**Severity**: MEDIUM | **Impact**: MITM attacks on backend communication

**Current State**: Backend connections use plain TCP (no TLS to backends)

**Implementation**:
- [ ] Add `backends[].tls.enabled` to config
- [ ] Add `backends[].tls.ca_cert_path` for CA bundle
- [ ] Add `backends[].tls.verify_hostname` (default: true)
- [ ] Add `backends[].tls.client_cert_path` for mTLS
- [ ] Validate certificates on connection (fail if invalid)
- [ ] Support certificate pinning (optional, for high-security deployments)

**Files to modify**: `src/router.c`, `src/http2_client.c`, `src/config.c`

**Acceptance criteria**:
- Upstream TLS connections validated by default
- Self-signed certs rejected unless CA explicitly trusted
- Hostname mismatch detected and rejected
- mTLS supported for zero-trust deployments

---

### P2: Log Injection Prevention
**Severity**: MEDIUM | **Impact**: Log forgery, audit trail corruption

**Current State**: `log_message()` doesn't sanitize newlines in user-controlled data

**Implementation**:
- [ ] Add sanitization function to escape `\r`, `\n`, `\t` in user data
- [ ] Apply sanitization to:
  - Request paths in access logs
  - Query parameters
  - User-Agent headers
  - Custom header values
- [ ] Use structured logging format to separate fields clearly
- [ ] Add unit tests for log injection attempts

**Files to modify**: `src/log.c`, `include/log.h`

**Acceptance criteria**:
- `curl "http://localhost/%0d%0aX-Injected: header"` logs safely
- No newlines appear in logged user-controlled fields
- Performance impact <1% (sanitization is fast)

---

### P2: Request ID / Correlation
**Severity**: MEDIUM | **Impact**: Cannot trace requests across logs for debugging

**Current State**: No correlation IDs

**Implementation**:
- [ ] Generate UUID v4 for each incoming request (128-bit, stored as 36-char string)
- [ ] Add `X-Request-ID` header to response (echo incoming or generate new)
- [ ] Include request ID in all log entries: `[req-abc123...] message`
- [ ] Support incoming `X-Request-ID` header (use if provided, don't regenerate)
- [ ] Add request ID to metrics labels (for high-cardinality debugging)

**Files to modify**: `src/server.c`, `src/log.c`, `src/http_parser.c`

**Acceptance criteria**:
- Every log line includes request ID
- Request ID traceable across proxy hops (if emme is behind proxy)
- UUID generation overhead <100ns per request

---

## Phase 3: Performance Optimization (Weeks 5-6)

### P2: Lock-free Thread Pool
**Severity**: MEDIUM | **Impact**: Mutex contention limits scalability

**Current State**: Mutex-based task queue synchronization

**Implementation**:
- [ ] Replace mutex with lock-free ring buffer (single-producer, multi-consumer)
- [ ] Use atomic operations for queue head/tail pointers
- [ ] Implement work-stealing: idle threads steal from busy threads' local queues
- [ ] Use per-thread local queues to reduce contention
- [ ] Add backoff strategy for contention (exponential backoff before steal)

**Files to modify**: `src/thread_pool.c`, `include/thread_pool.h`

**Acceptance criteria**:
- Zero mutex locks in hot path (task push/pop)
- Scalability: 32 threads achieves >90% CPU utilization under load
- Work stealing activates when queue depth > threshold

---

### P3: Zero-copy Static Files
**Severity**: LOW | **Impact**: Unnecessary memory copies for static content

**Current State**: Static files read into buffer, then copied to SSL buffer

**Implementation**:
- [ ] Use `sendfile()` for non-TLS connections
- [ ] Use `splice()` for TLS connections (kernel 5.10+)
- [ ] Memory-map large files (>1MB) to avoid read syscalls
- [ ] Implement file descriptor cache (avoid reopen on every request)

**Files to modify**: `src/router.c`

**Acceptance criteria**:
- Static file serving throughput increases 2x for large files
- Memory usage per static request drops to ~4KB (page cache only)
- No regression for small files (<4KB)

---

### P3: Batch Accept
**Severity**: LOW | **Impact**: High syscall overhead under connection bursts

**Current State**: Single accept per io_uring submission

**Implementation**:
- [ ] Use `IORING_OP_ACCEPT` with multi-shot (kernel 5.19+)
- [ ] Accept up to 8 connections per SQE
- [ ] Distribute accepted connections across thread pool
- [ ] Handle partial failures (some accepts fail, others succeed)

**Files to modify**: `src/server.c`

**Acceptance criteria**:
- Connection burst (1K connections in 1s) handled with 8x fewer syscalls
- No increase in accept latency for individual connections

---

### P3: io_uring for TLS I/O
**Severity**: LOW | **Impact**: SSL_read/SSL_write block event loop

**Current State**: TLS I/O uses blocking OpenSSL calls with poll fallback

**Implementation**:
- [ ] Use io_uring for socket read/write operations
- [ ] Wrap SSL_read/SSL_write with io_uring async operations
- [ ] Handle OpenSSL's WANT_READ/WANT_WRITE with io_uring completion events
- [ ] Requires kernel 5.19+ for full async SSL support

**Files to modify**: `src/tls.c`, `src/server.c`

**Acceptance criteria**:
- No blocking calls in I/O event handlers
- Event loop latency <100μs even under SSL handshake load

---

## Phase 4: Advanced Features (Weeks 7-8)

### P3: HTTP/2 Push Hints
**Severity**: LOW | **Impact**: Missed optimization for cacheable assets

**Implementation**:
- [ ] Parse `Link: </style.css>; rel=preload` headers
- [ ] Proactively push hinted resources to client
- [ ] Limit push to cacheable assets (CSS, JS, fonts)
- [ ] Track pushed streams to avoid duplicate pushes

**Files to modify**: `src/server.c`, `src/http2_response.h`

**Acceptance criteria**:
- Page load time decreases 10-20% for asset-heavy pages
- No unnecessary pushes (client already cached asset)

---

### P3: TLS 1.3 Early Data (0-RTT)
**Severity**: LOW | **Impact**: Latency for repeat clients

**Implementation**:
- [ ] Enable TLS 1.3 0-RTT in OpenSSL config
- [ ] Add replay cache to detect replay attacks (configurable window)
- [ ] Only allow idempotent methods (GET, HEAD) in early data
- [ ] Document replay attack risk in deployment guide

**Files to modify**: `src/tls.c`, `src/config.c`

**Acceptance criteria**:
- Repeat clients complete TLS handshake in 0 RTTs
- Replay attacks detected and rejected for POST requests
- Early data rejected if replay detected

---

### P3: Adaptive Concurrency
**Severity**: LOW | **Impact**: Manual tuning required for different workloads

**Implementation**:
- [ ] Monitor request latency (p99) and error rate
- [ ] Adjust `max_connections` dynamically based on latency SLO
- [ ] Use LIM algorithm (Latency Invoked Multiplexing)
- [ ] Set min/max bounds to prevent thrashing

**Files to modify**: `src/server.c`, `src/thread_pool.c`

**Acceptance criteria**:
- Concurrency self-tunes under varying load
- p99 latency stays within 20% of target SLO
- No oscillation (stable convergence)

---

## Phase 5: Quality & Operations (Weeks 9-10)

### P3: Coverage Thresholds in CI
**Severity**: LOW | **Impact**: Code quality can regress without notice

**Current State**: Coverage generated but thresholds commented out

**Implementation**:
- [ ] Set branch coverage threshold: 80% for critical modules
- [ ] Set line coverage threshold: 85% for critical modules
- [ ] Fail CI if thresholds not met
- [ ] Add coverage badge to README
- [ ] Generate per-PR coverage diff comment

**Files to modify**: `.github/workflows/ci.yml`

**Acceptance criteria**:
- CI fails if coverage drops below threshold
- Coverage report posted as PR comment
- Trend visible over time (no silent regressions)

---

### P3: Rate Limiting
**Severity**: LOW | **Impact**: API abuse, brute force attacks unchecked

**Implementation**:
- [ ] Token bucket algorithm per IP
- [ ] Default: 100 requests/second per IP
- [ ] Configurable per-location in config.yaml
- [ ] Return 429 Too Many Requests with `Retry-After` header
- [ ] Expose rate limit metrics (`emme_rate_limit_hits_total`)

**Files to modify**: `src/rate_limiter.c` (new), `src/router.c`

**Acceptance criteria**:
- Burst of 1K requests from single IP limited within 1s
- Legitimate traffic not affected (average users <50 req/s)
- Rate limiter memory bounded (LRU eviction for old IPs)

---

### P3: Documentation & Runbooks
**Severity**: LOW | **Impact**: Operational knowledge gap for on-call engineers

**Implementation**:
- [ ] API documentation (OpenAPI spec for all endpoints)
- [ ] Deployment runbook (step-by-step for AWS, GCP, Azure)
- [ ] Performance benchmarks (req/s, latency under load)
- [ ] Troubleshooting guide (common errors, debugging steps)
- [ ] Architecture diagram (data flow, component interaction)

**Files to create**: `docs/API.md`, `docs/RUNBOOK.md`, `docs/BENCHMARKS.md`, `docs/ARCHITECTURE.md`

**Acceptance criteria**:
- New engineer can deploy to production in <1 hour using docs
- On-call can diagnose common issues without escalation
- Benchmarks reproducible (scripts provided)

---

## Completed Items

### v0.2.0 (May 2026)
- [x] HTTP/2 support via nghttp2 with ALPN negotiation
- [x] TLS 1.3 support
- [x] TLS session resumption (cache and tickets)
- [x] YAML configuration validation
- [x] Health check endpoint at `/health`
- [x] SSL buffer optimization (32KB default)
- [x] Thread pool with dynamic scaling (min/max threads)
- [x] Async logging with lock-free ring buffer
- [x] Performance tuning documentation

### v0.1.0 (April 2026)
- [x] io_uring-based async I/O
- [x] Custom HTTP parser
- [x] Thread pool implementation
- [x] TLS/HTTPS support with OpenSSL
- [x] YAML configuration loading
- [x] Structured logging (JSON and plain text)
- [x] Basic routing (static files, reverse proxy)
- [x] Unit and integration tests with Criterion

---

## Version History

| Version | Date | Key Features |
|---------|------|--------------|
| 0.2.0 | 2026-05-03 | HTTP/2, TLS 1.3, session resumption, SSL tuning |
| 0.1.0 | 2026-04-15 | Initial release with io_uring, TLS, HTTP/1.1 |

---

## Appendix: Competitive Analysis

### Feature Comparison with nginx

| Feature | nginx | Emme (v0.2.0) | Emme (Target) |
|---------|-------|---------------|---------------|
| HTTP/2 server | ✅ | ✅ | ✅ |
| HTTP/2 proxy | ✅ | ❌ | ✅ (Phase 1) |
| TLS 1.3 | ✅ | ✅ | ✅ |
| Graceful shutdown | ✅ | ❌ | ✅ (Phase 1) |
| Prometheus metrics | ❌ (requires nginx-plus) | ❌ | ✅ (Phase 1) |
| Per-IP connection limits | ✅ (with modules) | ❌ | ✅ (Phase 2) |
| Rate limiting | ✅ (limit_req) | ❌ | ✅ (Phase 3) |
| Dynamic config reload | ✅ | ❌ | ✅ (Future) |
| io_uring support | ❌ (epoll) | ✅ | ✅ |
| Work-stealing thread pool | ❌ | ❌ | ✅ (Phase 3) |
| Zero-copy static files | ✅ (sendfile) | ❌ | ✅ (Phase 3) |

### Differentiators

1. **io_uring-native**: Emme is built from the ground up with io_uring, providing better scalability on modern Linux kernels (5.10+)
2. **Work-stealing scheduler**: Planned implementation will outperform nginx's round-robin thread distribution under uneven load
3. **Simpler configuration**: YAML vs nginx's custom DSL reduces operational complexity
4. **Observable by default**: Prometheus metrics built-in vs nginx-plus paywall

---

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| io_uring kernel version requirements | High | Medium | Document minimum kernel version (5.10), provide epoll fallback |
| OpenSSL API complexity | Medium | High | Extensive testing, valgrind checks, fuzzing |
| Work-stealing correctness | Medium | High | Formal verification of lock-free algorithms, stress testing |
| Feature creep delaying v1.0 | High | Medium | Strict prioritization, defer P3 items to v1.1 |
| Performance regression from security features | Low | Medium | Benchmark after each feature, optimize hot paths |

---

## Success Metrics

### v1.0 Release Criteria (End of Phase 2)
- All P0 and P1 items completed
- 80% branch coverage on critical modules
- Zero known memory leaks (valgrind clean)
- Production deployment at 2+ reference customers
- p99 latency <10ms at 10K req/s

### v1.1 Release Criteria (End of Phase 4)
- All P2 items completed
- Performance parity with nginx on standard benchmarks
- Work-stealing thread pool operational
- 3+ production deployments

---

## Contribution Guidelines

When contributing to roadmap items:
1. Create feature branch: `feature/roadmap-<phase>-<item>`
2. Update AGENTS.md with implementation details
3. Add tests for new functionality
4. Update CHANGELOG.md with changes
5. Reference roadmap item in PR description

---

*Last updated: 2026-05-10*
