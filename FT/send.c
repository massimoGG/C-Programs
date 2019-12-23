#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFLEN 1024

int sendfile(int con,char *name,int size) {
	FILE *f;
	uint8_t *b=malloc(BUFLEN);
printf(">%d %s %d",con,name,size);
	read(con,b,BUFLEN);
	if ((f=fopen(name,"rb"))==NULL) {
		printf("Error: Failed to open file %s\n",name);
		return -1; // Failed to open file
	}
printf("Received: %s\n",b);
	if (b[0]=='R') {// It is ready to receive data
		while (fread(b,sizeof(b),1,f)) {
			write(con,b,BUFLEN);
			memset(b,0,BUFLEN);
		}
		write(con,'E',1);
		read(con,b,1);
		if (b[0]=='E') {
			printf("File '%s' uploaded\n",name);
		} else printf("Error: Downloader failed (finish)\n");
	} else printf("Error: Downloader failed (Ready)\n");
	free(b);
	fclose(f);
	return 0;
}

int main(int argc, char *argv[]) {
	struct sockaddr_in sime;
	int s,port=2100,l,size,slen=sizeof(sime);
	char filename[128],host[128];

	if (argc<3 || argc >4) {
		printf("Usage: %s <ip> [port] <filename>\n",argv[0]);
		return -1;
	}
	if (argc==3 || argc==4) {
		strcpy(host,argv[1]);
		if (argc==3) {
			strcpy(filename,argv[2]);
		}
		if (argc==4) {
			port = atoi(argv[2]);
			if (port<1 || port > 65535) {
				printf("Usage: %s <ip> [port] <filename>\n",argv[0]);
				return -1;
			}
			strcpy(filename,argv[3]);
		}
	} else return -1;
	FILE *f;
	f= fopen(filename,"r");
	if (!f) {
		printf("Error: File does not exit!\n");
		return -1;
	}
	fseek(f, 0L, SEEK_END);
	size = ftell(f);
	fclose(f);
	printf("[SEND] Connecting to %s:%d\n",host,port);
	char *buf = malloc(BUFLEN);

	if ((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		printf("Error: Could not create socket\n");
		return -1;
	}

	memset(&sime,0,sizeof(sime));
	sime.sin_addr.s_addr = inet_addr(host);
	sime.sin_family = AF_INET;
	sime.sin_port = htons(port);

	if (connect(s,(struct sockaddr *)&sime,slen) ==-1) {
		printf("Error: Could not connect to server\n");
		return -1;
	}

	char str[256];
	sprintf(str,"S|%s|%d|",filename,size);
	write(s,str,256);
puts("Sent data");
	sendfile(s,filename,size);
	close(s);
	free(buf);
	return 0;
}
