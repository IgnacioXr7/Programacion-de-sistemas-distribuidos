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

void betNumberLogic (char *playerName, int socket, unsigned int playerStack, unsigned int playerBet) {
	//1. server sends to player 1 code TURN_BET and stack
		sendNumber(socket, TURN_BET);
		sendNumber(socket, playerStack);

		//2. Client 1 has places a bet, check to see if its valid 
		receiveNumber(socket, &playerBet);
		while(playerBet > playerStack || 
		playerBet > MAX_BET ||
		playerBet < 1){
			//if the bet is higher than the stack, ask for a new bet
			printf("Player %s has placed an invalid bet of %u, asking for a new one\n",playerName, playerBet);
			sendNumber(socket, TURN_BET);
			sendNumber(socket, playerStack);
			receiveNumber(socket, &playerBet);
		}
		sendNumber(socket, TURN_BET_OK); //send confirmation
		printf("Player %s has placed a bet of %u\n", playerName, playerBet);
}

void playTurnLogic (int socketTurnPlayer, int socketWaitPlayer, int codeCurrentPlayer, int codeWaitPlayer, tDeck *gameDeck) {
		//Player that has the turn 
		sendNumber(socketTurnPlayer, codeCurrentPlayer);
		unsigned int totalPoints = calculatePoints(gameDeck);
		sendNumber(socketTurnPlayer, totalPoints);
		sendDeck(socketTurnPlayer, gameDeck);
		//Player that has to wait
		sendNumber(socketWaitPlayer, codeWaitPlayer);
		sendNumber(socketWaitPlayer, totalPoints);
		sendDeck(socketWaitPlayer, gameDeck);
}

void giveCardToPlayer (int card, tPlayer current_player, tSession *session){
	if(current_player == player1){
		session->player1Deck.cards[session->player1Deck.numCards] = card;
		session->player1Deck.numCards++;
	}
	else{
		session->player2Deck.cards[session->player2Deck.numCards] = card;
		session->player2Deck.numCards++;
	}
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
	
	unsigned int card;
	unsigned int code;
	tPlayer current_player;


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
	strcpy (message1, "Player 1 confirmed!! \n");
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

	printf("The 2 players have been confirmed, let's play... \n");

	//Iniciate a new session aka a new game
	initSession(&session);
	printSession(&session); //Debbug

	//Game loop
	int gameOver = FALSE;
	int turnBeginPlayer1 = TRUE; 
	while(!gameOver){
		//while the game isn't over
		if(turnBeginPlayer1) {
			current_player = player1;
			betNumberLogic(message1, socketPlayer1, session.player1Stack, session.player1Bet);
			betNumberLogic(message2, socketPlayer2, session.player2Stack, session.player2Bet);
			playTurnLogic(socketPlayer1, socketPlayer2, TURN_PLAY, TURN_PLAY_WAIT, &session.player1Deck);
			memset(&code, 0, sizeof(unsigned int));
			if(recv(socketPlayer1, &code, sizeof(unsigned int), 0) < 0)
				showError("ERROR while reading from the socket");
			//if we are debbuging, show the code received
			if(SERVER_DEBUG)
				showCode(code);
			switch(code){
				case TURN_PLAY_STAND: 
					//now player1 wait 
					playTurnLogic(socketPlayer1, socketPlayer2, TURN_PLAY_WAIT, TURN_PLAY_RIVAL_DONE, &session.player1Deck);

					break;
				case TURN_PLAY_HIT:
					//player whats another card 
					card = getRandomCard(&(session.gameDeck));
					giveCardToPlayer(card, current_player, &session);
					if(calculatePoints(&session.player1Deck) > 21){
						playTurnLogic(socketPlayer1, socketPlayer2, TURN_PLAY_OUT, TURN_PLAY_WAIT, &session.player1Deck);
					}
					else{
						playTurnLogic(socketPlayer1, socketPlayer2, TURN_PLAY, TURN_PLAY_WAIT, &session.player1Deck);
					}
					break;
			}

		}
		else {
			current_player = player2;
			betNumberLogic(message2, socketPlayer2, session.player2Stack, session.player2Bet);
			betNumberLogic(message1, socketPlayer1, session.player1Stack, session.player1Bet);
			playTurnLogic(socketPlayer2, socketPlayer1, TURN_PLAY, TURN_PLAY_WAIT, &session.player1Deck);
			memset(&code, 0, sizeof(unsigned int));
			if(recv(socketPlayer1, &code, sizeof(unsigned int), 0) < 0)
				showError("ERROR while reading from the socket");
			//if we are debbuging, show the code received
			if(SERVER_DEBUG)
				showCode(code);
			switch(code){
				case TURN_PLAY_STAND: 
					//now player1 wait 
					playTurnLogic(socketPlayer2, socketPlayer1, TURN_PLAY_WAIT, TURN_PLAY_RIVAL_DONE, &session.player1Deck);

					break;
				case TURN_PLAY_HIT:
					//player whats another card 
					//card = getRandomCard(&gameDeck);
					printf("Antes de robar carta\n");
					//giveCardToPlayer(card, current_player, session);
					if(calculatePoints(&session.player2Deck) > 21){
						playTurnLogic(socketPlayer2, socketPlayer1, TURN_PLAY_OUT, TURN_PLAY_WAIT, &session.player1Deck);
					}
					else{
						playTurnLogic(socketPlayer2, socketPlayer1, TURN_PLAY, TURN_PLAY_WAIT, &session.player1Deck);
					}
					break;
			}
		}
		turnBeginPlayer1 = !turnBeginPlayer1;
		//at the end of the loop check if any player has 0 chips left
	
		if(session.player1Stack == 0 || session.player2Stack == 0){
			gameOver = 1; //if any player has no chips left, the game is over
		}
		
		//gameOver = TRUE; //for testing purposes, end the game after one round
		printSession(&session); //Debbug
	}

	// Close sockets
	close(socketPlayer1);
	close(socketPlayer2);
	close(socketfd);
}
