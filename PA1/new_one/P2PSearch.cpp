/*
    01/24   build data struct, copy from lab2
            build class for peer table and getpeername can be used to check if the connection is
            still working
            everytime for the while loop go and check if there is any socket that is closed, then
            do remove. 
            for automatic redirection, case 1 is easy, just put all the entry in pt to declined
            table. case 2: once receive a PR_RDIRECT, put into declined table
            case 3: go through the pending join to see if there is the same inside
            case 4: the situation is like, two peers send connect request to each other at the
                    same time. how to determine which one accept is the key point 
            pending means you can send connect request to the address(port), when getting result,
            check if you need more to send connect
            once the socket returns message, either accept or redirection. that means the pending 
            comes completed.      -----where queue is needed to keep track
            
            a pending queque is needed
            a hashtable for declined table
            

    01/25   everytime there is a new connection being constructed, either in ISSET(server_socket)
            or ISSET(CLIENT_SOCKET), send all the picture request in the request table
            but if the picture is found, how to tell others not to send the request from me?
            if there is a pending connect, should I count it as a connection, that means should
            I make the counter--? 
            counter-- means I treat the new connection as a new peer, but there is possiblity
            that at last the corresponding socket receive a RDIRCT signal. That means that 
            I ignore a lot of potential connection between connect() and the receive()

            pending_queue is not neccessary, use a bit in pte to record if it is a pending
    
    01/26   for every peer wants to join the peer net, it keeps joining the peers they know
            until: 1. peer table is full
                   2. all the message back are RDIRECT and the peer that it connects to are in
                      the declined table.
            that means stop to join and start to search for picture

    01/29   could i use socket number to denote the different entry???
    01/30   it seems that user can type in 0 as table size???
            because a peer is a server and a client at the same time, we could not send 
            the image search message multiple times
            if it is a pure server(the first one), send after accept the first peer
            if it is a client, don't do send in server sd part.

*/

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

#include <errno.h>

#include <vector>
#include <set>
#include <iostream>
using namespace std;


#define net_assert(err, errmsg) { if ((err)) { perror(errmsg); assert(!(err)); } }

#define PORTSEP   ':'
#define UNINIT_SD  -1
#define MAXFQDN   255
#define QLEN      10
#define LINGER    2

#define PM_VERS      0x1
#define PM_WELCOME   0x1       // Welcome peer
#define PM_RDIRECT   0x2       // Redirect per


// data type
typedef struct {                // peer address structure
    struct in_addr peer_addr;   // IPv4 address
    u_short peer_port;          // port#, always stored in network byte order
    u_short peer_rsvd;          // reserved field
} peer_t;

// Message format:                  	8 bit  8 bit     16 bit
typedef struct {                	// +------+------+-------------+
    char pm_vers, pm_type;      	// | vers | type |   #peers    |
    u_short pm_npeers;          	// +------+------+-------------+
    peer_t pm_peer[6];             	// |     peer ipv4 address     |
} pmsg_t;                       	// +---------------------------+
                                	// |  peer port# |   reserved  |
                                	// +---------------------------+

typedef struct {                    // peer table entry
    int pte_sd;                     // socket peer is connected at
    char pte_pname[MAXFQDN+1];      // peer's fqdn
    bool pend;                      // is_pending
    peer_t pte_peer;                // peer's address+port#
} pte_t;


class pt {
    public:
        int table_size;
        pte_t *table_ptr;
        int current_size;
 
        // constructor 
        pt(int size);

        // linearly find the first empty place
        pte_t *find_empty();
        
        // check if the table is full
        bool is_full();

        void delete_entry(pte_t* temp_entry);

        // check if temp_entry is already in the table, if it is return the pointer
        // if not, return NULL
        pte_t* find_entry(const peer_t* peer_entry);

};
/*
// entry in pending queue
typedef struct {
    char pending_name[MAXFQDN+1];   //FQDN of the peer
    int pending_port;               //port number of the peer, ntohs()
} pending_entry;


// the pending queue class
class pending_queue_t {
    public:
        vector<pending_entry> queue;
        
        // add new connection to pending
        void add_entry(pending_entry new_entry);
} pending_queue;

*/
void
search_usage(char *progname) {
    fprintf(stderr, "Usage: %s [ -p peerFQDN:port -n maxsize ] \n", progname);
    exit(1);
}


int
search_args(int argc, char *argv[], char *pname, u_short *port, int *maxsize) {
    char c, *p;
    extern char *optarg;

    net_assert(!pname, "peer_args: pname not allocated");
    net_assert(!port, "peer_args: port not allocated");

    while ((c = getopt(argc, argv, "p:n:")) != EOF) {
        switch (c) {
        case 'p':
            for (p = optarg+strlen(optarg)-1;     // point to last character of addr:port arg
                p != optarg && *p != PORTSEP; // search for ':' separating addr from port
                 p--);
            net_assert((p == optarg), "peer_args: peer addressed malformed");
            *p++ = '\0';
            *port = htons((u_short) atoi(p)); // always stored in network byte order

            net_assert((p-optarg > MAXFQDN), "peer_args: FQDN too long");
            strcpy(pname, optarg);
            break;
        case 'n':
            *maxsize = atoi(optarg);
            net_assert((*maxsize <= 0), "peer_args: maxsize is less than or equal to 0");
            break;
        default:
            return(1);
            break;
        }
    }    
  

  return (0);

}

int
search_connect(pte_t *pte, sockaddr_in self) {

    // create a new TCP socket, store the socket in the pte
    struct sockaddr_in server;
    int sd;
    if ((sd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0){
        abort();
    }
    pte->pte_sd = sd;


    // reuse local address
    int flag = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
        abort();
    // initialize socket address with destination peer's IPv4 address and port number
    memset((char *)&server, 0, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = pte->pte_peer.peer_port;
    server.sin_addr = pte->pte_peer.peer_addr;
    // deep or shallow
   
    cerr << ntohs(self.sin_port) << endl; 
    if (self.sin_port) {
        if (bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in)) < 0) {
            cerr << "error is: " << errno << endl;
            abort();
        }
    }

    // connect to destination peer
    if (connect(sd, (struct sockaddr *)&server, sizeof(struct sockaddr_in)) < 0) {
        cerr << "Connect failed: " << pte->pte_pname << " " << pte->pte_peer.peer_port << endl; 
        abort();
    }  

    return(0);
}

int
search_setup(u_short port) {

    // create a TCP socket, store the socket descriptor in "sd"
    int sd;
    struct sockaddr_in self;
    if ((sd = socket(PF_INET, SOCK_STREAM, 0)) < 0)
        abort();

    // initialize socket address
    memset((char *) &self, 0, sizeof(struct sockaddr_in));
    self.sin_family = AF_INET;
    self.sin_addr.s_addr = INADDR_ANY;
    self.sin_port = port; // in network byte order

    // reuse local address so that bind doesn't complain of address already in use.
    int flag = 1;
    if (setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag)) < 0)
        abort();
    // bind address to socket
    if (bind(sd, (struct sockaddr *)&self, sizeof(struct sockaddr_in)) < 0)
        abort();
    // listen on socket
    if (listen(sd, QLEN) < 0)
        abort();
    // return socket id
        return (sd);
}



int
search_accept(int sd, pte_t *pte)
{
    struct sockaddr_in peer;

    int td;
    int len = sizeof(struct sockaddr_in);
    td = accept(sd, (struct sockaddr *) &peer, (socklen_t *)&len);
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


int
search_ack(int td, char type, pt peer_table)
{
	int err;
	// create a new message value
	pmsg_t msg;
	msg.pm_vers = PM_VERS;
	msg.pm_type = type;

    cerr << "current size is: " << peer_table.current_size << endl;

	// if the type is PM_WELCOME or PM_RDIRECT, we send the list of connections
	if (type == PM_WELCOME || type == PM_RDIRECT) {
		// set size
		// if more than 6, set 6, and send the first 6
		msg.pm_npeers = (peer_table.current_size > 6) ? 6 : peer_table.current_size;

		int write_size = 0;
		for (int i = 0; i < peer_table.table_size; i++) {
			if (peer_table.table_ptr[i].pte_sd > 0 && peer_table.table_ptr[i].pend == 0) {
				msg.pm_peer[write_size].peer_addr = peer_table.table_ptr[i].pte_peer.peer_addr;
				msg.pm_peer[write_size].peer_port = peer_table.table_ptr[i].pte_peer.peer_port;
				write_size++;
			}
			// if the message is full
			if (write_size == 6)
				break;
		}
	// else send image message
	} else {

	}
	pmsg_t *msg_ptr = &msg;
	err = send(td, msg_ptr, sizeof(pmsg_t), 0);
	if (err < (int)sizeof(pmsg_t))
		close(td);

	return err;
}


int
search_recv(int td, pmsg_t *msg)
{
	unsigned int size = 0;
	while (size < sizeof(pmsg_t)) {
	int result = recv(td, (char*)msg+size, sizeof(pmsg_t)-size, 0);
		if (result <= 0) {
			close(td);
			return result;
	    }
	    size += result;
	    // check result = 0????
	}
	return (sizeof(pmsg_t));
}


int peer_comp (const peer_t* entry1, const peer_t* entry2)
{
	int result = memcmp((char*)entry1, (char*)entry2, 6);
	return result;
}

bool peer_set_comp(peer_t peer1, peer_t peer2)
{
	return (peer_comp(&peer1, &peer2) < 0);
}


int
main(int argc, char *argv[]) {
    
    // create a temporary char[] to record the initial result
    char connect_name[MAXFQDN+1];
    memset (connect_name, 0, sizeof(connect_name));
    u_short connect_port = 0;
    int maxsize = 6;
    
    // the server socket
    int sd;
    // maximum descriptor
    int maxsd;
   
    // the descriptor set 
    fd_set rset;

    // store the hostname
    char host_name[MAXFQDN+1];
    memset (host_name, 0, sizeof(connect_name));

    // the address of the current host and initialize
    struct sockaddr_in self;
    memset((char *) &self, 0, sizeof(struct sockaddr_in));


    // the rdirect_set to save all the redirect peers.
    bool (*peer_set_comp_ptr) (peer_t, peer_t) = peer_set_comp;
    set<peer_t, bool(*)(peer_t, peer_t)> rdirect_set(peer_set_comp_ptr);


    // get value from the program argument
    if (search_args(argc, argv, connect_name, &connect_port, &maxsize))
        search_usage(argv[0]);
    
    // construct pte_table according to the maxsize
    pt peer_table(maxsize);
    
 //   cout << init_connect << endl;
 //   cout << init_port << endl;
 //   cout << maxsize << endl;    
    
    // don't die if peer is dead
    signal(SIGPIPE, SIG_IGN); 

    // if the port number is provided, connect to the peer
    // put into the pending queue and the entry table 
    if (strlen(connect_name) != 0) {
//cerr << "name provided" << endl;
        
        // find the first empty place
        net_assert(peer_table.is_full(), "there is no empty place in the vector");
        pte_t *entry_place = peer_table.find_empty();
        // copy the name
        strcpy(entry_place->pte_pname, connect_name);
        // set the port
        entry_place->pte_peer.peer_port = connect_port;
        entry_place->pend = 1;


//cerr << entry_place->pte_pname << endl;
//cerr << ntohs(entry_place->pte_peer.peer_port) << endl;

        // get the address
        struct hostent *sp;
        sp = gethostbyname(connect_name);
//cerr << "sp is null: " << (sp == NULL) << endl;
        net_assert(!sp, "the hostname provided cannot be parse");
        memcpy(&(entry_place->pte_peer.peer_addr), sp->h_addr, sp->h_length);


        // connect to the peer
        search_connect(entry_place, self);

        socklen_t len = sizeof(self);
        if (getsockname(entry_place->pte_sd, (struct sockaddr *)&self, &len) < 0){
            cerr << "getsockname error" << endl;
            abort();
        }
        
        // inform user of connection to peer
        fprintf(stderr, "Connected to peer %s:%d\n", entry_place->pte_pname,
            ntohs(entry_place->pte_peer.peer_port));
    }
    
    // setup and listen on connection
    sd = search_setup(self.sin_port);
    if (!self.sin_port) {
        socklen_t len = sizeof(self);
        if (getsockname(sd, (struct sockaddr *) &self, &len) < 0) 
            abort();
    }

    // get the hostname here
    gethostname(host_name, MAXFQDN+1);
    // inform user wihch port this peer is listening on
    fprintf(stderr, "This peer address is %s:%d\n", 
        host_name, ntohs(self.sin_port));
    
    
    // start to wait for descriptor by select
    do {
        // find the maxsd
        maxsd = -1;
        for (int i = 0; i < peer_table.table_size; i++) {
        	if (peer_table.table_ptr[i].pte_sd > maxsd)
        		maxsd = peer_table.table_ptr[i].pte_sd;
        }
        if (sd > maxsd)
        	maxsd = sd;
        
        FD_ZERO(&rset);
        FD_SET(STDIN_FILENO, &rset);
        FD_SET(sd, &rset);
        for (int i = 0; i < peer_table.table_size; i++) {
            int td = peer_table.table_ptr[i].pte_sd;
            if (td != UNINIT_SD) {
                FD_SET(td, &rset);
            }
        }

        // wait for activity
        select(maxsd+1, &rset, NULL, NULL, NULL);
        
        // user input
        if (FD_ISSET(STDIN_FILENO, &rset)) {
        	char c;
            // user input: if getchar() returns EOF or if user hits q, quit,
            // else flush input and go back to waiting
            if (((c = getchar()) == EOF) || (c == 'q') || (c == 'Q')) {
                fprintf(stderr, "Bye!\n");
                break;
            }
            fflush(stdin);
        }

        // if server socket
        if (FD_ISSET(sd, &rset)) {

        	// first accept the incoming socket and store into a temp pte_entry
        	pte_t temp_entry;
        	search_accept(sd, &temp_entry);

        	// first check if the table contains a pending
cerr << "server socket is set" << endl;

        	pte_t *peer_find = peer_table.find_entry(&(temp_entry.pte_peer));
        	if (peer_find != NULL) {
            
cerr << "peer_find != NULL" << endl;
        		// we accept temp_entry is smaller than ourself
        		// self entry is the peer_t of self address
        		 peer_t self_entry;
        		 self_entry.peer_addr = self.sin_addr;
        		 self_entry.peer_port = self.sin_port;

        		if (peer_comp(&temp_entry.pte_peer, &self_entry) < 0) {

cerr << "not null but accept" << endl;
                    
        			int err = search_ack(temp_entry.pte_sd, PM_WELCOME, peer_table);

        			err = (err != sizeof(pmsg_t));
        			net_assert(err, "search_ack PM_WELCOME error");

        			// no pending anymore
        			peer_find->pend = 0;
                    // not just change the pend, but also change sd number since it is different
                    // with the one that sends the connection request
                    peer_find->pte_sd = temp_entry.pte_sd;

/*
        			struct hostent *phost = gethostbyaddr((char*)&(peer_find->pte_peer.peer_addr),
        													sizeof(struct in_addr), AF_INET);
        			strcpy(peer_find->pte_pname,
        			               ((phost && phost->h_name) ? phost->h_name:
        			                inet_ntoa(peer_find->pte_peer.peer_addr)));
*/
                    net_assert(strlen(peer_find->pte_pname) == 0, 
                            "peer_find != NULL, accept.the name is empty");

        			fprintf(stderr, "Connect from peer %s:%d\n", 
                            peer_find->pte_pname, ntohs(peer_find->pte_peer.peer_port));

        		} else {
cerr << "not null and redirect" << endl;
/*
                    cerr << "not null and redirect" << endl;
        			// we send rdirect and delete the current entry,
        			//we know definitely that on the other side, our connection would be accepted
        			int err = search_ack(temp_entry.pte_sd, PM_RDIRECT, peer_table);
        			err = (err != sizeof(pmsg_t));
        			net_assert(err, "search_ack PM_RDIRECT error");

        			struct hostent *phost = gethostbyaddr(
                            (char*)&(peer_find->pte_peer.peer_addr),
							sizeof(struct in_addr), AF_INET);
					strcpy(peer_find->pte_pname,
							((phost && phost->h_name) ? phost->h_name:
							inet_ntoa(peer_find->pte_peer.peer_addr)));

                     net_assert(strlen(peer_find->pte_pname) == 0, 
                            "peer_find != NULL, redirect.the name is empty");

					fprintf(stderr, "Peer table full: %s:%d redirected\n", 
                            peer_find->pte_pname, ntohs(peer_find->pte_peer.peer_port));

					// delete the entry
					peer_table.delete_entry(peer_find);
*/
					// close the socket, 
        			close(temp_entry.pte_sd);

        		}

        	// if the table does not contain a pending.
            // peer_find == NULL
        	} else {
cerr << "peer_find == NULL" << endl;
        		// if there is still place for the connect
        		if (!peer_table.is_full()) {
                                        
cerr << "peer_table is not full" << endl;
                    
                    // send acknowledge first before adding to the table
                    int err = search_ack(temp_entry.pte_sd, PM_WELCOME, peer_table);
					err = (err != sizeof(pmsg_t));
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


        		} else {
        			int err = search_ack(temp_entry.pte_sd, PM_RDIRECT, peer_table);
					err = (err != sizeof(pmsg_t));
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
        		pmsg_t msg;
        		int err = search_recv(peer_table.table_ptr[i].pte_sd, &msg);
        		net_assert((err < 0), "peer: peer_recv");

        		if (err == 0) {
        			// closed by peer, reset the table entry
        			close(peer_table.table_ptr[i].pte_sd);
        			peer_table.table_ptr[i].pte_sd = UNINIT_SD;
        		} else {
        			fprintf(stderr, "Received ack from %s:%d\n", 
                            peer_table.table_ptr[i].pte_pname,
        			        ntohs(peer_table.table_ptr[i].pte_peer.peer_port));

        			if (msg.pm_vers != PM_VERS) {
        				fprintf(stderr, "unknown message version.\n");
        			} else {
        				// if there is connection with that client, try to connect
						if (msg.pm_npeers > 0) {
							// if message contains a peer address, inform user.
							// print all the peers connected to that one
							for (int j = 0; j < msg.pm_npeers; j++) {
								struct hostent *phost = gethostbyaddr(
                                                (char *) &msg.pm_peer[j].peer_addr,
												sizeof(struct in_addr), AF_INET);
								fprintf(stderr, "  which is peered with: %s:%d\n",
										((phost && phost->h_name) ? phost->h_name :
												inet_ntoa(msg.pm_peer[j].peer_addr)),
												ntohs(msg.pm_peer[j].peer_port));
							}
						}
						// if it is a PM_WELCOME, set pending to 0
						if (msg.pm_type == PM_WELCOME) {
							fprintf(stderr, 
                                "Connected succeed, try to connect to the peers above.\n");
							peer_table.table_ptr[i].pend = 0;
						}

						if (msg.pm_type == PM_RDIRECT) {
						    // inform user if message is a redirection
							fprintf(stderr, 
                                "Join redirected, try to connect to the peers above.\n");
                            rdirect_set.insert(peer_table.table_ptr[i].pte_peer);    
						    peer_table.delete_entry(peer_table.table_ptr + i);
                            
						}

						// try to connect all the peers that connects to that one, 
                        //as many as possible
						if (msg.pm_npeers > 0) {
							for (int j = 0; j < msg.pm_npeers; j++) {
								if (peer_table.is_full()) {
                                    fprint(stderr, "Peer table is full, stop join others.\n");
									break;
                                }
								// in the rdirect_set
								if (rdirect_set.find(msg.pm_peer[j]) != rdirect_set.end()) {
									fprintf(stderr, 
                                    "Entry %d has rejected the connection before.\n", j);
									continue;
								}

								if (peer_table.find_entry(msg.pm_peer + j) != NULL) {
									fprintf(stderr, 
                                    "Entry %d has already been in the peer table\n", j);
									continue;
								}
cerr << "try to join the others, port number in self: " << ntohs(self.sin_port) << endl;
								pte_t *entry_place = peer_table.find_empty();
								struct hostent *phost = gethostbyaddr(
                                        (char *) &msg.pm_peer[j].peer_addr,
										sizeof(struct in_addr), AF_INET);
								// get the name
								strcpy(entry_place->pte_pname,
									   ((phost && phost->h_name) ? phost->h_name:
										inet_ntoa(msg.pm_peer[j].peer_addr)));
								// get the address and the port
								entry_place->pend = 1;
								entry_place->pte_peer.peer_addr = msg.pm_peer[j].peer_addr;
								    entry_place->pte_peer.peer_port = msg.pm_peer[j].peer_port;

								search_connect(entry_place, self);

								// inform user of connection to peer
								fprintf(stderr, "Connected to peer %s:%d\n", 
                                    entry_place->pte_pname,
									ntohs(entry_place->pte_peer.peer_port));
							}
						}
        			}
        		}
        	}
        }


    } while(1);


}


// pt constructor
pt::pt(int size) {
    table_size = size;
    current_size = 0;
    table_ptr = new pte_t[size];
    // initialize the socket number, UNINIT_SD means nothing inside
    for (int i = 0; i < size; i++) {
        table_ptr[i].pte_sd = UNINIT_SD;
        table_ptr[i].pend = 1;
    } 
}


// find an empty entry for the new table entry
// when found, increment current_size
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



/*
void pending_queue_t::add_entry(pending_entry new_entry) {
    queue.push_back(new_entry);
}
*/



