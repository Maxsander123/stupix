#!/bin/bash
# stupix-bench: scored benchmark + health checks for CPU, memory, disks,
# GPU, chipset/PCIe, BMC, thermals and power (see bench/README.md).
# Raw disks are tested read-only; the file test is skipped because the
# live system's root filesystem lives in RAM.
# JSON report: /var/log/stupix/benchmark.json
# Skip with STUPIX_SKIP_BENCH=1 or the kernel parameter stupix.nobench.
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

if [ "${STUPIX_SKIP_BENCH:-0}" = 1 ] || grep -qw stupix.nobench /proc/cmdline 2>/dev/null; then
    echo "benchmark skipped"
    exit 0
fi

bash "$SCRIPT_DIR/../bench/run.sh" --no-color --no-disk-file --disk-dev auto \
    --json /var/log/stupix/benchmark.json
