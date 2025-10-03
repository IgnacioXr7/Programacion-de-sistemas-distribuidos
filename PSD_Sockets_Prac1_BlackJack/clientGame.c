#include "clientGame.h"

#define MAX_MSG_LENGTH 128	

unsigned int readBet (){

	int isValid, bet=0;
	tString enteredMove;
 
		// While player does not enter a correct bet...
		do{

			// Init...
			bzero (enteredMove, STRING_LENGTH);
			isValid = TRUE;

			printf ("Enter a value:");
			fgets(enteredMove, STRING_LENGTH-1, stdin);
			enteredMove[strlen(enteredMove)-1] = 0;

			// Check if each character is a digit
			for (int i=0; i<strlen(enteredMove) && isValid; i++)
				if (!isdigit(enteredMove[i]))
					isValid = FALSE;

			// Entered move is not a number
			if (!isValid)
				printf ("Entered value is not correct. It must be a number greater than 0\n");
			else
				bet = atoi (enteredMove);

		}while (!isValid);

		printf ("\n");

	return ((unsigned int) bet);
}

unsigned int readOption (){

	unsigned int bet;

		do{		
			printf ("What is your move? Press %d to hit a card and %d to stand\n", TURN_PLAY_HIT, TURN_PLAY_STAND);
			bet = readBet();
			if ((bet != TURN_PLAY_HIT) && (bet != TURN_PLAY_STAND))
				printf ("Wrong option!\n");			
		} while ((bet != TURN_PLAY_HIT) && (bet != TURN_PLAY_STAND));

	return bet;
}

//our auxiliary function 
void sendNumber (int socket, unsigned int number){
	//sends an unsigned int to the given socket
	int sent = send(socket, &number, sizeof(unsigned int), 0);
	if (sent < 0)
		showError("ERROR while writing to socket");
}

void receiveNumber (int socket, unsigned int *number){
	//receives an unsigned int from the given socket
	int received = recv(socket, number, sizeof(unsigned int), 0);
	if (received < 0)
		showError("ERROR while reading from socket");
}

void receiveDeck (int socket, tDeck *deck){
	//receives the deck of a player from their socket
	int received = recv(socket, deck, sizeof(tDeck), 0);
	if (received < 0)
		showError("ERROR while reading from socket");
}

int main(int argc, char *argv[]){

	int socketfd;						/** Socket descriptor */
	unsigned int port;					/** Port number (server) */
	struct sockaddr_in server_address;	/** Server address structure */
	char* serverIP;						/** Server IP */
	unsigned int endOfGame;				/** Flag to control the end of the game */
	tString playerName;					/** Name of the player */
	int nameLength;
	unsigned int code;					/** Code */
	unsigned int stack;					/** Stack */
	unsigned int bet;					/** Bet */
	tDeck playerDeck;					/** Player's deck */
	int wait = FALSE;
	unsigned int points;				/** Player's points */
	unsigned int action;				/** Player's action */
	unsigned int rivalPoints;
	tDeck rivalDeck;

		// Check arguments!
		if (argc != 3){
			fprintf(stderr,"ERROR wrong number of arguments\n");
			fprintf(stderr,"Usage:\n$>%s serverIP port\n", argv[0]);
			exit(0);
		}
	
		// Get the port
		port = atoi(argv[2]);

		// Create socket
		socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

		// Check if the socket has been successfully created
		if (socketfd < 0)
			showError("ERROR while creating the socket");

		// Get the server address
		serverIP = argv[1];

		// Fill server address structure
		memset(&server_address, 0, sizeof(server_address));
		server_address.sin_family = AF_INET;
		server_address.sin_addr.s_addr = inet_addr(serverIP);
		server_address.sin_port = htons(port);

		// Connect with server
		if (connect(socketfd, (struct sockaddr *) &server_address, sizeof(server_address)) < 0)
			showError("ERROR while establishing connection");

		// Init and read the message
		printf("Enter your name: ");
		memset(playerName, 0, MAX_MSG_LENGTH);
		fgets(playerName, MAX_MSG_LENGTH-1, stdin);
		playerName[strlen(playerName)-1] = '\0'; // Remove newline character

		// Send message to the server side
		nameLength = send(socketfd, playerName, strlen(playerName), 0);

		// Check the number of bytes sent
		if (nameLength < 0)
			showError("ERROR while writing the name");
		// Init for reading incoming message
		//memset(playerName, 0, MAX_MSG_LENGTH);
		//nameLength = recv(socketfd, playerName, MAX_MSG_LENGTH-1, 0);
		// Check bytes read
		//if (nameLength < 0)
			//showError("ERROR while reading from the socket");
		// Show the returned message
		//printf("%s\n",playerName);
		//in the beginning, the game is not finished
		endOfGame = FALSE;
		//main loop of the game
		while(!endOfGame){
			//receive code from server
			memset(&code, 0, sizeof(unsigned int));
			if(recv(socketfd, &code, sizeof(unsigned int), 0) < 0)
				showError("ERROR while reading from the socket");

			//if we are debbuging, show the code received
			if(DEBUG_CLIENT)
				showCode(code);

			//switch for the different types of codes
			switch(code){
				case TURN_BET:
					//recive my stack
					receiveNumber(socketfd, &stack);

					printf("Place your bet. Your stack is: %u\n", stack);
					//place my bet 
					bet = readBet();
					//send to server
					sendNumber(socketfd, bet);
					break;
				case TURN_BET_OK:
					printf("Bet is valid !!!\n");
					break;
				case TURN_PLAY:
						//receive my points and deck BEFORE i take action
						receiveNumber(socketfd, &points);
						receiveDeck(socketfd, &playerDeck);
						printf("Your points are: %u\n", points);
						printf("Your deck is: ");
						printDeck(&playerDeck); 
						printf("\n");

						//Player action 
						action = readOption();
						//send to server
						sendNumber(socketfd, action);
					break;
				case TURN_PLAY_WAIT:
					//receive rival points and deck
					receiveNumber(socketfd, &rivalPoints);
					receiveDeck(socketfd, &rivalDeck);
					printf("Waiting for your rival. Rival points are: %u\n", rivalPoints);
					printf("Rival deck is: ");
					printDeck(&rivalDeck);
					printf("\n");
					wait = TRUE;
					break;
				case TURN_PLAY_OUT:
					receiveNumber(socketfd, &points);
					receiveDeck(socketfd, &playerDeck);
					printf("You have exceeded 21 points and are out. Your points are: %u\n", points);
					printf("Your deck is: ");
					printDeck(&playerDeck);
					printf("\n");
					wait = TRUE;
					break;
				case TURN_GAME_WIN:
					printf("You win the game!!!\n");
					endOfGame = TRUE;
					break;
				case TURN_GAME_LOSE:
					printf("You lose the game!!!\n");
					endOfGame = TRUE;
					break;
				case TURN_PLAY_RIVAL_DONE:
					printf("Your opponent has finished his turn\n");
					break;
			}
		}
		// Close socket
		close(socketfd);

	return 0;
}
