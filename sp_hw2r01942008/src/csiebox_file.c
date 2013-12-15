#include "csiebox_file.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <utime.h>

int 
basesendregfile	(int conn_fd, const char* filepath, long filesize)
{
	FILE *readfile = fopen(filepath, "rb");
	if (filepath == NULL) {
		fprintf(stderr, "cannot open file for transfer: %s\n", filepath);
		return CSIEBOX_PROTOCOL_STATUS_FAIL;
	}
	char *buffer = (char*)malloc(sizeof(char)*BUFFER_SIZE);
	if (buffer == NULL) {
		fprintf(stderr, "cannot open memory for file buffer\n");
		return CSIEBOX_PROTOCOL_STATUS_FAIL;
	}
	int numr = 0;
    int size = 0;

	fprintf(stderr, "start send file %s, size %ld\n", filepath, filesize);
	while (filesize%BUFFER_SIZE > 0) {
        size = (filesize>BUFFER_SIZE)?BUFFER_SIZE:filesize%BUFFER_SIZE;
		if ((numr = fread(buffer, 1, size, readfile)) != size ) {
			if (ferror(readfile) != 0) {
				fprintf(stderr, "read file error: %s\n", filepath);
                return CSIEBOX_PROTOCOL_STATUS_FAIL;
			}
		}
		if (!send_message(conn_fd, buffer, numr)) {
			fprintf(stderr, "send file fail\n");
            return CSIEBOX_PROTOCOL_STATUS_FAIL;
		}
		filesize -= numr;
	}
	fclose(readfile);
	free(buffer);

	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_FILE);
}

int basesendhlink(int conn_fd, const char* src, const char* target){
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
            return header.res.status;
		} else if (
            header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
            header.res.op == header_type &&
            header.res.status == CSIEBOX_PROTOCOL_STATUS_BUSY) {
            return header.res.status;
        }
	}
	return -1;
}

void basegetregfile (int conn_fd, const char* filepath, int filesize, int *succ){
	fprintf(stderr, "sync file %s\n", filepath);
	FILE* writefile= fopen(filepath, "w");
	if (writefile == NULL) {
		fprintf(stderr, "cannot open write file\n");
		succ = 0;
	}
	char* buffer = (char*)malloc(sizeof(char)*BUFFER_SIZE);
	if (buffer == NULL) {
		fprintf(stderr, "cannot allocate write memory\n");
		succ = 0;
	}

    int size;

    fprintf(stderr, "%d\n", filesize);
	while (succ && (filesize != 0)) {
        size = (filesize>BUFFER_SIZE) ? BUFFER_SIZE : filesize%BUFFER_SIZE;
		if (!recv_message(conn_fd, buffer, size)) {
			fprintf(stderr, "Something wrong during file transfer\n");
			succ = 0;
			break;
		}
		fwrite(buffer, 1, size, writefile);
		filesize -= size;
	}
	fclose(writefile);
}

void basegetslink(int conn_fd, const char* filepath, int filesize, int *succ){
	char* buffer = (char*)malloc(sizeof(char)*filesize);
	if (!recv_message(conn_fd, buffer, filesize)) {
		fprintf(stderr, "cannot get slink file content\n");
		succ = 0;
	}
	buffer[filesize] = '\0';
	fprintf(stderr, "sync symbolic link %s point to %s\n", filepath, buffer);
	symlink(buffer, filepath);
}

//return protocol
void sendendheader(
		int conn_fd,
		csiebox_protocol_op header_type, 
		csiebox_protocol_status status){
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
	header.res.op = header_type;
	header.res.datalen = 0;
	header.res.status = status;
	send_message(conn_fd, &header, sizeof(header));
}

//subtract some offset on the 
void subOffset(char *filepath, long offset){
	struct stat statbuf;
	struct utimbuf timebuf;
	lstat(filepath, &statbuf);
	timebuf.actime = statbuf.st_atime;
	timebuf.modtime = statbuf.st_mtime - offset;
	utime(filepath, &timebuf);
}

int 
isHiddenfile(const char *filename) 
{
	return (filename[0] == '.') ? 1 : 0;
}
