#!/bin/bash
echo "--- Block Devices ---"
lsblk -o NAME,SIZE,TYPE,MODEL,SERIAL,TRAN
echo ""
echo "--- NVMe Devices ---"
nvme list 2>/dev/null || echo "No NVMe devices or nvme-cli not available"
echo ""
echo "--- SCSI Devices ---"
lsscsi 2>/dev/null || echo "lsscsi not available"
echo ""
echo "--- Software RAID (mdadm) ---"
mdadm --detail --scan 2>/dev/null || echo "No software RAID found"
cat /proc/mdstat 2>/dev/null || true
echo ""
echo "--- Disk Partitions ---"
fdisk -l 2>/dev/null || echo "fdisk not available"
