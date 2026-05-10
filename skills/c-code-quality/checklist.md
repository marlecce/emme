# C Code Quality Checklist

Use this checklist for every code quality improvement session.

## Pre-Flight Checks

- [ ] Current branch is up to date (`git pull`)
- [ ] All tests currently pass (`make test`)
- [ ] No uncommitted changes (or stashed)
- [ ] Target files identified

## Phase 1: Analysis

### Compiler Warnings
- [ ] `make clean && make 2>&1 | grep -E "(warning|error)"` - zero warnings
- [ ] Check for `-Wdiscarded-qualifiers` (const issues)
- [ ] Check for `-Wunused-parameter` (mark with `(void)param;`)
- [ ] Check for implicit casts, sign comparisons

### Code Metrics
- [ ] Functions >100 lines identified
- [ ] Functions >150 lines flagged as critical
- [ ] Cyclomatic complexity >10 identified
- [ ] Nested conditionals >3 levels identified

### Code Smells
- [ ] Magic numbers (literals >9, excluding 0,1,-1)
- [ ] Repeated code patterns (2+ occurrences)
- [ ] Long parameter lists (>4 parameters)
- [ ] Global mutable state without synchronization
- [ ] Missing error handling on return values

### Documentation
- [ ] Functions without comments (if non-obvious)
- [ ] Missing thread safety documentation
- [ ] Unclear variable/function names
- [ ] Outdated comments

## Phase 2: Prioritization

### High Priority (Must Fix)
- [ ] All compiler warnings resolved
- [ ] Missing error handling added
- [ ] Buffer overflow risks eliminated
- [ ] Resource leaks fixed
- [ ] Thread safety issues addressed

### Medium Priority (Should Fix)
- [ ] Functions >100 lines refactored
- [ ] Magic numbers replaced with constants
- [ ] Code duplication eliminated
- [ ] Error handling patterns standardized
- [ ] Unclear names improved

### Low Priority (Nice to Have)
- [ ] Redundant comments removed
- [ ] Formatting inconsistencies fixed
- [ ] Duplicate includes removed
- [ ] Documentation updated

## Phase 3: Implementation

### For Each Change
- [ ] Change is atomic (single responsibility)
- [ ] No behavior changes (unless intentional)
- [ ] Backward compatible (API unchanged)
- [ ] Follows existing code style
- [ ] 4-space indentation, no tabs
- [ ] `snake_case` for functions/variables
- [ ] `UPPER_CASE` for macros

### Safety Checks
- [ ] No unbounded string operations (`strcpy`, `sprintf`)
- [ ] All allocations have corresponding frees
- [ ] All file descriptors closed in all paths
- [ ] No blocking calls in I/O handlers
- [ ] Explicit bounds checking on arrays

### Error Handling
- [ ] Return values checked (0=success, -1=error)
- [ ] Errors logged with `log_message()`
- [ ] Cleanup in reverse order of allocation
- [ ] `goto` for cleanup (not deep nesting)

## Phase 4: Verification

### Build Verification
- [ ] `make clean && make` - zero warnings
- [ ] `make test` - all tests pass
- [ ] No new memory leaks (valgrind if available)
- [ ] Binary runs correctly (`./emme --help`)

### Code Review Self-Check
- [ ] Would I approve this PR?
- [ ] Is the intent clear from code (not just comments)?
- [ ] Are edge cases handled?
- [ ] Is error handling consistent with surrounding code?
- [ ] Are there any "clever" tricks that should be simpler?

### Git Hygiene
- [ ] Commit message is clear and imperative
- [ ] Commit is atomic (single logical change)
- [ ] No secrets/credentials committed
- [ ] `.gitignore` respected (no build artifacts)

## Quality Gates

### Mandatory (Block Merge)
- ✅ Zero compiler warnings (`-Wall -Wextra`)
- ✅ All tests passing
- ✅ No memory leaks (for memory-affecting changes)
- ✅ No behavior regression

### Target Metrics
- 📊 Function length: <100 lines (average)
- 📊 Cyclomatic complexity: <10 (average)
- 📊 Code duplication: <5%
- 📊 Test coverage: ≥80% line, ≥70% branch (critical modules)

## Common Pitfalls

### ❌ Don't Do This
```c
// Mixing error reporting styles
fprintf(stderr, "Error");  // Before logging init
log_message(...);         // After logging init
fprintf(stderr, "Warn");  // Inconsistent!

// Ignoring return values
snprintf(buf, size, "...");  // May truncate!
write(fd, buf, len);         // May write partial!

// Magic numbers
if (port > 0 && port <= 65535)  // What is 65535?

// Deep nesting
if (cond1) {
    if (cond2) {
        if (cond3) {
            // ... 5 levels deep
        }
    }
}
```

### ✅ Do This Instead
```c
// Consistent logging
if (!logging_initialized) {
    fprintf(stderr, "Error\n");
} else {
    log_message(LOG_LEVEL_ERROR, "...");
}

// Validate return values
int len = snprintf(buf, size, "...");
if (len < 0 || (size_t)len >= size) {
    return -1;
}

// Named constants
#define MAX_PORT_NUMBER 65535
if (port > 0 && port <= MAX_PORT_NUMBER)

// Early returns
if (!cond1) return -1;
if (!cond2) return -1;
if (!cond3) return -1;
// Main logic at base indentation
```

## Sign-Off

- [ ] All applicable checklist items completed
- [ ] Verification passed
- [ ] Ready for review/merge

---

**Note:** Not all items apply to every change. Use judgment to determine which are relevant.
