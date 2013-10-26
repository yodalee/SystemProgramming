#include "csiebox_client.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

static int parse_arg(csiebox_client* client, int argc, char** argv);
static int login(csiebox_client* client);
static int sendfile(csiebox_client* client);
static int sendmeta(csiebox_client* client);
static int sendhlink(csiebox_client* client);
static int rmfile(csiebox_client *client); 
int treewalk(csiebox_client *client);
int handlepath(char* path);
int handlefile(const char* path, const struct stat *statptr, int type);
enum {TW_F, TW_DNR, TW_NS};
/* file other than directory */
/* directory */
/* directory that can't be read */
/* file that we can't stat */

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
  if (!login(client)) {
    fprintf(stderr, "login fail\n");
    return 0;
  }
  fprintf(stderr, "login success\n");
  //walk through client directory, find longest path
  treewalk(client);
  //transfer file 
  //transfer 

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

//--------------------------------------------
// Function: treewalk
// Description: chdir to client path, then do treewalk
//--------------------------------------------
int
treewalk(csiebox_client* client) 
{
	char *fullpath = (char*)malloc(PATH_MAX);
	strncpy(fullpath, client->arg.path, PATH_MAX);
	fullpath[PATH_MAX-1] = '\0';
	return(handlepath(fullpath));
}

int
handlepath(char *fullpath)
{
	static int		maxLen;
	static char		maxName[PATH_MAX+1];
	struct stat		statbuf;
	struct dirent	*direntry;
	DIR				*dp;
	int				ret;
	char			*suffix;
	
	// check directory open permission
	if ((dp = opendir(fullpath)) == NULL) {
		return handlefile(fullpath, &statbuf, TW_DNR);
	}
	//is directory walk through directory
	suffix = fullpath + strlen(fullpath);
	*suffix++='/';
	*suffix = '\0';
	// walk through directory entry by readdir
	while ((direntry = readdir(dp)) != NULL) {
		if (strcmp(direntry->d_name, ".") == 0 || \
			strcmp(direntry->d_name, "..") == 0)
			continue;
		strcpy(suffix, direntry->d_name);
		lstat(fullpath, &statbuf);
		//call handlefile to sync
		handlefile(fullpath, &statbuf, TW_F);
		if (S_ISDIR(statbuf.st_mode)) {
			// get a subdirectory, call walkdir recursive
			handlepath(fullpath);
		}
	}
	suffix[-1] = '\0';
	if (closedir(dp) < 0) 
		fprintf(stderr, "can't close client directory %s", fullpath);
}

int
handlefile(const char *pathname, const struct stat *statptr,int type)
{
	switch(type){
		case TW_F:
			switch(statptr->st_mode & S_IFMT){
				case S_IFREG: printf("regular: %s\n", pathname); break;
				case S_IFBLK: printf("XD\n"); break;
				case S_IFCHR: printf("XD\n"); break;
				case S_IFIFO: printf("XD\n"); break;
				case S_IFLNK: printf("slink: %s\n", pathname); break;
				case S_IFDIR: printf("getdir: %s\n", pathname); break;
			}
			break;
		case TW_DNR:
			fprintf(stderr, "can't read directory %s", pathname);
			return 1;
		case TW_NS:
			fprintf(stderr, "stat error for %s", pathname);
			return 1;
		default: 
			fprintf(stderr, "unknown type %d for pathname %s\n", type, pathname);
			exit(1);
	}
	return (0);
}
