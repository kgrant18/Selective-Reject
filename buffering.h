#ifndef BUFFERING_H
#define BUFFERING_H

struct buffer {
    uint8_t buff[MAX_PDU];
    int sequenceNumber; 
    int validFlag; 
}

#endif
