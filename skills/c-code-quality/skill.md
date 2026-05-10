# C Code Quality Improvement Skill

## Role
Act as a **senior C expert engineer** specializing in code quality, refactoring, and maintainability improvements while preserving correctness and performance.

## Mission
Systematically improve C codebase quality through targeted refactoring that:
- Eliminates compiler warnings (`-Wall -Wextra -std=c11` clean)
- Reduces code duplication and complexity
- Improves readability and maintainability
- Preserves or enhances performance
- Maintains backward compatibility

## When to Use
- After feature implementation (cleanup pass)
- Before major merges (quality gate)
- During technical debt reduction sprints
- When onboarding new contributors (codebase familiarization)

## Core Workflow

### Phase 1: Analysis
```bash
# 1. Build with strict warnings
make clean && make 2>&1 | grep -E "(warning|error)"

# 2. Identify long functions (>100 lines)
awk '/^[a-zA-Z]/ {func=$0} /}/ {if (NR-start > 100) print func, NR-start}' src/*.c

# 3. Find magic numbers (excluding 0, 1, -1)
grep -nE '[0-9]{2,}' src/*.c | grep -v '#define' | grep -v '//\|/\*'

# 4. Detect code duplication
# Look for repeated patterns across files
```

### Phase 2: Prioritization

**High Priority (Correctness/Safety):**
- Compiler warnings (type mismatches, discarded qualifiers)
- Missing error handling (unchecked return values)
- Resource leaks (memory, file descriptors)
- Buffer overflow risks (unbounded string operations)
- Thread safety issues (missing synchronization)

**Medium Priority (Code Quality):**
- Functions >100 lines (target: <50 lines)
- Magic numbers without named constants
- Code duplication (copy-paste patterns)
- Inconsistent error handling patterns
- Unclear variable/function names

**Low Priority (Readability):**
- Missing or unclear comments
- Inconsistent formatting
- Redundant includes
- Outdated documentation

### Phase 3: Refactoring Patterns

#### Pattern 1: Extract Helper Function
**Before:**
```c
void process_data(void) {
    // 20 lines of validation
    if (!data) return;
    if (data->size == 0) return;
    // ... more validation ...
    
    // 30 lines of processing
    for (...) { ... }
    
    // 20 lines of cleanup
    free(buffer);
    close(fd);
}
```

**After:**
```c
static int validate_data(const Data *data);
static int process_data_impl(Data *data);
static void cleanup_resources(int fd, char *buffer);

void process_data(void) {
    if (validate_data(data) != 0) return;
    if (process_data_impl(data) != 0) return;
    cleanup_resources(fd, buffer);
}
```

#### Pattern 2: Named Constants
**Before:**
```c
char buffer[65536];
if (listen(fd, 10) < 0) ...
timeout.tv_sec = 30;
```

**After:**
```c
#define BUFFER_SIZE 65536
#define SERVER_BACKLOG 10
#define TIMEOUT_SECONDS 30

char buffer[BUFFER_SIZE];
if (listen(fd, SERVER_BACKLOG) < 0) ...
timeout.tv_sec = TIMEOUT_SECONDS;
```

#### Pattern 3: Error Handling Consolidation
**Before:**
```c
if (step1() < 0) {
    log_error("step1 failed");
    cleanup_a();
    return -1;
}
if (step2() < 0) {
    log_error("step2 failed");
    cleanup_a();
    cleanup_b();
    return -1;
}
```

**After:**
```c
int ret = -1;

if (step1() < 0) {
    log_error("step1 failed");
    goto err_cleanup_a;
}
if (step2() < 0) {
    log_error("step2 failed");
    goto err_cleanup_b;
}
ret = 0;
goto done;

err_cleanup_b:
    cleanup_b();
err_cleanup_a:
    cleanup_a();
done:
    return ret;
```

#### Pattern 4: snprintf Validation
**Before:**
```c
int len = snprintf(buf, size, "format", args);
strcpy(dest, buf);  // May overflow if len >= size
```

**After:**
```c
int len = snprintf(buf, size, "format", args);
if (len < 0 || (size_t)len >= size) {
    log_error("Buffer too small");
    return -1;
}
strcpy(dest, buf);  // Now safe
```

#### Pattern 5: Remove Duplication
**Before:**
```c
void init_histogram_a(void) {
    for (int i = 0; i < BUCKETS; i++) {
        hist_a.buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist_a.lock, NULL);
}

void init_histogram_b(void) {
    for (int i = 0; i < BUCKETS; i++) {
        hist_b.buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist_b.lock, NULL);
}
```

**After:**
```c
static void init_histogram(MetricHistogram *hist) {
    for (int i = 0; i < BUCKETS; i++) {
        hist->buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist->lock, NULL);
}

void init_histogram_a(void) {
    init_histogram(&hist_a);
}

void init_histogram_b(void) {
    init_histogram(&hist_b);
}
```

### Phase 4: Verification

**Mandatory Checks:**
```bash
# 1. Zero warnings
make clean && make 2>&1 | grep -E "(warning|error)"
# Expected: (no output)

# 2. All tests pass
make test
# Expected: "Passing: X | Failing: 0"

# 3. No new memory leaks (if valgrind available)
valgrind --leak-check=full ./emme
# Expected: "All heap blocks were freed"

# 4. Performance regression check (for hot paths)
# Compare before/after with same load
```

**Quality Metrics:**
- Function length: Target <100 lines, flag >150 lines
- Cyclomatic complexity: Target <10, flag >15
- Code duplication: <5% repeated code
- Test coverage: ≥80% line, ≥70% branch (critical modules)

## C-Specific Guidelines

### Memory Safety
- **Forbidden**: `strcpy`, `sprintf`, `gets`, `scanf` without width
- **Required**: `snprintf`, `strncpy` + null termination, explicit length parameters
- **Check**: All allocations have corresponding frees in all paths

### Thread Safety
- Document which functions are thread-safe
- Use `_Atomic` for lock-free counters
- Use `pthread_mutex_t` for complex state
- Never hold locks across I/O operations

### Error Handling
- Return `0` for success, `-1` for error (or specific error codes)
- Log errors at appropriate level (`LOG_LEVEL_ERROR`)
- Clean up resources in reverse order of allocation
- Use `goto` for cleanup (not deep nesting)

### Naming Conventions
- Functions/variables: `snake_case`
- Macros/constants: `UPPER_CASE`
- Globals: `g_` prefix or `static` at file scope
- Types: `typedef struct foo foo_t`

### Headers
- Guards required: `#ifndef MODULE_H`
- Minimal includes (forward declare when possible)
- Document ownership (who frees returned pointers?)
- Thread safety guarantees in comments

## Common Code Smells

| Smell | Indicator | Fix |
|-------|-----------|-----|
| Long function | >100 lines | Extract helper functions |
| Magic number | Literal >9 (not 0,1,-1) | Named constant |
| Duplication | Same pattern 2+ times | Extract to function |
| Deep nesting | >3 levels of if/for | Early returns, extract |
| Large struct | >8 fields | Group related fields |
| Global mutable | Non-const global | Encapsulate with accessors |
| Ignored return | Function without check | Add error handling |

## Examples from This Codebase

See `examples/` directory for before/after comparisons:
- `metrics-refactor.example` - Extract formatting helpers
- `main-cleanup.example` - Named constants, logging consistency
- `router-cleanup.example` - Remove duplicate includes

## Integration with AGENTS.md

This skill complements the repository guidelines:
- Follows coding rules from AGENTS.md (C11, 4-space indent, no tabs)
- Enforces safety requirements (bounded APIs, explicit ownership)
- Aligns with quality gates (zero warnings, all tests pass)
- Supports critical path requirements (resource cleanup, state machine correctness)

## Usage Example

```markdown
/implement code-quality-improvement

Target files: src/metrics.c, src/main.c
Focus areas: High priority (warnings), Medium priority (duplication)

Workflow:
1. Analyze files for warnings and duplication
2. Propose refactoring plan
3. Apply changes incrementally
4. Verify with `make && make test`
```

## Limitations

- Does not change public API without explicit request
- Does not optimize for performance without benchmarks
- Does not add new features (focus: quality only)
- Does not modify tests except to fix warnings

## References

- `AGENTS.md` - Repository coding standards
- `include/metrics.h` - Example of clean header structure
- `src/log.c` - Example of consistent error handling
- Linux kernel coding style (for general C conventions)
