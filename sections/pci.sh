#!/bin/bash
echo "--- PCI Devices ---"
lspci -v 2>/dev/null || echo "lspci not available"
