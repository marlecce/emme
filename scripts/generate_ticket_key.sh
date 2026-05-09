#!/bin/bash
# Generate 80-byte session ticket key for TLS session resumption
# Usage: sudo ./scripts/generate_ticket_key.sh

set -e

TICKET_DIR="/etc/emme"
TICKET_FILE="${TICKET_DIR}/ticket.key"

# Create directory if it doesn't exist
if [ ! -d "$TICKET_DIR" ]; then
    mkdir -p "$TICKET_DIR"
    echo "Created directory: $TICKET_DIR"
fi

# Generate 80 bytes of random data for AES-256 session ticket encryption
openssl rand 80 > "$TICKET_FILE"

# Set secure permissions (owner read/write only)
chmod 600 "$TICKET_FILE"
chown root:root "$TICKET_FILE" 2>/dev/null || true

echo "Session ticket key generated: $TICKET_FILE"
echo "Key size: 80 bytes (AES-256)"
echo "Permissions: 600 (owner read/write only)"
echo ""
echo "Add to config.yaml:"
echo "  ssl:"
echo "    session_ticket_key: $TICKET_FILE"
echo ""
echo "For security, rotate this key every 24 hours via cron:"
echo "  0 0 * * * $TICKET_DIR/generate_ticket_key.sh"
