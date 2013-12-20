#include "csiebox_server.h"
#include "csiebox_common.h"
#include "csiebox_file.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/file.h>
#include <dirent.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void prepare_arg(csiebox_server *server, int *conn_fd_ptr);
static void handle_request(void *inarg, void *outarg);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static int login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void synctime(csiebox_server* server, int conn_fd);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);

static void getfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_meta* meta);
static int getmeta(
	csiebox_server* server, int conn_fd, csiebox_protocol_meta* meta);
static void getregfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_file* file);
static void gethlink(
	csiebox_server* server, int conn_fd, csiebox_protocol_hardlink* hlink);
static void syncend(
	csiebox_server* server, int conn_fd, csiebox_protocol_header *header);
static void getrmfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_rm *rm);
static void handleconflict(
    csiebox_server *server, int conn, char *file1, int filesize);

static void notifyupdate(csiebox_server* server, int conn_fd, char* filename);
static void notifyremove(csiebox_server* server, int conn_fd, char* filename);
static void notifytree(csiebox_server* server, int conn_fd);
int treewalk(csiebox_client_info* client, char *filepath);
int handlepath(csiebox_client_info* client, char *path);

static int sendmeta(int conn_fd, const char *syncfile, const struct stat *statptr);
static int sendfile(csiebox_client_info* client, const char *syncfile, const struct stat *statptr);
static int sendrmfile(int conn_fd, const char *rmfile); 
static int sendend(int conn_fd); 

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
  init_thread_pool(&tmp->pool, tmp->arg.thread_num);
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
		//set monitor dile descriptor
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
                prepare_arg(server, &active_fd[i]);
				//handle_request(server, &active_fd[i]);
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
  int accept_config_total = 3;
  int accept_config[3] = {0, 0, 0};
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
    } else if (strcmp("thread", key) == 0) {
      server->arg.thread_num = strtol(val, NULL, 10);
      if (server->arg.thread_num <= 0) {
        accept_config[2] = 0;
      } else {
        accept_config[2] = 1;
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

static void
prepare_arg(csiebox_server *server, int *conn_fd_ptr)
{
  task_thread_arg *arg = (task_thread_arg*)malloc(sizeof(task_thread_arg));
  csiebox_task_arg *inarg = (csiebox_task_arg*)malloc(sizeof(csiebox_task_arg));
  inarg->server = server;
  inarg->conn_fd_ptr = conn_fd_ptr;
  arg->input = (void*)inarg;
  arg->output = NULL;
  arg->func = &handle_request;
  if((run_task(server->pool, arg)) < 0){
    //no free thread available
    //clear data in buffer and return busy
    int conn_fd = *conn_fd_ptr;
    char buf[BUFSIZ];
    csiebox_protocol_header header;
    recv_message(conn_fd, &header, sizeof(header));
    if (complete_message_with_header(conn_fd, &header, &buf)) { 
      sendendheader(conn_fd, header.req.op, CSIEBOX_PROTOCOL_STATUS_BUSY);
    }
  }
}

//this is where the server handle requests, you should write your code here
static void handle_request(void *inarg, void *outarg) {
    csiebox_task_arg *arg = (csiebox_task_arg*)inarg;
    csiebox_server* server = arg->server;
    int *conn_fd_ptr = arg->conn_fd_ptr;
    int conn_fd = *conn_fd_ptr;
    *conn_fd_ptr = 0; //stop select from monitor it
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
      }
      break;
    case CSIEBOX_PROTOCOL_OP_SYNC_META:
    {
      csiebox_protocol_meta meta;
      if (complete_message_with_header(conn_fd, &header, &meta)) {
          getfile(server, conn_fd, &meta);
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
      syncend(server, conn_fd, &header);
      break;
    }
    case CSIEBOX_PROTOCOL_OP_RM:
    {
      csiebox_protocol_rm rm;
      if (complete_message_with_header(conn_fd, &header, &rm)) {
        getrmfile(server, conn_fd, &rm);
      }
      break;
    }
    default:
    {
      fprintf(stderr, "unknown op %x\n", header.req.op);
      break;
    }
  }
  *conn_fd_ptr = conn_fd;
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
		int count = 0;
		for (i = 0; i < getdtablesize(); ++i) {
			if (server->client[i] && i != conn_fd){ 
				if (strncmp(info->account.user, server->client[i]->account.user, PATH_MAX) == 0) {
					tmp = server->client[i];
					++count;
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
	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_TIME
		, CSIEBOX_PROTOCOL_STATUS_OK);

}

static void logout(csiebox_server* server, int conn_fd) {
  if (server->client[conn_fd]) {
  	csiebox_client_info *tmp = server->client[conn_fd];
  	if (tmp->next) {
  		tmp->next->prev = tmp->prev;
  	}
  	if (tmp->prev) {
  		tmp->prev->next = tmp->next;
  	}
  	free(server->client[conn_fd]);
  }
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

static void getfile(
    csiebox_server *server, int conn_fd, csiebox_protocol_meta *meta) {
  int ret = getmeta(server, conn_fd, meta);
  if (ret == CSIEBOX_PROTOCOL_STATUS_MORE) {
    csiebox_protocol_header header;
    memset(&header, 0, sizeof(header));
    recv_message(conn_fd, &header, sizeof(header));
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
        return;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
      {
        csiebox_protocol_file file;
        if (complete_message_with_header(conn_fd, &header, &file)) {
            getregfile(server, conn_fd, &file);
        }
        break;
      }
    }
  }
}

//handle the send meta request, mkdir if the meta is a directory
//return STATUS_OK if no need to sendfile
//return STATUS_FAIL if something wrong
//return STATUS_MORE if need sendfile
static int getmeta(
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
	chdir(fullpath);
	char* filepath = (char*)malloc(length+1);
	recv_message(conn_fd, filepath, length);
	strncat(fullpath, filepath, length);
	filepath[length] = '\0';

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

	sendendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_META, status);

	free(fullpath);
	free(filepath);
    return status;
}

static void getregfile(
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
	chdir(fullpath);
	char* filepath = (char*)malloc(length+1);
	filepath[length] = '\0';
	recv_message(conn_fd, filepath, length);
	strncat(fullpath, filepath, length);

	//get file, if is slink, simply get slink
    //if is regular file, first get file lock, then using basegetregfile to retrieve file
    //if cannot get file lock, call file merger
	int succ = 1;
    int fd = 0;
    FILE *writefile;
	if (isSlink) {
	  basegetslink(conn_fd, fullpath, filesize, &succ);
      subOffset(fullpath, server->client[conn_fd]->offset);
	} else {
      fprintf(stderr, "sync file %s\n", fullpath);
      fd = open(fullpath, O_WRONLY | O_CREAT);
      if (flock(fd, LOCK_EX | LOCK_NB) == 0) {
        fprintf(stderr, "get file lock %s\n", fullpath);
        ftruncate(fileno(writefile), 0);
        writefile = fdopen(fd, "w"); //use this mode to prevent truncate file
        basegetregfile(conn_fd, writefile, filesize, &succ);
        subOffset(fullpath, server->client[conn_fd]->offset);
        flock(fd, LOCK_UN);
      } else {
        fprintf(stderr, "file locked, conflict detect on file: %s\n", fullpath);
        handleconflict(server, conn_fd, fullpath, filesize);
        return;
      }
	}
	sendendheader(
		conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_FILE,
		(succ)?CSIEBOX_PROTOCOL_STATUS_OK:CSIEBOX_PROTOCOL_STATUS_FAIL
		);

	notifyupdate(server, conn_fd, filepath);

	free(fullpath);
	free(filepath);
}

static void gethlink(
	csiebox_server* server, int conn_fd, csiebox_protocol_hardlink* hlink) {
	//extract user info header
	csiebox_client_info* info =
	  (csiebox_client_info*)malloc(sizeof(csiebox_client_info));
	memset(info, 0, sizeof(csiebox_client_info));
	
	int client_id = hlink->message.header.req.client_id;
	int srclen = hlink->message.body.srclen;
	int targetlen = hlink->message.body.targetlen;
	int succ = 1;
	
	//get file fullpath
	info = server->client[client_id];
    char* srcpath = get_user_homedir(server, info);
    char* targetpath = get_user_homedir(server, info);
	chdir(targetpath);
	char* filepath = (char*)malloc(PATH_MAX);
	recv_message(conn_fd, filepath, srclen);
	strncat(srcpath, filepath, srclen);
	filepath[srclen] = '\0';
	recv_message(conn_fd, filepath, targetlen);
	strncat(targetpath, filepath, targetlen);
	filepath[targetlen] = '\0';
	
	fprintf(stderr, "sync hardlink from %s point to %s\n", srcpath, targetpath);
	//create hardlink
	if ((link(srcpath, targetpath) != 0)) {
		subOffset(targetpath, server->client[conn_fd]->offset);
		succ = 0;
	}
	sendendheader( conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK,
		(succ)?CSIEBOX_PROTOCOL_STATUS_OK:CSIEBOX_PROTOCOL_STATUS_FAIL);
	notifyupdate(server, conn_fd, targetpath);

	free(srcpath);
	free(targetpath);
	free(filepath);
}

static void syncend(
    csiebox_server *server, int conn_fd, csiebox_protocol_header *header)
{
  header->res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header->res.status = CSIEBOX_PROTOCOL_STATUS_OK;
  send_message(conn_fd, &header, sizeof(csiebox_protocol_header));
  fprintf(stderr, "client %d sync file end\n", conn_fd);
  notifytree(server, conn_fd);
}

static void getrmfile(
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
	chdir(fullpath);
	char* filepath = (char*)malloc(PATH_MAX);
	recv_message(conn_fd, filepath, pathlen);
	strncat(fullpath, filepath, pathlen);
	filepath[pathlen] = '\0';
	
	//unlink file
	fprintf(stderr, "remove file %s\n", fullpath);
	if ((unlink(fullpath) != 0)) {
		succ = 0;
	}

	//return protocol
	sendendheader( conn_fd, CSIEBOX_PROTOCOL_OP_RM,
		(succ)?CSIEBOX_PROTOCOL_STATUS_OK:CSIEBOX_PROTOCOL_STATUS_FAIL);
	//notifyremove(server, conn_fd, filepath);

	free(fullpath);
	free(filepath);
}

static void
handleconflict(csiebox_server *server, int conn_fd, char *file1, int filesize)
{
  char tmp1[PATH_MAX] = "tempfile";
  char tmp2[PATH_MAX] = "tempfile";
  int succ = 1;
  pid_t pid;
  //try to get file lock, until upload thread end upload
  int fd = open(file1, O_WRONLY | O_CREAT, REG_S_FLAG);
  while (1) {
    int ret = flock(fd, LOCK_EX | LOCK_NB);
    fprintf(stderr, "busy wait =w=\n");
    if (ret == 0) { //get the lock, thread 1 file upload end
      break;
    }
  }
  //rename version 1 file into temporary name
  rename(file1, tmp1);
  //get version 2 file into temporary
  tmpnam(tmp1);
  int fd2 = mkstemp(tmp2);
  FILE *writefile = fdopen(fd2, "w+");
  basegetregfile(conn_fd, writefile, filesize, &succ);
  //call file_merger to merge
  if ((pid = fork()) < 0) {
    fprintf(stderr, "fork error\n");
  } else if (pid == 0) { //child call exec file_merger
    if (execl("./file_merger", "file_merger", "a", "a", "b", "c", (char*)0) < 0) {
      fprintf(stderr, "execle error\n");
    }
  } else {
    if (waitpid(pid, NULL, 0) < 0){
      fprintf(stderr, "wait error\n");
    }
  }
  //clear temporary file
  unlink(tmp1);
  unlink(tmp2);
}

static void 
notifyupdate(csiebox_server* server, int conn_fd, char* filename)
{
	struct stat statbuf;
	fprintf(stderr, "client %d upload a file, notify others\n", conn_fd);
	csiebox_client_info* client = server->client[conn_fd];
	if (client->prev || client->next) {
		lstat(filename, &statbuf);
		while (client->prev) { client = client->prev; } //go to the head of the link list
		while(1){
			if (client != server->client[conn_fd]) {
				fprintf(stderr, "Notify client with conn_fd %d\n", client->conn_fd);
				sendfile(client, filename, &statbuf);
			}
			if (client->next) {
				client = client->next;
			} else { //already at the end of the list
				break;
			}
		}
	} else {
		fprintf(stderr, "There are no other clients\n");
	}
}
static void
notifyremove (csiebox_server* server, int conn_fd, char* filename)
{
	fprintf(stderr, "client %d remove a file, notify others\n", conn_fd);
	csiebox_client_info* client = server->client[conn_fd];
	if (client->prev || client->next) {
		while (client->prev) { client = client->prev; } //go to the head of the link list
		while(1){
			if (client != server->client[conn_fd]) {
				fprintf(stderr, "Notify client with conn_fd %d\n", client->conn_fd);
				sendrmfile(client->conn_fd, filename);
			}
			if (client->next) {
				client = client->next;
			} else { //already at the end of the list
				break;
			}
		}
	} else {
		fprintf(stderr, "There are no other clients\n");
	}
}

static void 
notifytree(csiebox_server* server, int conn_fd)
{
	fprintf(stderr, "client %d login, check treewalk download\n", conn_fd);
	csiebox_client_info* client = server->client[conn_fd];
	if (client->prev || client->next) { //There is already a client link in
		char* homedir = get_user_homedir(server, client);
		fprintf(stderr, "Notify client with conn_fd %d\n", client->conn_fd);
		treewalk(client, homedir);
	} else {
		fprintf(stderr, "There are no other clients\n");
	}
}

int
treewalk(csiebox_client_info* client, char *filepath) 
{
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
	return(handlepath(client, filepath));
}

int
handlepath(csiebox_client_info* client, char *localpath)
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
		sendfile(client, localpath, &statbuf);
		if (S_ISDIR(statbuf.st_mode)) {
			// get a subdirectory, call walkdir recursive
			handlepath(client, localpath);
		}
	}
	suffix[-1] = '\0';
	if (closedir(dp) < 0) 
		fprintf(stderr, "can't close client directory %s", localpath);
}

static int sendmeta(int conn_fd, const char* syncfile, const struct stat* statptr) {
	//prepare protocol meta content
	csiebox_protocol_meta req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_META;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.pathlen = strlen(syncfile);
	memcpy(&req.message.body.stat, statptr, sizeof(struct stat));
	if ((statptr->st_mode & S_IFMT) != S_IFDIR) {
		md5_file(syncfile, req.message.body.hash);
	}

	//send content
	if (!send_message(conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - meta protocol\n");
		return -1;
	}
	if (!send_message(conn_fd, (void*)syncfile, strlen(syncfile))) {
		fprintf(stderr, "send fail - meta filename\n");
		return -1;
	}

	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	if (recv_message(conn_fd, &header, sizeof(header))) {
		if (header.res.magic == CSIEBOX_PROTOCOL_MAGIC_RES &&
			header.res.op == CSIEBOX_PROTOCOL_OP_SYNC_META) {
			return header.res.status;
		}
	}
	return -1;
}

static int sendfile(csiebox_client_info *client, const char *syncfile, const struct stat *statptr){
	int conn_fd = client->conn_fd;
	switch(sendmeta(conn_fd, syncfile, statptr) ) {
		case(CSIEBOX_PROTOCOL_STATUS_OK):
			printf("no need to send file\n");
			return 0;
		case(CSIEBOX_PROTOCOL_STATUS_FAIL):
			printf("there is something wrong on client\n");
			return -1;
		case(CSIEBOX_PROTOCOL_STATUS_MORE):
			printf("Start transfer file %s\n", syncfile);
			break;
		case -1:
			fprintf(stderr, "something wrong transfering %s\n", syncfile);
			return -1;
	}
	csiebox_protocol_file req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.datalen = statptr->st_size;
	req.message.body.pathlen = strlen(syncfile);
	req.message.body.isSlink = ((statptr->st_mode & S_IFMT) == S_IFLNK)? 1:0;
	if (!send_message(conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - file protocol\n");
		return -1;
	}
	if (!send_message(conn_fd, (void*)syncfile, strlen(syncfile))) {
		fprintf(stderr, "send fail - file filename\n");
		return -1;
	}
	if (!send_message(conn_fd, (void*)&client->offset, sizeof(long))) {
		fprintf(stderr, "send fail - file utime value\n");
		return -1;
	}
	switch(statptr->st_mode & S_IFMT){
		case S_IFREG:
			return basesendregfile(conn_fd, syncfile, statptr->st_size);
		case S_IFLNK:
			return basesendslink(conn_fd, syncfile);
	}
}

static int sendrmfile(int conn_fd, const char *rmfile){ 
	csiebox_protocol_rm req;
	memset(&req, 0, sizeof(req));
	req.message.header.req.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	req.message.header.req.op = CSIEBOX_PROTOCOL_OP_RM;
	req.message.header.req.datalen = sizeof(req) - sizeof(req.message.header);
	req.message.body.pathlen = strlen(rmfile);
	if (!send_message(conn_fd, &req, sizeof(req))) {
		fprintf(stderr, "send fail - rm protocol\n");
		return -1;
	}
	return basesendrm(conn_fd, rmfile);
}
static int sendend(int conn_fd){ 
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_REQ;
	header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_END;
	header.res.datalen = 0;
	if (!send_message(conn_fd, &header, sizeof(header))) {
		fprintf(stderr, "send fail - end protocol\n");
		return -1;
	}
	fprintf(stderr, "Sync file to client end\n");
	return getendheader(conn_fd, CSIEBOX_PROTOCOL_OP_SYNC_END);
}
