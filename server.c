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

#include "common.h"

int main(int argc, char *argv[])
{
  int listenfd;
  int connfd[NUM_LINKS];
  struct sockaddr_in local_addr;
  struct sockaddr_in serv_addr;
  int sockfd;
  int i;
  char buf[NUM_LINKS * BLOCK_SIZE];
  int res;
  int on = 1;

  if (argc != 4) {
    fprintf(stderr,"Usage: server server-ip server-port local-port\n");
    return 1;
  }

  /* Listen on any address, and local-port (argv[3]) */
  memset(&local_addr, '0', sizeof(local_addr));

  local_addr.sin_family = AF_INET;
  local_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  local_addr.sin_port = htons(atoi(argv[3]));
  
  listenfd = socket(AF_INET, SOCK_STREAM, 0);

  /*************************************************************/
  /* Allow socket descriptor to be reuseable                   */
  /*************************************************************/
  int rc = setsockopt(listenfd, SOL_SOCKET,  SO_REUSEADDR,
                  (char *)&on, sizeof(on));
  if (rc < 0)
    {
      perror("setsockopt() failed");
      close(listenfd);
      exit(-1);
    }

  res = bind(listenfd, (struct sockaddr*)&local_addr, sizeof(local_addr)); 

  if (res < 0) {
    perror("Bind error:");
    return 0;
  }
  listen(listenfd, 10); 

  for (i=0;i<NUM_LINKS;i++) {
    fprintf(stderr,"Waiting for connection %d\n",i);
    connfd[i] = accept(listenfd, (struct sockaddr*)NULL, NULL);
    if (ioctl(connfd[i], (int)FIONBIO, (char *)&on))
      {
	printf("ioctl FIONBIO call failed\n");
      }
    fprintf(stderr,"Accepted.\n");
  }

  fprintf(stderr,"We now have %d connections, start sending data\n",NUM_LINKS);

  /* Now connect to our server */

  memset(&serv_addr, '0', sizeof(serv_addr)); 

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(atoi(argv[2]));

  if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
  {
    printf("\n inet_pton error occured\n");
    return 1;
  } 

  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
  {
    printf("\n Error : Could not create socket \n");
    return 1;
  } 

  if( connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
  {
    printf("\n Error : Connect Failed \n");
    return 1;
  } 

  if (ioctl(sockfd, (int)FIONBIO, (char *)&on)) {
    fprintf(stderr,"ioctl FIONBIO call failed\n");
  }

  fprintf(stderr,"Connected to %s on socket %s\n",argv[1],argv[2]);

  /* We are now connected, start transferring data 
     Incoming data on connfd[0] is written to sockfd
     Incoming data on sockfd is distributed to connfd[0..NUM_LINKS-1]
   */

  int counts[NUM_LINKS];
  for (i=0;i<NUM_LINKS;i++) { counts[i] = 0; }

  fd_set readfds,writefds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);

  FD_SET(sockfd, &readfds);
  FD_SET(connfd[0], &readfds);
  int max_readfd = (sockfd > connfd[0] ? sockfd+1 : connfd[0]+1);

  int max_writefd = 0;
  for (i = 0; i < NUM_LINKS; i++) {
    FD_SET(connfd[i], &writefds);
    if (connfd[i] > max_writefd) { max_writefd = connfd[i]; }
  }
  max_writefd++;

  int64_t blockno = 0;
  int n;
  char inbuf[1024];
  while (1) {
    /* Wait for incoming data on either socket */
    int res;
    int bytesread = 0;
    while (bytesread < NUM_LINKS * BLOCK_SIZE) {
  struct timeval tv;
      /* wait for the socket to become ready for receiving */ 
      //fprintf(stderr,"Waiting on socket for reading...\n");
  tv.tv_sec = 0;
  tv.tv_usec= 5000; /* 5ms */
      res = select(max_readfd, &readfds, NULL, NULL, &tv);
res = 2;
      if (res > 0) {
        /* First try to read connfd[0] */
        n = read(connfd[0], inbuf, sizeof(inbuf));
        if (n > 0) {
          fprintf(stderr,"Read %d bytes from connfd[0]\n",(int)n);
          int n2 = write(sockfd, inbuf, n);
          fprintf(stderr,"Wrote %d bytes to sockfd - res=%d\n",(int)n, (int)n2);
        }

        /* Now try to read from sockfd */
        n = read(sockfd, buf+bytesread, NUM_LINKS * BLOCK_SIZE - bytesread);
        if (n > 0) {
          //fprintf(stderr,"Recieved %d bytes\n",(int)n);
          bytesread += n;
        }
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
