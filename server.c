#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h> 
#include <fcntl.h> 
#include <sys/ioctl.h>
//#include <asm/ioctls.h>

#include "common.h"

int main(int argc, char *argv[])
{
  int listenfd;
  int connfd[NUM_LINKS];
  struct sockaddr_in serv_addr;
  int i;
  char buf[NUM_LINKS * BLOCK_SIZE];

  memset(&serv_addr, '0', sizeof(serv_addr));

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  serv_addr.sin_port = htons(4444);
  
  listenfd = socket(AF_INET, SOCK_STREAM, 0);

  int res = bind(listenfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)); 

  if (res < 0) {
    perror("Bind error:");
    return 0;
  }
  listen(listenfd, 10); 

  for (i=0;i<NUM_LINKS;i++) {
    fprintf(stderr,"Waiting for connection %d\n",i);
    connfd[i] = accept(listenfd, (struct sockaddr*)NULL, NULL);
    int on = 1;
    if (ioctl(connfd[i], (int)FIONBIO, (char *)&on))
      {
	printf("ioctl FIONBIO call failed\n");
      }
    fprintf(stderr,"Accepted.\n");
  }

  fprintf(stderr,"We now have %d connections, start sending data\n",NUM_LINKS);

  int counts[NUM_LINKS];
  for (i=0;i<NUM_LINKS;i++) { counts[i] = 0; }

  int64_t blockno = 0;
  while (1) {
    int bytesread = 0;
    while (bytesread < NUM_LINKS * BLOCK_SIZE) {
      int n = read(0, buf+bytesread, (NUM_LINKS * BLOCK_SIZE) - bytesread);
      if (n >= 0) bytesread += n;
      if (n < 0) { 
        perror("Read error:");
      }
    }

    //fprintf(stderr,"Read %d bytes\n",bytesread);
    int byteswritten = 0;
    int x[NUM_LINKS];
    for (i=0;i<NUM_LINKS;i++) { x[i] = 0; }
    while (byteswritten < NUM_LINKS * BLOCK_SIZE) {
      for (i=0;i<NUM_LINKS;i++) {
        if (x[i] == 0) {
          int written = 0;
          while (written < 8) {
            int n = write(connfd[i],&blockno + written,8 - written);
            if (n > 0) { written += n; }
          }
          blockno++;
        }
        if (x[i] < BLOCK_SIZE) {
          int n = write(connfd[i],buf + (i * BLOCK_SIZE) + x[i], BLOCK_SIZE - x[i]);
          if (n > 0) { byteswritten += n; x[i] += n; }
        }
        //if (n < 0) { perror("Write error:"); usleep(10000); }
        //fprintf(stderr,"Done - %d\n",n);
       }
    }
  }

  for (i=0;i<NUM_LINKS;i++) {
    fprintf(stderr,"Wrote %d bytes to connection %d\n",counts[i],i);
  }

  for (i=0;i<NUM_LINKS;i++) {
    close(connfd[i]);
  }

}
