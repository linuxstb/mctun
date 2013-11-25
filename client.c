#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <arpa/inet.h> 

#include "common.h"

#define QUEUE_SIZE 128
int last_block_written = -1;
int blocks_in_queue = 0;
unsigned char qbuf[(BLOCK_SIZE + 8) * QUEUE_SIZE];

void dump_queue(void)
{
  int i = 0;
  int64_t b;
  unsigned char* p = qbuf;
  for (i=0;i<blocks_in_queue;i++) {
    b = *(int64_t*)p;
    fprintf(stderr,"%d: %d\n",i,(int)b);
    p += BLOCK_SIZE + 8;
  }
}

void empty_queue(int sockfd)
{
  int i = 0;
  int64_t b;
  unsigned char* p = qbuf;
  int blocks_to_write = 0;

  while (i < blocks_in_queue) {
    b = *(int64_t*)p;
    if (b == last_block_written + 1 + i) {
      blocks_to_write++;
    } else {
      break;
    }
    p += BLOCK_SIZE + 8;
    i++;
  }

  if (blocks_to_write == 0)
    return;

  p = qbuf;
  for (i=0;i<blocks_to_write;i++) {
    int n = write(sockfd, p + 8, BLOCK_SIZE);
    if (n != BLOCK_SIZE) {
      fprintf(stderr,"Short write: %d\n",n);
    }
    p += 8 + BLOCK_SIZE;
  }

  last_block_written += blocks_to_write;

  if (blocks_in_queue == blocks_to_write) {
    blocks_in_queue = 0;
  } else {
    /* Move remaining queue items to front */
    blocks_in_queue -= blocks_to_write;
    memmove(qbuf, p, (BLOCK_SIZE + 8) * blocks_in_queue);
  }
  //fprintf(stderr,"Written %d blocks, new queue:\n",blocks_to_write);
  //dump_queue();  
}

void add_to_queue(int64_t blockno, unsigned char* buf)
{
  if (blocks_in_queue == QUEUE_SIZE) {
    fprintf(stderr,"ERROR: Output queue full\n");
    //dump_queue();
    exit(1);
  }

  int i = 0;
  int64_t b;
  unsigned char* p = qbuf;
  while (i < blocks_in_queue) {
    b = *(int64_t*)p;
    //fprintf(stderr,"Adding %lld: i=%d ,blocks_in_queue=%d, blockno=%lld\n",blockno,i,blocks_in_queue,b);
    if (b > blockno) {
      /* Current block is after new block, insert here. */
      memmove(p + BLOCK_SIZE+8, p, (blocks_in_queue - i) * (BLOCK_SIZE + 8));
      memcpy(p, &blockno, 8);
      memcpy(p + 8, buf, BLOCK_SIZE);
      blocks_in_queue++;
      return ;
    }

    p += BLOCK_SIZE + 8;
    i++;
  }

  /* If we have got this far, we need to add to the end */
  memcpy(p, &blockno, 8);
  memcpy(p + 8, buf, BLOCK_SIZE);
  blocks_in_queue++;
}

double get_time(void)
{
  struct timeval tv;

  gettimeofday(&tv,NULL);

  double x = tv.tv_sec;
  x *= 1000;
  x += tv.tv_usec / 1000;

  return x;
}

int main(int argc, char *argv[])
{
  int connfd[NUM_LINKS];
  int sockfd;
  int listenfd;
  int i,n;
  struct sockaddr_in serv_addr, local_addr;
  int res;
  int on = 1;

  if (argc != 4) {
    fprintf(stderr,"client remote-ip remote-port local-port\n");
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

  fprintf(stderr,"Waiting for connection on port %s\n",argv[3]);
  sockfd = accept(listenfd, (struct sockaddr*)NULL, NULL);
  if (ioctl(sockfd, (int)FIONBIO, (char *)&on))
  {
    fprintf(stderr,"ioctl FIONBIO call failed\n");
  }
  fprintf(stderr,"Accepted.\n");


  /* Now connect to server */
  memset(&serv_addr, '0', sizeof(serv_addr)); 

  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(atoi(argv[2]));

  if(inet_pton(AF_INET, argv[1], &serv_addr.sin_addr)<=0)
  {
    printf("\n inet_pton error occured\n");
    return 1;
  } 


    for (i=0;i<NUM_LINKS;i++) {
      if((connfd[i] = socket(AF_INET, SOCK_STREAM, 0)) < 0)
      {
        printf("\n Error : Could not create socket \n");
        return 1;
      } 

      if( connect(connfd[i], (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
      {
       printf("\n Error : Connect Failed \n");
       return 1;
      } 

      int on = 1;
      if (ioctl(connfd[i], (int)FIONBIO, (char *)&on))
	{
	  printf("ioctl FIONBIO call failed\n");
	}

      printf("Connection %d connected\n",i);
    }

    fd_set readfds;
     
    /* Initialize the set of active sockets. */
    FD_ZERO (&readfds);
    int max_fd = 0;
    for (i = 0; i < NUM_LINKS; i++) {
      FD_SET (connfd[i], &readfds);
      if (connfd[i] > max_fd) { max_fd = connfd[i]; }
    }
    max_fd++;
    fprintf(stderr,"%d %d %d - max_fd=%d\n",connfd[0],connfd[1],connfd[2],max_fd);

    double last_time = get_time();

    int bytesread = 0;
    unsigned char buf[NUM_LINKS][BLOCK_SIZE+8];
    int x[NUM_LINKS];
    char inbuf[1024];
    for (i=0;i<NUM_LINKS;i++) { x[i] = 0; }
    while (1) {
      n = read(sockfd, inbuf, sizeof(inbuf));
      if (n > 0) {
        fprintf(stderr,"Read %d bytes from sockfd\n",(int)n);
        int n2 = write(connfd[0], inbuf, n);
        fprintf(stderr,"Wrote %d bytes to connfd[0] - res=%d\n",(int)n, (int)n2);
      }

      n = select(max_fd, &readfds, NULL, NULL, NULL);
      n = 1;
      if (n < 0) { 
        perror("select failed");
      } else {
        for (i = 0; i < NUM_LINKS; i++) {
	  //          if (FD_ISSET(connfd[i],&active_fd_set)) {  // This doesn't seem to work!a
            n = read(connfd[i], buf[i]+x[i], BLOCK_SIZE+8-x[i]); 
            if (n > 0) {
	      //fprintf(stderr,"Read %d from %d\n",n,i);
              bytesread += n;
              x[i] += n;
              if (x[i] == BLOCK_SIZE + 8) {
                int64_t blockno = *(int64_t*)&buf[i][0];
                //fprintf(stderr,"Received block %lld\n",blockno);
                add_to_queue(blockno,&buf[i][8]);
                x[i] = 0;
	      }
            }
	    //          }
        }
        empty_queue(sockfd);
      }
      if (get_time() > last_time + 1000.0) {
        fprintf(stderr,"%d bits/s (%dKB/s)\n",bytesread*8,bytesread/1024);
        bytesread = 0;
        last_time = get_time();
      }
    }
    return 0;
}
