#include <stdio.h>         // fprintf(), perror(), fflush()
#include <stdlib.h>        // atoi()
#include <assert.h>        // assert()
#include <string.h>        // memset(), memcmp(), strlen(), strcpy(), memcpy()
#include <unistd.h>        // getopt(), STDIN_FILENO, gethostname()
#include <signal.h>        // signal()
#include <netdb.h>         // gethostbyname(), gethostbyaddr()
#include <netinet/in.h>    // struct in_addr
#include <arpa/inet.h>     // htons(), inet_ntoa()
#include <sys/types.h>     // u_short
#include <sys/socket.h>    // socket API, setsockopt(), getsockname()
#include <sys/select.h>    // select(), FD_*
#include <sys/ioctl.h>
#include <errno.h>
#include <limits.h>
#include <vector>
#include <deque>
#include <set>
#include <iostream>
using namespace std;

#include <GL/glut.h>
#include <GL/gl.h>
#include "ltga.h"


#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define PORTSEP   	':'
#define UNINIT_SD  	-1
#define MAXFQDN   	255
#define QLEN      	10
#define LINGER    	2

#define PM_VERS     0x1			// Message version
#define PM_WELCOME  0x1       	// Welcome peer
#define PM_RDIRECT  0x2       	// Redirect peer
#define PM_SEARCH	0x4			// Search message
#define MAXNAMELEN  255    		// including terminating NULL character
#define QUEUE_LEN 	6			// the length of the circular queue
#define PIC_WIDTH   1280		// width of picture
#define PIC_HEIGHT  800			// height of picture
#define PIC_NUMSEG 	50
#define PIC_MSS    	1440
#define PIC_USLEEP  500000    	// 500 ms
#define WAIT_TIME 	100			// listening wait time 100s


typedef struct {
  unsigned char im_vers;
  unsigned char im_depth;   // in bytes, not in bits as returned by LTGA.GetPixelDepth()
  unsigned short im_format;
  unsigned short im_width;
  unsigned short im_height;
} imsg_t;


// data type
typedef struct {                // peer address structure
    struct in_addr peer_addr;   // IPv4 address
    u_short peer_port;          // port#, always stored in network byte order
    u_short peer_rsvd;          // reserved field
} peer_t;

// the message header
typedef struct {
	char pm_vers, pm_type;
	u_short pm_npeers;
} pmsg_header;

typedef struct {                    // peer table entry
    int pte_sd;                     // socket peer is connected at
    char pte_pname[MAXFQDN+1];      // peer's fqdn
    bool pend;                      // is_pending
    peer_t pte_peer;                // peer's address+port#
} pte_t;

// peer table class
class pt {
    public:
        int table_size;
        pte_t *table_ptr;
        int current_size;
 
        // initializer of the object
        void init(int size);

        // linearly find the first empty place
        pte_t *find_empty();
        
        // check if the table is full
        bool is_full();

        // delete the entry from the table
        void delete_entry(pte_t* temp_entry);

        // check if temp_entry is already in the table, if it is return the pointer
        // if not, return NULL
        pte_t* find_entry(const peer_t* peer_entry);
};


char connect_name[MAXFQDN+1];		// the server FQDN input from command
u_short connect_port = 0;			// the server port input from command
int maxsize = 6;					// the maxsize of table, changed according to input

int maxsd;							// maximum descriptor number
fd_set rset;						// the descriptor set

char host_name[MAXFQDN+1] = {0};	// store the host name

struct sockaddr_in self;			// the address of the message host
int server_sd;						// the listening socket of message

struct sockaddr_in picture_addr;	// the address of the picture host
int pic_sd = UNINIT_SD;				// the listening socket of picture
int pic_td = UNINIT_SD;				// the socket of receiving picture
int pic_rd = UNINIT_SD;				// the socket of sending picture

char hold_file[MAXNAMELEN] = {0};	// hold picture
char search_file[MAXNAMELEN] = {0};	// search picture
bool has_sent = 0;					// if the picture search packet has been sent

deque <int> req_queue;				// the queue of the pic request

pt peer_table;						// the peer table object

// time out for receiving image and corresponding pointer
struct timeval timeout = {WAIT_TIME, 0};
struct timeval* timeout_ptr = &timeout;


int wd;                   			/* GLUT window handle */
GLdouble width, height;   			/* window width and height */

imsg_t recv_imsg;					// global variables for obtaining picture
char *recv_image;
long recv_img_size;
long recv_img_offset;

LTGA send_image;					// global variables for sending picture
imsg_t send_imsg;
long send_img_size;

/* the comparison function of two pee_t type*/
int peer_comp (const peer_t* entry1, const peer_t* entry2)
{
	int result = memcmp((char*)entry1, (char*)entry2, 6);
	return result;
}

/* the order function for set*/
bool peer_set_comp(peer_t peer1, peer_t peer2)
{
	return (peer_comp(&peer1, &peer2) < 0);
}

// the rdirect_set to save all the redirect peers.
bool (*peer_set_comp_ptr) (peer_t, peer_t) = peer_set_comp;
set<peer_t, bool(*)(peer_t, peer_t)> rdirect_set(peer_set_comp_ptr);

/* command line error output function*/
void
search_usage(char *progname) {
    fprintf(stderr, "Usage: %s [ -p peerFQDN:port -n maxsize -q searchFileName -f holdFileName ] \n", progname);
    exit(1);
}

/* obtaining the arguments*/
int
search_args(int argc, char *argv[]) {
    char c, *p;
    extern char *optarg;

    while ((c = getopt(argc, argv, "p:n:f:q:")) != EOF) {
        switch (c) {
        case 'p':
            for (p = optarg+strlen(optarg)-1;     	// point to last character of addr:port arg
                p != optarg && *p != PORTSEP; 		// search for ':' separating addr from port
                 p--);
            net_assert((p == optarg), "peer_args: peer addressed malformed");
            *p++ = '\0';
            connect_port = htons((u_short) atoi(p)); // always stored in network byte order

            net_assert((p-optarg > MAXFQDN), "peer_args: FQDN too long");
            strcpy(connect_name, optarg);
            break;
        case 'n':
            maxsize = atoi(optarg);
            net_assert((maxsize <= 0), "peer_args: maxsize is less than or equal to 0");
            break;
        case 'f':
        	net_assert((strlen(optarg) >= MAXNAMELEN), "netdisp_args: image file name too long");
            strcpy(hold_file, optarg);
            break;
        case 'q':
        	net_assert((strlen(optarg) >= MAXNAMELEN), "netdisp_args: image file name too long");
			strcpy(search_file, optarg);
			break;
        default:
            return(1);
            break;
        }
    }    
  return (0);
}

/* Connect to the listening message server, save the socket and information into pte*/
int
search_connect(pte_t *pte) {

    struct sockaddr_in server;
    int sd;
    if ((sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        abort();
    }
    pte->pte_sd = sd;
    // reuse local address
    int flag = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) < 0)
        abort();

    memset((char *)&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = pte->pte_peer.peer_port;
    server.sin_addr = pte->pte_peer.peer_addr;
   
    if (self.sin_port) {
        if (bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in)) < 0) {
			fprintf(stderr, "Binding error. Error is %d\n", errno);
            abort();
        }
    }

    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0) {
	fprintf(stderr, "Connect failed %s %d\n", pte->pte_pname, ntohs(pte->pte_peer.peer_port)); 
        fprintf(stderr, "Connection error. Error is %d\n", errno);
	abort();
    }  

    return(0);
}

/* Setup a message listening socket according to the port number
 * return the socket number*/
int
search_setup(u_short port) {

    int sd;
    struct sockaddr_in temp_self;
    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        abort();

    memset((char *) &temp_self, 0, sizeof(struct sockaddr_in));
    temp_self.sin_family = AF_INET;
    temp_self.sin_addr.s_addr = INADDR_ANY;
    temp_self.sin_port = port; // in network byte order

    int flag = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag)) < 0)
        abort();
    if (bind(sd, (struct sockaddr *)&temp_self, sizeof(struct sockaddr_in)) < 0)
        abort();
    if (listen(sd, QLEN) < 0)
        abort();
	return (sd);
}

/*Accept a connecting socket and save it into the peer table*/
int
search_accept(pte_t *pte)
{
    struct sockaddr_in peer;
    int td;
    int len = sizeof(struct sockaddr_in);
    td = accept(server_sd, (struct sockaddr *) &peer, (socklen_t *)&len);
    pte->pte_sd = td;
    /* make the socket wait for PR_LINGER time unit to make sure
         that all data sent has been delivered when closing the socket */
    struct linger linger_opt;
    linger_opt.l_onoff = 1;
    linger_opt.l_linger = LINGER;
    if (setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt)))
        abort();
    /* store peer's address+port# in pte */
    memcpy((char *) &pte->pte_peer.peer_addr, (char *) &peer.sin_addr,
            sizeof(struct in_addr));
    pte->pte_peer.peer_port = peer.sin_port; /* stored in network byte order */

    return (pte->pte_sd);
}

/*Accept the incoming socket for sending picture*/
int
pic_accept(int sd)
{
	int td;
	struct sockaddr_in client;
	struct hostent *cp;

	int len = sizeof(struct sockaddr_in);
	td = accept(sd, (struct sockaddr *)&client, (socklen_t *)&len);
	struct linger linger_opt;
	linger_opt.l_onoff = 1;
	linger_opt.l_linger = LINGER;
	if (setsockopt(td, SOL_SOCKET, SO_LINGER, &linger_opt, sizeof(linger_opt)))
		abort();
	 /* inform user of connection */
	cp = gethostbyaddr((char *) &client.sin_addr, sizeof(struct in_addr), AF_INET);
	fprintf(stderr, "Connected from client %s:%d. Ready to obtain picture.\n",
			((cp && cp->h_name) ? cp->h_name : inet_ntoa(client.sin_addr)),
			ntohs(client.sin_port));
	return td;
}

/* send a message of connection information
 * return -1 if there is an error
 * return 0 is succeed*/
int
search_ack(int td, char type, pt peer_table)
{
	int err;
	pmsg_header header;
	header.pm_vers = PM_VERS;
	header.pm_type = type;
	peer_t peer_content[6];

	header.pm_npeers = htons((peer_table.current_size > 6) ? 6 : peer_table.current_size);

	int write_size = 0;
	for (int i = 0; i < peer_table.table_size; i++) {
		if (peer_table.table_ptr[i].pte_sd > 0 && peer_table.table_ptr[i].pend == 0) {
			peer_content[write_size].peer_addr = peer_table.table_ptr[i].pte_peer.peer_addr;
			peer_content[write_size].peer_port = peer_table.table_ptr[i].pte_peer.peer_port;
			write_size++;
		}
		// if the message is full
		if (write_size == 6)
			break;
	}
	err = send(td, &header, sizeof(pmsg_header), 0);
	if (err < (int)sizeof(pmsg_header)) {
		close(td);
		return -1;
	}

	err = send(td, peer_content, sizeof(peer_t)*ntohs(header.pm_npeers), 0);
	if (err < (int)sizeof(peer_t)*ntohs(header.pm_npeers)) {
		close(td);
		return -1;
	}
	return 0;
}


/* send a message of picture query
 * return -1 if error
 * return 0 if succeed*/
int
pic_ack(int td, in_addr address, u_short port, char* file)
{
	if (strlen(file) != 0) {
		int err;
		pmsg_header header;
		header.pm_vers = PM_VERS;
		header.pm_type = PM_SEARCH;
		header.pm_npeers = port;

		err = send(td, &header, sizeof(pmsg_header), 0);
		if (err < (int)sizeof(pmsg_header)) {
			close(td);
			return -1;
		}
		char buffer[MAXNAMELEN+sizeof(in_addr)+sizeof(char)];

		unsigned char length = strlen(file);

		// the actual length that would be sent
		unsigned int send_length = sizeof(in_addr) + sizeof(char) + strlen(file);
		memcpy(buffer, &(address), sizeof(in_addr));
		memcpy(buffer+sizeof(in_addr), &length, sizeof(char));
		memcpy(buffer+sizeof(in_addr)+sizeof(char), file, strlen(file));

		err = send(td, buffer, send_length, 0);
		if (err < (int)send_length) {
			close(td);
		return -1;
		}

	}
	return 1;
}

/* Receive the header from socket td and store into header*/
int
header_recv(int td, pmsg_header* header)
{
	int result = recv(td, (char*)header, sizeof(pmsg_header), 0);
	if(result <= 0) {
		close(td);
		return result;	
	}
	return 1;
}

/* Receive the connection information message content*/
int
content_recv(int td, int peer_number, peer_t* peer_ptr)
{
	unsigned int size = 0;
	while (size < sizeof(peer_t) * peer_number) {
		int result = recv(td, (char*)peer_ptr + size, sizeof(peer_t)*peer_number - size, 0);
		if (result <= 0) {
			close(td);
			return result;
		}
		size += result;
	}
	return 1;
}

/* receive the remained information of a picture query message from td
 * parse the information into address, length and image_name*/
int
pic_package_recv(int td, in_addr* address,unsigned char* length, char* image_name)
{
	int result = recv(td, (char*)address, sizeof(in_addr), 0);
	if (result <= 0) {
		close(td);
		return result;
	}
	result = recv(td, length, sizeof(char), 0);
	if (result <= 0) {
		close(td);
		return result;
	}
	u_short name_length = (u_short)*length;

	int size = 0;
	while (size < name_length) {
		result = recv(td, image_name, name_length, 0);
		if (result <= 0) {
			close(td);
			return result;
		}
		size += result;
	}
	return 1;
}

/* Connect to the server according to the address and port that are listening for the picture
 * save the sending socket into global pic_rd*/
int
pic_connect(in_addr address, u_short port)
{
	struct sockaddr_in server;

	if ((pic_rd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
	    fprintf(stderr, "Send picture socket init error\n");
		abort();
	}

	memset((char * ) &server, 0, sizeof(struct sockaddr_in));
	server.sin_addr = address;
	server.sin_port = port;
	server.sin_family = AF_INET;

	if (connect(pic_rd, (struct sockaddr *) &server, sizeof(struct sockaddr_in)) == -1) {
	    return -1;
	}
	return 0;
}

/* Send the image through the socket pic_rd
 * send the image info first and send the picture then*/
int
pic_imgsend()
{
	int segsize;
	char *ip;
	int bytes;
	long left;

	unsigned int size = 0;
	while(size < sizeof(imsg_t)) {
		int result = send(pic_rd, &send_imsg+size, sizeof(imsg_t)-size, 0);
	    if (result < 0)
	      return result;
	    size += result;
	}
	segsize = send_img_size/PIC_NUMSEG;                     /* compute segment size */
	segsize = segsize < PIC_MSS ? PIC_MSS : segsize; 		/* but don't let segment be too small*/
	ip = (char *) send_image.GetPixels();    				/* ip points to the start of byte buffer holding image */
	for (left = send_img_size; left; left -= bytes) {  		// "bytes" contains how many bytes was sent
                                                			// at the last iteration.
		if (left < segsize)
			bytes = send(pic_rd, ip, left, 0);
		else
			bytes = send(pic_rd, ip, segsize, 0);
		ip += bytes;
		fprintf(stderr, "pic_send: size %d, sent %d\n", (int) left, bytes);
		usleep(PIC_USLEEP);
	}
	return 1;
}

/* Initialize the picture */
void
pic_imginit()
{
	int tod;
	double img_dsize;

	img_dsize = (double) (recv_imsg.im_height*recv_imsg.im_width*(u_short)recv_imsg.im_depth);
	net_assert((img_dsize > (double) LONG_MAX), "pic: image too big");
	recv_img_size = (long) img_dsize;                 // global
	recv_image = (char *)malloc(recv_img_size*sizeof(char));

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();

	glGenTextures(1, (GLuint*) &tod);
	glBindTexture(GL_TEXTURE_2D, tod);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glEnable(GL_TEXTURE_2D);
}

/* Receive the image information message*/
void
pic_recvimsg(int td)
{
	u_short size = 0;
	while (size < sizeof(recv_imsg)) {
		int result = recv(td, (char *)&recv_imsg + size, sizeof(recv_imsg) - size, 0);
		if (result == -1)
			abort();
		size += result;
	}
	recv_imsg.im_width = ntohs(recv_imsg.im_width);
	recv_imsg.im_height = ntohs(recv_imsg.im_height);
	recv_imsg.im_format = ntohs(recv_imsg.im_format);
	if (recv_imsg.im_vers != PM_VERS) {
		exit(1);
	}
	return;
}


void
pic_display(void)
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
pic_reshape(int w, int h)
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
pic_kbd(unsigned char key, int x, int y)
{
	switch((char)key) {
		case 'q':
		case 27:
			glutDestroyWindow(wd);
			exit(0);
			break;
		default:
			break;
	}

	return;
}

int
pic_recvimage(int td)
{
	int bytes = 0;

	if (recv_img_offset < recv_img_size) {
		bytes = recv(td, recv_image + recv_img_offset, recv_img_size - recv_img_offset, 0);
		if (bytes == -1)
			abort();
		recv_img_offset += bytes;
		/* give the updated image to OpenGL for texturing */
		glTexImage2D(GL_TEXTURE_2D, 0, (GLint) recv_imsg.im_format,
					 (GLsizei) recv_imsg.im_width, (GLsizei) recv_imsg.im_height, 0,
					 (GLenum) recv_imsg.im_format, GL_UNSIGNED_BYTE, recv_image);
		/* redisplay */
		glutPostRedisplay();
	}
	return bytes;
}

void
pic_imgload()
{
	int alpha, greyscale;
	double img_dsize;

	send_image.LoadFromFile(hold_file);
	net_assert((!send_image.IsLoaded()), "netdisp_imginit: image not loaded");

	cout << "Image: " << endl;
	cout << "     Type   = " << LImageTypeString[send_image.GetImageType()]
	   << " (" << send_image.GetImageType() << ")" << endl;
	cout << "     Width  = " << send_image.GetImageWidth() << endl;
	cout << "     Height = " << send_image.GetImageHeight() << endl;
	cout << "Pixel depth = " << send_image.GetPixelDepth() << endl;
	cout << "Alpha depth = " << send_image.GetAlphaDepth() << endl;
	cout << "RL encoding  = " << (((int) send_image.GetImageType()) > 8) << endl;
	/* use image->GetPixels()  to obtain the pixel array */

	img_dsize = (double) (send_image.GetImageWidth()*send_image.GetImageHeight()*(send_image.GetPixelDepth()/8));
	net_assert((img_dsize > (double) LONG_MAX), "display: image too big");
	send_img_size = (long) img_dsize;

	send_imsg.im_vers = PM_VERS;
	send_imsg.im_depth = (unsigned char)(send_image.GetPixelDepth()/8);
	send_imsg.im_width = htons(send_image.GetImageWidth());
	send_imsg.im_height = htons(send_image.GetImageHeight());
	alpha = send_image.GetAlphaDepth();
	greyscale = send_image.GetImageType();
	greyscale = (greyscale == 3 || greyscale == 11);
	if (greyscale) {
		send_imsg.im_format = htons(alpha ? GL_LUMINANCE_ALPHA : GL_LUMINANCE);
	} else {
		send_imsg.im_format = htons(alpha ? GL_RGBA : GL_RGB);
	}

	return;
}

/* the select function that run by glutMainLoop*/
void
search_select()
{
	// find the maxsd
	maxsd = -1;
	for (int i = 0; i < peer_table.table_size; i++) {
		if (peer_table.table_ptr[i].pte_sd > maxsd)
			maxsd = peer_table.table_ptr[i].pte_sd;
	}
	if (server_sd > maxsd)
		maxsd = server_sd;

	FD_ZERO(&rset);
	FD_SET(STDIN_FILENO, &rset);
	FD_SET(server_sd, &rset);

	// if the pic_sd is waiting, FD_SET it
	if (pic_sd > 0) {
		if (pic_sd > maxsd)
			maxsd = pic_sd;
		FD_SET(pic_sd, &rset);
	}

	// if the pic_td has set up, FD_SET it
	if (pic_td > 0) {
		if (pic_td > maxsd)
			maxsd = pic_td;
		FD_SET(pic_td, &rset);
	}

	for (int i = 0; i < peer_table.table_size; i++) {
		int td = peer_table.table_ptr[i].pte_sd;
		if (td != UNINIT_SD) {
			FD_SET(td, &rset);
		}
	}

	// wait for activity
	// if the wait time is 0, close listening socket
	if (select(maxsd+1, &rset, NULL, NULL, timeout_ptr) == 0 && pic_sd > 0) {
		fprintf(stderr, "Time is up, stop image listening port\n");		
		close(pic_sd);
		pic_sd = UNINIT_SD;
		timeout_ptr = NULL;
	}

	// user input
	if (FD_ISSET(STDIN_FILENO, &rset)) {
		char c;
		// user input: if getchar() returns EOF or if user hits q, quit,
		// else flush input and go back to waiting
		if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
			fprintf(stderr, "Bye!\n");
			exit(0);
		}
		fflush(stdin);
	}

	// if the picture socket
	if (pic_sd > 0 && FD_ISSET(pic_sd, &rset)) {
		// receive and display and then close the socket
		pic_td = pic_accept(pic_sd);
		close(pic_sd);
		pic_sd = UNINIT_SD;
		// receive imsg_t
		pic_recvimsg(pic_td);
		pic_imginit();
	}

	if (pic_td > 0 && FD_ISSET(pic_td, &rset)) {
		if (!pic_recvimage(pic_td)) {
			close(pic_td);
			pic_td = UNINIT_SD;
		}
	}


	// if server socket
	if (FD_ISSET(server_sd, &rset)) {
		// first accept the incoming socket and store into a temp pte_entry
		pte_t temp_entry;
		search_accept(&temp_entry);
		// first check if the table contains a pending

		pte_t *peer_find = peer_table.find_entry(&(temp_entry.pte_peer));
		if (peer_find != NULL) {
			// we accept temp_entry is smaller than ourself
			// self entry is the peer_t of self address
			peer_t self_entry;
			self_entry.peer_addr = self.sin_addr;
			self_entry.peer_port = self.sin_port;

			if (peer_comp(&temp_entry.pte_peer, &self_entry) < 0) {

				int err = search_ack(temp_entry.pte_sd, PM_WELCOME, peer_table);
				err = (err == -1);
				net_assert(err, "search_ack PM_WELCOME error");

				// no pending anymore
				peer_find->pend = 0;
				// not just change the pend, but also change sd number since it is different
				// with the one that sends the connection request
				peer_find->pte_sd = temp_entry.pte_sd;

				fprintf(stderr, "Connect from peer %s:%d\n",
						peer_find->pte_pname, ntohs(peer_find->pte_peer.peer_port));

				// send the picture acknowledge
				if (pic_sd > 0 && !has_sent) {
					int err = pic_ack(temp_entry.pte_sd, picture_addr.sin_addr, picture_addr.sin_port, search_file);
					err = (err == -1);
					net_assert(err, "pic_ack PM_SEARCH error");
					has_sent = 1;
				}

			} else {

				int err = search_ack(temp_entry.pte_sd, PM_RDIRECT, peer_table);
				err = (err == -1);
				net_assert(err, "search_ack PM_RDIRECT error");

				fprintf(stderr, "Peer table full: %s:%d redirected\n",
						peer_find->pte_pname, ntohs(peer_find->pte_peer.peer_port));

				// close the socket,
				close(temp_entry.pte_sd);

			}

		// if the table does not contain a pending.
		} else {
			// if there is still place for the connect
			if (!peer_table.is_full()) {

				// send acknowledge first before adding to the table
				int err = search_ack(temp_entry.pte_sd, PM_WELCOME, peer_table);
				err = (err == -1);
				net_assert(err, "search_ack PM_WELCOME error");

				// get the empty entry
				pte_t* entry_place = peer_table.find_empty();

				// it sets up pte_sd, pte_peer(peer_port and peer_addr) from temp_entry
				*entry_place = temp_entry;
				// sets pend to 0. since if the server accepts,
				// means there must be some place and the pending is set to 0 immediately
				entry_place->pend = 0;

				struct hostent *phost = gethostbyaddr(
								(char*)&(entry_place->pte_peer.peer_addr),
								sizeof(struct in_addr), AF_INET);

				strcpy(entry_place->pte_pname,
					   ((phost && phost->h_name) ? phost->h_name:
							   inet_ntoa(entry_place->pte_peer.peer_addr)));
				fprintf(stderr, "Connect from peer %s:%d\n",
				entry_place->pte_pname, ntohs(entry_place->pte_peer.peer_port));

				// send the search image
				if (pic_sd > 0 && !has_sent) {
					err = pic_ack(temp_entry.pte_sd, picture_addr.sin_addr, picture_addr.sin_port, search_file);
					err = (err == -1);
					net_assert(err, "pic_ack PM_SEARCH error");
					has_sent = 1;
				}

			} else {
				int err = search_ack(temp_entry.pte_sd, PM_RDIRECT, peer_table);
				err = (err == -1);
				net_assert(err, "search_ack PM_RDIRECT error");

				struct hostent *phost = gethostbyaddr(
							(char*)&(temp_entry.pte_peer.peer_addr),
							sizeof(struct in_addr), AF_INET);
				fprintf(stderr, "Peer table full: %s:%d redirected\n",
						((phost && phost->h_name) ?
						phost->h_name:inet_ntoa(peer_find->pte_peer.peer_addr)),
						ntohs(temp_entry.pte_peer.peer_port));

				// close the socket
				close(temp_entry.pte_sd);
			}
		}
	}

	// check client socket
	for (int i = 0; i < peer_table.table_size; i++) {
		if (peer_table.table_ptr[i].pte_sd > 0 &&
			FD_ISSET(peer_table.table_ptr[i].pte_sd, &rset)) {

			// get a socket number
			// first get the received information
			// check if it is a PM_WELCOME or PM_RDIRECT
			// if it is a PM_RDIRECT, insert into set
			// if it is a PM_WELCOME, set the corresponding entry pending to 0
			// for the other potential connections, for every peer, check set, check if it is in the peer table
			// and then connection. If the table comes to full, just break

			// save the received information
			pmsg_header header;
			int err = header_recv(peer_table.table_ptr[i].pte_sd, &header);
			net_assert((err < 0), "peer: peer_recv");
			if (err == 0) {
				// closed by peer, reset the table entry
				close(peer_table.table_ptr[i].pte_sd);
				peer_table.table_ptr[i].pte_sd = UNINIT_SD;
				continue;

			}
			fprintf(stderr, "Received ack from %s:%d\n",
					peer_table.table_ptr[i].pte_pname,
					ntohs(peer_table.table_ptr[i].pte_peer.peer_port));

			if (header.pm_vers != PM_VERS) {
				fprintf(stderr, "unknown message version.\n");
				continue;
			}
			int peer_number = ntohs(header.pm_npeers);

			if (header.pm_type == PM_WELCOME || header.pm_type == PM_RDIRECT) {

				peer_t* peer_ptr = new peer_t[peer_number];
				err = content_recv(peer_table.table_ptr[i].pte_sd, peer_number, peer_ptr);
				net_assert((err < 0), "peer: peer_recv");
				if (err == 0) {
					// closed by peer, reset the table entry
					close(peer_table.table_ptr[i].pte_sd);
					peer_table.table_ptr[i].pte_sd = UNINIT_SD;
					continue;				
				}
				// if there is connection with that client, try to connect
				if (peer_number > 0) {
					// if message contains a peer address, inform user.
					// print all the peers connected to that one
					for (int j = 0; j < peer_number; j++) {
						struct hostent *phost = gethostbyaddr(
								 (char *) &peer_ptr[j].peer_addr,
								sizeof(struct in_addr), AF_INET);
						fprintf(stderr, "  which is peered with: %s:%d\n",
								((phost && phost->h_name) ? phost->h_name :
								inet_ntoa(peer_ptr[j].peer_addr)),
								ntohs(peer_ptr[j].peer_port));
					}
				}
				// if it is a PM_WELCOME, set pending to 0
				if (header.pm_type == PM_WELCOME) {
					fprintf(stderr,
					"Connected succeed, try to connect to the peers above.\n");
					peer_table.table_ptr[i].pend = 0;

					// send the image search
					if (pic_sd > 0 && !has_sent) {
						int err = pic_ack(peer_table.table_ptr[i].pte_sd, picture_addr.sin_addr, picture_addr.sin_port, search_file);
						err = (err == -1);
						net_assert(err, "pic_ack PM_SEARCH error");
						has_sent = 1;
					}
				}
				// if it is a PM_RDIRECT and the it is not due to simultaneous join
				if (header.pm_type == PM_RDIRECT && peer_table.table_ptr[i].pend == 1) {
					// inform user if message is a redirection
					fprintf(stderr,
					"Join redirected, try to connect to the peers above.\n");
					rdirect_set.insert(peer_table.table_ptr[i].pte_peer);
					peer_table.delete_entry(peer_table.table_ptr + i);
				}

				// try to connect all the peers that connects to that one,
				// as many as possible
				if (peer_number > 0) {
					for (int j = 0; j < peer_number; j++) {
						if (peer_table.is_full()) {
							fprintf(stderr, "Peer table is full, stop join others.\n");
							break;
						}
						// in the rdirect_set
						if (rdirect_set.find(peer_ptr[j]) != rdirect_set.end()) {
							fprintf(stderr,
							"Entry %d has rejected the connection before.\n", j);
							continue;
						}

						if (peer_table.find_entry(peer_ptr + j) != NULL) {
							fprintf(stderr,
							"Entry %d has already been in the peer table\n", j);
							continue;
						}
						pte_t *entry_place = peer_table.find_empty();
						struct hostent *phost = gethostbyaddr(
							  (char *) &peer_ptr[j].peer_addr,
								sizeof(struct in_addr), AF_INET);
						// get the name
						strcpy(entry_place->pte_pname,
							   ((phost && phost->h_name) ? phost->h_name:
								inet_ntoa(peer_ptr[j].peer_addr)));
						// get the address and the port
						entry_place->pend = 1;
						entry_place->pte_peer.peer_addr = peer_ptr[j].peer_addr;
						entry_place->pte_peer.peer_port = peer_ptr[j].peer_port;

						search_connect(entry_place);

						// inform user of connection to peer
						fprintf(stderr, "Connected to peer %s:%d\n",
						  entry_place->pte_pname,
							ntohs(entry_place->pte_peer.peer_port));
					}
				}
				if (peer_ptr != NULL)
					delete[] peer_ptr;

			// PM_SEARCH
			} else {
				in_addr address;
				unsigned char length_net_order;
				char image_name[MAXNAMELEN] = {0};
				pic_package_recv(peer_table.table_ptr[i].pte_sd, &address, &length_net_order, image_name);

				// save into a peer_t type, ready to compare in req_queue
				peer_t current_req_address;
				current_req_address.peer_addr = address;
				current_req_address.peer_port = header.pm_npeers;
				// check if the current peer holds the picture
				if (strcmp(image_name, hold_file) == 0) {
					// holds the picture, try to connect and if the socket is close
					struct hostent *phost = gethostbyaddr(
								 (char *) &address,
								sizeof(struct in_addr), AF_INET);
						fprintf(stderr, "Receive image query from %s:%d\n",
								((phost && phost->h_name) ? phost->h_name :
								inet_ntoa(address)),
								peer_number);


					fprintf(stderr, "Ready to send image.\n");
					if (pic_connect(address, header.pm_npeers) < 0) {
						fprintf(stderr, "Image search has completed, socket closed.\n");
						close(pic_rd);
					} else {
						pic_imgsend();
						close(pic_rd);
						pic_rd = UNINIT_SD;
					}

				// if the current peer does not hold the picture, consider to send to others
				} else {
					char id_buf[264];
					memcpy(id_buf, &header, 4);
					memcpy(id_buf+4, &address, 4);
					memcpy(id_buf+8, &length_net_order, 1);
					memcpy(id_buf+9, image_name, MAXNAMELEN);

					int *num_ptr = (int*)id_buf;
					int id = 0;
					for (int j = 0; j < 66; j++) {
						id += num_ptr[j];
					}
					bool exist_in_queue = 0;
					// check all the entry
					for (int j = 0; j < (int)req_queue.size(); j++) {
						if (req_queue[j] == id) {
							exist_in_queue = 1;
							break;
						}
					}
					// if not exist in the queue
					if (!exist_in_queue) {
						if (req_queue.size() < QUEUE_LEN) {
							req_queue.push_back(id);
						} else {
							req_queue.pop_front();
							req_queue.push_back(id);
						}
					
						// go into every table entry
						for (int j = 0; j < peer_table.table_size; j++) {
							// if the peer is set, pending is 0, and is not the one that sends the request, send ack
							if (peer_table.table_ptr[j].pte_sd > 0 && peer_table.table_ptr[j].pend == 0 &&
										peer_table.table_ptr[j].pte_sd != peer_table.table_ptr[i].pte_sd) {
								int err = pic_ack(peer_table.table_ptr[j].pte_sd, address, header.pm_npeers, image_name);
								err = (err == -1);
								net_assert(err, "pic_ack PM_SEARCH error");
							}
						}
					}
				}
			}
		}
	}
}


void
pic_glutinit(int *argc, char *argv[])
{

	width  = PIC_WIDTH;    /* initial window width and height, */
	height = PIC_HEIGHT;         /* within which we draw. */

	glutInit(argc, argv);
	glutInitDisplayMode(GLUT_SINGLE | GLUT_RGBA);
	glutInitWindowSize((int) PIC_WIDTH, (int) PIC_HEIGHT);
	wd = glutCreateWindow("p2p Display" /* title */ );   // wd global
	glutDisplayFunc(pic_display);
	glutReshapeFunc(pic_reshape);
	glutKeyboardFunc(pic_kbd);
	glutIdleFunc(search_select);

	return;
}

int
main(int argc, char *argv[]) {

    memset (connect_name, 0, sizeof(connect_name));
    memset((char *) &self, 0, sizeof(struct sockaddr_in));
    memset((char *) &picture_addr, 0, sizeof(struct sockaddr_in));

    // get value from the program argument
    if (search_args(argc, argv))
        search_usage(argv[0]);

	if (strlen(hold_file) != 0 && strcmp(hold_file, search_file) == 0) {
		fprintf(stderr, "Already hold the search file, no need to search anymore\n");
		memset(search_file, 0, MAXNAMELEN);
	}

    // construct pte_table according to the maxsize
    peer_table.init(maxsize);

    // don't die if peer is dead
    signal(SIGPIPE, SIG_IGN);

    // if the port number is provided, connect to the peer
    // put into the pending queue and the entry table
    if (strlen(connect_name) != 0) {
        // find the first empty place
        net_assert(peer_table.is_full(), "there is no empty place in the vector");
        pte_t *entry_place = peer_table.find_empty();
        // copy the name
        strcpy(entry_place->pte_pname, connect_name);
        // set the port
        entry_place->pte_peer.peer_port = connect_port;
        entry_place->pend = 1;

        // get the address
        struct hostent *sp;
        sp = gethostbyname(connect_name);
        net_assert(!sp, "the hostname provided cannot be parse");
        memcpy(&(entry_place->pte_peer.peer_addr), sp->h_addr, sp->h_length);

        // connect to the peer
        search_connect(entry_place);

        socklen_t len = sizeof(self);
        if (getsockname(entry_place->pte_sd, (struct sockaddr *)&self, &len) < 0){
			fprintf(stderr, "Get socket name error\n");
            abort();
        }

        // inform user of connection to peer
        fprintf(stderr, "Connected to peer %s:%d\n", entry_place->pte_pname,
            ntohs(entry_place->pte_peer.peer_port));
    }
    // setup and listen on connection
    server_sd = search_setup(self.sin_port);
    if (!self.sin_port) {
        socklen_t len = sizeof(self);
        if (getsockname(server_sd, (struct sockaddr *) &self, &len) < 0)
            abort();
    }

    // get the hostname here
    gethostname(host_name, MAXFQDN+1);
    // inform user which port this peer is listening on
    fprintf(stderr, "This peer address is %s:%d\n",
        host_name, ntohs(self.sin_port));

    // set up listening to picture
    if (strlen(search_file) != 0) {
    	pic_sd = search_setup(0);
    	socklen_t len = sizeof(picture_addr);
    	if (getsockname(pic_sd, (struct sockaddr *) &picture_addr, &len) < 0)
    		abort();
		struct hostent *phost = gethostbyname(host_name);
		memcpy(&(picture_addr.sin_addr), phost->h_addr, phost->h_length);

		fprintf(stderr, "Searching for picture, listening address is %s:%d\n", host_name, ntohs(picture_addr.sin_port));
    }

    if (strlen(hold_file) != 0) {
    	pic_imgload();
    }

    pic_glutinit(&argc, argv);
    glutMainLoop();

}


void pt::init(int size) {
    table_size = size;
    current_size = 0;
    table_ptr = new pte_t[size];
    // initialize the socket number, UNINIT_SD means nothing inside
    for (int i = 0; i < size; i++) {
        table_ptr[i].pte_sd = UNINIT_SD;
        table_ptr[i].pend = 1;
    } 
}

pte_t *pt::find_empty() {
    for (int i = 0; i < table_size; i++) {
        if (table_ptr[i].pte_sd == UNINIT_SD) {
            current_size++;
            return table_ptr + i;
        }
    }
    return NULL;
}

bool pt::is_full() {
    return (current_size == table_size);
}

pte_t* pt::find_entry(const peer_t* peer_entry) {
    
	for (int i = 0; i < table_size; i++) {
		if (peer_comp(&(table_ptr[i].pte_peer), peer_entry) == 0) {
			return table_ptr+i;
		}
	}
	return NULL;
}

void pt::delete_entry(pte_t* temp_entry) {
    temp_entry->pte_sd = UNINIT_SD;
    current_size--;
}
