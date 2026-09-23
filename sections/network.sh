#!/bin/bash
echo "--- Interfaces ---"
ip -4 addr show
echo ""
echo "--- Routing ---"
ip route show
echo ""
echo "--- DNS ---"
cat /etc/resolv.conf
