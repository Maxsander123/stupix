#!/bin/bash
for disk in $(lsblk -d -n -o NAME | grep -E '^sd|^hd|^nvme'); do
    echo "--- /dev/$disk ---"
    smartctl -a "/dev/$disk" 2>/dev/null || echo "smartctl failed for /dev/$disk"
    echo ""
done
