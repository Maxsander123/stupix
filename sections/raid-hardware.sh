#!/bin/bash
echo "--- RAID controllers detected via lspci ---"
CTLS=$(lspci 2>/dev/null | grep -iE "(megaraid|lsi|broadcom.*raid|smart array|adaptec|areca|3ware|highpoint|sas.*hba|hba)")
[ -n "$CTLS" ] && echo "$CTLS" || echo "No hardware RAID controllers found via lspci"

echo ""
echo "--- LSI/Broadcom MegaRAID / Dell PERC (storcli/perccli/megacli) ---"
if   command -v storcli64  >/dev/null 2>&1; then storcli64  /call show all 2>/dev/null
elif command -v storcli    >/dev/null 2>&1; then storcli    /call show all 2>/dev/null
elif command -v perccli64  >/dev/null 2>&1; then perccli64  /call show all 2>/dev/null
elif command -v perccli    >/dev/null 2>&1; then perccli    /call show all 2>/dev/null
elif command -v megacli    >/dev/null 2>&1; then
    megacli -AdpAllInfo -aALL 2>/dev/null
    megacli -PDList -aALL 2>/dev/null
    megacli -LDInfo -Lall -aALL 2>/dev/null
else
    echo "Not found. Download: https://www.broadcom.com/support/download-search (StorCLI)"
fi

echo ""
echo "--- HP/HPE Smart Array (ssacli/hpssacli) ---"
if   command -v ssacli    >/dev/null 2>&1; then ssacli    ctrl all show config detail 2>/dev/null
elif command -v hpssacli  >/dev/null 2>&1; then hpssacli  ctrl all show config detail 2>/dev/null
elif command -v hpacucli  >/dev/null 2>&1; then hpacucli  ctrl all show config detail 2>/dev/null
else
    echo "Not found. Download: https://support.hpe.com (HPE Smart Storage Administrator)"
fi

echo ""
echo "--- Adaptec (arcconf) ---"
if command -v arcconf >/dev/null 2>&1; then
    arcconf GETCONFIG 1 2>/dev/null
else
    echo "Not found. Download: https://storage.microsemi.com/en-us/speed/raid/storage_manager/"
fi

echo ""
echo "--- Areca (cli64) ---"
if command -v cli64 >/dev/null 2>&1; then
    cli64 hw info 2>/dev/null
    cli64 disk info 2>/dev/null
else
    echo "Not found."
fi

echo ""
echo "--- Software RAID (mdadm) ---"
mdadm --detail --scan 2>/dev/null || echo "No software RAID"
cat /proc/mdstat 2>/dev/null || true
