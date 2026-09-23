#!/bin/bash
if ! command -v memtester >/dev/null 2>&1; then
    echo "memtester not installed, skipping"
    exit 0
fi

TOTAL_MB=$(awk '/MemAvailable/{print int($2/1024)}' /proc/meminfo)
TEST_MB=$(( TOTAL_MB / 2 < 256 ? TOTAL_MB / 2 : 256 ))
[ "$TEST_MB" -lt 64 ] && TEST_MB=64

echo "RAM available: ${TOTAL_MB} MB"
echo "Testing: ${TEST_MB} MB, 1 pass"
echo ""
memtester "${TEST_MB}M" 1 2>&1
