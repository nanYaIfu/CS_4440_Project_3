# Makefile — builds for Parts 1–5
CC       := gcc
CFLAGS   := -O2 -std=c17 -Wall -Wextra -pedantic
LDFLAGS  := -pthread

# ---- Binaries ----
Q1_BINS := server client
Q2_BINS := ls_server ls_client
Q3_BINS := disk_server command_client random_client
Q4_BINS := file_system_server file_system_client
Q5_BINS := file_system_server+directory file_system_directory_client

ALL := $(Q1_BINS) $(Q2_BINS) $(Q3_BINS) $(Q4_BINS) $(Q5_BINS)

.PHONY: all q1 q2 q3 q4 q5 clean
all: $(ALL)

q1: $(Q1_BINS)
q2: $(Q2_BINS)
q3: $(Q3_BINS)
q4: $(Q4_BINS)
q5: $(Q5_BINS)

# Compile
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------------- Part 1: Basic CLient-Server ----------------
server: server.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

client: client.c
	$(CC) $(CFLAGS) $< -o $@

# ---------------- Part 2 - Directory Listing Server ----------------
ls_server: ls_server.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

ls_client: ls_client.c
	$(CC) $(CFLAGS) $< -o $@

# ---------------- Part 3 - Disk Server ----------------
disk_server: disk_server.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

command_client: command_client.c
	$(CC) $(CFLAGS) $< -o $@

random_client: random_client.c
	$(CC) $(CFLAGS) $< -o $@

# ---------------- Part 4 - File System Server ----------------
file_system_server: file_system_server.c
	$(CC) $(CFLAGS) $(LDFLAGS) $< -o $@

file_system_client: file_system_client.c
	$(CC) $(CFLAGS) $< -o $@

# ---------------- Part 5 - Directory Structure ----------------
file_system_server+directory: file_system_server+directory.c
	$(CC) $(CFLAGS) $(LDFLAGS) "file_system_server+directory.c" -o $@

file_system_directory_client: file_system_directory_client.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(ALL) *.o
