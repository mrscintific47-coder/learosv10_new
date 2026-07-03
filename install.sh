#!/usr/bin/env bash
# ============================================================
# LearnOS Install & Privilege Setup Script
# Run as root: sudo bash install.sh
# ============================================================
set -e

BINARY_DIR="$(cd "$(dirname "$0")/build" && pwd)"
BINARY="$BINARY_DIR/LearnOS"

echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║         LearnOS — Install & Privilege Setup          ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""

# ── 0. Check we're root ──────────────────────────────────────
if [ "$(id -u)" != "0" ]; then
    echo "ERROR: This script must be run as root (sudo bash install.sh)"
    exit 1
fi

# ── 1. Check binary exists ───────────────────────────────────
if [ ! -f "$BINARY" ]; then
    echo "Building LearnOS first..."
    cd "$(dirname "$0")"
    mkdir -p build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release
    make -j"$(nproc)"
    cd ..
    BINARY_DIR="$(pwd)/build"
    BINARY="$BINARY_DIR/LearnOS"
fi

echo "[1/6] Binary: $BINARY"

# ── 2. Install libcap tools if needed ───────────────────────
if ! command -v setcap &>/dev/null; then
    echo "[2/6] Installing libcap2-bin..."
    apt-get install -y libcap2-bin
else
    echo "[2/6] libcap2-bin already installed ✓"
fi

# ── 3. Set capabilities on the binary ───────────────────────
#
# CAP_SYS_NICE   — setpriority() on any PID, sched_setattr()
# CAP_SYS_ADMIN  — /proc/sys writes, unshare(), cgroup v2, perf_event_open
# CAP_NET_ADMIN  — network namespace, tc (traffic control)
# CAP_IPC_LOCK   — mlock() large regions for memory experiments
# CAP_PERFMON    — perf_event_open() hardware counters (kernel >= 5.8)
# CAP_KILL       — send signals to processes owned by other users
#
echo "[3/6] Setting Linux capabilities on LearnOS binary..."
setcap cap_sys_nice,cap_sys_admin,cap_net_admin,cap_ipc_lock,cap_kill+ep "$BINARY"
setcap cap_sys_nice,cap_sys_admin,cap_net_admin,cap_ipc_lock,cap_kill+ep "$BINARY_DIR/memlab_worker"
setcap cap_sys_nice,cap_sys_admin,cap_net_admin,cap_ipc_lock,cap_kill+ep "$BINARY_DIR/dslab_worker"

# CAP_PERFMON is kernel >= 5.8; try, don't fail on older kernels
setcap cap_sys_nice,cap_sys_admin,cap_net_admin,cap_ipc_lock,cap_kill,cap_perfmon+ep "$BINARY" 2>/dev/null || true

echo "    Capabilities set ✓"
getcap "$BINARY"

# ── 4. Mount debugfs (for ftrace / observability lab) ───────
echo "[4/6] Checking debugfs..."
if ! mountpoint -q /sys/kernel/debug 2>/dev/null; then
    echo "    Mounting debugfs at /sys/kernel/debug..."
    mount -t debugfs none /sys/kernel/debug
    # Make it persistent
    if ! grep -q "debugfs" /etc/fstab; then
        echo "none /sys/kernel/debug debugfs defaults 0 0" >> /etc/fstab
        echo "    Added debugfs to /etc/fstab ✓"
    fi
else
    echo "    debugfs already mounted ✓"
fi

# Enable ftrace
echo 1 > /sys/kernel/debug/tracing/tracing_on 2>/dev/null || true
echo "    ftrace enabled ✓"

# ── 5. Setup cgroup v2 for sandbox isolation ────────────────
echo "[5/6] Setting up cgroup v2 for sandbox..."
# Make sure cgroup v2 is mounted
if ! mountpoint -q /sys/fs/cgroup 2>/dev/null; then
    mount -t cgroup2 none /sys/fs/cgroup
fi
# Create learnos cgroup hierarchy if not exists
mkdir -p /sys/fs/cgroup/learnos
# Enable controllers for the learnos subtree
CONTROLLERS="cpu memory pids"
for ctrl in $CONTROLLERS; do
    if grep -q "$ctrl" /sys/fs/cgroup/cgroup.controllers 2>/dev/null; then
        echo "+$ctrl" > /sys/fs/cgroup/cgroup.subtree_control 2>/dev/null || true
    fi
done
chmod 777 /sys/fs/cgroup/learnos 2>/dev/null || true
echo "    cgroup v2 ready at /sys/fs/cgroup/learnos ✓"

# ── 6. perf_event_paranoia — allow perf counters ────────────
echo "[6/6] Configuring perf_event_paranoia..."
CURRENT_PARANOIA=$(cat /proc/sys/kernel/perf_event_paranoid 2>/dev/null || echo "3")
if [ "$CURRENT_PARANOIA" -gt 1 ]; then
    echo 1 > /proc/sys/kernel/perf_event_paranoid
    # Make persistent
    if ! grep -q "perf_event_paranoid" /etc/sysctl.conf 2>/dev/null; then
        echo "kernel.perf_event_paranoid = 1" >> /etc/sysctl.conf
    fi
    echo "    perf_event_paranoid set to 1 (was $CURRENT_PARANOIA) ✓"
else
    echo "    perf_event_paranoid = $CURRENT_PARANOIA (already permissive) ✓"
fi

# ── Summary ──────────────────────────────────────────────────
echo ""
echo "╔══════════════════════════════════════════════════════╗"
echo "║                   Setup Complete!                    ║"
echo "╚══════════════════════════════════════════════════════╝"
echo ""
echo "  Binary:      $BINARY"
echo "  Run with:    $BINARY"
echo "  (No sudo needed — capabilities are set on the binary)"
echo ""
echo "  What's enabled:"
echo "  ✓ /proc/sys writes — kernel parameter sliders work live"
echo "  ✓ setpriority() — nice values apply to any PID"
echo "  ✓ Namespace isolation — clone(CLONE_NEWPID|...) in Namespace Lab"
echo "  ✓ debugfs + ftrace — Observability Lab kernel traces"
echo "  ✓ perf_event_open — hardware counters in Observability Lab"
echo "  ✓ cgroup v2 — Sandbox CPU/memory hard limits"
echo "  ✓ kill() any PID — Signal Panel works on all processes"
echo ""
