# Security Headers

## Overview

Emme sends configurable security headers on all HTTP responses to protect against common web vulnerabilities including clickjacking, MIME type sniffing, XSS attacks, and protocol downgrade attacks. Security headers are pre-computed at startup for zero runtime overhead and support per-route overrides with an inheritance model. CORS headers are also supported for API endpoints.

## Configuration

### YAML Configuration

```yaml
security_headers:
  enabled: true  # Default: true
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
    inherit_global_headers: true  # Inherit global security headers (default: true)
    security_headers:
      enabled: true
      headers:
        - name: "Content-Security-Policy"
          value: "default-src 'self' 'unsafe-inline'"  # Override for this route
    
  - path: /api/
    handler: reverse_proxy
    upstreams:
      - http://localhost:3000
    inherit_global_headers: true
    cors:
      enabled: true
      allow_origin: "*"
      allow_methods: "GET, POST, OPTIONS"
      allow_headers: "Content-Type, Authorization"
      allow_credentials: false
      max_age_seconds: 86400
```

### Environment Variables

- `EMME_SECURITY_HEADERS_ENABLED`: Enable/disable security headers globally (default: true)

## Behavior

### Normal Operation

Security headers are automatically added to all HTTP/1.1 and HTTP/2 responses:

1. **Global headers**: Applied to all routes by default
2. **Per-route overrides**: Routes can override or extend global headers
3. **Inheritance model**: Routes inherit global headers unless `inherit_global_headers: false`
4. **CORS preflight**: OPTIONS requests to routes with CORS enabled receive automatic responses

### Default Security Headers

| Header | Value | Purpose |
|--------|-------|---------|
| `Strict-Transport-Security` | `max-age=31536000; includeSubDomains` | Enforce HTTPS for 1 year |
| `X-Content-Type-Options` | `nosniff` | Prevent MIME type sniffing |
| `X-Frame-Options` | `DENY` | Prevent clickjacking |
| `X-XSS-Protection` | `1; mode=block` | Enable browser XSS filter (legacy) |
| `Content-Security-Policy` | `default-src 'self'` | Restrict resource loading |
| `Referrer-Policy` | `strict-origin-when-cross-origin` | Control referrer information |

### CORS Headers

When CORS is enabled for a route, the following headers are added:

| Header | Config Key | Example |
|--------|------------|---------|
| `Access-Control-Allow-Origin` | `cors.allow_origin` | `*` or `https://example.com` |
| `Access-Control-Allow-Methods` | `cors.allow_methods` | `GET, POST, OPTIONS` |
| `Access-Control-Allow-Headers` | `cors.allow_headers` | `Content-Type, Authorization` |
| `Access-Control-Allow-Credentials` | `cors.allow_credentials` | `true` or omitted |
| `Access-Control-Max-Age` | `cors.max_age_seconds` | `86400` (24 hours) |

### Error Handling

- **Invalid YAML**: Config validation fails at startup with line number error
- **Missing required fields**: Uses sensible defaults (e.g., CORS origin defaults to `*`)
- **Header count exceeded**: Maximum 16 security headers per config, excess ignored with warning

## Performance Impact

- **Memory overhead**: ~1KB for pre-computed headers (one-time allocation at startup)
- **CPU overhead**: <0.1% (headers copied from pre-computed buffer, no runtime formatting)
- **Latency impact**: p50/p95/p99 unchanged (headers added in existing response path)

## Monitoring

### Metrics

- `emme_security_headers_sent_total` (counter): Total number of security header sets sent
- `emme_cors_headers_sent_total` (counter): Total number of CORS header sets sent

Query examples:

```promql
# Security headers sent per second
rate(emme_security_headers_sent_total[5m])

# CORS headers sent per second
rate(emme_cors_headers_sent_total[5m])

# Ratio of CORS to total responses
emme_cors_headers_sent_total / emme_security_headers_sent_total
```

### Logs

No specific logs for security headers (silent success). Configuration errors logged at startup:

```
[ERROR] config.yaml:45: Invalid security header name: X-Custom-Header
[WARN] config.yaml:52: Route /api/ overrides global security headers
```

## Testing

### Unit Tests

- `tests/unit/test_security_headers.c`: 11 tests covering:
  - Configuration parsing and validation
  - Default header values
  - Per-route inheritance model
  - CORS configuration
  - HTTP/1.1 header buffer integration
  - HTTP/2 response header integration
  - Metrics increment verification

### Integration Tests

Security headers are verified in integration tests:

```bash
# Verify security headers in HTTP/1.1 response
curl -vk https://localhost:8443/ 2>&1 | grep -E "^(Strict-Transport-Security|X-Content-Type-Options|X-Frame-Options):"

# Verify CORS headers on API endpoint
curl -vk -X OPTIONS https://localhost:8443/api/test \
  -H "Origin: https://example.com" \
  2>&1 | grep "Access-Control-Allow-"
```

## Operational Notes

### Troubleshooting

**Security headers not appearing**:
1. Check `security_headers.enabled` in config.yaml (default: true)
2. Verify route doesn't have `inherit_global_headers: false`
3. Check metrics: `curl http://localhost:9090/metrics | grep security_headers`

**CORS preflight failing**:
1. Verify CORS is enabled for the route
2. Check `allow_origin` matches the requesting origin (or use `*`)
3. Ensure `allow_methods` includes OPTIONS
4. Check browser console for specific CORS error

**Header validation errors at startup**:
1. Review config.yaml line number in error message
2. Ensure header names use standard capitalization (e.g., `Content-Security-Policy`)
3. Verify header values don't contain unescaped quotes

### Tuning

**Strict CSP for production**:
```yaml
security_headers:
  headers:
    - name: "Content-Security-Policy"
      value: "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'"
```

**Relaxed CORS for internal APIs**:
```yaml
routes:
  - path: /internal/
    cors:
      enabled: true
      allow_origin: "https://admin.internal.example.com"
      allow_methods: "GET, POST, PUT, DELETE"
      allow_headers: "Authorization, Content-Type"
      allow_credentials: true
```

**Disable specific header**:
```yaml
security_headers:
  headers:
    - name: "X-XSS-Protection"
      value: ""  # Empty value effectively disables
```

## Security Considerations

### CSP Configuration

The default CSP (`default-src 'self'`) is strict and may break applications that:
- Load resources from CDNs
- Use inline scripts or styles
- Make WebSocket connections to external servers

Adjust CSP based on application needs:

```yaml
- name: "Content-Security-Policy"
  value: "default-src 'self'; script-src 'self' https://cdn.example.com; connect-src 'self' wss://api.example.com"
```

### CORS Security

**Never use `allow_origin: "*"` with `allow_credentials: true`** - this is a security vulnerability. Use specific origins:

```yaml
cors:
  allow_origin: "https://app.example.com"
  allow_credentials: true
```

### HSTS Preloading

To enable HSTS preloading in browsers, add `preload` to the HSTS header:

```yaml
- name: "Strict-Transport-Security"
  value: "max-age=63072000; includeSubDomains; preload"
```

Then submit your domain to the [HSTS preload list](https://hstspreload.org/).

## References

- [OWASP Security Headers](https://owasp.org/www-project-secure-headers/)
- [MDN CSP Documentation](https://developer.mozilla.org/en-US/docs/Web/HTTP/CSP)
- [MDN CORS Documentation](https://developer.mozilla.org/en-US/docs/Web/HTTP/CORS)
- [HSTS Preload List](https://hstspreload.org/)
