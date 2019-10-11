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

#include <semaphore.h>
#include <stdbool.h>
#include <unordered_map>
using namespace std;
struct sockaddr_in si_other;

typedef struct sharedData {
    int timeout;
    int dupAcksCount;
    int threshold;
    double CW;
    int mode;
    int sendBase;
    long int timer;
    int currIdx;
    bool complete;
    sem_t sem;
} shared_data;

typedef struct Segment{
    long seq_num;
    short len;
    char datagram[1280];
} segment;

int s, slen;
int nums = 0;
int package_total;
unordered_map<int, segment*> buffer;

shared_data data;

void diep(char *s) {
    perror(s);
    exit(1);
}





void* receiveAck(void*){
    
}

void reliablyTransfer(char* hostname, unsigned short int hostUDPport, char* filename, unsigned long long int bytesToTransfer) {
    //Open the file
    FILE *fp;
    fp = fopen(filename, "rb");
    if (fp == NULL) {
        printf("Could not open file to send.");
        exit(1);
    }

	/* Determine how many bytes to transfer */

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


	/* Send data and receive acknowledgements on s*/
	pthread_t t; // ?
	clock_t start = clock();
	while(1){
		if(data.complete){
		    break;
		}

		sem_wait(&data.sem);
        int upper = data.sendBase + (int)data.CW;
        sem_post(&data.sem);

        while(data.currIdx < package_total && data.currIdx < upper){
            segment *seg = buffer[data.currIdx];
            if(sendto(s, seg, sizeof(*seg), 0, (struct sockaddr *)&si_other, sizeof(si_other)) == -1){
                diep("Sending error");
            }

            data.currIdx++;

            sem_wait(&data.sem);
            nums++;
            sem_post(&data.sem);

            if(data.currIdx == 1){
                pthread_create(&t, NULL, &receiveAck, NULL);
            }
        }
	}
    printf("Closing the socket\n");
    close(s);
    return;

}



/*
 * 
 */
int main(int argc, char** argv) {

    unsigned short int udpPort;
    unsigned long long int numBytes;

    if (argc != 5) {
        fprintf(stderr, "usage: %s receiver_hostname receiver_port filename_to_xfer bytes_to_xfer\n\n", argv[0]);
        exit(1);
    }
    udpPort = (unsigned short int) atoi(argv[2]);
    numBytes = atoll(argv[4]);



    reliablyTransfer(argv[1], udpPort, argv[3], numBytes);


    return (EXIT_SUCCESS);
}


