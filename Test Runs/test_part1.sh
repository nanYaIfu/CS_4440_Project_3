#!/bin/bash
# Andy Lim and Ifunaya Okafor
# Test Script for Part 1: String Reversal Server

echo "=========================================="
echo "Testing Part 1: String Reversal Server"
echo "=========================================="
echo ""

# Compile if needed
if [ ! -f "./server" ] || [ ! -f "./client" ]; then
    echo "Compiling programs..."
    make server client
    echo ""
fi

# Start the server in background
echo "Starting server..."
./server > server_test.log 2>&1 &
SERVER_PID=$!
sleep 1

# Check if server started
if ! ps -p $SERVER_PID > /dev/null; then
    echo "ERROR: Server failed to start"
    cat server_test.log
    exit 1
fi

echo "Server started (PID: $SERVER_PID)"
echo ""

# Test 1: Simple string
echo "Test 1: Reversing 'Hello'"
./client 127.0.0.1 "Hello"
echo ""

# Test 2: Longer string
echo "Test 2: Reversing 'Hello World'"
./client 127.0.0.1 "Hello World"
echo ""

# Test 3: Numbers
echo "Test 3: Reversing '12345'"
./client 127.0.0.1 "12345"
echo ""

# Test 4: Multiple clients
echo "Test 4: Multiple concurrent clients"
for i in {1..5}; do
    ./client 127.0.0.1 "Message$i" &
done
wait
echo ""

# Cleanup
echo "Stopping server..."
kill $SERVER_PID 2>/dev/null
wait $SERVER_PID 2>/dev/null

echo ""
echo "=========================================="
echo "Part 1 testing complete!"
echo "=========================================="