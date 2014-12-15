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
 * Authors: Allen Hillaker (hillaker@umich.edu), Sugih Jamin (jamin@eecs.umich.edu)
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
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ltga.h"
#include "netimg.h"

#define NETIS_MAXFNAME  255
#define NETIS_QLEN       10 
#define NETIS_LINGER      2
#define NETIS_NUMSEG     50
#define NETIS_MSS      1440
#define NETIS_USLEEP 500000    // 500 ms

char NETIS_IMVERS;

void
netis_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -f <image>.tga [-v version]\n", progname); 
  exit(1);
}

/*
 * netis_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * the provided TGA image file name is copied to memory pointed to by
 * "fname", which must be allocated by caller.  The buffer fname
 * points to must be of size NETIS_MAXFNAME.
 * Nothing else is modified.
 */
int
netis_args(int argc, char *argv[], char *fname)
{
  char c;
  extern char *optarg;

  if (argc < 3) {
    return (1);
  }
  
  NETIS_IMVERS = IM_VERS;
  
  while ((c = getopt(argc, argv, "f:v:")) != EOF) {
    switch (c) {
    case 'f':
      net_assert((strlen(optarg) >= NETIS_MAXFNAME), "netis_args: image file name too long");
      strcpy(fname, optarg);
      break;
    case 'v':
      NETIS_IMVERS = (char) atoi(optarg);
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * netis_imginit: load TGA image from file to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All three variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 */
void
netis_imginit(char *fname, LTGA *image, imsg_t *imsg, long *img_size)
{
  int alpha, greyscale;
  double img_dsize;
  
  image->LoadFromFile(fname);
  net_assert((!image->IsLoaded()), "netis_imginit: image not loaded");

  cout << "Image: " << endl;
  cout << "     Type   = " << LImageTypeString[image->GetImageType()] 
       << " (" << image->GetImageType() << ")" << endl;
  cout << "     Width  = " << image->GetImageWidth() << endl;
  cout << "     Height = " << image->GetImageHeight() << endl;
  cout << "Pixel depth = " << image->GetPixelDepth() << endl;
  cout << "Alpha depth = " << image->GetAlphaDepth() << endl;
  cout << "RL encoding  = " << (((int) image->GetImageType()) > 8) << endl;
  /* use image->GetPixels()  to obtain the pixel array */

  img_dsize = (double) (image->GetImageWidth()*image->GetImageHeight()*(image->GetPixelDepth()/8));
  net_assert((img_dsize > (double) LONG_MAX), "netis: image too big");
  *img_size = (long) img_dsize;

  imsg->im_vers = NETIS_IMVERS;
  imsg->im_depth = (unsigned char)(image->GetPixelDepth()/8);
  imsg->im_width = htons(image->GetImageWidth());
  imsg->im_height = htons(image->GetImageHeight());
  alpha = image->GetAlphaDepth();
  greyscale = image->GetImageType();
  greyscale = (greyscale == 3 || greyscale == 11);
  if (greyscale) {
    imsg->im_format = htons(alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE);
  } else {
    imsg->im_format = htons(alpha ? GL_RGBA : GL_RGB);
  }

  return;
}
  
/*
 * netis_sockinit: sets up a TCP socket listening for connection.
 * Let the call to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int
netis_sockinit()
{
  int sd;
  struct sockaddr_in self;
  char sname[NETIS_MAXFNAME+1] = { 0 };

#ifdef _WIN32
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "netis: WSAStartup");
#endif

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
  */
  /* create a TCP socket, store the socket descriptor in "sd" */
  
  if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
    abort();  
  
  int flag = 1;
  if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
    abort();
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = htonl(INADDR_ANY);
  self.sin_port = htons((u_short)0);

  /* bind address to socket */
  if (bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in)) < 0)
    abort();
  /* listen on socket */

  if (listen(sd, NETIS_QLEN) < 0)
    abort();

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  socklen_t len = sizeof(self);
  if (getsockname(sd, (struct sockaddr *) &self, &len) < 0)
     abort();
  
  /* Find out the FQDN of the current host and store it in the local
     variable "sname" */
  gethostname(sname, NETIS_MAXFNAME+1);

  /* inform user which port this peer is listening on */
  fprintf(stderr, "netis address is %s:%d\n", sname, ntohs(self.sin_port));
  return sd;
}

/*
 * netis_accept: accepts connection on the given socket, sd.
 *
 * On connection, set the linger option for NETIS_LINGER to
 * allow data to be delivered to client.  Return the descriptor
 * of the connected socket.
 * Terminates process on error.
*/
int
netis_accept(int sd)
{
  int td;
  struct sockaddr_in client;
  struct hostent *cp;

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
   * Accept the new connection.
   * Use the variable "td" to hold the new connected socket.
  */
  int len = sizeof(struct sockaddr_in);
  td = accept(sd, (struct sockaddr *) &client, (socklen_t *)&len);
  /* make the socket wait for PR_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  struct linger linger_opt;
  linger_opt.l_onoff = 1;
  linger_opt.l_linger = NETIS_LINGER;
  if (setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt)))
    abort();

  /* inform user of connection */
  cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from client %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
          ntohs(client.sin_port));

  return td;
}

/*
 * netis_imgsend: send the image to the client
 * First send the specifics of the image (width, height, etc.)
 * contained in *imsg to the client.  *imsg must have been
 * initialized by caller.
 * Then send the image contained in *image, but for future
 * debugging purposes we're going to send the image in
 * chunks of segsize instead of as one single image.
 * We're going to send the image slowly, one chunk for every
 * NETIS_USLEEP microseconds.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void
netis_imgsend(int td, imsg_t *imsg, LTGA *image, long img_size)
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  /* Task 2: YOUR CODE HERE
   * Send the imsg packet to client connected to socket td.
   */
  int size = 0;
  while(size < sizeof(imsg_t)) {
    int result = send(td, imsg+size, sizeof(imsg_t)-size, 0);
    if (result < 0)
      abort();
    size += result;
  }

  segsize = img_size/NETIS_NUMSEG;                     /* compute segment size */
  segsize = segsize < NETIS_MSS ? NETIS_MSS : segsize; /* but don't let segment be too small*/

  ip = (char *) image->GetPixels();    /* ip points to the start of byte buffer holding image */

  for (left = img_size; left; left -= bytes) {  // "bytes" contains how many bytes was sent
                                                // at the last iteration.

    /* Task 2: YOUR CODE HERE
     * Send one segment of data of size segsize at each iteration.
     * The last segment may be smaller than segsize
    */
    if (left < segsize) {
      bytes = send(td, ip, left, 0);
    } else {
      bytes = send(td, ip, segsize, 0);
    }
    if (bytes < 0){
      abort();
}
    ip += bytes;

    fprintf(stderr, "netis_send: size %d, sent %d\n", (int) left, bytes);
    usleep(NETIS_USLEEP);
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  int sd, td;
  LTGA image;
  imsg_t imsg;
  long img_size;
  char fname[NETIS_MAXFNAME] = { 0 };

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for netis_args()
  if (netis_args(argc, argv, fname)) {
    netis_usage(argv[0]);
  }

  netis_imginit(fname, &image, &imsg, &img_size);
  sd = netis_sockinit();  // Task 2
  
  while (1) {
    td = netis_accept(sd);  // Task 2
    netis_imgsend(td, &imsg, &image, img_size); // Task 2
    close(td);
  }

#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
