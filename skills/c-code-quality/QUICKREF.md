# C Code Quality - Quick Reference Card

## One-Liner Commands

```bash
# Check for warnings
make clean && make 2>&1 | grep -E "(warning|error)"

# Run all tests
make test

# Find long functions (>100 lines)
awk 'BEGIN{RS="^[a-zA-Z].*\\("} NF{print NR": "$0}' src/*.c | head -20

# Find magic numbers
grep -nE '[0-9]{2,}' src/*.c | grep -v '#define' | head -20

# Check for duplicate includes
for f in src/*.c; do grep '^#include' "$f" | sort | uniq -d; done
```

## Priority Order

1. **HIGH** → Compiler warnings, memory leaks, missing error handling
2. **MEDIUM** → Functions >100 lines, magic numbers, duplication
3. **LOW** → Comments, formatting, naming

## Patterns at a Glance

| Problem | Pattern | Example |
|---------|---------|---------|
| Long function | Extract Helper | `metrics_format_prometheus()` → 3 helpers |
| Magic numbers | Named Constants | `65536` → `METRICS_BUFFER_SIZE` |
| Repeated cleanup | Goto Cleanup | Multiple `free()` paths → single label |
| Buffer truncation | Validate snprintf | Check return value |
| Copy-paste | Remove Duplication | Extract common function |
| Deep nesting | Early Returns | Guard clauses |

## Quality Gates

```bash
# MUST PASS before merge
make clean && make 2>&1 | grep warning  # (no output)
make test                                # 100% passing
```

## Common Constants

```c
#define BUFFER_SIZE 65536
#define MAX_PORT_NUMBER 65535
#define SERVER_BACKLOG 10
#define TIMEOUT_SECONDS 30
#define THREAD_POOL_MIN_THREADS 32
```

## Error Handling Template

```c
int function(void)
{
    int ret = -1;
    
    if (step1() < 0) goto err_exit;
    if (step2() < 0) goto err_cleanup1;
    if (step3() < 0) goto err_cleanup2;
    
    ret = 0;
    goto done;

err_cleanup2: cleanup2();
err_cleanup1: cleanup1();
err_exit:
done:
    return ret;
}
```

## snprintf Validation

```c
int len = snprintf(buf, size, "format", args);
if (len < 0 || (size_t)len >= size) {
    log_error("Buffer too small");
    return -1;
}
```

## Naming Conventions

- Functions/variables: `snake_case`
- Macros/constants: `UPPER_CASE`
- Globals: `g_` prefix or `static`
- Types: `typedef struct foo foo_t`

## Forbidden Functions

```c
// NEVER use (use safe alternatives)
strcpy   → strncpy + null termination
sprintf  → snprintf
gets     → fgets
scanf    → scanf with width specifier
```

## File Structure

```
skills/c-code-quality/
├── README.md          # Start here
├── skill.md           # Full workflow
├── checklist.md       # Quality gates
├── patterns.md        # Refactoring techniques
└── examples/          # Real examples
```

## Usage

```bash
# Load skill
/skill c-code-quality

# Typical session
1. Read README.md (5 min)
2. Run analysis commands (10 min)
3. Apply patterns (varies)
4. Verify with checklist (10 min)
```

## Success Metrics

- ✅ Zero warnings
- ✅ All tests pass
- ✅ Code is clearer
- ✅ No behavior changes

## Files Modified (This Codebase)

- `src/metrics.c` - Extracted helpers, added validation
- `src/main.c` - Named constants, logging consistency
- `src/router.c` - Removed duplicate includes

**Result**: 76/76 tests passing, zero warnings
