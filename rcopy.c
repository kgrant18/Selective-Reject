// Client side - UDP Code				    
// By Hugh Smith	4/1/2017		

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

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "pollLib.h"
#include "windowing.h"
#include "bufferMngment.h"
#include "checksum.h"
#include "cpe464.h"

#define MAXBUF 1407
#define MAXPAYLOAD 1400

typedef enum State STATE;

enum State {
	FILENAME, FILE_OK, SEND_DATA, WAIT_ON_ACK, WAIT_ON_EOF_ACK, DONE
};

void processFile(char *argv[], int socketNum, int portNumber, struct sockaddr_in6 *server);
STATE sendSetupPacket(char *argv[], int socketNum, struct sockaddr_in6 *server);
STATE dataTransfer(int socketNum, struct sockaddr_in6 *server, int from_file_fd, int window_size, int buffer_size, struct SR_buffer *window_buffer, int *eof, int *eofSeqNum);
STATE waitOnAck(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer, int *counter, int *eof, int *eofSeqNum);
STATE waitOnEOFAck(int socketNum, struct sockaddr_in6 *server, int sequenceNum, int window_size, struct SR_buffer *window_buffer);
int processRRorSREJ(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer);
void sendEOFPacket(int socketNum, struct sockaddr_in6 *server, int sequenceNum);
int checkArgs(int argc, char * argv[]);

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	char *hostname = argv[6];

	double err_rate = atof(argv[5]);

	sendErr_init(err_rate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);

	socketNum = setupUdpClientToServer(&server, hostname, portNumber);
	
	processFile(argv, socketNum, portNumber, &server);
	
	close(socketNum);

	return 0;
}

void processFile(char *argv[], int socketNum, int portNumber, struct sockaddr_in6 *server) {
	/**
	 * handles the main control and state machine of the sending side (rcopy)
	*/

	STATE NS = FILENAME; 

	char *from_file = argv[1];
	//if file exists, open
	int from_file_fd = open(from_file, O_RDONLY);
	if (from_file_fd == -1) {
		char file_error_str[64]; 
		sprintf(file_error_str, "Error file [%s] not found", from_file);
		perror(file_error_str);
		exit(-1);
	}

	setupPollSet();
	addToPollSet(socketNum);

	//parse command line arguments
	int window_size = atoi(argv[3]);
	int buffer_size = atoi(argv[4]);
	
	//create buffer
	struct SR_buffer *window_buffer = initializeBuffer(window_size); 
	
	int eof = 0; 
	int counter = 0; 
	int eofSeqNum = 0; 

	//FSM
	while (NS != DONE) {
		switch (NS) {
			case FILENAME: {
				NS = sendSetupPacket(argv, socketNum, server);
				break; 
			}
			
			case FILE_OK: {
				NS = SEND_DATA;
				break;
			}

			case SEND_DATA: {
				NS = dataTransfer(socketNum, server, from_file_fd, window_size, buffer_size, window_buffer, &eof, &eofSeqNum);
				break;
			}

			case WAIT_ON_ACK: {
				NS = waitOnAck(socketNum, server, window_size, window_buffer, &counter, &eof, &eofSeqNum);
				break; 
			}

			case WAIT_ON_EOF_ACK: {
				NS = waitOnEOFAck(socketNum, server, eofSeqNum, window_size, window_buffer);
				break;
			}

			case DONE: {
				//nothing to be done here, transfer should be complete
				NS = DONE; 
				break; 
			}

			default: 
				NS = DONE; 
		}
	}

	close(from_file_fd);
	free(window_buffer);
}

STATE sendSetupPacket(char *argv[], int socketNum, struct sockaddr_in6 *server) {
	/**
	 * send setup packet to server and wait for response
	*/
 
	int serverAddrLen = sizeof(struct sockaddr_in6); 
	int counter = 0; 

	//first packet sent will have seqNum = 0
	int sequenceNumber = 0; 

	//parse parameters to be sent
	char *to_filename = argv[2]; 
	uint16_t window_size_no = htons(atoi(argv[3]));
	uint16_t buffer_size_no = htons(atoi(argv[4]));

	//format payload: [window size] [buffer size] [filename]
	uint8_t payload[MAXPAYLOAD];
	memcpy(payload, &window_size_no, 2);
	memcpy(payload + 2, &buffer_size_no, 2);
	memcpy(payload + 4, to_filename, strlen(to_filename) + 1);

	//create PDU to send to server
	uint8_t PDUbuffer[MAXBUF];
	int payloadLen = 2 + 2 + strlen(to_filename) + 1; 
	int pduLen = createPDU(PDUbuffer, sequenceNumber, RCOPY_TO_SERVER, payload, payloadLen);

	//loop for 10 times maximum
	while (counter < 10) {
		safeSendto(socketNum, PDUbuffer, pduLen, 0, (struct sockaddr *)server, serverAddrLen);

		//poll for 1 second
		int pollSocket = pollCall(1000); 

		if (pollSocket < 0) {
			//timeout = resend filename packet
			counter++; 
			continue; 
		}

		//receive the ACK in return
		uint8_t recvBuffer[MAXBUF];
		int bytes_recv = safeRecvfrom(socketNum, recvBuffer, MAXBUF, 0, (struct sockaddr *)server, &serverAddrLen);
		
		if (bytes_recv < 0) {
			//resend;
			counter++;
			continue; 
		}

		else {
			int flag = recvBuffer[6];
			if (flag == SERVER_TO_RCOPY) {
				return FILE_OK;
			}
			else {
				//not correct flag, resend
				counter++;
				continue; 
			}
		}
	}

	//should never get here, but terminate if it somehow does
	return DONE; 
}

STATE dataTransfer(int socketNum, struct sockaddr_in6 *server, int from_file_fd, int window_size, int buffer_size, struct SR_buffer *window_buffer, int *eof, int *eofSeqNum) {
	/**
	 * Continue to send data PDUs while the window is open and not EOF
	*/

	int serverAddrLen = sizeof(struct sockaddr_in6); 

	int nextSeqNum = getSeqNum() + 1; 
	while ((*eof == 0) && (checkWindowOpen(nextSeqNum, window_size) == 1)) {

		//continue to read data from disk while window is open
        uint8_t disk_buffer[buffer_size];
        int bytes_read = read(from_file_fd, disk_buffer, buffer_size);
        if (bytes_read < 0) {
            perror("read didn't work");
			close(from_file_fd);
			free(window_buffer);
            return DONE;
        }

        else if (bytes_read == 0) {
            //reached EOF
			*eof = 1; 
			*eofSeqNum = nextSeqNum; 

			int lower = getLowestPacket(); 
			if (lower == *eofSeqNum) {
				//only send EOF ack once all other acks are completed
				return WAIT_ON_EOF_ACK;
			}
			
			return WAIT_ON_ACK;
        }

		//increment API sequence number
		int cur_seqNum = getNextSeqNum(); 
		//increment my seqNum tracker
		nextSeqNum = getSeqNum() + 1; 

        //create PDU
        uint8_t pduBuffer[MAXBUF];
        int pduLen = createWindowPacket(pduBuffer, cur_seqNum, disk_buffer, bytes_read);

		//store in window buffer
        windowStorePacket(window_buffer, cur_seqNum, pduBuffer, pduLen, window_size);

        //send data
        safeSendto(socketNum, pduBuffer, pduLen, 0, (struct sockaddr *)server, serverAddrLen);

        //check if there's data to process now (doesn't wait)
        while (pollCall(0) > 0) {
			int returnVal = processRRorSREJ(socketNum, server, window_size, window_buffer);

			if (returnVal < 0) {
				//received a corrupted RR/SREJ
				continue; 
			}
		}
	}

	//when window is closed, need to wait for RRs
	return WAIT_ON_ACK;
}

STATE waitOnAck(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer, int *counter, int *eof, int *eofSeqNum) {
	/**
	 * Window is closed and need to wait for acknowledgements before continuing 
	 */
	
	int serverAddrLen = sizeof(struct sockaddr_in6);

	int pollResult = pollCall(1000); 

	if (pollResult < 0 && *counter < 10) {
		//keep resending packet on timeout
		uint8_t resendPDU[MAXBUF];
		int lower = getLowestPacket(); 
		
		//get the packet to be resent from the buffer
		int resendLen = getPacketToResend(resendPDU, lower, window_size, window_buffer);
		if (resendLen > 0) {
			safeSendto(socketNum, resendPDU, resendLen, 0, (struct sockaddr *)server, serverAddrLen);
		}

		(*counter)++; 
		return WAIT_ON_ACK;
	}

	else if (*counter >= 10) {
		//assume the other side closed after 10 tries
		return DONE; 
	}

	else {
		//process RR/SREJ
		int returnVal = processRRorSREJ(socketNum, server, window_size, window_buffer);
	
		//only continue sending data if lower is advanced
		if (returnVal == 1) {
			*counter = 0; 

			//check if it's time to wait on EOF
			int lower = getLowestPacket(); 
			if ((*eof == 1) && (lower == *eofSeqNum)) {
				return WAIT_ON_EOF_ACK;
			}

			return SEND_DATA;
		}
		
		else if (returnVal == 0) {
			//check if it's time to wait on EOF
			int lower = getLowestPacket();  
			if ((*eof == 1) && (lower == *eofSeqNum)) {
				return WAIT_ON_EOF_ACK;
			}

			return WAIT_ON_ACK;
		}

		else {
			return WAIT_ON_ACK;
		}
	}
}

STATE waitOnEOFAck(int socketNum, struct sockaddr_in6 *server, int sequenceNum, int window_size, struct SR_buffer *window_buffer) { 
	/**
	 * Wait up to 10 seconds to receive the EOF acknowledgment
	*/

	int counter = 0; 
	int serverAddrLen = sizeof(struct sockaddr_in6);

	//send last PDU
	sendEOFPacket(socketNum, server, sequenceNum);

	while (counter < 10) {
		//poll for one second 
		int pollResult = pollCall(1000);

		if (pollResult < 0) {
			// resending EOF ack on timeout for up to 10 times
			sendEOFPacket(socketNum, server, sequenceNum);

			counter++; 
			continue;
		}

		else {
			//received a packet
			uint8_t EOF_buffer[MAXBUF];
			int bytes_recv = safeRecvfrom(socketNum, EOF_buffer, MAXBUF, 0, (struct sockaddr *)server, &serverAddrLen);

			if (bytes_recv < 0) {
				continue; 
			}

			//check checksum for any data corruption
			unsigned short checksum = in_cksum((unsigned short *)EOF_buffer, bytes_recv);
			if (checksum != 0) {
				printf("checksum failed waiting on EOF ack\n");
				continue; 
			}

			//only finish when EOF ack is received
			uint8_t flag = EOF_buffer[6]; 
			if (flag == EOF_ACK) {
				printf("file transfer complete\n");
				return DONE; 
			}

			else if (flag == RR_PACKET) {
				processRRpacket(EOF_buffer, window_buffer, window_size);
				sendEOFPacket(socketNum, server, sequenceNum);
				continue;
			}
			
			else if (flag == SREJ_PACKET) {
				processSREJpacket(EOF_buffer, socketNum, server, window_size, window_buffer);
				sendEOFPacket(socketNum, server, sequenceNum);
				continue;
			}
		}
	}

	printf("I give up\n");
	return DONE;
}

void sendEOFPacket(int socketNum, struct sockaddr_in6 *server, int sequenceNum) {
	/**
	 * Send the last packet with EOF_FLAG to indicate file transfer is complete 
	*/
	
	int flag = EOF_FLAG; 
	int serverAddrLen = sizeof(struct sockaddr_in6); 

	uint8_t PDU[MAXBUF];
	uint8_t payload[1]; 
	payload[0] = 0; 

	//format PDU
	int PDU_len = createPDU(PDU, sequenceNum, flag, payload, 1);

	//send PDU
	safeSendto(socketNum, PDU, PDU_len, 0, (struct sockaddr *)server, serverAddrLen);
}

int processRRorSREJ(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer) {
	/**
	 * Process either a RR or SREJ packet 
	*/
	int serverAddrLen = sizeof(struct sockaddr_in6);

	//receive the packet
	uint8_t received_PDU[MAXBUF];
	int bytes_recv = safeRecvfrom(socketNum, received_PDU, MAXBUF, 0, (struct sockaddr *)server, &serverAddrLen);
	if (bytes_recv < 0) {
		return -1; 
	}

	//check data hasn't been changed after transfer
	unsigned short checksum = in_cksum((unsigned short *)received_PDU, bytes_recv);
	if (checksum != 0) {
		fprintf(stderr, "checksum failed\n");
		return -1; 
	}

	//parse data
	int flag = received_PDU[6];
	if (flag == RR_PACKET) {
		int b_lowest = getLowestPacket();

		processRRpacket(received_PDU, window_buffer, window_size);

		int a_lowest = getLowestPacket(); 

		//if lowest incremented, return 1
		if (a_lowest > b_lowest) {
			return 1; 
		}
		else {
			return 0; 
		}
	}
	else if (flag == SREJ_PACKET) {
		processSREJpacket(received_PDU, socketNum, server, window_size, window_buffer);

		return 0; 
	}

	return 0; 
}


int checkArgs(int argc, char * argv[])
{
    int portNumber = 0;
	
    /* check command line arguments  */
	
	if (argc != 8)
	{
		printf("usage: %s [fromFile] [toFile] [window_size] [buffer_size] [error_rate] [hostname] [port] \n", argv[0]);
		exit(1);
	}

	if (strlen(argv[1]) > 1000) {
		printf("FROM filename is too long, must be < 1000 charactesr\n");
		exit(-1);
	}

	if (strlen(argv[2]) > 1000) {
		printf("TO filename is too long, must be < 1000 characters\n");
		exit(-1); 
	}

	if (atoi(argv[4]) < 400 || atoi(argv[4]) > 1400) {
		printf("buffer size must be between 400 and 1400 bytes\n");
		exit(-1); 
	}

	if (atoi(argv[5]) < 0 || atoi(argv[5]) > 1) {
		printf("error rate must be between 0 and 1\n");
		exit(-1); 
	}

	portNumber = atoi(argv[7]);
		
	return portNumber;
}





