#!/bin/bash
# =============================================================
# STUPIX auto.sh
# Wird automatisch nach dem Boot + git clone ausgeführt
# =============================================================

LOG="/var/log/stupix-auto.log"

log() {
    echo "[$(date '+%H:%M:%S')] $*" | tee -a "$LOG"
}

# Banner
clear
cat << 'BANNER'
  ███████╗████████╗██╗   ██╗██████╗ ██╗██╗  ██╗
  ██╔════╝╚══██╔══╝██║   ██║██╔══██╗██║╚██╗██╔╝
  ███████╗   ██║   ██║   ██║██████╔╝██║ ╚███╔╝ 
  ╚════██║   ██║   ██║   ██║██╔═══╝ ██║ ██╔██╗ 
  ███████║   ██║   ╚██████╔╝██║     ██║██╔╝ ██╗
  ╚══════╝   ╚═╝    ╚═════╝ ╚═╝     ╚═╝╚═╝  ╚═╝
  Server Diagnostics Live System
BANNER

log "======================================"
log " STUPIX auto.sh gestartet"
log "======================================"

# === NETZWERK ===
log ""
log "=== NETZWERK ==="
ip -4 addr show | grep "inet " | tee -a "$LOG"

# === IPMI ===
log ""
log "=== IPMI STATUS ==="
modprobe ipmi_si 2>/dev/null && modprobe ipmi_devintf 2>/dev/null && log "IPMI Treiber geladen" || log "IPMI Treiber nicht ladbar"
if ipmitool chassis status 2>/dev/null | tee -a "$LOG"; then
    log "IPMI: Lokal erreichbar ✅"
else
    log "IPMI: Nicht verfügbar (evtl. kein BMC)"
fi

# === SYSTEM INFO ===
log ""
log "=== SYSTEM INFORMATIONEN ==="
log "Hostname : $(hostname)"
log "Kernel   : $(uname -r)"
log "Datum    : $(date)"

log ""
log "--- Hersteller / Modell ---"
dmidecode -t system 2>/dev/null | grep -E "Manufacturer|Product Name|Serial Number|UUID" | tee -a "$LOG" || log "dmidecode: kein Zugriff"

log ""
log "--- CPU ---"
lscpu | grep -E "Model name|Socket|Core|Thread" | tee -a "$LOG"

log ""
log "--- RAM ---"
free -h | tee -a "$LOG"

log ""
log "--- Disks ---"
lsblk -o NAME,SIZE,TYPE,MODEL 2>/dev/null | tee -a "$LOG"

log ""
log "--- PCI Devices (Top 20) ---"
lspci 2>/dev/null | head -20 | tee -a "$LOG"

# === S.M.A.R.T. ===
log ""
log "=== DISK S.M.A.R.T. ==="
for disk in $(lsblk -d -n -o NAME | grep -E "^sd|^nvme"); do
    log "--- /dev/$disk ---"
    smartctl -H /dev/$disk 2>/dev/null | grep -E "overall|result|passed|failed" | tee -a "$LOG" || true
done

# === IPMI FRU ===
log ""
log "=== FRU INVENTORY ==="
ipmitool fru print 2>/dev/null | head -40 | tee -a "$LOG" || log "FRU nicht verfügbar"

# === IPMI SENSORS ===
log ""
log "=== IPMI SENSORS ==="
ipmitool sdr list 2>/dev/null | head -30 | tee -a "$LOG" || log "Sensoren nicht verfügbar"

# === FERTIG ===
echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo " STUPIX bereit!"
echo ""
echo " IP-Adressen:"
ip -4 addr show | grep "inet " | grep -v "127.0.0.1" | awk '{print "  " $2}'
echo ""
echo " Nützliche Befehle:"
echo "  ipmitool chassis status"
echo "  ipmitool sdr list"
echo "  ipmitool sel list"
echo "  ipmitool fru print"
echo "  ipmi-sensors              (freeipmi)"
echo "  bmc-info                  (freeipmi)"
echo "  dmidecode -t system"
echo "  smartctl -a /dev/sdX"
echo "  lshw -short"
echo "  nvme list"
echo "  nmap -sn 192.168.x.0/24"
echo "  tcpdump -i eth0"
echo ""
echo " Log: /var/log/stupix-auto.log"
echo " Repo: /opt/stupix"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
