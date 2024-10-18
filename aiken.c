#include <stdlib.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <getopt.h>
#include <errno.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT_UDP 46226
#define PORT_TCP 46227

#define BUF_SIZE 1024

char *basename(const char *path) {
	char *res = strrchr(path, '/');
	return (res == NULL) ? path : (res + 1);
}

void token_generate(char *dst) {
	sprintf(dst, "%04x", (uint16_t) time(NULL));
}

void share(const char *path) {
	FILE *file = fopen(path, "rb");
	if (file == NULL) {
		perror("Failed to open file");
		exit(errno);
	}
	char token[5];
	int udp_sock = socket(AF_INET, SOCK_DGRAM, 0), sock_option = 1;
	if (udp_sock < 0) {
		perror("Failed to initialize UDP socket");
		fclose(file);
		exit(errno);
	}
	if (setsockopt(udp_sock, SOL_SOCKET, SO_REUSEADDR, &sock_option, sizeof(sock_option)) < 0) {
		perror("Failed to set UDP socket options");
		exit(errno);
	}
	struct sockaddr_in listen_addr, receiver_addr;
	socklen_t addr_len = sizeof(receiver_addr);
	bzero(&listen_addr, sizeof(listen_addr));
	listen_addr.sin_family = AF_INET;
	listen_addr.sin_addr.s_addr = INADDR_ANY;
	listen_addr.sin_port = htons(PORT_UDP);
	if (bind(udp_sock, (struct sockaddr *) &listen_addr, sizeof(listen_addr)) < 0) {
		perror("Failed to bind UDP socket");
		fclose(file);
		close(udp_sock);
		exit(errno);
	}
	token_generate(token);
	printf("Successfully shared!\nToken: %s\n\n", token);
	char buffer[BUF_SIZE];
	char *filename = basename(path);
	int tcp_server_sock, tcp_client_sock;
	printf("Waiting for request...\n");
	// recieve request from receiver
	if (recvfrom(udp_sock, buffer, 4, 0, (struct sockaddr *) &receiver_addr, &addr_len) < 4) {
		printf("Got invalid request from %s\n", inet_ntoa(receiver_addr.sin_addr));
		fclose(file);
		close(udp_sock);
		exit(errno);
	}
	// check request token
	if (memcmp(buffer, token, 4) != 0) {
		printf("Got invalid token from %s\n", inet_ntoa(receiver_addr.sin_addr));
		// send NO packet which tells that request is rejected
		if (sendto(udp_sock, "NO", 2, 0, (struct sockaddr *) &receiver_addr, addr_len) < 2) {
			perror("Failed to send NO packet");
		}
		fclose(file);
		close(udp_sock);
		exit(errno);
	}
	printf("Got request from %s\n", inet_ntoa(receiver_addr.sin_addr));
	// create a TCP socket to send file
	tcp_server_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_server_sock < 0) {
		perror("Failed to initialize TCP socket");
		fclose(file);
		close(udp_sock);
		exit(errno);
	}
	if (setsockopt(tcp_server_sock, SOL_SOCKET, SO_REUSEADDR, &sock_option, sizeof(sock_option)) < 0) {
		perror("Failed to set TCP socket options");
		exit(errno);
	}
	listen_addr.sin_port = htons(PORT_TCP);
	if (bind(tcp_server_sock, (struct sockaddr *) &listen_addr, addr_len) < 0) {
		perror("Failed to bind TCP socket");
		fclose(file);
		close(udp_sock);
		close(tcp_server_sock);
		exit(errno);
	}
	if (listen(tcp_server_sock, 1) < 0) {
		perror("Failed to listen to TCP socket");
		fclose(file);
		close(udp_sock);
		close(tcp_server_sock);
		exit(errno);
	}
	// send OK packet which tells that we are ready to send file
	if (sendto(udp_sock, "OK", 2, 0, (struct sockaddr *) &receiver_addr, addr_len) < 2) {
		perror("Failed to send OK packet");
		fclose(file);
		close(udp_sock);
		close(tcp_server_sock);
		exit(errno);
	}
	close(udp_sock);
	printf("Waiting for reciever connection...\n");
	// wait for receiver connection
	if ((tcp_client_sock = accept(tcp_server_sock, (struct sockaddr *) &receiver_addr, &addr_len)) < 0) {
		perror("Failed to accept receiver connection");
		fclose(file);
		close(tcp_server_sock);
		exit(errno);
	}
	printf("Receiver connected, sending...\n");
	// send file name
	if (send(tcp_client_sock, filename, strlen(filename) + 1, 0) < strlen(filename) + 1) {
		perror("Failed to send file name");
		fclose(file);
		close(tcp_client_sock);
		close(tcp_server_sock);
		exit(errno);
	}
	// send file data
	ssize_t rcount;
	while ((rcount = fread(buffer, 1, BUF_SIZE, file)) > 0) {
		if (send(tcp_client_sock, buffer, rcount, 0) != rcount) {
			perror("Failed to send file data");
			fclose(file);
			close(tcp_client_sock);
			close(tcp_server_sock);
			exit(errno);
		}
	}
	close(tcp_client_sock);
	close(tcp_server_sock);
	printf("Sending complete!\n");
}

void get(const char *token, const char *path) {
	size_t abspath_len = strlen(path) + 1;
	if (abspath_len > PATH_MAX) {
		printf("Invalid path: too long\n");
		exit(1);
	}
	char abspath[PATH_MAX];
	strcpy(abspath, path);
	char fname[FILENAME_MAX];
	size_t fname_len;
	struct stat stat_buf;
	int udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
	if (udp_sock < 0) {
		perror("Failed to initialize UDP socket");
		exit(errno);
	}
	int sock_option = 1;
	if (setsockopt(udp_sock, SOL_SOCKET, SO_BROADCAST, &sock_option, sizeof(sock_option)) < 0) {
		perror("Failed to set UDP socket options");
		exit(errno);
	}
	struct sockaddr_in broadcast_addr, server_addr;
	socklen_t addr_len = sizeof(server_addr);
	bzero(&broadcast_addr, sizeof(broadcast_addr));
	broadcast_addr.sin_family = AF_INET;
	broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
	broadcast_addr.sin_port = htons(PORT_UDP);
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = INADDR_ANY;
	server_addr.sin_port = htons(PORT_TCP);
	int tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (tcp_sock < 0) {
		perror("Failed to initialize TCP socket");
		exit(errno);
	}
	if (setsockopt(tcp_sock, SOL_SOCKET, SO_REUSEADDR, &sock_option, sizeof(sock_option)) < 0) {
		perror("Failed to set TCP socket options");
		exit(errno);
	}
	if (bind(tcp_sock, (struct sockaddr *) &server_addr, addr_len) < 0) {
		perror("Failed to bind TCP socket");
		exit(errno);
	}
	char buffer[BUF_SIZE];
	printf("Searching server... ");
	// send broadcast with the token
	if (sendto(udp_sock, token, strlen(token), 0, (struct sockaddr *) &broadcast_addr, sizeof(broadcast_addr)) < strlen(token)) {
		perror("Failed to broadcast request");
		exit(errno);
	}
	// receive response from server
	if (recvfrom(udp_sock, (void *) buffer, 2, 0, (struct sockaddr  *) &server_addr, &addr_len) < 2) {
		perror("Failed to receive response");
		exit(errno);
	}
	// check server response
	printf("Server found, response: %.2s\n", buffer);
	if (memcmp(buffer, "OK", 2) != 0) {
		if (memcmp(buffer, "NO", 2) != 0) {
			printf("Error: Got unexpected response from server\n");
		} else {
			printf("Request was rejected by server\n");
		}
		exit(1);
	}
	// connect to server
	server_addr.sin_port = htons(PORT_TCP);
	if (connect(tcp_sock, (struct sockaddr *) &server_addr, addr_len) < 0) {
		perror("Failed to connect to server");
		exit(errno);
	}
	printf("Getting file...\n");
	// get file name
	ssize_t rcount;
	if ((rcount = read(tcp_sock, buffer, BUF_SIZE)) <= 0) {
		perror("Failed to get file name");
		exit(errno);
	}
	fname_len = strlen(buffer) + 1;
	strcpy(fname, buffer);
	// if path is a directory, append file name to it
	if (stat(abspath, &stat_buf) == 0 && S_ISDIR(stat_buf.st_mode)) {
		abspath[abspath_len - 1] = '/';
		abspath[abspath_len] = 0;
		strcat(abspath, fname);
	}
	FILE *file = fopen(abspath, "wb");
	if (file == NULL) {
		perror("Failed to open file");
		exit(errno);
	}
	if (rcount > fname_len) {
		fwrite(buffer + fname_len, 1, rcount - fname_len, file);
	}
	// get file data
	for (;;) {
		rcount = read(tcp_sock, buffer, BUF_SIZE);
		if (rcount == 0) {
			break;
		}
		if (rcount < 0) {
			perror("Failed to get file");
			exit(errno);
		}
		fwrite(buffer, 1, rcount, file);
	}
	fclose(file);
	printf("Success!\nFile path: %s\n", realpath(abspath, abspath));
}

int main(int argc, char **argv) {
	if (argc == 1) {
		printf("Use '-h' to view help.\n");
		return 0;
	}
	char *path = NULL,
		 *token = NULL;
	int mode_share;
	int opt;
	while ((opt = getopt(argc, argv, "hs:g:o:V")) != -1) {
		switch (opt) {
			case 'h':
				printf(
					"(C) Masahiko AMANO aka H1K0, 2024—present\n"
					"(https://github.com/H1K0/aiken)\n\n"
					"Usage:\n"
					"  aiken <options> [arguments]\n\n"
					"Options:\n"
					"  -h           Print this help and exit\n"
					"  -s <path>    Share the file of the specified <path>\n"
					"  -g <token>   Get the file by its token\n"
					"  -o <path>    Specify output path when getting file\n"
					"               (defaults to current working directory and\n"
					"               the initial file name)\n"
					"  -V           Print version info and exit\n"
				);
				return 0;
			case 'V':
				printf("Aiken 0.1\n");
				return 0;
			case 's':
				mode_share = 1;
				path = realpath(optarg, path);
				if (path == NULL) {
					perror("Invalid share path");
					return errno;
				}
				break;
			case 'g':
				mode_share = 0;
				token = optarg;
				break;
			case 'o':
				if (mode_share) {
					fprintf(stderr, "Warning: The option '-o' is ignored when sharing.\n");
					break;
				}
				path = optarg;
				break;
		}
	}
	if (mode_share) {
		share(path);
	} else {
		get(token, path);
	}
	return 0;
}
