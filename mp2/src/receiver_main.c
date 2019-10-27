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
#include <unordered_map>
using namespace std;

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 6000

typedef struct {
	ull seqNum;
	ull length;
	char end;
	char data[payload];
} segment;

/* Parameters */
ull nextByteExpected = 0;
FILE* fp;
ull total = 0;
unordered_map<ull, segment*> buffer;

void diep(const char *s);
void writeFile(ull packetNum);

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

    /* Receive file */
	fp = fopen(destinationFile, "wb");
    ull packetNum = INT_MAX;
	while (1) {
    	segment packet;
		recvfrom(s, &packet, sizeof(segment), 0,
			(struct sockaddr *)&si_other, &slen);
		ull seqNum = packet.seqNum;
		printf("seqNum = %lld, nextByteExpected = %lld\n", seqNum, nextByteExpected);
		if (!buffer.count(seqNum)) { // new ACK
			segment* cur = (segment*)malloc(sizeof(segment));
			cur->seqNum = seqNum;
			ull numbytes = packet.length;
			cur->length = numbytes;
			cur->end = packet.end;
			memcpy(cur->data, packet.data, payload);
			buffer[seqNum] = cur;
		}
		while (buffer.count(nextByteExpected)) {
			writeFile(nextByteExpected);
			nextByteExpected++;
		}
		sendto(s, &nextByteExpected, sizeof(ull), 0,
			(struct sockaddr *)&si_other, slen);
		if (packet.end == '1')
			packetNum = seqNum+1;
		if (nextByteExpected == packetNum)
			break;
	}

	/* Close connection */
	for (int i = 0; i < 20; i++) {
		sendto(s, &nextByteExpected, sizeof(ull), 0,
			(struct sockaddr *)&si_other, slen);
	}

	/* End */
	fclose(fp);
	printf("File written, total %lld packets, %lld bytes\n", packetNum, total);
    close(s);
	printf("%s received\n", destinationFile);
    return;
}

void writeFile(ull nextByteExpected) {
	segment* packet = buffer[nextByteExpected];
	ull length = packet->length;
	total += length;
	fwrite(packet->data, 1, length, fp);
	free(packet);
}

void diep(const char *s) {
    perror(s);
    exit(1);
}

int main(int argc, char** argv) {
	printf("segment size: %zu\n", sizeof(segment));
    us udpPort;
    if (argc != 3) {
        fprintf(stderr, "usage: %s UDP_port filename_to_write\n\n", argv[0]);
        exit(1);
    }
    udpPort = (us) atoi(argv[1]);
    reliablyReceive(udpPort, argv[2]);
}