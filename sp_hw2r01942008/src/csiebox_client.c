#include "csiebox_client.h"
#include "csiebox_common.h"
#include "csiebox_sendget.h"
#include "connect.h"
#include "array.h"

#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <linux/inotify.h>
#include <time.h>

static int parse_arg(csiebox_client *client, int argc, char **argv);
static int login(csiebox_client *client);
static int synctime(csiebox_client *client);
static int monitor(csiebox_client *client, filearray *list);
static int sendmeta(csiebox_client *client, const char *syncfile, const struct stat *statptr);
static int sendfile(csiebox_client *client, const char *syncfile, const struct stat *statptr);
static int sendhlink(csiebox_client *client, fileinfo *src, fileinfo *target);
static int sendrmfile(csiebox_client *client, const char *rmfile); 
static int sendend(csiebox_client *client); 
int treewalk(csiebox_client *client, filearray* list);
int handlepath(char *path, filearray* list);
int checkfile(csiebox_client *client, filearray *list, int idx);
int findfile(filearray *list, char *filename);
int handlefile(csiebox_client *client, fileinfo* info);
int isHiddenfile(char *filename);

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

	if (!login(client)) {
		fprintf(stderr, "login fail\n");
		return 0;
	}
	fprintf(stderr, "login success\n");
	//sync time getween server and client
	synctime(client);

	//walk through client directory, generate filelist
	filearray list;
	initArray(&list, 10);
	treewalk(client, &list);

	//upload file, check hardlink in the sametime
	for (idx = 0; idx < list.used; ++idx) {
		checkfile(client, &list, idx);
	}

	sendend(client);
	
	//start monitor modification
	monitor(client, &list);

	freeArray(&list);
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
    fprintf(stderr, "config (%zd, %s)=(%zd, %s)\n", keylen, key, vallen, val);
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
    } else if (strcmp("offset", key) == 0) {
		client->arg.offset = atoi(val);
        accept_config[5] = 1;
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

static int
synctime(csiebox_client* client){
	fprintf(stderr, "Sync time with server\n");
	csiebox_protocol_synctime timestamp;
	memset(&timestamp, 0, sizeof(timestamp));

	if (recv_message(client->conn_fd, &timestamp, sizeof(timestamp))) {
		if (timestamp.message.header.res.magic == CSIEBOX_PROTOCOL_MAGIC_REQ &&
			timestamp.message.header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_TIME) {
			timestamp.message.body.t[1] = time(0) + 3600*client->arg.offset;
			timestamp.message.body.t[2] = time(0) + 3600*client->arg.offset;
			send_message(client->conn_fd, &timestamp, sizeof(timestamp));
		}
	}

	return getendheader(client->conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_TIME);
}

static int monitor(csiebox_client* client, filearray* list){
	int length, i = 0, idx = 0;
	int fd;
	int wd;
	char buffer[EVENT_BUF_LEN];
	memset(buffer, 0, EVENT_BUF_LEN);

	//create a instance and returns a file descriptor
	//add directory "." to watch list with specified events
	fd = inotify_init();
	if (fd < 0) {
		perror("inotify_init");
	}
	wd = inotify_add_watch(fd, client->arg.path, IN_CREATE | IN_DELETE | IN_ATTRIB | IN_MODIFY);

	fprintf(stderr, "Start monitor directory: %s\n", client->arg.path);
	while ((length = read(fd, buffer, EVENT_BUF_LEN)) > 0) {
		i = 0;
		while (i < length) {
			struct inotify_event* event = (struct inotify_event*)&buffer[i];
			if (!isHiddenfile(event->name)) {
				if (event->mask & IN_CREATE) {
					fileinfo ele;
					strncpy(ele.path, event->name, strlen(event->name));
					ele.path[strlen(event->name)] = '\0';
					insertArray(list, ele);
					printf("create file/dir %s\n", ele.path);
					checkfile(client, list, list->used-1);
				}
				if ((event->mask & IN_ATTRIB) || (event->mask & IN_MODIFY)) {
					char* buf = (char*)malloc(PATH_MAX);
					strncpy(buf, event->name, strlen(event->name));
					buf[strlen(event->name)] = '\0';
					
					printf("modify attrib/content of file: %s\n", buf);
					if ((idx = findfile(list, buf)) >= 0) {
						checkfile(client, list, idx);
					}
					free(buf);
				}
				if (event->mask & IN_DELETE) {
					char* buf = (char*)malloc(PATH_MAX);
					strncpy(buf, event->name, strlen(event->name));
					buf[strlen(event->name)] = '\0';
					
					printf("delete file: %s\n", buf);
					if ((idx = findfile(list, buf)) >= 0) {
						sendrmfile(client, list->array[idx].path);
						delArray(list, idx);
					}
					free(buf);
				}
			}
			i += EVENT_SIZE + event->len;
		}
		memset(buffer, 0, EVENT_BUF_LEN);
	}
	//inotify_rm_watch(fd, wd);
	close(fd);
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
static int sendfile(csiebox_client* client, const char* syncfile, const struct stat* statptr) {
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
	req.message.body.isSlink = ((statptr->st_mode & S_IFMT) == S_IFLNK)? 1:0;
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
			return basesendregfile(client->conn_fd, syncfile, statptr->st_size);
		case S_IFLNK:
			return basesendslink(client->conn_fd, syncfile);
	}
}

static int sendhlink(csiebox_client* client, fileinfo* src, fileinfo* target){
	csiebox_protocol_hardlink req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
	req.message.header.req.client_id = client->client_id;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.srclen = strlen(src->path);
	req.message.body.targetlen = strlen(target->path);
	if (!send_message(client->conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - hlink protocol\n");
		return -1;
	}
	basesendhlink(client->conn_fd, src->path, target->path);
}

static int sendrmfile(csiebox_client *client, const char* path){
	csiebox_protocol_rm req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
	req.message.header.req.client_id = client->client_id;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.pathlen = strlen(path);
	if (!send_message(client->conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - rm protocol\n");
		return -1;
	}
	return basesendrm(client->conn_fd, path);
}

static int 
sendend(csiebox_client* client){
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
	header.res.client_id = client->client_id;
	header.res.datalen = 0;
	if (!send_message(client->conn_fd, &header, sizeof(header))) {
		fprintf(stderr, "send fail - end protocol\n");
		return -1;
	}
	fprintf(stderr, "Sync file to server end\n");
	return getendheader(client->conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_END);
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
	filepath[1] = '\0';
	return(handlepath(filepath, list));
}

int
handlepath(char *localpath, filearray* list)
{
	struct stat		statbuf;
	struct dirent	*direntry;
	DIR				*dp;
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
		if ((strcmp(direntry->d_name, ".") == 0) || \
			(strcmp(direntry->d_name, "..") == 0) || \
			isHiddenfile(direntry->d_name)) 
			continue;
		strcpy(suffix, direntry->d_name);
		lstat(localpath, &statbuf);
		fileinfo ele;
		strncpy(ele.path, localpath+2, strlen(localpath)); //+2 to remove "./" at beginning
		ele.path[strlen(localpath)] = '\0';
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
findfile(filearray* list, char* filename){
	int i = 0;
	for (i = 0; i < list->used; ++i) {
		if ((strncmp(list->array[i].path, filename, PATH_MAX)) == 0) {
			return i;
		}
	}
	return -1;
}

int 
checkfile(csiebox_client* client, filearray* list, int idx){
	int i;
	fileinfo* target = &list->array[idx];
	//get newest statbuf
	lstat(target->path, &target->statbuf);
	//search for hardlink
	if (target->statbuf.st_nlink > 1 &&
			(target->statbuf.st_mode & S_IFMT) == S_IFREG) {
		for (i = 0; i < idx; i++) {
			if (target->statbuf.st_ino == list->array[i].statbuf.st_ino &&
			   (target->statbuf.st_dev == list->array[i].statbuf.st_dev)) {
				printf("get a hard link from %s to %s\n", target->path, list->array[i].path);
				sendhlink(client, &list->array[i], target);
				return 0;
			}
		}
	}
	handlefile(client, target);
}

int
handlefile(csiebox_client* client, fileinfo* info)
{
	switch(info->statbuf.st_mode & S_IFMT){
		case S_IFREG:
			printf("get a regular file: %s\n", info->path);
			sendfile(client, info->path, &info->statbuf);
			break;
		case S_IFLNK:
			printf("get a slink: %s\n", info->path);
			sendfile(client, info->path, &info->statbuf);
			break;
		case S_IFDIR:
			printf("get a directory: %s\n", info->path);
			sendmeta(client, info->path, &info->statbuf);
			break;
		case S_IFBLK:
		case S_IFCHR:
		case S_IFIFO:
			printf("Don't know how to handle OAO\n");
			return -1;
	}
	return 0;
}

int 
isHiddenfile(char *filename) 
{
	return (filename[0] == '.') ? 1 : 0;
}
