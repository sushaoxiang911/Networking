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
#include <stdlib.h>        // atoi(), random()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX, INT_MAX
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
#include <sys/time.h>
#include <sys/select.h>
#include <sys/ioctl.h>     // ioctl(), FIONBIO
#endif
#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include "ltga.h"
#include "netimg.h" 
#include "fec.h"

#include <set>

using std::set;
using std::cout; using std::endl;

void
imgdb_usage(char *progname)
{
  fprintf(stderr, "Usage: %s [-d <drop probability>]\n", progname); 
  exit(1);
}

/*
 * imgdb_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return,
 * the provided drop probability is copied to memory pointed to by
 * "pdrop", which must be allocated by caller.  
 *
 * Nothing else is modified.
 */
int
imgdb_args(int argc, char *argv[], float *pdrop)
{
  char c;
  extern char *optarg;

  if (argc < 1) {
    return (1);
  }
  
  *pdrop = NETIMG_PDROP;

  while ((c = getopt(argc, argv, "d:")) != EOF) {
    switch (c) {
    case 'd':
      *pdrop = atof(optarg);
      if (*pdrop > 0.0 && (*pdrop > 0.11 || *pdrop < 0.051)) {
        fprintf(stderr, "%s: recommended drop probability between 0.011 and 0.51.\n", argv[0]);
      }
      break;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * imgdb_imginit: load TGA image from file to *image.
 * Store size of image, in bytes, in *img_size.
 * Initialize *imsg with image's specifics.
 * All three variables must point to valid memory allocated by caller.
 * Terminate process on encountering any error.
 */
void
imgdb_imginit(char *fname, LTGA *image, imsg_t *imsg, int *img_size)
{
  int alpha, greyscale;
  double img_dsize;
  
  imsg->im_vers = NETIMG_VERS;
  imsg->im_type = NETIMG_DIM;

  image->LoadFromFile(fname);

  if (!image->IsLoaded()) {
    imsg->im_found = 0;
  } else {
    imsg->im_found = 1;

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
    net_assert((img_dsize > (double) NETIMG_MAXSEQ), "imgdb: image too big");
    *img_size = (int) img_dsize;

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
  }

  return;
}
  
/*
 * imgdb_sockinit: sets up a UDP server socket.
 * Let the call to bind() assign an ephemeral port to the socket.
 * Determine and print out the assigned port number to screen so that user
 * would know which port to use to connect to this server.
 *
 * Terminates process on error.
 * Returns the bound socket id.
*/
int
imgdb_sockinit()
{
  int sd=-1;
  int err, len;
  struct sockaddr_in self;
  char sname[NETIMG_MAXFNAME+1] = { 0 };

#ifdef _WIN32
  WSADATA wsa;

  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "imgdb: WSAStartup");
#endif

  /* create a UDP socket */
  /* Lab 5: YOUR CODE HERE */
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  net_assert((sd < 0), "imgdb: create socket");
  
  memset((char *) &self, 0, sizeof(struct sockaddr_in));
  self.sin_family = AF_INET;
  self.sin_addr.s_addr = INADDR_ANY;
  self.sin_port = 0;

  /* bind address to socket */
  err = bind(sd, (struct sockaddr *) &self, sizeof(struct sockaddr_in));
  net_assert(err, "imgdb_sockinit: bind");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "self".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(sd, (struct sockaddr *) &self, (socklen_t *) &len);
  net_assert(err, "imgdb_sockinit: getsockname");

  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  err = gethostname(sname, NETIMG_MAXFNAME);
  net_assert(err, "imgdb_sockinit: gethostname");

  /* inform user which port this peer is listening on */
  fprintf(stderr, "imgdb address is %s:%d\n", sname, ntohs(self.sin_port));

  return sd;
}

/* 
 * imgdb_recvquery: receive an iqry_t packet and store
 * the client's address and port number in the provided
 * "client" argument.  Check that the incoming iqry_t packet
 * is of version NETIMG_VERS and of type NETIMG_SYN.  If so,
 * save the mss and rwnd and image name information into the
 * provided "mss", "rwnd", "fwnd", and "fname" arguments respectively.
 * The provided arguments must all point to pre-allocated space.
 *
 * If error encountered when receiving packet or if packet is
 * of the wrong version or type, throw it away and wait for another
 * packet.  Loop until an iqry_t packet is received.
 *
 * Nothing else is modified.
*/
void
imgdb_recvquery(int sd, struct sockaddr_in *client, unsigned short *mss,
                unsigned char *rwnd, unsigned char *fwnd, char *fname)
{
  /* Receive an iqry_t packet as you did in Lab 5 and Lab6,
   * however, if the arriving packet is not an iqry_t packet, throw it
   * away and wait for another packet.  Loop until an iqry_t packet is
   * received.  When an iqry_t packet is received, save the mss, rwnd,
   * and fwnd and return to caller.
  */
  
  do {
    /* Task 2.1: if the arriving message is a valid query message,
       return to caller, otherwise, loop again until a valid query
       message is received. */
    /* Lab 6: YOUR CODE HERE */
    iqry_t iqry_msg;
    socklen_t slen = sizeof(sockaddr_in);
    int err = recvfrom(sd, &iqry_msg, sizeof(iqry_t), 0, (sockaddr*)client, &slen);
//    net_assert((err < (int)sizeof(iqry_t)), "imgdb_recvquery: bad size");
//    net_assert((iqry_msg.iq_vers != NETIMG_VERS), "imgdb_recvquery: bad ver");
    if (iqry_msg.iq_type == NETIMG_SYN && err == (int)sizeof(iqry_t) && 
            iqry_msg.iq_vers == NETIMG_VERS) {
      *mss = ntohs(iqry_msg.iq_mss);
      *rwnd = iqry_msg.iq_rwnd;
      strcpy(fname, iqry_msg.iq_name);
      *fwnd = iqry_msg.iq_fwnd;
      break;
    }
  } while (1);

  return;
}
  
/* 
 * imgdb_sendpkt: sends the provided "pkt" of size "size"
 * to the client "client" and wait for an ACK packet.
 * If ACK doesn't return before retransmission timeout,
 * re-send the packet.  Keep on trying for NETIMG_MAXTRIES times.
 *
 * Returns 0 on success: pkt sent without error and ACK returned.
 * Else, return 1 if ACK is not received.
 * Terminate process on send or receive error.
 *
 * Nothing else is modified.
*/
int
imgdb_sendpkt(int sd, struct sockaddr_in *client, char *pkt, int size, ihdr_t *ack)
{
  /* Task 2.1: sends the provided pkt to client as you did in Lab 5.
   * In addition, initialize a struct timeval timeout variable to
   * NETIMG_SLEEP sec and NETIMG_USLEEP usec and wait
   * for read event on socket sd up to the timeout value.
   * If no read event occurs before the timeout, try sending
   * the packet to client again.  Repeat NETIMG_MAXTRIES times.
   * If read event occurs before timeout, receive the incoming
   * packet and make sure that it is an ACK pkt as expected.
   */
  /* YOUR CODE HERE */
  int try_time = 0;
  do {
    int err = sendto(sd, pkt, size, 0, (sockaddr*)client, sizeof(sockaddr_in));
    net_assert((err < 0), "imgdb_sendpkt: send error");
  
  
    fd_set rset;
    FD_ZERO(&rset);
    FD_SET(sd, &rset);
    timeval timeout;
    timeout.tv_sec = NETIMG_SLEEP;
    timeout.tv_usec = NETIMG_USLEEP;
    err = select(sd+1, &rset, NULL, NULL, &timeout);
    net_assert((err < 0), "imgdb_sendpkt: select error");
    
    if (err > 0) {
      socklen_t slen = sizeof(sockaddr_in);
      err = recvfrom(sd, ack, sizeof(ihdr_t), 0, (sockaddr*)client, &slen);
      net_assert((err < (int)sizeof(ihdr_t)), "imgdb_sendpkt: bad size");
      net_assert((ack->ih_vers != NETIMG_VERS), "imgdb_sendpkt: bad vers");
      net_assert((ack->ih_type != NETIMG_ACK), "imgdb_sendpkt: bad type");


      return(0);
    }
    ++try_time;
  } while (try_time < NETIMG_MAXTRIES);
  
  return(1);
}

/*
 * imgdb_sendimage: 
 * Send the image contained in *image to the client
 * pointed to by *client. Send the image in
 * chunks of segsize, not to exceed mss, instead of
 * as one single image. With probability pdrop, drop
 * a segment instead of sending it.
 *
 * If receive wrong packet type while waiting for ACK,
 * assume client has exited, and simply return to caller.
 *
 * Terminate process upon encountering any other error.
 * Doesn't otherwise modify anything.
*/
void
imgdb_sendimage(int sd, struct sockaddr_in *client, unsigned short mss,
                unsigned char rwnd, unsigned char fwnd,
                LTGA *image, int img_size, float pdrop)

{
  unsigned char *ip;

  int datasize;

  ip = image->GetPixels();    /* ip points to the start of byte buffer holding image */
  datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIPSIZE;

  /* make sure that the send buffer is of size at least mss. */
  /* Lab 5: YOUR CODE HERE */
  
  unsigned int min_size = mss;
  int err = setsockopt(sd, SOL_SOCKET, SO_SNDBUF, &min_size, sizeof(unsigned int));
  net_assert((err <  0), "imgdb_sendimage: setsockopt");
  
  /* 
   * Populate a struct msghdr with information of the destination client,
   * a pointer to a struct iovec array.  The iovec array should be of size
   * NETIMG_NUMIOVEC.  The first entry of the iovec should be initialized
   * to point to an ihdr_t, which should be re-used for each chunk of data
   * to be sent.
  */
  /* Lab 5: YOUR CODE HERE */
  ihdr_t ihdr_msg;
  ihdr_msg.ih_vers = NETIMG_VERS;
  ihdr_msg.ih_type = NETIMG_DAT;

  iovec iov[NETIMG_NUMIOVEC];
  iov[0].iov_base = &ihdr_msg;
  iov[0].iov_len = sizeof(ihdr_t);

  msghdr msg;
  msg.msg_name = client;
  msg.msg_namelen = sizeof(sockaddr_in);
  msg.msg_iov = iov;
  msg.msg_iovlen = NETIMG_NUMIOVEC;
  msg.msg_control = NULL;
  msg.msg_controllen = 0;
  
  /* Task 2.2 and Task 4.1: initialize any necessary variables
   * for your sender side sliding window and FEC window.
   */
  /* YOUR CODE HERE */
  int swnd_size = rwnd*datasize;
  int swnd_start = 0;
  int swnd_end = rwnd*datasize;
  int fec_start = 0;
  int fec_count = 0;
  unsigned char* fecdata = new unsigned char[datasize];
  do {
    /* Task 2.2: estimate the receiver's receive buffer based on packets
     * that have been sent and ACKed.  We can only send as much as the
     * receiver can buffer.
     */
    /* YOUR CODE HERE */

    int img_left = img_size - swnd_start;
    if (img_left < swnd_size)
      swnd_size = img_left;
    swnd_end = swnd_start + swnd_size;
    /* Task 2.2: Send one usable window-full of data to client using
     * sendmsg() to send each segment as you did in Lab5.  As in Lab
     * 5, you probabilistically drop a segment instead of sending it.
     * Basically, copy the code within the do{}while loop in
     * imgdb_sendimage() from Lab 5 here, but put it within another
     * loop such that a usable window-full of segments can be sent out
     * using the Lab 5 code.
     *
     * Task 4.1: Before you send out each segment, update your FEC variables and
     * initialize or accumulate your FEC data packet by copying your
     * Lab 6 code here appropriately.
     *
     * Task 4.1: After you send out each segment, if you have accumulated an FEC
     * window full of segments or the last segment has been sent, send
     * your FEC data.  Again, copy your Lab 6 code here to the
     * appropriate place.
     */
    /* YOUR CODE HERE */

    int snd_next = swnd_start;
    do {
      int swnd_left = swnd_end - snd_next;
      int segsize = datasize > swnd_left ? swnd_left : datasize;

      if (fec_count % fwnd == 0) {
        fec_init(fecdata, ip+snd_next, datasize, segsize);
        fec_start = snd_next;
      }
      else 
        fec_accum(fecdata, ip+snd_next, datasize, segsize);
      ++fec_count; 

      if (((float) random())/INT_MAX < pdrop) {
        fprintf(stderr, "imgdb_sendimage: DROPPED offset 0x%d, %d bytes\n",
                 snd_next, segsize);
      } else {

        ihdr_msg.ih_size = htons(segsize);
        ihdr_msg.ih_seqn = htonl(snd_next);

        iov[1].iov_base = ip + snd_next;
        iov[1].iov_len = segsize;

        err = sendmsg(sd, &msg, 0);
        net_assert((err < 0), "sendimg:sendmsg error");

        fprintf(stderr, "imgdb_sendimage: sent offset 0x%d, %d bytes\n",
               snd_next, segsize);
      }
      snd_next += segsize;
      if (fec_count % fwnd == 0 || 
                snd_next == img_size) {

        for (;fec_count%fwnd != 0; ++fec_count)
          fec_accum(fecdata, NULL, datasize, 0);  

        ihdr_msg.ih_size = htons(datasize);
        ihdr_msg.ih_seqn = htonl(fec_start + fwnd*datasize);
        iov[1].iov_base = fecdata;
        iov[1].iov_len = datasize;
      
        ihdr_msg.ih_type = NETIMG_FEC;


        if (((float) random())/INT_MAX < pdrop) {
          fprintf(stderr, "imgdb_sendimage: DROPPED NETIMG_FEC packet of seqn %d\n",
                 fec_start + fwnd*datasize);
        } else {
          fprintf(stderr, "imgdb_sendimage: sent NETIMG_FEC packet of seqn %d\n",
                 fec_start+fwnd*datasize);
          err = sendmsg(sd, &msg, 0);
          net_assert((err < 0), "sendimg: sendmsg error (fec)");

        }
        ihdr_msg.ih_type = NETIMG_DAT;
      }  
    } while (snd_next < swnd_end);


    /* Task 2.2: Next wait for ACKs for up to NETIMG_SLEEP secs and NETIMG_USLEEp usec. */
    /* YOUR CODE HERE */

    int try_time = 0;
    do {
      fd_set rset;
      FD_ZERO(&rset);
      FD_SET(sd, &rset);
      timeval timeout;
      timeout.tv_sec = NETIMG_SLEEP;
      timeout.tv_usec = NETIMG_USLEEP;
    
      err = select(sd+1, &rset, NULL, NULL, &timeout);
      net_assert((err < 0), "imgdb_sendimg: select error");
      
    /* Task 2.2: If an ACK arrived, grab it off the network and slide
     * our window forward when possible. Continue to do so for all the
     * ACKs that have arrived.  Remember that we're using cumulative ACK.
     *
     * We have a blocking socket, but here we want to
     * opportunistically grab all the ACKs that have arrived but not
     * wait for the ones that have not arrived.  So, when we call
     * receive, we want the socket to be non-blocking.  However,
     * instead of setting the socket to non-blocking and then set it
     * back to blocking again, we simply set flags=MSG_DONTWAIT when
     * calling the receive function.
     */
    /* YOUR CODE HERE */
      if (err > 0) {
        ihdr_t data_ack;
        socklen_t slen = sizeof(sockaddr_in);
        err = recvfrom(sd, &data_ack, sizeof(ihdr_t), MSG_DONTWAIT, (sockaddr*)client, &slen);
        net_assert((err < (int)sizeof(ihdr_t)), "imgdb_sendpkt: bad size");
        net_assert((data_ack.ih_vers != NETIMG_VERS), "imgdb_sendpkt: bad vers");
        net_assert((data_ack.ih_type != NETIMG_ACK), "imgdb_sendpkt: bad type");

        int ack_seqn = ntohl(data_ack.ih_seqn);
        swnd_start = ack_seqn;
        fprintf(stderr, "imgdb_sendimg: received NETIMG_ACK packet with seqn %d\n", ack_seqn);
      } else
        ++try_time;
    } while (try_time < NETIMG_MAXTRIES); 

    /* Task 2.2: If no ACK returned up to the timeout time, trigger Go-Back-N
     * and re-send all segments starting from the last unACKed segment.
     *
     * Task 4.1: If you experience RTO, reset your FEC window to start
     * at the segment to be retransmitted.
     */
    /* YOUR CODE HERE */
    fec_count = 0;


  } while (swnd_start < img_size); 

  fprintf(stderr, "imgdb_sendimg: sent NETIMG_FIN packet\n");
  ihdr_t fin_hdr;
  fin_hdr.ih_vers = NETIMG_VERS;
  fin_hdr.ih_type = NETIMG_FIN;
  fin_hdr.ih_seqn = htonl(NETIMG_FINSEQ);

  ihdr_t fin_return;
  imgdb_sendpkt(sd, client, (char *)&fin_hdr, sizeof(ihdr_t), &fin_return);
  return;
}

int
main(int argc, char *argv[])
{ 
  int sd;
  float pdrop=NETIMG_PDROP;
  LTGA image;
  imsg_t imsg;
  ihdr_t ack;
  int img_size;
  struct sockaddr_in client;
  unsigned short mss;
  unsigned char rwnd, fwnd;
  char fname[NETIMG_MAXFNAME] = { 0 };

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif

  // parse args, see the comments for imgdb_args()
  if (imgdb_args(argc, argv, &pdrop)) {
    imgdb_usage(argv[0]);
  }

  srandom(48914+(int)(pdrop*100));
  sd = imgdb_sockinit();
  
  while (1) {
    imgdb_recvquery(sd, &client, &mss, &rwnd, &fwnd, fname);
    imgdb_imginit(fname, &image, &imsg, &img_size);
    if (imgdb_sendpkt(sd, &client, (char *) &imsg, sizeof(imsg_t), &ack)) {
      continue;
    }

    net_assert((ntohl(ack.ih_seqn) != NETIMG_DIMSEQ), "imgdb: sendimsg received wrong ACK seqn");
    imgdb_sendimage(sd, &client, mss, rwnd, fwnd, &image, img_size, pdrop);
    fprintf(stderr, "sending completed\n");
  }
    
  //close(sd);
#ifdef _WIN32
  WSACleanup();
#endif
  exit(0);
}
