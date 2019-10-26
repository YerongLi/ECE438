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
#include <semaphore.h>

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 1400

typedef struct {
	ull seqNum;
	ull length;
	char end;
	char data[payload+1];
} segment;

/* Socket parameters*/
struct sockaddr_in si_other;
int s;
socklen_t slen;

/* Parameters shared by 2 threads */
segment** packetBuffer;
int packetNum;
enum Congestion_Control{SS, CA, FR};
int mode = SS;
int dupACKcount = 0;
int timeOutInterval = 50; // 50ms
ull ssthresh = 1000;
ull MSS = 10;
ull cwnd = 10;
ull sendBase = 0;
ull nextSeqNum = 0;
sem_t mutex;

void diep(char* s);
ull readSize(char* filename, ull bytesToTransfer);
void storeFile(char* filename, ull actualBytes);
void* threadRecvRetransmit();

void reliablyTransfer(char* hostname, us hostUDPport, char* filename, ull bytesToTransfer) {
	/* Open socket */
    slen = sizeof(si_other);
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");
    memset((char *) &si_other, 0, sizeof(si_other));
    si_other.sin_family = AF_INET;
    si_other.sin_port = htons(hostUDPport);
    if (inet_aton(hostname, &si_other.sin_addr) == 0)
    	diep("inet_aton() failed\n");

    /* Open file and store data into packet_buffer */
    ull actualBytes = readSize(filename, bytesToTransfer);
    packetNum = ceil(actualBytes/(float)payload);
    packetBuffer = malloc(packetNum * sizeof(segment*));
    storeFile(filename, actualBytes);

	/* Send data and receive acknowledgements on s */
	// for (int i = 0; i < packetNum; i++) {
	// 	segment* packet = packetBuffer[i];
	// 	sendto(s, packet, sizeof(segment), 0,
	// 		(struct sockaddr *)&si_other, slen);
	// }
	pthread_t recvThread;
	sem_init(&mutex, 0, 1);
	while (1) {
		if (nextSeqNum == packetNum)
			break;
		sem_wait(&mutex);
		ull wnEnd = sendBase + cwnd;
		sem_post(&mutex);
		while (nextSeqNum < packetNum && nextSeqNum < wnEnd) {
			segment* packet = packetBuffer[nextSeqNum];
			sendto(s, packet, sizeof(segment), 0,
				(struct sockaddr *)&si_other, slen);
			nextSeqNum++;
			if (nextSeqNum == 1)
				pthread_create(&recvThread, NULL, threadRecvRetransmit, NULL);
		}
	}
	pthread_join(recvThread, NULL);
    printf("%lld bytes sent\n", actualBytes);
    
    /* Release memory */
	for (int i = 0; i < packetNum; i++)
		free(packetBuffer[i]);
	free(packetBuffer);
	sem_destroy(&mutex);
    printf("Closing the socket\n");
    close(s);
    return;
}

void* threadRecvRetransmit() {
	struct timeval timeout;      
    timeout.tv_sec = 0;
    timeout.tv_usec = timeOutInterval * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    while (1) {
    	ull ack;
    	int numbytes = recvfrom(s, &ack, sizeof(ull), 0,
    		(struct sockaddr *)&si_other, &slen);
    	if (numbytes == -1) {
    		/* Timeout */
    		segment* packet = packetBuffer[sendBase];
			sendto(s, packet, sizeof(segment), 0,
				(struct sockaddr *)&si_other, slen);
			mode = SS;
			ssthresh = cwnd / 2;
			sem_wait(&mutex);
			cwnd = MSS;
			sem_post(&mutex);
			dupACKcount = 0;
			continue;
    	}
    	printf("Received ack = %lld\n", ack);
    	if (ack == packetNum)
    		break;
    	if (mode == SS) { // Slow start
    		if (ack == sendBase) {
    			dupACKcount++;
    			if (dupACKcount == 3) {
    				mode = FR;
					ssthresh = cwnd / 2;
    				sem_wait(&mutex);
					cwnd = MSS;
					sem_post(&mutex);
    			}
    		} else if (ack > sendBase) {
    			sem_wait(&mutex);
    			sendBase = ack;
				cwnd += MSS;
				sem_post(&mutex);
				dupACKcount = 0;
				if (cwnd >= ssthresh)
					mode = CA;
    		}
    	} else if (mode == CA) { // Congestion avoidance
    		if (ack == sendBase) {
    			dupACKcount++;
    			if (dupACKcount == 3) {
    				mode = FR;
					ssthresh = cwnd / 2;
    				sem_wait(&mutex);
					cwnd = MSS;
					sem_post(&mutex);
    			}
    		} else if (ack > sendBase) {
    			sem_wait(&mutex);
    			sendBase = ack;
				cwnd += MSS * MSS / cwnd;
				sem_post(&mutex);
				dupACKcount = 0;
    		}
    	} else { // Fast recovery
    		if (ack == sendBase) {
    			sem_wait(&mutex);
				cwnd += MSS * MSS / cwnd;
				sem_post(&mutex);
    		} else if (ack > sendBase) {
    			sem_wait(&mutex);
    			sendBase = ack;
				cwnd = ssthresh;
				sem_post(&mutex);
				dupACKcount = 0;
    		}
    	}
    }
    return NULL;
}

void storeFile(char* filename, ull actualBytes) {
	FILE *fp;
    fp = fopen(filename, "rb");
    ull read = 0;
    for (ull i = 0; i < packetNum; i++) {
    	segment* packet = malloc(sizeof(segment));
    	packet->seqNum = i;
    	if (i != packetNum-1) {
    		packet->length = payload;
    		read += payload;
    		packet->end = '0';
    	} else {
    		packet->length = actualBytes - read;
    		packet->end = '1';
    	}
    	fread(packet->data, 1, packet->length, fp);
    	packet->data[packet->length] = '\0';
    	packetBuffer[i] = packet;
    }
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
        printf("bytesToTransfer too large, use actual bytes %lld\n", actualBytes);
    } else
        actualBytes = bytesToTransfer;
    fclose(fp);
    return actualBytes;
}

void diep(char* s) {
    perror(s);
    exit(1);
}

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