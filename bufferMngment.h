#ifndef BUFFERING_H
#define BUFFERING_H

#define MAXBUF 1407
#define RR_PDU_SIZE 11

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>


typedef enum receiverState {
	INORDER, BUFFERING, FLUSHING, BUFFER_DONE
} receiverState;

enum FLAG {
	SETUP_PACKET=1, SETUP_RESPONSE=2, DATA_PKT=3, RR_PACKET=5, SREJ_PACKET=6, RCOPY_TO_SERVER=7, SERVER_TO_RCOPY=8, ERR_FLAG = 32, EOF_ACK=33
};

struct SR_buffer {
    uint8_t buff[MAXBUF];
    int pduLen;
    int sequenceNumber; 
    int validFlag; 
};

struct bufferInfo {
	int window_size;
	int buffer_size;
	int file_fd;
	struct sockaddr_in6 client;
};

struct serverStateData {
	receiverState state;
	int expectedSeqNum;
	int eofSeqNum;
	struct SR_buffer *buffer; 
};

struct SR_buffer *initializeBuffer(int window_size);
void bufferManagement(int childSocketNum, int seqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, struct receiverManager *manager);
receiverState inorderFunction(int *expectedSeqNum, int actualSeqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer, int eofSeqNum);
receiverState bufferingFunction(int *expectedSeqNum, int actualSeqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer);
receiverState flushingFunction(int *expectedSeqNum, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer);
int checkIfBuffered(int seqNum, int window_size, struct SR_buffer *buffer);
void sendRR(int childSocketNum, int RR_seqNum, struct bufferInfo *buffer_struct);
void sendSREJ(int childSocketNum, int SREJ_seqNum, struct bufferInfo *buffer_struct);
void storePacketInBuffer(struct SR_buffer *buffer, int seqNum, uint8_t *payload, int payloadLen, int window_size);
int formatReceiverReadyPDU(uint8_t *pduBuffer, uint32_t pkt_seqNum, uint32_t RR_seqNum, uint8_t flag);

#endif
