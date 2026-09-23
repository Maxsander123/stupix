#!/bin/bash
echo "--- NIC link status (ethtool) ---"
for NIC in $(ls /sys/class/net/ | grep -v '^lo$'); do
    echo "  Interface: $NIC"
    ethtool "$NIC" 2>/dev/null \
        | grep -E "(Speed|Duplex|Link detected|Auto-negotiation|Port)" \
        | sed 's/^/    /' \
        || echo "    ethtool not available"
    DRIVER=$(ethtool -i "$NIC" 2>/dev/null | awk '/^driver:/{print $2}')
    [ -n "$DRIVER" ] && echo "    Driver: $DRIVER"
    PERM=$(ethtool -P "$NIC" 2>/dev/null | awk '/Permanent/{print $NF}')
    [ -n "$PERM" ] && echo "    Permanent MAC: $PERM"
    echo ""
done

echo ""
echo "--- LLDP neighbors ---"
if command -v lldpcli >/dev/null 2>&1; then
    lldpcli show neighbors 2>/dev/null || echo "No LLDP neighbors found yet (lldpd may need more time)"
    echo ""
    echo "--- LLDP interfaces ---"
    lldpcli show interfaces 2>/dev/null || true
else
    echo "lldpcli not available (install lldpd)"
fi
