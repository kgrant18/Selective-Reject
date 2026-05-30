#ifndef WINDOWING_H
#define WINDOWING_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/time.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>

#include "bufferMngment.h"

int checkWindowOpen(int nextSeqNum, int window_size);
int createWindowPacket(uint8_t *packetBuffer, int sn, uint8_t *payload, int payloadLen);
void windowStorePacket(struct SR_buffer *window_buffer, int sn, uint8_t *pdu, int pduLen, int window_size);
int getNextSeqNum(void);
int getSeqNum(void);
void processRRpacket(uint8_t *RR_packet, struct SR_buffer *window_buffer, int window_size);
void processSREJpacket(uint8_t *SREJ_packet, int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer);
int getPacketToResend(uint8_t *resendPDU, int resendSeqNum, int window_size, struct SR_buffer *window_buffer);
void validateBuffer(struct SR_buffer *window_buffer, int seqNum, int window_size);
int getLowestPacket(void);

int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen);
void printPDU(uint8_t *aPDU, int pduLength);

#endif
