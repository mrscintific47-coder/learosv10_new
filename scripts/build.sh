#!/usr/bin/env bash
# LearnOS build wrapper — build then re-stamp capabilities.
# Usage: ./scripts/build.sh [extra cmake --build flags]
#
# Requires setup-permissions.sh to have been run once first.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$REPO/build/LearnOS"
CAPS="cap_sys_admin,cap_sys_ptrace,cap_perfmon,cap_net_admin+ep"
SETCAP=/usr/sbin/setcap

cd "$REPO"

echo "==> Building…"
cmake --build build --parallel "$(nproc)" "$@"

echo "==> Re-stamping capabilities (passwordless sudo rule)…"
sudo "$SETCAP" "$CAPS" "$BINARY"
echo "    $(getcap "$BINARY")"

echo ""
echo "Ready. Run with: ./build/LearnOS"
