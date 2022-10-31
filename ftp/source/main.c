/*
 * Copyright (c) 2015 Sergi Granell (xerpi)
 * Copyright (c) 2016 Antonio Jose Ramos Marquez aka (bigboss) @psxdev
 */


#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <inttypes.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <prosperodev.h>
#include <debugnet.h>

dlsym_t *prosperoDlsym;



#define UNUSED(x) (void)(x)



void ftp_fini();


#define PATH_MAX 256
#define TemporaryNameSize 4096

#define NET_INIT_SIZE 1*1024*1024
#define FILE_BUF_SIZE 4*1024*1024

#define FTP_DEFAULT_PATH "/"

typedef enum {
	FTP_DATA_CONNECTION_NONE,
	FTP_DATA_CONNECTION_ACTIVE,
	FTP_DATA_CONNECTION_PASSIVE,
} DataConnectionType;

typedef struct ClientInfo {
	/* Client number */
	int num;
	/* Thread UID */
	ScePthread thid;
	/* Control connection socket FD */
	int ctrl_sockfd;
	/* Data connection attributes */
	int data_sockfd;
	DataConnectionType data_con_type;
	struct sockaddr_in data_sockaddr;
	/* PASV mode client socket */
	struct sockaddr_in pasv_sockaddr;
	int pasv_sockfd;
	/* Remote client net info */
	struct sockaddr_in addr;
	/* Receive buffer attributes */
	int n_recv;
	char recv_buffer[512];
	/* Current working directory */
	char cur_path[PATH_MAX];
	/* Rename path */
	char rename_path[PATH_MAX];
	/* Client list */
	struct ClientInfo *next;
	struct ClientInfo *prev;
} ClientInfo;

typedef void (*cmd_dispatch_func)(ClientInfo *client);

typedef struct
{
	char *cmd;
	cmd_dispatch_func func;
} cmd_dispatch_entry;
cmd_dispatch_entry cmd_dispatch_table[21];
void *net_memory = NULL;
int ftp_initialized = 0;
int ftp_active=1;
struct in_addr ps4_addr;
unsigned short int ps4_port;
ScePthread server_thid;
int server_sockfd;
int number_clients = 0;
ClientInfo *client_list = NULL;
ScePthreadMutex client_list_mtx;


#define client_send_ctrl_msg(cl, str) sceNetSend(cl->ctrl_sockfd, str, strlen(str), 0)

void client_send_data_msg(ClientInfo *client, const char *str)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		sceNetSend(client->data_sockfd, str, strlen(str), 0);
	} else {
		sceNetSend(client->pasv_sockfd, str, strlen(str), 0);
	}
}

int client_send_recv_raw(ClientInfo *client, void *buf, unsigned int len)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		return sceNetRecv(client->data_sockfd, buf, len, 0);
	} else {
		return sceNetRecv(client->pasv_sockfd, buf, len, 0);
	}
}

void client_send_data_raw(ClientInfo *client, const void *buf, unsigned int len)
{
	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		sceNetSend(client->data_sockfd, buf, len, 0);
	} else {
		sceNetSend(client->pasv_sockfd, buf, len, 0);
	}
}

void cmd_USER_func(ClientInfo *client)
{
	client_send_ctrl_msg(client, "331 Username OK, need password b0ss.\n");
}

void cmd_PASS_func(ClientInfo *client)
{
	client_send_ctrl_msg(client, "230 User logged in!\n");
}

void cmd_QUIT_func(ClientInfo *client)
{
	client_send_ctrl_msg(client, "221 Goodbye senpai :'(\n");
	ftp_active=0;
	
}

void cmd_SYST_func(ClientInfo *client)
{
	client_send_ctrl_msg(client, "215 UNIX Type: L8\n");
}

void cmd_PASV_func(ClientInfo *client)
{
	int ret;
	UNUSED(ret);

	char cmd[512];
	unsigned int namelen;
	struct sockaddr_in picked;

	/* Create data mode socket name */
	char data_socket_name[64];
	sprintf(data_socket_name, "FTPS4_client_%i_data_socket",client->num);

	/* Create the data socket */
	client->data_sockfd = sceNetSocket(data_socket_name,
		AF_INET,
		SOCK_STREAM,
		0);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] PASV data socket fd: %d\n", client->data_sockfd);

	/* Fill the data socket address */
	client->data_sockaddr.sin_len = sizeof(client->data_sockaddr);
	client->data_sockaddr.sin_family = AF_INET;
	client->data_sockaddr.sin_addr.s_addr = sceNetHtonl(INADDR_ANY);
	/* Let the PS4 choose a port */
	client->data_sockaddr.sin_port = sceNetHtons(0);

	/* Bind the data socket address to the data socket */
	ret = sceNetBind(client->data_sockfd,
		(struct sockaddr *)&client->data_sockaddr,
		sizeof(client->data_sockaddr));
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] sceNetBind(): 0x%08X\n", ret);

	/* Start listening */
	ret = sceNetListen(client->data_sockfd, 128);
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] sceNetListen(): 0x%08X\n", ret);

	/* Get the port that the PS4 has chosen */
	namelen = sizeof(picked);
	sceNetGetsockname(client->data_sockfd, (struct sockaddr *)&picked,
		&namelen);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] PASV mode port: 0x%04X\n", picked.sin_port);

	/* Build the command */
	sprintf(cmd, "227 Entering Passive Mode (%hhu,%hhu,%hhu,%hhu,%hhu,%hhu)\n",
		(ps4_addr.s_addr >> 0) & 0xFF,
		(ps4_addr.s_addr >> 8) & 0xFF,
		(ps4_addr.s_addr >> 16) & 0xFF,
		(ps4_addr.s_addr >> 24) & 0xFF,
		(picked.sin_port >> 0) & 0xFF,
		(picked.sin_port >> 8) & 0xFF);

	client_send_ctrl_msg(client, cmd);

	/* Set the data connection type to passive! */
	client->data_con_type = FTP_DATA_CONNECTION_PASSIVE;
}

void cmd_PORT_func(ClientInfo *client)
{
	unsigned char data_ip[4];
	unsigned char porthi, portlo;
	unsigned short data_port;
	char ip_str[16];
	struct in_addr data_addr;

	sscanf(client->recv_buffer, "%*s %hhu,%hhu,%hhu,%hhu,%hhu,%hhu",
		&data_ip[0], &data_ip[1], &data_ip[2], &data_ip[3],
		&porthi, &portlo);

	data_port = portlo + porthi*256;

	/* Convert to an X.X.X.X IP string */
	sprintf(ip_str, "%d.%d.%d.%d",
		data_ip[0], data_ip[1], data_ip[2], data_ip[3]);

	/* Convert the IP to a struct in_addr */
	sceNetInetPton(AF_INET, ip_str, &data_addr);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] PORT connection to client's IP: %s Port: %d\n", ip_str, data_port);

	/* Create data mode socket name */
	char data_socket_name[64];
	sprintf(data_socket_name, "FTPS4_client_%i_data_socket",
		client->num);

	/* Create data mode socket */
	client->data_sockfd = sceNetSocket(data_socket_name,
		AF_INET,
		SOCK_STREAM,
		0);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Client %i data socket fd: %d\n", client->num,
		client->data_sockfd);

	/* Prepare socket address for the data connection */
	client->data_sockaddr.sin_len = sizeof(client->data_sockaddr);
	client->data_sockaddr.sin_family = AF_INET;
	client->data_sockaddr.sin_addr = data_addr;
	client->data_sockaddr.sin_port = sceNetHtons(data_port);

	/* Set the data connection type to active! */
	client->data_con_type = FTP_DATA_CONNECTION_ACTIVE;

	client_send_ctrl_msg(client, "200 PORT command successful!\n");
}

void client_open_data_connection(ClientInfo *client)
{
	int ret;
	UNUSED(ret);

	unsigned int addrlen;

	if (client->data_con_type == FTP_DATA_CONNECTION_ACTIVE) {
		/* Connect to the client using the data socket */
		ret = sceNetConnect(client->data_sockfd,
			(struct sockaddr *)&client->data_sockaddr,
			sizeof(client->data_sockaddr));

		debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] sceNetConnect(): 0x%08X\n", ret);
	} else {
		/* Listen to the client using the data socket */
		addrlen = sizeof(client->pasv_sockaddr);
		client->pasv_sockfd = sceNetAccept(client->data_sockfd,
			(struct sockaddr *)&client->pasv_sockaddr,
			&addrlen);
		debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] PASV client fd: 0x%08X\n", client->pasv_sockfd);
	}
}

void client_close_data_connection(ClientInfo *client)
{
	sceNetSocketClose(client->data_sockfd);
	/* In passive mode we have to close the client pasv socket too */
	if (client->data_con_type == FTP_DATA_CONNECTION_PASSIVE) {
		sceNetSocketClose(client->pasv_sockfd);
	}
	client->data_con_type = FTP_DATA_CONNECTION_NONE;
}

char file_type_char(mode_t mode)
{
	return S_ISBLK(mode) ? 'b' :
		S_ISCHR(mode) ? 'c' :
		S_ISREG(mode) ? '-' :
		S_ISDIR(mode) ? 'd' :
		S_ISFIFO(mode) ? 'p' :
		S_ISSOCK(mode) ? 's' :
		S_ISLNK(mode) ? 'l' : ' ';
}

int gen_list_format(char *out, int n, mode_t mode, unsigned int file_size,
	int month_n, int day_n, int hour, int minute, const char *filename)
{
	char num_to_month[][4] = {
		"Jan", "Feb", "Mar", "Apr", "May", "Jun",
		"Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
	};

	return snprintf(out, n,
		"%c%s 1 ps5 ps5 %d %s %-2d %02d:%02d %s\r\n",
		file_type_char(mode),
		S_ISDIR(mode) ? "rwxr-xr-x" : "rw-r--r--",
		file_size,
		num_to_month[month_n%12],
		day_n,
		hour,
		minute,
		filename);
}

void send_LIST(ClientInfo *client, const char *path)
{
	char buffer[512];
	char *dentbuf;
	struct dirent *dent;
	int dfd;
	struct stat st;
	struct tm * tm;
	int i;
	char *temporaryName;
	
	temporaryName = malloc(TemporaryNameSize);
	if(temporaryName == NULL)
	{
		debugNetPrintf(DEBUGNET_ERROR,"error calling malloc\n");
		return;
	}

	dfd = open(path, O_RDONLY, 0);
	if (dfd < 0) {
		client_send_ctrl_msg(client, "550 Invalid directory.\n");
		return;
	}

	//memset(dentbuf, 0, sizeof(dentbuf));

	client_send_ctrl_msg(client, "150 Opening ASCII mode data transfer for LIST.\n");
	client_open_data_connection(client);


	int err=fstat(dfd, &st);
	if(err<0)
	{
		debugNetPrintf(DEBUGNET_DEBUG, "fstat error return  0x%08X \n",err);
		return;
	}
	dentbuf=mmap(NULL, st.st_blksize+sizeof(struct dirent), PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (dentbuf)
	{
		// Make sure we will have a null terminated entry at the end.Thanks people leaving CryEngine code for orbis on github  :)
		for(i=0;i<st.st_blksize+sizeof(struct dirent);i++)
		{
			dentbuf[i]=0;
		}
		err=getdents(dfd, dentbuf, st.st_blksize);

		int nOffset = err;
		while (err > 0 && err < st.st_blksize)
		{
			err = getdents(dfd, dentbuf + nOffset, st.st_blksize-nOffset);
			nOffset += err;
		}
		
		if (err>0)
			err=0;
		
		
		
		
		
		
		
		dent = (struct dirent *)dentbuf;
		while(dent->d_fileno ) {
			
			strcpy(temporaryName, path);
			size_t l = strlen(path);
			if(l > 0 && path[l - 1] != '/')
			{
				strcat(temporaryName, "/");
			}
			strcat(temporaryName, dent->d_name);
			
			err=stat(temporaryName, &st);
			if ( err== 0) 
			{
				
				 tm = localtime(&(st.st_ctim));
				 
				 gen_list_format(buffer, sizeof(buffer),
					st.st_mode,
					st.st_size,
					tm->tm_mon,
					tm->tm_mday,
					tm->tm_hour,
					tm->tm_min,
					dent->d_name);
				
				client_send_data_msg(client, buffer);
			}
			else
			{
				debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] error stat %d %s\n",err,dent->d_name);
			}
			
			dent = (struct dirent *)((void *)dent + dent->d_reclen);
				
			memset(buffer, 0, sizeof(buffer));
				
		}
	}
	munmap(dentbuf,st.st_blksize+sizeof(struct dirent));
	free(temporaryName);


	

	close(dfd);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Done sending LIST\n");

	client_close_data_connection(client);
	client_send_ctrl_msg(client, "226 Transfer complete.\n");
}


void cmd_LIST_func(ClientInfo *client)
{
	char list_path[PATH_MAX];

	int n = sscanf(client->recv_buffer, "%*s %[^\r\n\t]", list_path);

	if (n > 0) {  /* Client specified a path */
		send_LIST(client, list_path);
	} else {      /* Use current path */
		send_LIST(client, client->cur_path);
	}
}

void cmd_PWD_func(ClientInfo *client)
{
	char msg[PATH_MAX];
	sprintf(msg, "257 \"%s\" is the current directory.\n", client->cur_path);
	client_send_ctrl_msg(client, msg);
}

void cmd_CWD_func(ClientInfo *client)
{
	char cmd_path[PATH_MAX];
	char path[PATH_MAX];
	int pd;
	int n = sscanf(client->recv_buffer, "%*s %[^\r\n\t]", cmd_path);

	if (n < 1) {
		client_send_ctrl_msg(client, "500 Syntax error, command unrecognized.\n");
	} else {
		if (cmd_path[0] != '/') { /* Change dir relative to current dir */
			sprintf(path, "%s%s", client->cur_path, cmd_path);
		} else {
			strcpy(path, cmd_path);
		}

		/* If there isn't "/" at the end, add it */
		if (path[strlen(path) - 1] != '/') {
			strcat(path, "/");
		}

		/* Check if the path exists */
		pd = open(path, O_RDONLY, 0);
		if (pd < 0) {
			client_send_ctrl_msg(client, "550 Invalid directory.\n");
			return;
		}
		close(pd);

		strcpy(client->cur_path, path);
		client_send_ctrl_msg(client, "250 Requested file action okay, completed.\n");
	}
}

void cmd_TYPE_func(ClientInfo *client)
{
	char data_type;
	char format_control[8];
	int n_args = sscanf(client->recv_buffer, "%*s %c %s", &data_type, format_control);

	if (n_args > 0) {
		switch(data_type) {
		case 'A':
		case 'I':
			client_send_ctrl_msg(client, "200 Okay\n");
			break;
		case 'E':
		case 'L':
		default:
			client_send_ctrl_msg(client, "504 Error: bad parameters?\n");
			break;
		}
	} else {
		client_send_ctrl_msg(client, "504 Error: bad parameters?\n");
	}
}

void dir_up(char *path)
{
	char *pch;
	size_t len_in = strlen(path);
	if (len_in == 1) {
		strcpy(path, "/");
		return;
	}
	if (path[len_in - 1] == '/') {
		path[len_in - 1] = '\0';
	}
	pch = strrchr(path, '/');
	if (pch) {
		size_t s = len_in - (pch - path);
		memset(pch + 1, '\0', s);
	}
}

void cmd_CDUP_func(ClientInfo *client)
{
	dir_up(client->cur_path);
	client_send_ctrl_msg(client, "200 Command okay.\n");
}

void send_file(ClientInfo *client, const char *path)
{
	unsigned char *buffer;
	int fd;
	unsigned int bytes_read;

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Opening: %s\n", path);

	if ((fd = open(path, O_RDONLY, 0)) >= 0) {

		buffer = malloc(FILE_BUF_SIZE);
		if (buffer == NULL) {
			client_send_ctrl_msg(client, "550 Could not allocate memory.\n");
			return;
		}

		client_open_data_connection(client);
		client_send_ctrl_msg(client, "150 Opening Image mode data transfer.\n");

		while ((bytes_read = read(fd, buffer, FILE_BUF_SIZE)) > 0) {
			client_send_data_raw(client, buffer, bytes_read);
		}

		close(fd);
		free(buffer);
		client_send_ctrl_msg(client, "226 Transfer completed.\n");
		client_close_data_connection(client);

	} else {
		client_send_ctrl_msg(client, "550 File not found.\n");
	}
}

/* This function generates a PS4 valid path with the input path
 * from RETR, STOR, DELE, RMD, MKD, RNFR and RNTO commands */
void gen_filepath(ClientInfo *client, char *dest_path)
{
	char cmd_path[PATH_MAX];
	sscanf(client->recv_buffer, "%*[^ ] %[^\r\n\t]", cmd_path);

	if (cmd_path[0] != '/') { /* The file is relative to current dir */
		/* Append the file to the current path */
		sprintf(dest_path, "%s%s", client->cur_path, cmd_path);
	} else {
		strcpy(dest_path, cmd_path);
	}
}

void cmd_RETR_func(ClientInfo *client)
{
	char dest_path[PATH_MAX];
	gen_filepath(client, dest_path);
	send_file(client, dest_path);
}

void receive_file(ClientInfo *client, const char *path)
{
	unsigned char *buffer;
	int fd;
	unsigned int bytes_recv;

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Opening: %s\n", path);

	if ((fd = open(path, O_CREAT | O_WRONLY | O_TRUNC, 0777)) >= 0) {

		buffer = malloc(FILE_BUF_SIZE);
		if (buffer == NULL) {
			client_send_ctrl_msg(client, "550 Could not allocate memory.\n");
			return;
		}

		client_open_data_connection(client);
		client_send_ctrl_msg(client, "150 Opening Image mode data transfer.\n");

		while ((bytes_recv = client_send_recv_raw(client, buffer, FILE_BUF_SIZE)) > 0) {
			write(fd, buffer, bytes_recv);
		}

		close(fd);
		free(buffer);
		client_send_ctrl_msg(client, "226 Transfer completed.\n");
		client_close_data_connection(client);

	} else {
		client_send_ctrl_msg(client, "550 File not found.\n");
	}
}

void cmd_STOR_func(ClientInfo *client)
{
	char dest_path[PATH_MAX];
	gen_filepath(client, dest_path);
	receive_file(client, dest_path);
}

void delete_file(ClientInfo *client, const char *path)
{
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Deleting: %s\n", path);

	if (unlink(path) >= 0) {
		client_send_ctrl_msg(client, "226 File deleted.\n");
	} else {
		client_send_ctrl_msg(client, "550 Could not delete the file.\n");
	}
}

void cmd_DELE_func(ClientInfo *client)
{
	char dest_path[PATH_MAX];
	gen_filepath(client, dest_path);
	delete_file(client, dest_path);
}

void delete_dir(ClientInfo *client, const char *path)
{
	int ret;
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Deleting: %s\n", path);
	ret = rmdir(path);
	if (ret >= 0) {
		client_send_ctrl_msg(client, "226 Directory deleted.\n");
	} else if (ret == 0x8001005A) { /* DIRECTORY_IS_NOT_EMPTY */
		client_send_ctrl_msg(client, "550 Directory is not empty.\n");
	} else {
		client_send_ctrl_msg(client, "550 Could not delete the directory.\n");
	}
}

void cmd_RMD_func(ClientInfo *client)
{
	char dest_path[PATH_MAX];
	gen_filepath(client, dest_path);
	delete_dir(client, dest_path);
}

void create_dir(ClientInfo *client, const char *path)
{
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Creating: %s\n", path);

	if (mkdir(path, 0777) >= 0) {
		client_send_ctrl_msg(client, "226 Directory created.\n");
	} else {
		client_send_ctrl_msg(client, "550 Could not create the directory.\n");
	}
}

void cmd_MKD_func(ClientInfo *client)
{
	char dest_path[PATH_MAX];
	gen_filepath(client, dest_path);
	create_dir(client, dest_path);
}

int file_exists(const char *path)
{
	struct stat s;
	return (stat(path, &s) >= 0);
}

void cmd_RNFR_func(ClientInfo *client)
{
	char from_path[PATH_MAX];
	/* Get the origin filename */
	gen_filepath(client, from_path);

	/* Check if the file exists */
	if (!file_exists(from_path)) {
		client_send_ctrl_msg(client, "550 The file doesn't exist.\n");
		return;
	}
	/* The file to be renamed is the received path */
	strcpy(client->rename_path, from_path);
	client_send_ctrl_msg(client, "250 I need the destination name b0ss.\n");
}

void cmd_RNTO_func(ClientInfo *client)
{
	char path_to[PATH_MAX];
	/* Get the destination filename */
	gen_filepath(client, path_to);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Renaming: %s to %s\n", client->rename_path, path_to);

	if (rename(client->rename_path, path_to) < 0) {
		client_send_ctrl_msg(client, "550 Error renaming the file.\n");
	}

	client_send_ctrl_msg(client, "226 Rename completed.\n");
}

void cmd_SIZE_func(ClientInfo *client)
{
	struct stat s;
	char path[PATH_MAX];
	char cmd[64];
	/* Get the filename to retrieve its size */
	gen_filepath(client, path);

	/* Check if the file exists */
	if (stat(path, &s) < 0) {
		client_send_ctrl_msg(client, "550 The file doesn't exist.\n");
		return;
	}
	/* Send the size of the file */
	sprintf(cmd, "213: %lld\n", s.st_size);
	client_send_ctrl_msg(client, cmd);
}

/*#define add_entry(name) {#name, cmd_##name##_func}
cmd_dispatch_entry cmd_dispatch_table[20] =
{
	{"USER",cmd_USER_func},
	add_entry(PASS),
	add_entry(QUIT),
	add_entry(SYST),
	add_entry(PASV),
	add_entry(PORT),
	add_entry(LIST),
	add_entry(PWD),
	add_entry(CWD),
	add_entry(TYPE),
	add_entry(CDUP),
	add_entry(RETR),
	add_entry(STOR),
	add_entry(DELE),
	add_entry(RMD),
	add_entry(MKD),
	add_entry(RNFR),
	add_entry(RNTO),
	add_entry(SIZE),
	{NULL, NULL}
};*/
void populateCmd()
{
	cmd_dispatch_table[0].cmd="USER";
	cmd_dispatch_table[0].func=cmd_USER_func;
	cmd_dispatch_table[1].cmd="PASS";
	cmd_dispatch_table[1].func=cmd_PASS_func;
	cmd_dispatch_table[2].cmd="QUIT";
	cmd_dispatch_table[2].func=cmd_QUIT_func;
	cmd_dispatch_table[3].cmd="SYST";
	cmd_dispatch_table[3].func=cmd_SYST_func;
	cmd_dispatch_table[4].cmd="PASV";
	cmd_dispatch_table[4].func=cmd_PASV_func;
	cmd_dispatch_table[5].cmd="PORT";
	cmd_dispatch_table[5].func=cmd_PORT_func;
	cmd_dispatch_table[6].cmd="LIST";
	cmd_dispatch_table[6].func=cmd_LIST_func;
	cmd_dispatch_table[7].cmd="PWD";
	cmd_dispatch_table[7].func=cmd_PWD_func;
	cmd_dispatch_table[8].cmd="CWD";
	cmd_dispatch_table[8].func=cmd_CWD_func;
	cmd_dispatch_table[9].cmd="TYPE";
	cmd_dispatch_table[9].func=cmd_TYPE_func;
	cmd_dispatch_table[10].cmd="CDUP";
	cmd_dispatch_table[10].func=cmd_CDUP_func;
	cmd_dispatch_table[11].cmd="RETR";
	cmd_dispatch_table[11].func=cmd_RETR_func;
	cmd_dispatch_table[12].cmd="STOR";
	cmd_dispatch_table[12].func=cmd_STOR_func;
	cmd_dispatch_table[13].cmd="DELE";
	cmd_dispatch_table[13].func=cmd_DELE_func;
	cmd_dispatch_table[14].cmd="RMD";
	cmd_dispatch_table[14].func=cmd_RMD_func;
	cmd_dispatch_table[15].cmd="MKD";
	cmd_dispatch_table[15].func=cmd_MKD_func;
	cmd_dispatch_table[16].cmd="RNFR";
	cmd_dispatch_table[16].func=cmd_RNFR_func;
	cmd_dispatch_table[17].cmd="RNTO";
	cmd_dispatch_table[18].func=cmd_RNTO_func;
	cmd_dispatch_table[19].cmd="SIZE";
	cmd_dispatch_table[19].func=cmd_SIZE_func;
	cmd_dispatch_table[20].cmd=NULL;
	cmd_dispatch_table[20].func=NULL;
}
cmd_dispatch_func get_dispatch_func(const char *cmd)
{

	int i;
	for(i = 0; cmd_dispatch_table[i].cmd && cmd_dispatch_table[i].func; i++) {
		if (strcmp(cmd, cmd_dispatch_table[i].cmd) == 0) {
			return cmd_dispatch_table[i].func;
		}
	}
	return NULL;
}



void client_list_add(ClientInfo *client)
{
	/* Add the client at the front of the client list */
	scePthreadMutexLock(&client_list_mtx);

	if (client_list == NULL) { /* List is empty */
		client_list = client;
		client->prev = NULL;
		client->next = NULL;
	} else {
		client->next = client_list;
		client->next->prev = client;
		client->prev = NULL;
		client_list = client;
	}

	scePthreadMutexUnlock(&client_list_mtx);
}

void client_list_delete(ClientInfo *client)
{
	/* Remove the client from the client list */
	scePthreadMutexLock(&client_list_mtx);

	if (client->prev) {
		client->prev->next = client->next;
	}
	if (client->next) {
		client->next->prev = client->prev;
	}

	scePthreadMutexUnlock(&client_list_mtx);
}

void client_list_close_sockets()
{
	/* Iterate over the client list and close their sockets */
	scePthreadMutexLock(&client_list_mtx);

	ClientInfo *it = client_list;

	while (it) {
		sceNetSocketClose(it->ctrl_sockfd);
		it = it->next;
	}

	scePthreadMutexUnlock(&client_list_mtx);
}

void *client_thread(void *arg)
{
	char cmd[16];
	cmd_dispatch_func dispatch_func;
	ClientInfo *client = (ClientInfo *)arg;

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Client thread %i started!\n", client->num);

	client_send_ctrl_msg(client, "220 FTPS4 Server ready.\n");

	while (ftp_active) {

		memset(client->recv_buffer, 0, sizeof(client->recv_buffer));

		client->n_recv = sceNetRecv(client->ctrl_sockfd, client->recv_buffer, sizeof(client->recv_buffer), 0);
		if (client->n_recv > 0) {
			debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Received %i bytes from client number %i:\n",
				client->n_recv, client->num);

			debugNetPrintf(DEBUGNET_INFO,"\t%i> %s", client->num, client->recv_buffer);

			/* The command are the first chars until the first space */
			sscanf(client->recv_buffer, "%s", cmd);

			/* Wait 1 ms before sending any data */
			sceKernelUsleep(1*1000);

			if ((dispatch_func = get_dispatch_func(cmd))) {
				dispatch_func(client);
			} else {
				client_send_ctrl_msg(client, "502 Sorry, command not implemented. :(\n");
			}

		} else if (client->n_recv == 0) {
			/* Value 0 means connection closed by the remote peer */
			debugNetPrintf(DEBUGNET_INFO,"Connection closed by the client %i.\n", client->num);
			/* Close the client's socket */
			sceNetSocketClose(client->ctrl_sockfd);
			/* Delete itself from the client list */
			client_list_delete(client);
			break;
		} else {
			/* A negative value means error */
			break;
		}
	}

	/* If there's an open data connection, close it */
	if (client->data_con_type != FTP_DATA_CONNECTION_NONE) {
		sceNetSocketClose(client->data_sockfd);
		if (client->data_con_type == FTP_DATA_CONNECTION_PASSIVE) {
			sceNetSocketClose(client->pasv_sockfd);
		}
	}

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Client thread %i exiting!\n", client->num);

	free(client);

	//scePthreadExit(NULL);
	return NULL;
}

void *server_thread(void *arg)
{
	int ret;
	UNUSED(ret);

	struct sockaddr_in serveraddr;

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Server thread started!\n");

	/* Create server socket */
	server_sockfd = sceNetSocket("FTPS4_server_sock",
		AF_INET,
		SOCK_STREAM,
		0);

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Server socket fd: %d\n", server_sockfd);

	/* Fill the server's address */
	serveraddr.sin_len = sizeof(serveraddr);
	serveraddr.sin_family = AF_INET;
	serveraddr.sin_addr.s_addr = sceNetHtonl(INADDR_ANY);
	serveraddr.sin_port = sceNetHtons(ps4_port);

	/* Bind the server's address to the socket */
	ret = sceNetBind(server_sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr));
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] sceNetBind(): 0x%08X\n", ret);

	/* Start listening */
	ret = sceNetListen(server_sockfd, 128);
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] sceNetListen(): 0x%08X\n", ret);
	ftp_active=1;
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] ftp_active: 0x%08X\n", ftp_active);

	while (ftp_active) {

		/* Accept clients */
		struct sockaddr_in clientaddr;
		int client_sockfd;
		unsigned int addrlen = sizeof(clientaddr);

		debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Waiting for incoming connections on port: %d...\n", ps4_port);

		client_sockfd = sceNetAccept(server_sockfd, (struct sockaddr *)&clientaddr, &addrlen);
		if (client_sockfd >= 0) {

			debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] New connection, client fd: 0x%08X\n", client_sockfd);

			/* Get the client's IP address */
			char remote_ip[16];
			sceNetInetNtop(AF_INET,
				&clientaddr.sin_addr.s_addr,
				remote_ip,
				sizeof(remote_ip));

			debugNetPrintf(DEBUGNET_INFO,"Client %i connected, IP: %s port: %i\n",
				number_clients, remote_ip, clientaddr.sin_port);

			/* Allocate the ClientInfo struct for the new client */
			ClientInfo *client = malloc(sizeof(*client));
			client->num = number_clients;
			client->ctrl_sockfd = client_sockfd;
			client->data_con_type = FTP_DATA_CONNECTION_NONE;
			strcpy(client->cur_path, FTP_DEFAULT_PATH);
			memcpy(&client->addr, &clientaddr, sizeof(client->addr));

			/* Add the new client to the client list */
			client_list_add(client);

			/* Create a new thread for the client */
			char client_thread_name[64];
			sprintf(client_thread_name, "FTPS4_client_%i_thread",
				number_clients);

			/* Create a new thread for the client */
			scePthreadCreate(&client->thid, NULL, client_thread, client, client_thread_name);

			debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Client %i thread UID: 0x%08X\n", number_clients, client->thid);

			number_clients++;
		} else {
			/* if sceNetAccept returns < 0, it means that the listening
			 * socket has been closed, this means that we want to
			 * finish the server thread */
			debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Server socket closed, 0x%08X\n", client_sockfd);
			break;
		}
	}

	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Server thread exiting!\n");
	
	return NULL;
}

void ftp_init(const char *ip, unsigned short int port)
{
	int ret;
	UNUSED(ret);

	//SceNetInitParam initparam;
	//SceNetCtlInfo info;

	if (ftp_initialized) {
		return;
	}

	/* Init Net */
	/*if (sceNetShowNetstat() == PSP2_NET_DEBUGNET_ERROR_ENOTINIT) {
		net_memory = malloc(NET_INIT_SIZE);
		initparam.memory = net_memory;
		initparam.size = NET_INIT_SIZE;
		initparam.flags = 0;
		ret = sceNetInit(&initparam);
		DEBUGNET_DEBUG("sceNetInit(): 0x%08X\n", ret);
	} else {
		DEBUGNET_DEBUG("Net is already initialized.\n");
	}*/

	/* Init NetCtl */
	//ret = sceNetCtlInit();
	//DEBUGNET_DEBUG("sceNetCtlInit(): 0x%08X\n", ret);

	/* Get IP address */
	//ret = sceNetCtlInetGetInfo(PSP2_NETCTL_DEBUGNET_INFO_GET_IP_ADDRESS, &info);
	//DEBUGNET_DEBUG("sceNetCtlInetGetInfo(): 0x%08X\n", ret);

	/* Return data */
	//strcpy(vita_ip, info.ip_address);
	//*vita_port = FTP_PORT;

	/* Save the listening port of the PS4 to a global variable */
	ps4_port = port;

	/* Save the IP of the PS4 to a global variable */
	sceNetInetPton(AF_INET, ip, &ps4_addr);

	/* Create the client list mutex */
	scePthreadMutexInit(&client_list_mtx, NULL, "FTPS4_client_list_mutex");
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Client list mutex UID: 0x%08X\n", client_list_mtx);

	/* Create server thread */
	scePthreadCreate(&server_thid, NULL, server_thread, NULL, "FTPS4_server_thread");
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] Server thread UID: 0x%08X\n", server_thid);

	ftp_initialized = 1;
	
	scePthreadJoin(server_thid, NULL);
	
	
}

void ftp_fini()
{
	if (ftp_initialized) {
		/* In order to "stop" the blocking sceNetAccept,
		 * we have to close the server socket; this way
		 * the accept call will return an error */
		sceNetSocketClose(server_sockfd);
		//sceNetSocketAbort(server_sockfd,1);
		/* To close the clients we have to do the same:
		 * we have to iterate over all the clients
		 * and close their sockets */
		client_list_close_sockets();
		client_list = NULL;

		/* UGLY: Give 50 ms for the threads to exit */
		sceKernelUsleep(50*1000);

		/* Delete the client list mutex */
		scePthreadMutexDestroy(client_list_mtx);

		//sceNetCtlTerm();
		//sceNetTerm();

		

		ftp_initialized = 0;
	}
}
int initApp()
{
	libprospero_init();
	debugNetInit("192.168.1.12",18194,3);
	ftp_init("0.0.0.0",1337);
	return 0;
}

void finishApp()
{
	//prosperoPadFinish();
	debugNetPrintf(DEBUGNET_DEBUG,"[PROSPEROFTP] calling ftp_fini\n");
	ftp_fini();
	debugNetFinish();
}
int payload_main(struct payload_args *args)
{
	if(args==NULL)
	{
		return -1;
	}
	prosperoDlsym=args->dlsym;
	populateCmd();
	initApp();


		
	//we finish this connecting to ftp and calling quit command. It will call ftp_fini
	//while(ftp_active)
	//{
	//	sceKernelUsleep(100 * 1000);
	//}
	
	return EXIT_SUCCESS;
}
