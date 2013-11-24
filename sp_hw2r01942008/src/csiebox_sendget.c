#include "csiebox_sendget.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>

int 
basesendregfile	(int conn_fd, const char* filepath, long filesize)
{
	FILE *readfile = fopen(filepath, "rb");
	if (filepath == NULL) {
		fprintf(stderr, "cannot open file for transfer: %s\n", filepath);
		return -1;
	}
	char *buffer = (char*)malloc(sizeof(char)*BUFFER_SIZE);
	if (buffer == NULL) {
		fprintf(stderr, "cannot open memory for file buffer\n");
		return -2;
	}
	int numr = 0;

	fprintf(stderr, "start send file %s\n", filepath);
	while (filesize%BUFFER_SIZE > 0) {
		if ((numr = fread(buffer, 1, filesize%BUFFER_SIZE, readfile)) != filesize%BUFFER_SIZE ) {
			if (ferror(readfile) != 0) {
				fprintf(stderr, "read file error: %s\n", filepath);
				return -1;
			}
		}
		if (!send_message(conn_fd, buffer, numr)) {
			fprintf(stderr, "send file fail\n");
			return -1;
		}
		filesize -= numr;
	}
	fclose(readfile);
	free(buffer);

	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_FILE);
}

int basesendhlink(int conn_fd, const char* src, char* target){
	if (!send_message(conn_fd, (void*)src, strlen(src))) {
		fprintf(stderr, "send fail - hlink src filename\n");
		return -1;
	}
	if (!send_message(conn_fd, (void*)target, strlen(target))) {
		fprintf(stderr, "send fail - hlink target filename\n");
		return -1;
	}
	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK);
}

int basesendslink(int conn_fd, const char* filepath){
	char *buffer = (char*)malloc(sizeof(char)*PATH_MAX);
	if (buffer == NULL) {
		fprintf(stderr, "cannot open memory for file buffer\n");
		return -2;
	}
	int numr = readlink(filepath, buffer, PATH_MAX);

	if (!send_message(conn_fd, (void*)buffer, numr)) {
		fprintf(stderr, "send file fail\n");
		return -1;
	}
	free(buffer);

	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_FILE);
}

int basesendrm	(int conn_fd, const char* filepath){
	if (!send_message(conn_fd, (void*)filepath, strlen(filepath))) {
		fprintf(stderr, "send fail - rm filename\n");
		return -1;
	}
	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_RM);
}

int getendheader(int conn_fd, csiebox_protocol_op header_type){
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	if (recv_message(conn_fd, &header, sizeof(header))) {
		if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
			header.res.op == header_type &&
			header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
			return 0;
		}
	}
	return -1;
}
