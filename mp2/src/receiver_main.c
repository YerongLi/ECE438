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

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 1400
const int receiveBuffer = 2000;

typedef struct {
	ull seqNum;
	ull length;
	char end;
	char data[payload+1];
} segment;

/* Parameters */
ull nextByteExpected = 1;


void diep(char *s);
void writeFile(char* destinationFile, segment** buffer, ull packetNum);

void reliablyReceive(us myUDPport, char* destinationFile) {
	/* Open socket and bind */
	struct sockaddr_in si_me, si_other;
	int s;
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
    segment* buffer[receiveBuffer] = {NULL};
    ull packetNum = 0;
	while (1) {
    	segment packet;
		recvfrom(s, &packet, sizeof(packet), 0,
			(struct sockaddr *)&si_other, &slen);
		ull seqNum = packet.seqNum;
		if (seqNum == nextByteExpected)
			nextByteExpected++;
		if (!buffer[seqNum]) {
			segment* cur = malloc(sizeof(segment));
			cur->seqNum = packet.seqNum;
			cur->length = packet.length;
			cur->end = packet.end;
			cur->data = packet.data;
			buffer[seqNum] = cur;
		}
		if (packet.end == '1')
			packetNum = seqNum;
		if (packetNum && nextByteExpected > packetNum)
			break;
	}
	// ull numbytes, total = 0;
	// while (recvfrom(s, &packet, sizeof(packet), 0,
	// (struct sockaddr *)&si_other, &slen)) {
	// 	FILE* fp = fopen(destinationFile, "a+");
	// 	numbytes = packet.length;
	// 	packet.data[numbytes] = '\0';
	// 	total += numbytes;
	// 	fwrite(packet.data, 1, numbytes, fp);
	// 	// printf("File written, cumulative %lld bytes.\n", total);
	// 	fclose(fp);
	// 	if (packet.end == '1')
	// 		break;
	// }
	writeFile(destinationFile, buffer, packetNum);
    close(s);
	printf("%s received.\n", destinationFile);
    return;
}

void diep(char *s) {
    perror(s);
    exit(1);
}

void writeFile(char* destinationFile, segment** buffer, int packetNum) {
	FILE* fp = fopen(destinationFile, "w");
	ull total = 0;
	for (int i = 1; i <= packetNum; i++) {
		segment* packet = buffer[i];
		ull length = packet->length;
		total += length;
		fwrite(packet->data, 1, length, fp);
	}
	fclose(fp);
	printf("File written, total %lld bytes.\n", total);
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

