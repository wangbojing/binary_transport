
/*
 * Author  : WangBoJing
 * Email   : 1989wangbojing@163.com 
 * 
 */



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



#define KING_SERVER_PORT		9096
#define KING_SERVER_IP			"192.168.189.128"

#define MAX_BUFFER_LENGTH		1024
#define MAX_FILE_LENGTH			512*1024

#define MAX_EPOLLSIZE			512
#define MAX_CLIENTS_COUNT		1024

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


char *filetype[] = {
	".jpg",
	".mp3"
};


#define ENABLE_NONBLOCK			1



typedef struct entrys {
	int client_id;
	char *data;
	int index;
} ENTRYS;


struct entrys clients_list[MAX_CLIENTS_COUNT] = {0};

int close_fd(int sockfd) {
#ifdef _WIN32
	return closesocket(sockfd);
#elif __linux__
	return close(sockfd);
#elif __unix__
	return close(sockfd);
#else
#error "Unknown compiler"
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
		printf("fread failed count : %d\n", size);
		goto _exit;
	}

_exit:
	fclose(fp);
	return size;
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
		printf(" %s file too big\n", filename);
		fclose(fp);
		return -2;
	}
	
	fseek(fp, 0, SEEK_SET);

	int size = fread(buffer, 1, count, fp);
	if (size != count) {
		printf("fread failed count : %d\n", size);
		goto _exit;
	}

_exit:
	fclose(fp);
	return size;
}


int send_buffer(int sockfd, char *buffer, int length) {
	
	int idx = 0;

	while (idx < length) {

		int count = 0;
		if ((idx + MAX_BUFFER_LENGTH) < length) {
			count = send(sockfd, buffer+idx, MAX_BUFFER_LENGTH, 0);
		} else {
			count = send(sockfd, buffer+idx, length-idx, 0);
		}
		if (count <= 0) break;
		
		idx += count;
	}
	return idx;
}


int recv_buffer(int sockfd, char *buffer, int length, int *ret) {
	
	int idx = 0;

	while (1) {
		int count = recv(sockfd, buffer+idx, length - idx, 0);
		if (count == 0) {
			*ret = -1;
			close_fd(sockfd);
			break;
		} else if (count == -1) {
			printf("recv success --> count : %d\n", idx);
			*ret = 0;
			break;
		} else {			
			idx += count;
			if (idx == length) break;
		}
	}

	return idx;
}

int decode_packet(char *buffer, int *length,  int *selfid, unsigned short *idx, unsigned short *count, char *type) {

	*selfid = *(int *)(buffer+KING_PROTO_SELF_ID);
	*idx = *(unsigned short *)(buffer+KING_PROTO_PACKET_IDX);
	
	*count = *(unsigned short*)(buffer+KING_PROTO_PACKET_COUNT);
	*length = *(unsigned short*)(buffer+KING_PROTO_PACKET_LENGTH);

	*type = *(buffer+KING_PROTO_PACKET_TYPE);

	printf(" decode_packet --> idx:%d, count:%d, selfid:%d\n", *idx, *count, *selfid);

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


int main(void) {

	int sockfd = init_server();
	if (sockfd < 0) {
		printf("init_server failed\n");
		return -1;
	}

	int epoll_fd = epoll_create(MAX_EPOLLSIZE);
	struct epoll_event ev, events[MAX_EPOLLSIZE] = {0};

	ev.events = EPOLLIN;
	ev.data.fd = sockfd;
	epoll_ctl(epoll_fd, EPOLL_CTL_ADD, sockfd, &ev);

	while (1) {

		int nfds = epoll_wait(epoll_fd, events, MAX_EPOLLSIZE, -1);
		if (nfds == -1) {
			printf("epoll_wait failed\n");
			break;
		}

		int i = 0;
		for (i = 0;i < nfds;i ++) {
			int connfd = events[i].data.fd;
			if (connfd == sockfd) {
				struct sockaddr_in client_addr;
				memset(&client_addr, 0, sizeof(struct sockaddr_in));
				socklen_t client_len = sizeof(client_addr);
			
				int clientfd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
				char str[INET_ADDRSTRLEN] = {0};
				printf("recvived from %s at port %d, sockfd:%d, clientfd:%d\n", inet_ntop(AF_INET, &client_addr.sin_addr, str, sizeof(str)),
					ntohs(client_addr.sin_port), sockfd, clientfd);
#if ENABLE_NONBLOCK
				ntySetNonblock(clientfd);

				memset(clients_list+clientfd, 0, sizeof(struct entrys));
				clients_list[clientfd].data = malloc(MAX_BUFFER_LENGTH * sizeof(char));
#endif
				ev.events = EPOLLIN | EPOLLET;
				ev.data.fd = clientfd;
				epoll_ctl(epoll_fd, EPOLL_CTL_ADD, clientfd, &ev);
				
			} else {
#if ENABLE_NONBLOCK		

#if 0
				int ret = 0;
				char data[MAX_BUFFER_LENGTH] = {0};
				
				int length = recv_buffer(connfd, data, MAX_BUFFER_LENGTH, &ret);
				if (ret < 0) {
					
					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = connfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connfd, &ev);

					printf(" data length : %d \n", clients_list[connfd].index);
					int count = write_file("a.jpg", clients_list[connfd].data, clients_list[connfd].index, 0);
					if (count < 0) {
						printf("write_file failed\n");
						continue;
					}
					
				}  else if (ret == 0) {

					memcpy(clients_list[connfd].data+clients_list[connfd].index, data, length);
					clients_list[connfd].index += length;

				}

#else

				int ret = 0;
				char *data = clients_list[connfd].data;
				int rIdx = clients_list[connfd].index;

				int length = recv_buffer(connfd, data+rIdx, MAX_BUFFER_LENGTH-rIdx, &ret);
				rIdx += length;
				
				if (ret < 0) {
					
					ev.events = EPOLLIN | EPOLLET;
					ev.data.fd = connfd;
					epoll_ctl(epoll_fd, EPOLL_CTL_DEL, connfd, &ev);

					printf(" data length : %d \n", clients_list[connfd].index);
					
					
				}  else if (ret == 0) {

					int selfid = 0;
					unsigned short idx = 0;
					unsigned short count = 0;
					char type = 0;

					int ret = decode_packet(data, &length, &selfid, &idx, &count, &type);
					if (ret) {
						printf("read finished\n");
 					} else {
 						if (rIdx != MAX_BUFFER_LENGTH) {
 							clients_list[connfd].index = rIdx;
							printf(" decode_packet --> continue\n");
							continue;
 						}
 					}

					char filename[128] = {0};
					
					sprintf(filename, "./recv_file/%d%s", selfid, filetype[type]);
					//printf("filename : %s\n", filename);
				
					int size = write_file(filename, data+KING_PACKET_HEADER, rIdx-KING_PACKET_HEADER-KING_PACKET_TAIL, 1);
					if (size < 0) {
						printf("write_file failed\n");
						//continue;
					}

					rIdx = 0;
				}

				clients_list[connfd].index = rIdx;

#endif

#else
				char *data = (char*)malloc(MAX_FILE_LENGTH * sizeof(char));
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

					printf("length : %d\n", length);
					int count = write_file("a.jpg", data, length, 1);
					if (count < 0) {
						printf("write_file failed\n");
						break;
					}
					
				}

#endif

				
			}
		}
	}

}













