// Names: Ifunanya Okafor and Andy Lim || Course: CS 4440-03
// Description: Interactive client for the flat filesystem server.
//              Supports: F | C f | D f | L b | R f | W f l | A f l
//              For W/A, prompts for exactly l bytes of raw data.
// Compile Build: gcc -O2 -std=c17 -Wall -Wextra -pedantic fs_client.c -o fs_client
// Run:           ./fs_client <host> <port>
// Example: ./fs_client 127.0.0.1 10090

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

static ssize_t read_full(int fd, void *buf, size_t n) {
    unsigned char *p = buf; size_t left = n;
    while (left>0) { ssize_t r = read(fd,p,left); if (r==0) return (ssize_t)(n-left); if (r<0){ if(errno==EINTR) continue; return -1;} p+=r; left-=(size_t)r; }
    return (ssize_t)n;
}
static ssize_t write_full(int fd, const void *buf, size_t n) {
    const unsigned char *p = buf; size_t left = n;
    while (left>0) { ssize_t w = write(fd,p,left); if (w<0){ if(errno==EINTR) continue; return -1;} p+=w; left-=(size_t)w; }
    return (ssize_t)n;
}

static int connect_to(const char *host, const char *port) {
    struct addrinfo hints={0}, *res=NULL, *it; int fd=-1;
    hints.ai_family=AF_UNSPEC; hints.ai_socktype=SOCK_STREAM;
    if (getaddrinfo(host, port, &hints, &res) != 0) return -1;
    for (it=res; it; it=it->ai_next) {
        fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol);
        if (fd<0) continue; if (connect(fd,it->ai_addr,it->ai_addrlen)==0) break; close(fd); fd=-1;
    }
    freeaddrinfo(res); return fd;
}

static void hexdump(const unsigned char *p, size_t n) {
    for (size_t i=0;i<n;i+=16) {
        printf("%04zx : ", i);
        for (size_t j=0;j<16;j++) { if (i+j<n) printf("%02x ", p[i+j]); else printf("   "); }
        printf(" | ");
        for (size_t j=0;j<16;j++) { if (i+j<n) { unsigned char c=p[i+j]; putchar((c>=32&&c<127)?c:'.'); } }
        putchar('\n');
    }
}

int main(int argc, char **argv) {
    if (argc != 3) { fprintf(stderr, "Usage: %s <host> <port>\n", argv[0]); return 1; }
    int fd = connect_to(argv[1], argv[2]); if (fd<0) { perror("connect"); return 1; }
    printf("Connected. Commands: F | C f | D f | L b | R f | W f l | A f l | quit\n");

    char *line=NULL; size_t cap=0;
    while (printf("> "), fflush(stdout), getline(&line,&cap,stdin) != -1) {
        size_t len=strlen(line); if (len>0 && line[len-1]=='\n') line[len-1]='\0';
        if (line[0]=='\0') continue; if (!strcmp(line,"quit")||!strcmp(line,"exit")) break;

        if (line[0]=='F' && (line[1]=='\0' || line[1]==' ')) {
            const char *msg = "F "; write_full(fd, msg, strlen(msg));
            char resp[64]={0}; read(fd, resp, sizeof(resp)-1); printf("%s", resp);
        } else if (line[0]=='L') {
            char flag='0'; if (len>=3) flag=line[2]; char msg[16]; int n=snprintf(msg,sizeof(msg),"L %c ", flag);
            write_full(fd, msg, (size_t)n);
            char buf[1024]; ssize_t r;
            // Reads until system gets a blank line terminator or socket closes
            while ((r = read(fd, buf, sizeof(buf))) > 0) {
                fwrite(buf, 1, (size_t)r, stdout);
                if (memmem(buf, (size_t)r, "\n\n", 2)) break;
                if (r < (ssize_t)sizeof(buf)) break;
            }
        } else if (line[0]=='C' || line[0]=='D' || line[0]=='R' || line[0]=='W' || line[0]=='A') {
            char cmd; char name[64]; long L=0;
            if (line[0]=='C') { if (sscanf(line, "C %63s", name)!=1) { puts("Usage: C <name>"); continue; } char out[80]; int n=snprintf(out,sizeof(out),"C %s ", name); write_full(fd,out,(size_t)n); }
            else if (line[0]=='D') { if (sscanf(line, "D %63s", name)!=1) { puts("Usage: D <name>"); continue; } char out[80]; int n=snprintf(out,sizeof(out),"D %s ", name); write_full(fd,out,(size_t)n); }
            else if (line[0]=='R') { if (sscanf(line, "R %63s", name)!=1) { puts("Usage: R <name>"); continue; } char out[80]; int n=snprintf(out,sizeof(out),"R %s ", name); write_full(fd,out,(size_t)n);
                // Expect: code len data
                char hdr[64]={0}; ssize_t r = read(fd, hdr, sizeof(hdr)-1); if (r<=0) { puts("read error"); continue; }
                int code=0; size_t flen=0; // parses like: "0 <len> "
                if (sscanf(hdr, "%d %zu", &code, &flen) < 2) { puts("bad header"); continue; }
                // find position after second token and a space
                char *sp1 = strchr(hdr, ' '); char *sp2 = sp1? strchr(sp1+1,' ') : NULL;
                size_t consumed = sp2 ? (size_t)(sp2 - hdr + 1) : (size_t)r;
                if (code != 0) { printf("ERR %d, len=%zu\n", code, flen); continue; }
                // if header already brought some data, print it and then read remaining
                size_t already = (size_t)r - consumed; size_t remain = (flen > already) ? (flen - already) : 0;
                if (already > 0) hexdump((unsigned char*)hdr + consumed, already);
                if (remain > 0) {
                    unsigned char *buf = malloc(remain); if (!buf) { puts("oom"); continue; }
                    if (read_full(fd, buf, remain) != (ssize_t)remain) { puts("short read"); free(buf); continue; }
                    hexdump(buf, remain); free(buf);
                }
            } else { // W or A
                char op = line[0]; if (sscanf(line, "%c %63s %ld", &cmd, name, &L)!=3) { puts("Usage: W <name> <len> | A <name> <len>"); continue; }
                if (L < 0) { puts("len must be >=0"); continue; }
                char out[96]; int n=snprintf(out,sizeof(out),"%c %s %ld ", op, name, L); write_full(fd,out,(size_t)n);
                if (L > 0) {
                    printf("DATA: enter exactly %ld bytes (shorter -> zero padding not applied here)\n", L);
                    char *dline=NULL; size_t dcap=0; ssize_t r=getline(&dline,&dcap,stdin); if (r<0){ perror("getline"); free(dline); continue; }
                    // Send exactly L bytes (truncate or pad with zeros if user typed fewer)
                    unsigned char *buf = calloc(1,(size_t)L); size_t tocpy = (size_t)r; if (tocpy > (size_t)L) tocpy = (size_t)L; memcpy(buf, dline, tocpy);
                    write_full(fd, buf, (size_t)L); free(buf); free(dline);
                }
                char resp[64]={0}; read(fd, resp, sizeof(resp)-1); printf("%s", resp);
            }
        } else {
            puts("Unknown. Try: F | C f | D f | L b | R f | W f l | A f l");
        }
    }
    free(line); close(fd); return 0;
}
