# Performance Tuning Guide

## Overview

Emme is designed for high-performance HTTPS serving with io_uring-based async I/O, optimized TLS handling, and efficient HTTP/2 support.

---

## Performance Benchmarks

### Baseline Performance (SSL Optimized)

**Configuration:**
- SSL read buffer: 32KB
- Partial writes: Enabled
- Buffer release: Enabled
- TLS 1.3 with session resumption

**Benchmark:**
```bash
h2load -n 10000 -c 100 -m 2 https://localhost:8443/
```

**Results:**
```
Reqs/sec: 2,236
Throughput: 697.04 KB/s
Success rate: 100% (0 failures)
Mean latency: 43.02ms
p98 latency: 48.64ms
TLS protocol: TLSv1.3
Cipher: TLS_AES_128_GCM_SHA256
```

### Optimization Impact

| Optimization | Before | After | Improvement |
|--------------|--------|-------|-------------|
| SSL buffer size | 8KB | 32KB | 4x increase |
| Throughput | 460 req/s | 2,236 req/s | **4.9x** |
| Memory per connection | ~84KB | ~50KB | **40% reduction** |
| Syscall overhead | High | Low | Reduced by 75% |

---

## SSL/TLS Performance Tuning

### Buffer Size Configuration

```yaml
ssl:
  read_buffer_size: 32768  # 32KB (range: 4KB-64KB)
```

**Guidelines:**
- **4KB-8KB**: Low memory, high connection count (>10K connections)
- **16KB-32KB**: Balanced (recommended default)
- **64KB**: Maximum throughput, higher memory usage

**Trade-offs:**
- Larger buffers → fewer syscalls, better throughput
- Smaller buffers → more connections per MB of RAM

### Partial Writes

```yaml
ssl:
  enable_partial_write: 1  # Default: enabled
```

**Purpose:** Allows SSL_read/SSL_write to return after processing partial data, enabling better integration with io_uring's async I/O.

**Impact:**
- ✅ Better async I/O integration
- ✅ Reduced latency for large payloads
- ⚠️ Slightly more complex state management

### Buffer Release

```yaml
ssl:
  release_buffers: 1  # Default: enabled
```

**Purpose:** Releases SSL buffers when connections are idle, saving ~34KB per idle connection.

**Memory Savings:**
```
Idle connections: 1,000
Memory saved: 1,000 × 34KB = 34MB
```

**Trade-offs:**
- **Enabled (1)**: Memory efficient, slight overhead on reactivation
- **Disabled (0)**: Faster response for bursty traffic, higher memory usage

### Session Resumption

```yaml
ssl:
  session_cache_size: 100000    # 100K sessions (~10MB RAM)
  session_timeout: 300          # 5 minutes
```

**Impact:**
- **Handshake time**: Full handshake ~150ms → Resumed handshake ~50ms
- **CPU savings**: 60-70% reduction in handshake CPU usage
- **Memory usage**: ~100 bytes per cached session

**Tuning:**
```yaml
# High-traffic site (1M+ sessions/day)
session_cache_size: 1000000  # 1M sessions (~100MB RAM)
session_timeout: 600         # 10 minutes

# Low-memory environment
session_cache_size: 10000    # 10K sessions (~1MB RAM)
session_timeout: 120         # 2 minutes
```

---

## HTTP/2 Tuning

### Keepalive Timeout

```yaml
http2:
  keepalive_timeout: 60  # seconds (10-300)
```

**Guidelines:**
- **Short (10-30s)**: Aggressive connection cleanup, lower resource usage
- **Medium (60s)**: Balanced (recommended default)
- **Long (120-300s)**: Connection reuse for long-polling, WebSocket-like patterns

### Max Requests Per Connection

```yaml
http2:
  max_requests_per_connection: 1000  # (1-100000)
```

**Purpose:** Limits requests per connection to prevent resource exhaustion.

**Tuning:**
- **Low (100-500)**: High-security environments, frequent client rotation
- **Medium (1000)**: General purpose (recommended)
- **High (10000+)**: Long-lived connections, API gateways

### Max Concurrent Streams

```yaml
http2:
  max_concurrent_streams: 100  # (1-1000)
```

**Purpose:** Limits concurrent HTTP/2 streams per connection.

**Tuning:**
- **Low (10-50)**: Prevent head-of-line blocking, simpler resource management
- **Medium (100)**: Balanced for web browsing patterns
- **High (500-1000)**: API workloads with many parallel requests

---

## Thread Pool Tuning

### Current Implementation

The thread pool uses mutex-protected task queue with dynamic scaling:

```yaml
# Planned configuration (future enhancement)
thread_pool:
  min_threads: 4
  max_threads: 32
  idle_timeout: 5  # seconds
```

### Future Lock-Free Implementation

Planned improvements:
- Lock-free ring buffer with C11 atomics
- Work-stealing for load balancing
- Per-thread local queues
- Futex-based thread parking

**Expected improvements:**
- 2-5x throughput at 100+ concurrent connections
- Lower p99 latency under load
- Better CPU cache utilization

---

## System-Level Tuning

### File Descriptor Limits

```bash
# /etc/security/limits.conf
www-data soft nofile 65536
www-data hard nofile 65536
```

**Calculation:**
```
Max connections: 10,000
File descriptors per connection: 3 (socket, log, epoll)
Recommended limit: 10,000 × 3 = 30,000 + buffer = 65,536
```

### TCP Tuning

```bash
# /etc/sysctl.conf
net.core.somaxconn = 65536           # Connection backlog
net.ipv4.tcp_max_syn_backlog = 65536 # SYN backlog
net.ipv4.tcp_fastopen = 3            # Enable TCP Fast Open
net.ipv4.tcp_tw_reuse = 1            # Reuse TIME_WAIT sockets

# Buffer sizes
net.core.rmem_max = 16777216         # 16MB read buffer
net.core.wmem_max = 16777216         # 16MB write buffer
net.ipv4.tcp_rmem = 4096 65536 16777216
net.ipv4.tcp_wmem = 4096 65536 16777216
```

Apply: `sudo sysctl -p`

### io_uring Tuning

```bash
# Check io_uring limits
cat /proc/sys/fs/aio-max-nr         # Max async I/O requests
cat /proc/sys/fs/aio-nr             # Current usage

# Increase if needed (temporary)
echo 65536 > /proc/sys/fs/aio-max-nr

# Permanent: add to /etc/sysctl.conf
fs.aio-max-nr = 65536
```

---

## Memory Optimization

### Connection Memory Footprint

**Per-connection memory usage:**
```
Base connection structure: ~16KB
SSL context: ~34KB (released when idle if release_buffers=1)
HTTP/2 stream state: ~8KB per active stream
io_uring SQE/CQE: ~2KB
Total: ~60KB per active connection
```

**Memory calculation for 10,000 connections:**
```
With release_buffers=1:
  Active (1,000): 1,000 × 60KB = 60MB
  Idle (9,000): 9,000 × 26KB = 234MB
  Total: ~294MB

With release_buffers=0:
  All connections: 10,000 × 60KB = 600MB
```

### GCov Coverage Instrumentation Overhead

When building with coverage (`make COVERAGE=1`):
- **Memory overhead**: ~20-30%
- **CPU overhead**: ~10-15%
- **Binary size**: 2-3x larger

**Recommendation**: Never run coverage builds in production.

---

## Logging Performance

### Async Ring Buffer

```yaml
logging:
  buffer_size: 4096  # Number of log messages
```

**Impact:**
- Larger buffer → less blocking on log writes
- Default (4096) → ~4MB ring buffer (assuming 1KB per message)
- Dedicated logging thread prevents I/O blocking

### Log Format

```yaml
logging:
  format: json  # or "plain"
```

**Performance:**
- **Plain text**: ~10% faster, human-readable
- **JSON**: Structured, better for log aggregation (ELK, Splunk)

### Log Rotation

```yaml
logging:
  rollover_size: 10485760  # 10MB
  rollover_daily: true
```

**Best practices:**
- Size-based rotation: Prevents disk exhaustion
- Daily rotation: Easier log analysis by date
- Keep 7-14 days of logs for troubleshooting

---

## Load Testing

### h2load Benchmarks

**Basic test:**
```bash
h2load -n 10000 -c 100 -m 2 https://localhost:8443/
```

**Stress test:**
```bash
h2load -n 100000 -c 1000 -m 10 https://localhost:8443/
```

**Parameters:**
- `-n`: Total requests
- `-c`: Concurrent clients
- `-m`: Max concurrent streams per connection (HTTP/2)

### Apache Bench (HTTP/1.1)

```bash
ab -n 10000 -c 100 -k https://localhost:8443/
```

### Custom Load Test

```bash
# Simulate realistic workload
for i in {1..100}; do
  curl -vk https://localhost:8443/api/data &
done
wait
```

---

## Monitoring Performance

### Key Metrics

**Throughput:**
- Requests per second (req/s)
- Bytes per second (B/s)

**Latency:**
- Mean latency
- p50, p95, p99 latencies

**Resource Usage:**
- Active connections
- Thread pool utilization
- Memory usage (RSS, VSZ)
- io_uring queue depths

### Prometheus Metrics (Planned)

```promql
# Request rate
rate(emme_requests_total[5m])

# Latency histogram
histogram_quantile(0.99, rate(emme_request_duration_seconds_bucket[5m]))

# Active connections
emme_active_connections

# Thread pool utilization
emme_thread_pool_active_threads / emme_thread_pool_max_threads
```

---

## Troubleshooting Performance Issues

### High Latency

**Symptoms:**
- p99 latency > 500ms
- Request timeouts

**Diagnosis:**
```bash
# Check active connections
netstat -an | grep 8443 | wc -l

# Check thread pool (future)
# Look for thread pool exhaustion in logs

# Check io_uring queue depths
cat /sys/kernel/debug/io_uring/
```

**Solutions:**
1. Increase `max_connections`
2. Tune thread pool (min/max threads)
3. Optimize SSL buffer sizes
4. Enable session resumption

### High Memory Usage

**Symptoms:**
- RSS > expected based on connection count
- OOM killer activation

**Diagnosis:**
```bash
# Check memory usage
ps -o pid,rss,vsz,comm -C emme

# Count connections
netstat -an | grep 8443 | wc -l
```

**Solutions:**
1. Enable `release_buffers=1`
2. Reduce `read_buffer_size`
3. Lower `session_cache_size`
4. Decrease `max_connections`

### Low Throughput

**Symptoms:**
- req/s << expected
- High CPU usage

**Diagnosis:**
```bash
# Check CPU usage
top -p $(pgrep emme)

# Check syscall overhead
strace -c -p $(pgrep emme)
```

**Solutions:**
1. Increase SSL buffer size
2. Enable partial writes
3. Tune TCP buffers
4. Consider lock-free thread pool (future)

---

## Performance Checklist

### Pre-Deployment

- [ ] SSL buffer size tuned for workload
- [ ] Session resumption enabled
- [ ] File descriptor limits increased
- [ ] TCP tuning applied
- [ ] io_uring support verified

### Post-Deployment

- [ ] Baseline benchmark completed
- [ ] Memory usage within budget
- [ ] Latency targets met (p99 < 100ms)
- [ ] Error rate < 0.1%
- [ ] Monitoring dashboards configured

### Ongoing

- [ ] Weekly performance regression tests
- [ ] Monthly load testing with production-like traffic
- [ ] Quarterly review of memory/CPU usage trends

---

## Related Documentation

- [Deployment Guide](DEPLOYMENT.md) - Production deployment
- [Configuration Improvements](CONFIG_IMPROVEMENTS.md) - Config system details
- [README](../README.md) - General documentation
