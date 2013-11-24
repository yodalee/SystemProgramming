#include "csiebox_server.h"
#include "csiebox_common.h"
#include "csiebox_sendget.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <utime.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static int login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void synctime(csiebox_server* server, int conn_fd);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);
static void checkmeta(
	csiebox_server* server, int conn_fd, csiebox_protocol_meta* rm);
static void getfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_file* rm);
static void gethlink(
	csiebox_server* server, int conn_fd, csiebox_protocol_hardlink* rm);
static void subOffset(char *filepath, long offset);
static void removefile(
	csiebox_server* server, int conn_fd, csiebox_protocol_rm *rm);

//read config file, and start to listen
void csiebox_server_init(
  csiebox_server** server, int argc, char** argv) {
  csiebox_server* tmp = (csiebox_server*)malloc(sizeof(csiebox_server));
  if (!tmp) {
    fprintf(stderr, "server malloc fail\n");
    return;
  }
  memset(tmp, 0, sizeof(csiebox_server));
  if (!parse_arg(tmp, argc, argv)) {
    fprintf(stderr, "Usage: %s [config file]\n", argv[0]);
    free(tmp);
    return;
  }
  int fd = server_start();
  if (fd < 0) {
    fprintf(stderr, "server fail\n");
    free(tmp);
    return;
  }
  tmp->client = (csiebox_client_info**)
      malloc(sizeof(csiebox_client_info*) * getdtablesize());
  if (!tmp->client) {
    fprintf(stderr, "client list malloc fail\n");
    close(fd);
    free(tmp);
    return;
  }
  memset(tmp->client, 0, sizeof(csiebox_client_info*) * getdtablesize());
  tmp->listen_fd = fd;
  *server = tmp;
}

//wait client to connect and handle requests from connected socket fd
int csiebox_server_run(csiebox_server* server) {
	int i;
	const int maxlink = 1024;
	//socket
	int conn_fd, conn_len;
	struct sockaddr_in addr;
	int maxfd = server->listen_fd;
	//active fd
	int active_fd[maxlink];
	for (i = 0; i < maxlink; ++i) {
		active_fd[i] = 0;
	}
	int current_max = 0;

	fd_set readset;
	struct timeval tv;
	
	while (1) {
		//wait forever, until any descriptor return
		FD_ZERO(&readset);
		FD_SET(server->listen_fd, &readset);
		for (i = 0; i < maxlink; ++i) {
			if(active_fd[i] != 0){
				FD_SET(active_fd[i], &readset);
			}
		}
		//reset waiting time
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		
		switch (select(maxfd+1, &readset, NULL, NULL, &tv)) {
			case -1:
				fprintf(stderr, "select error\n");
				return 1;
			case 0:
				continue;
		}
		// handle request from connected socket fd
		for (i = 0; i < current_max; ++i) {
			if (FD_ISSET(active_fd[i], &readset)) {
				handle_request(server, conn_fd);
			}
		}
		// A new connection
		if (FD_ISSET(server->listen_fd, &readset)) {
			memset(&addr, 0, sizeof(addr));
			conn_len = 0;
			// waiting client connect
			conn_fd = accept(server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
			if (conn_fd < 0) {
				if (errno == ENFILE) {
					fprintf(stderr, "out of file descriptor table\n");
					continue;
				} else if (errno == EAGAIN || errno == EINTR) {
					continue;
				} else {
					fprintf(stderr, "accept err\n");
					fprintf(stderr, "code: %s\n", strerror(errno));
					break;
				}
			}
			if (current_max < maxlink) {
				active_fd[current_max++] = conn_fd;
				fprintf(stderr, "New connection on file descriptor %d\n", conn_fd);
				if (conn_fd > maxfd) {
					maxfd = conn_fd;
				}
			} else {
				fprintf(stderr, "Max connections exceed, close fd\n");
				close(conn_fd);
			}
		}
	}
	return 1;
}

void csiebox_server_destroy(csiebox_server** server) {
  csiebox_server* tmp = *server;
  *server = 0;
  if (!tmp) {
    return;
  }
  close(tmp->listen_fd);
  int i = getdtablesize() - 1;
  for (; i >= 0; --i) {
    if (tmp->client[i]) {
      free(tmp->client[i]);
    }
  }
  free(tmp->client);
  free(tmp);
}

//read config file
static int parse_arg(csiebox_server* server, int argc, char** argv) {
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
  int accept_config_total = 2;
  int accept_config[2] = {0, 0};
  while ((keylen = getdelim(&key, &keysize, '=', file) - 1) > 0) {
    key[keylen] = '\0';
    vallen = getline(&val, &valsize, file) - 1;
    val[vallen] = '\0';
    fprintf(stderr, "config (%zu, %s)=(%zu, %s)\n", keylen, key, vallen, val);
    if (strcmp("path", key) == 0) {
      if (vallen <= sizeof(server->arg.path)) {
        strncpy(server->arg.path, val, vallen);
        accept_config[0] = 1;
      }
    } else if (strcmp("account_path", key) == 0) {
      if (vallen <= sizeof(server->arg.account_path)) {
        strncpy(server->arg.account_path, val, vallen);
        accept_config[1] = 1;
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

//this is where the server handle requests, you should write your code here
static void handle_request(csiebox_server* server, int conn_fd) {
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	recv_message(conn_fd, &header, sizeof(header));
	if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
		return;
	}
	switch (header.req.op) {
		case CSIEBOX_PROTOCOL_OP_LOGIN:
			{
				fprintf(stderr, "login\n");
				csiebox_protocol_login req;
				if (complete_message_with_header(conn_fd, &header, &req)) {
					if(login(server, conn_fd, &req) ){
						synctime(server, conn_fd);
					}
				}
				break;
			}
		case CSIEBOX_PROTOCOL_OP_SYNC_META:
			{
				csiebox_protocol_meta meta;
				if (complete_message_with_header(conn_fd, &header, &meta)) {
					checkmeta(server, conn_fd, &meta);
				}
				break;
			}
		case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
			{
				csiebox_protocol_file file;
				if (complete_message_with_header(conn_fd, &header, &file)) {
					getfile(server, conn_fd, &file);
				}
				break;
			}
		case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
			{
				csiebox_protocol_hardlink hardlink;
				if (complete_message_with_header(conn_fd, &header, &hardlink)) {
					gethlink(server, conn_fd, &hardlink);
				}
				break;
			}
		case CSIEBOX_PROTOCOL_OP_SYNC_END:
			{
				header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
				header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
			    send_message(conn_fd, &header, sizeof(header));
				fprintf(stderr, "client %d sync file end\n", conn_fd);
				break;
			}
		case CSIEBOX_PROTOCOL_OP_RM:
			{
				csiebox_protocol_rm rm;
				if (complete_message_with_header(conn_fd, &header, &rm)) {
					removefile(server, conn_fd, &rm);
				}
				break;
			}
		default:
			fprintf(stderr, "unknown op %x\n", header.req.op);
			break;
	}
	//fprintf(stderr, "end of connection\n");
	//logout(server, conn_fd);
}

//open account file to get account information
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info) {
  FILE* file = fopen(server->arg.account_path, "r");
  if (!file) {
    return 0;
  }
  size_t buflen = 100;
  char* buf = (char*)malloc(sizeof(char) * buflen);
  memset(buf, 0, buflen);
  ssize_t len;
  int ret = 0;
  int line = 0;
  while ((len = getline(&buf, &buflen, file) - 1) > 0) {
    ++line;
    buf[len] = '\0';
    char* u = strtok(buf, ",");
    if (!u) {
      fprintf(stderr, "ill form in account file, line %d\n", line);
      continue;
    }
    if (strcmp(user, u) == 0) {
      memcpy(info->user, user, strlen(user));
      char* passwd = strtok(NULL, ",");
      if (!passwd) {
        fprintf(stderr, "ill form in account file, line %d\n", line);
        continue;
      }
      md5(passwd, strlen(passwd), info->passwd_hash);
      ret = 1;
      break;
    }
  }
  free(buf);
  fclose(file);
  return ret;
}

//handle the login request from client
static int login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login) {
  int succ = 1;
  csiebox_client_info* info =
    (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
  memset(info, 0, sizeof(csiebox_client_info));
  if (!get_account_info(server, login->message.body.user, &(info->account))) {
    fprintf(stderr, "cannot find account\n");
    succ = 0;
  }
  if (succ &&
      memcmp(login->message.body.passwd_hash,
             info->account.passwd_hash,
             MD5_DIGEST_LENGTH) != 0) {
    fprintf(stderr, "passwd miss match\n");
    succ = 0;
  }

	csiebox_client_info* tmp;
	if (succ) {
		if (server->client[conn_fd]) {
			tmp = server->client[conn_fd];
			if (tmp->next) {
				tmp->next->prev = tmp->prev;
			}
			if (tmp->prev) {
				tmp->prev->next = tmp->next;
			}
			free(server->client[conn_fd]);
		}
		info->conn_fd = conn_fd;
		info->next = NULL;
		info->prev = NULL;
		server->client[conn_fd] = info;
		//find same client and link into
		int i;
		int count = 1;
		for (i = 0; i < getdtablesize(); ++i) {
			if (server->client[i] && i != conn_fd){ 
				if (strncmp(info->account.user, server->client[i]->account.user, PATH_MAX) == 0) {
					tmp = server->client[i];
					while(tmp->next){
						tmp = tmp->next;
						++count;
					}
					info->prev = tmp;
					tmp->next = info;
					break;
				}
			}
		}
		printf("There are %d client link to this server\n", count);
		char* homedir = get_user_homedir(server, info);
		mkdir(homedir, DIR_S_FLAG);
		free(homedir);
	}
  //prepare return
  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));
}

static void 
synctime(csiebox_server* server, int conn_fd){
	fprintf(stderr, "Sync time with client\n");
	csiebox_protocol_synctime req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_TIME;
	req.message.body.t[0] = time(0);
	//start sync
	send_message(conn_fd, &req, sizeof(req));
	recv_message(conn_fd, &req, sizeof(req));
	req.message.body.t[3] = time(0);
	time_t offset = (
		req.message.body.t[1] - req.message.body.t[0] +
		req.message.body.t[2] - req.message.body.t[3])/2;
	server->client[conn_fd]->offset = offset;
	fprintf(stderr, "Get offset %ld\n", offset);
	
	//return OK to client, prevent sync again
	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_TIME, 1);
}

static void logout(csiebox_server* server, int conn_fd) {
  free(server->client[conn_fd]);
  server->client[conn_fd] = 0;
  close(conn_fd);
}

static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info) {
  char* ret = (char*)malloc(sizeof(char) * PATH_MAX);
  memset(ret, 0, PATH_MAX);
  sprintf(ret, "%s/%s/", server->arg.path, info->account.user);
  return ret;
}

//handle the send meta request, mkdir if the meta is a directory
//return STATUS_OK if no need to sendfile
//return STATUS_FAIL if something wrong
//return STATUS_MORE if need sendfile
static void checkmeta(
		csiebox_server* server, int conn_fd, csiebox_protocol_meta* meta) {
	//extract user info header
	int status = 1;
	csiebox_client_info* info =
	  (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
	memset(info, 0, sizeof(csiebox_client_info));
	int length = meta->message.body.pathlen;
	int client_id = meta->message.header.req.client_id;

	//get home directory from client_id
	info = server->client[client_id];
    char* fullpath = get_user_homedir(server, info);
	char* filepath = (char*)malloc(length);
	recv_message(conn_fd, filepath, length);
	strncat(fullpath, filepath, length);

	//checkmeta, if is directory, just call mkdir. file then compare hash
	fprintf(stderr, "sync meta: %s\n", fullpath);
	if ((meta->message.body.stat.st_mode & S_IFMT) == S_IFDIR) {
		mkdir(fullpath, DIR_S_FLAG);
		status = CSIEBOX_PROTOCOL_STATUS_OK;
	} else {
		uint8_t filehash[MD5_DIGEST_LENGTH];
		md5_file(fullpath, filehash);
		if (memcmp(meta->message.body.hash,
			filehash, MD5_DIGEST_LENGTH) != 0) {
			//return more
			fprintf(stderr, "md5 is different\n");
			status = CSIEBOX_PROTOCOL_STATUS_MORE;
		} else {
			//update meta content, return ok
			fprintf(stderr, "md5 is identical\n");
			struct stat statbuf;
			lstat(fullpath, &statbuf);
			memcpy(&statbuf, &meta->message.body.stat, sizeof(struct stat));
			subOffset(fullpath, server->client[conn_fd]->offset);
			status = CSIEBOX_PROTOCOL_STATUS_OK;
		}
	}

	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
	header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
	header.res.datalen = 0;
	header.res.status = status;
	send_message(conn_fd, &header, sizeof(header));

	free(fullpath);
	free(filepath);
}

static void getfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_file* file) {
	//extract user info header
	csiebox_client_info* info =
	  (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
	memset(info, 0, sizeof(csiebox_client_info));
	unsigned long filesize = file->message.body.datalen;
	int length = file->message.body.pathlen;
	int isSlink = file->message.body.isSlink;
	int client_id = file->message.header.req.client_id;

	//get home directory from client_id
	info = server->client[client_id];

    char* fullpath = get_user_homedir(server, info);
	char* filepath = (char*)malloc(length);
	recv_message(conn_fd, filepath, length);
	strncat(fullpath, filepath, length);

	//get file, here is using some dangerous mechanism
	int succ = 1;
	if (isSlink) {
		basegetslink(conn_fd, fullpath, filesize, &succ);
	} else {
		basegetregfile(conn_fd, fullpath, filesize, &succ);
	}
	subOffset(fullpath, server->client[conn_fd]->offset);

	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_FILE, succ);

	free(fullpath);
	free(filepath);
}

static void gethlink(
	csiebox_server* server, int conn_fd, csiebox_protocol_hardlink* file) {
	//extract user info header
	csiebox_client_info* info =
	  (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
	memset(info, 0, sizeof(csiebox_client_info));
	
	int client_id = file->message.header.req.client_id;
	int srclen = file->message.body.srclen;
	int targetlen = file->message.body.targetlen;
	int succ = 1;
	
	//get file fullpath
	info = server->client[client_id];
    char* srcpath = get_user_homedir(server, info);
    char* targetpath = get_user_homedir(server, info);
	char* filepath = (char*)malloc(PATH_MAX);
	recv_message(conn_fd, filepath, srclen);
	strncat(srcpath, filepath, srclen);
	recv_message(conn_fd, filepath, targetlen);
	strncat(targetpath, filepath, targetlen);
	
	fprintf(stderr, "sync hardlink from %s point to %s\n", srcpath, targetpath);
	//create hardlink
	if ((link(srcpath, targetpath) != 0)) {
		subOffset(targetpath, server->client[conn_fd]->offset);
		succ = 0;
	}
	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK, succ);

	free(srcpath);
	free(targetpath);
	free(filepath);
}

static void
subOffset(char *filepath, long offset){
	struct stat statbuf;
	struct utimbuf timebuf;
	lstat(filepath, &statbuf);
	timebuf.actime = statbuf.st_atime;
	timebuf.modtime = statbuf.st_mtime - offset;
	utime(filepath, &timebuf);
}

static void removefile(
	csiebox_server* server, int conn_fd, csiebox_protocol_rm *rm){
	csiebox_client_info* info =
	  (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
	memset(info, 0, sizeof(csiebox_client_info));
	
	int client_id = rm->message.header.req.client_id;
	int pathlen = rm->message.body.pathlen;
	int succ = 1;
	
	//get file fullpath
	info = server->client[client_id];
    char* fullpath = get_user_homedir(server, info);
	char* filepath = (char*)malloc(PATH_MAX);
	recv_message(conn_fd, filepath, pathlen);
	strncat(fullpath, filepath, pathlen);
	
	//unlink file
	fprintf(stderr, "remove file %s\n", fullpath);
	if ((unlink(fullpath) != 0)) {
		succ = 0;
	}

	//return protocol
	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_RM, succ);

	free(fullpath);
	free(filepath);
}
