#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <errno.h>

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);
static int sendmeta(csiebox_client* client, const char* syncfile, const struct stat* statptr);
static int senddata(csiebox_client* client, const char* syncfile, const struct stat* statptr);
static int senddataend(csiebox_client* client);
static int sendfile(csiebox_client* client, const char* syncfile, const struct stat* statptr);
static int sendslink(csiebox_client* client, const char* syncfile);
static int sendhlink(csiebox_client* client);
static int rmfile(csiebox_client *client); 
int treewalk(csiebox_client *client, filearray* list);
int handlepath(char* path, filearray* list);
int findmax(filearray* list);
int handlefile(csiebox_client *client, fileinfo* info);
enum {TW_F, TW_DNR};
/* file*/
/* directory that can't be read */

//read config file, and connect to server
void csiebox_client_init(
  csiebox_client** client, int argc, char** argv) {
  csiebox_client* tmp = (csiebox_client*)malloc(sizeof(csiebox_client));
  if (!tmp) {
    fprintf(stderr, "client malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_client));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = client_start(tmp->arg.name, tmp->arg.server);
  if (fd < 0) {
    fprintf(stderr, "connect fail\n");
    free(tmp);
    return;
  }
  tmp->conn_fd = fd;
  *client = tmp;
}

//this is where client sends request, you sould write your code here
int csiebox_client_run(csiebox_client* client) {
	int idx = 0;
	char longest[PATH_MAX];

	if (!login(client)) {
		fprintf(stderr, "login fail\n");
		return 0;
	}
	fprintf(stderr, "login success\n");

	//walk through client directory, generate filelist
	filearray list;
	initArray(&list, 10);
	treewalk(client, &list);

	//find maximum
	findmax(&list);

	for (idx = 0; idx < list.used; ++idx) {
		handlefile(client, &list.array[idx]);
	}
	return 1;
}

void csiebox_client_destroy(csiebox_client** client) {
  csiebox_client* tmp = *client;
  *client = 0;
  if (!tmp) {
    return;
  }
  close(tmp->conn_fd);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_client* client, int argc, char** argv) {
  if (argc != 2) {
    return 0;
  }
  FILE* file = fopen(argv[1], "r");
  if (!file) {
    return 0;
  }
  fprintf(stderr, "reading config...\n");
  size_t keysize = 20, valsize = 20;
  char* key = (char*)malloc(sizeof(char) * keysize);
  char* val = (char*)malloc(sizeof(char) * valsize);
  ssize_t keylen, vallen;
  int accept_config_total = 5;
  int accept_config[5] = {0, 0, 0, 0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
    if (strcmp("name", key) == 0) {
      if (vallen <= sizeof(client->arg.name)) {
        strncpy(client->arg.name, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("server", key) == 0) {
      if (vallen <= sizeof(client->arg.server)) {
        strncpy(client->arg.server, val, vallen);
        accept_config[1] = 1;
      }
    } else if (strcmp("user", key) == 0) {
      if (vallen <= sizeof(client->arg.user)) {
        strncpy(client->arg.user, val, vallen);
        accept_config[2] = 1;
      }
    } else if (strcmp("passwd", key) == 0) {
      if (vallen <= sizeof(client->arg.passwd)) {
        strncpy(client->arg.passwd, val, vallen);
        accept_config[3] = 1;
      }
    } else if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(client->arg.path)) {
        strncpy(client->arg.path, val, vallen);
        accept_config[4] = 1;
      }
    }
  }
  free(key);
  free(val);
  fclose(file);
  int i, test = 1;
  for (i = 0; i < accept_config_total; ++i) {
    test = test & accept_config[i];
  }
  if (!test) {
    fprintf(stderr, "config error\n");
    return 0;
  }
  return 1;
}

static int login(csiebox_client* client) {
  csiebox_protocol_login req;
  memset(&req, 0, sizeof(req));
  req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
  req.message.header.req.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
  memcpy(req.message.body.user, client->arg.user, strlen(client->arg.user));
  md5(client->arg.passwd,
      strlen(client->arg.passwd),
      req.message.body.passwd_hash);
  if (!send_message(client->conn_fd, &req, sizeof(req))) {
    fprintf(stderr, "send fail\n");
    return 0;
  }
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  if (recv_message(client->conn_fd, &header, sizeof(header))) {
    if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
        header.res.op == CSIEBOX_PROTOCOL_OP_LOGIN &&
        header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
      client->client_id = header.res.client_id;
      return 1;
    } else {
      return 0;
    }
  }
  return 0;
}

//sendmeta return whether file need update, or send directory path to server
static int sendmeta(csiebox_client* client, const char* syncfile, const struct stat* statptr) {
	//prepare protocol meta content
	csiebox_protocol_meta req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
	req.message.header.req.client_id = client->client_id;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.pathlen = strlen(syncfile);
	memcpy(&req.message.body.stat, statptr, sizeof(struct stat));
	if ((statptr->st_mode & S_IFMT) != S_IFDIR) {
		md5_file(syncfile, req.message.body.hash);
	}

	//send content
	if (!send_message(client->conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - meta protocol\n");
		return -1;
	}
	if (!send_message(client->conn_fd, (void*)syncfile, strlen(syncfile))) {
		fprintf(stderr, "send fail - meta filename\n");
		return -1;
	}

	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	if (recv_message(client->conn_fd, &header, sizeof(header))) {
		if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
			header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META) {
			return header.res.status;
		}
	}
	return -1;
}

//automatic sync data(file, slink, dir) up to server
static int senddata(csiebox_client* client, const char* syncfile, const struct stat* statptr) {
	switch( sendmeta(client, syncfile, statptr) ) {
		case(CSIEBOX_PROTOCOL_STATUS_OK):
			printf("no need to send file\n");
			return 0;
		case(CSIEBOX_PROTOCOL_STATUS_FAIL):
			printf("there is something wrong on server\n");
			return -1;
		case(CSIEBOX_PROTOCOL_STATUS_MORE):
			printf("Start uploading file %s\n", syncfile);
			break;
		case -1:
			fprintf(stderr, "something wrong uploading %s\n", syncfile);
			return -1;
	}
	csiebox_protocol_file req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
	req.message.header.req.client_id = client->client_id;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.datalen = statptr->st_size;
	req.message.body.pathlen = strlen(syncfile);
	if (!send_message(client->conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - file protocol\n");
		return -1;
	}
	if (!send_message(client->conn_fd, (void*)syncfile, strlen(syncfile))) {
		fprintf(stderr, "send fail - file filename\n");
		return -1;
	}
	switch(statptr->st_mode & S_IFMT){
		case S_IFREG:
			sendfile(client, syncfile, statptr);
			break;
		case S_IFLNK:
			sendslink(client, syncfile);
			break;
	}
}

static int sendfile(csiebox_client* client, const char* syncfile, const struct stat* statptr) {
	FILE *readfile = fopen(syncfile, "rb");
	if (readfile == NULL) {
		fprintf(stderr, "cannot open file for transfer: %s\n", syncfile);
		return -1;
	}
	char *buffer = (char*)malloc(sizeof(char)*BUFFER_SIZE);
	if (buffer == NULL) {
		fprintf(stderr, "cannot open memory for file buffer\n");
		return -2;
	}
	unsigned long filesize = statptr->st_size;
	int numr = 0;

	while (filesize%BUFFER_SIZE > 0) {
		if ((numr = fread(buffer, 1, filesize%BUFFER_SIZE, readfile)) != filesize%BUFFER_SIZE ) {
			if (ferror(readfile) != 0) {
				fprintf(stderr, "read file error: %s\n", syncfile);
				return -1;
			}
		}
		if (!send_message(client->conn_fd, buffer, numr)) {
			fprintf(stderr, "send file fail\n");
			return -1;
		}
		filesize -= numr;
	}
	fclose(readfile);
	free(buffer);

	return senddataend(client);
}

static int sendslink(csiebox_client* client, const char* syncfile) {
	char *buffer = (char*)malloc(sizeof(char)*PATH_MAX);
	if (buffer == NULL) {
		fprintf(stderr, "cannot open memory for file buffer\n");
		return -2;
	}
	int numr = readlink(syncfile, buffer, PATH_MAX);

	if (!send_message(client->conn_fd, buffer, numr)) {
		fprintf(stderr, "send file fail\n");
		return -1;
	}
	free(buffer);

	return senddataend(client);
}

static int senddataend(csiebox_client* client) {
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	if (recv_message(client->conn_fd, &header, sizeof(header))) {
		if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
			header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_FILE &&
			header.res.status == CSIEBOX_PROTOCOL_STATUS_OK) {
			return 0;
		}
	}
	return -1;
}
//--------------------------------------------
// Function: treewalk
// Description: 
// first check client directory exist, if not make it
// then chdir to client path, do treewalk
//--------------------------------------------
int
treewalk(csiebox_client* client, filearray* list) 
{
	char *filepath = (char*)malloc(PATH_MAX);
	strncpy(filepath, client->arg.path, PATH_MAX);

	//try to make client directory
	if ( mkdir(filepath, DIR_S_FLAG) == -1){
		if( EEXIST != errno ){
			fprintf(stderr, "Error when creating client home directory\n");
			exit(1);
		}
	} else {
		printf("Create home directory at: %s\n", filepath);
	}
	chdir(filepath);
	strncpy(filepath, ".", 2);
	filepath[1] == '\0';
	return(handlepath(filepath, list));
}

int
handlepath(char *localpath, filearray* list)
{
	static int		maxLen;
	static char		maxName[PATH_MAX+1];
	struct stat		statbuf;
	struct dirent	*direntry;
	DIR				*dp;
	int				ret;
	char			*suffix;
	
	// check directory open permission
	if ((dp = opendir(localpath)) == NULL) {
		return 1;
	}
	//is directory walk through directory
	suffix = localpath + strlen(localpath);
	*suffix++='/';
	*suffix = '\0';
	// walk through directory entry by readdir
	while ((direntry = readdir(dp)) != NULL) {
		if (strcmp(direntry->d_name, ".") == 0 || \
			strcmp(direntry->d_name, "..") == 0)
			continue;
		strcpy(suffix, direntry->d_name);
		lstat(localpath, &statbuf);
		fileinfo ele;
		strncpy(ele.path, localpath, strlen(localpath));
		ele.path[strlen(localpath)] = '\0';
		memcpy(&ele.statbuf, &statbuf, sizeof(struct stat));
		insertArray(list, ele);
		if (S_ISDIR(statbuf.st_mode)) {
			// get a subdirectory, call walkdir recursive
			handlepath(localpath, list);
		}
	}
	suffix[-1] = '\0';
	if (closedir(dp) < 0) 
		fprintf(stderr, "can't close client directory %s", localpath);
}

int
handlefile(csiebox_client* client, fileinfo* info)
{
	switch(info->statbuf.st_mode & S_IFMT){
		case S_IFREG:
			printf("get a regular file: %s\n", info->path);
			senddata(client, info->path, &info->statbuf);
			break;
		case S_IFLNK:
			printf("get a slink: %s\n", info->path);
			senddata(client, info->path, &info->statbuf);
			break;
		case S_IFDIR:
			printf("get a directory: %s\n", info->path);
			sendmeta(client, info->path, &info->statbuf);
			break;
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			printf("Don't know how to handle OAO\n");
			break;
	}
	return 0;
}

int findmax( filearray* list ){
	int idx = 0;
	int maxlen = 0;
	int maxidx = 0;
	struct stat statbuf;
	for (idx = 0; idx < list->used; ++idx) {
		if (strlen(list->array[idx].path) > maxlen) {
			maxlen = strlen(list->array[idx].path);
			maxidx = idx;
		}
	}
	FILE* record = fopen(default_name, "w");
	fprintf( record, "%s\n", list->array[maxidx].path);
	fclose(record);
	
	lstat(default_name, &statbuf);
	fileinfo ele;
	strncpy(ele.path, default_name, strlen(default_name));
	ele.path[strlen(default_name)] = '\0';
	memcpy(&ele.statbuf, &statbuf, sizeof(struct stat));

	insertArray(list, ele);
	return 0;
}

