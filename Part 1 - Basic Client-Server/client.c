// Andy Lim and Ifunaya Okafor
// Client for Project 3 Question 1

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#define CLNPORT 8080
#define BUFSZ   1024

int main(int argc, char const *argv[])
{
    if (argc < 3) {
        printf("Usage: %s <srvaddr> <string>\n", argv[0]);
        return 1;
    }

    printf("IP: %s\n", argv[1]);
    printf("String: %s\n", argv[2]);

    int clnSck = socket(AF_INET, SOCK_STREAM, 0);
    if (clnSck < 0) {
        printf("Socket error\n");
        return 1;
    }

    struct sockaddr_in saddr;
    memset(&saddr, 0, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = htons(CLNPORT);

    if (inet_pton(AF_INET, argv[1], &saddr.sin_addr) <= 0) {
        printf("Invalid address\n");
        close(clnSck);
        return 1;
    }

    if (connect(clnSck, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        printf("Connection failed\n");
        close(clnSck);
        return 1;
    }

    printf("Connected\n");

    // send (no loop)
    send(clnSck, argv[2], strlen(argv[2]), 0);
    printf("Sent: %s\n", argv[2]);

    // receive
    char rbuff[BUFSZ];
    int n = recv(clnSck, rbuff, BUFSZ - 1, 0);
    if (n > 0) {
        rbuff[n] = '\0';
        printf("Received: %s\n", rbuff);
    } else {
        printf("No response\n");
    }

    close(clnSck);
    return 0;
}
