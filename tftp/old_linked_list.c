#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define	ERROR(n) ((n)<0)
#define CLIENT_TIMEOUT	5
#define MAX_RESENDS	5
#define PORT		69
#define BUF_MAX		516

typedef enum {
	RRQ 	= 1,
	WRQ 	= 2,
	DATA 	= 3,
	ACK 	= 4,
	ERR 	= 5,
} OPCODE;

typedef enum {
	UNDEFINED = 0,
	FNF,
	ACCESS_VIOLATION,
	DISK_FULL,
	ILLEGAL_OP,
	UNKNOWN_ID,
	FILE_ALREADY_EXISTS,
	NO_USER,
} error_code;

typedef enum {
	netascii = 1,
	octet 	 = 2,
	mail	 = 3,
} FILEMODE;

typedef struct {
	int 	sfd;
	struct sockaddr_storage	server_address;
	char 	buf[BUF_MAX];
	int	active;
	struct sockaddr_storage tmp_address;
} SERVER;
// We can cast server and client in each other
typedef struct clients{
	int 	sfd;	// Changes when we're assigning a new TID
	struct sockaddr_storage client_address;
	char 	buf[BUF_MAX];
	FILE 	*fd;
	// Block number
	//  WRQ: expected result sending to host
	//  RRQ: expected result from host
	unsigned short block;
	// When we're in RRQ
	unsigned short resends;
	// For server handler
	time_t	last_action;
	struct clients *next;
} CLIENT;

// Extracts a string untill character ch has been found
char *extract(char *buf, int offset, char ch){
        int i=0, tfs = 5;
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

int create_socket(SERVER *s, int family, int port);

// Linked list 	functions
CLIENT *init_pool();					// Create the first element
void add_pool(CLIENT *pool, CLIENT *cl); //struct sockaddr *add); 	// Add a client to the pool
int del_pool(CLIENT *pool, CLIENT *cl); //struct sockaddr *add);	// Remove a client from the pool
CLIENT *find_pool(CLIENT *pool, struct sockaddr *add);	// Find client in pool
int free_pool(CLIENT *pool);				// Clear the pool

// Dynamic array	functions
/*
Array 
CLIENT *pool = malloc(sizeof(CLIENT)*amount); // Amount = 10?


*/

// Transfer 	functions
CLIENT *init_rrq(struct sockaddr*, char *filename, FILEMODE fm);
CLIENT *init_wrq(struct sockaddr*, char *filename);
int con_rrq(CLIENT *client);
int con_wrq(CLIENT *client);

int main(int argc, char **argv) {
	SERVER s; s.active =1; size_t addrlen = sizeof(s.server_address);
	CLIENT *clpool = init_pool();	long pool_size = 1;
	char buf[BUF_MAX];
	if (ERROR(create_socket(&s, AF_UNSPEC, PORT))) {
		fprintf(stderr,"[%d] main() : ERROR : Could not init server.\n",getpid());
		return -1;
	}

	time_t	first_timeout;
	// Server main loop
	while (s.active) {
		// TODO set to first_timeout
		struct timeval tv;
		tv.tv_sec = 5;
		tv.tv_usec = 0;
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(s.sfd, &rfds);
		int sel = select(s.sfd+1, &rfds, NULL,NULL,&tv);
		if (sel<0) { fprintf(stderr,"[%d] main() : ERROR : Select failed.\n",getpid()); return -2;}
		if (sel==0) {
			// timeout
			// Cycle through list, calculate remaining timeout -> Delete them if necessary
			struct timeval timeout;
			time_t now = time(NULL);
del_pool(clpool,clpool->next);
			for (CLIENT*p=clpool;p!=NULL;p=p->next) {
/* TODO
Use int gettimeofday(strict timeval *tv, NULL);
struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
}
*/				if (now - p->last_action<=0) {
					// Timed out
					printf("[%d] main() : Host timed out\n",getpid());
					del_pool(clpool, p);
				}
			}
		} else if (sel>0 && FD_ISSET(s.sfd,&rfds)) {
			// Now recvfrom from it
			int rbytes = recvfrom(s.sfd,buf,BUF_MAX,0,(struct sockaddr*)&s.tmp_address,&addrlen);
			if (rbytes<=0) continue;
			// Parse OPCODE
			OPCODE opc=(unsigned short)(buf[0]<<8) | buf[1];
			if (buf[0]!=0x00) { printf("[%d] main() : recvfrom() : Protocol mismatch\n"); continue; }
			switch(opc) {
				case RRQ: {
					char *filename = extract(buf,2,' ');
					printf("[%d] RRQ : Uploading %s to host\n",getpid(),filename);
// TODO check if client already exists in pool
					CLIENT *tmpcl = init_rrq((struct sockaddr*)&s.tmp_address, filename, octet);
					add_pool(clpool,tmpcl);
					free(filename);
//if (find_pool(clpool, (struct sockaddr*)&s.tmp_address)!=NULL) printf("Nice.\n");
					//printf("Deleted: %d\n",del_pool(clpool,find_pool(clpool,(struct sockaddr *)&s.tmp_address)));
					break; }
				case WRQ: {
					char *filename = extract(buf,2,' ');
					printf("[%d] WRQ : \n");
					//add_pool(init_wrq((struct sockaddr*)&s.tmp_address, filename));
					free(filename);
					break; }
				case DATA:
					printf("[%d] DATA : \n");
					// con_wrq(find_pool(clpool,(struct sockaddr*)&s.tmp_address));
					break;
				case ACK:
					printf("[%d] ACK : \n");
					// con_rrq();
					break;
				case ERR:
					printf("[%d] ERR : ERROR");
					// Remove the client from the pool
					break;
				default:
					break;
			}
// TODO timeout
// first_timeout -( before this - tv.tv_usec)
		}
	}

	// Free all
	free_pool(clpool);
	// Close all
	return 0;
}

// Create a socket, bind it to given port
int create_socket(SERVER *s, int family, int port) {
        char tmpport[5];
        sprintf(tmpport,"%d",port);
        struct addrinfo hints, *servinfo, *p;
        int sockfd, rv, addr_len;
        pid_t pid = getpid();
        memset(&hints, 0, sizeof(hints));
        hints.ai_family         = family;                       // IPv4 or IPv6
        hints.ai_socktype       = SOCK_DGRAM;                   // UDP
        hints.ai_flags          = AI_PASSIVE;                   // My IP
        if ((rv=getaddrinfo(NULL,tmpport,&hints, &servinfo))!=0) {
                fprintf(stderr,"[%d] create_local_socket() : Could not get addr info with port %d\n",pid,port);
                return -1;
        }
        for (p=servinfo; p!=NULL; p=p->ai_next) {
                if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
//                        fprintf(stderr,"[%d] create_local_socket() : Could not create socket!\n",pid);
                        continue;
                }
                if (bind(sockfd, p->ai_addr,p->ai_addrlen)==-1) {
                        close(sockfd);
//                        fprintf(stderr,"[%d] create_local_socket() : Could not bind socket!\n",pid);
                        continue;
                }
                break;
        }

        if (p==NULL) {
//                fprintf(stderr,"[%d] create_local_socket() : FATAL : Could not find/bind a socket!\n",pid);
                return -2;
        }
	s->sfd = sockfd;
        freeaddrinfo(servinfo);
        return sockfd;
}

/* Linked list 	functions */
CLIENT *init_pool() {
	CLIENT *p = malloc(sizeof(CLIENT));
	memset(p,0,sizeof(CLIENT));
	p->next=NULL;
	return p;
}

// Adds the client to the pool
void add_pool(CLIENT *pool, CLIENT *cl) {
printf("Adding %p in %p\n",&cl,&pool);
	if (cl==NULL) return; 	// If init_rrq failed because of a violation/error
//	CLIENT *cl = malloc(sizeof(CLIENT));
	CLIENT *p=pool;
	for(;p!=NULL;p=p->next);
	cl->next=NULL;
	p=cl;
	//printf("add_pool() : %p added to the pool.\n",&cl);
}

int del_pool(CLIENT *pool, CLIENT *cl) {
	if (cl==NULL) return -1;
	CLIENT *p=pool;
	for(;p->next!=cl;p=p->next);
printf("DELETE %p %p %p\n",&cl,&p->next, &p);
	if (p->next==cl) {
		printf("[] del_pool() : Be gone thot\n");
printf("%p %p\n",p->next,cl->next);
		p->next = cl->next;
//		free(cl);
		return 0;
	}
	return -2;
}

CLIENT *find_pool(CLIENT *pool, struct sockaddr *add) {
	for (CLIENT *p=pool;p!=NULL;p=p->next) {
		printf("find_pool() : %p\n",&p);
		if (memcmp((struct sockaddr *)&p->client_address,&add,sizeof(struct sockaddr))==sizeof(struct sockaddr)) {
			printf("Found your guy\n");
			return p;
		}
	}
	return NULL;
}

int free_pool(CLIENT *pool) {

}

// Path checking + access violation checking


// Init_RRQ // Checks for file, reads first 512 bytes -> Sends them and adds the client tothe pool
CLIENT *init_rrq(struct sockaddr *addr, char *filename, FILEMODE fm) {
	CLIENT *p = malloc(sizeof(CLIENT));
	// TODO switch (mode) { case octet: break case ascii: break default: break ]
/*	p->fd = fopen(filename,"rb");	// TODO CHECK PATH ACCESS
	if (p->fd==NULL) {
		if (errno==EACCES || errno==EFAULT) {
			// No access
//			senderror(ACCESS_VIOLATION);
		} else if (errno==ENOENT) {
			//
		}
		fprintf(stderr,"[] init_rrq() : ERROR : Could not open file.\n");
		return NULL;
	}
	printf("init_rrq() : File opened.\n");
//	if (memcpy((struct sockaddr *)&p->client_address,addr,sizeof(struct sockaddr))==NULL) {
		// TODO send error
//		return NULL;
//	}
	// Open file and blabla
	p->block = 1;
	p->resends=0;
	p->last_action = time(NULL);
	// Read first chunk of bytes
	// TODO put data in buf of client in case of resend
*/printf("Created : %p %p\n",&p,p);
	return p;
}

CLIENT *init_wrq(struct sockaddr *addr, char *filename) {

}
