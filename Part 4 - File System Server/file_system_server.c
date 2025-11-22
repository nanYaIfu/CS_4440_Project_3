// Names: Ifunanya Okafor and Andy Lim || Course: CS 4440-03
// Description: Flat filesystem TCP server on a 128-byte block device, using mmap for persistence.
//              Single directory with fixed-size entries; FAT for block allocation.
//              Protocol: F | C f | D f | L b | R f | W f l <data> | A f l <data>
// Compile Build: gcc -O2 -std=c17 -Wall -Wextra -pedantic -pthread fs_server.c -o fs_server
// Run:           ./fs_server <port> <cylinders> <sectors_per_cyl> <backing_file>
// Example: ./fs_server 10090 200 32 ./fs.img

// Libraries used
#define _POSIX_C_SOURCE 200809L
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
#include <unistd.h>

// Constants defined
#define BLOCK_SIZE 128
#define NAME_MAXLEN 48
#define DIR_DEFAULT_BLOCKS 8   // Note: This is because 8 * 128 = 1024 bytes; with 64B entries -> up to 16 files
#define BACKLOG 64
#define FAT_FREE (-1)
#define FAT_EOC  (-2)

// ---- On-disk structures (packed into 128-byte blocks [see constants defined above] ) ----------------------

typedef struct {
    uint32_t magic;            // 'FSL1' = 0x46534C31
    uint32_t cylinders;
    uint32_t sectors;
    uint32_t block_size;       // 128
    uint32_t total_blocks;     // cylinders * sectors
    uint32_t fat_start;        // block index of FAT start
    uint32_t fat_blocks;       // number of blocks that hold FAT
    uint32_t dir_start;        // block index of directory start
    uint32_t dir_blocks;       // number of directory blocks
    uint32_t data_start;       // first data block index
    uint32_t max_files;        // computed from dir_blocks
    uint8_t  reserved[128 - (11*4)];
} __attribute__((packed)) super_t;

// 64-byte directory entry: fits 2 per 128-byte block
typedef struct {
    uint8_t  used;             // 0 free, 1 used
    uint8_t  _pad1[3];         // alignment padding
    int32_t  first_block;      // -1 if empty
    uint32_t size_bytes;       // file size in bytes
    char     name[NAME_MAXLEN];// NUL-terminated (truncated if needed)
    uint8_t  _pad2[64 - (1+3+4+4+NAME_MAXLEN)];
} __attribute__((packed)) dirent_t;

// ---- In-memory state -------------------------------------------------------

typedef struct {
    int fd;
    uint8_t *base;             // mmap base
    size_t bytes;              // mapping size (total_blocks * BLOCK_SIZE)
    super_t *sb;               // pointer to superblock in mapping
    int32_t *fat;              // pointer to FAT (int32 table)
    dirent_t *dir;             // pointer to first dir block
    pthread_mutex_t lock;      // serialize FS metadata + data access
} fs_t;

static volatile sig_atomic_t g_stop = 0;
static fs_t g_fs;

static void on_sigint(int signo) { (void)signo; g_stop = 1; }

// ---- Helpers ---------------------------------------------------------------

static inline size_t blocks_total(uint32_t cyl, uint32_t sec) {
    return (size_t)cyl * (size_t)sec;
}
static inline uint8_t *block_ptr(fs_t *fs, uint32_t bindex) {
    return fs->base + (size_t)bindex * BLOCK_SIZE;
}

static int mk_listen_socket(const char *port) {
    int sfd = -1; struct addrinfo hints = {0}, *res = NULL, *it;
    hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM; hints.ai_flags = AI_PASSIVE;
    int rc = getaddrinfo(NULL, port, &hints, &res);
    if (rc != 0) { fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rc)); return -1; }
    for (it = res; it; it = it->ai_next) {
        sfd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (sfd < 0) continue;
        int yes=1; setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        if (bind(sfd, it->ai_addr, it->ai_addrlen) == 0) { if (listen(sfd, BACKLOG) == 0) break; }
        close(sfd); sfd = -1;
    }
    freeaddrinfo(res); return sfd;
}

static ssize_t read_full(int fd, void *buf, size_t n) {
    uint8_t *p=buf; size_t left=n;
    while (left>0) { ssize_t r=read(fd,p,left); if (r==0) return (ssize_t)(n-left); if (r<0){ if(errno==EINTR) continue; return -1;} p+=r; left-=(size_t)r; }
    return (ssize_t)n;
}
static ssize_t write_full(int fd, const void *buf, size_t n) {
    const uint8_t *p=buf; size_t left=n;
    while (left>0) { ssize_t w=write(fd,p,left); if (w<0){ if(errno==EINTR) continue; return -1;} p+=w; left-=(size_t)w; }
    return (ssize_t)n;
}

// token reader: reads next non-empty whitespace-separated ASCII token
static int read_token(int fd, char *out, size_t outsz) {
    char ch;
    for (;;) { ssize_t r = read(fd,&ch,1); if (r==0) return 0; if (r<0){ if(errno==EINTR) continue; return -1;} if (ch!=' '&&ch!='\t'&&ch!='\n'&&ch!='\r') break; }
    size_t i=0;
    for (;;) {
        if (i+1<outsz) out[i++]=ch;
        ssize_t r=read(fd,&ch,1);
        if (r==0){ out[i]='\0'; return 1; }
        if (r<0){ if(errno==EINTR) continue; out[i]='\0'; return -1; }
        if (ch==' '||ch=='\t'||ch=='\n'||ch=='\r'){ out[i]='\0'; return 1; }
    }
}

// ---- FS core ---------------------------------------------------------------

static void fs_bind_views(fs_t *fs) {
    fs->sb  = (super_t *)block_ptr(fs, 0);
    fs->fat = (int32_t *)block_ptr(fs, fs->sb->fat_start);
    fs->dir = (dirent_t *)block_ptr(fs, fs->sb->dir_start);
}

static int fs_format(fs_t *fs, uint32_t cyl, uint32_t sec) {
    size_t total_blocks = blocks_total(cyl, sec);
    if (total_blocks < 16) return -1; // need some room for meta + data

    // Layout
    uint32_t fat_entries_per_block = BLOCK_SIZE / sizeof(int32_t); // 32
    uint32_t fat_blocks = (uint32_t)((total_blocks + fat_entries_per_block - 1) / fat_entries_per_block);
    uint32_t dir_blocks = DIR_DEFAULT_BLOCKS;
    if (1 + fat_blocks + dir_blocks >= total_blocks) return -1;

    super_t sb = {0};
    sb.magic        = 0x46534C31u; // 'FSL1'
    sb.cylinders    = cyl;
    sb.sectors      = sec;
    sb.block_size   = BLOCK_SIZE;
    sb.total_blocks = (uint32_t)total_blocks;
    sb.fat_start    = 1;
    sb.fat_blocks   = fat_blocks;
    sb.dir_start    = sb.fat_start + sb.fat_blocks;
    sb.dir_blocks   = dir_blocks;
    sb.data_start   = sb.dir_start + sb.dir_blocks;
    sb.max_files    = (sb.dir_blocks * BLOCK_SIZE) / sizeof(dirent_t);

    // Write superblock
    memcpy(block_ptr(fs,0), &sb, sizeof(sb));

    fs_bind_views(fs);

    // Initialize FAT
    size_t fat_entries = total_blocks;
    for (size_t i=0;i<fat_entries;i++) {
        fs->fat[i] = FAT_FREE;
    }
    // Reserve metadata blocks (super, fat region, dir region)
    for (uint32_t b=0; b<sb.data_start; b++) fs->fat[b] = FAT_EOC;

    // Initialize directory
    size_t dir_entries = sb.max_files;
    for (size_t i=0;i<dir_entries;i++) {
        dirent_t *de = &fs->dir[i];
        memset(de, 0, sizeof(*de));
        de->used = 0; de->first_block = -1; de->size_bytes = 0; de->name[0]='\0';
    }

    // Persist
    msync(fs->base, fs->bytes, MS_SYNC);
    return 0;
}

static int dir_find(fs_t *fs, const char *name) {
    for (uint32_t i=0;i<fs->sb->max_files;i++) {
        dirent_t *de = &fs->dir[i];
        if (de->used && strncmp(de->name, name, NAME_MAXLEN) == 0) return (int)i;
    }
    return -1;
}
static int dir_find_free(fs_t *fs) {
    for (uint32_t i=0;i<fs->sb->max_files;i++) if (!fs->dir[i].used) return (int)i; return -1;
}

static int32_t alloc_chain(fs_t *fs, uint32_t blocks_needed) {
    int32_t head = -1, prev = -1;
    uint32_t got = 0;
    for (uint32_t i = fs->sb->data_start; i < fs->sb->total_blocks && got < blocks_needed; i++) {
        if (fs->fat[i] == FAT_FREE) {
            if (head < 0) head = (int32_t)i; else fs->fat[prev] = (int32_t)i;
            prev = (int32_t)i; fs->fat[i] = FAT_EOC; got++;
        }
    }
    if (got < blocks_needed) {
        // rollback
        int32_t b = head;
        while (b >= 0) { int32_t next = fs->fat[b]; fs->fat[b] = FAT_FREE; if (next == FAT_EOC) break; b = next; }
        return -1;
    }
    return head;
}

static void free_chain(fs_t *fs, int32_t head) {
    int safety = 0;
    while (head >= 0 && safety < (int)fs->sb->total_blocks) {
        int32_t next = fs->fat[head];
        fs->fat[head] = FAT_FREE;
        if (next == FAT_EOC) break;
        head = next; safety++;
    }
}

static ssize_t io_read_chain(fs_t *fs, int32_t head, uint8_t *out, size_t want) {
    size_t off=0; int safety=0; int32_t b=head;
    while (b >= 0 && off < want && safety < (int)fs->sb->total_blocks) {
        size_t to_copy = (want - off < BLOCK_SIZE) ? (want - off) : BLOCK_SIZE;
        memcpy(out + off, block_ptr(fs,(uint32_t)b), to_copy);
        off += to_copy;
        int32_t next = fs->fat[b];
        if (next == FAT_EOC) break; b = next; safety++;
    }
    return (ssize_t)off;
}

static ssize_t io_write_chain(fs_t *fs, int32_t head, const uint8_t *in, size_t nbytes) {
    size_t off=0; int safety=0; int32_t b=head;
    while (b >= 0 && off < nbytes && safety < (int)fs->sb->total_blocks) {
        size_t to_copy = (nbytes - off < BLOCK_SIZE) ? (nbytes - off) : BLOCK_SIZE;
        memcpy(block_ptr(fs,(uint32_t)b), in + off, to_copy);
        if (to_copy < BLOCK_SIZE) memset(block_ptr(fs,(uint32_t)b)+to_copy, 0, BLOCK_SIZE-to_copy);
        off += to_copy;
        int32_t next = fs->fat[b];
        if (next == FAT_EOC) break; b = next; safety++;
    }
    return (ssize_t)off;
}

static int ensure_capacity(fs_t *fs, dirent_t *de, size_t new_size) {
    uint32_t need_blocks = (uint32_t)((new_size + BLOCK_SIZE - 1) / BLOCK_SIZE);
    uint32_t have_blocks = 0;
    // count current blocks
    if (de->first_block >= 0) {
        int32_t b = de->first_block; int safety=0; have_blocks=1;
        while (fs->fat[b] != FAT_EOC && safety < (int)fs->sb->total_blocks) { b = fs->fat[b]; have_blocks++; safety++; }
    }
    if (need_blocks == have_blocks) return 0;
    if (need_blocks == 0) {
        if (de->first_block >= 0) { free_chain(fs, de->first_block); de->first_block = -1; }
        return 0;
    }
    if (have_blocks == 0) {
        int32_t head = alloc_chain(fs, need_blocks);
        if (head < 0) return -1; de->first_block = head; return 0;
    }
    if (need_blocks > have_blocks) {
        // extend chain by (need-have)
        uint32_t add = need_blocks - have_blocks;
        // find tail
        int32_t tail = de->first_block; while (fs->fat[tail] != FAT_EOC) tail = fs->fat[tail];
        // allocate additional blocks
        int32_t head2 = alloc_chain(fs, add);
        if (head2 < 0) return -1;
        // splice: replace EOC at tail with head2
        fs->fat[tail] = head2;
        return 0;
    }
    // need < have: shrink
    uint32_t keep = need_blocks;
    int32_t b = de->first_block; int32_t prev = -1;
    for (uint32_t i=0;i<keep;i++) { prev = b; b = fs->fat[b]; }
    // prev is last we keep; b is first to free (may be EOC)
    if (prev >= 0) fs->fat[prev] = FAT_EOC;
    if (b >= 0 && b != FAT_EOC) free_chain(fs, b);
    return 0;
}

// ---- Command handlers ------------------------------------------------------

static int cmd_format(fs_t *fs) {
    return fs_format(fs, fs->sb->cylinders, fs->sb->sectors);
}

static int cmd_create(fs_t *fs, const char *name) {
    if (strlen(name) == 0) return 2;
    if (dir_find(fs, name) >= 0) return 1;
    int idx = dir_find_free(fs); if (idx < 0) return 2;
    dirent_t *de = &fs->dir[idx]; memset(de, 0, sizeof(*de));
    de->used = 1; de->first_block = -1; de->size_bytes = 0; strncpy(de->name, name, NAME_MAXLEN-1); de->name[NAME_MAXLEN-1]='\0';
    msync((void*)de, sizeof(*de), MS_SYNC);
    return 0;
}

static int cmd_delete(fs_t *fs, const char *name) {
    int idx = dir_find(fs, name); if (idx < 0) return 1;
    dirent_t *de = &fs->dir[idx];
    if (de->first_block >= 0) free_chain(fs, de->first_block);
    memset(de, 0, sizeof(*de)); msync((void*)de, sizeof(*de), MS_SYNC);
    return 0;
}

static int cmd_write(fs_t *fs, const char *name, const uint8_t *data, size_t len) {
    int idx = dir_find(fs, name); if (idx < 0) return 1;
    dirent_t *de = &fs->dir[idx];
    if (ensure_capacity(fs, de, len) < 0) return 2;
    if (len > 0 && de->first_block >= 0) io_write_chain(fs, de->first_block, data, len);
    if (len == 0) { if (de->first_block >= 0) { free_chain(fs, de->first_block); de->first_block = -1; } }
    de->size_bytes = (uint32_t)len;
    msync(fs->base, fs->bytes, MS_SYNC);
    return 0;
}

static int cmd_append(fs_t *fs, const char *name, const uint8_t *data, size_t len) {
    int idx = dir_find(fs, name); if (idx < 0) return 1;
    dirent_t *de = &fs->dir[idx]; size_t new_len = (size_t)de->size_bytes + len;
    if (ensure_capacity(fs, de, new_len) < 0) return 2;
    // write at offset = old size
    size_t off = de->size_bytes; size_t left = len; const uint8_t *p = data;
    if (de->first_block >= 0) {
        // walk to block containing old tail
        size_t skip = off; int32_t b = de->first_block; while (skip >= BLOCK_SIZE) { b = fs->fat[b]; skip -= BLOCK_SIZE; }
        // write partial first block
        size_t space = BLOCK_SIZE - skip; size_t to_copy = (left < space) ? left : space;
        if (to_copy > 0) { memcpy(block_ptr(fs,(uint32_t)b)+skip, p, to_copy); p += to_copy; left -= to_copy; }
        while (left > 0) {
            b = fs->fat[b]; size_t n = (left < BLOCK_SIZE) ? left : BLOCK_SIZE; memcpy(block_ptr(fs,(uint32_t)b), p, n); if (n < BLOCK_SIZE) memset(block_ptr(fs,(uint32_t)b)+n, 0, BLOCK_SIZE-n); p += n; left -= n;
        }
    }
    de->size_bytes = (uint32_t)new_len; msync(fs->base, fs->bytes, MS_SYNC);
    return 0;
}

static int cmd_read(fs_t *fs, const char *name, uint8_t **out, size_t *outlen) {
    int idx = dir_find(fs, name); if (idx < 0) return 1;
    dirent_t *de = &fs->dir[idx]; size_t len = de->size_bytes;
    *out = NULL; *outlen = 0;
    if (len == 0) { *out = NULL; *outlen = 0; return 0; }
    uint8_t *buf = malloc(len); if (!buf) return 2;
    if (de->first_block >= 0) {
        ssize_t r = io_read_chain(fs, de->first_block, buf, len);
        if (r < 0 || (size_t)r != len) { free(buf); return 2; }
    }
    *out = buf; *outlen = len; return 0;
}

// ---- Connection handling ---------------------------------------------------

static void respond_code(int cfd, int code) {
    char line[32]; int n = snprintf(line, sizeof(line), "%d\n", code);
    (void)write_full(cfd, line, (size_t)n);
}

static void *client_thread(void *arg) {
    int cfd = *(int*)arg; free(arg);
    char tok[64];
    for (;;) {
        int rt = read_token(cfd, tok, sizeof(tok)); if (rt == 0) break; if (rt < 0) { perror("read_token"); break; }
        if (!strcmp(tok, "F")) {
            pthread_mutex_lock(&g_fs.lock);
            int rc = cmd_format(&g_fs);
            pthread_mutex_unlock(&g_fs.lock);
            respond_code(cfd, rc == 0 ? 0 : 2);
        } else if (!strcmp(tok, "C")) {
            char name[NAME_MAXLEN]; if (read_token(cfd, name, sizeof(name)) <= 0) break;
            pthread_mutex_lock(&g_fs.lock);
            int rc = cmd_create(&g_fs, name);
            pthread_mutex_unlock(&g_fs.lock);
            respond_code(cfd, rc);
        } else if (!strcmp(tok, "D")) {
            char name[NAME_MAXLEN]; if (read_token(cfd, name, sizeof(name)) <= 0) break;
            pthread_mutex_lock(&g_fs.lock);
            int rc = cmd_delete(&g_fs, name);
            pthread_mutex_unlock(&g_fs.lock);
            respond_code(cfd, rc);
        } else if (!strcmp(tok, "L")) {
            char flag[8]; if (read_token(cfd, flag, sizeof(flag)) <= 0) break;
            int verbose = (flag[0] == '1');
            pthread_mutex_lock(&g_fs.lock);
            for (uint32_t i=0;i<g_fs.sb->max_files;i++) {
                dirent_t *de = &g_fs.dir[i];
                if (!de->used) continue;
                if (verbose) {
                    char line[256]; int n = snprintf(line, sizeof(line), "%s %u\n", de->name, de->size_bytes);
                    write_full(cfd, line, (size_t)n);
                } else {
                    char line[256]; int n = snprintf(line, sizeof(line), "%s\n", de->name);
                    write_full(cfd, line, (size_t)n);
                }
            }
            pthread_mutex_unlock(&g_fs.lock);
            write_full(cfd, "\n", 1); // terminator line
        } else if (!strcmp(tok, "R")) {
            char name[NAME_MAXLEN]; if (read_token(cfd, name, sizeof(name)) <= 0) break;
            pthread_mutex_lock(&g_fs.lock);
            uint8_t *buf=NULL; size_t len=0; int rc = cmd_read(&g_fs, name, &buf, &len);
            pthread_mutex_unlock(&g_fs.lock);
            char hdr[64]; int n = snprintf(hdr, sizeof(hdr), "%d %zu ", rc, len);
            if (write_full(cfd, hdr, (size_t)n) < 0) { free(buf); break; }
            if (rc == 0 && len > 0) { if (write_full(cfd, buf, len) < 0) { free(buf); break; } }
            free(buf);
        } else if (!strcmp(tok, "W") || !strcmp(tok, "A")) {
            int is_append = (tok[0] == 'A');
            char name[NAME_MAXLEN], ltok[32];
            if (read_token(cfd, name, sizeof(name)) <= 0 || read_token(cfd, ltok, sizeof(ltok)) <= 0) break;
            long l = strtol(ltok, NULL, 10); if (l < 0) { respond_code(cfd, 2); continue; }
            uint8_t *buf = NULL; if (l > 0) { buf = malloc((size_t)l); if (!buf) { respond_code(cfd, 2); continue; } if (read_full(cfd, buf, (size_t)l) != (ssize_t)l) { free(buf); break; } }
            pthread_mutex_lock(&g_fs.lock);
            int rc = is_append ? cmd_append(&g_fs, name, buf, (size_t)l)
                               : cmd_write(&g_fs,  name, buf, (size_t)l);
            pthread_mutex_unlock(&g_fs.lock);
            free(buf);
            respond_code(cfd, rc);
        } else {
            // unknown command — ignore line
        }
    }
    close(cfd); return NULL;
}

static void usage(const char *prog) {
    fprintf(stderr, "Usage: %s <port> <cylinders> <sectors_per_cyl> <backing_file>\n", prog);
}

//  Main function
int main(int argc, char **argv) {
    if (argc != 5) { usage(argv[0]); return 1; }
    const char *port = argv[1]; uint32_t cyl = (uint32_t)atoi(argv[2]); uint32_t sec = (uint32_t)atoi(argv[3]); const char *file = argv[4];
    if (cyl == 0 || sec == 0) { usage(argv[0]); return 1; }

    signal(SIGINT, on_sigint);

    // Prepares backing file mapping
    size_t total_blocks = blocks_total(cyl, sec);
    size_t map_bytes = total_blocks * BLOCK_SIZE;
    int fd = open(file, O_RDWR | O_CREAT, 0644); if (fd < 0) { perror("open"); return 1; }
    if (ftruncate(fd, (off_t)map_bytes) < 0) { perror("ftruncate"); close(fd); return 1; }
    uint8_t *base = mmap(NULL, map_bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    if (base == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    // Bind global FS
    memset(&g_fs, 0, sizeof(g_fs)); g_fs.fd = fd; g_fs.base = base; g_fs.bytes = map_bytes; pthread_mutex_init(&g_fs.lock, NULL);

    // If superblock looks valid, bind; otherwise, initialize a tentative sb and expect F
    super_t *sb = (super_t *)block_ptr(&g_fs, 0);
    if (sb->magic == 0x46534C31u && sb->cylinders == cyl && sb->sectors == sec && sb->block_size == BLOCK_SIZE) {
        g_fs.sb = sb; g_fs.fat = (int32_t *)block_ptr(&g_fs, sb->fat_start); g_fs.dir = (dirent_t *)block_ptr(&g_fs, sb->dir_start);
    } else {
        // write a minimal header so format knows geometry
        memset(sb, 0, sizeof(*sb)); sb->magic = 0x46534C31u; sb->cylinders = cyl; sb->sectors = sec; sb->block_size = BLOCK_SIZE; sb->total_blocks = (uint32_t)total_blocks;
        fs_bind_views(&g_fs);
    }

    int lfd = mk_listen_socket(port); if (lfd < 0) { fprintf(stderr, "listen failed on %s\n", port); return 1; }
    fprintf(stderr, "fs_server listening on %s (cyl=%u sec=%u)\n", port, cyl, sec);

    // Trivial final cleanup
    while (!g_stop) {
        struct sockaddr_storage ss; socklen_t slen = sizeof(ss);
        int cfd = accept(lfd, (struct sockaddr *)&ss, &slen);
        if (cfd < 0) { if (errno==EINTR) continue; perror("accept"); break; }
        int *hp = malloc(sizeof(int)); if (!hp) { close(cfd); continue; }
        *hp = cfd; pthread_t th; pthread_create(&th, NULL, client_thread, hp); pthread_detach(th);
    }

    close(lfd); msync(g_fs.base, g_fs.bytes, MS_SYNC); munmap(g_fs.base, g_fs.bytes); close(g_fs.fd);
    return 0;
}
