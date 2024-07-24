#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/time.h>
#include <time.h>
#include <assert.h>

#include "packet.h"
#include "common.h"

#define STDIN_FD    0
#define RETRY  120 // milliseconds
#define window_size 10

// global vars for seq numbers and window mangement
int next_seqno = 0;
int send_base = 0;
// int window_size = 10; // Window size is set to 10
int sockfd, serverlen;
struct sockaddr_in serveraddr;
struct itimerval timer;
tcp_packet *sndpkt;
tcp_packet *recvpkt;
sigset_t sigmask;
tcp_packet *window[window_size]; // Window to hold packets

// resend packets during timeout
void resend_packets(int sig) {
    if (sig == SIGALRM) {
        // Resend all packets in the window
        VLOG(INFO, "Timeout happened, resending packets in window");
        for (int i = 0; i < window_size; i++) {
            if (window[i] != NULL) {
                VLOG(DEBUG, "Resending packet %d", window[i]->hdr.seqno);
                if (sendto(sockfd, window[i], TCP_HDR_SIZE + get_data_size(window[i]), 0,
                        (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                    error("sendto");
                }
            }
        }
    }
}

void start_timer() {
    sigprocmask(SIG_UNBLOCK, &sigmask, NULL);
    setitimer(ITIMER_REAL, &timer, NULL);
}

void stop_timer() {
    sigprocmask(SIG_BLOCK, &sigmask, NULL);
}

// initializes timer
void init_timer(int delay, void (*sig_handler)(int)) {
    signal(SIGALRM, resend_packets);
    timer.it_interval.tv_sec = delay / 1000;
    timer.it_interval.tv_usec = (delay % 1000) * 1000;
    timer.it_value.tv_sec = delay / 1000;
    timer.it_value.tv_usec = (delay % 1000) * 1000;

    sigemptyset(&sigmask);
    sigaddset(&sigmask, SIGALRM);
}

int main (int argc, char **argv) {
    int portno, len;
    char *hostname;
    char buffer[DATA_SIZE];
    FILE *fp;

    // user input validation
    if (argc != 4) {
        fprintf(stderr,"usage: %s <hostname> <port> <FILE>\n", argv[0]);
        exit(0);
    }
    hostname = argv[1];
    portno = atoi(argv[2]);
    fp = fopen(argv[3], "r");
    if (fp == NULL) {
        error(argv[3]);
    }

    // create UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serverlen = sizeof(serveraddr);

    // convert hostname to network address 
    if (inet_aton(hostname, &serveraddr.sin_addr) == 0) {
        fprintf(stderr,"ERROR, invalid host %s\n", hostname);
        exit(0);
    }

    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);

    assert(MSS_SIZE - TCP_HDR_SIZE > 0);

    init_timer(RETRY, resend_packets);
    next_seqno = 0;

    while (1) {
        // send packets while within the window size
        while (next_seqno < send_base + window_size) {
            len = fread(buffer, 1, DATA_SIZE, fp);
            // printf("len size: %d \n", len);
            if (len <= 0) {
                VLOG(INFO, "End Of File has been reached");
                sndpkt = make_packet(0);
                if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                    error("sendto");
                }
                break;
            }

            // printf("making pkt\n");
            sndpkt = make_packet(len);
            memcpy(sndpkt->data, buffer, len);
            sndpkt->hdr.seqno = next_seqno;
            // printf("index: %d \n",next_seqno % window_size);
            window[next_seqno % window_size] = sndpkt;

            VLOG(DEBUG, "Sending packet %d to %s", next_seqno, inet_ntoa(serveraddr.sin_addr));
            if (sendto(sockfd, sndpkt, TCP_HDR_SIZE + get_data_size(sndpkt), 0,
                    (const struct sockaddr *)&serveraddr, serverlen) < 0) {
                error("sendto");
            }

            if (next_seqno == send_base) {
                start_timer();
            }
            next_seqno += len;
        }

        // recieve ACK from server
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &serveraddr, (socklen_t *)&serverlen) < 0) {
            error("recvfrom");
        }

        recvpkt = (tcp_packet *)buffer;
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        // if (recvpkt->hdr.ackno == 0) { // // alternate
        //     break;
        // }
        
        // handle recieved acknowledgment
        if (recvpkt->hdr.ackno > send_base) {
            VLOG(DEBUG, "Received ACK for packet %d", recvpkt->hdr.ackno);
            send_base = recvpkt->hdr.ackno;
            if (send_base == next_seqno) {
                stop_timer();
            } else {
                start_timer();
            }
        }
        if (len <= 0) {
            break;
        }
    }

    return 0;
}
