#ifndef CSIEBOX_SENDGET_
#define CSIEBOX_SENDGET_ value

#ifdef __cplusplus
extern "C" {
#endif

#include "csiebox_common.h"

int basesendregfile	(int conn_fd, const char* filepath, long filesize);
int basesendhlink(int conn_fd, const char* filepath1, char* filepath2);
int basesendslink(int conn_fd, const char* filepath);
int basesendrm	(int conn_fd, const char* filepath);

void basegetregfile (int conn_fd, const char* filepath, int filesize, int *succ);
void basegetslink (int conn_fd, const char* filepath, int filesize, int *succ);

int getendheader(int conn_fd, csiebox_protocol_op header_type);
void sendendheader(int conn_fd, csiebox_protocol_op header_type, int succ);

#ifdef __cplusplus
}
#endif
#endif /* end of include guard: CSIEBOX_SENDGET_ */
