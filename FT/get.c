#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#define BUFLEN 1024

int getfile(int con,char *name,int size) {
	FILE *f;
	int l;
	unsigned char *buf = malloc(BUFLEN);
	if ((f=fopen(name,"wb"))==NULL) {
		printf("Error: Failed to open file %s\n",name);
		write(con,'0',1);
		return -1; // Failed to open file
	}
	write(con,'R',1); // Ready
        while ((l=read(con,buf,BUFLEN))) {
                if (buf[0]=='E'){ // End
         	       write(con,'E',1); // End
                       break;
                }
                fwrite(buf,1,BUFLEN,f); // binary
                memset(buf,0,BUFLEN);
        }
	printf("File '%s' downloaded\n",name);
	free(buf);
	fclose(f);
	return 0;
}

char *getstr(char *str,char split,int size) {
        char *b = malloc(size);
        int x=0;
        for (x;x<size;x++) {
                if (str[x]==split) break;
                b[x] = str[x];
        }
        return b;
}

int getloc(char *str,char split,int size) {
	int x=0;
	for (x;x<size;x++) {
		if (str[x]==split) break;
	}
	return x+1;
}

char *nextstr(char *str,int size,int start) {
	char *b=malloc(size-start);
	int x=start;
	for (x;x<size-start;x++) {
		b[x-start]=str[x];
	}
	return b;
}

int main(int argc, char *argv[]) {
	struct sockaddr_in sime,sicl;
	int s,port=2100,l,slen=sizeof(sime);

	if (argc==2) {
		if ((port = atoi(argv[1]))==-1 || port<1 || port > 65535) {
			printf("Usage: %s [port]",argv[0]);
			return -1;
		}
	}
	printf("[GET] Using port %d\n",port);

	char *buf = malloc(BUFLEN);

	if ((s=socket(AF_INET,SOCK_STREAM,0))==-1) {
		printf("Error: Could not create socket\n");
		return -1;
	}

	memset(&sime,0,sizeof(sime));
	sime.sin_addr.s_addr = INADDR_ANY;
	sime.sin_family = AF_INET;
	sime.sin_port = htons(port);

	if (bind(s,(struct sockaddr *)&sime,slen)==-1) {
		printf("Error: Could not bind port\n");
		return -1;
	}
	printf("Listening for a connection\n");
	listen(s,1);

	char filename[128];
	int cl,size;
	slen = sizeof(sicl);

	cl = accept(s,(struct sockaddr *)&sicl,&slen);
	printf("Connected with %s:%d\n",inet_ntoa(sicl.sin_addr),ntohs(sicl.sin_port));

	l = read(cl,buf,BUFLEN);
	if (l>0) {
		char *d = getstr(buf,0,256);
		if (d[0]=='S') {
			free(d);
        		d=nextstr(buf,256,2);
        		char *d2 = getstr(d,'|',256);
        		strcpy(filename,d2);
        		free(d2);
        		int d3 = getloc(d,'|',256);
       	 		d2 = nextstr(d,256,d3);
       			char *d4 = getstr(d2,'|',256);
      	 		size=atoi(d4);
        		free(d2);
		        free(d4);
        		printf("Downloading %s (%d)\n",filename,size);
			getfile(cl,filename,size);
		        free(d);

		}
	}
	close(s);
	free(buf);
	return 0;
}
