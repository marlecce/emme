# Production Deployment Guide

## Overview

Emme is designed for high-performance deployment on bare metal and virtual machines. For maximum performance, avoid container orchestration platforms that add network overhead and restrict io_uring capabilities.

## Target Environments

**Recommended:**
- ✅ Bare metal servers (maximum performance)
- ✅ Cloud VMs (EC2, GCE, Azure VMs)
- ✅ Edge servers with direct NIC access

**Not Recommended:**
- ❌ Kubernetes (NAT overhead, io_uring restrictions)
- ❌ Container-heavy environments
- ❌ Multi-tenant shared hosts

## Installation

### Manual Installation

```bash
# Build from source
git clone https://github.com/your-org/emme.git
cd emme
make clean && make

# Install binary
sudo cp emme /usr/local/bin/
sudo chmod +x /usr/local/bin/emme
```

### Systemd Service

Create `/etc/systemd/system/emme.service`:

```ini
[Unit]
Description=Emme HTTPS Web Server
After=network.target

[Service]
Type=simple
User=www-data
Group=www-data
ExecStart=/usr/local/bin/emme --config /etc/emme/config.yaml
Restart=on-failure
RestartSec=5

# Security hardening
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
ReadWritePaths=/var/log/emme

# Performance tuning
LimitNOFILE=65536
LimitNPROC=4096

[Install]
WantedBy=multi-user.target
```

Enable and start:

```bash
sudo systemctl daemon-reload
sudo systemctl enable emme
sudo systemctl start emme
sudo systemctl status emme
```

## Configuration

### Environment Variables

Override configuration at runtime:

| Variable | Description | Default |
|----------|-------------|---------|
| `EMME_CONFIG_PATH` | Path to config.yaml | `config.yaml` |
| `EMME_PORT` | HTTPS listen port | `8443` |
| `EMME_LOG_LEVEL` | Log level (debug/info/warn/error) | `info` |
| `EMME_LOG_FILE` | Log file path | `/var/log/emme/emme.log` |

Example:
```bash
EMME_PORT=9443 EMME_LOG_LEVEL=debug ./emme
```

### Configuration File

Location: `/etc/emme/config.yaml`

```yaml
server:
  port: 8443
  max_connections: 10000
  keep_alive_timeout: 30

tls:
  certificate: /etc/ssl/certs/emme.crt
  private_key: /etc/ssl/private/emme.key
  min_version: TLS1.2

logging:
  level: info
  file: /var/log/emme/emme.log
  rotation:
    max_size_mb: 100
    max_files: 7
```

## Load Balancer Integration

### HAProxy

```haproxy
global
    log /dev/log local0
    maxconn 4096

defaults
    log global
    mode http
    option httplog
    timeout connect 5s
    timeout client 50s
    timeout server 50s

frontend https_front
    bind *:443 ssl crt /etc/ssl/certs/emme.pem
    default_backend emme_servers

backend emme_servers
    balance roundrobin
    option httpchk GET /health
    http-check expect status 200
    server emme1 192.168.1.10:8443 check ssl verify required
    server emme2 192.168.1.11:8443 check ssl verify required
```

### Nginx (Reverse Proxy)

```nginx
upstream emme {
    least_conn;
    server 192.168.1.10:8443;
    server 192.168.1.11:8443;
    keepalive 32;
}

server {
    listen 443 ssl http2;
    server_name example.com;

    ssl_certificate /etc/ssl/certs/emme.crt;
    ssl_certificate_key /etc/ssl/private/emme.key;

    location / {
        proxy_pass https://emme;
        proxy_ssl_verify off;  # For self-signed certs
        proxy_http_version 1.1;
        proxy_set_header Connection "";
        
        # Health check
        health_check match=/health/interval=5s;
    }
}
```

### AWS Application Load Balancer

1. Create target group:
   - Protocol: HTTPS
   - Port: 8443
   - Health check path: `/health`
   - Health check protocol: HTTPS
   - Expected: 200 OK

2. Register EC2 instances with target group

3. Configure listener on port 443

## Health Checks

The `/health` endpoint returns HTTP 200 when the server is running:

```bash
curl -vk https://localhost:8443/health
```

Response:
```json
{"status":"ok"}
```

See [HEALTH_CHECK.md](HEALTH_CHECK.md) for detailed health check integration examples.

## Performance Tuning

### System Limits

Increase file descriptor limits in `/etc/security/limits.conf`:

```
www-data soft nofile 65536
www-data hard nofile 65536
```

### Kernel Tuning

For high-throughput workloads, add to `/etc/sysctl.conf`:

```conf
# Increase connection backlog
net.core.somaxconn = 65536

# Optimize TCP for high performance
net.ipv4.tcp_max_syn_backlog = 65536
net.ipv4.tcp_fastopen = 3
net.ipv4.tcp_tw_reuse = 1

# Increase buffer sizes
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
```

Apply: `sudo sysctl -p`

### io_uring Verification

Verify io_uring is available:

```bash
# Check kernel version (5.10+ recommended)
uname -r

# Check io_uring support
cat /proc/version
```

## TLS Configuration

### Production Certificates

**Never use development certificates in production.**

Obtain certificates from:
- Let's Encrypt (free, automated)
- DigiCert, GlobalSign (enterprise)
- AWS ACM (for ALB integration)

Example with Let's Encrypt:
```bash
sudo apt install certbot
sudo certbot certonly --standalone -d example.com
```

Update config.yaml:
```yaml
tls:
  certificate: /etc/letsencrypt/live/example.com/fullchain.pem
  private_key: /etc/letsencrypt/live/example.com/privkey.pem
```

### Recommended TLS Settings

Minimum TLS 1.2 with strong ciphers:
```yaml
tls:
  min_version: TLS1.2
  ciphers: |
    TLS_AES_256_GCM_SHA384:
    TLS_CHACHA20_POLY1305_SHA256:
    TLS_AES_128_GCM_SHA256:
    ECDHE-RSA-AES256-GCM-SHA384:
    ECDHE-RSA-AES128-GCM-SHA256
```

## Logging

### Log Location

Default: `/var/log/emme/emme.log`

### Log Rotation

Systemd journal rotation in `/etc/systemd/journald.conf`:

```conf
[Journal]
SystemMaxUse=1G
SystemKeepFree=5G
MaxFileSec=day
```

### Log Levels

- `debug`: Development and troubleshooting
- `info`: Production default
- `warn`: Warnings only
- `error`: Errors only (minimal logging)

## Graceful Shutdown

Emme handles SIGTERM gracefully:

1. Stop accepting new connections
2. Complete in-flight requests (30s timeout)
3. Clean shutdown

```bash
# Graceful restart
sudo systemctl reload emme

# Graceful stop
sudo systemctl stop emme
```

## Monitoring

### Health Check Monitoring

Integrate with your monitoring system:

- **Prometheus Blackbox**: Probe `/health` endpoint
- **Nagios/Icinga**: HTTP SSL certificate check
- **Datadog**: HTTP check with SSL verification

### Metrics

For detailed metrics (request rate, latency, errors), see [MONITORING.md](MONITORING.md).

## Troubleshooting

### Server Won't Start

```bash
# Check logs
sudo journalctl -u emme -f

# Verify port is available
sudo netstat -tlnp | grep 8443

# Test config
emme --config /etc/emme/config.yaml --test
```

### High Latency

1. Check thread pool utilization in logs
2. Verify io_uring queue depths
3. Monitor active connections: `netstat -an | grep 8443 | wc -l`

### TLS Handshake Failures

1. Verify certificate chain is complete
2. Check client TLS version support
3. Review cipher suite compatibility

```bash
# Test TLS configuration
openssl s_client -connect localhost:8443 -tls1_2
```

## Security Hardening

### Firewall Rules

Allow only necessary ports:

```bash
sudo ufw allow 443/tcp    # HTTPS
sudo ufw allow 8443/tcp   # Emme direct (if behind proxy)
sudo ufw deny 9090/tcp    # Metrics (internal only)
```

### User Permissions

Run as unprivileged user:

```bash
sudo useradd -r -s /bin/false www-data
sudo chown www-data:www-data /var/log/emme
```

### File Permissions

```bash
sudo chmod 600 /etc/ssl/private/emme.key
sudo chmod 644 /etc/ssl/certs/emme.crt
sudo chmod 755 /usr/local/bin/emme
```

## Backup and Recovery

### Backup Checklist

- [ ] Configuration files (`/etc/emme/`)
- [ ] TLS certificates and keys
- [ ] Custom static content
- [ ] Log rotation configuration

### Recovery Procedure

1. Install emme binary
2. Restore configuration
3. Restore certificates
4. Start service
5. Verify health check

```bash
sudo systemctl start emme
curl -vk https://localhost:8443/health
```

## Related Documentation

- [Health Check Endpoint](HEALTH_CHECK.md)
- [Monitoring Setup](MONITORING.md)
- [README](../README.md)
