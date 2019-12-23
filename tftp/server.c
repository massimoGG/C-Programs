/*      TFTP server created by Massimo Giardina 16/08/2019
        Implementing RFC 1350. (https://tools.ietf.org/html/rfc1350)
        Using MIT license
*/
/*	CURRENT BUGS:
	- Files requiring more than 256 blocks will time out!
		-> Pottential bug: Bit manipulation error
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

#define	ERROR(n) ((n)<0)
#define DEBUG		1
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
	int 	family;
	unsigned short port;
	unsigned int ipv4;
	char 	ipv6[16];
	struct sockaddr_storage client_address;
	char 	buf[BUF_MAX];
	size_t	bufsize;
	FILE 	*fd;
	// Block number
	//  WRQ: expected result sending to host
	//  RRQ: expected result from host
	unsigned short block;
	// When we're in RRQ
	short resends;
	// For server handler
	struct timeval last_action;
} CLIENT;

// Extracts a string untill character ch has been found
char *extract(char *buf, int offset, char ch, int *foffset){
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
	*foffset = i-offset;
        return fn;
}

// Converts general sockaddr struct to ipv4/6 struct
void *get_in_addr(struct sockaddr *sa) {
        if (sa->sa_family == AF_INET) {
                // IPv4
                return &(((struct sockaddr_in*)sa)->sin_addr);  // Return address of the location
        }
        // IPv6
        // Return address of that location. but first convert those address locations into the right cast
        return &(((struct sockaddr_in6*)sa)->sin6_addr);
//      return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Gets from general sockaddr struct the port
unsigned short get_in_port(struct sockaddr *sa) {
        if (sa->sa_family == AF_INET) {
                return ntohs (((struct sockaddr_in*)sa)->sin_port);
        }
        return ntohs(((struct sockaddr_in6*)sa)->sin6_port);
}

int create_socket(SERVER *s, int family, int port);
// Dynamic array	functions
/*
CLIENT *pool = malloc(sizeof(CLIENT)*amount); // Amount = 10?
*/
int send_error(CLIENT *c,error_code err);
void timeout(CLIENT *pool, int pool_size);
CLIENT *init_pool(int num); 	// num of clients
int add_pool(CLIENT *pool, int *pool_size, CLIENT *cl);				// Adds the client in an empty slot
int del_pool(CLIENT *pool,int clid);//, int *pool_size, int clid);				// Memset(0) the 
int opt_pool(CLIENT *pool, int *pool_size);				// Optimizes the pool by removing empty slots
int find_pool(CLIENT *pool, int pool_size, struct sockaddr *addr);
void free_pool(CLIENT *pool, int *pool_size);

// Transfer 	functions
int init_rrq(int s, CLIENT *cl, struct sockaddr *, char *filename, char *fm);
int init_wrq(int s, CLIENT *cl, struct sockaddr *, char *filename, char *fm);
int con_rrq(CLIENT *pool, int clid);
int con_wrq(CLIENT *pool, int clid, char *buf,size_t ssize);

int main(int argc, char **argv) {
	SERVER s; s.active = 1;
	size_t addrlen = sizeof(s.server_address);
	char buf[BUF_MAX];
	if (ERROR(create_socket(&s, AF_UNSPEC, PORT))) {
		fprintf(stderr,"[%d] main() : ERROR : Could not init server.\n",getpid());
		return -1;
	}

	// Pool initialisation
	int pool_size = 10;
	CLIENT *clpool = init_pool(pool_size);
	if (clpool==NULL) {printf("main() : ERROR : Failed to create client pool!\n"); s.active = 0;}
	printf("main \t\t: INFO : TFTP server started listening on port %d\n",PORT);
	// Server main loop
	time_t	first_timeout;
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
			timeout(clpool,pool_size);
/*			for (CLIENT*p=clpool;p!=NULL;p=p->next) {
/* TODO
Use int gettimeofday(strict timeval *tv, NULL);
struct timeval {
	time_t		tv_sec;
	suseconds_t	tv_usec;
}
				if (now - p->last_action<=0) {
					// Timed out
					printf("[%d] main() : Host timed out\n",getpid());
					del_pool(clpool, p);
				}
			}
*/		} else if (sel>0 && FD_ISSET(s.sfd,&rfds)) {
			// Now recvfrom from it
			int rbytes = recvfrom(s.sfd,buf,BUF_MAX,0,(struct sockaddr*)&s.tmp_address,&addrlen);
			if (rbytes<=0) continue;

			char sip[INET6_ADDRSTRLEN];
			inet_ntop(s.tmp_address.ss_family,get_in_addr((struct sockaddr *)&s.tmp_address), sip, sizeof(sip));
			unsigned short po = get_in_port((struct sockaddr *)&s.tmp_address.ss_family);

			// Parse OPCODE
			OPCODE opc=(unsigned short)(buf[0]<<8) | buf[1]; int foffset=0;
			if (buf[0]!=0x00) { printf("main() \t\t: recvfrom() : Protocol mismatch\n"); continue; }

			// PACKET HEX DECODER
			printf("recvfrom() \t: HEX : ");
			for (int i=0;i<4;i++) {
				printf("%X ",buf[i]);
			}
			printf("\n");

			switch(opc) {
				case RRQ: {
					char *filename = extract(buf,2,0,&foffset);
					char *tmode=extract(buf,foffset+3,0,&foffset);
					if (DEBUG) printf("recvfrom() \t: RRQ : %s:%d : %s : Filemode: %s\n",sip,po,filename,tmode);
// TODO check if client already exists in pool
					CLIENT *cl = calloc(1,sizeof(CLIENT));
					if (ERROR(init_rrq(s.sfd,cl,(struct sockaddr*)&s.tmp_address, filename, tmode))) break;
					add_pool(clpool,&pool_size,cl);
					free(filename);
					break; }
				case WRQ: {
					char *filename = extract(buf,2,0,&foffset);
					char *tmode=extract(buf,foffset+3,0,&foffset);
					if (DEBUG) printf("recvfrom() \t: WRQ : %s:%d : %s : Filemode: %s\n",sip,po,filename,tmode);
					CLIENT *cl = calloc(1,sizeof(CLIENT));
					if (ERROR(init_wrq(s.sfd,cl,(struct sockaddr*)&s.tmp_address,filename,tmode))) break;
					add_pool(clpool,&pool_size,cl);
					free(filename);
					break; }
				case DATA:
					if (DEBUG) printf("recvfrom() \t: DATA : \n");
					con_wrq(clpool,find_pool(clpool,pool_size,(struct sockaddr*)&s.tmp_address),buf,rbytes);
					break;
				case ACK:
					if (DEBUG) printf("recvfrom() \t: ACK : %s:%d : \n",sip,po);
					if (ERROR(con_rrq(clpool,find_pool(clpool,pool_size,(struct sockaddr*)&s.tmp_address)))) {
						del_pool(clpool,find_pool(clpool,pool_size,(struct sockaddr*)&s.tmp_address));
					}
					break;
				case ERR:
					if (DEBUG) printf("recvfrom() \t: ERR : Received an error from host\n");
					del_pool(clpool,find_pool(clpool,pool_size,(struct sockaddr*)&s.tmp_address));
					// Remove the client from the pool
					break;
				default:
					break;
			}
			int tod = find_pool(clpool,pool_size,(struct sockaddr*)&s.tmp_address);
			if (tod>=0 && tod<pool_size) {
				// Updates timeout
				gettimeofday(&clpool[tod].last_action,NULL);
				// Updates resends
				if (DEBUG) printf("main:timout \t: DEBUG : Updated timeout for %d\n",tod);
			}
			// timeout(clpool, pool_size); // TODO?????
			printf("\n");
// TODO timeout
// first_timeout -( before this - tv.tv_usec)
		}
	}

	// Free all
	free(clpool);
//	free_pool(clpool,&pool_size);
	// Close all
	return 0;
}

void timeout(CLIENT *pool, int pool_size) {
	for (int i=0;i<pool_size;i++) {
		if (pool[i].sfd!=0) {
			struct timeval now; gettimeofday(&now,NULL);
			unsigned long t= ( ((now.tv_sec-pool[i].last_action.tv_sec)*1000000) + (now.tv_usec-pool[i].last_action.tv_usec));
			if (DEBUG) printf("timeout() \t: INFO : Remaining time for %i: %lu %lu\n",i, t,CLIENT_TIMEOUT*1000000);
			if (t > CLIENT_TIMEOUT*1000000) {
				// Timed out! :(
				if (DEBUG) printf("timeout() \t: INFO : Client %d timed out! :(\n",i);
				// WRQ or RRQ? If rrq resend last buf
				if (pool[i].resends>=0) {
					// Means host RRQ
					// So resend the buf
					if (pool[i].resends<MAX_RESENDS) sendto(pool[i].sfd,pool[i].buf,pool[i].bufsize,0,(struct sockaddr*)&pool[i].client_address,sizeof(struct sockaddr));
					else {
						del_pool(pool,i);
						fprintf(stderr,"timeout() \t: DEBUG : Client timed out too many times while downloading!\n");
					}
				} else {
					// del_pool(pool,i);
					fprintf(stderr,"timeout() \t: DEBUG : Client timed out while uploading!\n");
					if (-1* (pool[i].resends) > MAX_RESENDS)  {
						send_error(&pool[i],UNDEFINED);
						fprintf(stderr,"timeout() \t: DEBUG : Client timed out too many times while uploading!\n");
						del_pool(pool,i);
					} else {
						// Send ack previous one
//printf("FUCKING HELL WHATS THIS? RESENDS: %d MAX: %d\n",pool[i].resends,MAX_RESENDS);
						if (DEBUG) fprintf(stderr,"timeout() \t: DEBUG : Resending client an ACK\n");
						sendto(pool[i].sfd,pool[i].buf,pool[i].bufsize,0,(struct sockaddr*)&pool[i].client_address,sizeof(struct sockaddr));
					}
				}
			}
		}
	}
}

// Parse filename to see if it's not exiting 
int parse() {

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
                        if (DEBUG) fprintf(stderr,"[%d] create_local_socket() : Could not create socket!\n",pid);
                        continue;
                }
                if (bind(sockfd, p->ai_addr,p->ai_addrlen)==-1) {
                        close(sockfd);
                        if (DEBUG) fprintf(stderr,"[%d] create_local_socket() : Could not bind socket!\n",pid);
                        continue;
                }
                break;
        }

        if (p==NULL) {
                if (DEBUG) fprintf(stderr,"[%d] create_local_socket() : FATAL : Could not find/bind a socket!\n",pid);
                return -2;
        }
	s->sfd = sockfd;
        freeaddrinfo(servinfo);
        return sockfd;
}

CLIENT *init_pool(int num) {
	CLIENT *p = calloc(num,sizeof(CLIENT));	// Array of structs
//	memset(p,0,sizeof(CLIENT)*num);
	return p;
}

// Adds the client to the pool
int add_pool(CLIENT *pool, int *pool_size,CLIENT *cl) {
	if (cl==NULL) {
		return -1; 	// If init_rrq failed because of a violation/error
	}
	int i=0;
	for(i=0;pool[i].sfd>0 && i<*pool_size;i++);
	if (pool[i].sfd!=0 && i>=*pool_size) {
		// The pool is full -> expand it
		*(pool_size)++; i++;
		pool = realloc(pool,sizeof(CLIENT)* *(pool_size));
		// TODO CHECK memory
		if (DEBUG) printf("add_pool() \t: INFO : Expanded pool size to %d\n",*pool_size);
	}
	// Copy everything
	//printf("memcpy %p to %p place: %d\n",&cl,&pool[i],i);
	memcpy(&pool[i],cl,sizeof(CLIENT));
	if (DEBUG) {
		char fsip[INET6_ADDRSTRLEN];
		inet_ntop(cl->client_address.ss_family,get_in_addr((struct sockaddr*)&cl->client_address), fsip, sizeof(fsip));
		unsigned short fpo = get_in_port((struct sockaddr*)&cl->client_address);
		printf("add_pool() \t: INFO : Client added in pool %s:%d\n",fsip,fpo);
	}
	free(cl);
	return i;
}

int del_pool(CLIENT *pool, int clid) {
	if (clid<0) return -1;
	// TODO Check for file closes etc
	if (pool[clid].fd!=NULL) fclose(pool[clid].fd);
	if (DEBUG) printf("del_pool() \t: INFO : Deleting client %d\n",clid);
	pool[clid].sfd =0;
	pool[clid].port =0;
	pool[clid].ipv4=0;
	//if (memset(pool[clid],0,sizeof(CLIENT))) return 0;
//	if (cl!=NULL) if (memset(&cl,0,sizeof(CLIENT))) return 0;
	return -1;
}

//inline -> place the function into the calling function to make it faster
inline int opt_pool(CLIENT *pool, int *pool_size) {
	int i=0;
	for (i=0;pool[i].sfd>0;i++);
	if (i>=*pool_size) return 1;
	// If we're NOT at the end
	for (int y=i;y<*pool_size;y++) {
		if (y+1<=*pool_size) memcpy(&pool[y],&pool[y+1],sizeof(CLIENT));
	}
	return 0;
}

int find_pool(CLIENT *pool, int pool_size,struct sockaddr *addr) {
	int i=0;
	unsigned int 	ipv4;
	unsigned char 	ipv6;
	unsigned short	port = get_in_port(addr);
	if (DEBUG) printf("find_pool() \t: ");
	if (addr->sa_family==AF_INET) {
		ipv4=((struct sockaddr_in*)addr)->sin_addr.s_addr;
		if (DEBUG) printf("%u\n",ipv4);
	} else if (addr->sa_family==AF_INET6) {
		memcpy(&ipv6,((struct sockaddr_in6*)&addr)->sin6_addr.s6_addr,16);
		if (DEBUG) printf("ipv6 fuck\n");
	}
	for (i=0;i<pool_size;i++){
		if (pool[i].port==port) {
			if (DEBUG) printf("find_pool() \t: DEBUG : Port match!\n");
			if (pool[i].family==AF_INET) {
				if (ipv4==pool[i].ipv4) {
					if (DEBUG) printf("find_pool() \t: DEBUG : IPv4 match.\n");
					return i;
				}
				if (DEBUG) printf("find_pool() \t: DEBUG : IPv4 mismatch\n");
			} else if (pool[i].family==AF_INET6) {
				if (memcmp(&ipv6,pool[i].ipv6,16)==0) {
					if (DEBUG) printf("find_pool() \t: DEBUG : IPv6 match.\n");
					return i;
				}
			}
		}
	}
	return -1;
}

void free_pool(CLIENT *pool, int *pool_size) {
	for (int i=0;i<*pool_size;i++) {
		free(&pool+i);
	}
	if (DEBUG) printf("free_pool() \t: INFO : Memory pool cleared.\n");
}

int send_error(CLIENT *cl, error_code code) {
	OPCODE opc=ERR;
	cl->buf[0]=(char)((opc>>8)&0xFF);
	cl->buf[1]=(char)(opc&0xFF);
	cl->buf[2]=(char)((code>>8)&0xFF00);
	cl->buf[3]=(char)(code&0x00FF);
	return sendto(cl->sfd,cl->buf,4,0,(struct sockaddr *)&cl->client_address,sizeof(struct sockaddr));
}

// Init_RRQ // Checks for file, reads first 512 bytes -> Sends them and adds the client tothe pool
int init_rrq(int s,CLIENT *p, struct sockaddr *addr, char *filename, char *fm) {
	p->sfd = s;
	if (memcpy(&p->client_address,addr,sizeof(struct sockaddr))==NULL) {
                // TODO send error
                fprintf(stderr,"init_rrq() : ERROR : Could not copy address to pool! Ignoring...\n");
        //      return -3;
        }
	if (strcmp(fm,"octet")!=0) {
                fprintf(stderr,"init_rrq() \t: ERROR : Filemode is not octet!\n");
                send_error(p,ILLEGAL_OP);
                return -1;
        }
	char sip[INET6_ADDRSTRLEN];
//        inet_ntop(addr->sa_family,get_in_addr(addr), sip, sizeof(sip));
//        unsigned short po = get_in_port(addr);
//	CLIENT *p = calloc(1,sizeof(CLIENT));
	if (p==NULL) {
		if (DEBUG) fprintf(stderr,"init_rrq() \t: ERROR : Out of memory?\n");
		return -1;
	}
	// TODO switch (mode) { case octet: break case ascii: break default: break ]
	p->fd = fopen(filename,"rb");	// TODO CHECK PATH ACCESS
	if (p->fd==NULL) {
		if (errno==EACCES || errno==EFAULT) {
			// No access
			send_error(p,ACCESS_VIOLATION);
		} else if (errno==ENOENT) {
			//
		}
		fprintf(stderr,"init_rrq() \t: ERROR : Could not open file.\n");
		send_error(p,UNDEFINED);
		return -2;
	}
	p->family = addr->sa_family;
	if (addr->sa_family==AF_INET) {
		p->ipv4=((struct sockaddr_in*)addr)->sin_addr.s_addr;
		if (DEBUG) printf("inet_rrq() \t: IPv4: %u\n",p->ipv4);
	} else {
		memcpy(p->ipv6,((struct sockaddr_in6*)addr)->sin6_addr.s6_addr,16);
		if (DEBUG) printf("inet_rrq() \t: IPv6: ");//%s\n",p->ipv6);
	}
	p->port = get_in_port(addr);
	// Open file and blabla
	p->block = 1;
	p->resends=0;
	// Read first chunk of bytes
	// TODO put data in buf of client in case of resend
	int rbytes = fread(p->buf+4,1,BUF_MAX-4,p->fd);
	// Now form the packet
	OPCODE opc = DATA;
	p->buf[0]=(char)((opc>>8)&0xFF);
	p->buf[1]=(char)(opc&0xFF);
	p->buf[2]=(char)((p->block>>8)&0xFF);
	p->buf[3]=(char)(p->block&0xFF);
	int sbytes = sendto(p->sfd,p->buf,rbytes+4,0,addr,sizeof(struct sockaddr));
	if (sbytes!=rbytes+4) {
		// Well we've got a problem.. This would be VERY concerning if this owuld happen.
		inet_ntop(addr->sa_family,get_in_addr(addr),sip,sizeof(sip));
		fprintf(stderr,"init_rrq() \t: NET_ERROR : fread/sendto mismatch. %s:%d (%d:%d)\n",sip,get_in_port(addr),rbytes,sbytes);
	}
	p->bufsize = rbytes+4;
	return 0;
}

int init_wrq(int s,CLIENT *p,struct sockaddr *addr, char *filename, char *fm) {
	if (p==NULL) {
		if (DEBUG) fprintf(stderr,"init_wra() \t: ERROR : Out of memory!\n");
		return -1;
	}
	p->sfd = s;	// RFC Method is assigning a new socket with a random port.
	if (memcpy(&p->client_address, addr, sizeof(struct sockaddr))==NULL) {
                fprintf(stderr,"init_wrq() \t: ERROR : Could not copy address to pool! Ignoring...\n");
                // return -3;
        }
	if (strcmp(fm,"octet")!=0) {
                fprintf(stderr,"init_wrq() \t: ERROR : Filemode is not octet!\n");
                send_error(p,ILLEGAL_OP);
                return -1;
        }
	p->fd = fopen(filename,"wb");
	if (p->fd==NULL) {
		// TODO errno-> File already exists, accessviolation, diskfull 
		send_error(p,UNDEFINED); // Temporarely
		fprintf(stderr,"init_wrq() \t: ERROR : Could not open file.\n");
		return -2;
	}
	// TODO CHECK FILE ALREADY EXISTS WITH ==NULL TODO TODO TODO TODO TODO
	p->family = addr->sa_family;
	if (p->family==AF_INET) {
		p->ipv4=((struct sockaddr_in*)addr)->sin_addr.s_addr;
		if (DEBUG) printf("inet_wrq() \t: IPv4: %u\n",p->ipv4);
	} else {
		memcpy(p->ipv6,((struct sockaddr_in6*)addr)->sin6_addr.s6_addr,16);
		if (DEBUG) printf("inet_wrq() \t: IPv6: \n");
	}
	p->port = get_in_port(addr);
	p->block = 0;
	p->resends=-1;	// Useless for wrq
//        int wbytes = fwrite(buf+4,1,BUF_MAX-4,p->fd);
	OPCODE opc = ACK;
        p->buf[0]=(char)((opc>>8)&0xFF);
        p->buf[1]=(char)(opc&0xFF);
        p->buf[2]=(char)((p->block>>8)&0xFF);
        p->buf[3]=(char)(p->block&0xFF);
        int sbytes = sendto(p->sfd,p->buf,4,0,addr,sizeof(struct sockaddr));
        if (sbytes!=4) {
		char sip[INET6_ADDRSTRLEN];
        	inet_ntop(addr->sa_family,get_in_addr(addr),sip,sizeof(sip));
         	fprintf(stderr,"init_wrq() \t: NET_ERROR : ACK sendto failed. %s:%d\n",sip,get_in_port(addr));
	}
	return 0;
}

int con_rrq(CLIENT *pool, int clid) {
	if (clid<0) return -1;
	CLIENT *client = &pool[clid];
	if (client==NULL) {
		// The client was not found in our pool
		return -1;
	}
	char fsip[INET6_ADDRSTRLEN];
        inet_ntop(client->client_address.ss_family,get_in_addr((struct sockaddr*)&client->client_address), fsip, sizeof(fsip));
        unsigned short fpo = get_in_port((struct sockaddr*)&client->client_address);
//	printf("con_rrq() \t: INFO : Continuing rrq %s:%d\n",fsip,fpo);

	// Extract block number!
	// Increase resends if neccesary
	unsigned short block = (unsigned short)(client->buf[2]<<8 | client->buf[3]); int sbytes;
	if (DEBUG) printf("con_rrq() \t: INFO : Host is at block %hu. Should be at block: %hu\n",block,client->block);
	// TODO Page 7 of RFC1350 "All packets other than DUPLICATE ACK's and those used for termination ARE ACKNOWLEDGED UNLESS A TIMEOUT OCCURS"
	if (block < client->block) {
		// Means that a packet has been dropped. -> Resend client->bufs
		sbytes = sendto(client->sfd,client->buf,client->bufsize,0,(struct sockaddr*)&client->client_address,sizeof(struct sockaddr));
		client->resends++;
		if (client->resends>=MAX_RESENDS) {
			send_error(client,UNDEFINED);
			fprintf(stderr,"con_rrq() \t: ERROR : Too many resends! Disconnecting client!\n");
			return -2;
		}
		if (DEBUG) printf("con_rrq() \t: NET_ERROR : Packets lost for block %d, resends: %d\n",client->block, client->resends);
		return 0;
	}
	client->resends=0;
	// Client is with us -> send the next block
	fseek(client->fd,(BUF_MAX-4)*(client->block),SEEK_SET);
	int rbytes = fread(client->buf+4,1,BUF_MAX-4,client->fd);
	if (rbytes<=0) {
		//senderror?
		if (DEBUG) fprintf(stderr,"conn_rrq() \t: ERROR : Reading from a closed file?\n");
		send_error(client,ILLEGAL_OP);
		return -3;
	}
	if (DEBUG) printf("con_rrq() \t: INFO : Read next block %d (%d bytes)\n",client->block,rbytes);
	client->block++;
	// Set opcode + block number
	OPCODE opc = DATA;
	client->buf[0]=(char)((opc>>8)&0xFF);
        client->buf[1]=(char)(opc&0xFF);
        client->buf[2]=(char)((client->block>>8)&0xFF);
        client->buf[3]=(char)(client->block&0xFF);
	client->bufsize=sendto(client->sfd,client->buf,rbytes+4,0,(struct sockaddr*)&client->client_address,sizeof(struct sockaddr));
        if (client->bufsize!=rbytes+4) {
		fprintf(stderr,"con_rrq() \t: NET_ERROR : fread/sendto mismatch. (%d:%d)\n",rbytes,client->bufsize);
	}
	if (rbytes<BUF_MAX-4) {// && feof(client->fd)) {
		// EOF -> Delete client
//		fclose(client->fd);
		client->block--;
		printf("con_rrq() \t: INFO : %s:%d : Transfer finished! %d blocks\n",fsip, fpo,client->block);
		del_pool(pool,clid);
	}
	return 0;
}

int con_wrq(CLIENT *pool, int clid, char *buf,size_t ssize) {
	if (clid<0) return -1;
        CLIENT *client = &pool[clid];
        if (client==NULL) {
	        return -1;
        }
        char fsip[INET6_ADDRSTRLEN];
        inet_ntop(client->client_address.ss_family,get_in_addr((struct sockaddr*)&client->client_address), fsip, sizeof(fsip));
        unsigned short fpo = get_in_port((struct sockaddr*)&client->client_address);

        // Extract block number!
        // Increase resends if neccesary
        unsigned short block = (unsigned short)(buf[2]<<8 | buf[3]);
        if (DEBUG) printf("con_wrq() \t: INFO : Host is at block %hu. Expected: %hu\n",block,client->block+1);
	OPCODE opc = ACK; int sbytes=0;
	client->buf[0] = (char)((opc>>8)&0xFF);
	client->buf[1] = (char)(opc&0xFF);

        if (block > client->block+1) {
/*		// A packet has been lost. We need to resend with an ACK of previous block num
		client->buf[2] = (char)(client->block&0xFF00);
		client->buf[3] = (char)(client->block&0x00FF);
                sbytes = sendto(client->sfd,client->buf,4,0,(struct sockaddr*)&client->client_address,sizeof(struct sockaddr));
                client->resends--;
                if (client->resends>=MAX_RESENDS) {
                        // send_error(); // TOOMANY
                        fprintf(stderr,"con_rrq() \t: ERROR : Too many resends! Disconnecting client!\n");
                        return -2;
                }*/ // THIS SHOULD NEVER EVER HAPPEN BECAUSE OF THE PROTOCOL ^
                if (DEBUG) printf("con_wrq() \t: NET_ERROR : Packets lost for block %d, resends: %d\n",client->block, -1*(client->resends+1));
                return 0;
        }
	// The right block -> write it!
//        fseek(client->fd,(BUF_MAX-4)*(client->block),SEEK_SET);
        int wbytes = fwrite(buf+4,1,ssize-4,client->fd);
	fflush(client->fd);	// DEBUG
        if (DEBUG) printf("con_wrq() \t: INFO : Wrote next block %d (%d bytes)\n",client->block+1,wbytes);

        // Set opcode + block number
	client->block++;
	client->resends=-1;
        client->buf[2]=(unsigned char)((client->block >> 8)&0xFF);
        client->buf[3]=(unsigned char)(client->block&0xFF);
	sbytes=sendto(client->sfd,client->buf,4,0,(struct sockaddr*)&client->client_address,sizeof(struct sockaddr));
        if (wbytes!=ssize-4) {
                fprintf(stderr,"con_wrq() \t: ERROR : fwrite could not write %d bytes.\n",ssize-4);
		return -2;
        }
        if (ssize<BUF_MAX) {
                // EOF -> Delete client
//                fclose(client->fd);
                del_pool(pool,clid);
                printf("con_wrq() \t: INFO : %s:%d : Transfer finished! %d blocks\n",fsip, fpo,client->block);
        }
        return 0;
}
