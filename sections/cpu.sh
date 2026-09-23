#!/bin/bash
echo "--- CPU Info (lscpu) ---"
lscpu
echo ""
echo "--- CPU Topology (dmidecode) ---"
dmidecode -t processor 2>/dev/null || echo "dmidecode not available"
