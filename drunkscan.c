/* TESTED ON: FreeBSD 3.x
 * TODO: break down into more functions: filling our structs,	*
 * for instance. Make a string to hold hostname for printing.	*/

#include <netdb.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h> /* atoi() etc */
#include <string.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

#define SOURCE_PORT 7777
#define DEFAULT_MIN_PORT 1
#define DEFAULT_MAX_PORT 65535
#define DEST_PORT 6666
#define DEST_IP "127.0.0.1"
#define BUFFSIZE 64 /* 2013 was 32; print some more */

/* 2013 cleanup - had constant "30" chosen arbitratily. Meant to 
 * fix it originally, but since it worked with my test cases on my
 * old .ether domain, I guess I forgot. Then I got a real job... 	*/
/*#define BUFF_HOSTNAME  HOST_NAME_MAX+1*/  /* is FQDN max len defined somewhere? */
#define BUFF_HOSTNAME  255+1  /* is FQDN max len defined somewhere? */
#define BUFF_ADDR_STR  30 /* TODO: fix this; max for ipv6? POSIX definition */

int do_scan(char *ahost, u_short startp, u_short stopp);

int
main(int argc, char *argv[])
{
  /* char a;	2013 cleanup - not sure what this was.... */
  char *ahost;
  u_short startp;
  u_short stopp;

  if ((argc != 2) && (argc != 4))
  {
    printf("usage: %s <host> [<start port> <end port>]\n",argv[0]);
    return 1;
  }

  if (argc == 4)
  {
    startp = (u_short)atoi(argv[2]);
    stopp = (u_short)atoi(argv[3]);
  }
  else
  {
    startp = (u_short)DEFAULT_MIN_PORT;
    stopp = (u_short)DEFAULT_MAX_PORT;
  }

  ahost = argv[1];
  if ( do_scan(ahost, startp, stopp) < 0)
    return 1;
  else 
    return 0;
}

int
do_scan(char *ahost, u_short startp, u_short stopp)
{
  int sockfd;
  struct sockaddr_in dest_addr;
  u_short curr_port;
  struct servent *serv_struct;
  struct hostent *htoscan;
  char buf[BUFFSIZE];
  u_short minport = startp;
  u_short maxport = stopp;
  struct pollfd stopoll;
  char hn_string[BUFF_HOSTNAME];
  char ip_string[BUFF_ADDR_STR];
  int timedout = 0; /* errors which we'll warn about the first time */
  int accessperm = 0;

  /* 2013 cleanup - memory initialization. I was long on attitude and short
   * on clue in 2000. I was just learning the ropes of coding; this scanner
   * was the most sophisticated thing I had done. My buddies thought it was 
   * el1te, so it served its purpose. bzero() was deprecated  in favor of 
   memset().				*/
  memset(hn_string, '\0', BUFF_HOSTNAME);
  memset(ip_string, '\0', BUFF_ADDR_STR);
  		

  dest_addr.sin_family = AF_INET;

  if ((dest_addr.sin_addr.s_addr = inet_addr(ahost)) == INADDR_NONE)
  {
    if ( (htoscan = gethostbyname(ahost)) == NULL)
    {
      herror("gethostbyname() failed");
      return -1;
    }
    else
    {
      dest_addr.sin_addr = *(struct in_addr *)*htoscan->h_addr_list;		
      /* 2013 - changed from sizeof(hn_string) */ 
      strncpy(hn_string, ahost, BUFF_HOSTNAME); 
      strncpy(ip_string, (char *)inet_ntoa(dest_addr.sin_addr), BUFF_ADDR_STR);
    }
  }
  else  	/* string was dot-quad. */
  {
    if ( (htoscan = gethostbyaddr(ahost, 4, AF_INET) ) == NULL)
    {
      strncpy(hn_string, "hostname unknown", BUFF_HOSTNAME);
      strncpy(ip_string, ahost, BUFF_ADDR_STR);
    }
    else
    {
      strncpy(hn_string, htoscan->h_name, BUFF_HOSTNAME);
      strncpy(ip_string, ahost, BUFF_ADDR_STR);
    }
  } /* 2013 just in case, ensure termination. */
  hn_string[BUFF_HOSTNAME-1] = '\0';
  ip_string[BUFF_ADDR_STR-1] = '\0';
  printf("%s\t(%s)\n", hn_string, ip_string);
  /* at this point we should have an address for sure.  */
  memset(&(dest_addr.sin_zero), 0, 8);
  /* Aug 2013 oops, this was looping since maxport+1 wraps to 0 I think. 
   * Check for 0. */
  for (curr_port = minport; curr_port <= maxport && curr_port != 0; curr_port++)
  {
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
      perror("socket() call failed");
      return -1;
    }
    /* initialise our struct for polling */
    stopoll.fd = sockfd;
    stopoll.events = POLLIN;
  
    dest_addr.sin_port = htons(curr_port);

    if (!((connect(sockfd, 
          (struct sockaddr *)&dest_addr,
          sizeof(struct sockaddr))) 
        < 0))
    {
      /* print tha pr0t number. */
      serv_struct = getservbyport(htons(curr_port), NULL);
      printf("%d\t", curr_port);

      if (serv_struct != NULL)
        printf("%s \t", serv_struct->s_name);

      /* poll our single descriptor for 1 second. Could maybe be shorter	*/
      if ( (poll(&stopoll, 1, 1000) > 0) && (stopoll.events == stopoll.revents) )
      {
        memset(buf, 0, BUFFSIZE);
        recv(sockfd, buf, (BUFFSIZE-1), 0);
        printf("%s\n", buf); /* 2013 TODO: Sanitize binary data */
      }
      else
      {
        printf("\n");
      }
    }
    else /* handle connect() failure */
    {
      switch (errno)
      {
        case ECONNREFUSED:
          break;

        case ETIMEDOUT:
        case ENETUNREACH:
          if (!timedout)
          {
            timedout = 1;
            perror("socket error");
            fprintf(stderr, "Host may be down or firewalled\n");
            fprintf(stderr, 
              "I'll try to continue, but this may take a while\n");
          }
          break;

        case EACCES:
        case EPERM:
          if (!accessperm)
          {
            accessperm = 1;
            perror("socket error");
            fprintf(stderr,
              "We might be blocked by a local firewall rule\n");
            fprintf(stderr, 
              "I'll try to continue, but this is likely to end in tears.");
          }
          break;

        /* TODO: gracefully handle other connect() errors which 
           might indicate the port is being filtered */
        default: /* we simply won't tolerate any more errors */
          perror("socket error");
          return -1;  /* indicate failure */
      }
    }
    close(sockfd);
  }
  return 0;
}
