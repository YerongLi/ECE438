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
#define payload 1450
#define cwndRatio 0.5

typedef struct {
    ull seqNum;
    ull length;
    char end;
    char data[payload];
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
int timeOutInterval = 30; // ms
double ssthresh = 100;
double cwnd = 1;
ull sendBase = 0;
ull nextSeqNum = 0;
ull packetResent = 0;
ull timeOutNum = 0;
/* sendBase and cwnd are shared by 2 threads, but in thread 1 they are only read.
And they are only changed in thread 2, so in thread 2 only write need mutex */
sem_t mutex;

void diep(const char* s);
ull readSize(char* filename, ull bytesToTransfer);
void storeFile(char* filename, ull actualBytes);
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
    clock_t start = clock();
    pthread_t recvThread;
    sem_init(&mutex, 0, 1);
    while (1) {
        if (nextSeqNum == packetNum)
            break;
        sem_wait(&mutex);
        ull wnEnd = sendBase + cwnd;
        sem_post(&mutex);
        if (nextSeqNum < packetNum && nextSeqNum < wnEnd) {
            segment* packet = packetBuffer[nextSeqNum];
            sendto(s, packet, sizeof(segment), 0,
                (struct sockaddr *)&si_other, slen);
            // printf("Message %lld sent from main thread\n", nextSeqNum);
            nextSeqNum++;
            if (nextSeqNum == 1)
                pthread_create(&recvThread, NULL, threadRecvRetransmit, NULL);
        }
    }
    pthread_join(recvThread, NULL);
    clock_t end = clock();
    double timeUsed = ((double)(end-start))/CLOCKS_PER_SEC;
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
    struct timeval timeout;      
    timeout.tv_sec = 0;
    timeout.tv_usec = timeOutInterval * 1000;
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    while (1) {
        /* TCP friendliness */
        ull ack;
        int numbytes = recvfrom(s, &ack, sizeof(ull), 0,
            (struct sockaddr *)&si_other, &slen);
        if (numbytes == -1) { // Timeout
            segment* packet = packetBuffer[sendBase];
            // printf("Timeout! base=%lld, Resend packet with seqNum=%lld\n", sendBase, packet->seqNum);
            timeOutNum++;
            sendto(s, packet, sizeof(segment), 0,
                (struct sockaddr *)&si_other, slen);
            packetResent++;
            mode = SS;
            ssthresh = cwnd * cwndRatio;
            sem_wait(&mutex);
            cwnd = 1;
            sem_post(&mutex);
            dupACKcount = 0;
            continue;
        }
        // printf("ack=%lld, base=%lld, seq=%lld, mode=%d, cwnd=%.3f, thresh=%.3f, dup=%d\n", ack, sendBase, nextSeqNum, mode, cwnd, ssthresh, dupACKcount);
        if (ack == packetNum)
            break;
        if (mode == SS) { // Slow start
            if (ack == sendBase) {
                dupACKcount++;
                if (dupACKcount == 3) {
                    mode = FR;
                    ssthresh = cwnd * cwndRatio;
                    sem_wait(&mutex);
                    cwnd = ssthresh + 3;
                    sem_post(&mutex);
                    segment* packet = packetBuffer[sendBase];
                    sendto(s, packet, sizeof(segment), 0,
                        (struct sockaddr *)&si_other, slen);
                    packetResent++;
                }
            } else if (ack > sendBase) {
                sem_wait(&mutex);
                sendBase = ack;
                cwnd += 1;
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
                    ssthresh = cwnd * cwndRatio;
                    sem_wait(&mutex);
                    cwnd = ssthresh + 3;
                    sem_post(&mutex);
                    segment* packet = packetBuffer[sendBase];
                    sendto(s, packet, sizeof(segment), 0,
                        (struct sockaddr *)&si_other, slen);
                    packetResent++;
                }
            } else if (ack > sendBase) {
                sem_wait(&mutex);
                sendBase = ack;
                cwnd += 1 / cwnd;
                sem_post(&mutex);
                dupACKcount = 0;
            }
        } else { // Fast recovery
            if (ack == sendBase) {
                dupACKcount++;
                sem_wait(&mutex);
                cwnd += 1 / cwnd;
                sem_post(&mutex);
                if (dupACKcount % 3 == 0) {
                    segment* packet = packetBuffer[sendBase];
                    sendto(s, packet, sizeof(segment), 0,
                        (struct sockaddr *)&si_other, slen);
                    packetResent++;
                }
            } else if (ack > sendBase) {
                mode = CA;
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