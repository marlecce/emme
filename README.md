# emme

This project implements a high-performance web server in C that aims to outperform popular servers like Nginx and Apache. It leverages advanced features such as **io_uring** for asynchronous I/O and a custom thread pool to efficiently handle multiple client connections. A lightweight, in-place HTTP parser is integrated to minimize overhead and maximize performance.

## Features

- **Asynchronous I/O with io_uring:** Efficiently handles I/O operations without blocking.
- **Custom Thread Pool:** Manages concurrent client connections.
- **Optimized HTTP Parsing:** Minimalist in-place HTTP parser for fast request handling.
- **Configurable:** Loads settings from a YAML configuration file.

## Project Structure

- **`server.c`**: Main server logic including the event loop using io_uring and connection handling.
- **`http_parser.h` / `http_parser.c`**: Custom HTTP parser implementation.
- **`config.h` / `config.c`**: Configuration file loader for server settings.
- **`server.h`**: Definitions and declarations used throughout the server.
- **`test_http_parser.c`**: Unit tests for the custom HTTP parser.
- **`config.yaml`**: Sample configuration file.

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
```

Adjust these settings as needed for your environment.

## Usage

Start the server by running:

```bash
./emme
```

The server will listen on the configured port and handle incoming HTTP requests.
