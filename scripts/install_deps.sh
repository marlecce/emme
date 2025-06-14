#!/bin/bash
set -e

echo "Detecting OS and installing dependencies..."

if [ -f /etc/debian_version ]; then
    # Debian/Ubuntu
    sudo apt-get update
    sudo apt-get install -y build-essential liburing-dev libssl-dev libyaml-dev libnghttp2-dev pkg-config
elif [ -f /etc/fedora-release ]; then
    # Fedora
    sudo dnf install -y gcc make liburing-devel openssl-devel libyaml-devel nghttp2-devel pkgconf-pkg-config
elif [ -f /etc/arch-release ]; then
    # Arch Linux
    sudo pacman -Sy --noconfirm base-devel liburing openssl libyaml nghttp2 pkgconf
else
    echo "Unsupported OS. Please install the following dependencies manually:"
    echo "  - build tools (gcc, make)"
    echo "  - liburing-dev"
    echo "  - libssl-dev"
    echo "  - libyaml-dev"
    echo "  - libnghttp2-dev"
    exit 1
fi

echo "All dependencies installed!"