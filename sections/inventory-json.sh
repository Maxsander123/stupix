#!/bin/bash
# Writes /var/log/stupix/inventory.json
LOG_DIR="/var/log/stupix"
JSON="$LOG_DIR/inventory.json"

python3 - <<'PYEOF'
import subprocess, json, re, os

LOG_DIR = "/var/log/stupix"

def dmi(key):
    try:
        out = subprocess.check_output(["dmidecode", "-s", key], stderr=subprocess.DEVNULL, text=True)
        lines = [l.strip() for l in out.splitlines() if l.strip()]
        return lines[0] if lines else ""
    except Exception:
        return ""

def run(*args):
    try:
        return subprocess.check_output(list(args), stderr=subprocess.DEVNULL, text=True)
    except Exception:
        return ""

def lsblk_names(pattern):
    out = run("lsblk", "-d", "-n", "-o", "NAME")
    return [l.strip() for l in out.splitlines() if re.match(pattern, l.strip())]

def lsblk_size(dev):
    return run("lsblk", "-d", "-n", "-o", "SIZE", dev).strip()

def smartctl_field(dev, field):
    out = run("smartctl", "-i", dev)
    for line in out.splitlines():
        if field in line and ":" in line:
            return line.split(":", 1)[1].strip()
    return ""

def nvme_field(dev, field):
    out = run("nvme", "id-ctrl", dev)
    for line in out.splitlines():
        if line.startswith(field + " ") or line.startswith(field + ":"):
            return line.split(":", 1)[-1].strip().rstrip()
    return ""

import datetime
report = {"generated": datetime.datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%SZ")}

report["hostname"] = run("hostname").strip()
report["kernel"]   = run("uname", "-r").strip()

report["system"] = {
    "manufacturer": dmi("system-manufacturer"),
    "product":      dmi("system-product-name"),
    "version":      dmi("system-version"),
    "serial":       dmi("system-serial-number"),
    "uuid":         dmi("system-uuid"),
}
report["baseboard"] = {
    "manufacturer": dmi("baseboard-manufacturer"),
    "product":      dmi("baseboard-product-name"),
    "serial":       dmi("baseboard-serial-number"),
    "asset_tag":    dmi("baseboard-asset-tag"),
}
report["chassis"] = {
    "manufacturer": dmi("chassis-manufacturer"),
    "type":         dmi("chassis-type"),
    "serial":       dmi("chassis-serial-number"),
    "asset_tag":    dmi("chassis-asset-tag"),
}
report["bios"] = {
    "vendor":       dmi("bios-vendor"),
    "version":      dmi("bios-version"),
    "release_date": dmi("bios-release-date"),
}

# CPU
try:
    raw = run("lscpu")
    def lscpu(key):
        for line in raw.splitlines():
            if line.startswith(key):
                return line.split(":", 1)[1].strip()
        return ""
    sockets = int(lscpu("Socket(s)") or 0)
    cores   = int(lscpu("Core(s) per socket") or 0)
    threads = int(lscpu("Thread(s) per core") or 0)
    report["cpu"] = {
        "model":          lscpu("Model name"),
        "architecture":   lscpu("Architecture"),
        "sockets":        sockets,
        "cores_per_socket": cores,
        "threads_per_core": threads,
        "total_cores":    sockets * cores,
        "total_threads":  sockets * cores * threads,
        "mhz":            lscpu("CPU MHz"),
    }
except Exception:
    report["cpu"] = {}

# Memory
try:
    with open("/proc/meminfo") as f:
        mi = {k.strip(): v.strip() for k, v in (l.split(":", 1) for l in f if ":" in l)}
    report["memory"] = {
        "total_mb":     int(mi.get("MemTotal", "0 kB").split()[0]) // 1024,
        "available_mb": int(mi.get("MemAvailable", "0 kB").split()[0]) // 1024,
    }
except Exception:
    report["memory"] = {}

# DIMMs
dimms = []
try:
    raw = run("dmidecode", "-t", "memory")
    for block in raw.split("\n\n"):
        if "Memory Device" not in block or "Memory Array" in block:
            continue
        def f(key, b=block):
            m = re.search(rf"^\s*{re.escape(key)}:\s*(.+)$", b, re.MULTILINE)
            return m.group(1).strip() if m else ""
        size = f("Size")
        if not size or "No Module" in size or size == "Unknown":
            continue
        dimms.append({
            "locator":      f("Locator"),
            "bank":         f("Bank Locator"),
            "size":         size,
            "type":         f("Type"),
            "speed":        f("Speed"),
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
        "device":    dev,
        "size":      lsblk_size(dev),
        "model":     smartctl_field(dev, "Device Model") or smartctl_field(dev, "Product"),
        "serial":    smartctl_field(dev, "Serial Number"),
        "firmware":  smartctl_field(dev, "Firmware Version"),
        "transport": smartctl_field(dev, "Transport protocol") or "SATA/SAS",
    })
for name in lsblk_names(r"^nvme[0-9]+$"):
    dev = f"/dev/{name}"
    disks.append({
        "device":    dev,
        "size":      lsblk_size(dev),
        "model":     nvme_field(dev, "mn"),
        "serial":    nvme_field(dev, "sn"),
        "firmware":  nvme_field(dev, "fr"),
        "transport": "NVMe",
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
        out = run("ethtool", "-P", nic)
        m = re.search(r"Permanent address: (\S+)", out)
        if m:
            perm_mac = m.group(1)
    except Exception:
        pass
    ip = ""
    try:
        out = run("ip", "-4", "addr", "show", nic)
        m = re.search(r"inet (\S+)", out)
        if m:
            ip = m.group(1)
    except Exception:
        pass
    speed = ""
    link  = ""
    try:
        out = run("ethtool", nic)
        ms = re.search(r"Speed: (\S+)", out)
        ml = re.search(r"Link detected: (\S+)", out)
        if ms: speed = ms.group(1)
        if ml: link  = ml.group(1)
    except Exception:
        pass
    nics.append({"interface": nic, "ip": ip, "mac": mac,
                 "permanent_mac": perm_mac, "speed": speed, "link": link})
report["network_interfaces"] = nics

# IPMI
bmc = {}
try:
    for line in run("ipmitool", "bmc", "info").splitlines():
        if "Firmware Revision" in line:
            bmc["firmware"] = line.split(":", 1)[1].strip()
    for line in run("ipmitool", "lan", "print", "1").splitlines():
        if "IP Address  " in line and "Source" not in line:
            bmc["ip"] = line.split(":", 1)[1].strip()
        if "MAC Address" in line:
            bmc["mac"] = line.split(":", 1)[1].strip()
    m = re.search(r"System GUID\s*:\s*(\S+)", run("ipmitool", "mc", "guid"))
    if m:
        bmc["guid"] = m.group(1)
    fru_entries = []
    current = {}
    for line in run("ipmitool", "fru", "print").splitlines():
        if line.startswith("FRU Device Description"):
            if current:
                fru_entries.append(current)
            current = {"device": line.split(":", 1)[1].strip() if ":" in line else line}
        for key, field in [("board_mfr", "Board Mfg"), ("board_serial", "Board Serial"),
                           ("board_part", "Board Part Number"), ("product_name", "Product Name"),
                           ("product_serial", "Product Serial"), ("product_part", "Product Part Number")]:
            if field in line and ":" in line:
                current[key] = line.split(":", 1)[1].strip()
    if current:
        fru_entries.append(current)
    bmc["fru"] = fru_entries
except Exception:
    pass
report["bmc"] = bmc

# Error counts
try:
    dmesg_out = run("dmesg")
    report["errors"] = {
        "mce_count":      len(re.findall(r"(?i)mce|machine check", dmesg_out)),
        "io_error_count": len(re.findall(r"(?i)I/O error|ata.*error|Buffer I/O", dmesg_out)),
        "edac_count":     len(re.findall(r"(?i)edac|corrected error|uncorrected", dmesg_out)),
        "kernel_taint":   int(open("/proc/sys/kernel/tainted").read().strip()),
    }
except Exception:
    report["errors"] = {}

with open(f"{LOG_DIR}/inventory.json", "w") as f:
    json.dump(report, f, indent=2)

print(f"inventory.json written ({os.path.getsize(f'{LOG_DIR}/inventory.json')} bytes)")
PYEOF
