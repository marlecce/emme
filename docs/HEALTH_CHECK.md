# Health Check Endpoint

## Overview

The `/health` endpoint provides a simple health check for the Emme web server. It returns HTTP 200 with a JSON response when the server is running and able to handle requests.

## Endpoint

```
GET /health
```

**Port**: Same as main HTTPS port (default: 8443)  
**Protocol**: HTTP/1.1 and HTTP/2  
**Authentication**: None (public endpoint)

## Response

### Healthy (HTTP 200)

```json
{"status":"ok"}
```

## HTTP Headers

```
Content-Type: application/json
```

## Usage Examples

### curl (HTTP/1.1)

```bash
curl -vk https://localhost:8443/health
```

### curl (HTTP/2)

```bash
curl -vk --http2 https://localhost:8443/health
```

### Load Balancer Health Checks

**HAProxy:**
```haproxy
backend emme_servers
    option httpchk GET /health
    http-check expect status 200
    server emme1 192.168.1.10:8443 check ssl verify required
```

**Nginx:**
```nginx
upstream emme {
    server 192.168.1.10:8443;
    health_check match=/health/interval=5s;
}
```

**AWS ALB:**
- Health check path: `/health`
- Protocol: HTTPS
- Expected: 200 OK

### Docker Compose

```yaml
services:
  emme:
    image: emme:latest
    healthcheck:
      test: ["CMD", "curl", "-vk", "https://localhost:8443/health"]
      interval: 30s
      timeout: 5s
      retries: 3
      start_period: 10s
```

### Prometheus Blackbox Exporter

```yaml
modules:
  emme_health:
    prober: http
    timeout: 5s
    http:
      method: GET
      tls_config:
        insecure_skip_verify: true  # For self-signed certs
      valid_status_codes: [200]
```

## Implementation Details

The health check is minimal by design:
- No metrics tracking overhead
- No atomic operations or counters
- Just proves the server can accept and respond to requests
- Works over both HTTP/1.1 and HTTP/2

## Security Considerations

### Access Control

The `/health` endpoint is **public by design** to enable:
- Load balancer health checks
- External monitoring systems

**Recommendation**: Restrict access at the network level:
- Firewall rules (allow only from load balancer IPs)
- Network policies

### Information Disclosure

The endpoint only exposes:
- Server availability (binary: up/down)

No internal metrics, version info, or system details are exposed.

## Troubleshooting

### Health Check Returns Connection Refused

**Causes**:
1. Server not running
2. Wrong port configured
3. Firewall blocking

**Solution**:
```bash
# Check server is running
ps aux | grep emme

# Check port is listening
netstat -tlnp | grep 8443

# Test connectivity
telnet localhost 8443
```

### Health Check Timeout

**Causes**:
1. Server overloaded
2. TLS handshake slow

**Solution**: 
- Increase timeout in load balancer configuration
- Check server logs for errors

## Related Documentation

- [Deployment Guide](DEPLOYMENT.md) - Production deployment and load balancer integration
- [Monitoring Setup](MONITORING.md) - Prometheus and Grafana integration
- [README](../README.md) - General project documentation
