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

int blackJackns__register (struct soap *soap, blackJackns__tMessage playerName, int* result){
	printf("Bienvenido! Vamos a jugar al BlackJack!\n");
	// Set \0 at the end of the string
	playerName.msg[playerName.__size] = 0;

	if (DEBUG_SERVER)
		printf ("[Register] Registering new player -> [%s]\n", playerName.msg);
	
	for(int i = 0; i < MAX_GAMES; i++){
		pthread_mutex_lock(&(games[i].gameMutex));

		if(games[i].status == gameEmpty){
			//hemos encontrado un hueco, metemos al primer jugador
			strncpy(games[i].player1Name, playerName.msg, playerName.__size);
    		games[i].player1Name[playerName.__size] = '\0';
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
				strncpy(games[i].player2Name, playerName.msg, playerName.__size);
    			games[i].player2Name[playerName.__size] = '\0';
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

		pthread_mutex_unlock(&games[i].gameMutex);		
	}

	//si hemos llegado hasta aqui, es que no hemos encontradoni un hueco 
	*result = ERROR_SERVER_FULL;
  	return SOAP_OK;
}

void *processRequest(void *soap){

	pthread_detach(pthread_self());

	printf ("Processing a new request...");

	soap_serve((struct soap*)soap);
	soap_destroy((struct soap*)soap);
	soap_end((struct soap*)soap);
	soap_done((struct soap*)soap);
	free(soap);

	return NULL;
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


	// Init environment
	soap_init(&soap);

	initServerStructures(&soap);

	// Configure timeouts
	soap.send_timeout = 60; // 60 seconds
	soap.recv_timeout = 60; // 60 seconds
	soap.accept_timeout = 3600; // server stops after 1 hour of inactivity
	soap.max_keep_alive = 100; // max keep-alive sequence

	// Get listening port
	port = atoi(argv[1]);

	// Bind
	m = soap_bind(&soap, NULL, port, 100);

	if (!soap_valid_socket(m)){
		exit(1);
	}

	printf("Server BlackJack is ON!\n");

	while (TRUE){

		// Accept a new connection
		s = soap_accept(&soap);

		// Socket is not valid :(
		if (!soap_valid_socket(s)){

			if (soap.errnum){
				soap_print_fault(&soap, stderr);
				exit(1);
			}

			fprintf(stderr, "Time out!\n");
			break;
		}

		// Copy the SOAP environment
		tsoap = soap_copy(&soap);

		if (!tsoap){
			printf ("SOAP copy error!\n");
			break;
		}

		// Create a new thread to process the request
		pthread_create(&tid, NULL, (void*(*)(void*))processRequest, (void*)tsoap);
	}

	// Detach SOAP environment
	soap_done(&soap);
	return 0;
}

//funciones auxiliares para evitar repetir codigo innecesariamente

void nameSafe(blackJackns__tMessage *playerName) {
	//comprobaciones para no pasar de los limites del
	//playerName->msg y agregar el '\0'
	int size = playerName->__size;
	if(size < 0) size = 0;
	if(size >= STRING_LENGTH) size = STRING_LENGTH - 1;
	playerName->msg[size] = 0;
}

int getPlayerIndex(int gameId, const char *name) {
	//comprobaciones para no pasar de los limites del
	if (gameId < 0 || gameId >= MAX_GAMES) return 0;                      /* índice fuera de rango */
    if (strlen(games[gameId].player1Name) && strcmp(games[gameId].player1Name, name) == 0)
        return player1;                                                         /* coincide con player1 */
    if (strlen(games[gameId].player2Name) && strcmp(games[gameId].player2Name, name) == 0)
        return player2;                                                         /* coincide con player2 */
    return -1;
}

int StatusAndUnlock(tGame *g, blackJackns__tBlock *status, const char *msg, blackJackns__tDeck *deck, int code) {
    //copia en status, desbloquea el mutex de la partida y devuelve SOAP_OK.
    copyGameStatusStructure(status, (char *)msg, deck, code);  
    pthread_mutex_unlock(&g->gameMutex);                       
    return SOAP_OK;                                            
}

//implementacion de los servicios

int blackJackns__getStatus(struct soap *soap, blackJackns__tMessage playerName, int gameId, blackJackns__tBlock* status){

	char message[STRING_LENGTH];
	blackJackns__tDeck *playerDeck = NULL;        
    blackJackns__tDeck *opponentDeck = NULL; 
	int playerIndex = 0;
	//Se reserva memoria para almacenar los datos necesarios
	allocClearBlock(soap, status);
	nameSafe(&playerName);
	//se valida el gameID
    if (gameId < 0 || gameId >= MAX_GAMES) {
        copyGameStatusStructure(status, message, NULL, ERROR_PLAYER_NOT_FOUND);
        return SOAP_OK;
    }
	pthread_mutex_lock(&games[gameId].gameMutex);

	//comprobar si el jugador está registrado en esta partida
    playerIndex = getPlayerIndex(gameId, playerName.msg);
    if (playerIndex == -1) {
        pthread_mutex_unlock(&games[gameId].gameMutex);
        copyGameStatusStructure(status, message, NULL, ERROR_PLAYER_NOT_FOUND);
        return SOAP_OK;
    }
	//Se optienen los mazos correspondientes
	playerDeck = (playerIndex == 0) ? &games[gameId].player1Deck : &games[gameId].player2Deck;
	opponentDeck = (playerIndex == 0) ? &games[gameId].player2Deck : &games[gameId].player1Deck;
	
	//verifica si empezo la partida
	if (games[gameId].status != gameReady) {
        return StatusAndUnlock(&games[gameId], status, message, myDeck, TURN_WAIT);
    }

	while (!games[gameId].endOfGame &&
           ((playerIndex == 0 && games[gameId].currentPlayer != player1) ||
            (playerIndex == 1 && games[gameId].currentPlayer != player2)))
    {
        //se esperamos a que el turno cambie 
		//(se asume que el mutex fue bloqueado antes)
        pthread_cond_wait(&games[gameId].turnCond, &games[gameId].gameMutex);
    }
	//una vez que ya se dejo de esperar
	//puede haber terminado esta espera porque es su turno
	//o termino la partida
    if (games[gameId].endOfGame) {
        /* la partida ha terminado: calculamos puntos y devolvemos GAME_WIN/GAME_LOSE */
        unsigned int playerPoints  = calculatePoints(playerDeck);    /* puntos del solicitante */
        unsigned int opponentPoints  = calculatePoints(opponentDeck);   /* puntos del rival */

        if (playerPoints > 21) {
            return respond_and_unlock(&games[gameId], status, message, playerDeck, GAME_LOSE);
        } 
    }
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName, int gameId, int action, blackJackns__tBlock* status){
	char buffer[];



	return SOAP_OK;
}