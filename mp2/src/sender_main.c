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
#include <time.h>

typedef unsigned long long int ull;
typedef unsigned short int us;
#define payload 1400
#define beta 0.25
#define alpha 0.125

typedef struct {
    ull seqNum;
    ull length;
    char end;
    char data[payload];
    struct timespec sendTime;
} segment;

typedef struct {
	ull ackNum;
    struct timespec sendTime;
} ACK;

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
double ssthresh = 100;
double cwnd = 1;
ull sendBase = 0;
ull nextSeqNum = 0;
ull packetResent = 0;
ull timeOutNum = 0;
/* sendBase and cwnd are shared by 2 threads, but in thread 1 they are only read.
And they are only changed in thread 2, so in thread 2 only write need mutex.
timeOutInterval, timerNum, timerReady is shared by thread 1, 2 and signal handler */
sem_t mutex;
/* Timeout parameters, ms */
double timeOutInterval = 30;
double estimatedRTT = 30;
double devRTT = 0;
ull timerNum;
bool timerReady = true;

void diep(const char* s);
ull readSize(char* filename, ull bytesToTransfer);
void storeFile(char* filename, ull actualBytes);
void calculateRTT(struct timespec sendTime);
void timeOutHandler(int);
void* threadRecvRetransmit(void*);

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
    packetBuffer = (segment**)malloc(packetNum * sizeof(segment*));
    storeFile(filename, actualBytes);

    /* Send data and receive acknowledgements on s */
    struct timespec start;
    clock_gettime(CLOCK_REALTIME, &start);
    pthread_t recvThread;
    sem_init(&mutex, 0, 1);
	/* Retransmit through signal */
    signal(SIGALRM, timeOutHandler);
    while (1) {
        if (nextSeqNum == packetNum)
            break;
        sem_wait(&mutex);
        ull wnEnd = sendBase + cwnd;
        sem_post(&mutex);
        while (nextSeqNum < packetNum && nextSeqNum < wnEnd) {
        	segment* packet = packetBuffer[nextSeqNum];
            clock_gettime(CLOCK_REALTIME, &packet->sendTime);
            sendto(s, packet, sizeof(segment), 0,
                (struct sockaddr *)&si_other, slen);
            printf("Message %lld sent from main thread\n", nextSeqNum);
            sem_wait(&mutex);
        	if (timerReady) {
        		timerNum = packet->seqNum;
    			printf("Timer restart! timerNum=%lld\n", timerNum);
        		timerReady = false;
    			ualarm(timeOutInterval*1000, 0);
        	}
	        sem_post(&mutex);
            nextSeqNum++;
            if (nextSeqNum == 1)
                pthread_create(&recvThread, NULL, threadRecvRetransmit, NULL);
        }
    }
    pthread_join(recvThread, NULL);
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    double timeUsed = end.tv_sec-start.tv_sec+(end.tv_nsec-start.tv_nsec)/1000000000.0;
    printf("%lld bytes sent, total %d packets, total time %.3f\n", actualBytes, packetNum, timeUsed);
    printf("total %lld packets resent, %lld timeout\n", packetResent, timeOutNum);

    /* Release memory */
    for (int i = 0; i < packetNum; i++)
        free(packetBuffer[i]);
    free(packetBuffer);
    sem_destroy(&mutex);
    printf("Closing the socket\n");
    close(s);
    return;
}

void* threadRecvRetransmit(void*) {
    while (1) {
        ACK ack;
        printf("blocked\n");
        recvfrom(s, &ack, sizeof(ACK), 0,
            (struct sockaddr *)&si_other, &slen);
        ull ackNum = ack.ackNum;
        if (ackNum == packetNum) { // Last ACK received, finish
        	ualarm(0, 0);
            break;
        }
        calculateRTT(ack.sendTime);
        printf("ack=%lld, base=%lld, seq=%lld, mode=%d, cwnd=%.3f, thresh=%.3f, dup=%d, interval=%.3f\n"
        	, ackNum, sendBase, nextSeqNum, mode, cwnd, ssthresh, dupACKcount, timeOutInterval);
        sem_wait(&mutex);
        if (!timerReady && ackNum > timerNum) {
        	ualarm(0, 0);
        	timerReady = true;
        	printf("Timer stop, received timerNum=%lld\n", timerNum);
        }
        sem_post(&mutex);
        if (mode == SS) { // Slow start
            if (ackNum == sendBase) {
                dupACKcount++;
                if (dupACKcount == 3) {
                    mode = FR;
                    ssthresh = cwnd * 0.5;
                    sem_wait(&mutex);
                    cwnd = ssthresh + 3;
                    sem_post(&mutex);
                    segment* packet = packetBuffer[sendBase];
            		clock_gettime(CLOCK_REALTIME, &packet->sendTime);
                    sendto(s, packet, sizeof(segment), 0,
                        (struct sockaddr *)&si_other, slen);
                    printf("3 dup! Resend packet with seqNum=%lld\n", packet->seqNum);
                    packetResent++;
                }
            } else if (ackNum > sendBase) {
                sem_wait(&mutex);
                sendBase = ackNum;
                cwnd += 1;
                sem_post(&mutex);
                dupACKcount = 0;
                if (cwnd >= ssthresh)
                    mode = CA;
            }
        } else if (mode == CA) { // Congestion avoidance
            if (ackNum == sendBase) {
                dupACKcount++;
                if (dupACKcount == 3) {
                    mode = FR;
                    ssthresh = cwnd * 0.5;
                    sem_wait(&mutex);
                    cwnd = ssthresh + 3;
                    sem_post(&mutex);
                    segment* packet = packetBuffer[sendBase];
            		clock_gettime(CLOCK_REALTIME, &packet->sendTime);
                    sendto(s, packet, sizeof(segment), 0,
                        (struct sockaddr *)&si_other, slen);
                    printf("3 dup! Resend packet with seqNum=%lld\n", packet->seqNum);
                    packetResent++;
                }
            } else if (ackNum > sendBase) {
                sem_wait(&mutex);
                sendBase = ackNum;
                cwnd += 1 / cwnd;
                sem_post(&mutex);
                dupACKcount = 0;
            }
        } else { // Fast recovery
            if (ackNum == sendBase) {
                dupACKcount++;
                sem_wait(&mutex);
                cwnd += 1;
                sem_post(&mutex);
            } else if (ackNum > sendBase) {
                mode = CA;
                sem_wait(&mutex);
                sendBase = ackNum;
                cwnd = ssthresh;
                sem_post(&mutex);
                dupACKcount = 0;
            }
        }
    }
    return NULL;
}

void timeOutHandler(int) {
	timeOutNum++;
    sem_wait(&mutex);
    segment* packet = packetBuffer[sendBase];
    sem_post(&mutex);
    printf("Timeout! Expect ack of %lld. Resend packet with seqNum=%lld\n", timerNum, packet->seqNum);
    clock_gettime(CLOCK_REALTIME, &packet->sendTime);
    sendto(s, packet, sizeof(segment), 0,
        (struct sockaddr *)&si_other, slen);
    packetResent++;
    sem_wait(&mutex);
    mode = SS;
    ssthresh = cwnd * 0.5;
    cwnd = 1;
    timerNum = packet->seqNum;
    printf("Timer restart! timerNum=%lld\n", timerNum);
    printf("%f\n", timeOutInterval*1000);
    ualarm(timeOutInterval*1000, 0);
    dupACKcount = 0;
    sem_post(&mutex);
}

void calculateRTT(struct timespec sendTime) {
	struct timespec recvTime;
	clock_gettime(CLOCK_REALTIME, &recvTime);
	double sampleRTT = (recvTime.tv_sec-sendTime.tv_sec)*1000 + (recvTime.tv_nsec-sendTime.tv_nsec)/1000000.0;
	devRTT = (1-beta)*devRTT + beta*fabs(sampleRTT-estimatedRTT);
	estimatedRTT = (1-alpha)*estimatedRTT + alpha*sampleRTT;
	sem_wait(&mutex);
	timeOutInterval = estimatedRTT + 4*devRTT;
    sem_post(&mutex);
}

void storeFile(char* filename, ull actualBytes) {
    FILE *fp;
    fp = fopen(filename, "rb");
    ull read = 0;
    for (ull i = 0; i < packetNum; i++) {
        segment* packet = (segment*)malloc(sizeof(segment));
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

void diep(const char* s) {
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