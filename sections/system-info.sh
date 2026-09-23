#!/bin/bash
echo "--- Hostname ---"
hostname
echo ""
echo "--- Kernel ---"
uname -a
echo ""
echo "--- Uptime ---"
uptime
echo ""
echo "--- System (dmidecode) ---"
dmidecode -t system 2>/dev/null || echo "dmidecode not available"
echo ""
echo "--- BIOS ---"
dmidecode -t bios 2>/dev/null || echo "dmidecode not available"
