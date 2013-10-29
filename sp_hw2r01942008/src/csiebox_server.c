#include "csiebox_server.h"

#include "csiebox_common.h"
#include "connect.h"

#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <stdlib.h>

static int parse_arg(csiebox_server* server, int argc, char** argv);
static void handle_request(csiebox_server* server, int conn_fd);
static int get_account_info(
  csiebox_server* server,  const char* user, csiebox_account_info* info);
static void login(
  csiebox_server* server, int conn_fd, csiebox_protocol_login* login);
static void logout(csiebox_server* server, int conn_fd);
static char* get_user_homedir(
  csiebox_server* server, csiebox_client_info* info);
void gen_fullpath(char* fullpath, char* localpath, int length );
static void checkmeta(
	csiebox_server* server, int conn_fd, csiebox_protocol_meta* rm);
static void getfile(
	csiebox_server* server, int conn_fd, csiebox_protocol_file* rm);
static void sethlink(
	csiebox_server* server, int conn_fd, csiebox_protocol_hardlink* rm);
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
  int conn_fd, conn_len;
  struct sockaddr_in addr;
  while (1) {
    memset(&addr, 0, sizeof(addr));
    conn_len = 0;
    // waiting client connect
    conn_fd = accept(
      server->listen_fd, (struct sockaddr*)&addr, (socklen_t*)&conn_len);
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
    // handle request from connected socket fd
    handle_request(server, conn_fd);
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
    fprintf(stderr, "config (%d, %s)=(%d, %s)\n", keylen, key, vallen, val);
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
  while (recv_message(conn_fd, &header, sizeof(header))) {
    if (header.req.magic != CSIEBOX_PROTOCOL_MAGIC_REQ) {
      continue;
    }
    switch (header.req.op) {
      case CSIEBOX_PROTOCOL_OP_LOGIN:
        fprintf(stderr, "login\n");
        csiebox_protocol_login req;
        if (complete_message_with_header(conn_fd, &header, &req)) {
          login(server, conn_fd, &req);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_META:
        fprintf(stderr, "sync meta\n");
        csiebox_protocol_meta meta;
        if (complete_message_with_header(conn_fd, &header, &meta)) {
			checkmeta(server, conn_fd, &meta);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_FILE:
        fprintf(stderr, "sync file\n");
        csiebox_protocol_file file;
        if (complete_message_with_header(conn_fd, &header, &file)) {
			getfile(server, conn_fd, &file);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK:
        fprintf(stderr, "sync hardlink\n");
        csiebox_protocol_hardlink hardlink;
        if (complete_message_with_header(conn_fd, &header, &hardlink)) {
			sethlink(server, conn_fd, &hardlink);
        }
        break;
      case CSIEBOX_PROTOCOL_OP_SYNC_END:
        fprintf(stderr, "sync end\n");
        csiebox_protocol_header end;
        // TODO
        break;
      case CSIEBOX_PROTOCOL_OP_RM:
        fprintf(stderr, "rm\n");
        csiebox_protocol_rm rm;
        if (complete_message_with_header(conn_fd, &header, &rm)) {
			removefile(server, conn_fd, &rm);
        }

        break;
      default:
        fprintf(stderr, "unknow op %x\n", header.req.op);
        break;
    }
  }
  fprintf(stderr, "end of connection\n");
  logout(server, conn_fd);
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
static void login(
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

  csiebox_protocol_header header;
  memset(&header, 0, sizeof(header));
  header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
  header.res.op = CSIEBOX_PROTOCOL_OP_LOGIN;
  header.res.datalen = 0;
  if (succ) {
    if (server->client[conn_fd]) {
      free(server->client[conn_fd]);
    }
    info->conn_fd = conn_fd;
    server->client[conn_fd] = info;
    header.res.status = CSIEBOX_PROTOCOL_STATUS_OK;
    header.res.client_id = info->conn_fd;
    char* homedir = get_user_homedir(server, info);
    mkdir(homedir, DIR_S_FLAG);
    free(homedir);
  } else {
    header.res.status = CSIEBOX_PROTOCOL_STATUS_FAIL;
    free(info);
  }
  send_message(conn_fd, &header, sizeof(header));
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
  sprintf(ret, "%s/%s", server->arg.path, info->account.user);
  return ret;
}

void gen_fullpath(char* fullpath, char* localpath, int length) {
	localpath[length] = '\0';
	if (localpath[0] == '.') {
		memmove(localpath, localpath+1, strlen(localpath)); //remove first .
	}
	strncat(fullpath, localpath, strlen(localpath));
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
	gen_fullpath(fullpath, filepath, length);

	//checkmeta, if is directory, just call mkdir. file then compare hash
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
			struct stat statbuf;
			lstat(fullpath, &statbuf);
			memcpy(&statbuf, &meta->message.body.stat, sizeof(struct stat));
			fprintf(stderr, "md5 is identical\n");
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
	int client_id = file->message.header.req.client_id;

	//get home directory from client_id
	info = server->client[client_id];

    char* fullpath = get_user_homedir(server, info);
	char* filepath = (char*)malloc(length);
	recv_message(conn_fd, filepath, length);
	gen_fullpath(fullpath, filepath, length);

	//get file, here is using some dangerous mechanism
	int succ = 1;
	FILE* writefile= fopen(fullpath, "w");
	if (writefile == NULL) {
		fprintf(stderr, "cannot open writefile\n");
		succ = 0;
	}
	char* buffer = (char*)malloc(sizeof(char)*BUFFER_SIZE);
	if (buffer == NULL) {
		fprintf(stderr, "cannot allocate write memory\n");
		succ = 0;
	}

	while (succ && (filesize != 0)) {
		if (!recv_message(conn_fd, buffer, filesize % BUFFER_SIZE)) {
			fprintf(stderr, "Something wrong during file transfer\n");
			succ = 0;
			break;
		}
		fwrite( buffer, 1, filesize%BUFFER_SIZE, writefile);
		filesize -= filesize%BUFFER_SIZE;
	}
	fclose(writefile);

	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
	header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_FILE;
	header.res.datalen = 0;
	header.res.status = (succ)? CSIEBOX_PROTOCOL_STATUS_OK: CSIEBOX_PROTOCOL_STATUS_FAIL;
	send_message(conn_fd, &header, sizeof(header));

	free(fullpath);
	free(filepath);
}

static void sethlink(
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
	gen_fullpath(srcpath, filepath, srclen);
	recv_message(conn_fd, filepath, targetlen);
	gen_fullpath(targetpath, filepath, targetlen);
	
	//create hardlink
	if ((link(srcpath, targetpath) != 0)) {
		succ = 0;
	}

	//return protocol
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
	header.res.op = CSIEBOX_PROTOCOL_OP_SYNC_HARDLINK;
	header.res.datalen = 0;
	header.res.status = (succ)? CSIEBOX_PROTOCOL_STATUS_OK: CSIEBOX_PROTOCOL_STATUS_FAIL;
	send_message(conn_fd, &header, sizeof(header));

	free(srcpath);
	free(targetpath);
	free(filepath);
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
	gen_fullpath(fullpath, filepath, pathlen);
	
	//create hardlink
	if ((unlink(fullpath) != 0)) {
		succ = 0;
	}

	//return protocol
	csiebox_protocol_header header;
	memset(&header, 0, sizeof(header));
	header.res.magic = CSIEBOX_PROTOCOL_MAGIC_RES;
	header.res.op = CSIEBOX_PROTOCOL_OP_RM;
	header.res.datalen = 0;
	header.res.status = (succ)? CSIEBOX_PROTOCOL_STATUS_OK: CSIEBOX_PROTOCOL_STATUS_FAIL;
	send_message(conn_fd, &header, sizeof(header));

	free(fullpath);
	free(filepath);
}
