#!/bin/bash
# =====================================================
# Stupix check-deps.sh
# Verifies all tools required by the section scripts
# are present on the running system.
# Called by auto.sh before any section runs.
# Exit code: 0 = all required tools present
#            1 = one or more required tools missing
# =====================================================

LOG_DIR="/var/log/stupix"
mkdir -p "$LOG_DIR"
OUT="$LOG_DIR/check-deps.log"

MISSING_REQUIRED=()
MISSING_OPTIONAL=()

check() {
    local TYPE="$1"   # required | optional
    local CMD="$2"
    local PKG="$3"    # package that provides it
    local SECTION="$4"

    if ! command -v "$CMD" >/dev/null 2>&1; then
        if [ "$TYPE" = "required" ]; then
            MISSING_REQUIRED+=("$CMD (pkg: $PKG, needed by: $SECTION)")
        else
            MISSING_OPTIONAL+=("$CMD (pkg: $PKG, needed by: $SECTION)")
        fi
        return 1
    fi
    return 0
}

# ---- required: without these sections produce no useful output ----
check required  ip              iproute2            network
check required  hostname        hostname            system-info
check required  uname           coreutils           system-info
check required  dmidecode       dmidecode           system-info/cpu/memory/serials
check required  lscpu           util-linux          cpu
check required  free            procps              memory
check required  lsblk           util-linux          storage
check required  fdisk           fdisk               storage
check required  dmesg           util-linux          dmesg-errors
check required  lspci           pciutils            pci/raid-hardware
check required  lsusb           usbutils            usb
check required  ipmitool        ipmitool            ipmi/serials
check required  lshw            lshw                hardware-full
check required  smartctl        smartmontools       smart/serials
check required  nvme            nvme-cli            storage/serials
check required  lsscsi          lsscsi              storage
check required  mdadm           mdadm               storage/raid-hardware
check required  python3         python3             inventory-json/serials

# ---- optional: section runs but output is reduced ----
check optional  ethtool         ethtool             nic-link/serials
check optional  lldpcli         lldpd               nic-link
check optional  fio             fio                 disk-perf
check optional  memtester       memtester           memtest
check optional  bmc-info        freeipmi-tools      ipmi
check optional  ipmi-sensors    freeipmi-tools      ipmi
check optional  nvme            nvme-cli            storage/serials
check optional  sg_inq          sg3-utils           storage
check optional  jq              jq                  inventory-json
check optional  tcpdump         tcpdump             (manual use)
check optional  nmap            nmap                (manual use)
check optional  stress-ng       stress-ng           (manual use)
check optional  iperf3          iperf3              (manual use)
check optional  git             git                 stupix-init

# ---- hardware RAID tools (optional, vendor-specific) ----
check optional  storcli64       "(download from broadcom.com)"  raid-hardware
check optional  perccli64       "(download from dell.com)"      raid-hardware
check optional  ssacli          "(download from hpe.com)"       raid-hardware
check optional  arcconf         "(download from microsemi.com)" raid-hardware

# ---- write report ----
{
    echo "Stupix dependency check"
    echo "======================="
    echo "Timestamp : $(date)"
    echo "Hostname  : $(hostname)"
    echo ""

    if [ ${#MISSING_REQUIRED[@]} -eq 0 ]; then
        echo "✅ All required tools present"
    else
        echo "❌ MISSING REQUIRED tools (${#MISSING_REQUIRED[@]}):"
        for t in "${MISSING_REQUIRED[@]}"; do echo "   - $t"; done
    fi

    echo ""
    if [ ${#MISSING_OPTIONAL[@]} -eq 0 ]; then
        echo "✅ All optional tools present"
    else
        echo "⚠️  Missing optional tools (${#MISSING_OPTIONAL[@]}):"
        for t in "${MISSING_OPTIONAL[@]}"; do echo "   - $t"; done
    fi
    echo ""
} | tee "$OUT"

# ---- console summary ----
if [ ${#MISSING_REQUIRED[@]} -gt 0 ]; then
    echo "❌ ${#MISSING_REQUIRED[@]} required tool(s) missing — some sections will fail." | tee -a "$OUT"
    echo "   See $OUT for details." | tee -a "$OUT"
    exit 1
else
    echo "✅ All required tools present (${#MISSING_OPTIONAL[@]} optional tools missing)." | tee -a "$OUT"
    exit 0
fi
