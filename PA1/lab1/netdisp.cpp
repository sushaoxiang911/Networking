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
#include "netdisp.h"

#define DISP_PORTSEP   ':'
#define DISP_LINGER        2
#define DISP_NUMSEG       50
#define DISP_MSS        1440
#define DISP_USLEEP   500000    // 500 ms

char DISP_IMVERS;

void
netdisp_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -f <image>.tga  -d displayFQDN.port [-v version]\n", progname); 
  exit(1);
}

/*
 * netdisp_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, the
 * provided TGA image file name is copied to memory pointed to by
 * "fname", which must be allocated by caller.  The provided display
 * FQDN is copied to memory pointed to by "dname". Then "port" points
 * to the port to connect at the display, in network byte order.  Both
 * "pname", and "port" must be allocated by caller.  The buffers
 * "fname" and "dname" points to must each of size NETDISP_MAXNAMELEN.
 *
 * Nothing else is modified.
 */
int
netdisp_args(int argc, char *argv[], char *fname, char *dname, u_short *port)
{
  char c, *p;
  extern char *optarg;

  if (argc < 5) {
    return (1);
  }
  
  DISP_IMVERS = IM_VERS;
  
  while ((c = getopt(argc, argv, "f:d:v:")) != EOF) {
    switch (c) {
    case 'f':
      net_assert((strlen(optarg) >= NETDISP_MAXNAMELEN), "netdisp_args: image file name too long");
      strcpy(fname, optarg);
      break;
    case 'd':
      for (p = optarg+strlen(optarg)-1;      // point to last character of addr:port arg
           p != optarg && *p != DISP_PORTSEP;  // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "netdisp_args: display address malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETDISP_MAXNAMELEN), "netdisp_args: FQDN too long");
      strcpy(dname, optarg);
      break;
    case 'v':
      DISP_IMVERS = (char) atoi(optarg);
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * netdisp_imginit: load TGA image from file to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All three variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 */
void
netdisp_imginit(char *fname, LTGA *image, imsg_t *imsg, long *img_size)
{
  int alpha, greyscale;
  double img_dsize;
  
  image->LoadFromFile(fname);
  net_assert((!image->IsLoaded()), "netdisp_imginit: image not loaded");

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
  net_assert((img_dsize > (double) LONG_MAX), "display: image too big");
  *img_size = (long) img_dsize;

  imsg->im_vers = DISP_IMVERS;
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
 * Task 1: YOUR CODE HERE:
 * Fill out this function netdisp_sockinit:
 * creates a new socket to connect to the provided display's FQDN and
 * port number.  The port number provided is assumed to already be in
 * netowrk byte order.  On connection, set the linger option for
 * DISP_LINGER to allow data to be delivered to display.
 *
 * On success, return the connected socket descriptor.
 * On error, terminates process.
 */
int
netdisp_sockinit(char *dname, u_short port)
{
#ifdef _WIN32
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "display: WSAStartup");
#endif

  /* 
   * create a new TCP socket, store the socket in the global variable sd
  */

  /* obtain the display's IPv4 address from sname and initialize the
     socket address with display's address and port number . */

  /* connect to display */

  /* make the socket wait for DISP_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  
  return(sd);
}

/*
 * netdisp_imgsend: send the image to the client
 * First send the specifics of the image (width, height, etc.)
 * contained in *imsg to the client.  *imsg must have been
 * initialized by caller.
 * Then send the image contained in *image, but for future
 * debugging purposes we're going to send the image in
 * chunks of segsize instead of as one single image.
 * We're going to send the image slowly, one chunk for every
 * DISP_USLEEP microseconds.
 *
 * Terminate process upon encountering any error.
 * Doesn't otherwise modify anything.
*/
void
netdisp_imgsend(int td, imsg_t *imsg, LTGA *image, long img_size)
{
  int segsize;
  char *ip;
  int bytes;
  long left;

  /* Task 1: YOUR CODE HERE
   * Send the imsg packet to client connected to socket td.
   */

  segsize = img_size/DISP_NUMSEG;                     /* compute segment size */
  segsize = segsize < DISP_MSS ? DISP_MSS : segsize; /* but don't let segment be too small*/

  ip = (char *) image->GetPixels();    /* ip points to the start of byte buffer holding image */

  for (left = img_size; left; left -= bytes) {  // "bytes" contains how many bytes was sent
                                                // at the last iteration.

    /* Task 1: YOUR CODE HERE
     * Send one segment of data of size segsize at each iteration.
     * The last segment may be smaller than segsize
    */

    fprintf(stderr, "netdisp_send: size %d, sent %d\n", (int) left, bytes);
    usleep(DISP_USLEEP);
  }

  return;
}

int
main(int argc, char *argv[])
{ 
  int sd;
  LTGA image;
  imsg_t imsg;
  long img_size;
  char fname[NETDISP_MAXNAMELEN] = { 0 };
  char dname[NETDISP_MAXNAMELEN] = { 0 };
  u_short port;

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for netdisp_args()
  if (netdisp_args(argc, argv, fname, dname, &port)) {
    netdisp_usage(argv[0]);
  }

  netdisp_imginit(fname, &image, &imsg, &img_size);
  sd = netdisp_sockinit(dname, port);  // Task 1
  netdisp_imgsend(sd, &imsg, &image, img_size); // Task 1
  close(sd);

#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
