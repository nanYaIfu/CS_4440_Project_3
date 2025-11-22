#!/bin/bash
set -euo pipefail

echo "Compiling Q5 sources…"
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread "file_system_server+directory.c" -o fs_server_dirs
gcc -O2 -std=c17 -Wall -Wextra -pedantic          "file_system_directory_client.c" -o fs_client_dirs
echo

logdir="test_logs"; mkdir -p "$logdir"

wait_for_port() { local p="$1"; for _ in {1..50}; do (echo >"/dev/tcp/127.0.0.1/$p") >/dev/null 2>&1 && return 0; sleep 0.1; done; echo "Port $p not ready" >&2; return 1; }
start_server()   { local cmd="$1" log="$2"; echo "[server] $cmd"; bash -lc "$cmd" >"$log" 2>&1 & echo $!; }

echo "=== Q5: directory-enabled filesystem tests ==="
PORT=11090
IMG="./fs_dirs.img"
PID=$(start_server "./fs_server_dirs $PORT 10 10 $IMG" "$logdir/q5_server.log")
trap 'kill -TERM $PID >/dev/null 2>&1 || true' EXIT
wait_for_port "$PORT"

{
  echo "F"
  echo "PWD"                      # if supported
  echo "MKDIR docs"               # 0
  echo "MKDIR docs"               # 1 (exists)
  echo "MKDIR bad/name"           # 2 (invalid) if validated
  echo "L 1"                      # should show docs
  echo "CD docs"                  # 0
  echo "C notes.txt"              # 0
  echo "W notes.txt 5"; echo "hello"
  echo "L 1"
  echo "CD notes.txt"             # error (not a directory)
  echo "CD .."                    # 0
  echo "RMDIR docs"               # 2 (not empty)
  echo "CD docs"; echo "D missing.txt"  # 1
  echo "D notes.txt"              # 0
  echo "CD /"; echo "RMDIR docs"  # 0
  echo "PWD"
  echo "quit"
} | ./fs_client_dirs 127.0.0.1 "$PORT" | tee "$logdir/q5_cli.txt"

kill -TERM "$PID" || true
trap - EXIT
echo "Q5 tests complete; transcripts in $logdir/"
