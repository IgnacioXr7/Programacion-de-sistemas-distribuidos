#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h> 

#define MAX_MSG_LENGTH 256		// Maximum length of the message


/** Function that shows an error message */
void showError(const char *errorMsg){
    perror(errorMsg);
    exit(0);
}

int main(int argc, char *argv[]){

    int socketfd;						/** Socket descriptor */
    unsigned short portClient;			/** Port number (client) */
    unsigned short portServer;			/** Port number (server) */
    struct sockaddr_in client_address;	/** Client address structure */
    struct sockaddr_in server_address;	/** Server address structure */
    socklen_t serverLength;				/** Size of server structure */
    char* serverIP;						/** Server IP */
    char message [MAX_MSG_LENGTH];		/** Message sent to the server side */
    int msgLength;						/** Length of the message */

		// Check arguments!
		if (argc < 4){
			fprintf(stderr,"ERROR wrong number of arguments\n");
		   	fprintf(stderr,"Usage:\n$>%s host portClient portServer\n", argv[0]);
		   	exit(0);
		}
		
		// Get the server address
		serverIP = argv[1];

		// Get the port
		portClient = atoi(argv[2]);
		portServer = atoi(argv[3]);

		// Create socket
		socketfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

		// Check if the socket has been successfully created
		if (socketfd < 0)
			showError("ERROR while creating the socket");
		
		// Fill client address structure
		memset(&client_address, 0, sizeof(client_address));
		client_address.sin_family = AF_INET;
		client_address.sin_addr.s_addr = htonl(INADDR_ANY);
		client_address.sin_port = htons(portClient);	
		
		// Bind!
		if(bind(socketfd, (struct sockaddr *)&client_address, sizeof(client_address))<0) 
			showError("ERROR while binding");
		
		// Fill server address structure
		memset(&server_address, 0, sizeof(server_address));
		server_address.sin_family = AF_INET;
		server_address.sin_addr.s_addr = inet_addr(serverIP);
		server_address.sin_port = htons(portServer);

		// Init and read the message
		printf("Enter a message: ");
		memset(message, 0, MAX_MSG_LENGTH);
		fgets(message, MAX_MSG_LENGTH-1, stdin);

		// Send message to the server side
		msgLength = sendto(socketfd, 
							message, 
							strlen(message), 
							0, 
							(struct sockaddr *) &server_address, 
							sizeof(server_address));

		// Check the number of bytes sent
		if (msgLength < strlen(message))
			showError("ERROR while writing to the socket");

		// Init for reading incoming message
		memset(message, 0, MAX_MSG_LENGTH);
		
		serverLength = sizeof (server_address);
		
		// Receive message from server
		msgLength = recvfrom(socketfd, 
								message, 
								MAX_MSG_LENGTH-1, 
								0, 
								(struct sockaddr *) &server_address, 
								&serverLength);

		// Check bytes read
		if (msgLength < strlen(message))
			showError("ERROR while reading from the socket");

		// Show the returned message
		printf("%s\n",message);

		// Close socket
		close(socketfd);

	return 0;
}
