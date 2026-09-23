#!/bin/bash
# =====================================================
# Stupix auto.sh
# Automatically executed after boot and git clone.
# All output is written to /var/log/stupix/
# =====================================================

LOG_DIR="/var/log/stupix"
mkdir -p "$LOG_DIR"
chmod 755 "$LOG_DIR"

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_DIR/auto.log"
}

run_section() {
    local NAME="$1"
    local FILE="$LOG_DIR/${NAME}.log"
    shift
    log "Running section: $NAME -> $FILE"
    {
        echo "=== $NAME ==="
        echo "Timestamp: $(date)"
        echo ""
        "$@"
        echo ""
    } > "$FILE" 2>&1
    log "Section $NAME done."
}

log "=== auto.sh started ==="
log "Output directory: $LOG_DIR"

# ===== NETWORK =====
run_section "network" bash -c "
    echo '--- Interfaces ---'
    ip -4 addr show
    echo ''
    echo '--- Routing ---'
    ip route show
    echo ''
    echo '--- DNS ---'
    cat /etc/resolv.conf
"

# ===== SYSTEM INFO =====
run_section "system-info" bash -c "
    echo '--- Hostname ---'
    hostname
    echo ''
    echo '--- Kernel ---'
    uname -a
    echo ''
    echo '--- Uptime ---'
    uptime
    echo ''
    echo '--- System Manufacturer and Model ---'
    dmidecode -t system 2>/dev/null || echo 'dmidecode not available'
    echo ''
    echo '--- BIOS ---'
    dmidecode -t bios 2>/dev/null || echo 'dmidecode not available'
"

# ===== CPU =====
run_section "cpu" bash -c "
    echo '--- CPU Info ---'
    lscpu
    echo ''
    echo '--- CPU Topology (dmidecode) ---'
    dmidecode -t processor 2>/dev/null || echo 'dmidecode not available'
"

# ===== MEMORY =====
run_section "memory" bash -c "
    echo '--- Memory Overview ---'
    free -h
    echo ''
    echo '--- Memory Modules (dmidecode) ---'
    dmidecode -t memory 2>/dev/null || echo 'dmidecode not available'
"

# ===== STORAGE =====
run_section "storage" bash -c "
    echo '--- Block Devices ---'
    lsblk -o NAME,SIZE,TYPE,MODEL,SERIAL,TRAN
    echo ''
    echo '--- NVMe Devices ---'
    nvme list 2>/dev/null || echo 'No NVMe devices or nvme-cli not available'
    echo ''
    echo '--- SCSI Devices ---'
    lsscsi 2>/dev/null || echo 'lsscsi not available'
    echo ''
    echo '--- Software RAID (mdadm) ---'
    mdadm --detail --scan 2>/dev/null || echo 'No software RAID found'
    cat /proc/mdstat 2>/dev/null || true
    echo ''
    echo '--- Disk partitions ---'
    fdisk -l 2>/dev/null || echo 'fdisk not available'
"

# ===== SMART =====
run_section "smart" bash -c "
    for disk in \$(lsblk -d -n -o NAME | grep -E '^sd|^hd|^nvme'); do
        echo \"--- /dev/\$disk ---\"
        smartctl -a /dev/\$disk 2>/dev/null || echo \"smartctl failed for /dev/\$disk\"
        echo ''
    done
"

# ===== PCI DEVICES =====
run_section "pci" bash -c "
    echo '--- PCI Devices ---'
    lspci -v 2>/dev/null || echo 'lspci not available'
"

# ===== USB DEVICES =====
run_section "usb" bash -c "
    echo '--- USB Devices ---'
    lsusb 2>/dev/null || echo 'lsusb not available'
"

# ===== IPMI / BMC =====
run_section "ipmi" bash -c "
    echo '--- Loading IPMI kernel modules ---'
    modprobe ipmi_si 2>/dev/null && echo 'ipmi_si loaded' || echo 'ipmi_si failed'
    modprobe ipmi_devintf 2>/dev/null && echo 'ipmi_devintf loaded' || echo 'ipmi_devintf failed'
    modprobe ipmi_msghandler 2>/dev/null && echo 'ipmi_msghandler loaded' || echo 'ipmi_msghandler failed'
    sleep 1
    echo ''
    echo '--- Chassis Status ---'
    ipmitool chassis status 2>/dev/null || echo 'ipmitool chassis status failed'
    echo ''
    echo '--- BMC Info ---'
    ipmitool bmc info 2>/dev/null || echo 'ipmitool bmc info failed'
    echo ''
    echo '--- LAN Channel ---'
    ipmitool lan print 1 2>/dev/null || echo 'ipmitool lan print failed'
    echo ''
    echo '--- FRU Inventory ---'
    ipmitool fru print 2>/dev/null || echo 'ipmitool fru print failed'
    echo ''
    echo '--- Sensor Data Records ---'
    ipmitool sdr list 2>/dev/null || echo 'ipmitool sdr list failed'
    echo ''
    echo '--- System Event Log (last 20 entries) ---'
    ipmitool sel list 2>/dev/null | tail -20 || echo 'ipmitool sel list failed'
    echo ''
    echo '--- freeipmi bmc-info ---'
    bmc-info 2>/dev/null || echo 'bmc-info not available'
    echo ''
    echo '--- freeipmi ipmi-sensors (first 40 lines) ---'
    ipmi-sensors 2>/dev/null | head -40 || echo 'ipmi-sensors not available'
"

# ===== FULL HARDWARE INVENTORY =====
run_section "hardware-full" bash -c "
    echo '--- lshw full output ---'
    lshw 2>/dev/null || echo 'lshw not available'
"

# ===== DMESG ERROR ANALYSIS =====
run_section "dmesg-errors" bash -c "
    echo '--- All kernel errors and warnings ---'
    dmesg --level=emerg,alert,crit,err,warn 2>/dev/null \
        || dmesg | grep -iE '(error|warning|fail|warn)' | head -200
    echo ''
    echo '--- Machine Check Exceptions (MCE) ---'
    dmesg | grep -iE '(mce|machine check)' || echo 'No MCE events found'
    echo ''
    echo '--- Disk I/O errors ---'
    dmesg | grep -iE '(ata[0-9]+.*error|blk_update_request.*I/O error|SCSI error|Buffer I/O error|EXT[234]-fs error)' \
        || echo 'No disk I/O errors found'
    echo ''
    echo '--- Memory errors (EDAC) ---'
    dmesg | grep -iE '(edac|corrected error|uncorrected error|memory error)' \
        || echo 'No EDAC/memory errors found'
    echo ''
    echo '--- Kernel taint flags ---'
    TAINT=\$(cat /proc/sys/kernel/tainted 2>/dev/null)
    echo \"Taint value: \${TAINT:-0}\"
    if [ \"\${TAINT:-0}\" != '0' ]; then
        echo 'WARNING: Kernel is tainted!'
    fi
"

# ===== NIC LINK STATUS + LLDP =====
run_section "nic-link" bash -c "
    echo '--- NIC link status (ethtool) ---'
    for NIC in \$(ls /sys/class/net/ | grep -v '^lo$'); do
        echo \"  Interface: \$NIC\"
        ethtool \$NIC 2>/dev/null \
            | grep -E '(Speed|Duplex|Link detected|Auto-negotiation|Port)' \
            | sed 's/^/    /' \
            || echo '    ethtool not available'
        DRIVER=\$(ethtool -i \$NIC 2>/dev/null | grep '^driver:' | awk '{print \$2}')
        [ -n \"\$DRIVER\" ] && echo \"    Driver: \$DRIVER\"
        echo ''
    done
    echo ''
    echo '--- LLDP neighbors ---'
    if command -v lldpcli >/dev/null 2>&1; then
        lldpcli show neighbors 2>/dev/null || echo 'No LLDP neighbors found (lldpd may need more time)'
        echo ''
        lldpcli show interfaces 2>/dev/null || true
    else
        echo 'lldpcli not available (install lldpd)'
    fi
"

# ===== HARDWARE RAID DETECTION =====
run_section "raid-hardware" bash -c "
    echo '--- RAID controllers detected via lspci ---'
    RAID_CTLS=\$(lspci 2>/dev/null | grep -iE '(megaraid|lsi|broadcom.*raid|hp.*smart array|adaptec|areca|3ware|highpoint|sas|hba)')
    if [ -n \"\$RAID_CTLS\" ]; then
        echo \"\$RAID_CTLS\"
    else
        echo 'No hardware RAID controllers found via lspci'
    fi
    echo ''

    echo '--- LSI/Broadcom MegaRAID (storcli) ---'
    if command -v storcli64 >/dev/null 2>&1; then
        storcli64 /call show all 2>/dev/null
    elif command -v storcli >/dev/null 2>&1; then
        storcli /call show all 2>/dev/null
    elif command -v perccli64 >/dev/null 2>&1; then
        perccli64 /call show all 2>/dev/null
    elif command -v megacli >/dev/null 2>&1; then
        megacli -AdpAllInfo -aALL 2>/dev/null
        megacli -PDList -aALL 2>/dev/null
        megacli -LDInfo -Lall -aALL 2>/dev/null
    else
        echo 'storcli/perccli/megacli not found'
        echo 'To install: download storcli from https://www.broadcom.com/support/download-search'
    fi
    echo ''

    echo '--- HP Smart Array (ssacli) ---'
    if command -v ssacli >/dev/null 2>&1; then
        ssacli ctrl all show detail 2>/dev/null
        ssacli ctrl all show config detail 2>/dev/null
    elif command -v hpssacli >/dev/null 2>&1; then
        hpssacli ctrl all show detail 2>/dev/null
    elif command -v hpacucli >/dev/null 2>&1; then
        hpacucli ctrl all show detail 2>/dev/null
    else
        echo 'ssacli/hpssacli not found'
        echo 'To install: download from https://support.hpe.com'
    fi
    echo ''

    echo '--- Adaptec (arcconf) ---'
    if command -v arcconf >/dev/null 2>&1; then
        arcconf GETCONFIG 1 2>/dev/null
    else
        echo 'arcconf not found'
    fi
    echo ''

    echo '--- Areca (cli64) ---'
    if command -v cli64 >/dev/null 2>&1; then
        cli64 hw info 2>/dev/null
        cli64 disk info 2>/dev/null
    else
        echo 'cli64 not found'
    fi
    echo ''

    echo '--- Software RAID (mdadm) ---'
    mdadm --detail --scan 2>/dev/null || echo 'No software RAID'
    cat /proc/mdstat 2>/dev/null || true
"

# ===== FIO DISK BENCHMARK =====
run_section "disk-perf" bash -c "
    if ! command -v fio >/dev/null 2>&1; then
        echo 'fio not installed, skipping benchmark'
        exit 0
    fi

    DISKS=\$(lsblk -d -n -o NAME | grep -E '^sd|^hd|^nvme')
    if [ -z \"\$DISKS\" ]; then
        echo 'No disks found for benchmark'
        exit 0
    fi

    for DISK in \$DISKS; do
        DEV=\"/dev/\$DISK\"
        SIZE=\$(lsblk -d -n -o SIZE \$DEV 2>/dev/null | tr -d ' ')
        echo \"=== \$DEV (\$SIZE) ===\"
        echo ''

        echo '--- Sequential Read (1M blocks, 15s) ---'
        fio --name=seq-read \
            --filename=\$DEV \
            --direct=1 \
            --rw=read \
            --bs=1M \
            --numjobs=1 \
            --runtime=15 \
            --time_based \
            --output-format=terse \
            --terse-version=3 2>/dev/null \
        | awk -F';' '{
            read_bw  = \$6  / 1024;
            read_iops = \$7;
            read_lat  = \$40 / 1000;
            printf \"  BW: %.0f MB/s  IOPS: %s  Lat(avg): %.2f ms\n\", read_bw, read_iops, read_lat
          }' \
        || echo '  fio sequential read failed'
        echo ''

        echo '--- Random Read 4K (15s) ---'
        fio --name=rand-read \
            --filename=\$DEV \
            --direct=1 \
            --rw=randread \
            --bs=4k \
            --numjobs=1 \
            --runtime=15 \
            --time_based \
            --output-format=terse \
            --terse-version=3 2>/dev/null \
        | awk -F';' '{
            read_bw  = \$6  / 1024;
            read_iops = \$7;
            read_lat  = \$40 / 1000;
            printf \"  BW: %.0f MB/s  IOPS: %s  Lat(avg): %.2f ms\n\", read_bw, read_iops, read_lat
          }' \
        || echo '  fio random read failed'
        echo ''
    done
"

# ===== MEMTESTER QUICK CHECK =====
run_section "memtest" bash -c "
    if ! command -v memtester >/dev/null 2>&1; then
        echo 'memtester not installed, skipping'
        exit 0
    fi

    TOTAL_MB=\$(awk '/MemAvailable/{print int(\$2/1024)}' /proc/meminfo)
    # Test 256 MB or half of available RAM, whichever is smaller
    TEST_MB=\$(( TOTAL_MB / 2 < 256 ? TOTAL_MB / 2 : 256 ))
    [ \"\$TEST_MB\" -lt 64 ] && TEST_MB=64

    echo \"--- RAM available: \${TOTAL_MB} MB, testing \${TEST_MB} MB (1 pass) ---\"
    echo ''
    memtester \"\${TEST_MB}M\" 1 2>&1
"

# ===== JSON INVENTORY REPORT =====
log "Generating JSON inventory report..."
{
    # Collect raw values
    TS=$(date -u '+%Y-%m-%dT%H:%M:%SZ')
    HN=$(hostname)
    KN=$(uname -r)

    SYS_MFR=$(dmidecode -s system-manufacturer 2>/dev/null | head -1 | sed 's/"/\\"/g')
    SYS_PROD=$(dmidecode -s system-product-name 2>/dev/null | head -1 | sed 's/"/\\"/g')
    SYS_SERIAL=$(dmidecode -s system-serial-number 2>/dev/null | head -1 | sed 's/"/\\"/g')
    SYS_UUID=$(dmidecode -s system-uuid 2>/dev/null | head -1 | sed 's/"/\\"/g')
    BIOS_VER=$(dmidecode -s bios-version 2>/dev/null | head -1 | sed 's/"/\\"/g')
    BIOS_DATE=$(dmidecode -s bios-release-date 2>/dev/null | head -1 | sed 's/"/\\"/g')

    CPU_MODEL=$(lscpu 2>/dev/null | awk -F': +' '/^Model name/{print $2; exit}' | sed 's/"/\\"/g')
    CPU_SOCKETS=$(lscpu 2>/dev/null | awk '/^Socket\(s\)/{print $2; exit}')
    CPU_CORES_PER=$(lscpu 2>/dev/null | awk '/^Core\(s\) per socket/{print $4; exit}')
    CPU_THREADS_PER=$(lscpu 2>/dev/null | awk '/^Thread\(s\) per core/{print $4; exit}')
    CPU_SOCKETS=${CPU_SOCKETS:-0}
    CPU_CORES_PER=${CPU_CORES_PER:-0}
    CPU_THREADS_PER=${CPU_THREADS_PER:-0}
    CPU_TOTAL_CORES=$(( CPU_SOCKETS * CPU_CORES_PER ))
    CPU_TOTAL_THREADS=$(( CPU_SOCKETS * CPU_CORES_PER * CPU_THREADS_PER ))

    MEM_TOTAL_MB=$(awk '/^MemTotal/{print int($2/1024)}' /proc/meminfo 2>/dev/null)
    MEM_AVAIL_MB=$(awk '/^MemAvailable/{print int($2/1024)}' /proc/meminfo 2>/dev/null)
    MEM_TOTAL_MB=${MEM_TOTAL_MB:-0}
    MEM_AVAIL_MB=${MEM_AVAIL_MB:-0}

    BMC_IP=$(ipmitool lan print 1 2>/dev/null | awk '/^IP Address  /{print $NF}')
    BMC_IP=${BMC_IP:-"unknown"}

    MCE_COUNT=$(dmesg 2>/dev/null | grep -icE 'mce|machine check' || echo 0)
    IO_ERR_COUNT=$(dmesg 2>/dev/null | grep -icE 'I/O error|ata.*error|Buffer I/O' || echo 0)
    EDAC_COUNT=$(dmesg 2>/dev/null | grep -icE 'edac|corrected error|uncorrected' || echo 0)
    TAINT=$(cat /proc/sys/kernel/tainted 2>/dev/null || echo 0)

    # Build interface array
    NIC_JSON="["
    FIRST=1
    for NIC in $(ls /sys/class/net/ 2>/dev/null | grep -v '^lo$'); do
        IP=$(ip -4 addr show "$NIC" 2>/dev/null | awk '/inet /{print $2; exit}')
        MAC=$(cat "/sys/class/net/$NIC/address" 2>/dev/null | head -1)
        SPEED=$(ethtool "$NIC" 2>/dev/null | awk '/Speed:/{print $2}')
        LINK=$(ethtool "$NIC" 2>/dev/null | awk '/Link detected/{print $3}')
        [ "$FIRST" -ne 1 ] && NIC_JSON="${NIC_JSON},"
        NIC_JSON="${NIC_JSON}{\"name\":\"${NIC}\",\"ip\":\"${IP:-}\",\"mac\":\"${MAC:-}\",\"speed\":\"${SPEED:-}\",\"link\":\"${LINK:-}\"}"
        FIRST=0
    done
    NIC_JSON="${NIC_JSON}]"

    # Build disk array
    DISK_JSON="["
    FIRST=1
    while IFS='|' read -r NAME SIZE TYPE MODEL SERIAL TRAN; do
        [ "$FIRST" -ne 1 ] && DISK_JSON="${DISK_JSON},"
        MODEL=$(echo "$MODEL" | sed 's/"/\\"/g')
        DISK_JSON="${DISK_JSON}{\"name\":\"${NAME}\",\"size\":\"${SIZE}\",\"type\":\"${TYPE}\",\"model\":\"${MODEL}\",\"serial\":\"${SERIAL}\",\"transport\":\"${TRAN}\"}"
        FIRST=0
    done < <(lsblk -d -n -o NAME,SIZE,TYPE,MODEL,SERIAL,TRAN 2>/dev/null | awk '{printf "%s|%s|%s|%s|%s|%s\n",$1,$2,$3,$4,$5,$6}')
    DISK_JSON="${DISK_JSON}]"

    cat > "$LOG_DIR/inventory.json" <<JSONEOF
{
  "generated": "${TS}",
  "hostname": "${HN}",
  "kernel": "${KN}",
  "system": {
    "manufacturer": "${SYS_MFR}",
    "product": "${SYS_PROD}",
    "serial": "${SYS_SERIAL}",
    "uuid": "${SYS_UUID}",
    "bios_version": "${BIOS_VER}",
    "bios_date": "${BIOS_DATE}"
  },
  "cpu": {
    "model": "${CPU_MODEL}",
    "sockets": ${CPU_SOCKETS},
    "cores_per_socket": ${CPU_CORES_PER},
    "threads_per_core": ${CPU_THREADS_PER},
    "total_cores": ${CPU_TOTAL_CORES},
    "total_threads": ${CPU_TOTAL_THREADS}
  },
  "memory": {
    "total_mb": ${MEM_TOTAL_MB},
    "available_mb": ${MEM_AVAIL_MB}
  },
  "network": {
    "interfaces": ${NIC_JSON}
  },
  "storage": {
    "disks": ${DISK_JSON}
  },
  "ipmi": {
    "bmc_ip": "${BMC_IP}"
  },
  "errors": {
    "mce_count": ${MCE_COUNT},
    "io_error_count": ${IO_ERR_COUNT},
    "edac_count": ${EDAC_COUNT},
    "kernel_taint": ${TAINT}
  }
}
JSONEOF
    log "JSON inventory written to $LOG_DIR/inventory.json"
} 2>&1 | tee -a "$LOG_DIR/auto.log"

# ===== SUMMARY =====
log ""
log "=== Summary ==="
log "Log files written to $LOG_DIR:"
ls -lh "$LOG_DIR" | tee -a "$LOG_DIR/auto.log"
log ""
log "Network addresses:"
ip -4 addr show | grep "inet " | grep -v "127.0.0.1" | awk '{print "  " $2}' | tee -a "$LOG_DIR/auto.log"
log ""
log "=== auto.sh complete ==="

# Print status to console
echo ""
echo "============================================================"
echo " Stupix - Server Diagnostics Live System"
echo "============================================================"
echo " Hostname   : $(hostname)"
echo " Kernel     : $(uname -r)"
echo " Date       : $(date)"
echo ""
echo " IP Addresses:"
ip -4 addr show | grep "inet " | grep -v "127.0.0.1" | awk '{print "   " $2}'
echo ""
echo " Log directory: $LOG_DIR"
echo " Log files:"
ls "$LOG_DIR" | awk '{print "   " $1}'
echo ""
if [ -f "$LOG_DIR/inventory.json" ]; then
    echo " Inventory JSON: $LOG_DIR/inventory.json"
    echo " Quick view: cat $LOG_DIR/inventory.json | python3 -m json.tool"
fi
echo ""
echo " Useful commands:"
echo "   ipmitool chassis status          ipmitool sdr list"
echo "   ipmitool fru print               ipmitool sel list"
echo "   ipmitool lan print 1             (shows BMC IP)"
echo "   smartctl -a /dev/sdX             nvme list"
echo "   lshw -short                      dmidecode -t system"
echo "   nmap -sn 192.168.x.0/24         tcpdump -i eth0"
echo "   stress-ng --cpu 4 --timeout 60   memtester 1G 1"
echo "   fio --name=test --filename=/dev/sda --rw=read --bs=1M --direct=1 --runtime=30 --time_based"
echo "============================================================"
