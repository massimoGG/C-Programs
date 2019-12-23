#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFLEN 1024

int main(int argc, char *argv[]) {
	struct sockaddr_in si,sicl;
	int s,cli,port,slen = sizeof(si);
	FILE *f;

	if ((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		printf("Error: Failed to create socket\n");
		return -1;
	}
	memset(&si,0,sizeof(si));
	si.sin_family = AF_INET;
	si.sin_addr.s_addr = INADDR_ANY;
	si.sin_port = htons(2100);
	if (bind(s,(struct sockaddr *)&si,slen)==-1) {
		printf("Error: Failed to bind port\n");
		return -1;
	}
	listen(s,1);
	// Open file
	if ((f=fopen("test","wb"))==NULL) {
		printf("Failed to open file\n");
		return -1;
	}
	cli = accept(s,(struct sockaddr *)&sicl,&slen);
	printf("Connected with %s:%d\n",inet_ntoa(sicl.sin_addr),ntohs(sicl.sin_port));
	unsigned char *buf = malloc(BUFLEN);
	int l;
	while ((l=read(cli,buf,BUFLEN))) {
		if (l<0) {
			printf("Error when receiving\n");
			break;
		}
		fwrite(buf,sizeof(char),l,f);
printf("Line written: %d\n",l);
	}
	fclose(f);
	printf("File downloaded\n");
	close(cli);
	return 0;
}
