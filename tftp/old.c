/*	TFTP server created by Massimo Giardina 16/08/2019
	Implementing RFC 1350. (https://tools.ietf.org/html/rfc1350)
	Using MIT license
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <arpa/inet.h>

#define PATH		"tftp/"
#define PORT 		69

#define MAXRETRIES	6
#define TIMEOUT		5

#define NS_BUF		512
#define NS_OPCODE       2
#define NS_ACK          2
#define NS_BLOCK	2

#define NS_MAX		NS_BUF+NS_BLOCK+NS_OPCODE

// OPCODE
// array of pointers to functions
// where the index is the opcode
typedef enum opc {	// HOLY SHIT STRUCT, UNIUM AND ENUM ._.
	/*
	2 bytes OPCODE + ...
	*/
	/*
	+ string filename + 2 bytes 0 + string Mode + 1 byte 0
	MODE: netascii, octet, mail..
		^ASCII	^ Binary
	*/
	RRQ 	= 1,
	WRQ 	= 2,
	/*
	+ 2 bytes block # + n bytes (max 512b)
	*/
	DATA 	= 3,
	/*
	+ 2 bytes block #
	*/
	ACK  	= 4,
	/*
	+ 2 bytes ErrorCode + string errmsg + 1 byte 0
	// ErrorCodes: 0: unspec, 1: Filenotfound, 2:access violation, 3:diskfull or alloc exceeded, 4: illegal tftp operation, 5:unknown transfer ID, 6: File already exists, 7: No such user.
	*/
	ERROR 	= 5,
} OPCODE;

typedef enum {
	UNDEF=0,
	FILE_NOT_FOUND,
	ACCESS_VIOLATION,
	DISK_FULL,
	ILLEGAL_TFTP_OPERATION,
	UNKNOWN_TRANSFER_ID,
	FILE_ALREADY_EXISTS,
	NO_SUCH_USER,
} ERRCODE;

typedef enum {
	netascii =1,
	octet,
	mail,
	invalid
} mode;

// Per client
typedef struct {
	int 	family;
	char 	address[INET6_ADDRSTRLEN];
	short 	cport;		// Their TID
	short	sport;		// Our TID
	char	*filename; 	// user strncpy
	long 	offset;		// Block offset
} _client;

// Converts general sockaddr struct to ipv4/6 struct
void *get_in_addr(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		// IPv4
		return &(((struct sockaddr_in*)sa)->sin_addr);	// Return address of the location
	}
	// IPv6
	// Return address of that location. but first convert those address locations into the right cast
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
//	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Gets from general sockaddr struct the port
int get_in_port(struct sockaddr *sa) {
	if (sa->sa_family == AF_INET) {
		return ntohs (((struct sockaddr_in*)sa)->sin_port);
	}
	return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
}

// Create a socket, bind it to given port
int create_local_socket(int family, int port) {
	char tmpport[5];
	sprintf(tmpport,"%d",port);
	struct addrinfo hints, *servinfo, *p;
	int sockfd, rv, addr_len;
	pid_t pid = getpid();
	memset(&hints, 0, sizeof(hints));
	hints.ai_family 	= family;			// IPv4 or IPv6
	hints.ai_socktype	= SOCK_DGRAM;			// UDP
	hints.ai_flags		= AI_PASSIVE;			// My IP
	if ((rv=getaddrinfo(NULL,tmpport,&hints, &servinfo))!=0) {
		fprintf(stderr,"[%d] create_local_socket() : Could not get addr info with port %d\n",pid,port);
		return rv;
	}
	for (p=servinfo; p!=NULL; p=p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
			fprintf(stderr,"[%d] create_local_socket() : Could not create socket!\n",pid);
			continue;
		}
		if (bind(sockfd, p->ai_addr,p->ai_addrlen)==-1) {
			close(sockfd);
			fprintf(stderr,"[%d] create_local_socket() : Could not bind socket!\n",pid);
			continue;
		}
		break;
	}

	if (p==NULL) {
		fprintf(stderr,"[%d] create_local_socket() : FATAL : Could not find/bind a socket!\n",pid);
		return -1;
	}

	freeaddrinfo(servinfo);
	return sockfd;
}

// Reads untill and ACK packet has been sent and returns the block
unsigned short getACK(int sfd,struct sockaddr *ca,int timeout) {
	unsigned short ack;
	char buf[NS_BLOCK+NS_OPCODE];	// 2: OPCODE + 2: BLOCK

        fd_set master;
        struct timeval tv;
        tv.tv_sec=timeout; tv.tv_usec=0;        // Get's updated on some systems with remaining time left//TODO USE gettimeofday()
        FD_ZERO(&master);
        FD_SET(sfd, &master);

	struct sockaddr_storage aa;
	int addr_len = sizeof(aa);
        while (1) {
        	// Recv timeout, select?
                int sel = select(sfd+1,&master,NULL,NULL,&tv);
                if (sel<0){
                	// TODO ERROR REPORTING
			fprintf(stderr,"[%d] getACK() : ERROR : Select failed with error code %d\n",getpid(),sel);
			return 0;
                } else if (sel==0) {
			// Timeout
			break;
		} else if (FD_ISSET(sfd,&master)) {
                	// Read from cfd and hope it's the ACK packet
			int rb = recvfrom(sfd,buf,NS_BLOCK+NS_OPCODE,0,(struct sockaddr *)&aa,&addr_len);
			// Check if from expected host
//			char tip[INET6_ADDRSTRLEN];
//			char tip2[INET6_ADDRSTRLEN];
			// Actually just memcmp
//			inet_ntop(ca->sa_family, get_in_addr(ca), tip, sizeof(tip));
//			inet_ntop(aa.ss_family, get_in_addr((struct sockaddr*)&aa), tip2, sizeof(tip2));
//			printf("ACK? %s:%hu and %s:%hu\n", tip, get_in_port((struct sockaddr*)&aa),tip2, get_in_port(ca));

//			printf("AA: %d\n",(((struct in_addr*)get_in_addr((struct sockaddr*)&aa))->s_addr));
//			printf("CA: %d\n",(((struct in_addr*)get_in_addr(ca))->s_addr));
			OPCODE opc = (unsigned short)(buf[0]<<8) | buf[1];
printf("ACK: Expected %hu Received %hu\n",ACK,opc);
			if (opc==ACK &&(((struct in_addr*)get_in_addr((struct sockaddr*)&aa))->s_addr)==(((struct in_addr*)get_in_addr(ca))->s_addr) && get_in_port((struct sockaddr*)&aa)==get_in_port(ca)) {
				ack = (unsigned short)(buf[2]<<8) | buf[3];
				return ack;
			} else { //retry -timeout
				// TODO calculate remaining Timeout
			}
		}
	}
	return 0; // Timeout
}


// Extracts a string untill character ch has been found
char *extract(char *buf, int offset, char ch){
	int i=0, tfs = 20;
        char *fn=malloc(sizeof(char)*tfs);
        for (i=offset;buf[i]!=0;i++){
                if (i-offset>=tfs) {
                        tfs*=2;
                        fn = realloc(fn,tfs);
                }
                fn[i-offset]=buf[i];
        }
        fn[i-offset] = '\0';
        return fn;
}

int main(int argc, char **argv) {
	int sockfd,numbytes;
	char buf[NS_MAX];					// Buffer for data
	struct sockaddr_storage their_addr;
	int addr_len;							// Error
	char s[INET6_ADDRSTRLEN];				// String for ip
	if ((sockfd = create_local_socket(AF_UNSPEC, PORT))==-1) {
		fprintf(stderr,"FATAL: Could not create server socket!\n");
		return -1;
	}

/* TODO	if (p->ai_family ==AF_INET)
   TODO		inet_ntop(p->ai_family,&((struct sockaddr_in*)p->ai_addr)->sin_addr,s,p->ai_addrlen);
   TODO	else
   TODO		inet_ntop(p->ai_family,&((struct sockaddr_in6*)p->ai_addr)->sin6_addr,s,p->ai_addrlen);
   TODO	printf("%d: TFTP server started on %s : %d\n",getpid(),s,get_in_port(p->ai_addr));
*/
	printf("%d: TFTP server started!\n", getpid());

	addr_len = sizeof(their_addr);
	time_t rawtime;
	struct tm *timeinfo;
	unsigned short lport = 1024;

	while (1) {
		lport++;
		time (&rawtime);
		timeinfo = localtime(&rawtime);
		// A transfer is established by sending a request (WRQ or RRQ) and receiving a positive reply (ACK for WRQ, first data packet for RRQ)
		if ((numbytes = recvfrom(sockfd, buf, NS_MAX,0,(struct sockaddr *)&their_addr, &addr_len)) == -1) {
			fprintf(stderr,"recvfrom failed horribly.\n");
			return 3;
		}
// ERROR
if (buf[0]) buf[1]=0;
//		char time[24];
//		strcpy(time,asctime(timeinfo));
//		time[23]=0;
		printf("%s: New packet from %s:%hu\n", time(NULL),inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof(s)), get_in_port((struct sockaddr *)&their_addr));

		OPCODE opc = (unsigned short)(buf[0]<<8) | buf[1]; // 0x00ff&buf[1]
		ERRCODE err=0;
		// PID management
		// Use alloc and realloc
		pid_t pid;

		// TODO Keep track of children
		if (!(pid=fork())) {
			// Child
			close(sockfd);
			switch (opc) {
				case RRQ: {
					// TODO Check it wasn't accedental
					char *fn = extract(buf,2,' ');
					memset(buf,0,NS_MAX);
					printf("[%d] RRQ : Connected host requests file \"%s\"\n",getpid(),fn);
					// Find file
					FILE *f;
					int cfd = create_local_socket(((struct sockaddr *)&their_addr)->sa_family,lport);
					// Create new socket with lport for TID
                                        if (cfd<0) {
                                                // We somehow fucked up
                                                fprintf(stderr,"[%d] RRQ : ERROR : Could not create new socket\n");
                                                break;
                                        }
					if ((f=fopen(fn,"rb"))==NULL) {
						opc = ERROR;
						if (errno==EACCES || errno==EFAULT) {
							// No access
							err=ACCESS_VIOLATION;
						}else if (errno==ENOENT) {
							// File not found
							err=FILE_NOT_FOUND;
						}
						buf[0]=(char)(opc&0xFF00);
						buf[1]=(char)(opc&0x00FF);
						buf[2]=(char)(err&0xFF00);
						buf[3]=(char)(err&0x00FF);
						buf[4]=0;
						fprintf(stderr,"[%d] RRQ : ERROR :  Could not open file\n");
						sendto(cfd,buf,5,0,(struct sockaddr *)&their_addr, addr_len);
					}
					// THEN reply with a new socket
					free(fn);

					long rbytes=0, sbytes=0,tbytes=0,sf=0;
					unsigned short block=1;
					// Find out size of file
					fseek(f,0,SEEK_END);
					sf=ftell(f);
					fseek(f,0,SEEK_SET);

					memset(buf,0,NS_MAX);
					opc=DATA;
					buf[0]=(char)(opc&0xFF00);
					buf[1]=(char)(opc&0x00FF);
					printf("[%d] RRQ : Sending file (%ld bytes)\n",getpid(),sf);
					while (tbytes<sf) {
						// TODO CHANGE FOR MODE
						if ((rbytes = fread(buf+NS_OPCODE+NS_BLOCK,1,NS_BUF,f))==0) {
							if (feof(f)) {
								// File fully sent
								// Uhhhh we shouldn't send anything else now
							}
							break;
						}
						buf[2]=(char)(block&0xFF00);
						buf[3]=(char)(block&0x00FF);
						sbytes = sendto(cfd,buf,NS_BLOCK+NS_OPCODE+rbytes,0,(struct sockaddr *)&their_addr, addr_len);
						printf("[%d] RRQ : Block %hu, Sent %ld<->%ld bytes\n",getpid(),block,sbytes,NS_BLOCK+NS_OPCODE+rbytes);
						tbytes += sbytes;
						memset(buf,0,NS_MAX);

						// ACK
						short ack = getACK(cfd,(struct sockaddr *)&their_addr,TIMEOUT);
						if (ack==0) {
							// retry // Timed out
							printf("[%d] RRQ/ACK: ERROR DID NOT GET AN ACK :(\n",getpid());
						} else if (ack==block) {
							block++;
						} else {
							printf("[%d] RRQ/ACK : ERROR : Block mismatch! Expected %hu, got %hu\n",getpid(),block,ack);
						}
					}
					printf("[%d] RRQ : File sent! (%ld bytes)\n",getpid(),tbytes);
					fclose(f);
					break;}
				case WRQ:
					printf("WRITE\n");
					break;
				case DATA:
					
					break;
				case ACK:
					
					break;
				case ERROR:
					// Uhm let's ignore these
					break;
				default:
					// return error with ERROR:4
					break;
			};
			exit(0);
		} else {
			// Update statistics
			// totalup, totaldown, totalcons, totalreq
			signal(SIGCHLD, SIG_IGN);
		}

		// Process OPCODE
/*
		ropcode = 0x0003;
		buf[0]=(char)(ropcode&0xFF00);
		buf[1]=(char)(ropcode&0x00FF);
*/

//		sendto(sockfd,buf,NS_OPCODE,0,(struct sockaddr *)&their_addr,addr_len);
	}

	close(sockfd);
	return 0;
}
