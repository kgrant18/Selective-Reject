/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "checksum.h"

#define MAXBUF 1407


typedef enum State STATE; 
enum State {
	ACCEPT_FILENAME, FILE_OK, READ_DISK, DONE
};

enum FLAG {
	SETUP_PACKET=1, SETUP_RESPONSE=2, DATA_PKT=3, RR_PACKET=5, SREJ_PACKET=6, RCOPY_TO_SERVER=7, SERVER_TO_RCOPY=8
};

void processFile(int socketNum);
int checkArgs(int argc, char *argv[]);
STATE receive_initial_PDU(int serverSocketNum);

int main (int argc, char *argv[])
{ 
	int serverSocketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
		
	serverSocketNum = udpServerSetup(portNumber);

	processFile(serverSocketNum);

	close(serverSocketNum);
	
	return 0;
}

void processFile(int serverSocketNum) {
	STATE state = ACCEPT_FILENAME;

	while (state != DONE) {
		switch (state) {
			case ACCEPT_FILENAME: 
				receive_initial_PDU(serverSocketNum);


		}
	}


}

STATE receive_initial_PDU(int serverSocketNum) {
	int recv_bytes = 0;
	uint8_t PDUbuffer[MAXBUF];
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
	
	
	recv_bytes = safeRecvfrom(serverSocketNum, PDUbuffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);

	printf("Received message from client with ");
	printIPInfo(&client);
	//printPDU(buffer, recv_bytes);

	//verify checksum
	unsigned short checksum = in_cksum((unsigned short *)PDUbuffer, recv_bytes);
	if (checksum != 0) {
		printf("failed checksum!\n");
		return DONE; 
	}

	//read flag
	int flag = PDUbuffer[6];
	if (flag != RCOPY_TO_SERVER) {
		printf("wrong flag!\n");
		return DONE; 
	}

	//parse payload
	uint8_t *payload = PDUbuffer + 7; 
	
	uint16_t window_size_no = 0;
	uint16_t buffer_size_no = 0;
	memcpy(&window_size_no, payload, 2); 
	memcpy(&buffer_size_no, payload + 2, 2); 

	uint16_t window_size = ntohs(window_size_no);
	uint16_t buffer_size = ntohs(buffer_size_no);
	
	char *filename = (char *)(payload + 4);


	//try to open output file
	int file_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	if (file_fd < 0) {
		printf("failed to open file\n");
		return DONE; 
	}
	else {
		//server responds to rcopy to acknowledge file has been created
		uint8_t serverBuffer[MAXBUF];
		int sequenceNumber = 1; 

		//payload doesn't matter here
		uint8_t payloadResponse[1]; 
		payloadResponse[0] = 0; 

		int pduLen = createPDU(serverBuffer, sequenceNumber, SERVER_TO_RCOPY, payloadResponse, 1);

		safeSendto(serverSocketNum, serverBuffer, pduLen, 0, (struct sockaddr *)&client, clientAddrLen);


	}

}


void processClient(int socketNum)
{
	int dataLen = 0; 
	char buffer[MAXBUF + 1];	  
	struct sockaddr_in6 client;		
	int clientAddrLen = sizeof(client);	
	
	buffer[0] = '\0';
	while (buffer[0] != '.')
	{
		dataLen = safeRecvfrom(socketNum, buffer, MAXBUF, 0, (struct sockaddr *) &client, &clientAddrLen);
	
		printf("Received message from client with ");
		printIPInfo(&client);
		printf(" Len: %d \'%s\'\n", dataLen, buffer);

		// just for fun send back to client number of bytes received
		sprintf(buffer, "bytes: %d", dataLen);
		safeSendto(socketNum, buffer, strlen(buffer)+1, 0, (struct sockaddr *) & client, clientAddrLen);

	}
}

int checkArgs(int argc, char *argv[])
{
	// Checks args and returns port number
	int portNumber = 0;
	
	if (argc < 2 || argc > 3)
	{
		fprintf(stderr, "Usage %s [error-rate] [optional-port-number]\n", argv[0]);
		exit(-1);
	}
	
	if (argc == 3)
	{
		portNumber = atoi(argv[2]);
	}
	
	else {
		portNumber = 0; 
	} 	

	return portNumber;
}

void handleZombies(int sig) {
	int stat = 0; 
	while (waitpid(-1, &stat, WNOHANG) > 0) {}
}


