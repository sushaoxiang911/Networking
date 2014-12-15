/*
 * Copyright (c) 2014 University of Michigan, Ann Arbor.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms are permitted
 * provided that the above copyright notice and this paragraph are
 * duplicated in all such forms and that any documentation,
 * advertising materials, and other materials related to such
 * distribution and use acknowledge that the software was developed
 * by the University of Michigan, Ann Arbor. The name of the University 
 * may not be used to endorse or promote products derived from this 
 * software without specific prior written permission.
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 *
 * Author: Sugih Jamin (jamin@eecs.umich.edu)
 *
*/
#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
#include <iostream>
using namespace std;
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#include "wingetopt.h"
#else
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/ioctl.h>     // ioctl(), FIONBIO
#endif

#include "ltga.h"
#include "netimg.h"
#include "imgdb.h"

void
dhtn_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -i <imagefolder> [-b <beginID> -e <endID>]\n", progname); 
  exit(1);
}

/*
 * dhtn_args: parses command line args.
 *
 * Presently simply calls imgdb->cli() and return what it returns.
 * Nothing else is modified.
 */
int
dhtn_args(int argc, char *argv[], imgdb *imgdb)
{
  /* BEGIN SOLUTION */
  //char c;
  //extern char *optarg;
  /* END SOLUTION */

  return(imgdb->cli(argc, argv));
}

/*
 * dhtn_sockinit: sets up a TCP socket listening for connection.
 * Let the call to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminates process on error.
 * Update the global variable "sd".
 * Returns the bound socket id.
*/
int
dhtn_sockinit()
{
  int sd;
  int err, len;
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME] = { 0 };

#ifdef _WIN32
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "dhtn_sockinit: WSAStartup");
#endif

  /* create a TCP socket, store the socket descriptor in global variable "sd" */
  sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  net_assert((sd < 0), "dhtn_sockinit: socket");
  
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  /* bind address to socket */
  err = bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  net_assert(err, "dhtn_sockinit: bind");

  /* listen on socket */
  err = listen(sd, NETIMG_QLEN);
  net_assert(err, "dhtn_sockinit: listen");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(sd, (struct sockaddr *) &self, (socklen_t *) &len);
  net_assert(err, "dhtn_sockinit: getsockname");

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  err = gethostname(sname, NETIMG_MAXFNAME);
  net_assert(err, "dhtn_sockinit: gethostname");

  /* inform user which port this peer is listening on */
  fprintf(stderr, "DHT node address is %s:%d\n", sname, ntohs(self.sin_port));

  return sd;
}

/*
 * dhtn_accept: accepts connection on the given socket, sd.
 *
 * On connection, set the linger option for NETIMG_LINGER to
 * allow data to be delivered to client.  Return the descriptor
 * of the connected socket.
 * Terminates process on error.
*/
int
dhtn_accept(int sd)
{
  int td;
  int err, len;
  struct linger linger_opt;
  struct sockaddr_in client;
  struct hostent *cp;

  /* Accept the new connection.
   * Use the variable "td" to hold the new connected socket.
  */
  len = sizeof(struct sockaddr_in);
  td = accept(sd, (struct sockaddr *) &client, (socklen_t *)&len);
  net_assert((td < 0), "dhtn_accept: accept");

  /* make the socket wait for NETIMG_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  linger_opt.l_onoff = 1;
  linger_opt.l_linger = NETIMG_LINGER;
  err = setsockopt(td, SOL_SOCKET, SO_LINGER,
                   (char *) &linger_opt, sizeof(struct linger));
  net_assert(err, "dhtn_accpet: setsockopt SO_LINGER");
  
  /* inform user of connection */
  cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from client %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
          ntohs(client.sin_port));

  return td;
}

/*
 * dhtn_recvqry: receive an iqry_t packet from client and store it 
 * in the provided variable "iqry".  The type iqry_t is defined in netimg.h.
 * Check that received message is of the right version number.
 * If message is of a wrong version number and for any other
 * error in receiving packet, terminate process.
 */
void
dhtn_recvqry(int td, iqry_t *iqry)
{
  int bytes;
  
  bytes = recv(td, (char *) iqry, 2*sizeof(unsigned char), 0);
  net_assert((bytes != 2*sizeof(unsigned char)), "dhtn_recvqry: recv iqry header");
  net_assert((iqry->iq_vers != NETIMG_VERS), "dhtn_recvqry: wrong version");
  net_assert(((int) iqry->iq_namelen > NETIMG_MAXFNAME), "dhtn_recvqry: iq_namelen too long");

  bytes = recv(td, (char *) &iqry->iq_name, ((int)iqry->iq_namelen)*sizeof(char), 0);
  net_assert((bytes != ((int)(iqry->iq_namelen*sizeof(char)))), "dhtn_recvqry: recv image name");

  return;
}

/*
 * dhtn_sendimg: send the image to the client
 * First send the specifics of the image (width, height, etc.)
 * in an imsg_t packet to the client.  The type imsg_t is defined in netimg.h.
 * If "found" is > 0, send the image contained in imgdb.  Otherwise, set the
 * img_depth field of the imsg_t packet to 0 and send only the imsg_t packet.
 * For debugging purposes if an image is sent, it is sent in chunks of segsize
 * instead of as one single image. We're going to send the image slowly, one
 * chunk for every NETIMG_USLEEP microseconds.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void
dhtn_sendimg(int td, int found, imgdb *imgdb)
{
  int segsize;
  char *ip;
  int bytes;
  long left;
  imsg_t imsg;
  double imgdsize;
  long imgsize=0L;

  imsg.im_vers = NETIMG_VERS;

  if (found <= 0) {
    if (!found) {
      cerr << "Bloom filter missed." << endl;
    } else {
      cerr << "Bloom filter false positive." << endl;
    }
    imsg.im_depth = (unsigned char) 0;
  } else {
    imgdsize = imgdb->marshall_imsg(&imsg);
    net_assert((imgdsize > (double) LONG_MAX), "dhtn_sendimg: image too large");
    imgsize = (long) imgdsize;

    imsg.im_width = htons(imsg.im_width);
    imsg.im_height = htons(imsg.im_height);
    imsg.im_format = htons(imsg.im_format);
  }
  
  /* 
   * Send the imsg packet to client connected to socket td.
  */
  bytes = send(td, (char *) &imsg, sizeof(imsg_t), 0);
  net_assert((bytes != sizeof(imsg_t)), "dhtn_sendimg: send imsg");

  if (found > 0) {
    segsize = imgsize/NETIMG_NUMSEG;                     /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize; /* but don't let segment be too small*/
    
    ip = imgdb->getimage();    /* ip points to the start of byte buffer holding image */
    
    for (left = imgsize; left; left -= bytes) {  // "bytes" contains how many bytes was sent
                                                 // at the last iteration.
      /* Send one segment of data of size segsize at each iteration.
       * The last segment may be smaller than segsize
       */
      bytes = send(td, (char *) ip, segsize > left ? left : segsize, 0);
      net_assert((bytes < 0), "dhtn_sendimg: send image");
      ip += bytes;
      
      fprintf(stderr, "dhtn_sendimg: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  int sd, td;
  imgdb imgdb;
  iqry_t iqry;
  int found;

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for dhtn_args()
  if (dhtn_args(argc, argv, &imgdb)) {
    dhtn_usage(argv[0]);
  }
  imgdb.loaddb();

  sd = dhtn_sockinit();
  
  while (1) {
    td = dhtn_accept(sd);
    dhtn_recvqry(td, &iqry);
    found = imgdb.searchdb(iqry.iq_name);
    dhtn_sendimg(td, found, &imgdb);
    close(td);
  }

#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
