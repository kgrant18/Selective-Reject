#ifndef BUFFERING_H
#define BUFFERING_H

#define MAX_PDU 1407

struct SR_buffer {
    uint8_t buff[MAX_PDU];
    int pduLen;
    int sequenceNumber; 
    int validFlag; 
};

#endif
