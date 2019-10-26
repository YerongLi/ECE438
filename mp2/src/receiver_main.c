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
#define MAXBUFLEN 2000
#define PAYLOAD 1400

typedef struct {
	ull seqNum;
	ull length;
	char data[PAYLOAD+1];
	/* end flag 
		0 : normal packet
		1 : end of the buffer
		2 : end of the file
	*/
	int end; 
} segment;

void diep(char *s) {
    perror(s);
    exit(1);
}


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
    if (bind(s, (struct sockaddr*)&si_me, sizeof si_me) == -1){
        diep("bind");
	}

	/* Now receive data and send acknowledgements */
	FILE* fp;
	/* Clear the target file */
	fp = fopen(destinationFile, "w");
	fclose(fp);
	segment* buffer;
	buffer = malloc((MAXBUFLEN + 1) * sizeof(segment));
	int received[MAXBUFLEN + 1];
    segment packet;
	ull ack = 0, expectNum = 1, total = 0;
	while (recvfrom(s, &packet, sizeof packet, 0,
	(struct sockaddr *)&si_other, &slen)) {
		if (packet.seqNum == -1) {
			diep("Reciever package sequence number");
		}
		

		if (0 == received[packet.seqNum]) {
			received[packet.seqNum] = 1;
			packet.data[packet.length] = '\0';
			total += packet.length;

			buffer[packet.seqNum] = packet;

			 if (packet.end > 0 && packet.seqNum == expectNum) {
				fp = fopen(destinationFile, "a+");
				for (int i = 1; i < expectNum; i++) {
					fwrite(buffer[i].data, sizeof(char), buffer[i].length, fp);
				}
				fclose(fp);
				/**
				 * If we have reach the end of file : the last packet have ending flag 2
				 * then we send ACKs with value 0 and close the socket 
				 */
				if (packet.end == 2) {
					for (int i = 0; i < 20; i++) {
						ack = 0;
						sendto(s, &ack, sizeof(int), 0,
             			(struct sockaddr *)&si_other, slen);
					}
					break;
				}
			 }
		}
		while (1 == received[expectNum])  {
				expectNum++;
		}
		sendto(s, &expectNum, sizeof(int), 0,
             (struct sockaddr *)&si_other, slen);



	//	strcpy(buffer + packet.seqNum, packet.data);
	//	fwrite(packet.data, 1, numbytes, fp);


	}
    close(s);
	printf("File written, cumulative %lld bytes.\n", total);
	printf("%s received.\n", destinationFile);
    return;
}

/*
 * 
 */
int main(int argc, char** argv) {

    us udpPort;

    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }

    udpPort = (us) atoi(argv[1]);

    reliablyReceive(udpPort, argv[2]);
}

