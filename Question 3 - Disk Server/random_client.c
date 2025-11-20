// Names: Ifunanya Okafor and Andy Lim || Course: CS 4440-03
// Description: Same as command client, but randomizd. Queries geometry, then performs N random
//              reads/writes over valid (c,s). Writes are full 128-byte random blocks.
//              Prints progress as 'R'/'W' (or '!' on error).
// Compile Build: gcc -O2 -std=c17 -Wall -Wextra -pedantic disk_client_rand.c -o disk_client_rand
// Run:           ./random_client <host> <port> <N_ops> <seed>
// Example: ./random_client  127.0.0.1 9090 10000 42

// Libraries used
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Constants defined
#define _POSIX_C_SOURCE 200809L
#define BLOCK_SIZE 128

static ssize_t read_full(int fd, void *buf, size_t n) {
    uint8_t *p = buf; size_t left = n;
    while (left > 0) { ssize_t r = read(fd, p, left); if (r == 0) return (ssize_t)(n-left); if (r < 0) { if (errno==EINTR) continue; return -1; } p+=r; left-= (size_t)r; }
    return (ssize_t)n;
}
static ssize_t write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf; size_t left = n;
    while (left > 0) { ssize_t w = write(fd, p, left); if (w < 0) { if (errno==EINTR) continue; return -1; } p+=w; left-= (size_t)w; }
    return (ssize_t)n;
}

static int connect_to(const char *host, const char *port) {
    struct addrinfo hints = {0}, *res=NULL, *it; int fd=-1;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    for (it = res; it; it = it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd < 0) continue;
        if (connect(fd, it->ai_addr, it->ai_addrlen) == 0) break;
        close(fd); fd = -1;
    }
    freeaddrinfo(res);
    return fd;
}

static void fill_rand(uint8_t *p, size_t n, unsigned *seed) {
    for (size_t i=0;i<n;i++) p[i] = (uint8_t)(rand_r(seed) & 0xFF);
}

// Main function
int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "Usage: %s <host> <port> <N_ops> <seed>\n", argv[0]); return 1; }
    const char *host = argv[1], *port = argv[2];
    long N = strtol(argv[3], NULL, 10); unsigned seed = (unsigned)strtoul(argv[4], NULL, 10);
    if (N <= 0) { fprintf(stderr, "N_ops must be > 0\n"); return 1; }

    int fd = connect_to(host, port); if (fd < 0) { perror("connect"); return 1; }

    // Query geometry
    const char *msg = "I "; if (write_full(fd, msg, strlen(msg)) < 0) { perror("write I"); return 1; }
    char line[128] = {0}; ssize_t r = read(fd, line, sizeof(line)-1); if (r <= 0) { perror("read I"); return 1; }
    int CYL=0, SEC=0; if (sscanf(line, "%d %d", &CYL, &SEC) != 2) { fprintf(stderr, "Bad I response: %s\n", line); return 1; }
    fprintf(stderr, "Geometry: cyl=%d sec=%d\n", CYL, SEC);

    for (long i=0;i<N;i++) {
        int c = rand_r(&seed) % CYL;
        int s = rand_r(&seed) % SEC;
        bool do_write = (rand_r(&seed) & 1) != 0;
        if (do_write) {
            char hdr[64]; int n = snprintf(hdr, sizeof(hdr), "W %d %d %d ", c, s, BLOCK_SIZE);
            if (write_full(fd, hdr, (size_t)n) < 0) { perror("write hdr"); break; }
            uint8_t blk[BLOCK_SIZE]; fill_rand(blk, sizeof blk, &seed);
            if (write_full(fd, blk, BLOCK_SIZE) < 0) { perror("write data"); break; }
            char status; if (read_full(fd, &status, 1) != 1) { perror("read status"); break; }
            putchar(status=='1' ? 'W' : '!');
        } else {
            char hdr[64]; int n = snprintf(hdr, sizeof(hdr), "R %d %d ", c, s);
            if (write_full(fd, hdr, (size_t)n) < 0) { perror("write hdr"); break; }
            char status; if (read_full(fd, &status, 1) != 1) { perror("read status"); break; }
            if (status == '1') {
                uint8_t blk[BLOCK_SIZE]; if (read_full(fd, blk, BLOCK_SIZE) != BLOCK_SIZE) { perror("read blk"); break; }
                putchar('R');
            } else {
                putchar('!');
            }
        }
        fflush(stdout);
    }
    putchar('\n');
    close(fd);
    return 0;
}
