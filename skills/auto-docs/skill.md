# Automatic Documentation Update Skill

## Role
Act as a **technical documentation specialist** with deep understanding of C systems programming, ensuring all code changes are accurately reflected in project documentation.

## Mission
Maintain living documentation that:
- Accurately reflects current implementation state
- Tracks completed vs. planned features
- Provides operational runbooks for on-call engineers
- Documents API contracts and configuration schemas
- Remains synchronized with code changes

## When to Use
**MANDATORY** after:
- New feature implementation (P0, P1, P2)
- Configuration schema changes
- API endpoint additions/modifications
- Performance optimizations with behavioral impact
- Security hardening changes
- Bug fixes that change error handling

**OPTIONAL** after:
- Refactoring (if architecture changes)
- Test additions (if they document edge cases)
- Performance improvements (update benchmarks)

## Core Workflow

### Phase 1: Identify Documentation Impact
```bash
# 1. What files were modified?
git diff --name-only HEAD~1

# 2. What features were added?
git log -1 --pretty=format:"%s"

# 3. What configuration changed?
git diff HEAD~1 config.yaml include/config.h src/config.c

# 4. What APIs changed?
git diff HEAD~1 include/*.h
```

### Phase 2: Update ROADMAP.md

**For Completed Features:**
1. Find the feature in ROADMAP.md
2. Mark checkboxes as `[x]`
3. Add implementation date
4. List files modified
5. Document acceptance criteria met
6. Add test results (X/Y tests passing)

**Example Update:**
```markdown
### P1: Request Timeout Enforcement ✅ COMPLETED
**Severity**: HIGH | **Impact**: DoS protection via slowloris mitigation

**Implementation** (Completed YYYY-MM-DD):
- [x] Feature 1 implemented
- [x] Feature 2 implemented

**Files modified**: `src/file1.c`, `include/header.h`

**Acceptance criteria** (All Met):
- [x] Criterion 1
- [x] Criterion 2

**Test results**: X/Y tests passing (Z%)
```

### Phase 3: Update CHANGELOG.md

**Format:**
```markdown
## [Unreleased] - YYYY-MM-DD

### Added
- Feature description (PR #XXX, resolves #YYY)

### Changed
- Configuration key now accepts range [min, max]
- Default value changed from X to Y

### Fixed
- Bug description (resolves #ZZZ)

### Security
- Vulnerability mitigation description
```

### Phase 4: Update Feature Documentation

**For new features, create/update `docs/FEATURE_NAME.md`:**

```markdown
# Feature Name

## Overview
One-paragraph description of what the feature does and why it exists.

## Configuration

### YAML Configuration
```yaml
section:
  key: value  # Default: X, Range: [min, max]
```

### Environment Variables
- `ENV_VAR_NAME`: Description (default: value)

## Behavior

### Normal Operation
Describe what happens during normal operation.

### Error Handling
Describe error conditions and recovery.

### Performance Impact
- Memory overhead: X bytes per connection
- CPU overhead: <1% in benchmarks
- Latency impact: p50/p95/p99 unchanged

## Monitoring

### Metrics
- `metric_name`: Description (type: counter/gauge/histogram)

### Logs
- `[LOG_LEVEL]` Message template

## Testing

### Unit Tests
- `tests/unit/test_feature.c`: Test descriptions

### Integration Tests
- `tests/integration/test_feature.c`: Test descriptions

## Operational Notes

### Troubleshooting
Common issues and how to resolve them.

### Tuning
Recommended configuration for different workloads.
```

### Phase 5: Update README.md

**Sections to review:**
1. **Features list** - Add new capabilities
2. **Configuration example** - Update `config.yaml` snippet
3. **Metrics** - Add new metrics to table
4. **Quick start** - Verify commands still work
5. **Requirements** - Update dependencies if needed

### Phase 6: Update AGENTS.md (if needed)

**Update if:**
- New coding conventions established
- New critical modules added
- Testing requirements changed
- Security guidelines updated

### Phase 7: Verification

**Checklist:**
- [ ] ROADMAP.md reflects current state
- [ ] CHANGELOG.md documents user-facing changes
- [ ] Feature docs exist for P0/P1 features
- [ ] README.md examples are current
- [ ] Configuration docs match `config.yaml`
- [ ] Metrics docs match `src/metrics.c`
- [ ] All links in docs are valid
- [ ] No references to removed features

## Documentation Templates

### Template 1: ROADMAP.md Feature Entry
```markdown
### P{N}: Feature Name {STATUS}
**Severity**: {LEVEL} | **Impact**: {ONE_SENTENCE}

**Implementation** ({STATUS}):
- [ ] Sub-task 1
- [ ] Sub-task 2

**Files modified**: `path/to/file.c`

**Acceptance criteria** ({STATUS}):
- [ ] Criterion 1
- [ ] Criterion 2

**Test results**: X/Y tests passing
```

### Template 2: Feature Documentation
```markdown
# {Feature Name}

## Overview
{Description}

## Configuration
### YAML
```yaml
{example}
```

### Environment Variables
- `{VAR}`: Description

## Behavior
{Description}

## Monitoring
### Metrics
- `{metric_name}`: Description

### Logs
- `[{LEVEL}]` Message

## Testing
{Test files and coverage}

## Operations
{Troubleshooting and tuning}
```

### Template 3: CHANGELOG.md Entry
```markdown
## [Unreleased] - YYYY-MM-DD

### Added
- {Feature} ({contributor})

### Changed
- {Change} ({reason})

### Fixed
- {Bug} ({issue_number})

### Security
- {Vulnerability} ({severity})
```

## Documentation Standards

### Writing Style
- **Imperative mood**: "Configure the timeout" not "The timeout should be configured"
- **Active voice**: "The server validates" not "Validation is performed by the server"
- **Concise**: One idea per sentence, one topic per paragraph
- **Technical precision**: Use exact terms (connection vs session vs stream)

### Code Examples
- Show complete, working examples
- Include error handling
- Use realistic values (not foo/bar)
- Annotate with comments for non-obvious parts

### Versioning
- Reference specific versions when behavior changed
- Use "Since v1.2.0" for new features
- Mark deprecated features with ⚠️

### Cross-References
- Link to related features
- Reference configuration keys
- Link to external docs (OpenSSL, nghttp2) when relevant

## Common Documentation Smells

| Smell | Indicator | Fix |
|-------|-----------|-----|
| Outdated screenshot | UI/config doesn't match | Replace with current |
| Broken link | 404 on reference | Update or remove |
| Vague instruction | "Configure appropriately" | Provide specific guidance |
| Missing context | Jumps into details | Add overview section |
| No examples | Only API signatures | Add usage examples |
| Stale roadmap | Completed features not marked | Update status |

## Integration Points

### With c-code-quality Skill
- Run after code quality improvements
- Document refactored functions if API changed
- Update CODE_QUALITY.md with new patterns

### With Testing
- Document test coverage for new features
- Add troubleshooting section based on test failures
- Update operational runbooks with test scenarios

### With CI/CD
- Ensure docs build in CI (if using static site generator)
- Validate links in documentation
- Check for TODO/FIXME references

## Metrics to Track

### Documentation Health
- **Coverage**: % of features with dedicated docs
- **Freshness**: Days since last update per doc
- **Accuracy**: Doc bugs reported vs resolved
- **Completeness**: Checklist completion rate

### Usage Metrics (if available)
- Most viewed docs
- Search queries with no results
- Time on page (engagement)

## Examples from This Codebase

### Good Example: GRACEFUL_SHUTDOWN.md
- Clear overview
- Configuration examples
- State machine diagram
- Troubleshooting section
- Test results

### Good Example: METRICS.md
- Complete metric list
- Type descriptions
- Example queries
- Alert thresholds

## Automation Opportunities

### Pre-commit Checks
```bash
# Check for TODO references
grep -r "TODO\|FIXME" docs/

# Validate YAML syntax
yamllint config.yaml

# Check markdown links
markdown-link-check README.md
```

### Post-merge Updates
```bash
# Auto-extract metrics from source
grep -h "metrics_.*\.name" src/*.c | sort -u

# Auto-extract config keys
grep -h "PARS" src/config.c | grep -o '"[^"]*"' | sort -u
```

## Usage Example

```markdown
/update-documentation

Feature: Request timeout enforcement
Files changed: src/server.c, include/config.h, src/metrics.c
Priority: P1

Update:
1. ROADMAP.md - mark P1 Request Timeout as completed
2. CHANGELOG.md - add timeout feature
3. docs/REQUEST_TIMEOUT.md - create new feature doc
4. README.md - update features list
```

## Limitations

- Does not write marketing material
- Does not create tutorials (unless explicitly requested)
- Does not translate documentation
- Does not update external dependencies' docs

## References

- `ROADMAP.md` - Feature tracking
- `CHANGELOG.md` - User-facing changes
- `docs/` - Feature documentation
- `README.md` - Project overview
- `AGENTS.md` - Development guidelines
