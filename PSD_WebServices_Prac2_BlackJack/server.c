#include "server.h"

/** Shared array that contains all the games. */
tGame games[MAX_GAMES];

void initGame (tGame *game){

	// Init players' name
	memset (game->player1Name, 0, STRING_LENGTH);	
	memset (game->player2Name, 0, STRING_LENGTH);

	// Alloc memory for the decks		
	clearDeck (&(game->player1Deck));	
	clearDeck (&(game->player2Deck));	
	initDeck (&(game->gameDeck));
	
	// Bet and stack
	game->player1Bet = 0;
	game->player2Bet = 0;
	game->player1Stack = INITIAL_STACK;
	game->player2Stack = INITIAL_STACK;
	
	// Game status variables
	game->endOfGame = FALSE;
	game->status = gameEmpty;

	//mutex y variable de condicion
	pthread_mutex_init(&game->gameMutex, NULL); //funcion espera la dir de memoria 
	pthread_cond_init(&game->turnCond, NULL);
}

void initServerStructures (struct soap *soap){

	if (DEBUG_SERVER)
		printf ("Initializing structures...\n");

	// Init seed
	srand (time(NULL));

	// Init each game (alloc memory and init)
	for (int i=0; i<MAX_GAMES; i++){
		games[i].player1Name = (xsd__string) soap_malloc (soap, STRING_LENGTH);
		games[i].player2Name = (xsd__string) soap_malloc (soap, STRING_LENGTH);
		allocDeck(soap, &(games[i].player1Deck));	
		allocDeck(soap, &(games[i].player2Deck));	
		allocDeck(soap, &(games[i].gameDeck));
		initGame (&(games[i]));
	}	
}

void initDeck (blackJackns__tDeck *deck){

	deck->__size = DECK_SIZE;

	for (int i=0; i<DECK_SIZE; i++)
		deck->cards[i] = i;
}

void clearDeck (blackJackns__tDeck *deck){

	// Set number of cards
	deck->__size = 0;

	for (int i=0; i<DECK_SIZE; i++)
		deck->cards[i] = UNSET_CARD;
}

tPlayer calculateNextPlayer (tPlayer currentPlayer){
	return ((currentPlayer == player1) ? player2 : player1);
}

unsigned int getRandomCard (blackJackns__tDeck* deck){

	unsigned int card, cardIndex, i;

		// Get a random card
		cardIndex = rand() % deck->__size;
		card = deck->cards[cardIndex];

		// Remove the gap
		for (i=cardIndex; i<deck->__size-1; i++)
			deck->cards[i] = deck->cards[i+1];

		// Update the number of cards in the deck
		deck->__size--;
		deck->cards[deck->__size] = UNSET_CARD;

	return card;
}

unsigned int calculatePoints (blackJackns__tDeck *deck){

	unsigned int points = 0;
		
		for (int i=0; i<deck->__size; i++){

			if (deck->cards[i] % SUIT_SIZE < 9)
				points += (deck->cards[i] % SUIT_SIZE) + 1;
			else
				points += FIGURE_VALUE;
		}

	return points;
}

void copyGameStatusStructure (blackJackns__tBlock* status, char* message, blackJackns__tDeck *newDeck, int newCode){

	// Copy the message
	memset((status->msgStruct).msg, 0, STRING_LENGTH);
	strcpy ((status->msgStruct).msg, message);
	(status->msgStruct).__size = strlen ((status->msgStruct).msg);

	// Copy the deck, only if it is not NULL
	if (newDeck->__size > 0)
		memcpy ((status->deck).cards, newDeck->cards, DECK_SIZE*sizeof (unsigned int));	
	else
		(status->deck).cards = NULL;

	(status->deck).__size = newDeck->__size;

	// Set the new code
	status->code = newCode;	
}

// funciones aux mias
void copyPlayerName(xsd__string *dest, blackJackns__tMessage playerName){
	//liberar memoria anterior si existe
    if (*dest != NULL) free(*dest);
    
    //reservar nueva memoria con espacio + '\0'
    *dest = (xsd__string) malloc(playerName.__size + 1);
    
    //copiar y añadir '\0'
    strncpy(*dest, playerName.msg, playerName.__size);
    (*dest)[playerName.__size] = '\0';
}

//

int blackJackns__register (struct soap *soap, blackJackns__tMessage playerName, int* result){
	// Set \0 at the end of the string
	playerName.msg[playerName.__size] = 0;

	if (DEBUG_SERVER)
		printf ("[Register] Registering new player -> [%s]\n", playerName.msg);
	
	for(int i = 0; i < MAX_GAMES; i++){
		pthread_mutex_lock(&(games[i].gameMutex));

		if(games[i].status == gameEmpty){
			//hemos encontrado un hueco, metemos al primer jugador
			copyPlayerName(&games[i].player1Name, playerName);
			games[i].status = gameWaitingPlayer;
			games[i].endOfGame = FALSE;
			pthread_mutex_unlock(&(games[i].gameMutex));
			*result = i;
			return SOAP_OK;
		}
		else if(games[i].status == gameWaitingPlayer){
			//hemos encontrado una partida que espera al jugador 2
			if(strcmp(games[i].player1Name, playerName.msg) == 0){
				//nombre repetido en la misma partida
				pthread_mutex_unlock(&games[i].gameMutex);
				*result = ERROR_NAME_REPEATED;
				return SOAP_OK;
			}
			else{
				//entra el segundo jugador, empezamos partida, se reparten cartas
				copyPlayerName(&games[i].player2Name, playerName);
				games[i].status = gameReady;
				initDeck(&games[i].gameDeck);
				clearDeck(&games[i].player1Deck);
				clearDeck(&games[i].player2Deck);

				for(int c = 0; c < 2; c++){
					games[i].player1Deck.cards[c] = getRandomCard(&games[i].gameDeck);
                    games[i].player2Deck.cards[c] = getRandomCard(&games[i].gameDeck);
				}
				games[i].player1Deck.__size = 2;
				games[i].player2Deck.__size = 2;

				//el primero en jugar se escoge de manera aleatoria
				games[i].currentPlayer = (rand() % 2 == 0) ? player1 : player2;
				games[i].endOfGame = FALSE;

				pthread_mutex_unlock(&games[i].gameMutex);
				*result = i;
				return SOAP_OK;
			}
		}
	}

  	return SOAP_OK;
}

int main(int argc, char **argv){ 

	struct soap soap;
	struct soap *tsoap;
	pthread_t tid;
	int port;
	SOAP_SOCKET m, s;

		// Check arguments
		if (argc !=2) {
			printf("Usage: %s port\n",argv[0]);
			exit(0);
		}

	
	return 0;
}

//implmentacion de los servicios

int blackJackns__getStatus(struct soap *soap, blackJackns__tMessage playerName, int gameId, blackJackns__tBlock* status){

	return SOAP_OK;
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName, int gameId, int action, blackJackns__tBlock* status){


	return SOAP_OK;
}