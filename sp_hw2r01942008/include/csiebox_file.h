#ifndef CSIEBOX_SENDGET_
#define CSIEBOX_SENDGET_ value

#ifdef __cplusplus
extern "C" {
#endif

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/limits.h>
#include <utime.h>

#include "csiebox_common.h"

int basesendregfile	(int conn_fd, const char* filepath, long filesize);
int basesendhlink(int conn_fd, const char* filepath1, const char* filepath2);
int basesendslink(int conn_fd, const char* filepath);
int basesendrm	(int conn_fd, const char* filepath);

void basegetregfile (int conn_fd, FILE *writefile, int filesize, int *succ);
void basegetslink (int conn_fd, const char* filepath, int filesize, int *succ);

int getendheader(int conn_fd, csiebox_protocol_op header_type);
void sendendheader(int conn_fd, csiebox_protocol_op header_type, csiebox_protocol_status status);

void subOffset(char *filepath, long offset);
int isHiddenfile(const char *filename);

#ifdef __cplusplus
}
#endif
#endif /* end of include guard: CSIEBOX_SENDGET_ */
