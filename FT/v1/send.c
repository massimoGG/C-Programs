#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFLEN 1024

int main(int argc, char *argv[]) {
	struct sockaddr_in si;
	int s,port,slen = sizeof(si);
	FILE *f;

	if ((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		printf("Error: Failed to create socket\n");
		return -1;
	}
	memset(&si,0,sizeof(si));
	si.sin_family = AF_INET;
	si.sin_addr.s_addr = inet_addr("127.0.0.1");
	si.sin_port = htons(2100);
	if (connect(s,(struct sockaddr *)&si,slen)==-1) {
		printf("Error: Failed to connect to server\n");
		return -1;
	}
	if ((f=fopen("test","rb"))==NULL) {
		printf("Error: Failed to open file\n");
		return -1;
	}
	unsigned char *buf = malloc(BUFLEN);
	int l=0;
puts("Connected");
	while ((l=fread(buf,sizeof(char),BUFLEN,f))>0) {
puts("File -> Socket");
		if (write(s,buf,l)<l) {
			puts("Error when sending line(write<fread)");
			break;
		}
		memset(buf,0,BUFLEN);
	}
printf("File uploaded in binary mode (%d bytes)\n",l);
	fclose(f);
	close(s);
	return 0;
}
