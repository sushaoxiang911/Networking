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

#include "netdisp.h"

#define DISPD_UNINIT_SD  -1
#define DISPD_QLEN       10 
#define DISPD_WIDTH    1280
#define DISPD_HEIGHT    800

int sd;                   /* listen socket descriptor: to accept connection */
int td;                   /* connected socket descriptor: to receive image */
int maxsd;                /* max socket descriptor */
int wd;                   /* GLUT window handle */
GLdouble width, height;   /* window width and height */

imsg_t imsg;
char *image;
long img_size;    
long img_offset;

/*
 * dispd_sockinit: sets up a TCP socket listening for connection.
 * Let the call to bind() assign an ephemeral port to this listening socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this display.
 *
 * Terminates process on error.
 * Update the global variables "sd" and "maxsd".
 * Returns the bound socket id.
*/
int
dispd_sockinit()
{
  int sd;
  struct sockaddr_in self;
  char dname[NETDISP_MAXNAMELEN] = { 0 };

#ifdef _WIN32
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "dispd_sockinit: WSAStartup");
#endif

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
  */
  /* create a TCP socket, store the socket descriptor in global variable "sd" */


  // update global variable "maxsd"
  maxsd = (maxsd > sd ? maxsd : sd); 
  
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  /* bind address to socket */

  /* listen on socket */

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */

  /* inform user which port this peer is listening on */
  fprintf(stderr, "display address is %s:%d\n", dname, ntohs(self.sin_port));

  return sd;
}

/*
 * dispd_accept: accepts connection on the provided socket sd.
 *
 * Return the descriptor of the connected socket.
 * Terminates process on error.
*/
int
dispd_accept(int sd)
{
  int td;
  struct sockaddr_in client;
  struct hostent *cp;

  /* Task 2: YOUR CODE HERE
   * Fill out the rest of this function.
   * Accept the new connection.
   * Use the variable "td" to hold the new connected socket.
  */

  /* inform user of connection */
  cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from client %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
          ntohs(client.sin_port));

  return td;
}

/*
 * Task 2: YOUR CODE HERE 
 * dispd_recvimsg: receive an imsg_t packet on provided socket sd
 * and store it in the global variable imsg.
 * Check that received message is of the right version number.
 * If message is of a wrong version number and for any other
 * error in receiving packet, terminate process.
 * Convert the integer fields of imsg back to host byte order.
 */
void
dispd_recvimsg(int sd)
{

  return;
}

void
dispd_imginit()
{
  int tod;
  double img_dsize;

  img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
  net_assert((img_dsize > (double) LONG_MAX), "dispd: image too big");
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

/*
 * dispd_recvimage: 
 * On each call, receive as much of the image is available on the network and
 * store it in global variable "image" at offset "img_offset" from the
 * start of the buffer.  The global variable "img_offset" must be updated
 * to reflect the amount of data received so far.  Another global variable "img_size"
 * stores the expected size of the image transmitted from the server.
 * The variable "img_size" must NOT be modified.
 *
 * Return bytes received.
 * Terminate process on receive error.
 */
int
dispd_recvimage(int sd)
{
  int bytes = 0;
   
  // img_offset is a global variable that keeps track of how many bytes
  // have been received and stored in the buffer.  Initialy it is 0.
  //
  // img_size is another global variable that stores the size of the image.
  // If all goes well, we should receive img_size bytes of data from the server.
  if (img_offset <  img_size) { 
    /* Task 2: YOUR CODE HERE
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
     *
     * Update local variable "bytes" with bytes received returned by recv().
     */
    
    /* give the updated image to OpenGL for texturing */
    glTexImage2D(GL_TEXTURE_2D, 0, (GLint) imsg.im_format,
                 (GLsizei) imsg.im_width, (GLsizei) imsg.im_height, 0,
                 (GLenum) imsg.im_format, GL_UNSIGNED_BYTE, image);
    /* redisplay */
    glutPostRedisplay();
  }

  return bytes;
}

/* Callback functions for GLUT */

/*
 * dispd_select(): called by GLUT when idle
*/
void
dispd_select()
{
  char c;
  fd_set rset;

  FD_ZERO(&rset);  // zero the read set

#ifndef _WIN32
  FD_SET(STDIN_FILENO, &rset); // wait for input from std input,
     // Winsock only works with socket and stdin is not a socket
#endif
     // or read-ready event on the connected socket,
  if (td != DISPD_UNINIT_SD) { FD_SET(td, &rset); }
     // or wait for exception event on the listening socket,
  if (sd != DISPD_UNINIT_SD) { FD_SET(sd, &rset); }
  
  /* Task 2: YOUR CODE HERE
     Call select() to wait for any activity on any of the above
     descriptors. Assume global variable "maxsd" has been correctly
     set to the largest descriptor number. */

#ifndef _WIN32
  if (FD_ISSET(STDIN_FILENO, &rset)) {
    // user input: if getchar() returns EOF or if user hits q, quit,
    // else flush input and go back to waiting
    if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
      fprintf(stderr, "Bye!\n");
      exit(0);
    }
    fflush(stdin);
  }
#endif
  
  if ((sd != DISPD_UNINIT_SD) && FD_ISSET(sd, &rset)) {
    td = dispd_accept(sd);   // both sd and td are global variables
    close(sd);               // accept only one connection
    sd = DISPD_UNINIT_SD;

    maxsd = (td > maxsd ? td : maxsd);
    dispd_recvimsg(td);  // Task 2
    dispd_imginit();
  }

  if ((td != DISPD_UNINIT_SD) && FD_ISSET(td, &rset)) {
    if (!dispd_recvimage(td)) {
      close(td);
      td = DISPD_UNINIT_SD;
    }
  }
    
  return;
}
  
void 
dispd_display(void)
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
dispd_reshape(int w, int h)
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
dispd_kbd(unsigned char key, int x, int y)
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
dispd_glutinit(int *argc, char *argv[])
{

  width  = DISPD_WIDTH;    /* initial window width and height, */
  height = DISPD_HEIGHT;         /* within which we draw. */

  glutInit(argc, argv);
  glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
  glutInitWindowSize((int) DISPD_WIDTH, (int) DISPD_HEIGHT);
  wd = glutCreateWindow("Netimg Display" /* title */ );   // wd global
  glutDisplayFunc(dispd_display);
  glutReshapeFunc(dispd_reshape);
  glutKeyboardFunc(dispd_kbd);
  glutIdleFunc(dispd_select); // Task 2: fill out dispd_select()

  return;
} 

int
main(int argc, char *argv[])
{
#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif
  
  maxsd = 0;
  td = DISPD_UNINIT_SD;
  sd = dispd_sockinit();  // Task 2: fill out dispd_sockinit()
  dispd_glutinit(&argc, argv);

  /* start the GLUT main loop */
  glutMainLoop();

  return 0;
}
