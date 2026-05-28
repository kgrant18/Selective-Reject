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

#define MAXBUF 1407
#define MAXPAYLOAD 1400

typedef enum State STATE;

enum State {
	FILENAME, FILE_OK, SEND_DATA, WAIT_ON_ACK, DONE
};

// enum FLAG {
// 	SETUP_PACKET=1, SETUP_RESPONSE=2, DATA_PKT=3, RR_PACKET=5, SREJ_PACKET=6, RCOPY_TO_SERVER=7, SERVER_TO_RCOPY=8
// };

void processFile(char *argv[], int socketNum, int portNumber, struct sockaddr_in6 *server);
STATE sendSetupPacket(char *argv[], int socketNum, struct sockaddr_in6 *server);
STATE dataTransfer(int socketNum, struct sockaddr_in6 *server, int from_file_fd, int window_size, int buffer_size, struct SR_buffer *window_buffer, int *eof);
STATE waitOnAck(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer);
int processRRorSREJ(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer);
void talkToServer(int socketNum, struct sockaddr_in6 * server);
int readFromStdin(char * buffer);
int checkArgs(int argc, char * argv[]);

int main (int argc, char *argv[])
 {
	int socketNum = 0;				
	struct sockaddr_in6 server;		// Supports 4 and 6 but requires IPv6 struct
	int portNumber = 0;
	
	portNumber = checkArgs(argc, argv);
	char *hostname = argv[6];
	
	socketNum = setupUdpClientToServer(&server, hostname, portNumber);
	
	processFile(argv, socketNum, portNumber, &server);
	
	close(socketNum);

	return 0;
}

void processFile(char *argv[], int socketNum, int portNumber, struct sockaddr_in6 *server) {

	STATE NS = FILENAME; 

	char *from_file = argv[1];
	//if file exists, open
	int from_file_fd = open(from_file, O_RDONLY);
	if (from_file_fd == -1) {
		perror("failed to open file");
		exit(-1);
	}

	int window_size = atoi(argv[3]);
	int buffer_size = atoi(argv[4]);
	struct SR_buffer *window_buffer = initializeBuffer(window_size); 
	int eof = 0; 

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
				NS = dataTransfer(socketNum, server, from_file_fd, window_size, buffer_size, window_buffer, &eof);
				break;
			}

			case WAIT_ON_ACK: {
				NS = waitOnAck(socketNum, server, window_size, window_buffer);
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

STATE dataTransfer(int socketNum, struct sockaddr_in6 *server, int from_file_fd, int window_size, int buffer_size, struct SR_buffer *window_buffer, int *eof) {
	int serverAddrLen = sizeof(struct sockaddr_in6); 

	int nextSeqNum = getSeqNum() + 1; 
	while ((*eof == 0) && (checkWindowOpen(nextSeqNum, window_size) == 1)) {

		//read data from disk
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
			return WAIT_ON_ACK;
        }

		//increment API sequence number
		int cur_seqNum = getNextSeqNum(); 
		//increment my tracker
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
			processRRorSREJ(socketNum, server, window_size, window_buffer);
		}
	}

	return WAIT_ON_ACK;
}

STATE waitOnAck(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer) {
	/**
	 * Window is closed and need to wait for acknowledgements before proceeding 
	 */
	
	int serverAddrLen = sizeof(struct sockaddr_in6);

	int pollResult = pollCall(1000); 

	if (pollResult < 0) {
		perror("pollCall failed");
		return DONE;
	}

	if (pollResult == 0) {
		//timeout = resend lowest packet in window
		uint8_t resendPDU[MAXBUF];
		int lower = getLowestPacket();

		int resendLen = getPacketToResend(resendPDU, lower, window_size, window_buffer);
		if (resendLen > 0) {
			safeSendto(socketNum, resendPDU, resendLen, 0, (struct sockaddr *)server, serverAddrLen);
		}

		return WAIT_ON_ACK;
	}

	else {
		//process RR/SREJ
		int returnVal = processRRorSREJ(socketNum, server, window_size, window_buffer);
		if (returnVal == 0) {
			return SEND_DATA;
		}
		else {
			return DONE;
		}
	}
}

int processRRorSREJ(int socketNum, struct sockaddr_in6 *server, int window_size, struct SR_buffer *window_buffer) {
	//process RR or SREJ
	int serverAddrLen = sizeof(struct sockaddr_in6);

	uint8_t received_PDU[MAXBUF];
	int bytes_recv = safeRecvfrom(socketNum, received_PDU, MAXBUF, 0, (struct sockaddr *)server, &serverAddrLen);

	//check data hasn't been changed after transfer
	unsigned short checksum = in_cksum((unsigned short *)received_PDU, bytes_recv);
	if (checksum != 0) {
		fprintf(stderr, "checksum failed\n");
		return -1; 
	}

	//parse data
	int flag = received_PDU[6];
	if (flag == RR_PACKET) {
		processRRpacket(received_PDU, window_buffer, window_size);
	}
	else if (flag == SREJ_PACKET) {
		processSREJpacket(received_PDU, socketNum, server, window_size, window_buffer);
	}

	return 0; 
}

STATE sendSetupPacket(char *argv[], int socketNum, struct sockaddr_in6 *server) {
	/**
	 * send setup packet to server and wait for response
	*/

	setupPollSet();
	addToPollSet(socketNum); 
	int serverAddrLen = sizeof(struct sockaddr_in6); 
	int counter = 0; 

	//first packet sent will have seqNum = 0
	int sequenceNumber = 0; 

	//parse parameters to be sent
	char *to_filename = argv[2]; 
	uint16_t window_size_no = htons(atoi(argv[3]));
	uint16_t buffer_size_no = htons(atoi(argv[4]));

	//format payload with window size, buffer size, and filename
	uint8_t payload[MAXPAYLOAD];
	memcpy(payload, &window_size_no, 2);
	memcpy(payload + 2, &buffer_size_no, 2);
	memcpy(payload + 4, to_filename, strlen(to_filename) + 1);

	//create PDU to send to server
	uint8_t PDUbuffer[MAXBUF];
	int payloadLen = 2 + 2 + strlen(to_filename) + 1; 
	int pduLen = createPDU(PDUbuffer, sequenceNumber, RCOPY_TO_SERVER, payload, payloadLen);

	while (counter < 10) {
		safeSendto(socketNum, PDUbuffer, pduLen, 0, (struct sockaddr *)server, serverAddrLen);

		int pollSocket = pollCall(1000); 

		if (pollSocket < 0) {
			perror("poll call failed");
			return DONE; 
		}
		
		else if (pollSocket == 0) {
			//timeout... resend filename packet
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

	//never makes it, terminate
	return DONE; 

}








void talkToServer(int socketNum, struct sockaddr_in6 * server)
{
	int serverAddrLen = sizeof(struct sockaddr_in6);
	char * ipString = NULL;
	int dataLen = 0; 
	char buffer[MAXBUF+1];
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = readFromStdin(buffer);

		printf("Sending: %s with len: %d\n", buffer,dataLen);
	
		safeSendto(socketNum, buffer, dataLen, 0, (struct sockaddr *) server, serverAddrLen);
		
		safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) server, &serverAddrLen);
		
		// print out bytes received
		ipString = ipAddressToString(server);
		printf("Server with ip: %s and port %d said it received %s\n", ipString, ntohs(server->sin6_port), buffer);
	      
	}
}

int readFromStdin(char * buffer)
{
	char aChar = 0;
	int inputLen = 0;        
	
	// Important you don't input more characters than you have space 
	buffer[0] = '\0';
	printf("Enter data: ");
	while (inputLen < (MAXBUF - 1) && aChar != '\n')
	{
		aChar = getchar();
		if (aChar != '\n')
		{
			buffer[inputLen] = aChar;
			inputLen++;
		}
	}
	
	// Null terminate the string
	buffer[inputLen] = '\0';
	inputLen++;
	
	return inputLen;
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





