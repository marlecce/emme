#!/bin/bash
# Script per generare un certificato self-signed valido per 365 giorni
# Usa questo certificato SOLO per sviluppo e test.

CERT_DIR="../certs"
CERT_FILE="${CERT_DIR}/dev.crt"
KEY_FILE="${CERT_DIR}/dev.key"

# Crea la cartella certs se non esiste
mkdir -p "$CERT_DIR"

openssl req -x509 -nodes -days 365 -newkey rsa:2048 \
    -keyout "$KEY_FILE" -out "$CERT_FILE" \
    -subj "/C=IT/ST=Italy/L=City/O=MyCompany/OU=Dev/CN=localhost"

echo "Certificato generato:"
echo "  Certificato: $CERT_FILE"
echo "  Chiave privata: $KEY_FILE"
