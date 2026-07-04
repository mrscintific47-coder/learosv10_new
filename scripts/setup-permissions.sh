#!/usr/bin/env bash
# LearnOS — one-time privilege setup
# Run ONCE with: sudo bash scripts/setup-permissions.sh
#
# What this does:
#   1. Stamps the current binary with the exact Linux capabilities it needs.
#   2. Adds a narrow passwordless sudoers rule so build.sh can re-stamp
#      after every rebuild without a password prompt.
#   3. Persists perf_event_paranoid=1 across reboots.
set -euo pipefail

REPO="$(cd "$(dirname "$0")/.." && pwd)"
BINARY="$REPO/build/LearnOS"
SETCAP=/usr/sbin/setcap
CAPS="cap_sys_admin,cap_sys_ptrace,cap_perfmon,cap_net_admin+ep"
USER="${SUDO_USER:-$(logname 2>/dev/null || echo "$USER")}"
SUDOERS_FILE="/etc/sudoers.d/learnos-setcap"

echo "==> Stamping capabilities on $BINARY"
"$SETCAP" "$CAPS" "$BINARY"
getcap "$BINARY"

echo ""
echo "==> Writing passwordless sudoers rule"
# sudoers requires each argument on the command line to be a separate token.
# Capability strings with commas are not allowed inline — use a wrapper script.
WRAPPER="$REPO/scripts/setcap-learnos.sh"
cat > "$WRAPPER" <<WRAP
#!/bin/bash
/usr/sbin/setcap cap_sys_admin,cap_sys_ptrace,cap_perfmon,cap_net_admin+ep "$BINARY"
WRAP
chmod 0755 "$WRAPPER"

# The sudoers rule now points at the wrapper script (a simple path, no args).
cat > "$SUDOERS_FILE" <<EOF
# Allow $USER to re-stamp LearnOS capabilities after each rebuild, no password.
# Scoped to this wrapper script only — not blanket sudo.
$USER ALL=(root) NOPASSWD: $WRAPPER
EOF
chmod 0440 "$SUDOERS_FILE"
visudo -c -f "$SUDOERS_FILE" && echo "   Syntax OK — $SUDOERS_FILE"

echo ""
echo "==> Persisting perf_event_paranoid=1"
echo "kernel.perf_event_paranoid = 1" > /etc/sysctl.d/60-learnos.conf
sysctl -w kernel.perf_event_paranoid=1

echo ""
echo "Done. From now on:"
echo "  ./scripts/build.sh    — build + auto-stamp (no password)"
echo "  ./build/LearnOS       — run (no sudo needed)"
