#include <stdio.h>
#include <stdlib.h>

#define MAX_PDU 1407

struct SR_buffer *allocateBuffer(int window_size) {
    struct SR_buffer *buffer = malloc(window_size * sizeof(struct SR_buffer));
    if (SR_buffer == NULL) {
        perror("malloc failed");
        exit(-1);
    }

    return buffer;
}


