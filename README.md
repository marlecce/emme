# emme

This project implements a high-performance web server in C that aims to outperform popular servers like Nginx and Apache. It leverages advanced features such as **io_uring** for asynchronous I/O and a custom thread pool to efficiently handle multiple client connections. A lightweight, in-place HTTP parser is integrated to minimize overhead and maximize performance.

## Features

- **Asynchronous I/O with io_uring:** Efficiently handles I/O operations without blocking.
- **Custom Thread Pool:** Manages concurrent client connections with dynamic scaling.
- **Optimized HTTP Parsing:** Minimalist in-place HTTP parser for fast request handling.
- **HTTP/2 Support:** Full HTTP/2 protocol support via nghttp2 with ALPN negotiation.
- **Advanced Logging Module:**
  - **Asynchronous Logging:** Uses a lock-free ring buffer and a dedicated logging thread to minimize performance impact.
  - **Configurable Log Output:** Supports multiple appenders (e.g., file and console) via an array-based configuration.
  - **Log Rollover:** Rollover based on file size or daily rotation.
  - **JSON or Plain Text:** Structured JSON logs for aggregation or human-readable plain text.
- **Health Check Endpoint:** Built-in `/health` endpoint for monitoring and load balancer integration.
- **Advanced Configuration System:**
  - **YAML-based Config:** Clean, hierarchical configuration with validation.
  - **Line Number Error Reporting:** Precise error messages with line numbers for quick debugging.
  - **Comprehensive Validation:** Range checking, type validation, and cross-field dependency checks.
  - **Sensible Defaults:** Production-ready defaults with minimal configuration required.
  - **Environment Variable Overrides:** Runtime configuration via `EMME_CONFIG_PATH`, `EMME_PORT`, `EMME_LOG_LEVEL`, `EMME_REQUEST_TIMEOUT`, `EMME_TLS_HANDSHAKE_TIMEOUT`.
- **HTTPS by Default:**
  - **TLS Termination:** The server terminates TLS connections using OpenSSL.
  - **SSL/TLS Configuration:** Certificate and private key settings are loaded from the configuration file.
  - **TLS 1.2/1.3 Support:** Modern TLS versions with strong cipher suites.
  - **Session Resumption:** TLS session caching and tickets for faster handshakes.
  - **TLS Handshake Timeout:** Configurable timeout (default 10s) to prevent slow handshake attacks.
  - **Performance Optimizations:** Configurable SSL buffer sizes (32KB default), partial write support, and memory-efficient buffer release.
  - **Self-Signed Certificate for Development:** A script is provided to generate a self-signed certificate for development and testing.
  - **Production Guidance:** Clear instructions on obtaining and configuring a certificate from a trusted CA for production use.
- **HTTP/2 Optimizations:**
  - **Keepalive Timeout:** Configurable idle connection timeout (default 60s).
  - **Request Limits:** Max requests per connection and concurrent streams to prevent resource exhaustion.
  - **ALPN Negotiation:** Automatic HTTP/2 or HTTP/1.1 selection via TLS ALPN.
- **Request Timeout Enforcement:**
  - **Slowloris Protection:** Configurable request timeout (default 30s) prevents connection hoarding attacks.
  - **408 Response:** Timeout violations receive HTTP 408 with `Retry-After: 5` header.
  - **UUID Correlation:** RFC 4122 version 4 request IDs for distributed tracing.
  - **Lock-Free Metrics:** `emme_request_timeouts_total` counter with <1% overhead.
- **Security Headers:**
  - **6 Default Headers:** HSTS, X-Content-Type-Options, X-Frame-Options, X-XSS-Protection, CSP, Referrer-Policy.
  - **Per-Route Overrides:** Configurable per-route with inheritance model.
  - **CORS Support:** Configurable CORS headers for API endpoints.
  - **Zero Overhead:** Pre-computed at startup, <0.1% CPU overhead.
  - **Metrics:** `emme_security_headers_sent_total`, `emme_cors_headers_sent_total`.
- **Graceful Shutdown:**
  - **SIGTERM Handling:** 30-second drain timeout for in-flight requests.
  - **Connection Tracking:** Atomic reference counting for active connections.
  - **Health Endpoint Integration:** Returns 503 with `Retry-After: 5` during drain.
  - **Shutdown Metrics:** Duration, completed, forced closures logged.
- **Observability:**
  - **Prometheus Metrics:** Built-in metrics server on port 9090 with request, connection, TLS, io_uring, and timeout metrics.
  - **Structured Logging:** JSON or plain text logs with async ring buffer for minimal overhead.
  - **Request Correlation:** UUID-based request IDs logged for distributed tracing.
  - **Health Endpoint:** `/health` returns 200 OK or 503 Service Unavailable during graceful shutdown.
- **Code Quality:**
  - **Reusable Skills:** `skills/c-code-quality/` and `skills/auto-docs/` with documented workflows, patterns, and examples.
  - **Systematic Refactoring:** 4-phase process (Analysis → Prioritization → Implementation → Verification).
  - **Automated Documentation:** 7-phase documentation update workflow triggered on feature completion.
  - **Quality Gates:** Zero warnings, 100% test pass rate, functions <100 lines target.

## Project Structure

```
src/              # Implementation files (*.c)
include/          # Headers (*.h)
tests/
  unit/           # Pure logic, parser, config edge cases
  integration/    # TLS, routing, static serving, HTTP/2
  e2e/            # Full stack verification
config.yaml       # Runtime configuration (dev defaults)
certs/            # Development TLS certificates
scripts/          # Build and deployment helpers
docs/             # Documentation
```

**Core Modules:**
- **src/main.c**: Entry point that loads configuration, initializes the logger, and starts the server.
- **src/server.c**: Main server logic including the event loop using io_uring, connection handling, and TLS handshake.
- **src/http_parser.c / include/http_parser.h**: Custom HTTP parser implementation.
- **src/config.c / include/config.h**: Configuration file loader for server settings (including logging and SSL configuration).
- **src/log.c / include/log.h**: Advanced logging module with async ring buffer.
- **src/tls.c / include/tls.h**: TLS module using OpenSSL to create and manage the SSL context.
- **src/router.c / include/router.h**: Request routing (static files, reverse proxy).
- **src/thread_pool.c / include/thread_pool.h**: Dynamic thread pool with mutex-protected queue (lock-free implementation planned).

## Configuration

The server configuration is loaded from a YAML file (e.g., config.yaml). A sample configuration file might look like:

```yaml
server:
  port: 8443
  max_connections: 100
  log_level: DEBUG
  routes:
    - path: /static/
      technology: static
      document_root: /var/www/html
    - path: /api/
      technology: reverse_proxy
      backend: 127.0.0.1:5000

logging:
  file: /var/log/emme.log           # Full path or file name for the log file
  level: "debug"                    # Log level: debug, info, warn, error
  format: "json"                    # Log format: json or plain
  buffer_size: 4096                 # Ring buffer size (number of log messages)
  rollover_size: 10485760           # Maximum file size in bytes before rollover (e.g., 10 MB)
  rollover_daily: true              # Enable daily rollover
  appender_flags:
    - file                        # Enable file logging
    - console                     # Enable console logging

ssl:
  certificate: certs/dev.crt      # Path to the SSL certificate (self-signed for development)
  private_key: certs/dev.key      # Path to the SSL private key (self-signed for development)
  # SSL Performance Settings
  read_buffer_size: 32768         # SSL read buffer size (4KB-64KB, default 32KB)
  enable_partial_write: 1         # Enable SSL partial writes for async I/O (default 1)
  release_buffers: 1              # Release SSL buffers on idle to save memory (~34KB per connection)
  # TLS Session Resumption
  session_cache_size: 100000      # Session cache size (default 100K entries)
  session_timeout: 300            # Session timeout in seconds (default 300s)
  # session_ticket_key: /path/to/ticket.key  # Optional: TLS session ticket key

http2:
  keepalive_timeout: 60           # HTTP/2 keepalive timeout in seconds (10-300, default 60)
  max_requests_per_connection: 1000  # Max requests per connection (1-100000, default 1000)
  max_concurrent_streams: 100     # Max concurrent streams (1-1000, default 100)

security_headers:
  enabled: true
  headers:
    - name: "Strict-Transport-Security"
      value: "max-age=31536000; includeSubDomains"
    - name: "X-Content-Type-Options"
      value: "nosniff"
    - name: "X-Frame-Options"
      value: "DENY"
    - name: "X-XSS-Protection"
      value: "1; mode=block"
    - name: "Content-Security-Policy"
      value: "default-src 'self'"
    - name: "Referrer-Policy"
      value: "strict-origin-when-cross-origin"

routes:
  - path: /
    handler: static
    root: ./public
    inherit_global_headers: true
  - path: /api/
    handler: reverse_proxy
    upstreams:
      - http://localhost:3000
    cors:
      enabled: true
      allow_origin: "*"
      allow_methods: "GET, POST, OPTIONS"
      allow_headers: "Content-Type, Authorization"
      max_age_seconds: 86400
```

Adjust these settings as needed for your environment.

## HTTPS Setup

### Development (Self-Signed Certificate)

For development and testing, a self-signed certificate is provided. To generate a self-signed certificate:

1. Navigate to the scripts/ directory (or the root of the project if placed there).

2. Run the provided script:

```bash
./generate_cert.sh
```

This script will generate:

- certs/dev.crt: The self-signed certificate.
- certs/dev.key: The corresponding private key.

These files will be used by default as specified in the config.yaml under the ssl: section. Note that self-signed certificates are not trusted by browsers or clients by default, and you will see warnings—but this is acceptable for development purposes.

### Production

For a production environment, you should use a certificate signed by a trusted Certificate Authority (CA). To configure your server for production:

1. Obtain a certificate and private key from a CA (e.g., via Let's Encrypt).

2. Update the config.yaml with the appropriate paths:

```yaml
ssl:
  certificate: /etc/ssl/certs/your_domain.crt
  private_key: /etc/ssl/private/your_domain.key
```

3. Ensure the certificate and key files have the correct permissions and are securely stored.

## Build Instructions

Ensure that you have the required dependencies installed:

- `liburing`
- `pthread`
- `libYAML`
- `libnghttp2-dev`
- OpenSSL development libraries (e.g., `libssl-dev`)

To install all necessary dependencies, try the following script:

```sh
./scripts/install_deps.sh
```

To compile the project, run:

```bash
make clean && make
```

## Usage

Start the server by running:

```bash
./emme
```

Or with a custom configuration file:

```bash
./emme --config /path/to/config.yaml
```

The server will listen on the configured HTTPS port (e.g., 8443) and handle incoming HTTPS requests. Use a browser or tool like curl to test:

```bash
curl -vk https://localhost:8443
```

### Health Check

Monitor server health with the built-in `/health` endpoint:

```bash
curl -vk https://localhost:8443/health
```

Response:
```json
{"status":"ok"}
```

### Security Headers

Verify security headers are sent:

```bash
curl -vk https://localhost:8443/ 2>&1 | grep -E "^(Strict-Transport-Security|X-Content-Type-Options|X-Frame-Options):"
```

Response headers:
```
Strict-Transport-Security: max-age=31536000; includeSubDomains
X-Content-Type-Options: nosniff
X-Frame-Options: DENY
Content-Security-Policy: default-src 'self'
Referrer-Policy: strict-origin-when-cross-origin
```

Verify CORS headers on API endpoints:

```bash
curl -vk -X OPTIONS https://localhost:8443/api/test -H "Origin: https://example.com" 2>&1 | grep "Access-Control-Allow-"
```

### Prometheus Metrics

Access metrics at `http://localhost:9090/metrics`:

```bash
curl http://localhost:9090/metrics | grep security_headers
```

Output:
```
# HELP emme_security_headers_sent_total Total number of security headers sent
# TYPE emme_security_headers_sent_total counter
emme_security_headers_sent_total 1234
# HELP emme_cors_headers_sent_total Total number of CORS headers sent
# TYPE emme_cors_headers_sent_total counter
emme_cors_headers_sent_total 567
```

### Environment Variables

Override configuration with environment variables:

```bash
EMME_PORT=9443 EMME_LOG_LEVEL=debug ./emme
```

| Variable | Description | Default |
|----------|-------------|---------|
| `EMME_CONFIG_PATH` | Path to config.yaml | `config.yaml` |
| `EMME_PORT` | Server port | From config |
| `EMME_LOG_LEVEL` | Log level | From config |

## Performance

### Benchmark Results

With SSL buffer optimizations (32KB buffers, partial writes, buffer release):

```bash
h2load -n 10000 -c 100 -m 2 https://localhost:8443/
```

**Results:**
- **Throughput:** 2,236 req/s (697 KB/s)
- **Success Rate:** 100% (0 failures)
- **Mean Latency:** 43.02ms
- **p98 Latency:** 48.64ms
- **TLS Protocol:** TLSv1.3
- **Cipher:** TLS_AES_128_GCM_SHA256

**Optimization Impact:**
- Buffer size increased from 8KB → 32KB (4x)
- SSL read buffer: 32KB (reduces syscall overhead)
- SSL_MODE_ENABLE_PARTIAL_WRITE: Better async I/O integration
- SSL_MODE_RELEASE_BUFFERS: Saves ~34KB per idle connection

### Performance Tests

```bash
h2load -n100 -c10 -m2 https://localhost:8443/
```

## Pipeline tests

```bash
act -j fedora-build --container-architecture linux/amd64
act -j ubuntu-build
```

## Coverage

The coverage is available [here](https://marlecce.github.io/emme/)

## Testing

### Test Suite

Run the full test suite:

```bash
make test
```

**Test Coverage:**
- **Unit Tests:** Config parsing, HTTP parser, router, thread pool
- **Integration Tests:** TLS handshakes, HTTP/2, static file serving, reverse proxy
- **E2E Tests:** Full stack verification with real HTTPS requests

**Current Status:** 108 tests passing (100% success rate)

### Coverage Analysis

Run with coverage instrumentation:

```bash
make COVERAGE=1
make coverage
```

View the coverage report:

```bash
# Text summary
cat coverage/summary.txt

# HTML report
open coverage/index.html
```

**Coverage Highlights:**
- **config.c:** 67% branch coverage (comprehensive config validation tests)
- **http_parser.c:** 75% branch coverage
- **Overall Project:** 54% branch coverage

### Test Categories

| Module | Tests | Coverage | Focus |
|--------|-------|----------|-------|
| Config Parser | 15 | 67% | SSL settings, HTTP/2, logging, validation |
| HTTP Parser | 12 | 75% | Request parsing, edge cases, invalid input |
| Router | 8 | 46% | Path matching, static/reverse proxy routing |
| Thread Pool | 5 | 52% | Task scheduling, dynamic scaling |
| Server | 7 | 48% | TLS, connections, graceful shutdown |
| HTTP/2 | 3 | 41% | Protocol negotiation, frame handling |
| E2E | 2 | 54% | Full request lifecycle |

## Production Deployment

See [Deployment Guide](docs/DEPLOYMENT.md) for:
- Systemd service configuration
- Load balancer integration (HAProxy, Nginx, AWS ALB)
- Performance tuning
- TLS configuration
- Security hardening

## Documentation

- [Performance Tuning Guide](docs/PERFORMANCE.md) - Benchmarks, SSL optimization, system tuning
- [Configuration Improvements](docs/CONFIG_IMPROVEMENTS.md) - Config system refactoring details
- [Deployment Guide](docs/DEPLOYMENT.md) - Production deployment and load balancer integration
- [Health Check Endpoint](docs/HEALTH_CHECK.md) - Health endpoint monitoring
- [Prometheus Metrics](docs/METRICS.md) - Metrics endpoint, available metrics, troubleshooting
- [Code Quality Skill](docs/CODE_QUALITY.md) - Refactoring workflow, patterns, examples
- [Monitoring Setup](docs/MONITORING.md) - Prometheus and Grafana integration - TBD

## License

TBD

## Contributing

TBD