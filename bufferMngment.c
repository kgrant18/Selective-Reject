#include "bufferMngment.h"
#include "safeUtil.h"
#include "checksum.h"
#include "windowing.h"

struct SR_buffer *initializeBuffer(int window_size) {
    /*
     * Create the buffer to be used on both server and rcopy sides 
    */
    
    //allocate memory for the buffer
    struct SR_buffer *buffer = malloc(window_size * sizeof(struct SR_buffer));
    if (buffer == NULL) {
        perror("malloc failed");
        exit(-1);
    }

    //default values to 0 to avoid garbage
    int i = 0; 
    for (i = 0; i < window_size; i++) {
        buffer[i].pduLen = 0; 
        buffer[i].sequenceNumber = 0; 
        buffer[i].validFlag = 0; 
    }

    return buffer;
}

void bufferManagement(int childSocketNum, int seqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, struct receiverManager *manager) {
    /**
     * The main control and state machine for buffering on the receiving (server) side
    */

    switch (manager->state) {
        case INORDER: {
            manager->state = inorderFunction(&manager->expectedSeqNum, seqNum, payload, payloadLen, buffer_struct, childSocketNum, manager->buffer, manager->eofSeqNum); 
            break;
        }

        case BUFFERING: {
            manager->state = bufferingFunction(&manager->expectedSeqNum, seqNum, payload, payloadLen, buffer_struct, childSocketNum, manager->buffer);
            break;
        }

        case FLUSHING: {
            manager->state = flushingFunction(&manager->expectedSeqNum, buffer_struct, childSocketNum, manager->buffer);
            break; 
        }

        case BUFFER_DONE: {
            manager->state = BUFFER_DONE; 
            break;
        }

        default: {
            manager->state = BUFFER_DONE; 
        }

    }
}

receiverState inorderFunction(int *expectedSeqNum, int actualSeqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer, int eofSeqNum) {
    /**
     * Determine if data is received in order and procceed as necessary
    */
    
    if (actualSeqNum == *expectedSeqNum) {
        //if expected, we can write to file and procceed
        int to_fd = buffer_struct->file_fd;

        //write data to file
        ssize_t w_bytes = write(to_fd, payload, payloadLen);
        if (w_bytes < 0) {
            perror("write failed");
            return BUFFER_DONE; 
        }

        //clear slot in buffer after writing to file
        int index = actualSeqNum % buffer_struct->window_size;
        buffer[index].validFlag = 0; 

        //increment expected to next sequence number
        (*expectedSeqNum)++; 

        //check if we've reached EOF 
        if (*expectedSeqNum > eofSeqNum) {
            sendEOFAck(int childSocketNum, *expectedSeqNum, buffer_struct);
            return BUFFER_DONE; 
        }

        //send the RR acknowledgment
        sendRR(childSocketNum, *expectedSeqNum, buffer_struct);

        //continue in this state until something is out of order
        return INORDER; 
    } 

    else if (actualSeqNum > *expectedSeqNum) {        
        //store the received packet in buffer
        storePacketInBuffer(buffer, actualSeqNum, payload, payloadLen, buffer_struct->window_size);

        //send SREJ for not receiving the expected packet
        sendSREJ(childSocketNum, *expectedSeqNum, buffer_struct);
        
        return BUFFERING; 
    }

    else {
        //actualSeqNum < *expectedSeqNum (duplicate data!)
        sendRR(childSocketNum, *expectedSeqNum, buffer_struct);
        
        return INORDER;
    }
}

receiverState bufferingFunction(int *expectedSeqNum, int actualSeqNum, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer) {

    if (actualSeqNum == *expectedSeqNum) {
        //the missing packet arrives 
        int to_fd = buffer_struct->file_fd; 
        ssize_t w_bytes = write(to_fd, payload, payloadLen);
        if (w_bytes < 0) {
            perror("write failed");
            return BUFFER_DONE; 
        }

        //clear slot in buffer after writing to file
        int index = actualSeqNum % buffer_struct->window_size;
        buffer[index].validFlag = 0; 

        //increment expected
        (*expectedSeqNum)++; 

        //check if we've reached EOF 
        if (*expectedSeqNum > eofSeqNum) {
            sendEOFAck(childSocketNum, *expectedSeqNum, buffer_struct);
            return BUFFER_DONE; 
        }

        return FLUSHING; 
    }

    else if (actualSeqNum > *expectedSeqNum) {
        //store the received packet in buffer
        storePacketInBuffer(buffer, actualSeqNum, payload, payloadLen, buffer_struct->window_size);

        //send RR
        sendRR(childSocketNum, *expectedSeqNum, buffer_struct);
        
        return BUFFERING; 
    }

    else {
        //actualSeqNum < *expectedSeqNum
        sendRR(childSocketNum, *expectedSeqNum, buffer_struct);

        return BUFFERING; 
    }
}

receiverState flushingFunction(int *expectedSeqNum, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer) {
    /**
     * if the expected packet is already buffered: write to file and send RR
    */

    while (checkIfBuffered(*expectedSeqNum, buffer_struct->window_size, buffer) == 1) {
        //write the files to file that have already been buffered 

        int index = *expectedSeqNum % buffer_struct->window_size;

        int to_file_fd = buffer_struct->file_fd; 
        ssize_t w_bytes = write(to_file_fd, buffer[index].buff, buffer[index].pduLen);
        if (w_bytes < 0) {
            perror("write failed");
            return BUFFER_DONE;
        }

        //free up buffer after writing to file
        buffer[index].validFlag = 0; 

        //increment expected
        (*expectedSeqNum)++; 
    }

    //send single RR after writing everything that's been buffered
    sendRR(childSocketNum, *expectedSeqNum, buffer_struct);

    return INORDER; 
}

void sendEOFAck(int childSocketNum, int *expectedSeqNum, struct bufferInfo *buffer_struct) {
    /**
     * Last ACK sent once EOF is reached 
    */
    
    int flag = EOF_ACK; 

    uint8_t PDU[MAXBUF];

    //payload doesn't matter
    uint8_t payload[1];
    memcpy(payload, 0, 1);

    //create EOF PDU
    int num_bytes = createPDU(PDU, *expectedSeqNum, flag, payload);

    //send to rcopy
    struct sockaddr_in6 client = buffer_struct->client;
    int clientAddrLen = sizeof(client); 
    safeSendto(childSocketNum, PDU, num_bytes, 0, (struct sockaddr *)&client, clientAddrLen);
}

int checkIfBuffered(int seqNum, int window_size, struct SR_buffer *buffer) {
    /**
     * Checks if a packet has been buffered  
    */

    int index = seqNum % window_size; 

    if ((buffer[index].validFlag == 1) && buffer[index].sequenceNumber == seqNum) {
        //yes buffered
        return 1; 
    }  
    else {
        //not buffered
        return 0; 
    }

    return 0; 
}

void sendRR(int childSocketNum, int RR_seqNum, struct bufferInfo *buffer_struct) {
    /*
     * Formats the RR PDU and sends to rcopy
    */
    
    uint8_t PDU[MAXBUF];

    //format
    int pdu_size = formatReceiverReadyPDU(PDU, 0, RR_seqNum, RR_PACKET);

    //send to rcopy
    struct sockaddr_in6 client = buffer_struct->client;
    int clientAddrLen = sizeof(client); 
    safeSendto(childSocketNum, PDU, pdu_size, 0, (struct sockaddr *)&client, clientAddrLen);
}

void sendSREJ(int childSocketNum, int SREJ_seqNum, struct bufferInfo *buffer_struct) {
    /* 
     * Formats the SREJ PDU and sends to rcopy
    */
    
    uint8_t PDU[MAXBUF];

    //format
    int PDU_size = formatReceiverReadyPDU(PDU, 0, SREJ_seqNum, SREJ_PACKET);

    //send to rcpoy
    struct sockaddr_in6 client = buffer_struct->client; 
    int clientAddrLen = sizeof(client);
    safeSendto(childSocketNum, PDU, PDU_size, 0, (struct sockaddr *)&client, clientAddrLen);
}

void storePacketInBuffer(struct SR_buffer *buffer, int seqNum, uint8_t *payload, int payloadLen, int window_size) {
    /**
     * Add PDU to the buffer 
    */
    
    int index = seqNum % window_size; 

    buffer[index].sequenceNumber = seqNum;
    buffer[index].pduLen = payloadLen;
    buffer[index].validFlag = 1;

    memcpy(buffer[index].buff, payload, payloadLen);
}

int formatReceiverReadyPDU(uint8_t *pduBuffer, uint32_t pkt_seqNum, uint32_t RR_seqNum, uint8_t flag) {
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