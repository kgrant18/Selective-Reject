#ifndef WINDOWING_H
#define WINDOWING_H

#include <stdint.h>


struct SR_buffer *initializeWindow(int window_size);
int checkWindowOpen(int nextSeqNum, int window_size);
int createWindowPacket(uint8_t *packetBuffer, int sn, uint8_t *payload, int payloadLen);
int windowStorePacket(struct SR_buffer *window_buffer, int sn, uint8_t *pdu, int pduLen, int window_size);
int getNextSeqNum(void);
int getSeqNum(void);
void processRRpacket(uint8_t *RR_packet, int sequenceNumber, struct SR_buffer *window_buffer, int window_size);
void processSREJpacket(uint8_t *SREJ_packet, int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer);
int getPacketToResend(uint8_t *resendPDU, int resendSeqNum, int *resend_len, int window_size, struct SR_buffer *window_buffer);
void validateBuffer(struct SR_buffer *window_buffer, int seqNum, int window_size);

int formatReceiverReadyPDU(uint8_t *pduBuffer, uint32_t pkt_seqNum, uint32_t RR_seqNum, uint8_t flag, uint8_t *payload, int payloadLen);
int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen);
void printPDU(uint8_t *aPDU, int pduLength);

#endif
