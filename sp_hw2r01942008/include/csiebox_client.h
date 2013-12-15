#ifndef _CSIEBOX_CLIENT_
#define _CSIEBOX_CLIENT_

#ifdef __cplusplus
extern "C" {
#endif

#include "csiebox_common.h"

#include <limits.h>

#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))
#define BUSY_WAIT_TIME 4

typedef struct {
  struct {
    char name[30];
    char server[30];
    char user[USER_LEN_MAX];
    char passwd[PASSWD_LEN_MAX];
    char path[PATH_MAX];
	int offset;
  } arg;
  int conn_fd;
  int client_id;
} csiebox_client;

void csiebox_client_init(
  csiebox_client** client, int argc, char** argv);
int csiebox_client_run(csiebox_client* client);
void csiebox_client_destroy(csiebox_client** client);

#ifdef __cplusplus
}
#endif

#endif
