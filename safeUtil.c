
// 
// Writen by Hugh Smith, April 2020, Feb. 2021, May 2026
//
// Put in system calls with error checking
// keep the function paramaters same as system call

// Supports sendtoErr library if using __LIBCPE464_ in compile line

#include <stdlib.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>

#include "safeUtil.h"

// For using the sendtoErr library
// if __LIBCPE464_ exists (see Makefile) then use sendtoErr/recvfromErr
// Make sure the Makefile has __LIBCPE464 uncommented
#ifndef __LIBCPE464_
#define __RECV_FROM recvfrom
#define __SEND_TO sendto
#else
#include "cpe464.h"
#define __RECV_FROM recvfromErr
#define __SEND_TO sendtoErr
#endif

int safeRecvfrom(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int *addrLen)
{
	int returnValue = 0;
	if ((returnValue = __RECV_FROM(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t *) addrLen)) < 0)
	{
		perror("recvfrom: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSendto(int socketNum, void * buf, int len, int flags, struct sockaddr *srcAddr, int addrLen)
{
	int returnValue = 0;
	if ((returnValue = __SEND_TO(socketNum, buf, (size_t) len, flags, srcAddr, (socklen_t) addrLen)) < 0)
	{
		perror("sendto: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeRecv(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = recv(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("recv: ");
		exit(-1);
	}
	
	return returnValue;
}

int safeSend(int socketNum, void * buf, int len, int flags)
{
	int returnValue = 0;
	if ((returnValue = send(socketNum, buf, (size_t) len, flags)) < 0)
	{
		perror("send: ");
		exit(-1);
	}
	
	return returnValue;
}

void * srealloc(void *ptr, size_t size)
{
	void * returnValue = NULL;
	
	if ((returnValue = realloc(ptr, size)) == NULL)
	{
		printf("Error on realloc (tried for size: %d\n", (int) size);
		exit(-1);
	}
	
	return returnValue;
} 

void * sCalloc(size_t nmemb, size_t size)
{
	void * returnValue = NULL;
	if ((returnValue = calloc(nmemb, size)) == NULL)
	{
		perror("calloc");
		exit(-1);
	}
	return returnValue;
}

