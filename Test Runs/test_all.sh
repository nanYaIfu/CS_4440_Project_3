#!/bin/bash
set -euo pipefail
./test_part1.sh
./test_part2.sh
./test_q3.sh
./test_q4.sh
./test_q5.sh
echo
echo "All tests completed. See test_logs/ for transcripts."
