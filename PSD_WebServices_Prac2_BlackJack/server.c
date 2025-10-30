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
			//SI HUBO UNA PARTIDA ANTES AQUI, LIMPIAMOS ANTES DE EMPEZAR OTRA PARTIDA 
			if(games[i].endOfGame == TRUE){
				memset (games[i].player1Name, 0, STRING_LENGTH);	
				memset (games[i].player2Name, 0, STRING_LENGTH);
				clearDeck(&games[i].player1Deck);
				clearDeck(&games[i].player2Deck);
				initDeck(&games[i].gameDeck);
				games[i].player1Bet = 0;
				games[i].player2Bet = 0;
				games[i].player1Stack = INITIAL_STACK;
				games[i].player2Stack = INITIAL_STACK;
				games[i].endOfGame = FALSE;
			}

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

				games[i].player1Bet = DEFAULT_BET;
				games[i].player2Bet = DEFAULT_BET;

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
		if(pthread_create(&tid, NULL, (void*(*)(void*))processRequest, (void*)tsoap) != 0) {
			fprintf(stderr, "Error al crear el hilo\n");
		}
	}

	// Detach SOAP environment
	soap_done(&soap);
	printf("Salida del servidor BlackJack...\n");
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
		snprintf(message, STRING_LENGTH, "ID de la partida invalido: %d.", gameId);
        copyGameStatusStructure(status, message, NULL, ERROR_PLAYER_NOT_FOUND);
        return SOAP_OK;
    }
	pthread_mutex_lock(&games[gameId].gameMutex);

	//comprobar si el jugador está registrado en esta partida
    playerIndex = getPlayerIndex(gameId, playerName.msg);
    if (playerIndex == -1) {
		snprintf(message, STRING_LENGTH, "Jugador '%s' no registrado en la partida con ID: %d.", playerName.msg, gameId);
        pthread_mutex_unlock(&games[gameId].gameMutex);
        copyGameStatusStructure(status, message, NULL, ERROR_PLAYER_NOT_FOUND);
        return SOAP_OK;
    }
	//Se optienen los mazos correspondientes
	playerDeck = (playerIndex == 0) ? &games[gameId].player1Deck : &games[gameId].player2Deck;
	opponentDeck = (playerIndex == 0) ? &games[gameId].player2Deck : &games[gameId].player1Deck;
	
	//verifica si empezo la partida
	if (games[gameId].status != gameReady) {
		snprintf(message, STRING_LENGTH, "Partida con ID %d: esperando al segundo jugador...", gameId);
        return StatusAndUnlock(&games[gameId], status, message, playerDeck, TURN_WAIT);
    }

	while (!games[gameId].endOfGame &&
           ((playerIndex == player1 && games[gameId].currentPlayer != player1) ||
            (playerIndex == player2 && games[gameId].currentPlayer != player2)))
    {
        //se esperamos a que el turno cambie 
		//(se asume que el mutex fue bloqueado antes)
        pthread_cond_wait(&games[gameId].turnCond, &games[gameId].gameMutex);
    }
	//una vez que ya se dejo de esperar
	//puede haber terminado esta espera porque es su turno
	//o termino la partida
	//turno del jugador actual (partida no terminada)
	unsigned int puntos_actuales = calculatePoints(playerDeck); 
	unsigned int puntos_rivales = calculatePoints(opponentDeck);
	snprintf(message, STRING_LENGTH, "Es tu turno.\n puntos del rival:  %u\n Tienes %u puntos.", puntos_rivales, puntos_actuales); 
	return StatusAndUnlock(&games[gameId], status, message, playerDeck, TURN_PLAY);
}

int blackJackns__playerMove(struct soap *soap, blackJackns__tMessage playerName, int gameId, int action, blackJackns__tBlock* status){
	// Set \0 at the end of the string
	playerName.msg[playerName.__size] = 0;
	blackJackns__tDeck* playerDeck;
	blackJackns__tDeck* rivalDeck;
	unsigned int playerStack; 
	unsigned int rivalStack;
	unsigned int bet = DEFAULT_BET;
	char message[STRING_LENGTH];

	if(DEBUG_SERVER){
		printf("Estoy en __playerMove. El jugador %s quiere hacer la accion %d, en el %d partida\n", playerName.msg, action, gameId);
	}

	allocClearBlock(soap, status);

	pthread_mutex_lock(&games[gameId].gameMutex);

	int isPlaying = getPlayerIndex(gameId, playerName.msg);
	int currentPlayer = games[gameId].currentPlayer;

	if(isPlaying == player1 && currentPlayer == player1){
		playerDeck = &games[gameId].player1Deck;
		rivalDeck = &games[gameId].player2Deck;
		playerStack = &games[gameId].player1Stack;
		rivalStack = &games[gameId].player2Stack;
	}
	else{
		playerDeck = &games[gameId].player2Deck;
		rivalDeck = &games[gameId].player1Deck;
		playerStack = &games[gameId].player2Stack;
		rivalStack = &games[gameId].player1Stack;
	}

	if(currentPlayer != isPlaying){
		//NO ES SU TURNO
		if(DEBUG_SERVER){
			printf("Estoy en __playerMove. ERROR: el jugador %s no tiene el turno\n", playerName.msg);
		}

		copyGameStatusStructure(status, "No es tu turno", playerDeck, TURN_WAIT);
		pthread_mutex_unlock(&games[gameId].gameMutex);
		return SOAP_OK;
	}

	if(action == PLAYER_HIT_CARD){
		//ACCION: PEDIR CARTA
		if(DEBUG_SERVER){
			printf("Estoy en __playerMove. El jugador %s ha pedido una carta\n", playerName.msg);
		}

		unsigned int carta = getRandomCard(&games[gameId].gameDeck);
		playerDeck->cards[playerDeck->__size] = carta;
		playerDeck->__size++;

		unsigned int playerPoints = calculatePoints(playerDeck);
		if(DEBUG_SERVER){
			printf("Estoy en __playerMove. el jugador %s tiene ahora %d puntos\n", playerName.msg, playerPoints);
		}

		if(playerPoints > 21){
			//se ha pasado. Pierde
			if(DEBUG_SERVER){
				printf("Estoy en __playerMove. El jugador %s se ha pasado con %d puntos.\n", playerName.msg, playerPoints);
			}

			playerStack -= bet;
			rivalStack += bet;

			if(DEBUG_SERVER){
				printf("Ajustamos los stacks: Player = %d Rival = %d\n", playerStack, rivalDeck);
			}

			
			if(playerStack <= 0){
				games[gameId].endOfGame = TRUE;
				games[gameId].status = gameEmpty;
				snprintf(message, STRING_LENGTH, "Tu stack ha llegado a 0. Has perdido la partida\n");
				copyGameStatusStructure(status, message, playerDeck, GAME_LOSE);

				//desbloquear el rival que estaba esperando
				//despierta en getStatus()
				pthread_cond_broadcast(&games[gameId].turnCond);
				pthread_mutex_unlock(&games[gameId].gameMutex);
				return SOAP_OK;
			}
			snprintf(message, STRING_LENGTH, "Has perdido con %d puntos! Tu stack ahora es de %d. Nueva ronda..\n", playerPoints, playerStack);
			
			initDeck(&games[gameId].gameDeck);
			clearDeck(&games[gameId].player1Deck);
			clearDeck(&games[gameId].player2Deck);

			games[gameId].player1Bet = DEFAULT_BET;
			games[gameId].player2Bet = DEFAULT_BET;

			for(int c = 0; c < 2; c++){
				games[gameId].player1Deck.cards[c] = getRandomCard(&games[gameId].gameDeck);
				games[gameId].player2Deck.cards[c] = getRandomCard(&games[gameId].gameDeck);
			}
			games[gameId].player1Deck.__size = 2;
			games[gameId].player2Deck.__size = 2;

			//el primero en jugar se escoge de manera aleatoria
			games[gameId].currentPlayer = (rand() % 2 == 0) ? player1 : player2;
			games[gameId].endOfGame = FALSE;

			copyGameStatusStructure(status, message, playerDeck, TURN_WAIT);

			//desbloquear el rival que estaba esperando
			//despierta en getStatus()
			pthread_cond_broadcast(&games[gameId].turnCond);
			pthread_mutex_unlock(&games[gameId].gameMutex);
			return SOAP_OK;
		}

		//no se paso de 21. puede pedir mas cartas 
		snprintf(message, STRING_LENGTH, "Carta recibida. Tus puntos ahora son %d\n", playerPoints);
		copyGameStatusStructure(status, message, playerDeck, TURN_PLAY);
		pthread_mutex_unlock(&games[gameId].gameMutex);
		return SOAP_OK;
	}
	else if(action == PLAYER_STAND){
		//ACCION: PLANTARSE 
		if(DEBUG_SERVER){
			printf("Estoy en __playerMode. El jugador %s se planta\n", playerName.msg);
		}

		unsigned int playerPoints = calculatePoints(playerDeck);
		unsigned int rivalPoints = calculatePoints(rivalDeck);

		if(DEBUG_SERVER){
			printf("Estoy en __playerMove. Estos son los puntos: Player = %d Rival = %d\n", playerPoints, rivalPoints);
		}

		games[gameId].currentPlayer = calculateNextPlayer(currentPlayer);

		if(DEBUG_SERVER){
			printf("Estoy en __playerMove. El turno pasa al jugador %s\n", games[gameId].currentPlayer);
		}

		snprintf(message, STRING_LENGTH, "Te plantas con %d puntos. Stack: %d\n", playerPoints, playerStack);
		copyGameStatusStructure(status, message, playerDeck, TURN_WAIT);

		//despertar al rival que esta bloqueado en getStatus()
		pthread_cond_signal(&games[gameId].turnCond);
		pthread_mutex_unlock(&games[gameId].gameMutex);
		return SOAP_OK;
	}

	return SOAP_OK;
}