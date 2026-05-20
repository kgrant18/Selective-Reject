#ifndef WINDOWING_H
#define WINDOWING_H

#include <stdint.h>

int formatReceiverReadyPDU(uint8_t *pduBuffer, uint32_t pkt_seqNum, uint32_t RR_seqNum, uint8_t flag, uint8_t *payload, int payloadLen);
int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen);
void printPDU(uint8_t *aPDU, int pduLength);

#endif
