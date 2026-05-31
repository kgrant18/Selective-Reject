/* Server side - UDP Code				    */
/* By Hugh Smith	4/1/2017	*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "gethostbyname.h"
#include "networks.h"
#include "safeUtil.h"
#include "checksum.h"
#include "windowing.h"
#include "bufferMngment.h"
#include "cpe464.h"
#include "pollLib.h"

#define MAXBUF 1407


typedef enum State STATE; 
enum State {
	ACCEPT_FILENAME, MANAGE_INCOMING_DATA, DONE
};

void processServer(int serverSocketNum);
void processFile(uint8_t *init_PDU, int init_PDU_len, struct sockaddr_in6 *client);
STATE manage_incoming_data(int childSocketNum, struct bufferInfo *buffer_struct);
STATE receive_initial_PDU(int childSocketNum, uint8_t *PDUbuffer, int init_PDU_len, struct sockaddr_in6 *client, struct bufferInfo *buffer_struct);
int checkArgs(int argc, char *argv[]);
void sendErrorPacket(int serverSocketNum, struct sockaddr_in6 *client, char *payload);
void handleZombies(int sig);

int main(int argc, char *argv[])
{ 
	int serverSocketNum = 0;				
	int portNumber = 0;

	portNumber = checkArgs(argc, argv);
	
	double err_rate = atof(argv[1]);
	sendErr_init(err_rate, DROP_ON, FLIP_ON, DEBUG_ON, RSEED_OFF);
		
	serverSocketNum = udpServerSetup(portNumber);

	processServer(serverSocketNum);

	close(serverSocketNum);
	
	return 0;
}

void processServer(int serverSocketNum) {
	/**
	 * Fork a new child for every client (multiprocessing)
	*/
	
	uint8_t buffer[MAXBUF];
	struct sockaddr_in6 client;		
	
	//cleanup signal handler 
	signal(SIGCHLD, handleZombies);
	
	while (1) {
		int clientAddrLen = sizeof(struct sockaddr_in6);

		//block waiting for a new client
		int recv_bytes = safeRecvfrom(serverSocketNum, buffer, MAXBUF, 0, (struct sockaddr *)&client, &clientAddrLen);
		if (recv_bytes < 0) {
			perror("safeRecvfrom() failed");
			continue; 
		}

		pid_t pid = fork(); 
		if (pid < 0) {
			perror("fork failed");
			exit(-1); 
		}

		else if (pid == 0) {
			//child process = new process for each client
			printf("Child fork() - child pid: %d\n", getpid());
			processFile(buffer, recv_bytes, &client);
			exit(0);  
		}
	}
}

void processFile(uint8_t *first_PDU, int first_PDU_len, struct sockaddr_in6 *client) {
	/**
	 * The logic control and state machine for the server (receiving side) 
	*/

	//default to accept_filename state
	STATE NS = ACCEPT_FILENAME; 

	//buffer_struct keeps track of the information that needs to be passed between states
	struct bufferInfo buffer_struct; 

	//create a new UDP socket for the new client
	int socketNum = createUdpSocket(); 

	while (NS != DONE) {
		switch (NS) {
			case ACCEPT_FILENAME: { 
				//receive the very first PDU sent from rcopy
				NS = receive_initial_PDU(socketNum, first_PDU, first_PDU_len, client, &buffer_struct);
				break;
			}

			case MANAGE_INCOMING_DATA: {
				NS = manage_incoming_data(socketNum, &buffer_struct); 
				break;
			}

			case DONE: {
				//file transfer is complete
				NS = DONE;
				break;
			}

			default: {
				NS = DONE; 
				break;
			}
		}
	}
}

STATE receive_initial_PDU(int childSocketNum, uint8_t *PDUbuffer, int PDU_len, struct sockaddr_in6 *client, struct bufferInfo *buffer_struct) {
	/**
	 * Receive the very first PDU that contains the window size, buffer size, and from-filename
	*/

	int clientAddrLen = sizeof(struct sockaddr_in6);

	printf("Received message from client with ");
	printIPInfo(client);

	//verify checksum to ensure data hasn't been changed
	unsigned short checksum = in_cksum((unsigned short *)PDUbuffer, PDU_len);
	if (checksum != 0) {
		printf("Checksum failed on first PDU sent\n");
		return DONE; 
	}

	//read flag and confirm it's correct
	int flag = PDUbuffer[6];
	if (flag != RCOPY_TO_SERVER) {
		printf("Incorrect flag received on first PDU sent\n");
		return DONE; 
	}

	//parse payload
	uint8_t *payload = PDUbuffer + 7; 
	
	//extract window and buffer sizes
	uint16_t window_size_no = 0;
	uint16_t buffer_size_no = 0;
	memcpy(&window_size_no, payload, 2); 
	memcpy(&buffer_size_no, payload + 2, 2); 

	uint16_t window_size = ntohs(window_size_no);
	uint16_t buffer_size = ntohs(buffer_size_no);

	//populate structure so that it can be used later on
	buffer_struct->window_size = window_size;
	buffer_struct->buffer_size = buffer_size;
	buffer_struct->client = *client;

	//get filename
	char *filename = (char *)(payload + 4);
	
	//try to open output file
	int file_fd = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0600);
	if (file_fd < 0) {
		printf("failed to open file\n");

		//send error packet to rcopy if cannot open file
		char payload[1024];
		sprintf(payload, "Error on open of output file: %s", filename);
		sendErrorPacket(childSocketNum, client, payload);
	}

	else {
		//server responds to rcopy to acknowledge file has been created
		buffer_struct->file_fd = file_fd; 

		uint8_t serverResponse[MAXBUF];
		int sequenceNumber = 0; 

		//payload doesn't matter here, only the flag is important
		uint8_t pyload[1]; 
		pyload[0] = 0; 

		int pduLen = createPDU(serverResponse, sequenceNumber, SERVER_TO_RCOPY, pyload, 1);

		safeSendto(childSocketNum, serverResponse, pduLen, 0, (struct sockaddr *)client, clientAddrLen);
	}

	//go to next state after acknowledging
	return MANAGE_INCOMING_DATA;
}

STATE manage_incoming_data(int childSocketNum, struct bufferInfo *buffer_struct) {
	/**
	 * This function handles receving the data and writing it to the file
	*/

	//need to keep track of another struct so that state, expected, and buffer persist across multiple sent packets
	struct serverStateData tracker;

	//default values and buffer initialization 
	tracker.state = INORDER;
	tracker.expectedSeqNum = 1; 
	tracker.eofSeqNum = -1; 
	tracker.buffer = initializeBuffer(buffer_struct->window_size);

	//add socketNum to pollSet!! finally
	setupPollSet();
	addToPollSet(childSocketNum);
	
	while (1) { 
		uint8_t PDU[MAXBUF];

		struct sockaddr_in6 client; 
		int clientAddrLen = sizeof(struct sockaddr_in6);

		//block until server receives a packet
		int recv_bytes = safeRecvfrom(childSocketNum, PDU, MAXBUF, 0, (struct sockaddr *)&client, &clientAddrLen);
		if (recv_bytes < 0) {
			perror("safeRecvfrom failed");
			continue; 
		}

		//check if data has been changed
		unsigned short checksum = in_cksum((unsigned short *)PDU, recv_bytes);
		if (checksum != 0) {
			printf("Bad checksum\n");
			sendSREJ(childSocketNum, tracker.expectedSeqNum, buffer_struct);
			continue; 
		}

		//get sequence number
		uint32_t seqNum_no = 0;
		memcpy(&seqNum_no, PDU, 4);

		//data needed for managing the buffer
		uint32_t seqNum = ntohl(seqNum_no);
		uint8_t *payload = PDU + 7; 
		int payloadLen = recv_bytes - 7; 

		//check if EOF flag was sent
		uint8_t flag = PDU[6]; 
		if (flag == EOF_FLAG) {
			//set seqNum of last packet sent
			tracker.eofSeqNum = seqNum; 
		}
		
		//if not a data flag, don't proceed
		else if (flag != DATA_PKT) {
			continue; 
		}

		//call buffer management function
		bufferManagement(childSocketNum, seqNum, flag, payload, payloadLen, buffer_struct, &tracker); 
		if (tracker.state == BUFFER_DONE) {
			break;
		}
	}

	free(tracker.buffer);
	
	return DONE; 
}

void sendErrorPacket(int serverSocketNum, struct sockaddr_in6 *client, char *payload) {
	/**
	 * Function to send an error packet back to rcopy
	 */
	
	int clientAddrLen = sizeof(struct sockaddr_in6);

	uint8_t PDU_buffer[MAXBUF];

	int pduLen = createPDU(PDU_buffer, 0, ERR_FLAG, (uint8_t *)payload, strlen(payload) + 1);

	safeSendto(serverSocketNum, PDU_buffer, pduLen, 0, (struct sockaddr *)&client, clientAddrLen);
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


