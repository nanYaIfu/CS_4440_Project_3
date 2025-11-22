#!/bin/bash
set -euo pipefail

echo "Compiling Q3 sources…"
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread "disk_server.c" -o disk_server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          "command_client.c" -o disk_client_cli
gcc -O2 -std=c17 -Wall -Wextra -pedantic          "random_client.c"  -o disk_client_rand
echo

logdir="test_logs"; mkdir -p "$logdir"

wait_for_port() { # the ip adress + TCP port
  local port="$1"
  for _ in {1..50}; do
    if (echo >"/dev/tcp/127.0.0.1/$port") >/dev/null 2>&1; then return 0; fi
    sleep 0.1
  done
  echo "ERROR: Port $port did not open in time" >&2; return 1
}

start_server() { # cmd log
  local cmd="$1" log="$2"
  echo "[server] $cmd"
  bash -lc "$cmd" >"$log" 2>&1 & echo $!
}

echo "=== Q3: disk_server + clients (correct + error runs) ==="
PORT=9090
IMG="./disk.img"
PID=$(start_server "./disk_server $PORT 4 8 100 $IMG --sync=after" "$logdir/q3_server.log")
trap 'kill -TERM $PID >/dev/null 2>&1 || true' EXIT
wait_for_port "$PORT"

{
  echo "I"
  echo "R 0 0"
  echo "W 0 0 5"; echo "hello"      # valid write
  echo "R 0 0"                      # verify
  echo "R 5 0"                      # invalid cylinder
  echo "W 0 9 3"; echo "abc"        # invalid sector
  echo "W 0 0 129"                  # invalid length
  echo "exit"
} | ./disk_client_cli 127.0.0.1 "$PORT" | tee "$logdir/q3_cli.txt"

./disk_client_rand 127.0.0.1 "$PORT" 50 42 | tee "$logdir/q3_rand.txt"

for i in $(seq 1 5); do ./disk_client_rand 127.0.0.1 "$PORT" 20 "$i" & done
wait

kill -TERM "$PID" || true
trap - EXIT
echo "Q3 tests complete; transcripts in $logdir/"
