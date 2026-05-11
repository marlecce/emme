# Auto-Docs Skill - Automatic Documentation Updates

## Overview
This skill ensures documentation stays synchronized with code changes by automatically updating:
- ROADMAP.md - Feature completion status
- CHANGELOG.md - User-facing changes
- docs/*.md - Feature documentation
- README.md - Project overview

## Quick Start

After implementing a feature:
```bash
# The skill will automatically:
# 1. Analyze git diff for changed files
# 2. Update ROADMAP.md with completion status
# 3. Add entry to CHANGELOG.md
# 4. Create/update feature documentation
# 5. Refresh README.md
```

## Documentation Hierarchy

```
emme/
├── README.md           # Project overview, quick start
├── CHANGELOG.md        # User-facing changes by version
├── ROADMAP.md          # Planned vs completed features
├── AGENTS.md           # Development guidelines
└── docs/
    ├── FEATURE1.md     # Detailed feature documentation
    ├── FEATURE2.md     # Configuration, monitoring, ops
    └── DEPLOYMENT.md   # Production deployment guide
```

## When to Trigger

### Mandatory (P0/P1 Features)
- New HTTP endpoints
- Configuration changes
- Security features
- Performance optimizations
- Breaking changes

### Recommended (P2 Features)
- Bug fixes with user impact
- Operational improvements
- New metrics or logs

### Optional
- Internal refactoring
- Test additions
- Performance improvements (no behavior change)

## Output Examples

### ROADMAP.md Update
```markdown
### P1: Request Timeout Enforcement ✅ COMPLETED
**Severity**: HIGH | **Impact**: DoS protection via slowloris mitigation

**Implementation** (Completed 2026-05-11):
- [x] Configuration with YAML and env var overrides
- [x] HTTP/1.1 timeout checks with 408 response
- [x] HTTP/2 timeout integration
- [x] TLS handshake timeout
- [x] UUID request correlation IDs
- [x] Metrics counter for timeouts

**Files modified**: 
- `src/server.c` - Timeout checks in HTTP/1.1 & HTTP/2 handlers
- `src/tls.c` - TLS handshake timeout
- `src/metrics.c` - Timeout counter
- `include/config.h` - Timeout configuration fields
- `src/uuid.c` - Fixed UUID v4 generation

**Acceptance criteria** (All Met):
- [x] Zero compiler warnings
- [x] 97/97 tests passing (100%)
- [x] Lock-free implementation (atomic counters)
- [x] <1% performance overhead
- [x] Configurable via YAML and environment variables

**Test results**: 97/97 tests passing (100%)
```

### CHANGELOG.md Update
```markdown
## [Unreleased] - 2026-05-11

### Added
- Request timeout enforcement with configurable timeout (30s default)
- UUID v4 request correlation IDs
- Request timeout metrics (`emme_request_timeouts_total`)
- TLS handshake timeout (10s default)

### Changed
- HTTP 408 Request Timeout response includes `Retry-After: 5` header

### Security
- Mitigated slowloris DoS attacks via request timeout enforcement
```

### Feature Doc (docs/REQUEST_TIMEOUT.md)
```markdown
# Request Timeout Enforcement

## Overview
Prevents DoS attacks via slowloris by enforcing maximum request duration.
Requests exceeding the timeout receive HTTP 408 with Retry-After header.

## Configuration

### YAML
```yaml
server:
  request_timeout_ms: 30000        # Default 30s
  tls_handshake_timeout_ms: 10000  # Default 10s
```

### Environment Variables
- `EMME_REQUEST_TIMEOUT`: Request timeout in seconds (default: 30)
- `EMME_TLS_HANDSHAKE_TIMEOUT`: TLS handshake timeout in seconds (default: 10)

## Behavior

### Timeout Enforcement
- HTTP/1.1: Checked on every SSL_read() call
- HTTP/2: Checked in main event loop
- TLS: Checked before each io_uring poll wait

### Response
```
HTTP/1.1 408 Request Timeout
Content-Length: 0
Retry-After: 5

```

## Monitoring

### Metrics
- `emme_request_timeouts_total` (counter): Total timeout events

### Logs
- `[WARN]` Request timeout: Xms exceeded (limit Yms)
- `[WARN]` TLS handshake timeout: Xms exceeded (limit Yms)

## Testing

### Unit Tests
- `tests/unit/test_timeout.c`: 7 tests
  - HTTP 408 status code defined
  - UUID format validation
  - UUID uniqueness
  - Elapsed time calculation
  - Timeout threshold logic
  - Config range validation

## Operational Notes

### Tuning
- Increase timeout for large file uploads
- Decrease timeout for API endpoints (10-15s)
- Monitor timeout metrics for abuse patterns

### Troubleshooting
**Symptom**: High timeout rate
- Check for slow clients or network issues
- Verify timeout configuration matches workload
- Consider per-route timeouts (P2 feature)
```

## Integration with Workflows

### After Feature Implementation
```bash
# 1. Implement feature
# 2. Run c-code-quality skill
make clean && make && make test

# 3. Run auto-docs skill
# (Updates documentation automatically)

# 4. Commit
git add -A
git commit -m "add request timeout enforcement"
```

### Before Pull Request
```bash
# Verify documentation is current
./scripts/validate-docs.sh

# Check for broken links
markdown-link-check docs/*.md

# Verify examples work
./scripts/test-doc-examples.sh
```

## Quality Checks

### Documentation Review Checklist
- [ ] ROADMAP.md reflects current state
- [ ] CHANGELOG.md has user-facing changes
- [ ] Feature docs exist for P0/P1 features
- [ ] Configuration examples are valid YAML
- [ ] Metrics documented match implementation
- [ ] Log message examples are accurate
- [ ] Troubleshooting section covers common issues
- [ ] All hyperlinks are valid
- [ ] No references to removed features
- [ ] Code examples compile/run

### Automated Validation
```bash
# Check YAML syntax
yamllint config.yaml docs/*.md

# Validate markdown
markdownlint docs/*.md

# Check for TODO/FIXME
grep -r "TODO\|FIXME" docs/

# Verify links
find docs -name "*.md" -exec markdown-link-check {} \;
```

## Metrics

### Documentation Health
- **Coverage**: 100% of P0/P1 features documented
- **Freshness**: <7 days since last update
- **Accuracy**: 0 doc bugs in last sprint
- **Completeness**: All templates filled

## References
- `skills/c-code-quality/skill.md` - Complementary code quality skill
- `AGENTS.md` - Development guidelines
- `docs/` - Existing documentation examples
