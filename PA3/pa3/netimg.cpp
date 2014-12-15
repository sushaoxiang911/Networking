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
#include <math.h>          // ceil()
#include <assert.h>        // assert()
#include <limits.h>        // LONG_MAX
#include <errno.h>
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
#include <sys/ioctl.h>     // ioctl(), FIONBIO
#endif
#ifdef __APPLE__
#include <GLUT/glut.h>
#else
#include <GL/glut.h>
#endif

#include "netimg.h"
#include "fec.h"           // Lab 6

#include <iostream>
using namespace std;

int sd;                    // socket descriptor
imsg_t imsg;
unsigned char *image;
int img_size;              // Lab 6
float pdrop;               // Lab 6: drop probability
unsigned short mss;        // receiver's maximum segment size, in bytes
unsigned char rwnd;        // receiver's window, in packets, of size <= mss
unsigned char fwnd;        // Lab 6: receiver's FEC window < rwnd, in packets
unsigned int next_seqn;    // Lab 6

void
netimg_usage(char *progname)
{
  fprintf(stderr, "Usage: %s -s serverFQDN.port -q <imagename.tga> -d <drop probability [0.011, 0.11]> -w <rwnd [1, 255]> -m <mss (>40)>\n", progname); 
  exit(1);
}

/*
 * netimg_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, *sname
 * points to the server's FQDN, and "port" points to the port to
 * connect at server, in network byte order.  Both "*sname", and
 * "port" must be allocated by caller.  The variable "*imagename"
 * points to the name of the image to search for.  The global
 * variables mss, rwnd, and pdrop are initialized.
 *
 * Nothing else is modified.
 */
int
netimg_args(int argc, char *argv[], char **sname, u_short *port, char **imagename)
{
  char c, *p;
  extern char *optarg;
  int arg;

  if (argc < 5) {
    return (1);
  }
  
  pdrop = NETIMG_PDROP;
  rwnd = NETIMG_NUMSEG;
  mss = NETIMG_MSS;
  while ((c = getopt(argc, argv, "s:q:w:m:d:")) != EOF) {
    switch (c) {
    case 's':
      for (p = optarg+strlen(optarg)-1;      // point to last character of addr:port arg
           p != optarg && *p != NETIMG_PORTSEP;  // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "netimg_args: server address malformed");
      *p++ = '\0';
      *port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETIMG_MAXFNAME), "netimg_args: FQDN too long");
      *sname = optarg;
      break;
    case 'q':
      net_assert((strlen(optarg) >= NETIMG_MAXFNAME), "netimg_args: image name too long");
      *imagename = optarg;
      break;
    case 'w':
      arg = atoi(optarg);
      if (arg < 1 || arg > NETIMG_MAXWIN) {
        return(1);
      }
      rwnd = (unsigned char) arg; 
      break;
    case 'm':
      arg = atoi(optarg);
      if (arg < NETIMG_MINSS || arg > NETIMG_MSS) {
        return(1);
      }
      mss = (unsigned short) arg;
      break;
    case 'd':
      pdrop = atof(optarg);  // global
      if (pdrop > 0.0 && (pdrop > 0.11 || pdrop < 0.051)) {
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
 * netimg_sockinit: creates a new socket to connect to the provided server.
 * The server's FQDN and port number are provided.  The port number
 * provided is assumed to already be in network byte order.
 *
 * On success, the global socket descriptor sd is initialized.
 * On error, terminates process.
 */
void
netimg_sockinit(char *sname, u_short port)
{
  int err;
  struct sockaddr_in server;
  struct hostent *sp;
  int bufsize=0;

#ifdef _WIN32
  WSADATA wsa;
  
  err = WSAStartup(MAKEWORD(2,2), &wsa);  // winsock 2.2
  net_assert(err, "netimg_sockinit: WSAStartup");
#endif

  /* 
   * create a new UDP socket, store the socket in the global variable sd
  */
  /* Lab 5: YOUR CODE HERE */
  
  sd = socket(AF_INET, SOCK_DGRAM, 0);
  net_assert((sd < 0), "netimg_sockinit: create socket");
  /* obtain the server's IPv4 address from sname and initialize the
     socket address with server's address and port number . */
  memset((char *) &server, 0, sizeof(struct sockaddr_in));
  server.sin_family = AF_INET;
  server.sin_port = port;
  sp = gethostbyname(sname);
  net_assert((sp == 0), "netimg_sockinit: gethostbyname");
  memcpy(&server.sin_addr, sp->h_addr, sp->h_length);

  /* set socket receive buffer size */
  /* Lab 5: YOUR CODE HERE */
  bufsize = mss*rwnd;
  err = setsockopt(sd, SOL_SOCKET, SO_RCVBUF, &bufsize, sizeof(int));
  net_assert((err < 0), "netimg_sockinit: setsockopt");
  
  fprintf(stderr, "netimg_sockinit: socket receive buffer set to %d bytes\n", bufsize);
  
  /* since this is a UDP socket, connect simply "remembers" server's address+port# */
  err = connect(sd, (struct sockaddr *) &server, sizeof(struct sockaddr_in));
  net_assert(err, "netimg_sockinit: connect");

  return;
}

/*
 * netimg_sendquery: send a query for provided imagename to connected
 * server.  Query is of type iqry_t, defined in netimg.h.  The query
 * packet must be of version NETIMG_VERS and of type NETIMG_SYN both
 * also defined in netimg.h. In addition to the filename of the image
 * the client is searching for, the query message also carries the
 * receiver's FEC window size, receive window size (rwnd) and maximum
 * segment size (mss).  Both rwnd and mss are global variables.
 *
 * On send error, return 0, else return 1
 */
int
netimg_sendquery(char *imagename)
{
  int bytes;
  iqry_t iqry;

  iqry.iq_vers = NETIMG_VERS;
  iqry.iq_type = NETIMG_SYN;
  iqry.iq_mss = htons(mss);   // global
  iqry.iq_rwnd = rwnd;        // global
  iqry.iq_fwnd = fwnd = NETIMG_FECWIN >= rwnd ? rwnd-1 : NETIMG_FECWIN;  // Lab 6
  strcpy(iqry.iq_name, imagename); 
  bytes = send(sd, (char *) &iqry, sizeof(iqry_t), 0);
  if (bytes != sizeof(iqry_t)) {
    return(0);
  }

  return(1);
}
  
/*
 * netimg_recvimsg: receive an imsg_t packet from server and store it
 * in the global variable imsg.  The type imsg_t is defined in
 * netimg.h.  Check that received message is of the right version
 * number and of type NETIMG_DIM.  If message is of the wrong version
 * number or the wrong type, terminate process. For error in receiving
 * packet, close the socket and return -1.  If packet successfully
 * received, convert the integer fields of imsg back to host byte
 * order.  If the received imsg has im_found field == 0, it indicates
 * that no image is sent back, most likely due to image not found.  In
 * which case, return 0, otherwise return 1.
 */
int
netimg_recvimsg()
{
  int i, bytes;
  double img_dsize;
  int format;

  /* receive imsg packet and check its version and type */
  bytes = recv(sd, (char *) &imsg, sizeof(imsg_t), 0);   // imsg global
  if (bytes <= 0) {
    //close(sd);
    return(-1);
  }
  net_assert((bytes != sizeof(imsg_t)), "netimg_recvimsg: malformed header");
  net_assert((imsg.im_vers != NETIMG_VERS), "netimg_recvimg: wrong imsg version");
  net_assert((imsg.im_type != NETIMG_DIM), "netimg_recvimg: wrong imsg type");

  if (imsg.im_found) {
    imsg.im_height = ntohs(imsg.im_height);
    imsg.im_width = ntohs(imsg.im_width);
    imsg.im_format = ntohs(imsg.im_format);
    
    /* compute image size */
    img_dsize = (double) (imsg.im_height*imsg.im_width*(u_short)imsg.im_depth);
    net_assert((img_dsize > (double) NETIMG_MAXSEQ), "netimg_recvimsg: image too large");
    img_size = (int) img_dsize;                 // global

    /* allocate space for image */
    image = (unsigned char *)malloc(img_size*sizeof(unsigned char));

    /* determine pixel format */
    switch(imsg.im_format) {
    case GL_RGBA:
      format = 4;
      break;
    case GL_RGB:
      format = 3;
      break;
    case GL_LUMINANCE_ALPHA:
      format = 2;
      break;
    default:
      format = 1;
      break;
    }

    /* paint the image texture background red if color, white otherwise
     to better visualize lost segments */
    for (i = 0; i < img_size; i += format) {
      image[i] = (unsigned char) 0xff;
    }

    /* Task 2.1:
     * Send back an ACK with ih_type = NETIMG_ACK.
     * Initialize any variable necessary to keep track of ACKs.
     *ve an email from the U
    * YOUR CODE HERE */
    ihdr_t hdr;
    hdr.ih_vers = NETIMG_VERS;
    hdr.ih_type = NETIMG_ACK;
    unsigned int send_seqn = NETIMG_DIMSEQ;
    hdr.ih_seqn = htonl(send_seqn);
    bytes = send(sd, (char*)&hdr, sizeof(ihdr_t), 0);
    net_assert((bytes < (int)sizeof(ihdr_t)), "recv_imsg: bad size");

    return (1);
  }

  return (0);
}

/* 
 * netimg_recvimage: called by GLUT when idle.
 * On each call, receive a chunk of the image from the network and
 * store it in global variable "image" at offset from the
 * start of the buffer as specified in the header of the packet.
 *
 * Terminate process on receive error.
 */
void
netimg_recvimage(void)
{
  ihdr_t hdr;
  
  /* The image data packet from the server consists of an ihdr_t header
   * followed by a chunk of data.  We want to put the data directly into
   * the buffer pointed to by the global variable "image" without any
   * additional copying. To determine the correct offset from the start of
   * the buffer to put the data into, we first need to retrieve the
   * sequence number stored in the packet header.  Since we're dealing with
   * UDP packet, however, we can't simply read the header off the network,
   * leaving the rest of the packet to be retrieved by subsequent calls to
   * recv(). Instead, what we need to do is call recv() with flags == MSG_PEEK.
   * This allows us to retrieve a copy of the header without removing the packet
   * from the receive buffer.
   *
   * Since our socket has been set to non-blocking mode, if there's no packet
   * ready to be retrieved from the socket, the call to recv() will return
   * immediately with return value -1 and the system global variable "errno"
   * set to EAGAIN or EWOULDBLOCK (defined in errno.h).  In which case, 
   * this function should simply return to caller.
   * 
   * Once a copy of the header is made, check that it has the version number and
   * that it is of type NETIMG_DAT.  Convert the size and sequence number in the
   * header to host byte order.
   */
  /* Lab 5: YOUR CODE HERE */
  int err = recv(sd, &hdr, sizeof(ihdr_t), MSG_PEEK);

  if (err == -1) {
    return;
  }
  int datasize = mss - sizeof(ihdr_t) - NETIMG_UDPIPSIZE;
  unsigned char* fecdata = new unsigned char[datasize]; 

  net_assert((err < (int)sizeof(ihdr_t)), "netimg_recvimage: peek header false");
  net_assert((hdr.ih_vers != NETIMG_VERS), "netimg_recvimage: bad ih_vers");
  net_assert((hdr.ih_type != NETIMG_DAT 
                && hdr.ih_type != NETIMG_FEC
                && hdr.ih_type != NETIMG_FIN), "netimg_recvimage: bad ih_type");
  
  int snd_next = ntohl(hdr.ih_seqn); 
  int segsize = ntohs(hdr.ih_size);

  /* Populate a struct msghdr with a pointer to a struct iovec
   * array.  The iovec array should be of size NETIMG_NUMIOVEC.  The
   * first entry of the iovec should be initialized to point to the
   * header above, which should be re-used for each chunk of data
   * received.
   *
   * This is the same code from Lab 5, we're just pulling 
   * the parts common to both NETIMG_DAT and NETIMG_FEC packets
   * out of the two code branches.
   */
  /* Lab 5: YOUR CODE HERE */
  iovec iov[NETIMG_NUMIOVEC];
  iov[0].iov_base = &hdr;
  iov[0].iov_len = sizeof(ihdr_t); 
  msghdr msg;
  msg.msg_iov = iov;
  msg.msg_iovlen = NETIMG_NUMIOVEC;


  static int first_lost = -1;
  static int fwnd_start = 0;
  static int expected_next = 0;

  static bool go_back_n = false;

  /* Task 2.3: initialize your ACK packet */
  ihdr_t ack_msg;
  ack_msg.ih_vers = NETIMG_VERS;
  ack_msg.ih_type = NETIMG_ACK;
  bool if_send_ack = false;
  
  if (hdr.ih_type == NETIMG_DAT) {
    fprintf(stderr, "netimg_recvimage: received offset 0x%d, %d bytes, waiting for 0x%d\n",
            snd_next, segsize, snd_next+segsize);
    
    /* 
     * Now that we have the offset/seqno information from the packet
     * header, point the second entry of the iovec to the correct offset from
     * the start of the image buffer pointed to by the global variable
     * "image".  Both the offset/seqno and the size of the data to be
     * received into the image buffer are recorded in the packet header
     * retrieved above. Receive the segment by calling recvmsg().
     * Convert the size and sequence number in the header to host byte order.
     */
    /* Lab 5: YOUR CODE HERE */
    iov[1].iov_base = image + snd_next;
    iov[1].iov_len = segsize;
     
    recvmsg(sd, &msg, 0);
    
    /* Task 2.3: If the incoming data packet carries the expected
     * sequence number, update our expected sequence number and
     * prepare to send back an ACK packet.  Otherwise, if the packet
     * arrived out-of-order and the sequence number is larger than the
     * expected one, don't send back an ACK, per Go-Back-N.  If the
     * sequence number is smaller than the expected sequence number,
     * however, do send back an ACK, tagged with the expected sequence
     * number, just to ensure that the sender knows what our current
     * expectation is.
     */
    /* YOUR CODE HERE */
//@@@!!!!! the seqn of ACK is the packet seqn + segsize
    if (snd_next < expected_next) {
    
//@@@ receive first lost from a retransmit
      if (snd_next == first_lost) {
        first_lost = -1;
      }

      ack_msg.ih_seqn = htonl(expected_next);
      if (first_lost == -1 && !go_back_n)
      if_send_ack = true;
    } else if (snd_next == expected_next) {
      ack_msg.ih_seqn = htonl(expected_next + datasize);
      
      
      expected_next += datasize;
      
      if (go_back_n)
        go_back_n = false;
      
      if (first_lost == -1 && !go_back_n)
        if_send_ack = true; 
    
    } else if (snd_next > expected_next && first_lost == -1 && !go_back_n) {
      first_lost = expected_next;
      expected_next += datasize;
      if (snd_next == expected_next)
        expected_next += datasize; 
      if (snd_next > expected_next) {
cout << "set go back n is true 1" << endl;
        go_back_n = true;   
      }
    } else {
cout << "set go back n is true 2" << endl;
      go_back_n = true;
    }
 
    
    if (snd_next-fwnd_start >= datasize*fwnd) {
//@@@ in go_back_n, don't operate on the fec data
cout << "inside move fec window" << endl;
cout << "snd_next: " << snd_next << endl;
cout << "expected_next: " << expected_next << endl;
      if (go_back_n) {
      } else {
//@@@ no lost 
//@@@ perhaps not occurs, only if we set fec can be itself lost
//@@@ we ignore the expected FEC and move the window
        if (first_lost == -1) {
          fwnd_start = expected_next;
/*
          if (snd_next + datasize  == expected_next) {
          } else if (snd_next == expected_next) {
            first_lost = expected_next;
            expected_next = snd_next + segsize;
          } else {
            first_lost = expected_next;
            expected_next = expected_next + datasize;
cout << "set go back n is true 3" << endl;
            go_back_n = true;
          }
*/
        } else {
//@@@ there is lost in the former packets
//@@@ dont do anything, but just set expected to the first lost
//@@@ no matter how, server will resend this packet
          expected_next = first_lost;
//@@@ make no sense, since in the server perspective, the start of the fec window is not
// neccessarily to be the expected_next     
//     fwnd_start = expected_next;
          first_lost = -1;
          go_back_n = true;
cout << "set go back n is true 3" << endl;
cout << "expected_next is set up: " << expected_next << endl;
        }
      }
/*
      fwnd_start = snd_next - datasize*fwnd;
      if (snd_next > fwnd_start)
        first_lost = fwnd_start;
*/
    }
 
    /* You should handle the case when the FEC data packet itself may be
     * lost, and when multiple packets within an FEC window are lost, and
     * when the first few packets from the subsequent FEC window following a
     * lost FEC data packet are also lost.  Thus in In addition to relying on
     * fwnd and the count of total packets received within an FEC
     * window, you want to rely on the sequence numbers in arriving
     * packets to determine when you have received an FEC-window full of data
     * bytes.
     *
     * To that end, in addition to keeping track of lost packet offset
     * below, every time a data packet arrives, first check whether
     * you have received an FEC-window full (or more) of data bytes
     * without receiving any FEC packet.  In which case, you need to
     * reposition your FEC window by computing the start of the
     * current FEC window, reset your count of packets received, and
     * determine the next expected packet.
     */
    /* Lab 6: YOUR CODE HERE */

    
//    expected_next = snd_next + datasize;
    /* Task 4.2: Next check whether the arriving data packet is the
     * next data packet you're expecting.  If so, we are not in
     * Go-Back-N retransmission mode, so we should increment our next
     * expected packet within the FEC window and if we were in
     * Go-Back-N retransmission mode, take ourselves out of it.
     *
     * If not, you've lost a packet, mark the location of the first
     * lost packet (i.e., the next packet you're expecting) in an FEC
     * window.  If more than one packet is lost, you don't need to
     * mark subsequent losses, just keep a count of the total number
     * of packets received.  If arriving data packet has sequence
     * number within the current fwnd, increment count, otherwise, pkt
     * was out of order or was sent before the fwnd got reset due to
     * lost pkt.
     *
     * If the gap between the expected byte within the FEC window and
     * the current byte is larger than an FEC-widow full of data,
     * there's no way to tell how many packets we have actually lost,
     * but for sure we have lost an FEC packet in addition to the
     * expected segment.  So there's no way for us to recover the lost
     * expected segment and Go-Back-N will be triggered and we should
     * ride it out by putting ourselves into Go-Back-N mode and
     * not rely on FEC until the expected segment has been
     * retransmitted and received.  At which point, we will reactivate
     * FEC, re-starting the FEC window at the retransmitted segment.
     *
     */
    /* YOUR CODE HERE */
    /* Task 4.2: If we're not in Go-Back-N mode, keep track of packet
       received within the current FEC window */
    /* YOUR CODE HERE */

  } else if (hdr.ih_type == NETIMG_FEC) { // FEC pkt
    /* 
     * Re-use the same struct msghdr above to receive an FEC packet.
     * Point the second entry of the iovec to your FEC data buffer and
     * update the size accordingly.
     * Receive the segment by calling recvmsg().
     *
     * Convert the size and sequence number in the header to host byte order.
     *
     * This is an adaptation of your Lab 5 code.
     */
    /* Lab 6: YOUR CODE HERE */
    iov[1].iov_base = fecdata;
    iov[1].iov_len = datasize;
    recvmsg(sd, &msg, 0);
    /* 
     * Task 4.2: If you're not in Go-Back-N mode:
     *
     * Check if you've lost only one packet within the FEC window, if so,
     * reconstruct the lost packet.  Remember that we're using the image data
     * buffer itself as our FEC buffer and that you've noted above the
     * sequence number that marks the start of the current FEC window.  To
     * reconstruct the lost packet, use fec.cpp:fec_accum() to XOR
     * the received FEC data against the image data buffered starting from
     * the start of the current FEC window, one <tt>datasize</tt> at a time,
     * skipping over the lost segment, until you've reached the end of the
     * FEC window.  If fec_accum() has been coded correctly, it
     * should be able to correcly handle the case when the last segment of
     * the FEC-window is smaller than datasize *(but you must still do the
     * detection for short last segment here and provide fec_accum() with the
     * appropriate segsize)*.
     *
     * Once you've reconstructed the lost segment, copy it from the FEC data buffer to
     * correct offset on the image buffer.  You must be careful that if the
     * lost segment is the last segment of the image data, it may be of size
     * smaller than datasize, in which case, you should copy only
     * the correct amount of bytes from the FEC data buffer to the image data
     * buffer.
     *
     * Task 4.2: After you've patched the lost packet, send back an
     * ACK for the last byte received within this FEC window.
     *
     * If no packet was lost in the current FEC window, there's
     * nothing further to do with the current FEC window, just move on
     * to the next one.
     *
     * Task 4.2: If more than 1 pkts were lost within the current FEC window,
     * put yourself in Go-Back-N mode.
     *
     * Before you move on to the next FEC window, you may want to
     * reset your FEC-window related variables to prepare for the
     * processing of the next window.
     */
    /* Lab 6: YOUR CODE HERE */
cout << "received fec data: " << snd_next << endl;
cout << "if in go_back_n: " << go_back_n << endl;
cout << "first_lost: " << first_lost << endl;
cout << "expected_next: " << expected_next << endl;
//@@@ not in go_back_n mode
    if (!go_back_n) {
//@@@ if there is no first_lost
//@@@ then might be the last packet is lost, use expected_next to check it
        if (first_lost == -1) {
//@@@ if the last packet is lost
//@@@ we set the first_lost to expected_next
//@@@ there is also possiblity that we lost two together
//@@@ then go_back_n should be set and we must not go into fec recovery
//@@@ notice we have to calculate the img_size last packet sequence
//@@@ which is img_size/datasize * datasize
          if ((snd_next >= img_size && expected_next <= (img_size/datasize)*datasize) || 
                (snd_next < img_size && expected_next <=  snd_next - datasize)) { 
cout << "lost last in this fec window" << endl;
            first_lost = expected_next;
          } 
          if ((snd_next >= img_size && expected_next < (img_size/datasize)*datasize) || 
                (snd_next < img_size && expected_next <  snd_next - datasize)) {
cout << "lost more!!" << endl;
cout << "set go_back_n is true 4" << endl;
            go_back_n = true;
            expected_next = first_lost + datasize;
//@@@ else we lost nothing and ignore it
        }
    }
//@@@ check if the go_back_n is modified
    if (!go_back_n) {
//@@@ then go into first_lost again
//@@@ after the previous update, if first_lost is not -1, then we can reconstruct it
        if (first_lost != -1) {
cout << "fec recovery" << endl;
//@@@ find the fwnd_start
//@@@ at this point, fwnd_start is consistent with the server
          fwnd_start = snd_next - fwnd * datasize;
          for (int i = 0; i < fwnd; ++i) {
            int current_offset = fwnd_start + datasize * i;
            if (current_offset == first_lost)
              continue;
//@@@ check the datasize to be accum
            int current_packet_size = datasize;
//@@@ the last packet which contains not a full datasize
            if (img_size - current_offset < datasize)
              current_packet_size = img_size - current_offset;
//@@@ the packet is the zeros (seqn after image_size)
//@@@ set to zero size
            if (current_packet_size <= 0)
              current_packet_size = 0;
            fec_accum(fecdata, image+current_offset, datasize, current_packet_size);
          }
//@@@ check if the packet is smaller than a datasize
          int lost_size = datasize;
          if (first_lost + datasize >= img_size)
            lost_size = img_size - first_lost;
          memcpy(image+first_lost, fecdata, lost_size);
        }
        first_lost = -1;
        fwnd_start = snd_next;
        expected_next = snd_next;    
        
        ack_msg.ih_seqn = htonl(snd_next);
        if_send_ack = true;
      }
    } else {
//@@@ there is a possiblity that the first lost is obtained so the second can be reconstructed
//@@@ by this FEC.
    }

/*
    int packet_number = (snd_next - fwnd_start) / datasize;
    if ((snd_next - fwnd_start) % datasize)
      ++packet_number;
    if (packet_number - packets_received == 1) {
      if (first_lost == -1) {
        first_lost = expected_next;
      }
      for (int i = 0; i < packet_number; ++i) {
        int current_offset = fwnd_start + datasize*i;
        if (current_offset == first_lost)
          continue;
        fec_accum(fecdata, image+current_offset, datasize, datasize);
         
      } 
      int lost_size = datasize;
      if (first_lost + datasize >= snd_next)
        lost_size = snd_next - first_lost;
      memcpy(image+first_lost, fecdata, lost_size);   
    }
    first_lost = -1;
    fwnd_start = snd_next;
    expected_next = snd_next;
*/
  } else {  // NETIMG_FIN pkt

    /* Task 2.3: else it's a NETIMG_FIN packet, prepare to send back an
       ACK with NETIMG_FINSEQ as the sequence number */
    /* YOUR CODE HERE */
    fprintf(stderr, "netimg_recvimg: received NETIMG_FIN");
//@@@ have to receive the message 
    err = recv(sd, &hdr, sizeof(ihdr_t), 0);
    net_assert((err != (int)sizeof(ihdr_t)), "netimg_recvimg: bad size of NETIMG_FIN")
    ack_msg.ih_seqn = htonl(NETIMG_FINSEQ);
    if_send_ack = true;
  }

  /* Task 2.3:
   * If we're to send back an ACK, send it now.
   * Probabilistically drop the ACK instead of sending it back.
   */ 
  /* YOUR CODE HERE */
  if (if_send_ack) {
    if (((float) random())/INT_MAX < pdrop)
      fprintf(stderr, "netimg_recvimg: DROPPED ack with seqn %d\n", ntohl(ack_msg.ih_seqn));
    else {
//@@@ send ack
      fprintf(stderr, "netimg_recvimg: send ack with seqn %d\n", ntohl(ack_msg.ih_seqn));
      err = send(sd, (char*) &ack_msg, sizeof(ihdr_t), 0);
      net_assert((err < (int)sizeof(ihdr_t)), "netimg_recvimg: send ack bad size");
    }
  }
  
  delete[] fecdata;
  /* give the updated image to OpenGL for texturing */
  glTexImage2D(GL_TEXTURE_2D, 0, (GLint) imsg.im_format,
               (GLsizei) imsg.im_width, (GLsizei) imsg.im_height, 0,
               (GLenum) imsg.im_format, GL_UNSIGNED_BYTE, image);
  /* redisplay */
  glutPostRedisplay();

  return;
}

int
main(int argc, char *argv[])
{
  char *sname, *imagename;
  u_short port;
  int err;

  // parse args, see the comments for netimg_args()
  if (netimg_args(argc, argv, &sname, &port, &imagename)) {
    netimg_usage(argv[0]);
  }

#ifndef _WIN32
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */
#endif
  
  srandom(48914+(int)(pdrop*100));

  netimg_sockinit(sname, port);

  if (netimg_sendquery(imagename)) {

    err = netimg_recvimsg();
    if (err == 1) { // if image found
      netimg_glutinit(&argc, argv, netimg_recvimage);
      netimg_imginit();
      
      /* set socket non blocking */
      /* Lab 5: YOUR CODE HERE */
	  int on = 1;
      err = ioctl(sd, FIONBIO, (char*)&on);
      net_assert((err < 0), "main: set socket non blocking");
      /* start the GLUT main loop */
      glutMainLoop();

    } else if (err < 0) {
      fprintf(stderr, "%s: server busy, please try again later.\n", argv[0]);

    } else {
      fprintf(stderr, "%s: %s image not found.\n", argv[0], imagename);
    }
  }

  return(0);
}
