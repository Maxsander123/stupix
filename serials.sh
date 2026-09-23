#!/bin/bash
# =====================================================
# Stupix serials.sh
# Collects all hardware serial numbers and writes
# /var/log/stupix/serials.log + serials.json
# =====================================================

LOG_DIR="/var/log/stupix"
mkdir -p "$LOG_DIR"

OUT="$LOG_DIR/serials.log"
JSON="$LOG_DIR/serials.json"

log()  { echo "$*" | tee -a "$OUT"; }
hdr()  { echo "" | tee -a "$OUT"; echo "=== $* ===" | tee -a "$OUT"; }
line() { printf "  %-30s %s\n" "$1" "$2" | tee -a "$OUT"; }

: > "$OUT"
log "Stupix Serial Number Collection"
log "================================"
log "Timestamp : $(date)"
log "Hostname  : $(hostname)"

# ---- helpers ----
dmi() { dmidecode -s "$1" 2>/dev/null | grep -v '^$' | head -1 | sed 's/^\s*//;s/\s*$//'; }
dmi_field() { dmidecode -t "$1" 2>/dev/null; }

# =============================================
# SYSTEM
# =============================================
hdr "SYSTEM"
line "Manufacturer:"  "$(dmi system-manufacturer)"
line "Product:"        "$(dmi system-product-name)"
line "Version:"        "$(dmi system-version)"
line "Serial:"         "$(dmi system-serial-number)"
line "UUID:"           "$(dmi system-uuid)"

# =============================================
# BASEBOARD / MOTHERBOARD
# =============================================
hdr "BASEBOARD / MOTHERBOARD"
line "Manufacturer:"   "$(dmi baseboard-manufacturer)"
line "Product:"        "$(dmi baseboard-product-name)"
line "Version:"        "$(dmi baseboard-version)"
line "Serial:"         "$(dmi baseboard-serial-number)"
line "Asset Tag:"      "$(dmi baseboard-asset-tag)"

# =============================================
# CHASSIS
# =============================================
hdr "CHASSIS"
line "Manufacturer:"   "$(dmi chassis-manufacturer)"
line "Type:"           "$(dmi chassis-type)"
line "Version:"        "$(dmi chassis-version)"
line "Serial:"         "$(dmi chassis-serial-number)"
line "Asset Tag:"      "$(dmi chassis-asset-tag)"

# =============================================
# BIOS
# =============================================
hdr "BIOS"
line "Vendor:"         "$(dmi bios-vendor)"
line "Version:"        "$(dmi bios-version)"
line "Release Date:"   "$(dmi bios-release-date)"

# =============================================
# CPU(s)
# =============================================
hdr "CPU(s)"
dmi_field processor | awk '
    /^Processor Information/ { idx++ }
    /^\s*Socket Designation:/{ socket=$0; sub(/.*: */,"",socket) }
    /^\s*Manufacturer:/      { mfr=$0;    sub(/.*: */,"",mfr) }
    /^\s*Version:/           { ver=$0;    sub(/.*: */,"",ver) }
    /^\s*Serial Number:/     { serial=$0; sub(/.*: */,"",serial) }
    /^\s*ID:/                { id=$0;     sub(/.*: */,"",id) }
    /^\s*Status:/            {
        if ($0 !~ /Unpopulated|Not Present/) {
            printf "  CPU%-2d  Socket: %-12s  Serial: %-25s  ID: %s\n", idx, socket, serial, id
        }
    }
' | tee -a "$OUT"

# =============================================
# MEMORY / RAM DIMMs
# =============================================
hdr "MEMORY DIMMs"
dmi_field memory | awk '
    /^Memory Device/ { in_dev=1; loc=""; bank=""; mfr=""; serial=""; part=""; size="" }
    !in_dev { next }
    /^\s*Locator:/        && !/Bank/ { loc=$0;    sub(/.*: */,"",loc) }
    /^\s*Bank Locator:/              { bank=$0;   sub(/.*: */,"",bank) }
    /^\s*Manufacturer:/              { mfr=$0;    sub(/.*: */,"",mfr) }
    /^\s*Serial Number:/             { serial=$0; sub(/.*: */,"",serial) }
    /^\s*Part Number:/               { part=$0;   sub(/.*: */,"",part) }
    /^\s*Size:/                      { size=$0;   sub(/.*: */,"",size) }
    /^\s*Speed:/                     {
        if (size !~ /No Module|Unknown/ && size != "") {
            printf "  %-10s %-12s  Size: %-8s  Mfr: %-20s  Part: %-20s  Serial: %s\n",
                   loc, bank, size, mfr, part, serial
        }
        in_dev=0
    }
' | tee -a "$OUT"

# =============================================
# DISKS (SATA/SAS via smartctl, NVMe via nvme)
# =============================================
hdr "DISKS"
for DEV in $(lsblk -d -n -o NAME 2>/dev/null | grep -E '^sd|^hd'); do
    MODEL=$(smartctl -i "/dev/$DEV" 2>/dev/null | awk -F': +' '/Device Model|Product/{print $2; exit}')
    SERIAL=$(smartctl -i "/dev/$DEV" 2>/dev/null | awk -F': +' '/Serial Number/{print $2; exit}')
    SIZE=$(lsblk -d -n -o SIZE "/dev/$DEV" 2>/dev/null | tr -d ' ')
    FW=$(smartctl -i "/dev/$DEV" 2>/dev/null | awk -F': +' '/Firmware Version/{print $2; exit}')
    line "/dev/$DEV  $SIZE  $MODEL" "Serial: ${SERIAL:-N/A}  FW: ${FW:-N/A}"
done

for DEV in $(lsblk -d -n -o NAME 2>/dev/null | grep '^nvme[0-9]*$'); do
    MODEL=$(nvme id-ctrl "/dev/$DEV" 2>/dev/null | awk -F': +' '/^mn /{gsub(/ +$/,"",$2); print $2; exit}')
    SERIAL=$(nvme id-ctrl "/dev/$DEV" 2>/dev/null | awk -F': +' '/^sn /{gsub(/ +$/,"",$2); print $2; exit}')
    FW=$(nvme id-ctrl "/dev/$DEV" 2>/dev/null | awk -F': +' '/^fr /{gsub(/ +$/,"",$2); print $2; exit}')
    SIZE=$(lsblk -d -n -o SIZE "/dev/$DEV" 2>/dev/null | tr -d ' ')
    line "/dev/$DEV  $SIZE  $MODEL" "Serial: ${SERIAL:-N/A}  FW: ${FW:-N/A}"
done

# =============================================
# NETWORK INTERFACES (MAC = hardware serial)
# =============================================
hdr "NETWORK INTERFACES"
for NIC in $(ls /sys/class/net/ 2>/dev/null | grep -v '^lo$'); do
    MAC=$(cat "/sys/class/net/$NIC/address" 2>/dev/null)
    DRIVER=$(ethtool -i "$NIC" 2>/dev/null | awk '/^driver:/{print $2}')
    SPEED=$(ethtool "$NIC" 2>/dev/null | awk '/Speed:/{print $2}')
    PERM_MAC=$(ethtool -P "$NIC" 2>/dev/null | awk '/Permanent/{print $NF}')
    line "$NIC" "MAC: ${MAC:-N/A}  Perm-MAC: ${PERM_MAC:-N/A}  Driver: ${DRIVER:-?}  Speed: ${SPEED:-?}"
done

# =============================================
# IPMI / BMC / FRU
# =============================================
hdr "IPMI / BMC"
modprobe ipmi_si ipmi_devintf ipmi_msghandler 2>/dev/null; sleep 1

BMC_GUID=$(ipmitool mc guid 2>/dev/null | grep -i 'system guid' | awk -F': ' '{print $2}')
BMC_FW=$(ipmitool bmc info 2>/dev/null | awk -F': ' '/Firmware Revision/{print $2}')
BMC_IP=$(ipmitool lan print 1 2>/dev/null | awk -F': ' '/^IP Address  /{print $2}')
BMC_MAC=$(ipmitool lan print 1 2>/dev/null | awk -F': ' '/^MAC Address/{print $2}')
line "BMC IP:"        "${BMC_IP:-N/A}"
line "BMC MAC:"       "${BMC_MAC:-N/A}"
line "BMC FW:"        "${BMC_FW:-N/A}"
line "BMC GUID:"      "${BMC_GUID:-N/A}"

log ""
log "--- FRU Inventory ---"
ipmitool fru print 2>/dev/null | grep -E '(Board Serial|Product Serial|Board Part|Product Part|Product Name|Board Mfg)' \
    | sed 's/^/  /' | tee -a "$OUT" \
    || log "  ipmitool fru not available"

# =============================================
# PCI DEVICES (those with serial numbers)
# =============================================
hdr "PCI DEVICE SERIALS"
# Some NICs, HBAs and storage controllers expose serial via vpd-r
lspci 2>/dev/null | while read -r SLOT CLASS DEV; do
    SN=$(lspci -v -s "$SLOT" 2>/dev/null | grep -i 'serial number' | awk '{print $NF}')
    if [ -n "$SN" ]; then
        printf "  %-12s %-40s Serial: %s\n" "$SLOT" "$DEV $CLASS" "$SN" | tee -a "$OUT"
    fi
done

# =============================================
# JSON OUTPUT
# =============================================
{
python3 - <<'PYEOF'
import subprocess, json, re, os

def dmi(key):
    try:
        out = subprocess.check_output(["dmidecode", "-s", key], stderr=subprocess.DEVNULL, text=True)
        return out.strip().splitlines()[0].strip() if out.strip() else ""
    except Exception:
        return ""

def smartctl_serial(dev):
    try:
        out = subprocess.check_output(["smartctl", "-i", dev], stderr=subprocess.DEVNULL, text=True)
        for line in out.splitlines():
            if "Serial Number" in line:
                return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return ""

def smartctl_model(dev):
    try:
        out = subprocess.check_output(["smartctl", "-i", dev], stderr=subprocess.DEVNULL, text=True)
        for line in out.splitlines():
            if "Device Model" in line or "Product" in line:
                return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return ""

def nvme_field(dev, field):
    try:
        out = subprocess.check_output(["nvme", "id-ctrl", dev], stderr=subprocess.DEVNULL, text=True)
        for line in out.splitlines():
            if line.startswith(field):
                return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return ""

def ipmitool(*args):
    try:
        return subprocess.check_output(["ipmitool"] + list(args), stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return ""

def lsblk_names(pattern):
    try:
        out = subprocess.check_output(["lsblk", "-d", "-n", "-o", "NAME"], stderr=subprocess.DEVNULL, text=True)
        return [l.strip() for l in out.splitlines() if re.match(pattern, l.strip())]
    except Exception:
        return []

def lsblk_size(dev):
    try:
        return subprocess.check_output(["lsblk", "-d", "-n", "-o", "SIZE", dev], stderr=subprocess.DEVNULL, text=True).strip()
    except Exception:
        return ""

report = {}

# System
report["system"] = {
    "manufacturer":  dmi("system-manufacturer"),
    "product":       dmi("system-product-name"),
    "version":       dmi("system-version"),
    "serial":        dmi("system-serial-number"),
    "uuid":          dmi("system-uuid"),
}

# Baseboard
report["baseboard"] = {
    "manufacturer":  dmi("baseboard-manufacturer"),
    "product":       dmi("baseboard-product-name"),
    "version":       dmi("baseboard-version"),
    "serial":        dmi("baseboard-serial-number"),
    "asset_tag":     dmi("baseboard-asset-tag"),
}

# Chassis
report["chassis"] = {
    "manufacturer":  dmi("chassis-manufacturer"),
    "type":          dmi("chassis-type"),
    "serial":        dmi("chassis-serial-number"),
    "asset_tag":     dmi("chassis-asset-tag"),
}

# BIOS
report["bios"] = {
    "vendor":        dmi("bios-vendor"),
    "version":       dmi("bios-version"),
    "release_date":  dmi("bios-release-date"),
}

# CPUs
cpus = []
try:
    raw = subprocess.check_output(["dmidecode", "-t", "processor"], stderr=subprocess.DEVNULL, text=True)
    for block in raw.split("\n\n"):
        if "Processor Information" not in block:
            continue
        def f(key):
            m = re.search(rf"^\s*{key}:\s*(.+)$", block, re.MULTILINE)
            return m.group(1).strip() if m else ""
        if "Unpopulated" in f("Status") or "Not Present" in f("Status"):
            continue
        cpus.append({
            "socket":  f("Socket Designation"),
            "version": f("Version"),
            "serial":  f("Serial Number"),
            "id":      f("ID"),
            "cores":   f("Core Count"),
            "threads": f("Thread Count"),
        })
except Exception:
    pass
report["cpus"] = cpus

# DIMMs
dimms = []
try:
    raw = subprocess.check_output(["dmidecode", "-t", "memory"], stderr=subprocess.DEVNULL, text=True)
    for block in raw.split("\n\n"):
        if "Memory Device" not in block or "Memory Array" in block:
            continue
        def f(key):
            m = re.search(rf"^\s*{key}:\s*(.+)$", block, re.MULTILINE)
            return m.group(1).strip() if m else ""
        size = f("Size")
        if not size or "No Module" in size or size == "Unknown":
            continue
        dimms.append({
            "locator":      f("Locator"),
            "bank":         f("Bank Locator"),
            "size":         size,
            "type":         f("Type"),
            "speed_mhz":    f("Speed"),
            "manufacturer": f("Manufacturer"),
            "serial":       f("Serial Number"),
            "part_number":  f("Part Number"),
        })
except Exception:
    pass
report["memory_dimms"] = dimms

# Disks
disks = []
for name in lsblk_names(r"^(sd|hd)"):
    dev = f"/dev/{name}"
    disks.append({
        "device":  dev,
        "size":    lsblk_size(dev),
        "model":   smartctl_model(dev),
        "serial":  smartctl_serial(dev),
    })
for name in lsblk_names(r"^nvme[0-9]+$"):
    dev = f"/dev/{name}"
    disks.append({
        "device":  dev,
        "size":    lsblk_size(dev),
        "model":   nvme_field(dev, "mn"),
        "serial":  nvme_field(dev, "sn"),
        "firmware": nvme_field(dev, "fr"),
    })
report["disks"] = disks

# NICs
nics = []
net_dir = "/sys/class/net"
for nic in sorted(os.listdir(net_dir)):
    if nic == "lo":
        continue
    mac = ""
    try:
        mac = open(f"{net_dir}/{nic}/address").read().strip()
    except Exception:
        pass
    perm_mac = ""
    try:
        out = subprocess.check_output(["ethtool", "-P", nic], stderr=subprocess.DEVNULL, text=True)
        m = re.search(r"Permanent address: (\S+)", out)
        if m:
            perm_mac = m.group(1)
    except Exception:
        pass
    nics.append({"interface": nic, "mac": mac, "permanent_mac": perm_mac})
report["network_interfaces"] = nics

# IPMI / BMC
bmc = {}
raw = ipmitool("bmc", "info")
for line in raw.splitlines():
    if "Firmware Revision" in line:
        bmc["firmware"] = line.split(":", 1)[1].strip()
raw_lan = ipmitool("lan", "print", "1")
for line in raw_lan.splitlines():
    if "IP Address  " in line and "Source" not in line:
        bmc["ip"] = line.split(":", 1)[1].strip()
    if "MAC Address" in line:
        bmc["mac"] = line.split(":", 1)[1].strip()
raw_guid = ipmitool("mc", "guid")
m = re.search(r"System GUID\s*:\s*(\S+)", raw_guid)
if m:
    bmc["guid"] = m.group(1)

# FRU
fru_entries = []
raw_fru = ipmitool("fru", "print")
current = {}
for line in raw_fru.splitlines():
    if line.startswith("FRU Device Description"):
        if current:
            fru_entries.append(current)
        current = {"device": line.split(":", 1)[1].strip() if ":" in line else line}
    for key, field in [("board_mfr", "Board Mfg"), ("board_serial", "Board Serial"),
                       ("board_part", "Board Part Number"), ("product_name", "Product Name"),
                       ("product_serial", "Product Serial"), ("product_part", "Product Part Number")]:
        if field in line:
            current[key] = line.split(":", 1)[1].strip()
if current:
    fru_entries.append(current)
bmc["fru"] = fru_entries
report["bmc"] = bmc

print(json.dumps(report, indent=2))
PYEOF
} > "$JSON" 2>/dev/null && log "" && log "JSON written to $JSON" || log "WARNING: JSON generation failed"

# =============================================
# SUMMARY
# =============================================
echo "" | tee -a "$OUT"
echo "============================================================" | tee -a "$OUT"
echo " Serial numbers written to:" | tee -a "$OUT"
echo "   $OUT" | tee -a "$OUT"
echo "   $JSON" | tee -a "$OUT"
echo "============================================================" | tee -a "$OUT"
