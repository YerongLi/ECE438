/* 
 * File:   receiver_main.c
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
#include <limits.h>

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 1400

typedef struct {
	ull seqNum;
	ull length;
	char end;
	char data[payload+1];
} segment;

/* Parameters */
ull nextByteExpected = 0;
const int receiveBuffer = 50000;

void diep(char *s);
void writeFile(char* destinationFile, segment** buffer, ull packetNum);

void reliablyReceive(us myUDPport, char* destinationFile) {
	/* Open socket and bind */
	struct sockaddr_in si_me, si_other;
	int s, fin = 0;
	ull total = 0;
	FILE* fp = fopen(destinationFile, "w");
	socklen_t slen;
    slen = sizeof (si_other);
    if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
        diep("socket");
    memset((char *) &si_me, 0, sizeof (si_me));
    si_me.sin_family = AF_INET;
    si_me.sin_port = htons(myUDPport);
    si_me.sin_addr.s_addr = htonl(INADDR_ANY);
    printf("Now binding\n");
    if (bind(s, (struct sockaddr*)&si_me, sizeof (si_me)) == -1)
        diep("bind");

	/* Now receive data and send acknowledgements */
    segment* buffer[receiveBuffer];
	segment* yerongbuffer[200000];
	ull cc = 0;
    for (int i = 0; i < receiveBuffer; i++) {
    	buffer[i] = NULL;
	}
	// packetNUM should be something different
    ull packetNum = INT_MAX;
	while (1) {
    	segment* packet = malloc(sizeof(segment));
		recvfrom(s, packet, sizeof(segment), 0,
			(struct sockaddr *)&si_other, &slen);
		ull seqNum = packet->seqNum;
		printf("seqNum = %lld, nextByteExpected = %lld\n", seqNum, nextByteExpected);
		if (!buffer[seqNum]) { // duplicate ACK
			segment* cur = malloc(sizeof(segment));
			cur->seqNum = seqNum;
			ull numbytes = packet->length;
			cur->length = numbytes;
			cur->end = packet->end;
			strncpy(cur->data, packet->data, payload+1);
			cur->data[numbytes] = '\0';
			buffer[seqNum] = cur;
			yerongbuffer[cc++] = cur;
		}
		/* Record next byte to be expected */
		while (buffer[nextByteExpected]) {
			nextByteExpected++;
		}
		sendto(s, &nextByteExpected, sizeof(ull), 0,
			(struct sockaddr *)&si_other, slen);
		if (packet->end != '0') {
			packetNum = seqNum + 1;
			if (packet->end == '2') {
				fin = 1;
			}
		}
		if (nextByteExpected == packetNum) {
			
			for (ull i = 0; i < packetNum; i++) {
				total += buffer[i] -> length;
				fwrite(buffer[i]->data, 1, buffer[i]->length, fp);
			}
			/* Release memory */
			for (int i = 0; i < receiveBuffer; i++) {
				if (buffer[i]) {
					//free(buffer[i]); 
					buffer[i] = NULL;
				} else {
					break;
				}
			}
			nextByteExpected = 0;
			if (1 == fin) {
				break;
			}
		}
	}

	/* Close connection */
	for (int i = 0; i < 20; i++) {
		sendto(s, &nextByteExpected, sizeof(ull), 0,
			(struct sockaddr *)&si_other, slen);
	}
	fclose(fp);
	printf("File written, total %lld bytes\n", total);
	writeFile("yerong.txt", yerongbuffer, cc);
    close(s);
	printf("%s received\n", destinationFile);
    return;
}

void writeFile(char* destinationFile, segment** buffer, ull packetNum) {
	FILE* fp = fopen(destinationFile, "a+");
	ull total = 0;
	for (ull i = 0; i < packetNum; i++) {
		segment* packet = buffer[i];
		ull length = packet->length;
		total += length;
		fwrite(packet->data, 1, length, fp);
	}
	fclose(fp);
	printf("File written, total %lld bytes\n", total);
}

void diep(char *s) {
    perror(s);
    exit(1);
}

int main(int argc, char** argv) {

    us udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (us) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

