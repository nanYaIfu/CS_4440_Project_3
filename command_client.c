// Names: Ifunanya Okafor and Andy Lim || Course: CS 4440-03
// Description: Interactive command client for manual testing (I, R c s, W c s l).
//              Prints hex dump for reads; prompts for exactly l data bytes on writes.
// Compile Build: gcc -O2 -std=c17 -Wall -Wextra -pedantic disk_client_cli.c -o disk_client_cli
// Run:           ./command_client <host> <port>
// Example: ./command_client 127.0.0.1 9090

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

static void hexdump(const uint8_t *p, size_t n) {
    for (size_t i=0;i<n;i+=16) {
        printf("%04zx : ", i);
        for (size_t j=0;j<16;j++) {
            if (i+j<n) printf("%02x ", p[i+j]); else printf("   ");
        }
        printf(" | ");
        for (size_t j=0;j<16;j++) {
            if (i+j<n) {
                unsigned char c=p[i+j]; putchar((c>=32&&c<127)?c:'.');
            }
        }
        putchar('\n');
    }
}

// Main function
int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]); return 1; }
    int fd = connect_to(argv[1], argv[2]); if (fd < 0) { perror("connect"); return 1; }
    printf("Connected. Type commands: I | R c s | W c s l\n");

    char *line = NULL; size_t cap = 0;
    while (printf("> "), fflush(stdout), getline(&line, &cap, stdin) != -1) {
        // Trim the trailing newline/space
        size_t len = strlen(line); if (len>0 && line[len-1]=='\n') line[len-1]='\0';
        if (line[0] == '\0') continue;

        if (line[0] == 'I') {
            const char *msg = "I ";
            if (write_full(fd, msg, strlen(msg)) < 0) { perror("write"); break; }
            char buf[128] = {0}; ssize_t r = read(fd, buf, sizeof(buf)-1);
            if (r <= 0) { perror("read"); break; }
            printf("Server geometry: %s", buf);
        } else if (line[0] == 'R') {
            int c, s; if (sscanf(line, "R %d %d", &c, &s) != 2) { puts("Usage: R c s"); continue; }
            char out[64]; int n = snprintf(out, sizeof(out), "R %d %d ", c, s);
            if (write_full(fd, out, (size_t)n) < 0) { perror("write"); break; }
            char status; if (read_full(fd, &status, 1) != 1) { perror("read status"); break; }
            if (status == '0') { puts("READ: invalid c/s"); continue; }
            uint8_t blk[BLOCK_SIZE]; if (read_full(fd, blk, BLOCK_SIZE) != BLOCK_SIZE) { perror("read blk"); break; }
            printf("READ OK (c=%d s=%d)\n", c, s); hexdump(blk, BLOCK_SIZE);
        } else if (line[0] == 'W') {
            int c, s, l; if (sscanf(line, "W %d %d %d", &c, &s, &l) != 3) { puts("Usage: W c s l"); continue; }
            if (l < 0 || l > BLOCK_SIZE) { puts("l must be 0..128"); continue; }
            char out[64]; int n = snprintf(out, sizeof(out), "W %d %d %d ", c, s, l);
            if (write_full(fd, out, (size_t)n) < 0) { perror("write hdr"); break; }
            printf("DATA: enter exactly %d bytes (printable); shorter will be zero‑filled)\n", l);
            char *dline = NULL; size_t dcap = 0; ssize_t r = getline(&dline, &dcap, stdin);
            if (r < 0) { perror("getline data"); free(dline); break; }
            // Send exactly l bytes (truncate or pad with zeros)
            uint8_t buf[BLOCK_SIZE] = {0};
            if (l > 0) {
                size_t to_copy = (size_t)l;
                if ((size_t)r > to_copy) memcpy(buf, dline, to_copy); else memcpy(buf, dline, (size_t)r);
                if ((size_t)r < (size_t)l) memset(buf + r, 0, (size_t)l - (size_t)r);
                if (write_full(fd, buf, (size_t)l) < 0) { perror("write data"); free(dline); break; }
            }
            free(dline);
            char status; if (read_full(fd, &status, 1) != 1) { perror("read status"); break; }
            puts(status=='1' ? "WRITE OK" : "WRITE FAILED");
        } else if (!strcmp(line, "quit") || !strcmp(line, "exit")) {
            break;
        } else {
            puts("Unknown. Use: I | R c s | W c s l");
        }
    }
    free(line); close(fd); return 0;
}