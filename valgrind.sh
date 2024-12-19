# this is the demo file to run all test cases for the UDP server and client

# --leak-check=full --show-leak-kinds=all
# Function to start the server and check if it started successfully
start_server() {
    valgrind ./server "$1" "$2" &
    SERVER_PID=$!
    sleep 2 # Allow some time for the server to start

    # Check if the server started successfully
    if ! ps -p $SERVER_PID > /dev/null; then
        echo "Error: Failed to start server for test $1 and window size $2"
        exit 1
    fi
}

# Function to stop the server gracefully
stop_server() {
    if [ -n "$SERVER_PID" ]; then
        kill $SERVER_PID
        wait $SERVER_PID 2>/dev/null # Wait for server to shut down
    fi
}

# --leak-check=full --show-leak-kinds=all
# test the unreliable send
echo "Test 1: Testing Unreliable send using UDP. Should give -2 as the resend # since it does not record resends. A -1 means an error occured."
#start webserver in the background and pause for a moment while it loads
start_server 1 10
valgrind ./client 1 10
echo ""
# shut down the webserver so it isn't running when we do this again.
stop_server

# test the stop and wait send
echo "Test 2: Testing the stop and wait send using UDP. Resends should be about 1% of the time ie: 200 in this case."
#start webserver in the background and pause for a moment while it loads
start_server 2 10
valgrind ./client 2 10
echo ""
# shut down the webserver so it isn't running when we do this again.
stop_server

# All of the following tests are between my retriever and my server -- both running on port 2087
echo "Test 3: Testing the sliding window send using UDP, goes through sizes 1-30. Resends should be about 1% of the time ie: 200 in this case. Resends should decrease as the window size increases."
#start webserver in the background and pause for a moment while it loads
for i in {1..1}; do
    start_server 3 $i
    valgrind ./client 3 $i
    # shut down the webserver so it isn't running when we do this again.
    stop_server
done
echo ""