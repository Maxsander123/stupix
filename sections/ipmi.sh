#!/bin/bash
echo "--- Loading IPMI kernel modules ---"
modprobe ipmi_si 2>/dev/null && echo "ipmi_si loaded" || echo "ipmi_si failed"
modprobe ipmi_devintf 2>/dev/null && echo "ipmi_devintf loaded" || echo "ipmi_devintf failed"
modprobe ipmi_msghandler 2>/dev/null && echo "ipmi_msghandler loaded" || echo "ipmi_msghandler failed"
sleep 1

echo ""
echo "--- Chassis Status ---"
ipmitool chassis status 2>/dev/null || echo "ipmitool chassis status failed"

echo ""
echo "--- BMC Info ---"
ipmitool bmc info 2>/dev/null || echo "ipmitool bmc info failed"

echo ""
echo "--- LAN Channel ---"
ipmitool lan print 1 2>/dev/null || echo "ipmitool lan print failed"

echo ""
echo "--- FRU Inventory ---"
ipmitool fru print 2>/dev/null || echo "ipmitool fru print failed"

echo ""
echo "--- Sensor Data Records ---"
ipmitool sdr list 2>/dev/null || echo "ipmitool sdr list failed"

echo ""
echo "--- System Event Log (last 20 entries) ---"
ipmitool sel list 2>/dev/null | tail -20 || echo "ipmitool sel list failed"

echo ""
echo "--- freeipmi bmc-info ---"
bmc-info 2>/dev/null || echo "bmc-info not available"

echo ""
echo "--- freeipmi ipmi-sensors ---"
ipmi-sensors 2>/dev/null | head -60 || echo "ipmi-sensors not available"
