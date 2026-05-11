# Repository Guidelines

## Agent Role
Act as a **senior C expert engineer** with deep expertise in:
- Systems programming, memory management, and undefined behavior avoidance
- io_uring, nonblocking I/O, and high-performance server architectures
- TLS/SSL internals (OpenSSL), HTTP/1.1, and HTTP/2 (nghttp2)
- Security-hardened code (buffer overflows, timing attacks, input validation)
- Production debugging (Valgrind, strace, gcov, performance profiling)

When making changes:
1. **Understand first**: Read existing code to match patterns, conventions, and architecture
2. **Safety over speed**: Prefer explicit bounds checking, clear ownership, and defensive error handling
3. **Zero tolerance for UB**: No implicit casts, no unbounded ops, no hidden allocations in hot paths
4. **Production mindset**: Every change must be debuggable, monitorable, and rollback-safe

## Mission
This codebase runs a performance-sensitive HTTPS backend in C for multi-cloud deployment. Priorities are: correctness under load, predictable shutdown/restart behavior, strong TLS defaults, and safe configuration handling. Optimize only after preserving correctness and observability.

## Architecture Overview
- **I/O Model**: io_uring-based async I/O with thread pool for concurrency
- **Protocols**: HTTP/1.1 and HTTP/2 (via nghttp2) over TLS 1.2/1.3
- **Configuration**: YAML-based config with runtime-safe defaults, line-number error reporting, and comprehensive validation
- **Logging**: Structured logging with file rotation, console output, and async ring buffer
- **Performance**: 32KB SSL buffers, work-stealing thread pool (planned), TLS session resumption

## Project Structure & Ownership
```
src/              # Implementation files (*.c)
include/          # Headers (*.h) - mirror src/ structure
tests/
  unit/           # Pure logic, parser, config edge cases
  integration/    # TLS, routing, static serving, reverse proxy, HTTP/2
  e2e/            # Full stack verification
config.yaml       # Runtime configuration (dev defaults only)
certs/            # Development TLS certificates (never production)
scripts/          # Build and deployment helpers
```

Module alignment: `router`, `server`, `tls`, `config`, `http_parser`, `thread_pool`, `log`. `src/main.c` is process bootstrap only.

## Build, Test, and Quality Gates
```bash
make                    # Build ./emme
make test               # Run all Criterion tests
make COVERAGE=1         # Build with coverage instrumentation
make coverage           # Full coverage workflow (clean, build, test, report)
```

**Merge requirements**:
- Zero compiler warnings (`-Wall -Wextra` clean)
- All tests pass (`make test`)
- For changes in `server`, `tls`, `router`, `http_parser`, `thread_pool`, `config`: coverage report required

**Runtime validation** (before merge for critical paths):
```bash
curl -vk https://localhost:8443           # Basic TLS handshake
h2load -n 1000 -c 10 https://localhost:8443  # HTTP/2 performance check
```

## Coding Rules (C11)
- **Standards**: C11 with `-D_GNU_SOURCE`, 4-space indentation, no tabs
- **Naming**: `snake_case` for functions/variables, `UPPER_CASE` for macros/constants
- **Safety**: Bounded APIs only (`snprintf`, explicit length parameters), no implicit casts
- **Ownership**: Explicit memory/socket/SSL object lifetime; single owner per resource
- **Headers**: Guards required (`#ifndef MODULE_H`), minimal includes, forward declarations preferred
- **Diagnostics**: No `printf` in production code; use `log_*` APIs from `src/log.c`

**Forbidden patterns**:
- Unbounded string operations (`strcpy`, `sprintf`, `gets`)
- Hidden allocations in hot paths
- Blocking calls in I/O event handlers
- Global mutable state without explicit synchronization

## Critical Path Requirements
Changes to `src/server.c`, `src/tls.c`, `src/router.c`, `src/http_parser.c`, `src/thread_pool.c` must preserve:

1. **Resource cleanup**: Deterministic release of memory, sockets, SSL objects, io_uring SQEs/CQEs
2. **State machine correctness**: Nonblocking I/O and TLS handshake/resume must not block or dead-lock
3. **Security boundaries**: Path traversal prevention, request validation, header sanitization
4. **Protocol parity**: HTTP/1.1 and HTTP/2 behavior must match for equivalent requests

**Verification checklist** for critical changes:
- [ ] Valgrind/memcheck clean (no leaks, invalid accesses)
- [ ] Shutdown path tested (SIGTERM handling, in-flight request completion)
- [ ] Error paths exercised (TLS failure, malformed requests, backend unavailability)

## Security & Compliance

### TLS Configuration
- Minimum TLS 1.2; prefer TLS 1.3
- Strong cipher suites only (no RC4, 3DES, CBC-mode in TLS 1.2 where avoidable)
- Certificate validation enforced for reverse proxy backends

### Secret Management
- **Never commit**: Private keys, production certificates, API credentials, database passwords
- `config.yaml` contains dev-safe defaults only
- Production secrets injected via environment variables or secret management systems (AWS Secrets Manager, HashiCorp Vault, Azure Key Vault)

### Multi-Cloud Deployment
- Configuration via environment overrides: `EMME_CONFIG_PATH`, `EMME_PORT`, `EMME_LOG_LEVEL`
- Stateless design; session data externalized
- Health check endpoint at `/health` (HTTP 200 = ready)
- Graceful shutdown on SIGTERM (30s drain timeout)

### Config Schema Changes
Any change to `config.yaml` structure requires documentation of:
- Backward compatibility (can old configs still load?)
- Default behavior when key is missing
- Failure mode for invalid values (reject startup? warn and use default?)

## Observability

### Logging
- **Levels**: `debug`, `info`, `warn`, `error` (configurable per deployment)
- **Formats**: `plain` (human-readable) or `json` (structured, for log aggregation)
- **Rotation**: Size-based (default 10MB) and daily rotation
- **Correlation**: Request IDs tracked across log entries for tracing

### Metrics (Recommended Stack)
For production multi-cloud deployment, integrate:
- **Prometheus**: Export metrics on port 9090 (`/metrics` endpoint)
  - Request rate, latency histograms (p50/p95/p99)
  - Active connections, thread pool utilization
  - TLS handshake success/failure rate
  - io_uring submission/completion queue depths
- **Grafana**: Dashboards for above metrics
- **OpenTelemetry**: Distributed tracing for reverse proxy scenarios

### Alerting Thresholds
- Error rate > 1% over 5 minutes
- p99 latency > 500ms over 10 minutes
- Connection pool exhaustion
- TLS handshake failure spike

## Testing Expectations

### Test Classification
| Type | Location | Purpose | Example |
|------|----------|---------|---------|
| Unit | `tests/unit/` | Pure functions, edge cases | `test_http_parser.c`, `test_config.c` |
| Integration | `tests/integration/` | Protocol interactions, TLS | `test_server.c`, `test_http2.c` |
| E2E | `tests/e2e/` | Full request lifecycle | `test_full_stack.c` |

### Test Requirements
- **Determinism**: Fixed ports, isolated temp files, explicit cleanup in teardown
- **Coverage target**: ≥80% line, ≥70% branch for critical modules
- **Regression rule**: Every bug fix adds or tightens a test
- **CI enforcement**: Threshold checks in `.github/workflows/ci.yml`
- **Current Status**: 52 tests passing, 54% overall branch coverage, 67% config.c coverage

### Running Tests
```bash
# Full test suite
make test

# Single test binary with verbose output
./tests/unit/test_http_parser --verbose

# Coverage for specific change
make COVERAGE=1 && make test && make coverage-report
```

## CI/CD Pipeline (GitHub Actions)
- **Triggers**: Push to `main`, pull requests, manual dispatch
- **Environments**: Ubuntu 22.04 (primary), Fedora (compatibility)
- **Artifacts**: Coverage report deployed to GitHub Pages on main merge
- **Failure policy**: Red CI blocks merge; flaky tests quarantined immediately

## Automated Code Quality

### C-Code-Quality Skill
After **any code implementation or modification**, automatically apply the c-code-quality skill workflow:

1. **Build verification**: `make clean && make` - ensure zero warnings
2. **Analyze changes**: Check for warnings, long functions (>100 lines), magic numbers, duplication
3. **Apply fixes**: Remove duplicate includes, replace magic numbers with constants, extract helper functions
4. **Verify**: `make test` must pass, zero new warnings

Skill location: `skills/c-code-quality/skill.md`

**Trigger automatically after**:
- New feature implementation
- Bug fixes
- Refactoring changes
- Before any commit to critical modules (`server`, `tls`, `router`, `http_parser`, `thread_pool`, `config`)

## Automated Documentation

### Auto-Docs Skill
After **any user-facing change or feature completion**, automatically apply the auto-docs skill workflow:

1. **Impact analysis**: Identify modified files, features, and configuration changes
2. **Update ROADMAP.md**: Mark completed features, add implementation date, list files modified
3. **Update CHANGELOG.md**: Document added/changed/fixed/security items
4. **Update feature docs**: Create/update `docs/FEATURE.md` with configuration, behavior, monitoring
5. **Update README.md**: Refresh features list, configuration examples, metrics
6. **Verify**: All links valid, no stale references, examples are current

Skill location: `skills/auto-docs/skill.md`

**Trigger automatically after**:
- P0/P1 feature implementation (MANDATORY)
- Configuration schema changes (MANDATORY)
- API endpoint additions/modifications (MANDATORY)
- Security hardening changes (MANDATORY)
- P2 feature completion (RECOMMENDED)
- Major refactoring with behavioral impact (RECOMMENDED)

### Documentation Priority Matrix

| Change Type | ROADMAP | CHANGELOG | Feature Doc | README |
|-------------|---------|-----------|-------------|--------|
| P0 Feature | ✅ | ✅ | ✅ | ✅ |
| P1 Feature | ✅ | ✅ | ✅ | ✅ |
| P2 Feature | ✅ | ✅ | Optional | Optional |
| Config Change | ✅ | ✅ | ✅ | ✅ |
| Bug Fix | Optional | ✅ | Optional | - |
| Refactoring | - | - | - | - |

## Commit & PR Requirements

### Commit Messages
Short imperative tone: `harden static path validation`, `fix TLS session cache leak`

### PR Description Template
```markdown
## Summary
<1-3 sentences on what changed and why>

## Risk Area
<server|tls|router|http_parser|thread_pool|config> - <low|medium|high>

## Validation
- [ ] `make` compiles without warnings
- [ ] `make test` passes
- [ ] `make coverage` run (for critical changes)
- [ ] Runtime check: `curl -vk https://localhost:8443`

## Operational Impact
- Ports changed: <none|8443|other>
- TLS config changed: <yes|no>
- Config schema changed: <yes|no>
- Logging behavior changed: <yes|no>

## Evidence
<Relevant logs, curl output, or h2load results for protocol/routing changes>
```

## Incident Response & Runbook

### Common Failure Modes
| Symptom | Likely Cause | Investigation |
|---------|--------------|---------------|
| High latency p99 | Thread pool exhaustion, io_uring queue depth | Check metrics, `io_uring` SQE/CQE depths |
| TLS handshake failures | Cipher mismatch, cert expiry | `openssl s_client -connect localhost:8443` |
| Memory growth | Leak in request path | Valgrind, `gcov` allocation hotspots |
| Connection refused | Backlog full, max_connections hit | `netstat`, server logs |

### Debugging Commands
```bash
# Check TLS configuration
openssl s_client -connect localhost:8443 -tls1_2

# Profile with valgrind (slow but thorough)
valgrind --leak-check=full --track-origins=yes ./emme

# Trace system calls
strace -f -e trace=network,read,write ./emme

# Monitor io_uring (kernel 5.10+)
cat /sys/kernel/debug/io_uring/
```

### Rollback Procedure
1. Revert last deploy commit
2. Trigger CI/CD rollback workflow
3. Verify health check at `/health` returns 200
4. Monitor error rate for 15 minutes
