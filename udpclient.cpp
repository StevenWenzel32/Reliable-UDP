#include <stdio.h>
#include <stdlib.h>
#include <iostream>
#include <sys/types.h>    // socket, bind
#include <sys/socket.h>   // socket, bind, listen, inet_ntoa
#include <unistd.h>       // read, write, close
#include <strings.h>      // bzero
#include <netinet/tcp.h>  // SO_REUSEADDR
#include <string.h>       // memset
#include <errno.h>        // errno
#include <fstream>        // ofstream for file creation
#include <netdb.h>        // gethostbyname
#include <sstream>        // for stringstream stuff
#include <random>         // for random #
#include <chrono>         // for steady_clock and timer stuff
#include <queue>          // for queue, duh
#include <cerrno>
#include <cstring>        // for strerror
#include <vector>
#include <array>

// the repetition of sending the data within the buffers
#define REPETITION 20000
// msg byte size
#define MSG_BYTES 1460
// the read buffer size based on size of type used in the array -- int in this case
#define BUFFER_SIZE (1460/sizeof(int))
// port used
#define PORT "2087"
// host
#define HOST "127.0.0.1"
// the window size to stop the tests at
#define MAX_WINDOW_SIZE 30
#define DEFAULT_WINDOW_SIZE 10
// how long in usec (microseconds) to wait before a time out
#define TIMEOUT 1500

using namespace std;

// handle making the socket structs
// can later add in params to change the family and scoktype
struct addrinfo* makeGetaddrinfo(const char* serverIp, const char* port){
    // for checking the return of getaddrinfo
    int status;
    // holds the info for the client address
    struct addrinfo client_addr;
    // points to the results that are in a linked list - is returned
    struct addrinfo *servinfo; 
    
    // create the struct and address info
    // make sure the struct is empty
    memset(&client_addr, 0, sizeof client_addr);
    // doesn't matter if its ipv4 or ipv6
    client_addr.ai_family = AF_UNSPEC;
    // tcp stream sockets
    client_addr.ai_socktype = SOCK_DGRAM;
    
    // getaddrinfo with error check
    if ((status = getaddrinfo(serverIp, port, &client_addr, &servinfo)) != 0 ) {
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        exit(1);
    }
    
    return servinfo;
}

// make the socket and do error checks, and return the Sd
int makeSocket(struct addrinfo* servinfo){
    // open a stream-oriented socket with the internet address family
    int clientSd = socket(servinfo->ai_family, servinfo->ai_socktype, 0);
    // check for error
    if(clientSd == -1){
        cerr << "ERROR: Failed to make socket" << endl;
        exit(1);
    }
    
    return clientSd;
}

// recieve and return the ackNum from server without waiting for a response - used for sliding window
int readAckNoBlock(int clientSd, struct addrinfo* servinfo){
    // "buffer" for reading in and returning the server ackNum
    int ackNum = -1;
    int nRead = 0;
    nRead = recvfrom(clientSd, &ackNum, sizeof(int), MSG_DONTWAIT, servinfo->ai_addr, &(servinfo->ai_addrlen));
        if (nRead == -1){
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // do nothing simply nothing to read yet
                ackNum = -1;
            } else {
                cerr << "Error reading from socket: clientSd = " << clientSd << endl;
                ackNum = -2;
            }
        } else if (nRead == 0) {
            cerr << "Server closed the connection" << endl;
            ackNum = -3;
        } else {
// cout << "CLIENT Recieved via non blocking read - AckNum = " << ackNum << endl;
            return ackNum;
        }
    return -4;
}

// process the response into the ackNum and check if it matches the sent seq #
bool processStopWaitAck(int ackNum, int seqNum){
    //check if the acknum matches the msg seq#
    if (ackNum == seqNum){
        return true;
    } else {
        return false;
    }
}

// close the socket and check for errors
void closeSocket(int sd){
    int bye = close(sd);
    if (bye == -1){
        cerr << "Error closing socket" << endl;
    }
}

// send a simple UDP msg as int[]
void sendMsg(int sd, int message[], struct addrinfo *servinfo){
    int bytes_sent = sendto(sd, message, BUFFER_SIZE, 0, servinfo->ai_addr, servinfo->ai_addrlen);
    if (bytes_sent == -1){
        cerr << "Problem with simple send" << endl;
    }
}

// get the start time in int form in usec
int startTimer(){
    // get current time/start timer in an int form in usec
    return chrono::duration_cast<chrono::microseconds>(chrono::steady_clock::now().time_since_epoch()).count();
}

// stop the timer and return the eplased time in usec
int stopTimer(int start){
    // get the end time
    int end = startTimer();
    // get the eplased time in an int
    int elapsed = (end - start);
    return elapsed;
}

// test 1 = send message[] to the server using the sd object 20k times
// send the msg REPITION times using UDP
void clientUnreliable(const int sd, int message[], struct addrinfo *servinfo){
    // send REPETITION messages
    for (int i = 0; i < REPETITION; i++){
        sendMsg(sd, message, servinfo);
    }
}

// test 2 = send message[] and recieve an ACK from the server 20k times using the sd object
// message[] must store the sequence numbers in the first element
// if no immediate ACK start a timer - if timeout resend the message and up a counter - can't block on recvfrom
// return = resend
int clientStopWait(const int sd, int message[], struct addrinfo *servinfo){
    int resend = 0;
    // pick rand starting sequence #
    int seqNum = 1;
    // send REpetition messages, loop - counting ACKs
    for (int i = 0; i < REPETITION;){
        // write in seq #
        message[0] = seqNum;

        // send message 
        sendMsg(sd, message, servinfo);

        // set the duration of the timer
        int duration = chrono::microseconds(TIMEOUT).count();

        // get current time/start timer
        auto start = startTimer();

        bool leave = false;
        // while no ACK loop and wait
        while (!leave){
            auto elapsed = stopTimer(start);

            // recieve the server response - ACKs
            int ackNum = readAckNoBlock(sd, servinfo);

            // process server ackNum
            bool acked = processStopWaitAck(ackNum, seqNum);

            // if ACK recievied
            if (acked){
                // up seq #
                seqNum++;
                // up message count
                i++; 
                // leave while
                leave = true;
            } 
            // if timeout - 1500 usec
            else if (elapsed >= duration){
                // resend message
                sendMsg(sd, message, servinfo);
                // restart timer
                start = startTimer();
                // up count
                resend++;
            }
        }
    }
    return resend;
}

// send message[] and recives an ACK from server 20k times
// message[] must store the sequence numbers and ACK numbers in the first element
// keeps writing as long as in transit is less than the windowSize, windowSize is a fixed #
// on timeout resends the whole window
// return = resend
int clientSlidingWindow(const int sd, int message[], int windowSize, struct addrinfo *servinfo){
    // # of msgs resent
    int resend = 0;
    // pick rand starting sequence # - might need this to wrap -- getRandNum()
    int seqNum = 1;
    // calc the max of the range of sequence #s, must follow this formula -- not used in the current version
    int seqMax = seqNum + ((2 * windowSize) + 1);
     // # of msgs not acked/inFlight
    int unacked = 0;
    // vector of unacked seq #
    vector<array<int, 2>> window;
    // set the duration of the timer
    int duration = chrono::microseconds(TIMEOUT).count();

    // send REPETITION messages, loop - counting ACKs/confirmed recived
    for (int i = 0; i < REPETITION;){
        // if window is not full send another msg -- perhaps put in a while loop that ends when there is a response to read****
        while (unacked < windowSize && seqNum < REPETITION + 1){
            // write in seq # in the msg
            message[0] = seqNum;
            // put the seq # in the window queue and start the timer
            window.push_back({seqNum, startTimer()});
            // send message 
            sendMsg(sd, message, servinfo);
            unacked++;
            seqNum++;
        } 
        // recieve the server response - ACKs
        int ackNum = readAckNoBlock(sd, servinfo);
        if (ackNum > 0) {
            // if ACK >= seqNum then keeping removing the messages from the window -- cumulative ACKs
            while (!window.empty() && ackNum >= window.front()[0]){
                // down unacked
                unacked--;
                // remove acked msg from window
                window.erase(window.begin());
                // up message sent/acked count
                i++;
            }
        }
        if (!window.empty()) {
            // get elapsed time by checking the start time in the window
            int elapsed = stopTimer(window.front()[1]);

            // if timeout - 1500 usec - can only have a time out when the window is full
            if (elapsed >= duration){
                // Resend message
                sendMsg(sd, window.front().data(), servinfo); 
                window.front()[1] = startTimer();
                // up resend count
                resend++;
            }
        }
    }
    return resend;
}

// call the right test, returns the resend counts from test 2-3
// if it returns -1 there was an error, if it returns -2 it called test 1
int pickTest(int testNum, int sd, struct addrinfo *servinfo, int windowSize){
    int resend = 0;
    int buffer[BUFFER_SIZE] = {0};
    if (testNum > 3 || testNum < 1){
        cerr << "ERROR: Unknown testNum: " << testNum << endl;
        return -1;
    } else if (testNum == 1){
        // call test 1
        clientUnreliable(sd, buffer, servinfo);
        // should always be -2
        return -2;
    } else if (testNum == 2){
        // call test 2
        resend = clientStopWait(sd, buffer, servinfo);
        return resend;
    } else {
        // call test 3
        resend = clientSlidingWindow(sd, buffer, windowSize, servinfo);
        return resend;
    }
}

void printResults(int testNum, int resend){
    string testName = "";
    // pick the right msg -- check is already done in pickTest
    if (testNum == 1){
        testName = "Unreliable";
    } else if (testNum == 2){
        testName = "Stop and Wait";
    } else {
        testName = "Sliding Window";
    }

    cout << "Test " << testNum << ": " << testName << ": Messages Sent = " << REPETITION << ", Resends = " << resend << endl; 
}

void printWindowSizeTests(int windowSize, int resend, int runTime){
    cout << "Test 3: Sliding Window: Messages Sent = " << REPETITION << ", Window Size = " << windowSize << ", Resends = " << resend << ", Run Time = " << runTime << "usec" << endl; 
}

// send get request to a web server
int main (int argc, char* argv[]) {
    // check that the command line has the right # of params
    if (argc < 3){
        cerr << "Error: Not enough parameters passed in. Usage: " << argv[0] << " <testNumber> <windowSize>\n";
        return 1;
    }

    // params passed in through command line
    int testNum = atoi(argv[1]);
    int windowSize = atoi(argv[2]);

    // make the socket structs and error check
    // pass in serverIP/domain -- getaddrinfo does the DNS lookup for us, how nice!
    struct addrinfo* servinfo = makeGetaddrinfo(HOST, PORT);

    // make the socket
    int clientSd = makeSocket(servinfo);

    int resend;
    // if sliding window print out the window size in results 
    if (testNum == 3){
        // get the start time of the test in usec
        int start = startTimer();
        // call the right test, tests call msg builds and they send the msg themselves
        // tests also call readResponse() and processResponse()
        resend = pickTest(testNum, clientSd, servinfo, windowSize);
        // get the run time in usec
        int runTime = stopTimer(start);
        printWindowSizeTests(windowSize, resend, runTime);
    } else {
        resend = pickTest(testNum, clientSd, servinfo, DEFAULT_WINDOW_SIZE);
        printResults(testNum, resend);
    }

    // call that handles error checks in other function
    closeSocket(clientSd);
    freeaddrinfo(servinfo);

    return 0;
}