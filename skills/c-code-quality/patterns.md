# Patterns for C Code Refactoring

Reusable patterns for common refactoring scenarios.

## Pattern 1: Extract Helper Function

### When to Use
- Function >100 lines
- Repeated code blocks (2+ times)
- Complex logic that can be named

### Template

**Before:**
```c
void process_request(request_t *req)
{
    // 20 lines: Validate request
    if (!req) return;
    if (req->size == 0) return;
    // ... more validation ...
    
    // 40 lines: Process request
    for (...) { ... }
    
    // 20 lines: Send response
    write(fd, buf, len);
}
```

**After:**
```c
static int validate_request(const request_t *req);
static int process_request_impl(request_t *req);
static int send_response(int fd, const response_t *resp);

void process_request(request_t *req)
{
    if (validate_request(req) != 0) return;
    if (process_request_impl(req) != 0) return;
    send_response(fd, &resp);
}
```

### Benefits
- Each function has single responsibility
- Easier to test in isolation
- Clearer intent from function name
- Reduces cyclomatic complexity

---

## Pattern 2: Named Constants

### When to Use
- Literal numbers >9 (excluding 0, 1, -1)
- Magic strings repeated multiple times
- Buffer sizes, timeouts, limits

### Template

**Before:**
```c
char buffer[65536];
timeout.tv_sec = 30;
if (listen(fd, 10) < 0) ...
if (port > 0 && port <= 65535) ...
```

**After:**
```c
#define BUFFER_SIZE 65536
#define TIMEOUT_SECONDS 30
#define SERVER_BACKLOG 10
#define MAX_PORT_NUMBER 65535

char buffer[BUFFER_SIZE];
timeout.tv_sec = TIMEOUT_SECONDS;
if (listen(fd, SERVER_BACKLOG) < 0) ...
if (port > 0 && port <= MAX_PORT_NUMBER) ...
```

### Guidelines
- Place `#define` near top of file (after includes)
- Use `UPPER_CASE` with underscores
- Add comment if purpose is not obvious
- Consider `enum` for related constants

---

## Pattern 3: Goto-Based Cleanup

### When to Use
- Multiple resources to allocate
- Multiple failure paths
- Need to cleanup in reverse order

### Template

**Before:**
```c
int init(void)
{
    if (alloc_a() < 0) {
        log_error("alloc_a failed");
        return -1;
    }
    if (alloc_b() < 0) {
        log_error("alloc_b failed");
        free_a();
        return -1;
    }
    if (alloc_c() < 0) {
        log_error("alloc_c failed");
        free_b();
        free_a();
        return -1;
    }
    return 0;
}
```

**After:**
```c
int init(void)
{
    int ret = -1;
    
    if (alloc_a() < 0) {
        log_error("alloc_a failed");
        goto err_exit;
    }
    if (alloc_b() < 0) {
        log_error("alloc_b failed");
        goto err_free_a;
    }
    if (alloc_c() < 0) {
        log_error("alloc_c failed");
        goto err_free_b;
    }
    
    ret = 0;
    goto done;

err_free_b:
    free_b();
err_free_a:
    free_a();
err_exit:
done:
    return ret;
}
```

### Benefits
- Cleanup logic in one place
- Easy to add new resources
- No code duplication
- Clear error flow

---

## Pattern 4: snprintf Validation

### When to Use
- Any `snprintf` call where truncation is an error
- Building strings for protocols (HTTP, etc.)
- Formatting user-visible messages

### Template

**Before:**
```c
int len = snprintf(buf, size, "format %s", str);
strcpy(dest, buf);  // May overflow!
```

**After:**
```c
int len = snprintf(buf, size, "format %s", str);
if (len < 0 || (size_t)len >= size) {
    log_error("Buffer too small for formatted string");
    return -1;
}
strcpy(dest, buf);  // Now safe
```

### For Multiple snprintf Calls

```c
size_t offset = 0;
int written;

written = snprintf(buf + offset, size - offset, "part1");
if (written < 0 || (size_t)written >= size - offset) {
    return -1;
}
offset += (size_t)written;

written = snprintf(buf + offset, size - offset, "part2");
if (written < 0 || (size_t)written >= size - offset) {
    return -1;
}
offset += (size_t)written;
```

### Helper Function Approach

```c
static int append_string(char *buf, size_t size, size_t *offset, 
                         const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf + *offset, size - *offset, fmt, args);
    va_end(args);
    
    if (len < 0 || (size_t)len >= size - *offset) {
        return -1;
    }
    *offset += (size_t)len;
    return 0;
}

// Usage:
size_t offset = 0;
if (append_string(buf, size, &offset, "part1") != 0) return -1;
if (append_string(buf, size, &offset, "part2") != 0) return -1;
```

---

## Pattern 5: Remove Duplication

### When to Use
- Same code pattern appears 2+ times
- Similar logic with different types/variables
- Copy-paste with minor modifications

### Template: Extract Common Logic

**Before:**
```c
void init_hist_a(void)
{
    for (int i = 0; i < BUCKETS; i++) {
        hist_a.buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist_a.lock, NULL);
}

void init_hist_b(void)
{
    for (int i = 0; i < BUCKETS; i++) {
        hist_b.buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist_b.lock, NULL);
}
```

**After:**
```c
static void init_histogram(MetricHistogram *hist)
{
    for (int i = 0; i < BUCKETS; i++) {
        hist->buckets[i] = bounds[i];
    }
    pthread_mutex_init(&hist->lock, NULL);
}

void init_hist_a(void) { init_histogram(&hist_a); }
void init_hist_b(void) { init_histogram(&hist_b); }
```

### Template: Macro for Repetitive Patterns

**Before:**
```c
#define FORMAT_COUNTER(name, help, value) \
    offset += snprintf(buf + offset, size - offset, \
                      "# HELP %s %s\n# TYPE counter\n%ld\n", \
                      name, help, value)

FORMAT_COUNTER("requests", "Total requests", g_requests);
FORMAT_COUNTER("errors", "Total errors", g_errors);
```

**After (Better - Use Function):**
```c
static int format_counter(char *buf, size_t size, size_t *offset,
                          const char *name, const char *help, long value)
{
    int len = snprintf(buf + *offset, size - *offset,
                      "# HELP %s %s\n# TYPE counter\n%ld\n",
                      name, help, value);
    if (len < 0 || (size_t)len >= size - *offset) return -1;
    *offset += (size_t)len;
    return 0;
}

if (format_counter(buf, size, &offset, "requests", "Total requests", g_requests) != 0)
    return -1;
```

**Why function over macro:**
- Type checking
- Debuggable
- No unexpected side effects
- Better error messages

---

## Pattern 6: Early Returns

### When to Use
- Deep nesting (>3 levels)
- Guard conditions at function start
- Error handling at call sites

### Template

**Before:**
```c
int process(data_t *data)
{
    if (data) {
        if (data->size > 0) {
            if (data->valid) {
                // Main logic here (deeply nested)
                for (...) {
                    if (cond) {
                        // ...
                    }
                }
            }
        }
    }
    return 0;
}
```

**After:**
```c
int process(data_t *data)
{
    if (!data) return -1;
    if (data->size == 0) return -1;
    if (!data->valid) return -1;
    
    // Main logic at base indentation
    for (...) {
        if (!cond) continue;
        // ...
    }
    return 0;
}
```

### Benefits
- Reduces cyclomatic complexity
- Easier to read (less indentation)
- Clearer error handling
- Follows "guard clause" pattern

---

## Pattern 7: Encapsulate Global State

### When to Use
- Multiple global variables
- Extern globals accessed from many files
- Need to add synchronization later

### Template

**Before:**
```c
// server.c
SSL_CTX *ssl_ctx = NULL;
static struct io_uring global_ring;
static int g_server_fd = -1;
shutdown_context_t g_shutdown_ctx = {0};

// Accessed directly from many files
extern SSL_CTX *ssl_ctx;
extern shutdown_context_t g_shutdown_ctx;
```

**After:**
```c
// server.c
typedef struct {
    SSL_CTX *ssl_ctx;
    struct io_uring ring;
    int server_fd;
    shutdown_context_t shutdown_ctx;
    _Atomic int is_initialized;
} ServerContext;

static ServerContext g_server = {
    .ssl_ctx = NULL,
    .server_fd = -1,
    .is_initialized = 0
};

// Accessor functions (in header)
SSL_CTX* get_ssl_ctx(void);
struct io_uring* get_ring(void);
int get_server_fd(void);
```

### Benefits
- Single point of initialization
- Easier to add locking later
- Clear ownership
- Better testability (can mock context)

---

## Pattern Selection Guide

| Symptom | Pattern |
|---------|---------|
| Function too long | Extract Helper Function |
| Magic numbers | Named Constants |
| Repeated cleanup code | Goto-Based Cleanup |
| Buffer truncation risk | snprintf Validation |
| Copy-paste code | Remove Duplication |
| Deep nesting | Early Returns |
| Too many globals | Encapsulate Global State |

## Applying Patterns

1. **Identify** the code smell
2. **Select** the appropriate pattern
3. **Apply** incrementally (one pattern at a time)
4. **Verify** tests still pass
5. **Measure** improvement (LOC, complexity, warnings)
