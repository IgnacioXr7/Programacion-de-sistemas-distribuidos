#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>

// Maximum message length
#define MAX_MSG_LENGTH 256

// Maximum number of connections
#define MAX_CONNECTIONS 5

// Function for thread processing
void *threadProcessing(void *arg);

// Thread parameter
struct ThreadArgs{
	int clntSock;
};

/** Function that shows an error message */
void showError(const char *errorMsg){
    perror(errorMsg);
    exit(0);
}

/**
 * Create, bind and listen
 */
int createBindListenSocket (unsigned short port){

	int socketId;
	struct sockaddr_in echoServAddr;

		if ((socketId = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
			showError("Error while creating a socket") ;

		// Set server address
		memset(&echoServAddr, 0, sizeof(echoServAddr));
		echoServAddr.sin_family = AF_INET;
		echoServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
		echoServAddr.sin_port = htons(port);

		// Bind
		if (bind(socketId, (struct sockaddr *) &echoServAddr, sizeof(echoServAddr)) < 0)
			showError ("Error while binding");

		if (listen(socketId, MAX_CONNECTIONS) < 0)
			showError("Error while listening") ;

	return socketId;
}

/**
 * Accept connection
 */
int acceptConnection (int socketServer){

	int clientSocket;
	struct sockaddr_in clientAddress;
	unsigned int clientAddressLength;

		// Get length of client address
		clientAddressLength = sizeof(clientAddress);

		// Accept
		if ((clientSocket = accept(socketServer, (struct sockaddr *) &clientAddress, &clientAddressLength)) < 0)
			showError("Error while accepting connection");

		printf("Connection established with client: %s\n", inet_ntoa(clientAddress.sin_addr));

	return clientSocket;
}

/**
 * Thread processing function
 */
void *threadProcessing(void *threadArgs){

	char message[MAX_MSG_LENGTH];
	int recvMsgSize;
	unsigned int clientSocket;

		// Detach resources
		pthread_detach(pthread_self()) ;

		// Get client socket from thread param
		clientSocket = ((struct ThreadArgs *) threadArgs)->clntSock;

		// Clear buffer
		memset (message, 0, MAX_MSG_LENGTH);

		// Receive message
		if ((recvMsgSize = recv(clientSocket, message, MAX_MSG_LENGTH, 0)) < 0)
			showError("Error while receiving");

		printf ("Message received -> %sThread ID:%lu [before sleep]\n", message, (unsigned long) pthread_self());
		sleep (5);
		printf ("Thread ID:%lu [after sleep]\n", (unsigned long) pthread_self());

		// Send message back to client
		if (send(clientSocket, message, strlen(message), 0) != strlen(message))
			showError("Error while sending message to client");

		close(clientSocket);

	return (NULL) ;
}




int main(int argc, char *argv[]){

	int serverSocket;
	int clientSocket;
	unsigned short listeningPort;
	pthread_t threadID;
	struct ThreadArgs *threadArgs;

		// Check arguments
		if (argc != 2){
			fprintf(stderr, "Usage: %s port\n", argv[0]);
			exit(1);
		}

		// Get the port
		listeningPort = atoi(argv[1]);

		// Create a socket (also bind and listen)
		serverSocket = createBindListenSocket (listeningPort);

		// Infinite loop...
		while(1){

			// Establish connection with a client
			clientSocket = acceptConnection(serverSocket);

			// Allocate memory
			if ((threadArgs = (struct ThreadArgs *) malloc(sizeof(struct ThreadArgs))) == NULL)
				showError("Error while allocating memory");

			// Set socket to the thread's parameter structure
			threadArgs->clntSock = clientSocket;

			// Create a new thread
			if (pthread_create(&threadID, NULL, threadProcessing, (void *) threadArgs) != 0)
				showError("pthread_create() failed");
		}
}





