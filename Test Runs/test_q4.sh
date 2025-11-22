#!/bin/bash
set -euo pipefail

echo "Compiling Q4 sources…"
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread "file_system_server.c" -o fs_server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          "file_system_client.c" -o fs_client
echo

logdir="test_logs"; mkdir -p "$logdir"

wait_for_port() { local p="$1"; for _ in {1..50}; do (echo >"/dev/tcp/127.0.0.1/$p") >/dev/null 2>&1 && return 0; sleep 0.1; done; echo "Port $p not ready" >&2; return 1; }
start_server()   { local cmd="$1" log="$2"; echo "[server] $cmd"; bash -lc "$cmd" >"$log" 2>&1 & echo $!; }

echo "=== Q4: flat filesystem tests ==="
PORT=10090
IMG="./fs.img"
PID=$(start_server "./fs_server $PORT 10 10 $IMG" "$logdir/q4_server.log")
trap 'kill -TERM $PID >/dev/null 2>&1 || true' EXIT
wait_for_port "$PORT"

{
  echo "F"                         # format
  echo "C alpha.txt"               # create ok
  echo "C alpha.txt"               # duplicate -> 1
  echo "W missing.txt 3"; echo "xyz"  # write missing -> 1
  echo "W alpha.txt 11"; echo "hello world"
  echo "R alpha.txt"               # read back
  echo "L 1"                       # list verbose
  echo "D alpha.txt"               # delete ok
  echo "R alpha.txt"               # read -> ERR 1
  echo "D no_such.txt"             # delete missing -> 1
  echo "quit"
} | ./fs_client 127.0.0.1 "$PORT" | tee "$logdir/q4_cli.txt"

kill -TERM "$PID" || true
trap - EXIT
echo "Q4 tests complete; transcripts in $logdir/"
