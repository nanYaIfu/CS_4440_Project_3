// Names: Ifunanya Okafor and Andy Lim || Course: CS 4440-03
// Description: TCP disk server with mmap-backed 128-byte sectors.
//              Thread-per-connection; supports I / R c s / W c s l [data], as instructed.
// Compile Build: gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread disk_server.c -o disk_server
// Run:           ./disk_server <port> <cylinders> <sectors_per_cyl> <track_us_us> <backing_file> [--sync=immediate|after]
// Run (example): ./disk_server 9090 200 32 500 disk.img --sync=after

// Libraries used
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Constants defined
#define _POSIX_C_SOURCE 200809L
#define BLOCK_SIZE 128
#define BACKLOG 64

typedef enum { SYNC_IMMEDIATE = 0, SYNC_AFTER = 1 } sync_mode_t;

typedef struct {
    uint8_t *base;            // mmap base
    size_t   bytes;           // total mapped size
    int      fd;              // backing file descriptor
    int      cylinders;       // geometry
    int      sectors;
    int      track_us;        // per-track latency (microseconds)
    int      current_cyl;     // simulated head position
    sync_mode_t sync_mode;    // reply timing mode
    pthread_mutex_t lock;     // serialize head movement + media access
} disk_t;

static volatile sig_atomic_t g_stop = 0;

static void on_sigint(int signo) {
    (void)signo;
    g_stop = 1;
}

// ---- Utility: robust I/O --------------------------------------------------
static ssize_t read_full(int fd, void *buf, size_t n) {
    uint8_t *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t r = read(fd, p, left);
        if (r == 0) return (ssize_t)(n - left); // EOF
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += r; left -= (size_t)r;
    }
    return (ssize_t)n;
}

static ssize_t write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p = buf;
    size_t left = n;
    while (left > 0) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        p += w; left -= (size_t)w;
    }
    return (ssize_t)n;
}

// Reads next ASCII token (non-empty, whitespace-separated). Returns:
//  1 on success, 0 on clean EOF (before any token), -1 on error.
// On success, token is NUL-terminated in out (max outsz). If token exceeds
// outsz-1, it is truncated. <--- MOST IMPORTANT PIECE
static int read_token(int fd, char *out, size_t outsz) {
    char ch;
    // Skip leading whitespace (if any)
    for (;;) {
        ssize_t r = read(fd, &ch, 1);
        if (r == 0) return 0;         
        if (r < 0) { if (errno == EINTR) continue; return -1; }
        if (ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r') break;
    }
    size_t i = 0;
    for (;;) {
        if (i + 1 < outsz) out[i++] = ch; // store (with room for NUL)
        // Peek next char
        ssize_t r = read(fd, &ch, 1);
        if (r == 0) { out[i] = '\0'; return 1; }
        if (r < 0) { if (errno == EINTR) continue; out[i] = '\0'; return -1; }
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
            out[i] = '\0';
            return 1;
        }
        // else loop; keeps on consuming
    }
}

static int mk_listen_socket(const char *port) {
    int sfd = -1; struct addrinfo hints = {0}, *res = NULL, *it;
    hints.ai_family = AF_UNSPEC; 
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); return -1; }
    for (it = res; it; it = it->ai_next) {
        sfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sfd < 0) continue;
        int yes = 1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(sfd, it->ai_addr, it->ai_addrlen) == 0) {
            if (listen(sfd, BACKLOG) == 0) break;
        }
        close(sfd); sfd = -1;
    }
    freeaddrinfo(res);
    return sfd;
}

static off_t sector_offset(const disk_t *d, int c, int s) {
    return (off_t)(( (long long)c * d->sectors + s) * BLOCK_SIZE);
}

static bool valid_csl(const disk_t *d, int c, int s, int l) {
    if (c < 0 || c >= d->cylinders) return false;
    if (s < 0 || s >= d->sectors) return false;
    if (l < 0 || l > BLOCK_SIZE) return false;
    off_t off = sector_offset(d, c, s);
    if (off < 0 || off + BLOCK_SIZE > (off_t)d->bytes) return false;
    return true;
}

static void simulate_seek_locked(disk_t *d, int target_c) {
    int delta = target_c - d->current_cyl;
    if (delta < 0) delta = -delta;
    if (d->track_us > 0 && delta > 0) {
        struct timespec ts;
        long total_us = (long)delta * d->track_us;
        ts.tv_sec = total_us / 1000000L;
        ts.tv_nsec = (total_us % 1000000L) * 1000L;
        nanosleep(&ts, NULL);
    }
    d->current_cyl = target_c;
}

static void *client_thread(void *arg) {
    int cfd = *(int *)arg; free(arg);
    extern disk_t g_disk; // defined in main (see main() below)

    char tok[64];
    for (;;) {
        int r = read_token(cfd, tok, sizeof(tok));
        if (r == 0) break;           // EOF
        if (r < 0) { perror("read_token"); break; }
        if (tok[0] == 'I' && tok[1] == '\0') {
            char b[64];
            int n = snprintf(b, sizeof(b), "%d %d\n", g_disk.cylinders, g_disk.sectors);
            if (write_full(cfd, b, (size_t)n) < 0) break;
        } else if (tok[0] == 'R' && tok[1] == '\0') {
            // Expect c and s
            char t1[32], t2[32];
            if (read_token(cfd, t1, sizeof(t1)) <= 0 || read_token(cfd, t2, sizeof(t2)) <= 0) { break; }
            int c = atoi(t1), s = atoi(t2);
            if (!valid_csl(&g_disk, c, s, BLOCK_SIZE)) {
                char z = '0'; if (write_full(cfd, &z, 1) < 0) break; continue;
            }
            // Simulate seek + read
            pthread_mutex_lock(&g_disk.lock);
            simulate_seek_locked(&g_disk, c);
            off_t off = sector_offset(&g_disk, c, s);
            char ok = '1';
            if (write_full(cfd, &ok, 1) < 0) { pthread_mutex_unlock(&g_disk.lock); break; }
            if (write_full(cfd, g_disk.base + off, BLOCK_SIZE) < 0) { pthread_mutex_unlock(&g_disk.lock); break; }
            pthread_mutex_unlock(&g_disk.lock);
        } else if (tok[0] == 'W' && tok[1] == '\0') {
            char t1[32], t2[32], t3[32];
            if (read_token(cfd, t1, sizeof(t1)) <= 0 || read_token(cfd, t2, sizeof(t2)) <= 0 || read_token(cfd, t3, sizeof(t3)) <= 0) { break; }
            int c = atoi(t1), s = atoi(t2), l = atoi(t3);
            bool ok_args = valid_csl(&g_disk, c, s, l);

            // Read payload (if l > 0) regardless of validity to keep stream in sync
            uint8_t buf[BLOCK_SIZE];
            memset(buf, 0, sizeof(buf));
            if (l > 0) {
                size_t want = (size_t)((l > BLOCK_SIZE) ? BLOCK_SIZE : l);
                ssize_t rr = read_full(cfd, buf, want);
                if (rr < 0 || (size_t)rr < want) { break; }
            }

            if (!ok_args) {
                char z = '0'; if (write_full(cfd, &z, 1) < 0) break; continue;
            }

            if (g_disk.sync_mode == SYNC_IMMEDIATE) {
                char one = '1'; if (write_full(cfd, &one, 1) < 0) break;
            }

            pthread_mutex_lock(&g_disk.lock);
            simulate_seek_locked(&g_disk, c);
            off_t off = sector_offset(&g_disk, c, s);
            // Write l bytes, zero-fill remainder
            memcpy(g_disk.base + off, buf, (size_t)l);
            if (l < BLOCK_SIZE) memset(g_disk.base + off + l, 0, (size_t)(BLOCK_SIZE - l));
            if (g_disk.sync_mode == SYNC_AFTER) {
                // make durable before responding
                msync(g_disk.base + off, BLOCK_SIZE, MS_SYNC);
                char one = '1'; if (write_full(cfd, &one, 1) < 0) { pthread_mutex_unlock(&g_disk.lock); break; }
            }
            pthread_mutex_unlock(&g_disk.lock);
        } else {
            // Unknown token — drain until newline and ignore
            const char x = '\n';
            (void)x; // no-op; you may wish to log
        }
    }

    close(cfd);
    return NULL;
}

// Global disk instance
static disk_t g_disk;

typedef struct { const char *port; int cyl; int sec; int track_us; const char *file; sync_mode_t sync; } args_t;

static void usage(const char *prog) {
    fprintf(stderr,
        "Usage: %s <port> <cylinders> <sectors_per_cyl> <track_us> <backing_file> [--sync=immediate|after]\n",
        prog);
}

// Main function
int main(int argc, char **argv) {
    if (argc < 6) { usage(argv[0]); return 1; }
    args_t A = {0};
    A.port = argv[1]; A.cyl = atoi(argv[2]); A.sec = atoi(argv[3]); A.track_us = atoi(argv[4]); A.file = argv[5]; A.sync = SYNC_AFTER;
    if (argc >= 7) {
        if (strcmp(argv[6], "--sync=immediate") == 0) A.sync = SYNC_IMMEDIATE;
        else if (strcmp(argv[6], "--sync=after") == 0) A.sync = SYNC_AFTER;
        else { usage(argv[0]); return 1; }
    }
    if (A.cyl <= 0 || A.sec <= 0 || A.track_us < 0) { usage(argv[0]); return 1; }

    signal(SIGINT, on_sigint);

    // Prepare backing file
    int fd = open(A.file, O_RDWR | O_CREAT, 0644);
    if (fd < 0) { perror("open backing file"); return 1; }
    size_t total = (size_t)A.cyl * (size_t)A.sec * (size_t)BLOCK_SIZE;
    if (ftruncate(fd, (off_t)total) < 0) { perror("ftruncate"); close(fd); return 1; }
    void *base = mmap(NULL, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    // Initializing disk
    g_disk.base = (uint8_t *)base;
    g_disk.bytes = total;
    g_disk.fd = fd;
    g_disk.cylinders = A.cyl;
    g_disk.sectors = A.sec;
    g_disk.track_us = A.track_us;
    g_disk.current_cyl = 0;
    g_disk.sync_mode = A.sync;
    pthread_mutex_init(&g_disk.lock, NULL);

    int lfd = mk_listen_socket(A.port);
    if (lfd < 0) { fprintf(stderr, "Failed to listen on %s\n", A.port); return 1; }
    fprintf(stderr, "disk_server listening on %s (cyl=%d sec=%d track_us=%d sync=%s)\n",
            A.port, A.cyl, A.sec, A.track_us, (A.sync==SYNC_AFTER?"after":"immediate"));

    // Accept loop
    while (!g_stop) {
        struct sockaddr_storage ss; socklen_t slen = sizeof(ss);
        int cfd = accept(lfd, (struct sockaddr *)&ss, &slen);
        if (cfd < 0) { if (errno == EINTR) continue; perror("accept"); break; }
        int *heap_fd = malloc(sizeof(int)); if (!heap_fd) { close(cfd); continue; }
        *heap_fd = cfd;
        pthread_t th; pthread_create(&th, NULL, client_thread, heap_fd); pthread_detach(th);
    }

    close(lfd);
    msync(g_disk.base, g_disk.bytes, MS_SYNC);
    munmap(g_disk.base, g_disk.bytes);
    close(g_disk.fd);
    return 0;
}
