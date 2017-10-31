



#include <stdio.h>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

#define KING_SERVER_PORT		9096
#define KING_SERVER_IP			"192.168.189.128"

#define MAX_BUFFER_LENGTH		1024
#define MAX_FILE_LENGTH			512*1024

#define SEND_FILENAME			"IMG_0481.jpg"

#if __linux__
typedef socket SOCKET;
typedef struct sockaddr_in SOCKADDR_IN;
#endif

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

int write_file(char *filename, char *buffer, int length) {
	
	FILE *fp = fopen(filename, "wb+");
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


int recv_buffer(int sockfd, char *buffer, int length) {
	
	int idx = 0;
	int ret = 0;

	while (1) {
		char rbuffer[MAX_BUFFER_LENGTH] = {0};

		int count = recv(sockfd, rbuffer, MAX_BUFFER_LENGTH, 0);
		if (count == 0) {
			ret = -1;
			close_fd(sockfd);
			break;
		} else if (count == -1) {
			ret = -2;
			break;
		} else {
			memcpy(buffer+idx, rbuffer, count);
			idx += count;
		}
	}

	return ret;
}

SOCKET init_client(void) {
	
	SOCKET clientsocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);  //创建一个Tcp
	if (clientsocket <= 0) {
		printf("套接字创建失败\n");
		return -1;
	}

	SOCKADDR_IN	serveraddr = {0};
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_port = htons(KING_SERVER_PORT);
	serveraddr.sin_addr.S_un.S_addr = inet_addr(KING_SERVER_IP);
	
	int result = connect(clientsocket, (SOCKADDR*)&serveraddr, sizeof(serveraddr));
	if (result != 0) {
		printf("连接失败");
		return -2;
	}

	return clientsocket;
}


int main(void) {

	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 0), &wsa);

	SOCKET clientfd = init_client();
	if (clientfd < 0) {
		printf("init_client failed\n");
		return -1;
	}
	
	char buffer[MAX_FILE_LENGTH] = {0};
	int length = read_file(SEND_FILENAME, buffer, MAX_FILE_LENGTH);
	if (length < 0) {
		printf("read_file failed\n");
		goto _exit;
	}

	int count = send_buffer(clientfd, buffer, length);
	if (count != length) {
		printf("send_buffer failed\n");
		goto _exit;
	}

_exit:
	system("pause");
	closesocket(clientfd);

	return 0;
}

