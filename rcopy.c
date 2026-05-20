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
#include "buffering.h"

#define MAXBUF 1407
#define MAXPAYLOAD 1400

typedef enum State STATE;

enum State {
	SETUP_STATE, FILENAME, FILE_OK, SEND_DATA, WAIT_ON_ACK, DONE
};

enum FLAG {
	SETUP_PACKET=1, SETUP_RESPONSE=2, DATA_PKT=3, RR_PACKET=5, SREJ_PACKET=6, RCOPY_TO_SERVER=7, SERVER_TO_RCOPY=8
};

void processFile(char *argv[], int socketNum, int portNumber, struct sockaddr_in6 *server);
STATE sendSetupPacket(char *argv[], int socketNum, struct sockaddr_in6 *server);
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

	STATE state = FILENAME; 

	while (state != DONE) {
		switch (state) {
			// case SETUP_STATE:
			// 	state = start_state(argv, portNumber, &server);
			// 	break;
			case FILENAME: 
				state = sendSetupPacket(argv, socketNum, server);
				break; 
			case SEND_DATA: 
				state = dataTransferSelRepeat(socketNum, server);
				break;
		}
	}
}

STATE dataTransferSelRepeat(int socketNum, struct sockaddr_in6 *server) {
	
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


// STATE setup_state(char *argv[], int portNumber, struct sockaddr_in6 server) {
// 	struct sockaddr_in6 server; 
// 	char *hostname = argv[6];
// 	int socketNum = 0;
// 	STATE returnState = FILENAME; 

// 	socketNum = setupUdpClientToServer(&server, hostname, portNumber);

// 	if (socketNum < 0) {
// 		//could not connect to server
// 		returnState = DONE; 
// 	}

// 	return returnState; 
	
// }







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





