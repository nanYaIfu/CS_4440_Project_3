#!/bin/bash
# Andy Lim and Ifunaya Okafor
# Test Script for Part 2: Directory Listing Server

echo "=========================================="
echo "Testing Part 2: Directory Listing Server"
echo "=========================================="
echo ""

# Compile if needed
if [ ! -f "./ls_server" ] || [ ! -f "./ls_client" ]; then
    echo "Compiling programs..."
    make ls_server ls_client
    echo ""
fi

# Start the server in background
echo "Starting ls_server..."
./ls_server > ls_server_test.log 2>&1 &
SERVER_PID=$!
sleep 1

# Check if server started
if ! ps -p $SERVER_PID > /dev/null; then
    echo "ERROR: ls_server failed to start"
    cat ls_server_test.log
    exit 1
fi

echo "ls_server started (PID: $SERVER_PID)"
echo ""

# Test 1: Basic listing
echo "Test 1: Basic directory listing"
./ls_client 127.0.0.1
echo ""

# Test 2: Listing with -l option
echo "Test 2: Detailed listing (-l)"
./ls_client 127.0.0.1 -l
echo ""

# Test 3: Listing with -la option
echo "Test 3: Detailed listing with hidden files (-la)"
./ls_client 127.0.0.1 -la
echo ""

# Test 4: Listing specific directory
echo "Test 4: Listing /tmp directory"
./ls_client 127.0.0.1 -l /tmp
echo ""

# Test 5: Multiple concurrent requests
echo "Test 5: Multiple concurrent clients"
for i in {1..3}; do
    ./ls_client 127.0.0.1 &
done
wait
echo ""

# Cleanup
echo "Stopping ls_server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "Part 2 testing complete!"
echo "=========================================="