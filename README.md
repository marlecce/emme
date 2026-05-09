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
- **Health Check Endpoint:** Built-in `/health` endpoint for monitoring and load balancer integration.
- **Configurable:** Loads settings from a YAML configuration file.
- **HTTPS by Default:**
  - **TLS Termination:** The server terminates TLS connections using OpenSSL.
  - **SSL/TLS Configuration:** Certificate and private key settings are loaded from the configuration file.
  - **TLS 1.2/1.3 Support:** Modern TLS versions with strong cipher suites.
  - **Session Resumption:** TLS session caching for faster handshakes.
  - **Self-Signed Certificate for Development:** A script is provided to generate a self-signed certificate for development and testing.
  - **Production Guidance:** Clear instructions on obtaining and configuring a certificate from a trusted CA for production use.

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
- **src/thread_pool.c / include/thread_pool.h**: Dynamic thread pool with work stealing.

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
  private_key: certs/dev.key"       # Path to the SSL private key (self-signed for development)
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

## Performance tests

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

Run the full test suite:

```bash
make test
```

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

## Production Deployment

See [Deployment Guide](docs/DEPLOYMENT.md) for:
- Systemd service configuration
- Load balancer integration (HAProxy, Nginx, AWS ALB)
- Performance tuning
- TLS configuration
- Security hardening

## Documentation

- [Health Check Endpoint](docs/HEALTH_CHECK.md) - Health endpoint details
- [Deployment Guide](docs/DEPLOYMENT.md) - Production deployment
- [Monitoring Setup](docs/MONITORING.md) - Prometheus and Grafana integration - TBD

## License

TBD

## Contributing

TBD