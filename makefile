# Makefile — builds for Parts 1,2, 3, 4, 5

CC       := gcc
CFLAGS   := -O2 -std=c17 -Wall -Wextra -pedantic
LDFLAGS  := -pthread

# ===== Question 3: Disk Server =====
Q3_SRCS  := disk_server.c command_client.c random_client.c
Q3_BINS  := disk_server command_client random_client

# ===== Question 4: File System Server =====
Q4_SRCS  := file_system_server.c file_system_client.c
Q4_BINS  := file_system_server file_system_client

# ===== Question 5: Directory Structure =====
Q5_SRCS  := file_system_server+directory.c file_system_directory_client.c
Q5_BINS  := file_system_server+directory file_system_directory_client

# All objects
OBJS     := $(Q3_SRCS:.c=.o) $(Q4_SRCS:.c=.o) $(Q5_SRCS:.c=.o)

.PHONY: all q3 q4 q5 clean

all: $(Q3_BINS) $(Q4_BINS) $(Q5_BINS)

q3: $(Q3_BINS)
q4: $(Q4_BINS)
q5: $(Q5_BINS)

# ---------- Compile ----------
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# ---------- Q3 ----------
disk_server: disk_server.o
	$(CC) $^ $(LDFLAGS) -o $@

command_client: command_client.o
	$(CC) $^ $(LDFLAGS) -o $@

random_client: random_client.o
	$(CC) $^ $(LDFLAGS) -o $@

# ---------- Q4 ----------
file_system_server: file_system_server.o
	$(CC) $^ $(LDFLAGS) -o $@

file_system_client: file_system_client.o
	$(CC) $^ $(LDFLAGS) -o $@

# ---------- Q5 ----------
file_system_server+directory: file_system_server+directory.o
	$(CC) $^ $(LDFLAGS) -o $@

file_system_directory_client: file_system_directory_client.o
	$(CC) $^ $(LDFLAGS) -o $@

# ---------- Cleanuo ----------
clean:
	rm -f *.o $(Q3_BINS) $(Q4_BINS) $(Q5_BINS)
