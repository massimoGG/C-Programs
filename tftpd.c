/*
 * tftp (RFC 1350) daemon server with extended operations
 * backwards compatibility with original tftp protocol
 * Made by Massimo Giardina 05/07/2020
 *
 * TODO: RFC 2347-2349
 */
/* Agenda
 * 04/07/2020 - Start of project : Linked list, util functions
 * 05/07/2020 - 
 * 06/07/2020 - Finished RRQ 
 * 
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>
#include <sys/time.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEBUG       0

#define PORT        69      //2302 // 69
#define TIMEOUT     5
#define MAXRETRIES  3
//#define MAXBUF      1500    //516     //512 + 4 (2opcode + 2block)
#define FILENAMESIZE    256

enum OPCODE {
    RRQ     = 1,
    WRQ,    //2
    DATA,   //3
    ACK,    //4
    ERROR,  //5
    OACK,   //6
    // Extended tftp
    LIST,
    CD,
    PWD,
    MKDIR,
};

enum ERRCODE {
    EUNDEF,
    ENOTFOUND,
    EACCESS,
    ENOSPACE,
    EBADOP,
    EBADID,
    EEXISTS,
    ENOUSER
};

struct errmsg {
    int e_code;
    const char *e_msg;
} errmsgs[] = {
    { EUNDEF,	"Undefined error code" },
    { ENOTFOUND,	"File not found" },
    { EACCESS,	"Access violation" },
    { ENOSPACE,	"Disk full or allocation exceeded" },
    { EBADOP,	"Illegal TFTP operation" },
    { EBADID,	"Unknown transfer ID" },
    { EEXISTS,	"File already exists" },
    { ENOUSER,	"No such user" },
};

typedef struct _client {
    // Address information
    int             sfd;
    unsigned short  s_family;
    unsigned char   s_addr[16];
    uint16_t        s_port;
    // tftp information
    unsigned char   operation;
    unsigned short  blocksize;
    unsigned short  curblock;    // What I've already sent
    long            set_timeout;
    long            resting_time;
    int             resends;
    // File information
    FILE            *file;
    char            *filename;
    long            fileprog;   // remember where in the file since curblock overflows
    long            filesize;

    struct _client  *next;
} CLIENT;

static unsigned short bufsize   = 516;

/*
 * Predefinitions
 */
static int handle_packet(CLIENT *master, int sockfd, struct sockaddr_storage *addr, 
                         char *buf, int recvbytes);
static int _sendto(CLIENT *c, char *buf, int nsize);
static int sendack(CLIENT *c, unsigned short  blknum);
static int senderr(CLIENT *c, unsigned short  e_code);
static int sendblock(CLIENT *c);
static int validate_access(const char *filename, int mode);
static int init_con(CLIENT *c, char *buf, int rb);
static int new_rrq(CLIENT *c);
static int new_wrq(CLIENT *c);
static int con_rrq(CLIENT *c, char *buf);
static int con_wrq(CLIENT *c, char *buf, int rb);
static CLIENT *new_list();
static CLIENT *new_client(int sfd, struct sockaddr_storage *addr);
static CLIENT *add_list(CLIENT *master, CLIENT *new_client);
static int free_client(CLIENT *c);
static int remove_list(CLIENT *master, CLIENT *rem_client);
static CLIENT *find_client(CLIENT *master, CLIENT *c);
static void free_list(CLIENT *master);



/*
 * Helper functions to support IPv4 & IPv6
 */
static void *get_in_addr(struct sockaddr *a) {
    if (a->sa_family == AF_INET)
        return &(((struct sockaddr_in*)a)->sin_addr);
    return &(((struct sockaddr_in6*)a)->sin6_addr);
}
static unsigned int get_in_port(struct sockaddr *a) {
   if (a->sa_family == AF_INET)
       return ntohs(((struct sockaddr_in*)a)->sin_port);
   else
       return ntohs((((struct sockaddr_in6*)a)->sin6_port));
}



/*
 * Helper functions data manipulation
 */
static void _pack_u16(char *buf, unsigned short a) {
    *buf++ = (a>>8)&0xFF;
    *buf++ = a&0xFF;
}
static unsigned short _unpack_u16(char *buf) {
    return (buf[0]<<8) | (buf[1])&0xFF;
}
static unsigned short _htons(unsigned short a) {
    unsigned short r = a;
#ifdef __x86_64__
    // If little endian
    r = (a<<8)|((a>>8)&0xFF);
#endif
    return r;
}
static unsigned short _ntohs(unsigned short a) {
    return _htons(a);
}
// Split the src data until element is encountered
static int _parse(char *dest, const char *src, char elem, size_t n) {
    size_t i = 0;
    for (; i<n; i++) {
        if (src[i] == elem) {
            break;
        }
        dest[i] = src[i];
    }
    dest[i] = 0;
    return i+1;
}
static char *_tolower(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i<n; i++) {
        if ((src[i] >= 65) && (src[i] <= 90)) {
            dest[i] = src[i]+32;
        } else {
            dest[i] = src[i];
            if (src[i] == 0)
                break;
        }
    }
    if (dest[i] != 0)
        dest[i+1] = 0;
    return dest;
}



/*
 * Main function. Sets server socket up & linked list
 */
int main(int argc, char *argv[]) {

    
    /* Prepare server socket */
    int sockfd6;
    int running = 1;
    
    struct sockaddr_in6 ipv6_server;
    memset(&ipv6_server, 0, sizeof(ipv6_server));
    ipv6_server.sin6_family     = AF_INET6;
    ipv6_server.sin6_port       = htons(PORT);
    ipv6_server.sin6_addr       = in6addr_any;

    if ((sockfd6 = socket(ipv6_server.sin6_family, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr,"Could not create IPv6 socket!\n");
        return 1;
    }

    // Bind sockets
    if (bind(sockfd6, (struct sockaddr *)&ipv6_server, 
             sizeof(ipv6_server)) == -1)  {
        fprintf(stderr,"Could not bind IPv6 socket!\n");
        return 2;
    }
    
    /* Handle signals for daemon */
    // SIGINT
    
    
    /* Start receiving */
    
    printf("[INFO] tftpd ready! Waiting for datagrams!\n");
    
    struct sockaddr_storage their_addr;
    unsigned int socklen = sizeof(their_addr);
    char *buf = malloc(bufsize);     // TODO: Have MAXBUF update. If there is one client with a higher block size than MAXBUF -> Increase maxbuf
    
    CLIENT *master = new_list();
    
    while (running) {
        
        // Clear buffer
        memset(buf, 0, bufsize);
        
        // select/pselect stuff
        fd_set readfs;
        FD_ZERO(&readfs);
        FD_SET(sockfd6, &readfs);
        
        // TODO: Update this with minimum time required for next timeout.
        struct timespec tv;//timeval tv;
        tv.tv_sec=1;
        //tv.tv_usec=0;
        tv.tv_nsec=0;
        
        int sel = pselect(sockfd6+1, &readfs, NULL, NULL, &tv, NULL);
        
        if (sel==0) {
            // TODO : Timeout -> Calculate timeouts for clients
            
            continue;
        }
        if (sel<0) {
            perror("[FATAL] select() fuckin failed");
            // stop server after 5 fatal errors
            static char errors;
            errors++;
            if (errors > 5) 
                running = -1;
            continue;
        }
        
        // Receive data
        int recvbytes = recvfrom(sockfd6, buf, bufsize, 0, 
                                 (struct sockaddr*)&their_addr, &socklen);

#if DEBUG==1
        char s[INET6_ADDRSTRLEN];
        printf("[INFO] Received (%d) from %s:%d -> ", recvbytes,
            inet_ntop(their_addr.ss_family, 
            get_in_addr((struct sockaddr*)&their_addr), 
            s, INET6_ADDRSTRLEN), 
            get_in_port((struct sockaddr*)&their_addr));
        
        unsigned char *as = (get_in_addr((struct sockaddr *)&their_addr));
        for (int i=0; i<16; i++) {
            printf("%x", as[i]);
            if ((i+1)%2==0)
                printf(":");
        }
#endif

        handle_packet(master, sockfd6, &their_addr, buf, recvbytes);
        
#if DEBUG == 1
        printf("\n");
#endif        
    }
    free_list(master);
    free(buf);
    close(sockfd6);
    return 0;
}

static int handle_packet(CLIENT *master, int sockfd, struct sockaddr_storage *addr,
                         char *buf, int recvbytes) {
    CLIENT *current_client  = new_client(sockfd, addr);
    CLIENT *c               = find_client(master, current_client);

    // Get OPCODE
    enum OPCODE opcode = _unpack_u16(buf);

    // If it does not exist already.
    if (c==NULL && (opcode==RRQ || opcode == WRQ)) {
        // Make a new client
        c = add_list(master, current_client);
        if (init_con(c, buf, recvbytes) == 0) {
            printf("RRQ or WRQ\n");
            opcode==RRQ ? new_rrq(c) : new_wrq(c);
            goto clean;
        }
        // Invalid -> remove client
        remove_list(master, c);
    }
    if (c != NULL) {
        switch (opcode) {
        case DATA:  // WRQ
            printf("Data! : ");
            
            con_wrq(c, buf, recvbytes);
            if (recvbytes < c->blocksize+4) {
                remove_list(master, c);
            }
            break;
        case ACK:   // RRQ
            printf("Acknowledgement : ");

            if (con_rrq(c, buf) < c->blocksize+4) {
                remove_list(master, c);
            }
            break;
        case ERROR:
            printf("Error : ");
            break;
        case LIST:
        case CD:
        case PWD:
        case MKDIR:
            printf("Unimplemented feature requested!\n");
            break;
        default:
            fprintf(stderr, "Invalid operation code!\n");
            break;
    };
    }
    
clean:
    free_client(current_client);
    return 0;
}

/*
 * tftp functions
 */
static int _sendto(CLIENT *c, char *buf, int nsize) {
    // Build address packet 
    struct sockaddr_in6 sa6;
    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family         = c->s_family;
    memcpy(sa6.sin6_addr.s6_addr, c->s_addr,16);
    sa6.sin6_port           = htons(c->s_port);
    // Send it 
    return sendto(c->sfd, buf, nsize, 0, (struct sockaddr*)&sa6, sizeof(sa6));
}
static int sendack(CLIENT *c, unsigned short blknum) {
    char buf[4];
    unsigned short a = ACK;
    _pack_u16(buf, a);
    _pack_u16(buf+2, blknum);
    _sendto(c, buf, 4);
}
static int senderr(CLIENT *c, unsigned short e_code) {
    char buf[40];
    _pack_u16(buf, ERROR);
    _pack_u16(buf+2, e_code);
    char *str = "Some error";
    // Iterate through list!!
    strcpy(buf+4, str);
    _sendto(c, buf, strlen(str));
}
static int sendblock(CLIENT *c) {
    // Dynamically allocate memory
    char *buf = malloc(c->blocksize+4);
    if (buf==NULL) {
        perror("Not enough memory!");
        return -1;
    }
    // Build header
    _pack_u16(buf, DATA);
    _pack_u16(buf+2, c->curblock);
    
    // Set pos
    if (c->file == NULL) {
        printf("File not opened\n");
        return -2;
    }
    fseek(c->file, c->fileprog, SEEK_SET); //c->blocksize*(c->curblock-1)
    // Read file & copy into buffer
    int readbytes = fread(buf+4, 1, c->blocksize, c->file);
    if (readbytes <= 0) {
        perror("fread()");
        return -3;
    }
    // Send it
    int ret = _sendto(c,buf,readbytes+4);
    // Free dynamic allocated memory
    free(buf);
    return ret;
}
// TODO Check whether we've got access and directory stuff
static int validate_access(const char *filename, int mode) {
    return 0;
}
// Gets options and initialize stuff
// TODO Include switch statement.
static int init_con(CLIENT *c, char *buf, int rb) {
    char *filename = malloc(FILENAMESIZE);
    // Filename
    int fileoffset = _parse(filename, buf+2, 0, rb) + 2;
        
    // Mode
    char mode[9];
    int parseoffset = _parse(mode, buf+fileoffset, 0, rb) + fileoffset;
    _tolower(mode, mode, 8);
    
    if (strncmp(mode,"octet",5)!=0) {
        senderr(c, EBADOP);
        return 1;
    }
    // Binary mode! 
    printf("Binary mode -> %s\n",filename);
    c->filename     = filename;
    
    // TODO: Get options (RFC 2348 & 2349)
    int optionoffset = parseoffset;
    
    int i=0;
    for (i=parseoffset; i<rb; i+=optionoffset) {
        char option[8];
        int offopname = _parse(option, buf+optionoffset, 0, rb)+optionoffset;
        char optiondata[20];
        int optiondataoff = _parse(optiondata, buf+offopname, 0, rb) + offopname;
        printf("Client option: '%s' equal to %s\n",offopname,optiondataoff);
        optionoffset += offopname +optiondataoff;
            
        // now apply these functions to the client struct
        _tolower(option,option,8);
        if (strncmp(option,"blksize",7)==0) {
            c->blocksize = atoi(optiondata);
            c->operation = (0x80|c->operation);
            // TODO: Answer with OACK
        }
        if (strncmp(option,"timeout",7)==0) {
            c->set_timeout = atoi(optiondata);
            c->operation = (0x80|c->operation);
            // TODO: Answer with OACK
        }
        if (strncmp(option,"tsize",5)==0) {
            c->filesize = atoi(optiondata);
            c->operation = (0x80|c->operation);
            // TODO: Answer with OACK
        }
    }
    // WARNING TODO: if there were options. You have to expect differently. this is why I check if ==0 
    
    char s[INET6_ADDRSTRLEN];
    printf("[INFO] Received (%d b) from %s:%d -> ", rb,
            inet_ntop(c->s_family, c->s_addr, 
            s, INET6_ADDRSTRLEN), c->s_port);
    unsigned char *as = c->s_addr;
    printf("[INFO] Connection from ( ");
    for (int i=0; i<16; i++) {
        printf("%x", as[i]);
        if ((i+1)%2==0)
            printf(":");
    }printf(":%d)\n",c->s_port);
    
    if (validate_access(filename, c->operation)!=0) {
        return 2;
    }
    return 0;
}
static int new_rrq(CLIENT *c) {
    c->file = fopen(c->filename, "r");
    if (c->file == NULL) {
        printf("[FATAL] Could not open file!\n");
        senderr(c, ENOTFOUND);
        return -1;
    }
    // Get filesize
    fseek(c->file, 0, SEEK_END);
    c->filesize = ftell(c->file);
    printf("File is %u bytes big\n",c->filesize);
    fseek(c->file, 0, SEEK_SET);

    c->curblock = 1;
    c->resends = -1;// for options See con_rrq()
    sendblock(c);
}
static int new_wrq(CLIENT *c) {
    c->file = fopen(c->filename, "w");
    sendack(c, 0);
    // This is it... lmao WARNING
}
// Receive data -> [2] ->opcode; [2] ->block#(starts at 1); n bytes = data
static int con_rrq(CLIENT *c, char *buf) {
    if (c==NULL) {
        printf("No valid client given\n");
        return -1;
    }
    // get block number
    unsigned short clientblock = _unpack_u16(buf+2);
    if (clientblock == 0 && (c->operation>>7)&0x01 == 1 && c->fileprog == 0) {
        // WARNING What if overflow?
        // Options were acknowledged
        printf("Options!");
    }
    
    // Some UI stuff
    // Get total size
    
    int maxwidth = 50;
    float prog = (float)c->fileprog/((float)c->filesize);//(float)c->curblock/((float)c->filesize/(float)c->blocksize); // aantal benodigde blokken
    int progd = (int)(maxwidth*prog);
    printf("[");
    for (int i=0; i<progd; i++)
        printf("-");
    for (int i=progd;i<maxwidth;i++)
        printf(" ");
    if (prog>1) prog = 1;
    printf("] %f%", prog*100);
//    fflush(stdout);

    if (clientblock == c->curblock) {
        if (clientblock == 0xFFFF)
            printf("\tWARNING: OVERFLOW!");
        // Client is on track!
        c->curblock++;
        // Progess fileprog
        c->fileprog+=c->blocksize;
    } else {
        // Resend!
        c->resends++;
    }
    printf("\n");
    return sendblock(c);
}
static int con_wrq(CLIENT *c, char *buf, int rb) {
    if (c==NULL) {
        printf("No valid client given\n");
        return -1;
    }
    unsigned short clientblock = _unpack_u16(buf+2);
    
    // Check if expected blocks etc
    if (clientblock == c->curblock+1) {
        // Get data
        fseek(c->file, c->fileprog, SEEK_SET);
        fwrite(buf+4, 1, rb-4, c->file);
        sendack(c,clientblock);
        c->fileprog += c->blocksize;
        c->curblock++;
        
    } else {
        sendack(c,c->curblock);
    }
}



/*
 * Linked list functions
 */
// Make a new linked list; return pointer to first element
static CLIENT *new_list() {
    CLIENT *c = calloc(1,sizeof(CLIENT));
    if (c==NULL) {
        perror("Not enough memory!");
        return NULL;
    }
    c->next = NULL;
    return c;
}
static CLIENT *new_client(int sfd, struct sockaddr_storage *addr) {
    CLIENT *newc = calloc(1, sizeof(CLIENT));
    if (newc==NULL) {
        perror("Not enough memory!");
        return NULL;
    }
    newc->sfd      = sfd;
    newc->s_family = addr->ss_family;

    memcpy(newc->s_addr, ((struct sockaddr_in6*)addr)->sin6_addr.s6_addr, 16);//get_in_addr((struct sockaddr*)addr).s6_addr, 16);
    newc->s_port   = ntohs(((struct sockaddr_in6*)addr)->sin6_port);//get_in_port((struct sockaddr*)&addr);
    newc->blocksize     = 512;
    newc->fileprog      = 0;
    // Nano seconds
    newc->resting_time  = TIMEOUT*1E9;
    newc->set_timeout   = TIMEOUT*1E9;
    return newc;
}
// Add a client at the end of the linked list; return pointer to it
static CLIENT *add_list(CLIENT *master, CLIENT *new_client) {
    CLIENT *a = malloc(sizeof(CLIENT));
    if (a==NULL) {
        perror("Not enough memory!");
        return NULL;
    }
    memcpy(a, new_client, sizeof(CLIENT));
    a->next = NULL;
    // Iterate until the end of the linked list
    CLIENT *i = NULL;
    for (i = master; i->next != NULL; i=i->next);
    // Point last element to the new client
    i->next = a;
    
    return a;
}
static int free_client(CLIENT *c) {
    if (c == NULL)
        return -1;
    if (c->file != NULL)
        fclose(c->file);
    if (c->filename != NULL)
        free(c->filename);
    free(c);
    return 0;
}
// Remove a client from the linked list
static int remove_list(CLIENT *master, CLIENT *c) {
    if (c==NULL) {
        return -1;
    }
    CLIENT *prev = master;
    for (CLIENT *cur = master; cur != NULL; cur = cur->next) {
        if ((memcmp(cur->s_addr, c->s_addr, 16) == 0) && 
            (cur->s_port == c->s_port)) {
            
            // We got a match -> Remove client
            CLIENT *next = cur->next;
            free_client(cur);
        
            prev->next = next;
            return 0;
        }
        prev = cur;
    }
    return 1;
}
// Find if client already exists
static CLIENT *find_client(CLIENT *master, CLIENT *c) {
    for (CLIENT *cur = master; cur != NULL; cur = cur->next) {
        if ((memcmp(cur->s_addr, c->s_addr, 16) == 0) && 
            (cur->s_port == c->s_port)) {
            return cur;
        }
    }
    return NULL;
}
static void free_list(CLIENT *master) {
    CLIENT *next = NULL;
    CLIENT *cur = master;
    
    while (cur!=NULL) {
        next = cur->next;
        free_client(cur);
        cur = next;
    }
}
