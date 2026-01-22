#!/bin/bash

# Define colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

BINARY="./build/MacNap"

echo -e "${CYAN}========================================${NC}"
echo -e "${CYAN}   MacNap WATCHDOG ${NC}"
echo -e "${CYAN}========================================${NC}"

# Check if binary exists
if [ ! -f "$BINARY" ]; then
    echo -e "${RED}[ERROR] MacNap binary not found at $BINARY${NC}"
    echo "Please run 'make' inside the build folder first."
    exit 1
fi

CRASH_COUNT=0

while true; do
    echo -e "${GREEN}[GUARD] Starting MacNap...${NC}"
    
    # Run MacNap and wait for it to finish
    $BINARY --daemon
    
    # Capture the exit code
    EXIT_CODE=$?
    
    # Exit Code 0 means user stopped it (Ctrl+C or clean exit)
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}[GUARD] MacNap exited cleanly. Shutting down.${NC}"
        break
    fi
    
    # Any other exit code means a CRASH
    CRASH_COUNT=$((CRASH_COUNT+1))
    echo -e "${RED}[GUARD] ⚠️ MacNap CRASHED! (Exit Code: $EXIT_CODE)${NC}"
    echo -e "${YELLOW}[GUARD] Restarting in 3 seconds... (Crash #$CRASH_COUNT)${NC}"
    
    # Send a system notification about the crash
    osascript -e 'display notification "MacNap crashed and is restarting." with title "⚠️ Watchdog Alert"'
    
    sleep 3
done