# CS 4440 Project 3

- **Q1:** Multithreaded client–server that reverses strings  
- **Q2:** “Remote `ls`” directory listing service over sockets  
- **Q3:** Disk server with 128-byte sectors over TCP  
- **Q4:** Flat filesystem (format/create/read/write/list/delete) on top of Q3  
- **Q5:** Filesystem with directories (`MKDIR/CD/PWD/RMDIR`)

All code is in **C**, using POSIX sockets/threads and a file-backed store .

---

## Table of Contents
- [Build](#build)
- [Overview](#overview)
  - [Q1 — Basic Client-Server](#q1--basic-client-server)
  - [Q2 — Directory Listing Server (`ls`)](#q2--directory-listing-server-ls)
  - [Q3 — Disk Server](#q3--disk-server)
  - [Q4 — File System Server](#q4--file-system-server)
  - [Q5 — Directory Structure](#q5--directory-structure)
- [Testing](#testing)

---

## Build

### Using `gcc` (direct compiles)

```bash
# Q1 — Basic client–server (reverse string)
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread server.c -o server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          client.c -o client

# Q2 — Directory listing server (exec ls)
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread ls_server.c -o ls_server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          ls_client.c -o ls_client

# Q3 — Disk server + clients
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread disk_server.c   -o disk_server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          command_client.c -o command_client
gcc -O2 -std=c17 -Wall -Wextra -pedantic          random_client.c  -o random_client

# Q4 — Flat filesystem
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread file_system_server.c -o file_system_server
gcc -O2 -std=c17 -Wall -Wextra -pedantic          file_system_client.c -o file_system_client

# Q5 — Filesystem with directories
gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread "file_system_server+directory.c" -o file_system_server+directory
gcc -O2 -std=c17 -Wall -Wextra -pedantic          file_system_directory_client.c   -o file_system_directory_client
```

> If you prefer a Makefile, you can adapt the above commands into the usual `all` target.

---

## Overview

### Q1 — Basic Client-Server
- **Server:** listens (default 8080 or the port defined in `server.c`), spawns a thread per client, reverses input.
- **Client:** connects and prints the reversed response.

```bash
# Terminal A
./server

# Terminal B
./client 127.0.0.1 "hello world"
# -> dlrow olleh
```

### Q2 — Directory Listing Server (`ls`)
- **Server:** listens (default 8083 unless changed); executes `/bin/ls` with client’s arguments.
- **Client:** send flags/paths; prints server’s stdout.

```bash
# Terminal A
./ls_server

# Terminal B
./ls_client 127.0.0.1 -lh ~
./ls_client 127.0.0.1 -la /tmp
```

### Q3 — Disk Server
Protocol: `I` | `R c s` | `W c s l <data>`  
- `I` → `<cyl> <sec>`
- `R` → `1<128 bytes>` or `0` (invalid)
- `W` → `1` on valid `c,s,l` (`0 ≤ l ≤ 128`), else `0`

```bash
# Terminal A
./disk_server 9090 4 8 100 ./disk.img --sync=after
#                 ^ ^ ^  ^track_us

# Terminal B (interactive CLI)
./command_client 127.0.0.1 9090
I
R 0 0
W 0 0 5
hello
R 0 0
exit

# Random workload
./random_client 127.0.0.1 9090 50 42
```

### Q4 — File System Server
Commands: `F`, `C f`, `D f`, `L b`, `R f`, `W f l <data>` (and optional `A f l <data>`)

```bash
# Terminal A
./file_system_server 10090 10 10 ./fs.img

# Terminal B
./file_system_client 127.0.0.1 10090
F
C alpha.txt
W alpha.txt 11
hello world
R alpha.txt
L 1
D alpha.txt
quit
```

### Q5 — Directory Structure
Adds: `MKDIR name`, `CD name|..|/`, `PWD`, `RMDIR name`  
`L` lists the **current** directory; with `b=1` it shows type and size.

```bash
# Terminal A
./file_system_server+directory 11090 10 10 ./fs_dirs.img

# Terminal B
./file_system_directory_client 127.0.0.1 11090
F
MKDIR docs
CD docs
C notes.txt
W notes.txt 5
hello
L 1
CD /
RMDIR docs     # fails (not empty)
CD docs
D notes.txt
CD /
RMDIR docs     # succeeds
quit
```

---

## Testing

If you included the helper scripts, you can run:

```bash
./test_q3.sh   # CLI tests, random load, short DoS burst
./test_q4.sh   # format/create/write/read/list/delete with errors
./test_q5.sh   # mkdir/cd/pwd/rmdir and file ops in subdirs
./test_all.sh  # run everything in sequence
```

Sample transcripts (“typescript”-style) demonstrate correct and error cases:
- `q3_typescript.txt`
- `q4_typescript.txt`
- `q5_typescript.txt`


