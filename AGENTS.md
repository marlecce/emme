# Repository Guidelines

## Mission
This codebase runs a performance-sensitive HTTPS backend in C. Priorities are: correctness under load, predictable shutdown/restart behavior, strong TLS defaults, and safe configuration handling. Optimize only after preserving correctness and observability.

## Project Structure & Ownership
Keep `src/*.c` and `include/*.h` aligned by module (`router`, `server`, `tls`, `config`, `http_parser`, `thread_pool`, `log`). `src/main.c` is process bootstrap only. Runtime config is in `config.yaml`; helper scripts are in `scripts/`; development certs are in `certs/`. Tests are scoped under `tests/unit`, `tests/integration`, and `tests/e2e`.

## Build, Test, and Quality Gates
Use:
- `make` to build `./emme`
- `make test` to run all Criterion tests
- `make coverage` for high-risk changes
- `./scripts/install_deps.sh` for local dependency setup

A backend change is not ready unless it compiles cleanly and passes `make test`. For changes in `server`, `tls`, `router`, `http_parser`, `thread_pool`, or `config`, run coverage and verify critical runtime behavior with `curl -vk https://localhost:8443` (and `h2load` when touching HTTP/2 or performance paths).

## Coding Rules (C11)
You are a super expert senior C engineer and developer.
Keep `-Wall -Wextra` clean. Use 4-space indentation, `snake_case` identifiers, uppercase macros, and header guards. Prefer bounded APIs (`snprintf`, checked lengths), explicit ownership/lifetime, and single-purpose functions. Do not leave debug `printf` in hot paths; route runtime diagnostics through logging.

## Critical Path Requirements
Changes to `src/server.c`, `src/tls.c`, `src/router.c`, `src/http_parser.c`, and `src/thread_pool.c` must preserve:
- deterministic cleanup of memory, sockets, SSL objects, and io_uring resources
- nonblocking/TLS state machine correctness
- path traversal and request validation protections
- behavioral parity where HTTP/1.1 and HTTP/2 should match

## Security & Config Hygiene
Never commit real secrets, private keys, or production certificates. Treat `config.yaml` as development-safe defaults only. Any config schema change must document compatibility impact, defaults, and failure behavior for invalid/missing values.

## Testing Expectations
Every bug fix adds or tightens a regression test. Use:
- unit tests for pure logic and parser/config edge cases
- integration tests for TLS handshake, routing, static serving, reverse proxy, and HTTP/2 negotiation
- deterministic setup/teardown (fixed ports, temp files, explicit cleanup)

## Commit & PR Requirements
Use short imperative commit messages (for example: `harden static path validation`). Keep commits focused. PRs must include:
- behavior change summary and risk area
- validation commands executed (`make test`, `make coverage`, runtime checks)
- operational impact (ports, TLS/config/logging changes)
- relevant request/response or log evidence for protocol/routing changes
