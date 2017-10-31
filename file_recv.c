

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

#include <sys/epoll.h>

#include <arpa/inet.h>
#include <fcntl.h>



#if __linux__
typedef int SOCKET;
#endif

#define MAX_BUFFER_LENGTH		1024
#define MAX_FILE_LENGTH			512*1024

#define KING_SERVER_PORT		9096
#define KING_SERVER_IP			"192.168.189.128"

#define KING_FILENAME			"IMG_0481.JPG"

#define MAX_EPOLL_SIZE			1024

#define MAX_CLIENTS_COUNT		1024

/************************************** protocol start *************************************/

#define CLINET_ID_LENGTH		4

#define KING_PROTO_VERSION		0
#define KING_PROTO_CMD			1
#define KING_PROTO_SELF_ID		2
#define KING_PROTO_PKT_IDX		(KING_PROTO_SELF_ID+CLINET_ID_LENGTH)
#define KING_PROTO_PKT_COUNT	(KING_PROTO_PKT_IDX+2)
#define KING_PROTO_PKT_LENGHT	(KING_PROTO_PKT_COUNT+2)
#define KING_PROTO_PKT_TYPE		(KING_PROTO_PKT_LENGHT+2)

#define KING_PACKET_CRC_VALUE	0x5A5AA5A5

#define KING_PACKET_HEADER		16
#define KING_PACKET_TAIL		4


#define KING_PACKET_TYPE_JPG	0x0
#define KING_PACKET_TYPE_MP3	0x1

#define KING_VERSION_0			0x0
#define KING_BINARY_CMD			'B'

/************************************** protocol end *************************************/

char *filetype[] = {
	".jpg",
	".mp3"
};


typedef struct entrys {

	int client_id;
	char *data;
	int index;
	
} ENTRYS;


struct entrys clients_list[MAX_CLIENTS_COUNT] = {0};





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

static int ntySetNonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;
	return 0;
}


int read_file(char *filename, char *buffer, int length) {
	
	FILE *fp = fopen(filename, "rb");
	if (fp == NULL) {
		printf("fopen failed\n");
		return -1;
	}

	fseek(fp, 0, SEEK_END);

	int count = ftell(fp);
	if (count > length) {
		printf("file too big\n");
		goto _exit;
	}

	fseek(fp, 0, SEEK_SET);

	int size = fread(buffer, 1, count, fp);
	if (size != count) {
		printf("fread failed\n");
		goto _exit;
	}

_exit:
	fclose(fp);

	return size;
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
			*ret = 0;
			close_fd(sockfd);
		} else {
			idx += count;
		}
	}
	
	return idx;
}

int decode_packet(char *buffer, int *length, int *selfid, unsigned short *idx, unsigned short *count, char *type) {

	*selfid = *(int *)(buffer+KING_PROTO_SELF_ID);
	*idx = *(unsigned short*)(buffer+KING_PROTO_PKT_IDX);
	
	*count = *(unsigned short*)(buffer+KING_PROTO_PKT_COUNT);
	*length = *(unsigned short*)(buffer+KING_PROTO_PKT_LENGHT);

	*type = *(buffer+KING_PROTO_PKT_TYPE);

	if (*idx == *count-1) return 1;

	return 0;
}


int init_server(void) {

	int sockfd = socket(AF_INET, SOCK_STREAM, 0);
	if (sockfd < 0) {
		printf("socket failed\n");
		return -1;
	}

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(KING_SERVER_PORT);
	addr.sin_addr.s_addr = INADDR_ANY;

	if (bind(sockfd, (struct sockaddr*)&addr, sizeof(struct sockaddr_in)) < 0) {
		printf("bind failed\n");
		return -2;
	}

	if (listen(sockfd, 5) < 0) {
		printf("listen failed\n");
		return -3;
	}

	return sockfd;
	
}


int main() {

	int sockfd = init_server();
	if (sockfd < 0) {
		printf("init_server failed\n");
		return -1;
	}

	int epoll_fd = epoll_create(MAX_EPOLL_SIZE);
	struct epoll_event ev, events[MAX_EPOLL_SIZE] = {0};

	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);

	while (1) {

		int nfds = epoll_wait(epoll_fd, events, MAX_EPOLL_SIZE, -1);
		if (nfds < 0) {
			printf("epoll_wait failed\n");
			break;
		}

		int i = 0;
		for (i = 0;i < nfds;i ++) {
			int connfd = events[i].data.fd;

			if (connfd == sockfd) {

				struct sockaddr_in client_addr = {0};
				socklen_t client_len = sizeof(struct sockaddr_in);

				int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
				if (clientfd < 0) continue;

				printf("New Client Comming\n");
				ntySetNonblock(clientfd); //ÐÂÌí¼ÓµÄ

				memset(clients_list+clientfd, 0, sizeof(struct entrys));
				clients_list[clientfd].data = malloc(MAX_BUFFER_LENGTH * sizeof(char));
				memset(clients_list[clientfd].data, 0, MAX_BUFFER_LENGTH);

				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = clientfd;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &ev);
				
			} else {
#if 0
				char *data = (char *)malloc(MAX_FILE_LENGTH * sizeof(char));
				if (data == NULL) {
					printf("malloc failed\n");
					break;
				}

				int ret = 0;
				int length = recv_buffer(connfd, data, MAX_FILE_LENGTH, &ret);
				if (ret < 0) {

					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = connfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connfd, &ev);

					int count = write_file("hey.jpg", data, length);
					if (count < 0) {
						printf("write_file failed\n");
						free(data);
						break;
					}
					
				}

				free(data);
#else
				int ret = 0;
				char *data = clients_list[connfd].data;
				int rLen = clients_list[connfd].index;

				int length = recv_buffer(connfd, data, MAX_FILE_LENGTH-rLen, &ret);
				rLen += length;

				if (ret < 0) {

					int selfid = 0;
					unsigned short idx = 0;
					unsigned short count = 0;
					char type = 0;
					
					int ret = decode_packet(data, &length, &selfid, &idx, &count, &type); 
					if (ret) {
						printf("read finisned\n");
					} else {
						if (rLen != MAX_FILE_LENGTH) {
							clients_list[connfd].index = rLen;
							
							continue;
						}
					}

					char filename[128] = {0};
					sprintf(filename, "./recv_file/%d%s", selfid, filetype[type]);

					int size = write_file(filename, data+KING_PACKET_HEADER, rLen-KING_PACKET_HEADER-KING_PACKET_TAIL, 1);
					if (size < 0) {
						printf("write_file failed\n");
					}

					rLen = 0;
					
				} else if (ret == 0) {
					ev.events = EPOLLIN;
					ev.data.fd = connfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connfd, &ev);

					printf("data length : %d\n", clients_list[connfd].index);
				}

				clients_list[connfd].index = rLen;

#endif
			}
		}
	}

	
}



