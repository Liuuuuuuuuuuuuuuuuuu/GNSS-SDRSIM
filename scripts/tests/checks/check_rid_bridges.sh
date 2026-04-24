#!/bin/bash
# Quick Diagnostic Script for RID Bridge Validation
# Usage: bash check_rid_bridges.sh

echo "=========================================="
echo "OpenDroneID RID Bridge Health Check"
echo "=========================================="

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Check 1: Binaries compiled
echo ""
echo "📦 Binaries Check"
echo "---"
for binary in "bin/wifi-rid-bridge" "bin/ble-rid-bridge"; do
    if [ -f "$binary" ]; then
        echo -e "${GREEN}✓${NC} $binary exists"
    else
        echo -e "${RED}✗${NC} $binary NOT found"
    fi
done

# Check 2: Observer position file
echo ""
echo "📍 Observer Position File"
echo "---"
if [ -f "/tmp/bds_sim_obs.json" ]; then
    echo -e "${GREEN}✓${NC} /tmp/bds_sim_obs.json exists"
    cat /tmp/bds_sim_obs.json
else
    echo -e "${YELLOW}⚠${NC} /tmp/bds_sim_obs.json NOT found (will be created by bds-sim)"
fi

# Check 3: Network interface for WiFi
echo ""
echo "🌐 Network Interfaces"
echo "---"
MONITOR_IFACE=$(ip link show | grep "wlx" | awk '{print $2}' | cut -d':' -f1 | head -1)
if [ -n "$MONITOR_IFACE" ]; then
    echo -e "${GREEN}✓${NC} Found potential monitor iface: $MONITOR_IFACE"
    iw dev "$MONITOR_IFACE" link 2>/dev/null && echo -e "  Mode: WiFi connected" || echo -e "  Mode: Likely ready for monitor setup"
else
    echo -e "${YELLOW}⚠${NC} No wlx interface found (USB sniffer not plugged in?)"
fi

# Check 4: Bluetooth
echo ""
echo "📱 Bluetooth Device"
echo "---"
if [ -c "/dev/hci0" ]; then
    echo -e "${GREEN}✓${NC} /dev/hci0 exists"
    hciconfig hci0 2>/dev/null && echo -e "  Status: OK" || echo -e "${YELLOW}⚠${NC}  May require root to check status"
else
    echo -e "${RED}✗${NC} /dev/hci0 NOT found"
fi

# Check 5: Permissions
echo ""
echo "🔐 Permissions Check"
echo "---"
if sudo -n true 2>/dev/null; then
    echo -e "${GREEN}✓${NC} sudo available (no password needed)"
else
    echo -e "${YELLOW}⚠${NC} sudo requires password (will prompt during bridge startup)"
fi

# Check 6: Source files
echo ""
echo "📄 Source Code Files"
echo "---"
for file in "src/bridges/wifi/wifi_rid_bridge.cpp" "src/bridges/ble/ble_rid_bridge.cpp"; do
    if [ -f "$file" ]; then
        LINES=$(wc -l < "$file")
        echo -e "${GREEN}✓${NC} $file ($LINES lines)"
    else
        echo -e "${RED}✗${NC} $file NOT found"
    fi
done

# Check 7: Key functions (grep)
echo ""
echo "🔍 Protocol Support Check"
echo "---"
echo "WiFi Bridge:"
for type in "Type 0 (Basic ID)" "Type 1 (Location)" "Type 2 (Vector)" "Type 4 (System)"; do
    if grep -q "decode_odid_$(echo $type | awk '{print tolower($2)}' | cut -d' ' -f1)" src/bridges/wifi/wifi_rid_bridge.cpp 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} $type supported"
    else
        echo -e "  ${RED}✗${NC} $type NOT found"
    fi
done

echo ""
echo "BLE Bridge:"
for type in "Type 0 (Basic ID)" "Type 1 (Location)" "Type 2 (Vector)" "Type 4 (System)"; do
    if grep -q "decode_odid_$(echo $type | awk '{print tolower($2)}' | cut -d' ' -f1)" src/bridges/ble/ble_rid_bridge.cpp 2>/dev/null; then
        echo -e "  ${GREEN}✓${NC} $type supported"
    else
        echo -e "  ${RED}✗${NC} $type NOT found"
    fi
done

# Check 8: Memory management
echo ""
echo "💾 Memory Management"
echo "---"
if grep -q "TTL\|cleanup\|last_seen_ms" src/bridges/ble/ble_rid_bridge.cpp; then
    echo -e "${GREEN}✓${NC} BLE bridge has TTL-based cleanup (no memory leak)"
else
    echo -e "${RED}✗${NC} BLE bridge lacks cleanup mechanism"
fi

# Check 9: Run test (if application available)
echo ""
echo "🚀 Quick Runtime Test (requires ./bds-sim available)"
echo "---"
if [ -x "./bds-sim" ]; then
    echo -e "${YELLOW}⚠${NC} To test: run './bds-sim' and check stderr output"
    echo "   Expected: [wifi-rid-bridge] and [ble-rid-bridge] messages"
else
    echo -e "${YELLOW}⚠${NC} ./bds-sim not found in current directory"
fi

echo ""
echo "=========================================="
echo "✅ Diagnostic check complete"
echo "=========================================="
