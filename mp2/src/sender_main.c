/* 
 * File:   sender_main.c
 * Author: 
 *
 * Created on 
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <signal.h>
#include <string.h>
#include <sys/time.h>
#include "utils.h"
typedef unsigned long long int ull;
typedef unsigned short int us;
const int payload = 1472;

struct sockaddr_in si_other;
int s;
socklen_t slen;

void diep(char *s) {
    perror(s);
    exit(1);
}


void reliablyTransfer(char* hostname, us hostUDPport, char* filename, ull bytesToTransfer) {
    slen = sizeof (si_other);
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");

    memset((char *) &si_other, 0, sizeof (si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0) {
        fprintf(stderr, "inet_aton() failed\n");
        exit(1);
    }

    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (!fp)
        diep("Could not open file to send.");
    ull fileSize, actualBytes;
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    rewind(fp);
    printf("fileSize = %lld\n", fileSize);

	/* Determine how many bytes to transfer */
    if (bytesToTransfer > fileSize) {
        actualBytes = fileSize;
        printf("bytesToTransfer too large, use actual bytes %lld.\n", actualBytes);
    } else
        actualBytes = bytesToTransfer;
    char buffer[actualBytes+1];
    fread(buffer, 1, actualBytes, fp);
    buffer[actualBytes] = '\0';
    fclose(fp);

	/* Send data and receive acknowledgements on s */
    ull sent = 0;
    while (sent < actualBytes) {
        ull left = actualBytes - sent;
        ull curSend = left < payload ? left : payload;
        sent += sendto(s, buffer+sent, curSend, 0,
             (struct sockaddr *)&si_other, slen);
    }
    printf("%lld bytes sent.\n", sent);
    printf("Closing the socket\n");
    close(s);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    us udpPort;
    ull numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (us) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


