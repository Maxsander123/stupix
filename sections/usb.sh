#!/bin/bash
echo "--- USB Devices ---"
lsusb 2>/dev/null || echo "lsusb not available"
echo ""
lsusb -v 2>/dev/null | grep -E "(idVendor|idProduct|iManufacturer|iProduct|iSerial)" || true
