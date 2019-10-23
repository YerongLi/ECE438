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


#define MAXBUFFERLEN 2000
typedef unsigned long long int ull;
typedef unsigned short int us;

struct sockaddr_in si_me, si_other;
int s;
socklen_t slen;

void diep(char *s) {
    perror(s);
    exit(1);
}

void reliablyReceive(us myUDPport, char* destinationFile) {
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
	FILE* fp = fopen(destinationFile, "w");
	fclose(fp);
    char buffer[MAXBUFFERLEN];
	ull numbytes, total = 0;
	while ((numbytes = recvfrom(s, buffer, MAXBUFFERLEN-1 , 0,
		(struct sockaddr *)&si_other, &slen))) {
		FILE* fp = fopen(destinationFile, "a+");
		buffer[numbytes] = '\0';
		total += numbytes;
		fwrite(buffer, 1, numbytes, fp);
		printf("File written, cumulative %lld bytes.\n", total);
		fclose(fp);
	}
    close(s);
	printf("%s received.", destinationFile);
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

