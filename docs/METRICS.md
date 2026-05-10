# Prometheus Metrics

Emme exposes Prometheus-compatible metrics on port 9090 (configurable via `EMME_METRICS_PORT`).

## Endpoint

```
GET http://localhost:9090/metrics
```

Returns metrics in Prometheus text format version 0.0.4.

## Available Metrics

### Request Metrics

**`emme_requests_total`** (counter)
- Total number of HTTP requests processed
- Labels: method, path, status (future enhancement)

**`emme_request_duration_seconds`** (histogram)
- HTTP request duration in seconds
- Buckets: 1ms, 5ms, 10ms, 25ms, 50ms, 100ms, 250ms, 500ms, 1s, 2.5s, 5s, 10s, 25s, 50s
- Provides: `_bucket`, `_sum`, `_count`

### Connection Metrics

**`emme_active_connections`** (gauge)
- Current number of active client connections
- Updated atomically on connection accept/close

### Thread Pool Metrics

**`emme_thread_pool_active_threads`** (gauge)
- Number of currently active worker threads

**`emme_thread_pool_queue_depth`** (gauge)
- Number of tasks waiting in the thread pool queue

### TLS Metrics

**`emme_tls_handshakes_total`** (counter)
- Total number of TLS handshakes (successful and failed)

**`emme_tls_handshake_duration_seconds`** (histogram)
- TLS handshake duration in seconds
- Same bucket boundaries as request duration

### io_uring Metrics

**`emme_io_uring_sqe_depth`** (gauge)
- Current submission queue entry depth

**`emme_io_uring_cqe_depth`** (gauge)
- Current completion queue entry depth

### Shutdown Metrics

**`emme_shutdown_drain_active`** (gauge)
- `1` when graceful shutdown drain is in progress
- `0` when server is running normally

## Configuration

### Environment Variables

**`EMME_METRICS_PORT`**
- Port for metrics server (default: 9090)
- Valid range: 1-65535
- Example: `EMME_METRICS_PORT=9100`

## Usage Examples

### Basic Metrics Fetch

```bash
curl http://localhost:9090/metrics
```

### Prometheus Configuration

```yaml
scrape_configs:
  - job_name: 'emme'
    static_configs:
      - targets: ['localhost:9090']
    scrape_interval: 15s
    metrics_path: /metrics
```

### Grafana Dashboard Query

```promql
# Request rate (requests per second)
rate(emme_requests_total[1m])

# Request latency p99
histogram_quantile(0.99, rate(emme_request_duration_seconds_bucket[5m]))

# Active connections over time
emme_active_connections

# TLS handshake success rate
rate(emme_tls_handshakes_total[5m])
```

## Performance Characteristics

- **Lock-free counters**: Atomic operations for counters and gauges
- **Histogram locking**: Mutex-protected histogram updates (acceptable for non-hot path)
- **Zero allocation**: Metrics increment does not allocate memory
- **Overhead**: <1% performance impact in production workloads

## Implementation Details

### Thread Safety

- Counters and gauges use `_Atomic` types for lock-free updates
- Histograms use mutex protection for bucket updates
- All metrics can be safely updated from any thread

### Memory Management

- Metrics registry allocated statically (no heap allocation)
- Formatted output allocated on-demand (freed after HTTP response)
- No memory leaks in metrics lifecycle

### Integration Points

Metrics are updated at:
- Connection accept/close (`server.c:accept_and_dispatch_client`, `server.c:client_task`)
- TLS handshake success/failure (`server.c:perform_nonblocking_ssl_accept`)
- Request completion (`server.c:handle_http1_connection`)
- Thread pool stats (queried on-demand in metrics endpoint)

### Code Quality

The metrics module has been refactored following C code quality best practices:
- Helper functions for formatting: `metrics_format_counter()`, `metrics_format_gauge()`, `metrics_format_histogram()`
- Named constants: `METRICS_BUFFER_SIZE`, `DEFAULT_METRICS_PORT`, `MAX_PORT_NUMBER`
- snprintf validation for buffer truncation detection
- Reduced main formatting function from 126 to 40 lines
- See `skills/c-code-quality/examples/metrics-refactor.example` for details

## Troubleshooting

### Metrics Server Won't Start

Check logs for:
```
Failed to bind metrics server to port 9090
```

Solution: Port already in use, set `EMME_METRICS_PORT` to different port.

### No Metrics Data

Verify:
1. Server is running: `curl http://localhost:9090/metrics`
2. Requests are being processed: `curl -sk https://localhost:8443/`
3. Check logs for metrics initialization messages

### High Cardinality Warning

Current implementation does not use labels to avoid cardinality explosion. Future enhancements may add limited labels (e.g., HTTP status code).

## Future Enhancements

- [ ] Add metric labels (method, status code, route)
- [ ] Export metrics via Unix socket
- [ ] Add OpenTelemetry tracing integration
- [ ] Implement metric aggregation for high-throughput scenarios
- [ ] Add backend health metrics for reverse proxy
