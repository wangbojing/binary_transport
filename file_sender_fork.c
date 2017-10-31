



#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/poll.h>
#include <sys/epoll.h>

#include <netinet/tcp.h>
#include <arpa/inet.h>

#include <fcntl.h>



#if __linux__
typedef int SOCKET;
#endif

#define MAX_BUFFER_LENGTH		1024
#define MAX_FILE_LENGTH			512*1024

#define KING_SERVER_PORT		9096
#define KING_SERVER_IP			"192.168.189.128"


/* ** **** ******** **************** protocol start **************** ******** **** ** */

#define CLIENT_ID_LENGTH		4

#define KING_PROTO_VERSION			0
#define KING_PROTO_CMD				1
#define KING_PROTO_SELF_ID			2
#define KING_PROTO_PACKET_IDX		(KING_PROTO_SELF_ID+CLIENT_ID_LENGTH)  //6
#define KING_PROTO_PACKET_COUNT		(KING_PROTO_PACKET_IDX+2) //8
#define KING_PROTO_PACKET_LENGTH	(KING_PROTO_PACKET_COUNT+2) //10
#define KING_PROTO_PACKET_TYPE		(KING_PROTO_PACKET_LENGTH+2) //10


#define KING_PACKET_CRC_VALUE		0x5A5AA5A5

#define KING_PACKET_HEADER			16
#define KING_PACKET_TAIL			4

#define KING_VERSION_0				0x0
#define KING_CMD_BINARY				'B'


#define KING_TYPE_JPG				0x00
#define KING_TYPE_MP3				0x01


/* ** **** ******** **************** protocol end **************** ******** **** ** */



enum {
	KING_FILE_START = 0x0,
	KING_FILE_0 = KING_FILE_START,
	KING_FILE_1,
	KING_FILE_2,
	KING_FILE_3,
	KING_FILE_4,
	KING_FILE_5,
	KING_FILE_6,
	KING_FILE_7,
	KING_FILE_8,
	KING_FILE_9,
	KING_FILE_END = KING_FILE_9,
};

char *filename_list[] = {
	"./sender_picture/0.jpg",
	"./sender_picture/1.jpg",
	"./sender_picture/2.jpg",
	"./sender_picture/3.jpg",
	"./sender_picture/4.jpg",
	"./sender_picture/5.jpg",
	"./sender_picture/6.jpg",
	"./sender_picture/7.jpg",
	"./sender_picture/8.mp3",
	"./sender_picture/9.mp3",
};


int close_fd(SOCKET sockfd) {
#ifdef _WIN32
	return closesocket(sockfd);
#elif __linux__
	return close(sockfd);
#elif __unix__
	return close(sockfd);
#else
#error "Unknow compile"
#endif
}

int read_file(char *filename, int *index, char *buffer, int length) {
	
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) {
		printf("fopen failed\n");
		return -1;
	}

	fseek(fp, *index, SEEK_SET);

	int size = fread(buffer, 1, length, fp);
	if (size != length) {
		printf("read file end\n");
	}
	*index += size;

_exit:
	fclose(fp);

	return size;
}

int count_file(char *filename) {
	
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) {
		printf("fopen failed\n");
		return -1;
	}

	fseek(fp, 0, SEEK_END);
	int length = ftell(fp);

	fclose(fp);

	return length;
}


int write_file(char *filename, char *buffer, int length, char isappend) {

	FILE *fp = NULL;

	if (isappend) {
		fp = fopen(filename, "ab+");
	} else {
		fp = fopen(filename, "wb+");
	}
	if (fp == NULL) {
		printf("fopen failed\n");
		return -1;
	}

	int size = fwrite(buffer, 1, length, fp);
	if (size != length) {
		printf("fread failed\n");
		goto _exit;
	}
	
_exit:
	fclose(fp);
	return size;
}

int send_buffer(SOCKET sockfd, char *buffer, int length) {
#if 0
	return send(sockfd, buffer, length, 0)
#else

	int idx = 0;
	while (idx < length) {
		
		int count = 0;
		if ((idx+MAX_BUFFER_LENGTH) < length) {
			count = send(sockfd, buffer+idx, MAX_BUFFER_LENGTH, 0);
		} else {
			count = send(sockfd, buffer+idx, length-idx, 0);
		}
		if (count < 0) break;

		idx += count;
	}

	return idx;
#endif
}


int recv_buffer(SOCKET sockfd, char *buffer, int length, int *ret) {

	int idx = 0;

	while (1) {
		
		int count = recv(sockfd, buffer+idx, MAX_BUFFER_LENGTH, 0);
		if (count < 0) {
			*ret = -1;
			break;
		} else if (count == 0) {
			*ret = -2;
			close_fd(sockfd);
		} else {
			idx += count;
		}
	}
	
	return idx;
}


char *encode_packet(char *buffer, int *length,  long selfid, unsigned short idx, unsigned short count, char type) {

	int len = *length + KING_PACKET_HEADER + KING_PACKET_TAIL;
	
	char *pkt = (char *)malloc(len+sizeof(char));
	if (pkt == NULL) {
		printf("encode_packet --> malloc failed\n");
		return NULL;
	}
	memset(pkt, 0, len);

	memcpy(pkt+KING_PACKET_HEADER, buffer, *length);

	*(pkt+KING_PROTO_VERSION) = 0x0;
	*(pkt+KING_PROTO_CMD) = KING_CMD_BINARY;

	*(long *)(pkt+KING_PROTO_SELF_ID) = selfid;
	*(unsigned short *)(pkt+KING_PROTO_PACKET_IDX) = idx;
	*(unsigned short*)(pkt+KING_PROTO_PACKET_COUNT) = count;
	*(unsigned short*)(pkt+KING_PROTO_PACKET_LENGTH) = *length;
	*(pkt+KING_PROTO_PACKET_TYPE) = type;

	printf(" encode_packet --> idx:%d, count:%d, selfid:%ld\n", idx, count, selfid);

	*(unsigned int*)(pkt+(*length + KING_PACKET_HEADER)) = KING_PACKET_CRC_VALUE;

	*length = len;

	return pkt;
}



int init_client(void) {
	
	int clientsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (clientsocket < 0) {
		printf("socket failed\n");
		return -1;
	}

	struct sockaddr_in serveraddr = {0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(KING_SERVER_PORT);
	serveraddr.sin_addr.s_addr = inet_addr(KING_SERVER_IP);

	int result = connect(clientsocket, (struct sockaddr*)&serveraddr, sizeof(serveraddr));
	if (result != 0) {
		printf("connect failed\n");
		return -2;
	}
	
	return clientsocket;

}


int main() {
#if 1
	fork();
	fork();
	fork();
	fork();
	fork();
#endif
	int clientfd = init_client();
	pid_t selfid = getpid();

	char buffer[MAX_BUFFER_LENGTH] = {0};
	int idx = 0;
	int pkt_length = MAX_BUFFER_LENGTH-KING_PACKET_HEADER-KING_PACKET_TAIL;
	int pkt_total = count_file(filename_list[selfid%10]);//filename_list[selfid%10]

	while (idx < pkt_total) {
		int count = read_file(filename_list[selfid%10], &idx, buffer, pkt_length);
		if (count < 0) {
			break;
		} else if (count < pkt_length) {
			printf("read finished\n");
		}

		char type = 0x0;
		if (selfid%10 > 7) {
			type = KING_TYPE_MP3;
		} else {
			type = KING_TYPE_JPG;
		}

		char *data = encode_packet(buffer, &count, selfid, (idx-1)/pkt_length, pkt_total/pkt_length+1, type);
		if (data == NULL) {
			break;
		} 

		send_buffer(clientfd, data, count);

		free(data);
	}

	getchar();
}







