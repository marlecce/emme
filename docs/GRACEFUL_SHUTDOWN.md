# Graceful Shutdown

## Overview

Emme implements a production-ready graceful shutdown mechanism that ensures zero request drops during deployments, rolling updates, and maintenance operations.

## Behavior

### Signal Handling

Emme responds differently to different termination signals:

| Signal | Behavior | Use Case |
|--------|----------|----------|
| **SIGTERM** | Graceful shutdown with drain | Production deployments, Kubernetes pod termination |
| **SIGINT** | Immediate shutdown | Development (Ctrl+C) |

### Graceful Shutdown Phases (SIGTERM)

1. **Stop Accepting Connections** (immediate)
   - Server socket closed
   - No new clients accepted
   - Health endpoint returns 503

2. **Drain Phase** (up to 30s default)
   - In-flight requests complete normally
   - Thread pool continues processing
   - Progress logged every second

3. **Forced Shutdown** (if timeout reached)
   - Remaining connections terminated
   - Resources cleaned up
   - Final metrics logged

4. **Cleanup** (immediate)
   - Thread pool destroyed
   - SSL context freed
   - io_uring resources released

## Configuration

### config.yaml

```yaml
server:
  shutdown_timeout_seconds: 30  # 1-300 seconds
```

### Environment Variable

Override the config file setting:

```bash
EMME_SHUTDOWN_TIMEOUT=10 ./emme --config config.yaml
```

**Priority**: Environment variable > config.yaml > default (30s)

## Health Endpoint Integration

During graceful shutdown, the `/health` endpoint changes behavior:

### Normal Operation
```bash
$ curl -k https://localhost:8443/health
HTTP/1.1 200 OK
Content-Type: application/json

{"status":"ok"}
```

### During Drain Phase
```bash
$ curl -k https://localhost:8443/health
HTTP/1.1 503 Service Unavailable
Content-Type: application/json
Retry-After: 5

{"status":"draining","reason":"graceful_shutdown"}
```

This signals load balancers to route traffic away from the draining instance.

## Logging

### Shutdown Initiation
```
SIGTERM received - graceful shutdown initiated. Draining 5 in-flight requests with 30s timeout
```

### Drain Progress (logged every second)
```
Draining: 3 requests still in-flight...
Draining: 1 requests still in-flight...
```

### Shutdown Complete
```
Graceful shutdown complete. Duration: 247ms | Completed: 5 | Forced: 0 | Peak: 5
```

**Metrics**:
- **Duration**: Total time from SIGTERM to exit (milliseconds)
- **Completed**: Requests that finished normally during drain
- **Forced**: Requests still in-flight when timeout reached (if any)
- **Peak**: Maximum concurrent in-flight requests during shutdown

### Immediate Shutdown (SIGINT)
```
SIGINT received - immediate shutdown (development mode)
Immediate shutdown completed (SIGINT)
```

## Load Balancer Integration

### Kubernetes

```yaml
apiVersion: v1
kind: Pod
metadata:
  name: emme
spec:
  terminationGracePeriodSeconds: 35  # Must be > shutdown_timeout_seconds
  containers:
  - name: emme
    image: emme:latest
    lifecycle:
      preStop:
        exec:
          command: ["sleep", "5"]  # Optional: delay SIGTERM for LB propagation
```

### HAProxy

```haproxy
backend emme_servers
    option httpchk GET /health
    http-check expect status 200
    server emme1 192.168.1.10:8443 check ssl verify required inter 5s fall 3 rise 2
```

### Nginx

```nginx
upstream emme {
    server 192.168.1.10:8443;
    health_check interval=5s fails=3 passes=2 match=/health/expect=200;
}
```

### AWS ALB

- **Health check path**: `/health`
- **Protocol**: HTTPS
- **Expected**: 200 OK
- **Deregistration delay**: 35 seconds (must exceed shutdown timeout)

## Implementation Details

### Lock-Free Design

The shutdown system uses lock-free atomic operations for zero overhead in the hot path:

```c
typedef enum {
    SHUTDOWN_STATE_RUNNING = 0,
    SHUTDOWN_STATE_DRAINING = 1,
    SHUTDOWN_STATE_FORCED = 2
} shutdown_state_t;

typedef struct {
    _Atomic shutdown_state_t state;
    _Atomic size_t in_flight_requests;
    struct timespec deadline;
    struct {
        _Atomic size_t completed;
        _Atomic size_t forced;
        _Atomic size_t peak_in_flight;
        struct timespec start_time;
        struct timespec end_time;
    } metrics;
    int timeout_seconds;
} shutdown_context_t;
```

**Performance Impact**: <1% overhead (2 atomic operations per connection)

### Thread Safety

- All state transitions are atomic
- No mutexes in the accept/dispatch hot path
- In-flight counter uses atomic fetch_add/fetch_sub
- State checks use atomic load

### Resource Cleanup Order

1. Stop accepting new connections (close server socket)
2. Wait for thread pool to drain
3. Force shutdown if timeout reached
4. Destroy thread pool
5. Free SSL context
6. Exit io_uring

## Troubleshooting

### Shutdown Takes Too Long

**Symptoms**:
```
Draining: 100 requests still in-flight...
Draining: 100 requests still in-flight...  # No progress
Graceful shutdown timeout (30s) reached. Forcing shutdown with 100 in-flight requests
```

**Causes**:
1. Long-running requests (e.g., large file uploads)
2. Backend timeouts longer than shutdown timeout
3. Stuck requests (backend not responding)

**Solutions**:
- Increase `shutdown_timeout_seconds` to accommodate slow requests
- Check backend health and response times
- Add request timeout enforcement (Phase 1 P1 item)
- Investigate stuck requests in logs

### Requests Dropped During Deploy

**Symptoms**:
- 5xx errors in client logs during rolling deploy
- Requests not completing despite graceful shutdown

**Causes**:
1. Load balancer still sending traffic after SIGTERM
2. Shutdown timeout too short
3. Health check interval too long

**Solutions**:
- Add preStop hook with sleep (Kubernetes)
- Increase deregistration delay (ALB)
- Reduce health check interval
- Verify load balancer respects 503 from health endpoint

### Health Check Returns 503 Unexpectedly

**Symptoms**:
- Load balancer marks instance unhealthy
- No SIGTERM was sent

**Causes**:
1. Server in drain phase (check logs for SIGTERM)
2. Bug in state machine (rare)

**Solutions**:
- Check server logs for SIGTERM signal
- Verify no accidental signal sent (e.g., from monitoring script)
- Restart instance if state machine bug suspected

## Testing

### Manual Test

```bash
# Terminal 1: Start server
./emme --config config.yaml &

# Terminal 2: Send long-running request in background
curl -k https://localhost:8443/static/large-file.txt &

# Terminal 3: Send SIGTERM
kill -SIGTERM $(pgrep emme)

# Observe: Request completes, server exits after drain
```

### Unit Tests

```bash
# Run shutdown unit tests
./tests/unit/test_shutdown --verbose

# Expected output:
# [PASS] shutdown_context::state_enum_values
# [PASS] shutdown_context::structure_exists
# [PASS] shutdown_context::in_flight_atomic_operations
# [PASS] shutdown_context::metrics_tracking
# [PASS] shutdown_context::timeout_configuration
# [PASS] shutdown_context::deadline_calculation
# [PASS] shutdown_context::state_transitions
# [PASS] shutdown_context::concurrent_access_safety
# [PASS] shutdown_context::global_instance_exists
```

### Integration Test

```bash
# Full test suite includes graceful shutdown tests
make test

# Look for:
# [PASS] Graceful shutdown drains requests properly
```

## Metrics for Monitoring

### Recommended Prometheus Metrics (Future)

```prometheus
# Gauge: 1 during drain, 0 otherwise
emme_shutdown_drain_active

# Counter: Total graceful shutdowns initiated
emme_shutdown_initiated_total{type="sigterm"}

# Counter: Total immediate shutdowns initiated
emme_shutdown_initiated_total{type="sigint"}

# Histogram: Shutdown duration in seconds
emme_shutdown_duration_seconds

# Gauge: In-flight requests during shutdown
emme_shutdown_in_flight_requests

# Counter: Requests completed during drain
emme_shutdown_completed_requests_total

# Counter: Requests forced closed during drain
emme_shutdown_forced_requests_total
```

## Security Considerations

### DoS Mitigation

The shutdown system is designed to resist DoS attacks:

1. **Timeout Enforcement**: Even under attack, shutdown completes within timeout
2. **No New Connections**: Attacker cannot open new connections after SIGTERM
3. **Forced Shutdown**: Prevents indefinite drain from stuck connections

### Signal Protection

Ensure only authorized processes can send signals:

```bash
# Run as dedicated user
sudo useradd -r -s /bin/false emme
sudo chown emme:emme /path/to/emme

# Restrict signal permissions
sudo setpriv --reuid=emme --regid=emme ./emme
```

## Related Documentation

- [Health Check Endpoint](HEALTH_CHECK.md) - `/health` endpoint behavior
- [Deployment Guide](DEPLOYMENT.md) - Production deployment strategies
- [ROADMAP](../ROADMAP.md#p0-graceful-shutdown--drain-logic) - Implementation details

---

*Last updated: 2026-05-10*
