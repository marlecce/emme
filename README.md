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

## Project Structure

- **src/main.c**: Entry point that loads configuration, initializes the logger, and starts the server.
- **src/server.c**: Main server logic including the event loop using io_uring and connection handling.
- **src/http_parser.c / include/http_parser.h**: Custom HTTP parser implementation.
- **src/config.c / include/config.h**: Configuration file loader for server settings (now including logging configuration).
- **src/advanced_log.c / include/advanced_log.h**: Advanced logging module implementation.
- **include/logging_common.h**: Shared logging definitions (log levels, formats, and the LoggingConfig structure).
- **Tests**:  
  - **tests/test_http_parser.c**: Unit tests for the HTTP parser.
  - Additional tests (e.g., test_config, test_server) can be added.
- **config.yaml**: Sample configuration file.

## Build Instructions

Ensure that you have the required dependencies installed:
- `liburing`
- `pthread`

To compile the project, run:

```bash
make clean && make 
```

To compile and run the tests, use:

```bash
make test
```

## Configuration

The server configuration is loaded from a YAML file (e.g., config.yaml). A sample configuration file might look like:

```yaml
server:
  port: 8080
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
  file: "/var/log/emme.log"        # Full path or file name for the log file
  level: "debug"                    # Log level: debug, info, warn, error
  format: "json"                    # Log format: json or plain
  buffer_size: 4096                 # Ring buffer size (number of log messages)
  rollover_size: 10485760           # Maximum file size in bytes before rollover (e.g., 10 MB)
  rollover_daily: true              # Enable daily rollover
  appender_flags:
    - file                        # Enable file logging
    - console                     # Enable console logging

```

Adjust these settings as needed for your environment.

## Usage

Start the server by running:

```bash
./emme
```

The server will listen on the configured port and handle incoming HTTP requests.
