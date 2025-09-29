#include "serverGame.h"
#include <pthread.h>

#define MAX_MSG_LENGTH 128

tPlayer getNextPlayer (tPlayer currentPlayer){

	tPlayer next;

		if (currentPlayer == player1)
			next = player2;
		else
			next = player1;

	return next;
}

void initDeck (tDeck *deck){

	deck->numCards = DECK_SIZE; 

	for (int i=0; i<DECK_SIZE; i++){
		deck->cards[i] = i;
	}
}

void clearDeck (tDeck *deck){

	// Set number of cards
	deck->numCards = 0;

	for (int i=0; i<DECK_SIZE; i++){
		deck->cards[i] = UNSET_CARD;
	}
}

void printSession (tSession *session){

		printf ("\n ------ Session state ------\n");

		// Player 1
		printf ("%s [bet:%d; %d chips] Deck:", session->player1Name, session->player1Bet, session->player1Stack);
		printDeck (&(session->player1Deck));

		// Player 2
		printf ("%s [bet:%d; %d chips] Deck:", session->player2Name, session->player2Bet, session->player2Stack);
		printDeck (&(session->player2Deck));

		// Current game deck
		if (DEBUG_PRINT_GAMEDECK){
			printf ("Game deck: ");
			printDeck (&(session->gameDeck));
		}
}

void initSession (tSession *session){

	clearDeck (&(session->player1Deck));
	session->player1Bet = 0;
	session->player1Stack = INITIAL_STACK;

	clearDeck (&(session->player2Deck));
	session->player2Bet = 0;
	session->player2Stack = INITIAL_STACK;

	initDeck (&(session->gameDeck));
}

unsigned int calculatePoints (tDeck *deck){

	unsigned int points;

		// Init...
		points = 0;

		for (int i=0; i<deck->numCards; i++){

			if (deck->cards[i] % SUIT_SIZE < 9)
				points += (deck->cards[i] % SUIT_SIZE) + 1;
			else
				points += FIGURE_VALUE;
		}

	return points;
}

unsigned int getRandomCard (tDeck* deck){

	unsigned int card, cardIndex, i;

		// Get a random card
		cardIndex = rand() % deck->numCards;
		card = deck->cards[cardIndex];

		// Remove the gap
		for (i=cardIndex; i<deck->numCards-1; i++)
			deck->cards[i] = deck->cards[i+1];

		// Update the number of cards in the deck
		deck->numCards--;
		deck->cards[deck->numCards] = UNSET_CARD;

	return card;
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

void sendDeck (int socket, tDeck *deck){
	//sends a deck of a player to their socket
	int sent = send(socket, deck, sizeof(tDeck), 0);
	if (sent < 0)
		showError("ERROR while writing to socket");
}

void receiveDeck (int socket, tDeck *deck){
	//receives the deck of a player from their socket
	int received = recv(socket, deck, sizeof(tDeck), 0);
	if (received < 0)
		showError("ERROR while reading from socket");
}

int main(int argc, char *argv[]){

	int socketfd;						/** Socket descriptor */
	char message1[MAX_MSG_LENGTH];	
	char message2[MAX_MSG_LENGTH];	
	int message1Length;
	int message2Length;
	struct sockaddr_in serverAddress;	/** Server address structure */
	unsigned int port;					/** Listening port */
	struct sockaddr_in player1Address;	/** Client address structure for player 1 */
	struct sockaddr_in player2Address;	/** Client address structure for player 2 */
	int socketPlayer1;					/** Socket descriptor for player 1 */
	int socketPlayer2;					/** Socket descriptor for player 2 */
	unsigned int client1Length;			/** Length of client1 structure */
	unsigned int client2Length;			/** Length of client1 structure */
	tThreadArgs *threadArgs; 			/** Thread parameters */
	pthread_t threadID;					/** Thread ID */
	tSession session;  					/** session -> game */
	tDeck gameDeck;
	unsigned int card;


	// Seed
	srand(time(0));

	// Check arguments
	if (argc != 2) {
		fprintf(stderr,"ERROR wrong number of arguments\n");
		fprintf(stderr,"Usage:\n$>%s port\n", argv[0]);
		exit(1);
	}

	// Create the socket
	socketfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	// Check
	if (socketfd < 0)
	showError("ERROR while opening socket");

	printf("WELCOME to the BlackJack server!!\n");

	// Init server structure
	memset(&serverAddress, 0, sizeof(serverAddress));

	// Get listening port
	port = atoi(argv[1]);

	// Fill server structure
	serverAddress.sin_family = AF_INET;
	serverAddress.sin_addr.s_addr = htonl(INADDR_ANY);
	serverAddress.sin_port = htons(port);

	// Bind
	if (bind(socketfd, (struct sockaddr *) &serverAddress, sizeof(serverAddress)) < 0)
		showError("ERROR while binding");

	// Listen
	listen(socketfd, 10);

	// Get length of client structure
	client1Length = sizeof(player1Address);

	// Accept!
	socketPlayer1 = accept(socketfd, (struct sockaddr *) &player1Address, &client1Length);
	
	// Check accept result
	if (socketPlayer1 < 0)
		showError("ERROR while accepting");	 
		

	// Init and read message
	memset(message1, 0, MAX_MSG_LENGTH);
	message1Length = recv(socketPlayer1, message1, MAX_MSG_LENGTH-1, 0);

	// Check read bytes
	if (message1Length < 0)
		showError("ERROR while reading from socket");

	// Same name of player1 and show message
	strcpy(session.player1Name, message1);
	printf("Player1: %s\n", message1);

	// Get the message length
	memset (message1, 0, MAX_MSG_LENGTH);
	strcpy (message1, "Player 1 confirmed!!");
	message1Length = send(socketPlayer1, message1, strlen(message1), 0);

	// Check bytes sent
	if (message1Length < 0)
		showError("ERROR while writing to socket");
	
	// Get length of client structure
	client2Length = sizeof(player2Address);

	// Accept!
	socketPlayer2 = accept(socketfd, (struct sockaddr *) &player2Address, &client2Length);
	
	// Check accept result
	if (socketPlayer2 < 0)
		showError("ERROR while accepting");	  

	// Init and read message
	memset(message2, 0, MAX_MSG_LENGTH);
	message2Length = recv(socketPlayer2, message2, MAX_MSG_LENGTH-1, 0);

	// Check read bytes
	if (message2Length < 0)
		showError("ERROR while reading from socket");

	// Save name of player2 and show message
	strcpy(session.player2Name, message2);
	printf("Player2: %s\n", message2);

	// Check bytes sent
	if (message2Length < 0)
		showError("ERROR while writing to socket");

	printf("Los 2 jugadores han sido confirmados, iniciando partida... \n");

	//Iniciate a new session aka a new game
	initSession(&session);
	printSession(&session); //Debbug

	//Game loop
	int gameOver = 0;
	while(!gameOver){
		//while the game isn't over

		//1. server sends to player 1 code TURN_BET and stack
		sendNumber(socketPlayer1, TURN_BET);
		sendNumber(socketPlayer1, session.player1Stack);

		//2. Client 1 has places a bet, check to see if its valid 
		receiveNumber(socketPlayer1, &session.player1Bet);
		while(session.player1Bet > session.player1Stack || 
		session.player1Bet > MAX_BET ||
		session.player1Bet < 1){
			//if the bet is higher than the stack, ask for a new bet
			printf("Player 1 has placed an invalid bet of %u, asking for a new one\n", session.player1Bet);
			sendNumber(socketPlayer1, TURN_BET);
			sendNumber(socketPlayer1, session.player1Stack);
			receiveNumber(socketPlayer1, &session.player1Bet);
		}
		sendNumber(socketPlayer1, TURN_BET_OK); //send confirmation
		printf("Player 1 has placed a bet of %u\n", session.player1Bet);

		//3. server sends to player 2 code TURN_BET and stack
		sendNumber(socketPlayer2, TURN_BET);
		sendNumber(socketPlayer2, session.player2Stack);

		//4. Client 2 has places a bet, check to see if its valid
		receiveNumber(socketPlayer2, &session.player2Bet);
		while(session.player2Bet > session.player2Stack ||
		session.player2Bet > MAX_BET ||
		session.player2Bet < 1){
			//if the bet is higher than the stack, ask for a new bet
			printf("Player 2 has placed an invalid bet of %u, asking for a new one\n", session.player2Bet);
			sendNumber(socketPlayer2, TURN_BET);
			sendNumber(socketPlayer2, session.player2Stack);
			receiveNumber(socketPlayer2, &session.player2Bet);
		}
		sendNumber(socketPlayer2, TURN_BET_OK); //send confirmation
		printf("Player 2 has placed a bet of %u\n", session.player2Bet);

		//at the end of the loop check if any player has 0 chips left
		/*
		if(session.player1Stack == 0 || session.player2Stack == 0){
			gameOver = 1; //if any player has no chips left, the game is over
		}
		*/
		gameOver = 1; //for testing purposes, end the game after one round
		printSession(&session); //Debbug
	}

	// Close sockets
	close(socketPlayer1);
	close(socketPlayer2);
	close(socketfd);
}
