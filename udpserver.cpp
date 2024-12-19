#include <iostream>
#include <sys/types.h>    // socket, bind
#include <sys/socket.h>   // socket, bind, listen, inet_ntoa
#include <netdb.h>        // gethostbyname
#include <unistd.h>       // read, write, close, sleep
#include <strings.h>      // bzero
#include <netinet/tcp.h>  // SO_REUSEADDR
#include <errno.h>        // errno
#include <cerrno>
#include <cstring>        // for strerror
#include <string.h>       // for memset
#include <filesystem>     // for exists
#include <fstream>        // for file stuff
#include <sstream>        // for stringstream stuff
#include <vector>         // for vectors, duh
#include <signal.h>       // for the shutdown signal
#include <fcntl.h>        // for fcntl -- to set non-blocking
#include <random>         // for random #

using namespace std;

// the repetition of sending the data within the buffers
#define REPETITION 20000
// msg byte size
#define MSG_BYTES 1460
// the read buffer size based on size of type used in the array -- int in this case
#define BUFFER_SIZE (1460/sizeof(int))
// port to use
#define PORT "2087"

// flag for shutting the server down -- ends all while loops - would probably be best to find a way to end for loops too
volatile sig_atomic_t shutdown_flag = 0;

// handles the shut down of the server
void signalHandler(int signum) {
    // cerr << "Server received shutdown signal: " << signum << ". Initiating clean shutdown..." << endl;
    shutdown_flag = 1;
}

// handle making the socket struct for listening -- makes a UDP socket
// can later add in params to change the family and socktype and optional flags and port #
struct addrinfo* makeGetaddrinfo(){
    // for checking the return of getaddrinfo
    int status;
    // holds the info for the server address
    struct addrinfo server_addr;
    // points to the results that are in a linked list - is returned
    struct addrinfo *servinfo; 
    
    // create the struct and address info
    // make sure the struct is empty
    memset(&server_addr, 0, sizeof(server_addr));
    // doesn't matter if its ipv4 or ipv6
    server_addr.ai_family = AF_UNSPEC;
    // tcp stream sockets
    server_addr.ai_socktype = SOCK_DGRAM;
    // fill in my IP for me 
    server_addr.ai_flags = AI_PASSIVE;

    // getaddrinfo and error check in one -- doesn't need an IP/host because this is for listening
    if ((status = getaddrinfo(NULL, PORT, &server_addr, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    
    return servinfo;
}

// make the listening socket, make it non-blocking, and do error checks, and return the Sd
int makeListeningSocket(struct addrinfo* servinfo){
    // open a stream-oriented socket with the internet address family
    int serverSd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
    // check if the socket call had an error
    if (serverSd == -1) {
        cerr << "error making the socket: serverSd - " << serverSd << endl;
    }

    // get the current flags
    int flags = fcntl(serverSd, F_GETFL, 0);
    // turn on the non-blocking flag
    fcntl(serverSd, F_SETFL, flags | O_NONBLOCK); 

    return serverSd;
}

// set the socket resue function to help free up unused sockets and ports
void setSocketReuse(int serverSd){
    // Enable socket reuse without waiting for the OS to recycle it
    // set the so-reuseaddr option
    const int on = 1;
    int success = setsockopt(serverSd, SOL_SOCKET, SO_REUSEADDR, (char *)&on, sizeof(int));
    // check if the option call failed
    if (success == -1) {
        cerr << "Error setting the socket reuse option: serverSd - " << serverSd << endl;
    }
}

// bind the socket
void bindSocket(int serverSd, struct addrinfo* servinfo){
    // Bind the socket to the port we passed into getaddrinfo
    int binding = bind(serverSd, servinfo->ai_addr, servinfo->ai_addrlen);
    // check if the bind had an error
    if (binding == -1) {
        cerr << "Error binding socket: serverSd - " << serverSd << " to port: " << PORT << endl;
    }
}

// read the msg from the client and feed into the buffer ie: message -- for UDP
int readMsg(int sd, struct addrinfo* servinfo){
    int messageBuf[BUFFER_SIZE] = {0};
    // recieve the message into the msg[] array and make sure it was read correctly
            int nRead = recvfrom(sd, &messageBuf, BUFFER_SIZE, 0, servinfo->ai_addr, &(servinfo->ai_addrlen));
            if (nRead == -1){
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // do nothing simply nothing to read yet
                    return -1;
                } else {
                    cerr << "Error reading from socket: SD = " << sd << endl; 
                    return -2;
                }
            } else if (nRead == 0) {
                cerr << "Client closed the connection" << endl;
                return -3;
            }
            return messageBuf[0];
    return -4;
}

// send a simple UDP int msg and
void sendMsg(int sd, int message, struct addrinfo *servinfo){
    int bytes_sent = sendto(sd, &message, sizeof(int), 0, servinfo->ai_addr, servinfo->ai_addrlen);
    if (bytes_sent == -1){
        cerr << "ERROR: Problem with simple send" << endl;
    }
}

// close the socket and check for errors
void closeSocket(int sd){
    int bye = close(sd);
    if (bye == -1){
        cerr << "ERROR: closing socket" << endl;
    }
}

bool validTestNum(int testNum){
    if (testNum < 0 || testNum > 3){
        cerr << "ERROR: Unknown testNum: " << testNum << endl;
        return false;
    }
    return true;
}

// just read messaages
void serverUnreliable(const int sd, int message[], struct addrinfo* servinfo){
    // loop through the reciving and reading of the data
    while (!shutdown_flag) {
        readMsg(sd, servinfo);
    }
}

// get a random number using the new random function in c++, the high end of the range should be 2 * (windowSize + 1)
int getRandNum(){
    //Will be used to obtain a seed for the random number engine
    random_device rd; 
    //Standard mersenne_twister_engine seeded with rd() 
    mt19937 gen(rd());
    // set up the range
    uniform_int_distribution<> randomInt(1, 100);    
    // pick a random value between 0 and 100
    int dropVal = randomInt(gen);
    // return the random #
    return dropVal;
}

// send acks back
void serverReliable(const int sd, int message[], struct addrinfo* servinfo){
    // loop through the reciving and reading of the data
    while (!shutdown_flag) {
        int ackNum = readMsg(sd, servinfo);
        // only bother sending valid acks
        if (ackNum > 0) {
            // randomly drop packets 1% of the time
            if (getRandNum() != 1){
                sendMsg(sd, ackNum, servinfo);
            }
        }
    }
} 

// send cumulative acks, simulate 1% chance packet drops
void serverEarlyRetrans(const int sd, int message[], int windowSize, struct addrinfo* servinfo){
    // to see if packets are recieved in order
    int expectedSeqNum = 1;
    // highest # of packets acked
    int lastAcked = 0;

    // loop through the reciving and reading of the data
    while (!shutdown_flag) {
        // read msg and get ackNum
        int ackNum = readMsg(sd, servinfo);
        if (ackNum == expectedSeqNum){
            int randNum = getRandNum();
            // randomly drop packets 1% of the time
            if (randNum != 1){
                // save seq # in the array -- as in the instructions -- not actually used in this version
                message[ackNum] = ackNum;
                // set lastAcked
                lastAcked = ackNum;
                // start at the expectedSeqNum and check if there are any to the right not empty
                for (int i = expectedSeqNum + 1; i < REPETITION; i++){
                    if (message[i] != 0){
                        // update max # acked
                        lastAcked = i;
                        expectedSeqNum = lastAcked++;
                    } else {
                        break;
                    }
                }
                sendMsg(sd, lastAcked, servinfo);
                // update next expected
                expectedSeqNum++;
            }
        } else if (ackNum > expectedSeqNum){
//            if (getRandNum() != 1){
                // store the out of order msg seqNum
                message[ackNum] = ackNum;
                // resend last acked -- useful for a more efficent version where the client looks for triple duplicate acks
            //    sendMsg(sd, lastAcked, servinfo);
//            }
        }
    }
} 

void pickResponseMode(int testNum, int serverSd, struct addrinfo* servinfo, int windowSize){
    // make the buffer for storing the data using pointers inside readMsg, set all to 0
    int msg[REPETITION] = {0};
    // check which test is called
    if (testNum == 1){
        serverUnreliable(serverSd, msg, servinfo);
    } else if (testNum == 2){
        serverReliable(serverSd, msg, servinfo);
    } else {
        serverEarlyRetrans(serverSd, msg, windowSize, servinfo);
    }
}

int main (int argc, char* argv[]) {
    // check that the command line has the right # of params
    if (argc < 3){
        cerr << "Error: Not enough parameters passed in. Usage: " << argv[0] << " <testNumber> <windowSize>\n";
        return 1;
    }
    // params passed in through command line
    int testNum = atoi(argv[1]);
    int windowSize = atoi(argv[2]);
    // check if bad testNum was passed in
    validTestNum(testNum);

    // Set up signal handling for SIGINT and SIGTERM so that the server can shut down properly
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    // make the structs needed for the sockets
    struct addrinfo* servinfo = makeGetaddrinfo();

    // Create the listening socket on serverSd
    int serverSd = makeListeningSocket(servinfo);
    
    // set resue of socket
    setSocketReuse(serverSd);

    // bind socket
    bindSocket(serverSd, servinfo);

    // call the right func to read msgs and send the acks
    pickResponseMode(testNum, serverSd, servinfo, windowSize);

    // can close once the shutdown signal is recieved
    closeSocket(serverSd);
    freeaddrinfo(servinfo);
    // cerr << "Server Shutdown Cleanly" << endl;
    return 0;
}