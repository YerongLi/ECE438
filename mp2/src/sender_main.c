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
#include <math.h>

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 1400
#define TIMEOUT 1000
const int TimeOutInterval = 25;
const int MaxBuffer = 1000000;

/* sockect config */
struct sockaddr_in si_other;
int s;
pthread_t thread_id;
socklen_t slen;

typedef struct {
	ull seqNum;
	ull length;
	char data[payload+1];
} segment;

typedef struct {
	ull seqNum;
} ackmnt;

void diep(char* s) {
    perror(s);
    exit(1);
}

int timeOut(clock_t start_time) {
	clock_t dif = clock() - start_time;
	int msec = dif * 1000 / CLOCKS_PER_SEC;
	if (msec < TimeOutInterval)
		return 0;
	return 1;
}

ull readSize(char* filename, ull bytesToTransfer) {
	FILE *fp;
    fp = fopen(filename, "rb");
    if (!fp)
        diep("Could not open file to send.");
    ull fileSize, actualBytes;
    fseek(fp, 0, SEEK_END);
    fileSize = ftell(fp);
    rewind(fp);
    printf("fileSize = %lld\n", fileSize);
    if (bytesToTransfer > fileSize) {
        actualBytes = fileSize;
        printf("bytesToTransfer too large, use actual bytes %lld.\n", actualBytes);
    } else
        actualBytes = bytesToTransfer;
    fclose(fp);
    return actualBytes;
}

void storeFile(char* filename, segment** packetBuffer, int packetNum, ull actualBytes) {
	FILE *fp;
    fp = fopen(filename, "rb");
    ull read = 0;
    for (int i = 0; i < packetNum; i++) {
    	segment* packet = malloc(sizeof(*packet));
    	packet->seqNum = i;
    	if (i != packetNum-1) {
    		packet->length = payload;
    		read += payload;
    	} else
    		packet->length = actualBytes - read;
    	fread(packet->data, 1, packet->length, fp);
    	packet->data[packet->length] = '\0';
    	packetBuffer[i] = packet;
    }
}

void *receiveAck(void *vargp) {
    ackmnt ack;
    printf("receive Ack started\n");
	while (1) {
        if (recvfrom(s, &ack, sizeof ack, 0,
            (struct sockaddr *)&si_other, &slen)) {
                printf("%lld\n", ack.seqNum);
                break;
            }

    }
    return NULL;
}

void reliablyTransfer(char* hostname, us hostUDPport, char* filename, ull bytesToTransfer) {
	/* Open socket */

    slen = sizeof(si_other);
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1) {
        diep("socket");
        }
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0)
    	diep("inet_aton() failed\n");

    /* Open file and store data into packet_buffer */
    ull actualBytes = readSize(filename, bytesToTransfer);
    int packetNum = ceil(actualBytes/(float)payload);
    segment* packetBuffer[packetNum];
    storeFile(filename, packetBuffer, packetNum, actualBytes);

	/* Send data and receive acknowledgements on s */
	ull sent = 0;
	for (int i = 0; i < packetNum; i++) {
		segment* packet = packetBuffer[i];
		sent += packet->length;
		sendto(s, packet, sizeof(*packet), 0,
             (struct sockaddr *)&si_other, slen);
        pthread_create(&thread_id, NULL, receiveAck, NULL);
        pthread_join(thread_id, NULL);
	}
	segment end;
	end.seqNum = -1;
	sendto(s, &end, sizeof(end), 0,
             (struct sockaddr *)&si_other, slen);
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


