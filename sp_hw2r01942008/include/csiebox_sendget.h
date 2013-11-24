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
int getendheader(int conn_fd, csiebox_protocol_op header_type);

int getfile	(int conn_fd, const char* filepath);
int gethlink	(int conn_fd, const char* filepath1, char* filepath2);
int getslink	(int conn_fd, const char* filepath);
int getrm	(int conn_fd, const char* filepath);

#ifdef __cplusplus
}
#endif
#endif /* end of include guard: CSIEBOX_SENDGET_ */
