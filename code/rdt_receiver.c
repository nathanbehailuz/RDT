#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <assert.h>

#include "common.h"
#include "packet.h"

//pointers to send and receive packets 
tcp_packet *recvpkt;
tcp_packet *sndpkt;

int main(int argc, char **argv) {
    int sockfd;
    int portno;
    int clientlen;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int optval;
    FILE *fp;
    char buffer[MSS_SIZE];
    struct timeval tp;
    int expected_seqno = 0;

    // user input check 
    if (argc != 3) {
        fprintf(stderr, "usage: %s <port> FILE_RECVD\n", argv[0]);
        exit(1);
    }
    portno = atoi(argv[1]);

    // open file for wiriting 
    fp  = fopen(argv[2], "w");
    if (fp == NULL) {
        error(argv[2]);
    }

    // create a UDP socket
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0)
        error("ERROR opening socket");

    optval = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, (const void *)&optval , sizeof(int));

    bzero((char *) &serveraddr, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons((unsigned short)portno);

    // bind the socket to the server address 
    if (bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
        error("ERROR on binding");

    VLOG(DEBUG, "epoch time, bytes received, sequence number");

    clientlen = sizeof(clientaddr);
    while (1) {
        // recieve data from the client 
        if (recvfrom(sockfd, buffer, MSS_SIZE, 0, (struct sockaddr *) &clientaddr, (socklen_t *)&clientlen) < 0) {
            error("ERROR in recvfrom");
        }
        recvpkt = (tcp_packet *) buffer;
        printf("size = %d from packet %d\n", recvpkt->hdr.data_size, recvpkt->hdr.seqno);
        assert(get_data_size(recvpkt) <= DATA_SIZE);

        // checking for EOF
        if (recvpkt->hdr.data_size == 0) {
            VLOG(INFO, "End Of File has been reached, closing file.");
            VLOG(INFO, "Sending final ACKs and closing connection.");
            for (int i=0; i<100; i++) {
                sndpkt = make_packet(0);
                sndpkt->hdr.ackno = 0;
                sndpkt->hdr.ctr_flags = ACK;
                sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen);
            }
            fclose(fp);
            break;
        }

        gettimeofday(&tp, NULL); // get current time
        VLOG(DEBUG, "%lu, %d, %d", tp.tv_sec, recvpkt->hdr.data_size, recvpkt->hdr.seqno);

        // check if the received packet is in order
        if (recvpkt->hdr.seqno == expected_seqno) {
            VLOG(INFO, "Writing packet %d to file.", recvpkt->hdr.seqno);
            fseek(fp, recvpkt->hdr.seqno, SEEK_SET);
            fwrite(recvpkt->data, 1, recvpkt->hdr.data_size, fp);
            expected_seqno += recvpkt->hdr.data_size;
        } else {
            VLOG(INFO, "Out-of-order packet received, expected %d but got %d", expected_seqno, recvpkt->hdr.seqno);
        }

        // send ACK for recieved packet
        sndpkt = make_packet(0);
        sndpkt->hdr.ackno = expected_seqno;
        sndpkt->hdr.ctr_flags = ACK;
        VLOG(INFO, "Sending ACK for packet %d", expected_seqno);
        if (sendto(sockfd, sndpkt, TCP_HDR_SIZE, 0, (struct sockaddr *) &clientaddr, clientlen) < 0) {
            error("ERROR in sendto");
        }
    }

    return 0;
}
