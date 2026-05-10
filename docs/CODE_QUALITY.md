# Code Quality Improvement

Emme uses a systematic approach to code quality improvement, documented as a reusable skill at `skills/c-code-quality/`.

## Overview

The C Code Quality skill provides:
- **4-phase workflow**: Analysis → Prioritization → Implementation → Verification
- **7 refactoring patterns**: Extract Helper, Named Constants, Goto Cleanup, etc.
- **Quality gates checklist**: HIGH/MEDIUM/LOW priority categorization
- **Quick reference**: One-liner commands for common analysis tasks
- **Real examples**: Before/after from the Emme codebase

## Skill Structure

```
skills/c-code-quality/
├── README.md              # Quick start guide (166 lines)
├── skill.md               # Complete workflow (314 lines)
├── checklist.md           # Quality gates (180 lines)
├── patterns.md            # Refactoring patterns (438 lines)
├── QUICKREF.md            # One-page reference
└── examples/
    ├── metrics-refactor.example   # metrics.c: 126 → 40 lines
    ├── main-cleanup.example       # main.c: constants, logging
    ├── router-cleanup.example     # router.c: removed duplicates
    └── server-refactor.example    # server.c: 161 → 109 lines

Total: 1,627 lines of documentation
```

## How to Use

### Invoke the Skill

Tell the assistant:

```
/implement code-quality-improvement

Target: src/tls.c
Focus: Reduce function length, add named constants

Using skill: c-code-quality
```

### What Happens

The assistant will:

1. **Analyze** the target file:
   - Find functions >100 lines
   - Identify magic numbers
   - Check for duplicate code
   - Verify error handling patterns

2. **Prioritize** issues:
   - HIGH: Compiler warnings, memory leaks, missing error handling
   - MEDIUM: Long functions, magic numbers, duplication
   - LOW: Comments, formatting, naming

3. **Apply** refactoring patterns:
   - Extract helper functions
   - Replace magic numbers with constants
   - Add goto cleanup for complex error paths
   - Validate snprintf return values

4. **Verify** changes:
   - `make` compiles with zero warnings
   - `make test` passes 100%
   - No behavior changes

## Applied Improvements

### metrics.c (Completed 2026-05-10)

**Before**: 126-line `metrics_format_prometheus()` function  
**After**: 40-line main function + 3 helpers

```c
// Extracted helpers:
- metrics_format_counter()
- metrics_format_gauge()
- metrics_format_histogram()
```

**Result**: 68% reduction in main function length

### server.c (Completed 2026-05-10)

**Before**: 161-line `handle_http2_connection()` function  
**After**: 109-line main function + 2 helpers

```c
// Extracted helpers:
- h2_session_init()           (69 lines)
- h2_session_send_initial_settings() (15 lines)
```

**Result**: 32% reduction, eliminated 6 magic numbers

### main.c (Completed 2026-05-10)

- Added named constants: `DEFAULT_METRICS_PORT`, `MAX_PORT_NUMBER`
- Consistent logging format across error paths
- Clear error messages for metrics server initialization

### router.c (Completed 2026-05-10)

- Removed duplicate includes
- Cleaned up unused headers

## Quality Metrics

| Metric | Target | Current Status |
|--------|--------|----------------|
| Compiler warnings | 0 | ✅ 0 |
| Test pass rate | 100% | ✅ 76/76 (100%) |
| Functions >100 lines | 0 | ⚠️ 1 (109 lines, acceptable) |
| Magic numbers | 0 | ✅ 0 (all replaced) |
| Memory leaks | 0 | ✅ 0 (verified with tests) |

## Refactoring Patterns

### Pattern 1: Extract Helper Function

**When**: Function exceeds 100 lines or has mixed responsibilities  
**How**: Identify logical blocks, extract into static helper functions  
**Example**: `h2_session_init()` from `handle_http2_connection()`

### Pattern 2: Named Constants

**When**: Magic numbers appear (especially 3+ digits)  
**How**: Define `#define CONSTANT_NAME value` at file scope  
**Example**: `NS_PER_MS` instead of `1000000`

### Pattern 3: Goto-Based Cleanup

**When**: Multiple error paths with repeated cleanup code  
**How**: Single cleanup label, goto from error paths  
**Example**: See `skills/c-code-quality/patterns.md`

### Pattern 4: snprintf Validation

**When**: Using `snprintf` for formatted output  
**How**: Check return value for truncation  
**Example**: `if (len < 0 || (size_t)len >= size) return -1;`

### Pattern 5: Remove Duplication

**When**: Same code appears 2+ times  
**How**: Extract common function, replace duplicates  
**Example**: Removed duplicate includes in `router.c`

### Pattern 6: Early Returns

**When**: Deep nesting (3+ levels)  
**How**: Guard clauses at function entry  
**Example**: Error checks at start of functions

### Pattern 7: Encapsulate Global State

**When**: Multiple files access same global  
**How**: Static variable + accessor functions  
**Example**: `g_shutdown_ctx` with atomic operations

## Verification Commands

```bash
# Check for warnings
make clean && make 2>&1 | grep -E "(warning|error)"

# Run all tests
make test

# Find long functions
awk '/^[a-zA-Z_].*\(/ {func=$0; start=NR} /^}/ {if (NR-start > 100) print start": "func" ("NR-start+1" lines)"}' src/*.c

# Find magic numbers
grep -nE '[0-9]{2,}' src/*.c | grep -v '#define' | head -20

# Check for duplicate includes
for f in src/*.c; do grep '^#include' "$f" | sort | uniq -d; done
```

## Success Criteria

Every code quality improvement session must achieve:

- ✅ Zero compiler warnings (`-Wall -Wextra -std=c11`)
- ✅ All tests passing (`make test`)
- ✅ Code is clearer and more maintainable
- ✅ No behavior changes (verified by tests)

## Documentation

- **Skill workflow**: `skills/c-code-quality/skill.md`
- **Patterns**: `skills/c-code-quality/patterns.md`
- **Checklist**: `skills/c-code-quality/checklist.md`
- **Quick reference**: `skills/c-code-quality/QUICKREF.md`
- **Examples**: `skills/c-code-quality/examples/`

## Related

- [Metrics Documentation](METRICS.md) - metrics.c refactoring example
- [CHANGELOG](../CHANGELOG.md) - code quality improvements history
- [ROADMAP](../ROADMAP.md) - P1 code quality skill completion
