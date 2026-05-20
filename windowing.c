#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>

#include "checksum.h"
#include "windowing.h"

#define RR_PDU_SIZE 11
#define MAX_PDU 1407

static int lower = 0;
static int upper = 0;
static int current = 0; 
static int sequenceNum = 2; 

int formatReceiverReadyPDU(uint8_t *pduBuffer, uint32_t pkt_seqNum, uint32_t RR_seqNum, uint8_t flag, uint8_t *payload, int payloadLen) {
    /**
     * Format: Packet seq num, checksum, flag, RR seq number
     */
    
    uint32_t seqNum_no = htonl(pkt_seqNum);
    memcpy(pduBuffer, &seqNum_no, 4);

    memcpy(pduBuffer + 6, &flag, 1);

    uint32_t RR_seqNum_no = htonl(RR_seqNum); 
    memcpy(pduBuffer + 7, &RR_seqNum_no, 4);

    //set checksum to 0s initially
    memset(pduBuffer + 4, 0, 2);
    //run checksum function
    unsigned short cksum = in_cksum((unsigned short *)pduBuffer, RR_PDU_SIZE);
    //now add to PDU
    memcpy(pduBuffer + 4, &cksum, 2);

    return RR_PDU_SIZE; 
}

int send_data(int file_fd) {

    while (window_open == 1) {
        window_open = window_open < 
    }


    return 0; 
}

int createPDU(uint8_t *pduBuffer, uint32_t sequenceNumber, uint8_t flag, uint8_t *payload, int payloadLen) {
   /**
    * Create PDU for Lab6/Program 3
    */

    uint32_t seqNum_no = htonl(sequenceNumber); 
    int pduLength = 4 + 2 + 1 + payloadLen;

    //first 4 bytes are sequence number
    memcpy(pduBuffer, &seqNum_no, 4); 

    //1 byte flag
    memcpy(pduBuffer + 6, &flag, 1);

    //add payload
    memcpy(pduBuffer + 7, payload, payloadLen);


    //run checksum at the end
    //set checksum field to 0s
    memset(pduBuffer + 4, 0, 2);
    //run checksum function
    unsigned short cksum = in_cksum((unsigned short *)pduBuffer, pduLength);
    //now add to PDU
    memcpy(pduBuffer + 4, &cksum, 2);
    
    return pduLength; 

}

void printPDU(uint8_t *aPDU, int pduLength) {
    uint32_t sequenceNumber;
    uint8_t flag;
    unsigned short checksum;
    uint8_t *payload; 

    int index = 0; 

    memcpy(&sequenceNumber, aPDU, 4);
    index += 4; 
    //checksum
    index += 2; 
    memcpy(&flag, aPDU + index, 1);
    index += 1; 

    int payloadLen = pduLength - 7; 
    
    payload = aPDU + index;  

    checksum = in_cksum((unsigned short *)aPDU, pduLength);

    if (checksum != 0) {
        fprintf(stderr, "checksum failed\n");
    }

    printf("Sequence Number: %u\n", ntohl(sequenceNumber));
    printf("Checksum: %u\n", checksum);
    printf("Flag: %u\n", flag);
    printf("Message: %.*s\n", payloadLen, payload);

}