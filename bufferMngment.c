#include "bufferMngment.h"
#include "safeUtil.h"
#include "checksum.h"
#include "windowing.h"
#include "pollLib.h"

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

void bufferManagement(int childSocketNum, int seqNum, int flag, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, struct serverStateData *tracker) {
    /**
     * The main control and state machine for buffering on the receiving (server) side
    */

    switch (tracker->state) {
        case INORDER: {
            tracker->state = inorderFunction(&tracker->expectedSeqNum, seqNum, flag, payload, payloadLen, buffer_struct, childSocketNum, tracker->buffer, tracker->eofSeqNum); 
            break;
        }

        case BUFFERING: {
            tracker->state = bufferingFunction(&tracker->expectedSeqNum, seqNum, flag, payload, payloadLen, buffer_struct, childSocketNum, tracker->buffer, tracker->eofSeqNum);
            break;
        }

        case FLUSHING: {
            tracker->state = flushingFunction(&tracker->expectedSeqNum, buffer_struct, childSocketNum, tracker->buffer, tracker->eofSeqNum);
            break; 
        }

        case SEND_EOF_ACK: {
            tracker->state = continuouslySendEOFAck(childSocketNum, tracker->eofSeqNum, buffer_struct);
            break;
        }

        case BUFFER_DONE: {
            printf("file transfer complete. Sitting in BUFFER_DONE state\n"); 
            break;
        }

        default: {
            tracker->state = BUFFER_DONE; 
        }

    }
}

receiverState inorderFunction(int *expectedSeqNum, int actualSeqNum, int flag, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer, int eofSeqNum) {
    /**
     * Determine if data is received in order and procceed as necessary
    */
    
    if (actualSeqNum == *expectedSeqNum) {
        if (flag == DATA_PKT) {
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
        }
         
        //increment expected to next sequence number
        (*expectedSeqNum)++; 

        //check if we've reached EOF 
        if (flag == EOF_FLAG) {
            sendEOFAck(childSocketNum, eofSeqNum, buffer_struct);
            return SEND_EOF_ACK; 
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

receiverState bufferingFunction(int *expectedSeqNum, int actualSeqNum, int flag, uint8_t *payload, int payloadLen, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer, int eofSeqNum) {

    if (actualSeqNum == *expectedSeqNum) {
        if (flag == DATA_PKT) {
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
        }
        
        //increment expected
        (*expectedSeqNum)++; 

        //check if we've reached EOF 
        if (flag == EOF_FLAG) {
            sendEOFAck(childSocketNum, eofSeqNum, buffer_struct);
            return SEND_EOF_ACK; 
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

receiverState flushingFunction(int *expectedSeqNum, struct bufferInfo *buffer_struct, int childSocketNum, struct SR_buffer *buffer, int eofSeqNum) {
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

    //check for EOF
    if ((*expectedSeqNum == eofSeqNum + 1) && (eofSeqNum != -1)) {
        sendEOFAck(childSocketNum, eofSeqNum, buffer_struct);
        return BUFFER_DONE;
    }

    //send single RR after writing everything that's been buffered
    sendRR(childSocketNum, *expectedSeqNum, buffer_struct);

    return INORDER; 
}

receiverState continuouslySendEOFAck(int childSocketNum, int eofSeqNum, struct bufferInfo *buffer_struct) {
    /**
     * Continue to resend EOF ACK packet until it is received. 
    */

    int counter = 0; 
 
    sendEOFAck(childSocketNum, eofSeqNum, buffer_struct);

    while (counter < 10) {
        //poll for 1 second
        int pollResult = pollCall(1000);
        
        if (pollResult <  0) {
            //timeout so hopefully the sender recieved the packet
            counter++;
            continue; 
        }

        else {
            //recieved something, so send EOF ack packet 
            uint8_t PDU[MAXBUF];
            struct sockaddr_in6 client;
            int clientAddrLen = sizeof(struct sockaddr_in6);

            safeRecvfrom(childSocketNum, PDU, MAXBUF, 0, (struct sockaddr *)&client, &clientAddrLen);

            sendEOFAck(childSocketNum, eofSeqNum, buffer_struct);
        }
    }
    
    return BUFFER_DONE;
}

void sendEOFAck(int childSocketNum, int eofSeqNum, struct bufferInfo *buffer_struct) {
    /**
     * Last ACK sent once EOF is reached 
    */
    
    int flag = EOF_ACK; 

    uint8_t PDU[MAXBUF];

    //payload doesn't matter for this packet
    uint8_t payload[1];
    payload[0] = 0; 

    //create EOF PDU
    int num_bytes = createPDU(PDU, eofSeqNum, flag, payload, 1);

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