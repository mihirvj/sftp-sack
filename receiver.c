/************************************************************
*			     receiver.c
*		Simple FTP Server Implementation
*	      Authors: Mihir Joshi, Fenil Kavathia
*			    csc573 NCSU
************************************************************/

#include "sock/ssock.h"
#include "config.h"
#include "fops/fileop.h"
#include<stdbool.h>
#include<assert.h>

#define SERVER_PORT 7735
#define SLIDE_WIN() RF = (RF + 1) % WINSIZE;\
		RN = (RN + 1) % WINSIZE

#define IS_IN_WINDOW(seq) (seq >= RF * MSS && seq <= RN * MSS)

uint RF, RN; // receiver window variables
uint VRF; // virtual receiver window head pointer

char *buffer;

bool marked[WINSIZE * MSS]; // this data structure directly maps to the sequence number. marked[429] is for seq no: 429
bool nakSent = false;
bool ackRequired = false;

int isValid(uchar segment[MSS]);

void removeHeader(uchar segment[MSS])
{
	int i;

	for(i=0; i<MSS - HEADSIZE; i++)
		segment[i] = segment[i + HEADSIZE];

	segment[i] = '\0';
}

void storeSegment(uchar segment[MSS])
{
	int i;
	uint bufSize = WINSIZE * MSS * 2;

	if(VRF > RN)
	{
#ifdef APP
	printf("[log] VRF reached RN. No window space available to store out of order segments..\n");
#endif
		return;
	}

#ifdef APP
	printf("[log] storing segment at location starting %d: ", (VRF * MSS) % bufSize);

#endif

	for(i=0;i<MSS-HEADSIZE;i++)
	{
		buffer[((VRF * MSS) % bufSize) + i] = segment[i];
#ifdef APP
	printf("%c(%d), ", buffer[((VRF * MSS) % bufSize) + i], (int) buffer[((VRF * MSS) % bufSize) + i]);
#endif
	}

#ifdef APP
	printf("\n");
#endif

	VRF = (VRF + 1) % (WINSIZE);

#ifdef APP
	printf("[log] VRF: %d\n", VRF);
#endif
}

void sendNak(int sock, char senderIP[50], int senderPort)
{
	int i;
	uint nakNo = (RF * MSS);
	uchar segment[HEADSIZE];

	segment[3] = nakNo & 0xFF;
	segment[2] = (nakNo >> 8) & 0xFF;
	segment[1] = (nakNo >> 16) & 0xFF;
	segment[0] = -1;

#ifdef APP
	printf("[log] nak sent for %d:\n", nakNo);

	for(i=0;i<HEADSIZE;i++)
		printf("%d, ", (int) segment[i]);

	printf("\n");
#endif

	write_to(sock, segment, HEADSIZE, senderIP, senderPort);
}

void sendAck(int sock, uint prev, char senderIP[50], int senderPort)
{
	int i;
	uint ackNo = prev;
	uchar segment[HEADSIZE];

	segment[3] = ackNo & 0xFF;
	segment[2] = (ackNo >> 8) & 0xFF;
	segment[1] = (ackNo >> 16) & 0xFF;
	segment[0] = (ackNo >> 24) & 0xFF;
#ifdef APP
	printf("[log] ack sent for %d:\n", ackNo);

	for(i=0;i<HEADSIZE;i++)
		printf("%d, ", (int) segment[i]);

	printf("\n");
#endif

	write_to(sock, segment, HEADSIZE, senderIP, senderPort);
}

void writeToFile(int file, uchar segment[MSS], int buf_len)
{
	int i, validCount = 0;

	for(i=0; i<buf_len; i++)
	{
		if((int) segment[i] != 0)
			validCount++;
		else
			break;
	}

#ifdef APP
	printf("[log] writing to file %d bytes\n", validCount);
#endif

#ifdef APP
	printf("[log] bytes stored in file: ");

	for(i=0; i < validCount; i++)
		printf("%c(%d), ", segment[i], (int) segment[i]);

	printf("\n");
#endif

	output_to(file, segment, validCount);
}

uint extractSeqNo(uchar segment[MSS])
{
	uint seqNo = 0;

	seqNo = (uint) segment[0];
	seqNo = seqNo << 24;

	seqNo = seqNo + (((uint) segment[1]) << 16);
	seqNo = seqNo + (((uint) segment[2]) << 8);
	seqNo = seqNo + ((uint) segment[3]);

	return seqNo;
}

int main()
{
	int sock, in_port;
	uchar request[MSS], req_from[50], segment[MSS];
	struct sockaddr_in clientCon;
	char ack[HEADSIZE];
	int file, i, j, bytesRead, packetCount = 0;
	uint recvSeq;

/*
	|_(|_|_|_|_|_|_|)_|
	  RF           RN
*/

	RF = 0;
	RN = RF + WINSIZE;
	VRF = RF;

	buffer = (char *) malloc(WINSIZE * MSS * 2);

	sock = get_sock();

	file = get_file_descriptor("test/received.txt", Create);

	bind_sock(sock, SERVER_PORT);

	listen_sock(sock);

	for(i=0;i<WINSIZE * MSS;i++)
		marked[i] = false;

	while(1) // listen continuosly
	{
		while((RN - RF) > WINSIZE)
		{
#ifdef APP
	printf("[log] buffer full.. waiting\n");
#endif
			sleep(2);
		}

		bytesRead = read_from(sock, request, MSS, &clientCon);

		if(strcmp(request, "<FINMJ>") == 0)
		{
#ifdef APP
	printf("<FINMJ> received\n");
#endif
			break;
		}

		sprintf(req_from, "%d.%d.%d.%d", (int)(clientCon.sin_addr.s_addr&0xFF),
    					(int)((clientCon.sin_addr.s_addr&0xFF00)>>8),
    					(int)((clientCon.sin_addr.s_addr&0xFF0000)>>16),
    					(int)((clientCon.sin_addr.s_addr&0xFF000000)>>24));

		in_port = ntohs(clientCon.sin_port);

#ifdef APP
	printf("[log]\nGot request from: %s\nPort: %d\n", req_from, in_port);

	printf("request: ");

	for(i=0;i<4;i++)
		printf("%d, ", (int) request[i]);

	for(i=4; i<MSS; i++)
		printf("%c(%d), ", request[i], (int) request[i]);

	printf("\n[/log]\n");
#endif

		recvSeq = extractSeqNo(request);

		if(recvSeq > RF * MSS && !nakSent)
		{

			sendNak(sock, req_from, in_port);

			nakSent = true;
		}
		else if(nakSent && IS_IN_WINDOW(recvSeq) && !marked[recvSeq]) // if received frame is in window range, then buffer it
		{
			removeHeader(request);

			storeSegment(request);

			marked[recvSeq] = true;
#ifdef APP
	printf("[log] marking %d\n", recvSeq);
#endif
		}
		else if(recvSeq == RF * MSS) // correct segment. yay!!!
		{
			int prev;

#ifdef APP
	printf("[log] valid segment found for seq no: %d\n", RF * MSS);
#endif
#ifdef DROP
	if(packetCount % 3 == 0 && packetCount != 0)
	{
		printf("[drop log]\n-------------- dropping packet: %d at seq no: %d\n---------------\n", packetCount, recvSeq);
		packetCount++;
		continue;
	}
	else
	{
		sleep(2);
	}
#endif	
			removeHeader(request);

			storeSegment(request);

			marked[recvSeq] = true;

#ifdef APP
	printf("[log] marking %d\n", recvSeq);
#endif

			for(i= RF * MSS; marked[i] == true || RF < VRF ; i = (i + MSS - HEADSIZE) % (WINSIZE * MSS))
			{
#ifdef APP
	printf("[log] extracting from buffer at location %d:\n", i);
#endif
				for(j = 0; j < MSS - HEADSIZE; j++)
				{
					segment[j] = buffer[i + j];
				}
#ifdef APP
	printf("[log] extracted segment from buffer: ");

	for(j=0; j < MSS - HEADSIZE; j++)
		printf("%c(%d), ", segment[j], (int)segment[j]);

	printf("\n");
#endif
				writeToFile(file, segment, MSS - HEADSIZE);

				SLIDE_WIN();

				marked[i] = false;
#ifdef APP
	printf("[log] marking %d false\n", recvSeq);
#endif
				prev = i; // prev points to previous frame to be acknowledged. This is for cumulative ack
			}

			nakSent = false;

			sendAck(sock, prev, req_from, in_port); // send cumulative ack
			assert(RF == VRF);
		}
		else
		{
#ifdef APP
	printf("[log] discarding packet\n");
#endif

			// take no action. sliently discard
		}

		packetCount++;
	}
	
	close_sock(sock);
	close(file);

	return 0;
}

int isValid(uchar segment[MSS])
{
	uint seqNo = 0;

	seqNo = extractSeqNo(segment);

	/*seqNo = (uint) segment[0];
	seqNo = seqNo << 24;

	seqNo = seqNo + (((uint) segment[1]) << 16);
	seqNo = seqNo + (((uint) segment[2]) << 8);
	seqNo = seqNo + ((uint) segment[3]);*/

#ifdef APP
	printf("[log]received sequence number: %d\nexpected sequence number: %d\n[/log]\n", seqNo, RN * MSS);
#endif

	return seqNo >= (RF * MSS) && seqNo <= (RN * MSS);
}
