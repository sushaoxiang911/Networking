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
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>      // socklen_t
#include "wingetopt.h"
#else
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include <iostream>

#include "netimg.h"

#define NETIC_PORTSEP   ':'
#define NETIC_MAXFQDN   255
#define NETIC_WIDTH    1280
#define NETIC_HEIGHT    800

using namespace std;

int sd;                   /* socket descriptor */
int wd;                   /* GLUT window handle */
GLdouble width, height;   /* window width and height */

imsg_t imsg;
char *image;
long img_size;    
long img_offset;

void
netic_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -s serverFQDN.port\n", progname); 
  exit(1);
}

/*
 * netic_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * the provided server FQDN is copied to memory pointed to by
 * "sname". Then "port" points to the port to connect at server, in
 * network byte order.  Both "pname", and "port" must be
 * allocated by caller.  The buffer sname points to must be of size
 * NETIC_MAXFQDN+1.
 * Nothing else is modified.
 */
int
netic_args(int argc, char *argv[], char *sname, u_short *port)
{
  char c, *p;
  extern char *optarg;

  if (argc < 3) {
    return (1);
  }
  
  while ((c = getopt(argc, argv, "s:")) != EOF) {
    switch (c) {
    case 's':
      for (p = optarg+strlen(optarg)-1;      // point to last character of addr:port arg
           p != optarg && *p != NETIC_PORTSEP;  // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "netic_args: server addressed malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETIC_MAXFQDN), "netic_args: FQDN too long");
      strcpy(sname, optarg);
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * Task 1: YOUR CODE HERE: Fill out this function
 * netic_sockinit: creates a new socket to connect to the provided server.
 * The server's FQDN and port number are provided.
 *
 * On success, the global socket descriptor sd is initialized.
 * On error, terminates process.
 */
void
netic_sockinit(char *sname, u_short port)
{
#ifdef _WIN32
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "netic: WSAStartup");
#endif

  /* 
   * create a new TCP socket, store the socket in the global variable sd
  */

  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */

  /* connect to server */
  
  struct sockaddr_in server;
  struct hostent *sp;  

  if ((sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
    abort();
  }
  
  memset((char * ) &server, 0, sizeof(struct sockaddr_in));

  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);
 
  if (connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) { 
    abort();
  }
  return;
}

/*
 * Task 1: YOUR CODE HERE 
 * netic_recvimsg: receive an imsg_t packet from server and store it 
 * in the global variable imsg.
 * Check that received message is of the right version number.
 * If message is of a wrong version number and for any other
 * error in receiving packet, terminate process.
 * Convert the integer fields of imsg back to host byte order.
 */
void
netic_recvimsg()
{
  u_short size = 0;
  while (size < sizeof(imsg)) {
    int result = recv(sd, (char *)&imsg + size, sizeof(imsg) - size, 0);
    if (result == -1)
        abort();
    size += result;
  }
  imsg.im_width = ntohs(imsg.im_width);
  imsg.im_height = ntohs(imsg.im_height);
  imsg.im_format = ntohs(imsg.im_format);
  if (imsg.im_vers != IM_VERS) {
    exit(1);
  }
  return;
}

void
netic_imginit()
{
  int tod;
  double img_dsize;

  img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
  net_assert((img_dsize > (double) LONG_MAX), "netic: image too big");
  img_size = (long) img_dsize;                 // global
  image = (char *)malloc(img_size*sizeof(char));

  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();

  glGenTextures(1, (GLuint*) &tod);
  glBindTexture(GL_TEXTURE_2D, tod);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); 
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR); 
  glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE); 
  glEnable(GL_TEXTURE_2D);
}

/* Callback functions for GLUT */

/*
 * netic_recvimage: called by GLUT when idle
 * On each call, receive as much of the image is available on the network and
 * store it in OpenGL's GL_PIXEL_UNPACK_BUFFER at offset img_offset from the
 * start of the buffer.  img_offset is a global variable that must be updated
 * to reflect the amount of data received so far.  Another global variable img_size
 * stores the expected size of the image transmitted from the server.
 * img_size is not to be modified.
 * Terminate process on receive error.
 */
void
netic_recvimage(void)
{
   
  // img_offset is a global variable that keeps track of how many bytes
  // have been received and stored in the buffer.  Initialy it is 0.
  //
  // img_size is another global variable that stores the size of the image.
  // If all goes well, we should receive img_size bytes of data from the server.
  if (img_offset <  img_size) { 
    /* Task 1: YOUR CODE HERE
     * Receive as much of the remaining image as available from the network
     * put the data in the buffer pointed to by the global variable 
     * "image" starting at "img_offset".
     *
     * For example, the first time this function is called, img_offset is 0
     * so the received data is stored at the start (offset 0) of the "image" 
     * buffer.  The global variable "image" should not be modified.
     *
     * Update img_offset by the amount of data received, in preparation for the
     * next iteration, the next time this function is called.
     */
     int result = recv(sd, image + img_offset, img_size - img_offset, 0); 
     if (result == -1)
        abort();
     img_offset += result;
    /* give the updated image to OpenGL for texturing */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint) imsg.im_format,
                 (GLsizei) imsg.im_width, (GLsizei) imsg.im_height, 0,
                 (GLenum) imsg.im_format, GL_UNSIGNED_BYTE, image);
    /* redisplay */
    glutPostRedisplay();
  }

  return;
}

void 
netic_display(void)
{
  glPolygonMode(GL_FRONT, GL_POINT);
  
  /* If your image is displayed upside down, you'd need to play with the
     texture coordinate to flip the image around. */
  glBegin(GL_QUADS); 
  glTexCoord2f(0.0,1.0); glVertex3f(0.0, 0.0, 0.0);
  glTexCoord2f(0.0,0.0); glVertex3f(0.0, height, 0.0);
  glTexCoord2f(1.0,0.0); glVertex3f(width, height, 0.0);
  glTexCoord2f(1.0,1.0); glVertex3f(width, 0.0, 0.0);
  glEnd();

  glFlush();
}

void
 (int w, int h)
{
  /* save new screen dimensions */
  width = (GLdouble) w;
  height = (GLdouble) h;

  /* tell OpenGL to use the whole window for drawing */
  glViewport(0, 0, (GLsizei) w, (GLsizei) h);

  /* do an orthographic parallel projection with the coordinate
     system set to first quadrant, limited by screen/window size */
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  gluOrtho2D(0.0, width, 0.0, height);
}

void
netic_kbd(unsigned char key, int x, int y)
{
  switch((char)key) {
  case 'q':
  case 27:
    glutDestroyWindow(wd);
#ifdef _WIN32
    WSACleanup();
#endif
    exit(0);
    break;
  default:
    break;
  }

  return;
}

void
netic_glutinit(int *argc, char *argv[])
{

  width  = NETIC_WIDTH;    /* initial window width and height, */
  height = NETIC_HEIGHT;         /* within which we draw. */

  glutInit(argc, argv);
  glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
  glutInitWindowSize((int) NETIC_WIDTH, (int) NETIC_HEIGHT);
  wd = glutCreateWindow("Netimg Display" /* title */ );   // wd global
  glutDisplayFunc(netic_display);
  glutReshapeFunc(netic_reshape);
  glutKeyboardFunc(netic_kbd);
  glutIdleFunc(netic_recvimage); // Task 1

  return;
} 

int
main(int argc, char *argv[])
{
  char sname[NETIC_MAXFQDN+1] = { 0 };
  u_short port;

  // parse args, see the comments for netic_args()
  if (netic_args(argc, argv, sname, &port)) {
    netic_usage(argv[0]);
  }

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif
  
  netic_sockinit(sname, port);  // Task 1
  netic_glutinit(&argc, argv);

  netic_recvimsg();  // Task 1
  netic_imginit();
  
  /* start the GLUT main loop */
  glutMainLoop();

  return 0;
}
