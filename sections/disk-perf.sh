#!/bin/bash
if ! command -v fio >/dev/null 2>&1; then
    echo "fio not installed, skipping benchmark"
    exit 0
fi

DISKS=$(lsblk -d -n -o NAME | grep -E '^sd|^hd|^nvme')
if [ -z "$DISKS" ]; then
    echo "No disks found for benchmark"
    exit 0
fi

for DISK in $DISKS; do
    DEV="/dev/$DISK"
    SIZE=$(lsblk -d -n -o SIZE "$DEV" 2>/dev/null | tr -d ' ')
    echo "=== $DEV ($SIZE) ==="

    echo "--- Sequential Read  1M blocks  15s ---"
    fio --name=seq-read \
        --filename="$DEV" \
        --direct=1 --rw=read --bs=1M \
        --numjobs=1 --runtime=15 --time_based \
        --output-format=terse --terse-version=3 2>/dev/null \
    | awk -F';' '{
        printf "  BW: %.0f MB/s   IOPS: %s   Lat(avg): %.2f ms\n",
               $6/1024, $7, $40/1000
      }' \
    || echo "  fio failed"

    echo "--- Random Read      4K blocks   15s ---"
    fio --name=rand-read \
        --filename="$DEV" \
        --direct=1 --rw=randread --bs=4k \
        --numjobs=1 --runtime=15 --time_based \
        --output-format=terse --terse-version=3 2>/dev/null \
    | awk -F';' '{
        printf "  BW: %.0f MB/s   IOPS: %s   Lat(avg): %.2f ms\n",
               $6/1024, $7, $40/1000
      }' \
    || echo "  fio failed"

    echo ""
done
