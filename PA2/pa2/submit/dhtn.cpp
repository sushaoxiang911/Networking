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
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/ioctl.h>     // ioctl(), FIONBIO

#include "netimg.h"
#include "hash.h"
#include "dhtn.h"

void
dhtn_usage(char *progname)
{
  fprintf(stderr, "Usage: %s [-p <FQDN:port> -I nodeID -i foldername]\n", progname); 

  exit(1);
}

/*
 * dhtn_args: parses command line args.
 *
 * Returns 0 on success or 1 on failure.  On successful return, the
 * provided known node's FQDN, if any, is pointed to by "cli_fqdn" and
 * "cli_port" points to the port to connect at the known node, in network
 * byte order.  If the optional -I option is present, the provided ID
 * is copied into the space pointed to by "id".  The variables "port"
 * and "id" must be allocated by caller. A string folder_name would be
 * set up.
 *
 * Nothing else is modified.
 */
int
dhtn_args(int argc, char *argv[], char **cli_fqdn, u_short *cli_port, int *id, string& folder_name)
{
  char c, *p;
  extern char *optarg;

  net_assert(!cli_fqdn, "dhtn_args: cli_fqdn not allocated");
  net_assert(!cli_port, "dhtn_args: cli_port not allocated");
  net_assert(!id, "dhtn_args: id not allocated");

  *id = ((int) NETIMG_IDMAX)+1;

  while ((c = getopt(argc, argv, "p:I:i:")) != EOF) {
    switch (c) {
    case 'p':
      for (p = optarg+strlen(optarg)-1;     // point to last character of addr:port arg
           p != optarg && *p != NETIMG_PORTSEP; // search for ':' separating addr from port
           p--);
      net_assert((p == optarg), "dhtn_args: peer addressed malformed");
      *p++ = '\0';
      *cli_port = htons((u_short) atoi(p)); // always stored in network byte order

      net_assert((p-optarg > NETIMG_MAXFNAME), "dhtn_args: FQDN too long");
      *cli_fqdn = optarg;
      break;
    case 'I':
      *id = atoi(optarg);
      net_assert((*id < 0 || *id > ((int) NETIMG_IDMAX)), "dhtn_args: id out of range");
      break;
    case 'i':
      folder_name = optarg;
    default:
      return(1);
      break;
    }
  }

  return (0);
}

/*
 * set ID of a dhtn node
 */
void dhtn::
setID(int id)
{
  int err, len;
  struct sockaddr_in node;
  char sname[NETIMG_MAXFNAME] = { 0 };
  char addrport[7] = { 0 };
  unsigned char md[SHA1_MDLEN];
  struct hostent *hp;

  /* create a TCP socket, store the socket descriptor in "listen_sd" */
  listen_sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  net_assert((listen_sd < 0), "dhtn::setID: socket");
  
  memset((char *) &node, 0, sizeof(struct sockaddr_in));
  node.sin_family = AF_INET;
  node.sin_addr.s_addr = INADDR_ANY;
  node.sin_port = 0;

  /* bind address to socket */
  err = bind(listen_sd, (struct sockaddr *) &node, sizeof(struct sockaddr_in));
  net_assert(err, "dhtn::setID: bind");

  /* listen on socket */
  err = listen(listen_sd, NETIMG_QLEN);
  net_assert(err, "dhtn::setID: listen");

  /*
   * Obtain the ephemeral port assigned by the OS kernel to this
   * socket and store it in the local variable "node".
   */
  len = sizeof(struct sockaddr_in);
  err = getsockname(listen_sd, (struct sockaddr *) &node, (socklen_t *) &len);
  net_assert(err, "dhtn::setID: getsockname");


  /* Find out the FQDN of the current host and store it in the local
     variable "sname".  gethostname() is usually sufficient. */
  err = gethostname(sname, NETIMG_MAXFNAME);
  net_assert(err, "dhtn::setID: gethostname");

  /* store the host's address and assigned port number in the "self" member variable */
  self.dhtn_port = node.sin_port;
  hp = gethostbyname(sname);
  net_assert((hp == 0), "dhtn::setID: gethostbyname");
  memcpy(&self.dhtn_addr, hp->h_addr, hp->h_length);

  /* if id is not valid, compute id from SHA1 hash of address+port */
  if (id < 0 || id > (int) NETIMG_IDMAX) {
    memcpy(addrport, (char *) &self.dhtn_port, 6*sizeof(char));
    addrport[6]='\0';
    SHA1((unsigned char *) addrport, 6*sizeof(char), md);
    self.dhtn_ID = ID(md);
  } else {
    self.dhtn_ID = (unsigned char) id;
  }

  /* inform user which port this node is listening on */
  fprintf(stderr, "DHT node ID %d address is %s:%d\n", self.dhtn_ID, sname, ntohs(self.dhtn_port));

  return;
}


/*
 * Constructor to construct a dhtn node
 */
dhtn::
dhtn(int id, char *cli_fqdn, u_short cli_port, string imagefolder)
{
  fqdn = cli_fqdn;
  port = cli_port;

  listen_sd = DHTN_UNINIT;
  search_sd = DHTN_UNINIT;

  setID(id);
  //set fID table
  for (int i = 0; i < DHTN_FINGERS; ++i) {
    int jump_num = 1 << i;
    fID[i] = (self.dhtn_ID + jump_num)%(NETIMG_IDMAX+1);
  
  }
  //Initialize fingers
  for (int i = 0; i < DHTN_FINGERS+1; ++i)
    fingers[i] = self;
  
  //Set imgdb
  if (imagefolder != "")
    dhtn_imgdb.setfolder(imagefolder);
  dhtn_imgdb.loaddb();
}

/*
 * first: node is the first node in the ID circle.
 * Set both predecssor and successors to be "self".
 */
void dhtn::
first()
{
  for (int i = 0; i < DHTN_FINGERS+1; ++i)
    fingers[i] = self;
}

/*
 * reID: called when the dht tells us that our ID collides
 * with that of an existing node.  We simply closes the listen
 * socket and call setID() to grab a new ephemeral port and
 * a corresponding new ID
*/
void dhtn::
reID()
{
  close(listen_sd);
  setID(((int) NETIMG_IDMAX)+1);

  //Set fID table
  for (int i = 0; i < DHTN_FINGERS; ++i) {
    int jump_num = 1 << i;
    fID[i] = (self.dhtn_ID + jump_num)%(NETIMG_IDMAX+1);
  }
  return;
}

/*
 * connremote: connect to a remote host. If the host's address is not given, assume we want
 * to connect to the known host whose fqdn is stored as a member variable.  The port given
 * must be in network byte order.
 *
 * Upon successful return, return the connected socket.
 */
int dhtn::
connremote(struct in_addr *addr, u_short portnum)
{
  int err, sd;
  struct sockaddr_in remote;
  struct hostent *rp;

  /* create a new TCP socket. */
  sd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
  net_assert((sd < 0), "dhtn::connremote: socket");

  memset((char *) &remote, 0, sizeof(struct sockaddr_in));
  remote.sin_family = AF_INET;
  remote.sin_port = portnum;
  if (addr) {
    memcpy(&remote.sin_addr, addr, sizeof(struct in_addr));
  } else {
    /* obtain the remote host's IPv4 address from fqdn and initialize the
       socket address with remote host's address. */
    rp = gethostbyname(fqdn);
    net_assert((rp == 0), "dhtn::connremote: gethostbyname");
    memcpy(&remote.sin_addr, rp->h_addr, rp->h_length);
  }
  
  /* connect to remote host */
  err = connect(sd, (struct sockaddr *) &remote, sizeof(struct sockaddr_in));
  net_assert(err, "dhtn::connremote: connect");

  return sd;
}

/*
 * join: called ONLY by a node upon start up if fqdn:port is specified
 * in the command line.  It sends a join message to the provided host.
 */
void dhtn::
join()
{
  int sd, err;
  dhtmsg_t dhtmsg;

  sd = connremote(NULL, port);

  /* send join message */
  dhtmsg.dhtm_vers = NETIMG_VERS;
  dhtmsg.dhtm_type = DHTM_JOIN;
  dhtmsg.dhtm_ttl = htons(DHTM_TTL);
  memcpy((char *) &dhtmsg.dhtm_node, (char *) &self, sizeof(dhtnode_t));

  err = send(sd, (char *) &dhtmsg, sizeof(dhtmsg_t), 0);
  net_assert((err != sizeof(dhtmsg_t)), "dhtn::join: send");
  
  close(sd);

  return;
}

/*
 * acceptconn: accept a connection on listen_sd.
 * Set the new socket to linger upon closing.
 * Inform user of connection.
 */
int dhtn::
acceptconn()
{
  int td;
  int err, len;
  struct linger linger_opt;
  struct sockaddr_in sender;
  struct hostent *cp;

  /* accept the new connection. Use the variable "td" to hold the new connected socket. */
  len = sizeof(struct sockaddr_in);
  td = accept(listen_sd, (struct sockaddr *) &sender, (socklen_t *)&len);
  net_assert((td < 0), "dhtn::acceptconn: accept");
  
  /* make the socket wait for NETIMG_LINGER time unit to make sure
     that all data sent has been delivered when closing the socket */
  linger_opt.l_onoff = 1;
  linger_opt.l_linger = NETIMG_LINGER;
  err = setsockopt(td, SOL_SOCKET, SO_LINGER,
                   (char *) &linger_opt, sizeof(struct linger));
  net_assert(err, "dhtn::acceptconn: setsockopt SO_LINGER");
  
  /* inform user of connection */
  cp = gethostbyaddr((char *) &sender.sin_addr, sizeof(struct in_addr), AF_INET);
  fprintf(stderr, "Connected from node %s:%d\n",
          ((cp && cp->h_name) ? cp->h_name : inet_ntoa(sender.sin_addr)),
          ntohs(sender.sin_port));

  return(td);
}

/* forward:
 * forward the message in "dhtmsg" along to the next node.
 */
void dhtn::
forward(unsigned char id, dhtmsg_t *dhtmsg, int size)
{
  //find the forward idx
  unsigned char forward_idx = 0;
  for (int i = 0; i < DHTN_FINGERS; ++i) {
    if (ID_inrange(fID[i], self.dhtn_ID, id))
      forward_idx = i;
    else 
      break;
  }
  int err;
  //Create a larger buffer of dhtsrch_t, notice that if it is a join no need to use the entire
  dhtsrch_t forward_msg;
  //Copy from the dhtmsg wich size, might copy a dhtsrch_t or a dhtmsg_t
  memcpy(&forward_msg, dhtmsg, size);
  //No need to consider if it is a dhtsrch_t or a dhtmsg_t just set flag if inrange
  if (ID_inrange(id, self.dhtn_ID, fingers[forward_idx].dhtn_ID))
    forward_msg.dhts_msg.dhtm_type |= DHTM_ATLOC;
  do {  
    int forward_d = connremote(&fingers[forward_idx].dhtn_addr, fingers[forward_idx].dhtn_port);
    //Forward size of the buffer (might be a dhtsrch_t of a dhtmsg_t)
    err = send(forward_d, (char *)&forward_msg, size, 0);
    net_assert((err != size), "dhtn::forward");
    //REDRT_msg could be either dhtsrch_t or dhtmsg_t 
    dhtsrch_t REDRT_msg;
    err = recv(forward_d, (char *)&REDRT_msg, size, 0);
    close(forward_d);
    if (err != 0) {
      net_assert((REDRT_msg.dhts_msg.dhtm_type != DHTM_REDRT), 
                        "dhtn::forward receive not DHTM_REDRT");
      fingers[forward_idx] = REDRT_msg.dhts_msg.dhtm_node;
      fixdn(forward_idx);
      fixup(forward_idx);
    } else 
      break;
  } while (true);

  return;
}

/*
 * Fix up the finger table to guarantee that with the known nodes, the finger 
 * table entry is proper.
 */
void dhtn::
fixup(int idx)
{
  for (int i = idx+1; i < DHTN_FINGERS; ++i) {
    if (ID_inrange(fID[i], self.dhtn_ID, fingers[idx].dhtn_ID))
      fingers[i] = fingers[idx];
    else
      break;  
  }
}
/*
 * Fix down the finger table.
 */
void dhtn::
fixdn(int idx)
{
  for (int i = idx-1; i >= 0; --i) {
    if (ID_inrange(fingers[idx].dhtn_ID, fID[i], fingers[i].dhtn_ID))
      fingers[i] = fingers[idx];
  }
}


/*
 * handlejoin:
 * "sender" is the node from which you receive a JOIN message.  It may not be the node
 * who initiated the join request.  Close it as soon as possible to prevent deadlock.
 * "dhtmsg" is the join message that contains the dhtnode_t of the node initiating the
 * join attempt (henceforth, the "joining node").
 */
void dhtn::
handlejoin(int sender, dhtmsg_t *dhtmsg)
{
  /* First check if the joining node's ID collides with predecessor or
     self.  If so, send back to joining node a REID message.  See
     dhtn.h for packet format.
  */
  int err;
  unsigned char join_ID = (dhtmsg->dhtm_node).dhtn_ID;
  struct in_addr join_addr = (dhtmsg->dhtm_node).dhtn_addr;
  u_short join_port = (dhtmsg->dhtm_node).dhtn_port;
  if (join_ID == self.dhtn_ID || join_ID == fingers[DHTN_FINGERS].dhtn_ID) {
    close(sender);
    int response_d = connremote(&join_addr, join_port); 
    dhtmsg_t REID_msg;
    REID_msg.dhtm_vers = NETIMG_VERS;
    REID_msg.dhtm_type = DHTM_REID;
    REID_msg.dhtm_node = self;
    err = send(response_d, (char *)&REID_msg, sizeof(dhtmsg_t), 0);    
    net_assert((err != sizeof(dhtmsg_t)), "dhtn::handlejoin: case1 send");
    close(response_d);
    return;  
  } 

  if (ID_inrange(join_ID, fingers[DHTN_FINGERS].dhtn_ID, self.dhtn_ID)) {
    close(sender);
    int response_d = connremote(&join_addr, join_port);
    dhtmsg_t WLCM_msg;
    WLCM_msg.dhtm_vers = NETIMG_VERS;
    WLCM_msg.dhtm_type = DHTM_WLCM;
    WLCM_msg.dhtm_node = self;
    err = send(response_d, (char *)&WLCM_msg, sizeof(dhtmsg_t), 0);
    net_assert((err != sizeof(dhtmsg_t)), "dhtn::handlejoin: case2 send");

    dhtnode_t predecessor = fingers[DHTN_FINGERS];
    err = send(response_d, (char *)&predecessor, sizeof(dhtnode_t), 0);
    net_assert((err != sizeof(dhtnode_t)), "dhtn::handlejoin: case2 send");

    fingers[DHTN_FINGERS] = dhtmsg->dhtm_node;
    fixdn(DHTN_FINGERS);
    //Reload since predeccessor has changed
    dhtn_imgdb.reloaddb(fingers[DHTN_FINGERS].dhtn_ID, self.dhtn_ID);

    if (fingers[0].dhtn_ID == self.dhtn_ID) {
      fingers[0] = dhtmsg->dhtm_node;
      fixup(0);
    }
    close(response_d);
    return;       
  }

  if ((dhtmsg->dhtm_type) & DHTM_ATLOC) {
    sendREDRT(sender, dhtmsg, sizeof(dhtmsg_t));
    return;
  }
  close(sender);
  forward(join_ID, dhtmsg, sizeof(dhtmsg_t));
  return;
}

/*
 * handlesearch
 * handle message with type DHTM_QUERY.
 */
void dhtn::
handlesearch(int sender, dhtsrch_t *dhtsrch)
{
  dhtmsg_t temp_msg = dhtsrch->dhts_msg;
  //The picture ID.
  unsigned char image_ID = dhtsrch->dhts_imgID;
  
  //The originator connection info
  struct in_addr query_addr = temp_msg.dhtm_node.dhtn_addr;
  u_short query_port = temp_msg.dhtm_node.dhtn_port;
  int query_result = dhtn_imgdb.searchdb(dhtsrch->dhts_name);
  
  dhtsrch_t return_msg;
  memcpy(&return_msg, dhtsrch, sizeof(dhtsrch_t));  
  
  int err;
  //Close sender and send the originator DHTM_REPLY
  if (query_result == IMGDB_FOUND) {
    close(sender);
    int response_d = connremote(&query_addr, query_port);
    return_msg.dhts_msg.dhtm_type = DHTM_REPLY;
    err = send(response_d, (char*)&return_msg, sizeof(dhtsrch_t), 0);
    net_assert((err != sizeof(dhtsrch_t)), "dhtn::handlesearch: case1 send");
    close(response_d);
    return;
  } 
  //If in the range but still not found, send DHTM_MISS
  if (ID_inrange(image_ID, fingers[DHTN_FINGERS].dhtn_ID, self.dhtn_ID)) {
    close(sender);
    int response_d = connremote(&query_addr, query_port);
    return_msg.dhts_msg.dhtm_type = DHTM_MISS;
    err = send(response_d, (char*)&return_msg, sizeof(dhtsrch_t), 0);
    net_assert((err != sizeof(dhtsrch_t)), "dhtn::handlesearch: case2 send");
    close(response_d);
    return;
  }

  //If ATLOC is setup in forward, do not close but sendREDRT
  if (temp_msg.dhtm_type & DHTM_ATLOC) {
    sendREDRT(sender, (dhtmsg_t*)dhtsrch, sizeof(dhtsrch_t));
    return;
  }

  //Close and further forward
  close(sender);
  forward(image_ID, (dhtmsg_t*)dhtsrch, sizeof(dhtsrch_t));
  return;
}


/*
 * If DHTM_ATLOC is set but still not in range, sendREDRT.
 */
void dhtn::
sendREDRT(int sender, dhtmsg_t *dhtmsg, int size)
{
  //Create a new dhrsrch message and set type and node.
  dhtsrch_t REDRT_msg;
  memcpy(&REDRT_msg, dhtmsg, size);

  REDRT_msg.dhts_msg.dhtm_vers = NETIMG_VERS;
  REDRT_msg.dhts_msg.dhtm_type = DHTM_REDRT;
  REDRT_msg.dhts_msg.dhtm_node = fingers[DHTN_FINGERS];
  
  //Send back size of the buffer
  int err = send(sender, (char*)&REDRT_msg, size, 0);
  net_assert((err != size), "dhtn::sendREDRT");
  close(sender);
        
}

/*
 * sendimg:
 * First send an imsg_t then send the image to the client.
 */
void dhtn::
sendimg(int found)
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
    imgdsize = dhtn_imgdb.marshall_imsg(&imsg);
    net_assert((imgdsize > (double) LONG_MAX), "dhtn_sendimg: image too large");
    imgsize = (long) imgdsize;

    imsg.im_width = htons(imsg.im_width);
    imsg.im_height = htons(imsg.im_height);
    imsg.im_format = htons(imsg.im_format);
  }
  
  /* 
   * Send the imsg packet to client connected to socket td.
  */
  bytes = send(search_sd, (char *) &imsg, sizeof(imsg_t), 0);
  net_assert((bytes != sizeof(imsg_t)), "dhtn_sendimg: send imsg");

  if (found > 0) {
    segsize = imgsize/NETIMG_NUMSEG;                     /* compute segment size */
    segsize = segsize < NETIMG_MSS ? NETIMG_MSS : segsize; /* but don't let segment be too small*/
    
    ip = dhtn_imgdb.getimage();    /* ip points to the start of byte buffer holding image */
    
    for (left = imgsize; left; left -= bytes) {  // "bytes" contains how many bytes was sent
                                                 // at the last iteration.
      /* Send one segment of data of size segsize at each iteration.
       * The last segment may be smaller than segsize
       */
      bytes = send(search_sd, (char *) ip, segsize > left ? left : segsize, 0);
      net_assert((bytes < 0), "dhtn_sendimg: send image");
      ip += bytes;
      
      fprintf(stderr, "dhtn_sendimg: size %d, sent %d\n", (int) left, bytes);
      usleep(NETIMG_USLEEP);
    }
  }

  return;
}

/*
 * handlepkt: receive and parse packet.
 * The argument "sender" is the socket where the a connection has been established.
 * First receive a packet from the sender.  Then depending on the packet type, call
 * the appropriate packet handler.
 */
void dhtn::
handlepkt(int sender)
{
  int err, bytes;
  dhtmsg_t dhtmsg;

  bytes = 0;
  do {
    /* receive packet from sender */
    err = recv(sender, ((char*)(&dhtmsg)+bytes), sizeof(dhtmsg_t), 0);
    if (err <= 0) { // connection closed or error
      close(sender);
      break;
    } 
    bytes += err;
  } while (bytes < (int) sizeof(dhtmsg_t));
  
  if (bytes == sizeof(dhtmsg_t)) {

    net_assert((dhtmsg.dhtm_vers != NETIMG_VERS), "dhtn::join: bad version");

    if (dhtmsg.dhtm_type == DHTM_REID) {
      /* packet is of type DHTM_REID: an ID collision has occurred and
         we, the newly joining node, has been told to generate a new
         ID. We close the connection to the sender, calls reID(),
         which will close our listening socket and create a new one
         with a new ephemeral port.  We then generta a new ID from the
         new ephemeral port and try to join again. */
      net_assert(!fqdn, "dhtn::handlepkt: received reID but no known node");
      fprintf(stderr, "\tReceived REID from node %d\n", dhtmsg.dhtm_node.dhtn_ID);
      close(sender);
      reID();
      join();

    } else if (dhtmsg.dhtm_type & DHTM_JOIN) {
      net_assert(!(fingers[0].dhtn_port),
                 "dhtn::handlpkt: receive a JOIN when not yet integrated into the DHT.");
      fprintf(stderr, "\tReceived JOIN (%d) from node %d\n",
              ntohs(dhtmsg.dhtm_ttl), dhtmsg.dhtm_node.dhtn_ID);
      handlejoin(sender, &dhtmsg);

    } else if (dhtmsg.dhtm_type & DHTM_WLCM) {


/*For the first join node, the received message contain a node that should be in idx 0
Set all the finger table entries with fingers[0] is between self id and fID[k]  to be
fingers[0] --- guarantee that 
*/

      fprintf(stderr, "\tReceived WLCM from node %d\n", dhtmsg.dhtm_node.dhtn_ID);
      // store successor node
      fingers[0] = dhtmsg.dhtm_node;
      //Fixup here from 0
      fixup(0);
      // store predecessor node
      err = recv(sender, (char *) &fingers[DHTN_FINGERS], sizeof(dhtnode_t), 0);
      net_assert((err <= 0), "dhtn::handlepkt: welcome recv pred");
      //Fixdn here from DHTN_FINGERS
      fixdn(DHTN_FINGERS);
      //Reload here since fingers[DHTN_FINGERS] has been set
      dhtn_imgdb.reloaddb(fingers[DHTN_FINGERS].dhtn_ID, self.dhtn_ID);      

      close(sender);


    } else if (dhtmsg.dhtm_type & DHTM_FIND) {
      //Create a new iqry_t and try to receive the remains. First copy all the contents
      iqry_t iqry_msg;
      memcpy((char* )&iqry_msg, (char* )&dhtmsg, sizeof(dhtmsg_t));
      do {
        /* receive packet from sender */
        err = recv(sender, ((char*)(&iqry_msg)+bytes), sizeof(iqry_t), 0);
        if (err <= 0) { // connection closed or error
          close(sender);
          break;
        } 
        bytes += err;
      } while (bytes < (int) sizeof(iqry_t));

      //After receive all the message, first check if there is a pending send
      //If there is one, directly close the sender
      if (search_sd != DHTN_UNINIT) {
        close(sender);
      }
      //Set search_sd
      search_sd = sender;

      int query_result = dhtn_imgdb.searchdb(iqry_msg.iq_name);
      //Find the image
      if (query_result == IMGDB_FOUND) {
        dhtn_imgdb.readimg(iqry_msg.iq_name);
        sendimg(query_result);
        close(search_sd);
        //Picture process end, reinit search_sd
        search_sd = DHTN_UNINIT;
      } else {
        //Might be a false here, check if it is inrange, if not directly return DHTM_MISS
        unsigned char md[SHA1_MDLEN];
        SHA1((unsigned char *)iqry_msg.iq_name, strlen(iqry_msg.iq_name), md);
        unsigned char id = ID(md);
        if (ID_inrange(id, fingers[DHTN_FINGERS].dhtn_ID, self.dhtn_ID))
          sendimg(IMGDB_MISS);

        else {
          //Create dhtsrch_t
          dhtmsg_t temp_msg;
          temp_msg.dhtm_vers = NETIMG_VERS;
          temp_msg.dhtm_type = DHTM_QUERY;
          temp_msg.dhtm_ttl = htons(DHTM_TTL);
          temp_msg.dhtm_node = self;
          dhtsrch_t dhtsrch_msg;
          dhtsrch_msg.dhts_msg = temp_msg;

          //Set id and query name
          dhtsrch_msg.dhts_imgID = id;
          strcpy(dhtsrch_msg.dhts_name, iqry_msg.iq_name);
          //Forward to the node that should have
          forward(dhtsrch_msg.dhts_imgID, (dhtmsg_t*)&dhtsrch_msg, sizeof(dhtsrch_t));
          //do not reinit search_sd here, 
          //since the search is not end until receive a DHTM_REPLY or DHTM_MISS
        }
      }
    } else if (dhtmsg.dhtm_type & DHTM_QUERY) {
      //Receive the whole message
      dhtsrch_t dhtsrch_msg;
      dhtsrch_msg.dhts_msg = dhtmsg;
      do {
        /* receive packet from sender */
        err = recv(sender, ((char*)(&dhtsrch_msg)+bytes), sizeof(dhtsrch_t), 0);
        if (err <= 0) { // connection closed or error
          close(sender);
          break;
        } 
        bytes += err;
      } while (bytes < (int) sizeof(dhtsrch_t));
    
      handlesearch(sender, &dhtsrch_msg);
    } else if (dhtmsg.dhtm_type == DHTM_REPLY) {
      //Receive the whole message
      dhtsrch_t dhtsrch_msg;
      memcpy(&dhtsrch_msg, &dhtmsg, sizeof(dhtmsg));
      
      do {
        /* receive packet from sender */
        err = recv(sender, ((char*)(&dhtsrch_msg)+bytes), sizeof(dhtsrch_t), 0);
        if (err <= 0) { // connection closed or error
          close(sender);
          break;
        } 
        bytes += err;
      } while (bytes < (int) sizeof(dhtsrch_t));
     
      unsigned char md[SHA1_MDLEN];
      SHA1((unsigned char * ) dhtsrch_msg.dhts_name, strlen(dhtsrch_msg.dhts_name), md);

      unsigned char id = ID(md);
      net_assert((dhtsrch_msg.dhts_imgID != id), "handlekept, id not equal");

      dhtn_imgdb.loadimg(dhtsrch_msg.dhts_imgID, md, dhtsrch_msg.dhts_name);
      dhtn_imgdb.readimg(dhtsrch_msg.dhts_name);
      sendimg(IMGDB_FOUND);
      close(search_sd);
      //Reinit search_sd here
      search_sd = DHTN_UNINIT;      
      close(sender);
    } else if (dhtmsg.dhtm_type == DHTM_MISS) {

      //Receive the whole message
      dhtsrch_t dhtsrch_msg;
      memcpy(&dhtsrch_msg, &dhtmsg, sizeof(dhtmsg));
      
      do {
        /* receive packet from sender */
        err = recv(sender, ((char*)(&dhtsrch_msg)+bytes), sizeof(dhtsrch_t), 0);
        if (err <= 0) { // connection closed or error
          close(sender);
          break;
        } 
        bytes += err;
      } while (bytes < (int) sizeof(dhtsrch_t));
      //Send imsg_t
      sendimg(IMGDB_MISS);
      
      close(search_sd);
      search_sd = DHTN_UNINIT; 
      close(sender);

    } else {
      net_assert((dhtmsg.dhtm_type & DHTM_REDRT),
                 "dhtn::handlepkt: overshoot message received out of band");
      close(sender);
    }

  }


  return;
}
  
/*
 * This is the main loop of the program.  It sets up the read set, calls select,
 * and handles input on the stdin and connection and packet arriving on the listen_sd
 * socket.
 */
int dhtn::
mainloop()
{
  char c;
  fd_set rset;
  int err, sender;
  /* set up and call select */
  FD_ZERO(&rset);
  FD_SET(listen_sd, &rset);
  FD_SET(STDIN_FILENO, &rset); // wait for input from std input,
  // Winsock only works with socket and stdin is not a socket

  err = select(listen_sd+1, &rset, 0, 0, 0);
  net_assert((err <= 0), "dhtn::mainloop: select error");

  if (FD_ISSET(STDIN_FILENO, &rset)) {
    // user input: if getchar() returns EOF or if user hits q, quit,
    // else flush input and go back to waiting
    if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
      fprintf(stderr, "Bye!\n");
      return(0);
    } else if (c == 'p') {
      fprintf(stderr, "Node ID: %d, pred: %d\n", self.dhtn_ID,
              fingers[DHTN_FINGERS].dhtn_ID);
      for (int i = 0; i < DHTN_FINGERS; ++i)
        fprintf(stderr, "successor in entry %d: %d\n", fID[i], fingers[i].dhtn_ID);
    }
    fflush(stdin);
  }

  if (FD_ISSET(listen_sd, &rset)) {
    sender = acceptconn();
    handlepkt(sender);
  }
  
  return(1);
}


int
main(int argc, char *argv[])
{ 
  char *cli_fqdn = NULL;
  u_short cli_port;
  int id;
  string folder_name = "";
  signal(SIGPIPE, SIG_IGN);    /* don't die if peer is dead */

  // parse args, see the comments for dhtn_args()
  if (dhtn_args(argc, argv, &cli_fqdn, &cli_port, &id, folder_name)) {
    dhtn_usage(argv[0]);
  }

//@@@first set imagefolder to null
  dhtn node(id, cli_fqdn, cli_port, folder_name);// initialize node, create listen socket

  if (cli_fqdn) {
    node.join();      // join DHT is known host given
  } else {
    node.first();     // else this is the first node on ID circle
  }
    
  while(node.mainloop());

  exit(0);
}


