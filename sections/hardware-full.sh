#!/bin/bash
echo "--- lshw full output ---"
lshw 2>/dev/null || echo "lshw not available"
