#!/bin/bash
echo "--- All kernel errors and warnings ---"
dmesg --level=emerg,alert,crit,err,warn 2>/dev/null \
    || dmesg | grep -iE "(error|warning|fail|warn)" | head -200

echo ""
echo "--- Machine Check Exceptions (MCE) ---"
dmesg | grep -iE "(mce|machine check)" || echo "No MCE events found"

echo ""
echo "--- Disk I/O errors ---"
dmesg | grep -iE "(ata[0-9]+.*error|blk_update_request.*I/O error|SCSI error|Buffer I/O error|EXT[234]-fs error)" \
    || echo "No disk I/O errors found"

echo ""
echo "--- Memory errors (EDAC) ---"
dmesg | grep -iE "(edac|corrected error|uncorrected error|memory error)" \
    || echo "No EDAC/memory errors found"

echo ""
echo "--- Kernel taint ---"
TAINT=$(cat /proc/sys/kernel/tainted 2>/dev/null || echo 0)
echo "Taint value: $TAINT"
[ "$TAINT" != "0" ] && echo "WARNING: Kernel is tainted!" || echo "Kernel not tainted."
