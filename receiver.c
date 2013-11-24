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

//#define SERVER_PORT 65413
#define SLIDE_WIN() RF = (RF + 1);\
		RN = (RN + 1)

#define IS_IN_WINDOW(seq) (seq > RF * MSS)
#define INFINITE_BUFFER 1024 * 1024

uint RF, RN; // receiver window variables
uint VRF; // virtual receiver window head pointer

char *buffer;

bool *marked; // this data structure directly maps to the sequence number. marked[429] is for seq no: 429
bool nakSent = false;
bool ackRequired = false;

int isValid(uchar *segment);

void removeHeader(uchar *segment)
{
	int i;

	for(i=0; i<MSS - HEADSIZE; i++)
		segment[i] = segment[i + HEADSIZE];

	segment[i] = '\0';
}

/*void storeSegment(uchar *segment)
{
	int i;

	if(VRF > RN)
	{
#ifdef APP
	printf("[log] VRF reached RN. No window space available to store out of order segments..\n");
#endif
		return;
	}

#ifdef APP
	printf("[log] storing segment at location starting %d: ", (VRF * (MSS)));

#endif

	for(i=0;i<MSS-HEADSIZE;i++)
	{
		buffer[((VRF * (MSS))) + i] = segment[i];
#ifdef APP
	printf("%c(%d), ", buffer[((VRF * (MSS))) + i], (int) buffer[((VRF * (MSS))) + i]);
#endif
	}

#ifdef APP
	printf("\n");
#endif

	VRF = (VRF + 1);

#ifdef APP
	printf("[log] VRF: %d\n", VRF);
#endif
}*/

void storeSegment(uchar *segment, uint start)
{
        int i;

#ifdef APP
        printf("[log] storing segment at location starting %d: ", (start));

#endif

        for(i=0;i<MSS-HEADSIZE;i++)
        {
                buffer[start + i] = segment[i];
#ifdef APP
        printf("%c(%d), ", buffer[start + i], (int) buffer[start + i]);
#endif
        }

#ifdef APP
        printf("\n");
#endif

        VRF = (VRF + 1);

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

void writeToFile(int file, uchar *segment, int buf_len)
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

uint extractSeqNo(uchar *segment)
{
	uint seqNo = 0;

	seqNo = (uint) segment[0];
	seqNo = seqNo << 24;

	seqNo = seqNo + (((uint) segment[1]) << 16);
	seqNo = seqNo + (((uint) segment[2]) << 8);
	seqNo = seqNo + ((uint) segment[3]);

	return seqNo;
}

int main(int argc, char **argv)
{
	int sock, in_port;
	uchar *request, req_from[50], *segment, initParam[10];
	struct sockaddr_in clientCon;
	char ack[HEADSIZE];
	int file, i, j, bytesRead, packetCount = 0;
	uint recvSeq;

	char *fileName;
	int SERVER_PORT;
	int probLoss;

	if(argc < 4)
	{
		printf("[usage] ./receiver <server_port> <file_name> <probability>\n");
		return 1;
	}

	/********************Read conole parameters***************/

	SERVER_PORT = atoi(argv[1]);
	fileName = argv[2];
	probLoss = atoi(argv[3]);
/*
	|_(|_|_|_|_|_|_|)_|
	  RF           RN
*/


	sock = get_sock();

	file = get_file_descriptor(fileName, Create);

	bind_sock(sock, SERVER_PORT);

	listen_sock(sock);

	read_from(sock, initParam, 10, &clientCon); // read WINSIZE
	WINSIZE = atoi(initParam);

	read_from(sock, initParam, 10, &clientCon); // read MSS
	MSS = atoi(initParam) + HEADSIZE;

#ifdef APP
	printf("[log] params set: winsize = %d, mss = %d\n", WINSIZE, MSS);
#endif

	marked = (bool *) malloc(INFINITE_BUFFER * sizeof(bool));

	for(i=0;i<INFINITE_BUFFER;i++)
		marked[i] = false;

	RF = 0;
	RN = RF + WINSIZE;
	VRF = RF;

	buffer = (char *) malloc(INFINITE_BUFFER * 2);
	request = (char *) malloc(MSS);
	segment = (char *) malloc(MSS);

	while(1) // listen continuosly
	{
		while((RN - RF) > WINSIZE)
		{
#ifdef APP
	printf("[log] buffer full.. waiting\n");
#endif
			sleep(1);
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
			removeHeader(request);

			storeSegment(request, recvSeq);

			marked[recvSeq] = true;
#ifdef APP
	printf("[log] marking %d\n", recvSeq);
#endif
			sendNak(sock, req_from, in_port);

			nakSent = true;
		}
		else if(nakSent && IS_IN_WINDOW(recvSeq) && !marked[recvSeq]) // if received frame is in window range, then buffer it
		{
			removeHeader(request);

			storeSegment(request, recvSeq);

			marked[recvSeq] = true;
#ifdef APP
	printf("[log] marking %d\n", recvSeq);
#endif
		}
		else if(recvSeq == RF * MSS) // correct segment. yay!!!
		{
			uint prev;

#ifdef APP
	printf("[log] valid segment found for seq no: %d\n", RF * MSS);
#endif
#ifdef DROP
	if(packetCount % probLoss == 0 && packetCount != 0)
	{
		printf("[drop log]\n-------------- dropping packet: %d at seq no: %d\n---------------\n", packetCount, recvSeq);
		packetCount++;
		continue;
	}
	else
	{
		usleep(100);
	}
#endif	
			removeHeader(request);

			storeSegment(request, recvSeq);

			marked[recvSeq] = true;

#ifdef APP
	printf("[log] marking %d\n", recvSeq);
#endif

			for(i= RF * (MSS); marked[i] == true || RF < VRF ; i = (i + MSS))
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
	printf("[log] marking %d false\n", i);
#endif
				prev = i; // prev points to previous frame to be acknowledged. This is for cumulative ack
			}

			nakSent = false;

			sendAck(sock, prev, req_from, in_port); // send cumulative ack
			//assert(RF == VRF);
		}
		else
		{
#ifdef APP
	printf("[log] discarding packet RF: %d, RN = %d\n", RF, RN);
#endif

			// take no action. sliently discard
		}

		packetCount++;
	}
	
	// here write remaining file in buffer

	for(i = RF * MSS; i <= RN * MSS; i += MSS)
	{
		for(j=0; j<MSS - HEADSIZE; j++)
			segment[j] = buffer[i + j];

		writeToFile(file, segment, MSS - HEADSIZE); 
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
