#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>

#include "checksum.h"
#include "windowing.h"
#include "pollLib.h"
#include "safeUtil.h"
#include "bufferMngment.h"

#define MAXBUF 1407

static int lower = 0;
static int sequenceNum = 0; 



int checkWindowOpen(int nextSeqNum, int window_size) {
    int window_open = 0; 
    int upper = lower + window_size;

    if (nextSeqNum < upper) {
        //window is open
        window_open = 1; 
    }

    return window_open;
}

int createWindowPacket(uint8_t *packetBuffer, int sn, uint8_t *payload, int payloadLen) {
    int pduLen = 0;
    int flag = 3;
    
    pduLen = createPDU(packetBuffer, sn, flag, payload, payloadLen);    

    return pduLen;
}

int windowStorePacket(struct SR_buffer *window_buffer, int sn, uint8_t *pdu, int pduLen, int window_size) {
    int index = sn % window_size;

    memcpy(window_buffer[index].buff, pdu, pduLen);
    window_buffer[index].pduLen = pduLen;
    window_buffer[index].sequenceNumber = sn;
    window_buffer[index].validFlag = 0; 

    return 0; 
}

int getNextSeqNum(void) {
    sequenceNum++; 
    return sequenceNum;
}

int getSeqNum(void) {
    return sequenceNum; 
}

void processRRpacket(uint8_t *RR_packet, struct SR_buffer *window_buffer, int window_size) {
    /**
     * Process the RR packet when received and update lower appropriately 
     */

    uint32_t RR_seq_num_no;
    memcpy(&RR_seq_num_no, RR_packet + 7, 4);

    //convert to host order seq num
    uint32_t RR_seq_num = ntohl(RR_seq_num_no);
    
    //validate the data packets until RR num
    while (lower < RR_seq_num) {
        validateBuffer(window_buffer, lower, window_size);
        
        //increment lower until lower = RR_seq_num
        lower++; 
    }
}

void processSREJpacket(uint8_t *SREJ_packet, int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer) {
    /**
     * Process the SREJ packet and resend that data
     */

    int serverAddrLen = sizeof(struct sockaddr_in6); 

    uint32_t SREJ_seq_num_no;
    memcpy(&SREJ_seq_num_no, SREJ_packet + 7, 4);

    //convert to host order
    uint32_t SREJ_seq_num = ntohl(SREJ_seq_num_no);

    uint8_t resend_packet[MAXBUF];

    int resend_len = getPacketToResend(resend_packet, SREJ_seq_num, window_size, window_buffer);

    safeSendto(socketNum, resend_packet, resend_len, 0, (struct sockaddr *)server, serverAddrLen);

}

int getPacketToResend(uint8_t *resendPDU, int resendSeqNum, int window_size, struct SR_buffer *window_buffer) {
    /**
     * Put the data that needs to be retransmitted in resendPDU 
     */

    int index = resendSeqNum % window_size; 

    //check the sequence numbres align
    if (window_buffer[index].sequenceNumber != resendSeqNum) {
        return -1; 
    }


    //copy data into resendPDU
    memcpy(resendPDU, window_buffer[index].buff, window_buffer[index].pduLen);

    return window_buffer[index].pduLen;
}


void validateBuffer(struct SR_buffer *window_buffer, int seqNum, int window_size) {
    /**
     * Set valid flag for the packets that have been RRed
     */

    int index = seqNum % window_size;     

    if (window_buffer[index].sequenceNumber == seqNum && window_buffer[index].validFlag == 0) {
        window_buffer[index].validFlag = 1; 
    }

}

int getLowestPacket(void) {
    return lower; 
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