#ifndef _CSIEBOX_CLIENT_
#define _CSIEBOX_CLIENT_

#ifdef __cplusplus
extern "C" {
#endif

#include "csiebox_common.h"

#include <limits.h>
#include <malloc.h>

#define default_name "./longestPath.txt"
#define EVENT_SIZE (sizeof(struct inotify_event))
#define EVENT_BUF_LEN (1024 * (EVENT_SIZE + 16))

typedef struct {
	char path[PATH_MAX];
	struct stat statbuf;
} fileinfo;

typedef struct {
	fileinfo *array;
	size_t used;
	size_t size;
} filearray;

void initArray(filearray *a, size_t initialSize) {
	a->array = (fileinfo*)malloc(initialSize*sizeof(fileinfo));
	a->used = 0;
	a->size = initialSize;
}

void insertArray(filearray *a, fileinfo element){
	if (a->used == a->size) {
		a->size *= 2;
		a->array = (fileinfo*)realloc(a->array, a->size*sizeof(fileinfo));
	}
	a->array[a->used++] = element;
}

void freeArray(filearray *a){
	free(a->array);
	a->array = NULL;
	a->used = a->size = 0;
}

typedef struct {
  struct {
    char name[30];
    char server[30];
    char user[USER_LEN_MAX];
    char passwd[PASSWD_LEN_MAX];
    char path[PATH_MAX];
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
