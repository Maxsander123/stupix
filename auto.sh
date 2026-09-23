#!/bin/bash
# =====================================================
# Stupix auto.sh — orchestrator
# Calls each section script and captures output.
# =====================================================

LOG_DIR="/var/log/stupix"
mkdir -p "$LOG_DIR"
chmod 755 "$LOG_DIR"

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SECTIONS_DIR="$SCRIPT_DIR/sections"

log() { echo "[$(date '+%Y-%m-%d %H:%M:%S')] $*" | tee -a "$LOG_DIR/auto.log"; }

run_section() {
    local NAME="$1"
    local SCRIPT="$2"
    local FILE="$LOG_DIR/${NAME}.log"
    log "Running: $NAME"
    {
        echo "=== $NAME ==="
        echo "Timestamp: $(date)"
        echo ""
        bash "$SCRIPT"
    } > "$FILE" 2>&1
    log "Done:    $NAME"
}

log "=== auto.sh started ==="
log "Scripts: $SCRIPT_DIR"
log "Logs:    $LOG_DIR"
log ""

# ---- dependency check (always runs first) ----
log "Running: check-deps"
bash "$SCRIPT_DIR/check-deps.sh" 2>&1 | tee -a "$LOG_DIR/auto.log"
log ""

# ---- diagnostic sections (stdout captured to .log files) ----
run_section "network"       "$SECTIONS_DIR/network.sh"
run_section "system-info"   "$SECTIONS_DIR/system-info.sh"
run_section "cpu"           "$SECTIONS_DIR/cpu.sh"
run_section "memory"        "$SECTIONS_DIR/memory.sh"
run_section "storage"       "$SECTIONS_DIR/storage.sh"
run_section "smart"         "$SECTIONS_DIR/smart.sh"
run_section "pci"           "$SECTIONS_DIR/pci.sh"
run_section "usb"           "$SECTIONS_DIR/usb.sh"
run_section "ipmi"          "$SECTIONS_DIR/ipmi.sh"
run_section "hardware-full" "$SECTIONS_DIR/hardware-full.sh"
run_section "dmesg-errors"  "$SECTIONS_DIR/dmesg-errors.sh"
run_section "nic-link"      "$SECTIONS_DIR/nic-link.sh"
run_section "raid-hardware" "$SECTIONS_DIR/raid-hardware.sh"
run_section "disk-perf"     "$SECTIONS_DIR/disk-perf.sh"
run_section "memtest"       "$SECTIONS_DIR/memtest.sh"

# ---- scripts that manage their own output files ----
log "Running: serials"
bash "$SCRIPT_DIR/serials.sh" >> "$LOG_DIR/auto.log" 2>&1
log "Done:    serials"

log "Running: inventory-json"
bash "$SECTIONS_DIR/inventory-json.sh" >> "$LOG_DIR/auto.log" 2>&1
log "Done:    inventory-json"

log ""
log "=== Summary ==="
log "Log files in $LOG_DIR:"
ls -lh "$LOG_DIR" | tee -a "$LOG_DIR/auto.log"
log ""
log "Network addresses:"
ip -4 addr show | grep "inet " | grep -v "127.0.0.1" | awk '{print "  " $2}' | tee -a "$LOG_DIR/auto.log"
log ""
log "=== auto.sh complete ==="

echo ""
echo "============================================================"
echo " Stupix - Server Diagnostics Live System"
echo "============================================================"
echo " Hostname : $(hostname)    Kernel: $(uname -r)"
echo " Date     : $(date)"
echo ""
echo " IP Addresses:"
ip -4 addr show | grep "inet " | grep -v "127.0.0.1" | awk '{print "   " $2}'
echo ""
echo " Logs : $LOG_DIR"
ls "$LOG_DIR" | awk '{printf "   %s\n", $1}'
echo ""
echo " Quick access:"
echo "   cat $LOG_DIR/serials.log"
echo "   python3 -m json.tool $LOG_DIR/inventory.json"
echo "   cat $LOG_DIR/dmesg-errors.log"
echo "   cat $LOG_DIR/check-deps.log"
echo "============================================================"
