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

# Print status to console (always visible on tty)
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
echo " Useful commands:"
echo "   ipmitool chassis status"
echo "   ipmitool sdr list"
echo "   ipmitool sel list"
echo "   ipmitool fru print"
echo "   ipmitool -I lanplus -H <BMC-IP> -U root -P <pass> chassis status"
echo "   ipmi-sensors"
echo "   bmc-info"
echo "   dmidecode -t system"
echo "   smartctl -a /dev/sdX"
echo "   lshw -short"
echo "   nvme list"
echo "   nmap -sn 192.168.x.0/24"
echo "   tcpdump -i eth0"
echo "   stress-ng --cpu 4 --timeout 60"
echo "   memtester 1G 1"
echo "============================================================"
