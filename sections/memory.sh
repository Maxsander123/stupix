#!/bin/bash
echo "--- Memory Overview ---"
free -h
echo ""
echo "--- Memory Modules (dmidecode) ---"
dmidecode -t memory 2>/dev/null || echo "dmidecode not available"
