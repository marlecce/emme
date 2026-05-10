# C Code Quality Improvement Skill

## Overview

This skill provides a systematic approach to improving C code quality through targeted refactoring while preserving correctness and performance.

## Structure

```
skills/c-code-quality/
├── skill.md           # Main instructions and workflow
├── checklist.md       # Quality gates and verification steps
├── patterns.md        # Reusable refactoring patterns
└── examples/          # Before/after examples from this codebase
    ├── metrics-refactor.example
    ├── main-cleanup.example
    └── router-cleanup.example
```

## Quick Start

```bash
# Load the skill
/skill c-code-quality

# Typical workflow
1. Analyze target files (see skill.md Phase 1)
2. Prioritize issues (High/Medium/Low)
3. Apply patterns from patterns.md
4. Verify with checklist.md
```

## When to Use

- ✅ After implementing a feature (cleanup pass)
- ✅ Before merging to main (quality gate)
- ✅ During technical debt sprints
- ✅ When investigating bugs (code familiarization)
- ✅ When onboarding new contributors

## When NOT to Use

- ❌ During critical production incidents
- ❌ When feature delivery is time-critical
- ❌ Without running tests first
- ❌ Without understanding the code's purpose

## Core Principles

1. **Correctness First**: Never break working code
2. **Incremental Changes**: One pattern at a time
3. **Verify Everything**: Build + tests must pass
4. **Preserve API**: Don't change public interfaces
5. **Measure Improvement**: Track metrics before/after

## Key Documents

| Document | Purpose | When to Use |
|----------|---------|-------------|
| `skill.md` | Overall workflow | Start of session |
| `checklist.md` | Quality gates | During and after changes |
| `patterns.md` | Refactoring techniques | When applying specific changes |
| `examples/` | Real-world examples | For inspiration/guidance |

## Workflow Summary

### Phase 1: Analysis (10-15 min)
```bash
make clean && make 2>&1 | grep warning
# Identify warnings, long functions, magic numbers
```

### Phase 2: Prioritization (5 min)
- High: Warnings, errors, leaks
- Medium: Duplication, complexity
- Low: Style, comments

### Phase 3: Implementation (varies)
- Apply one pattern at a time
- Commit after each logical change
- Verify after each commit

### Phase 4: Verification (10 min)
```bash
make clean && make && make test
# Zero warnings, all tests pass
```

## Metrics to Track

| Metric | Target | How to Measure |
|--------|--------|----------------|
| Compiler warnings | 0 | `make 2>&1 \| grep warning` |
| Function length | <100 lines | Manual count or `wc -l` |
| Test pass rate | 100% | `make test` |
| Code duplication | <5% | Visual inspection or tool |

## Integration

This skill works with:
- `AGENTS.md` - Repository coding standards
- `.github/workflows/ci.yml` - CI quality gates
- `Makefile` - Build and test commands

## Examples from This Codebase

### Metrics Module (src/metrics.c)
- **Problem**: 126-line function with duplication
- **Pattern**: Extract Helper Function
- **Result**: 40-line main + 3 reusable helpers
- **Test**: 76/76 passing

### Main Function (src/main.c)
- **Problem**: Magic numbers, inconsistent logging
- **Pattern**: Named Constants
- **Result**: `DEFAULT_METRICS_PORT`, `MAX_PORT_NUMBER`
- **Test**: Zero warnings

### Router Module (src/router.c)
- **Problem**: Duplicate includes, unnecessary coupling
- **Pattern**: Remove Duplication
- **Result**: Removed 2 includes, reduced coupling
- **Test**: Compile time improved ~5%

## Common Mistakes to Avoid

1. **Changing too much at once**
   - ❌ Refactor entire file in one commit
   - ✅ One pattern per commit

2. **Not verifying after each change**
   - ❌ Make 10 changes then test
   - ✅ Test after each pattern application

3. **Optimizing prematurely**
   - ❌ "This might be faster if..."
   - ✅ "This is clearer and tests pass"

4. **Breaking public APIs**
   - ❌ Change function signatures
   - ✅ Keep APIs stable, refactor internals

## Success Criteria

A code quality session is successful when:
- ✅ Zero new compiler warnings
- ✅ All tests still pass
- ✅ Code is clearer (subjective but verifiable)
- ✅ No behavior changes (unless intentional)
- ✅ Commit messages explain "why" not just "what"

## Next Steps

After mastering this skill:
1. Apply to other C projects
2. Extract language-agnostic patterns
3. Create companion skills (e.g., "performance-profiling")
4. Contribute improvements back to this skill

## Support

For questions or improvements:
- Review `examples/` for concrete cases
- Check `checklist.md` for verification steps
- Consult `patterns.md` for specific techniques
- Refer to `AGENTS.md` for project-specific standards
