#!/usr/bin/env bash
# ============================================================
# LearnOS Install Script
# Run as root: sudo bash install.sh
#
# Delegates all capability-stamping and system setup to
# scripts/setup-permissions.sh — that is the single source of
# truth for which capabilities are granted and why.
# ============================================================
set -e

if [ "$(id -u)" != "0" ]; then
    echo "ERROR: Run as root: sudo bash install.sh"
    exit 1
fi

REPO="$(cd "$(dirname "$0")" && pwd)"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║         LearnOS — Install & Privilege Setup          ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

exec bash "$REPO/scripts/setup-permissions.sh"
