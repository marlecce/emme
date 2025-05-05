# emme

This project implements a high-performance web server in C that aims to outperform popular servers like Nginx and Apache. It leverages advanced features such as **io_uring** for asynchronous I/O and a custom thread pool to efficiently handle multiple client connections. A lightweight, in-place HTTP parser is integrated to minimize overhead and maximize performance.

## Features

- **Asynchronous I/O with io_uring:** Efficiently handles I/O operations without blocking.
- **Custom Thread Pool:** Manages concurrent client connections.
- **Optimized HTTP Parsing:** Minimalist in-place HTTP parser for fast request handling.
- **Advanced Logging Module:**
  - **Asynchronous Logging:** Uses a lock-free ring buffer and a dedicated logging thread to minimize performance impact.
  - **Configurable Log Output:** Supports multiple appenders (e.g., file and console) via an array-based configuration.
  - **Log Rollover:** Rollover based on file size or daily rotation.
- **Configurable:** Loads settings from a YAML configuration file.
- **HTTPS by Default:**
  - **TLS Termination:** The server terminates TLS connections using OpenSSL.
  - **SSL/TLS Configuration:** Certificate and private key settings are loaded from the configuration file.
  - **Self-Signed Certificate for Development:** A script is provided to generate a self-signed certificate for development and testing.
  - **Production Guidance:** Clear instructions on obtaining and configuring a certificate from a trusted CA for production use.

## Project Structure

- **src/main.c**: Entry point that loads configuration, initializes the logger, and starts the server.
- **src/server.c**: Main server logic including the event loop using io_uring, connection handling, and TLS handshake.
- **src/http_parser.c / include/http_parser.h**: Custom HTTP parser implementation.
- **src/config.c / include/config.h**: Configuration file loader for server settings (including logging and SSL configuration).
- **src/advanced_log.c / include/advanced_log.h**: Advanced logging module implementation.
- **include/logging_common.h**: Shared logging definitions (log levels, formats, and the LoggingConfig structure).
- **src/tls.c / include/tls.h**: TLS module using OpenSSL to create and manage the SSL context.
- **Tests**:  
  - **tests/test_http_parser.c**: Unit tests for the HTTP parser.
  - Additional tests (e.g., test_config, test_server) can be added.
- **config.yaml**: Sample configuration file.
- **scripts/generate_cert.sh**: Shell script to generate a self-signed certificate for development.

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

To compile the project, run:

```bash
make clean && make
```

## Usage

Start the server by running:

```bash
./emme
```

The server will listen on the configured HTTPS port (e.g., 8443) and handle incoming HTTPS requests. Use a browser or tool like curl to test:

```bash
curl -vk https://localhost:8443
```

## Performance tests

```bash
h2load -n100 -c10 -m2 https://localhost:8443/
```