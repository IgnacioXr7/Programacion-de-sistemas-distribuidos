#include "client.h"

unsigned int readBet (){

	int isValid, bet=0;
	xsd__string enteredMove;

		// While player does not enter a correct bet...
		do{

			// Init...
			enteredMove = (xsd__string) malloc (STRING_LENGTH);
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
		free (enteredMove);

	return ((unsigned int) bet);
}

unsigned int readOption (){

	unsigned int bet;

		do{
			printf ("What is your move? Press %d to hit a card and %d to stand\n", PLAYER_HIT_CARD, PLAYER_STAND);
			bet = readBet();
			if ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND))
				printf ("Wrong option!\n");			
		} while ((bet != PLAYER_HIT_CARD) && (bet != PLAYER_STAND));

	return bet;
}

int main(int argc, char **argv){

	struct soap soap;					/** Soap struct */
	char *serverURL;					/** Server URL */
	blackJackns__tMessage playerName;	/** Player name */
	blackJackns__tBlock gameStatus;		/** Game status */
	unsigned int playerMove;			/** Player's move */
	int resCode, gameId;				/** Result and gameId */
	int gameFinish = FALSE;

	// Check arguments
	if (argc !=2) {
		printf("Usage: %s http://server:port\n",argv[0]);
		exit(0);
	}
	
	// Init gSOAP environment
	soap_init(&soap);

	// Obtain server address
	serverURL = argv[1];

	// Allocate memory
	allocClearMessage (&soap, &(playerName)); //reserva memoria para mensaje
	printf("Escribe el nombre del jugador: ");
	fgets(playerName.msg, STRING_LENGTH - 1, stdin);
	playerName.msg[strlen(playerName.msg) - 1 ] = '\0';
	playerName.__size = strlen(playerName.msg);

	//llamamos al servicio register
	printf("Registrando el nombre en servidor...\n");
	resCode = soap_call_blackJackns__register(&soap, argv[1], "", playerName, &gameId);

	//mensaje dependiendo de la respuesta 
	if(resCode == SOAP_OK){
		if(gameId >= 0){
			printf("Se ha registrado el jugador %s en el juego %d|n", playerName.msg, gameId);
		}
		else if(gameId == ERROR_NAME_REPEATED){
			printf("No puede haber dos jugadores en una misma partida con el mismo nombre\n");
		}
		else if(gameId == ERROR_SERVER_FULL){
			printf("El servidor esta lleno ahora mismo, pruebe mas tarde\n");
		}
		else{
			printf("Codigo desconocido: %d\n", gameId);
		}
	}
	else{
		soap_print_fault(&soap, stderr);
		soap_destroy(&soap);
        soap_end(&soap);
        soap_done(&soap);
        return 1;
	}

	printf("Consultando estado...\n");

   if (soap_call_blackJackns__getStatus(&soap, argv[1], NULL, playerName, gameId, &gameStatus) == SOAP_OK) {
        printf("Mensaje del servidor: %s\n", gameStatus.msgStruct.msg);
    } else {
        soap_print_fault(&soap, stderr);
    }

	while(!gameFinish){
		//bucle principal del cliente 
		allocClearBlock(&soap, &gameStatus);

		//primero miramos el estado de la partida
		printf("Consultando estado...\n");
		if (soap_call_blackJackns__getStatus(&soap, argv[1], NULL, playerName, gameId, &gameStatus) == SOAP_OK) {
			printf("Mensaje del servidor: %s\n", gameStatus.msgStruct.msg);
		} else {
			soap_print_fault(&soap, stderr);
			gameFinish = TRUE;
			return 1;
		}
		printStatus(&gameStatus, TRUE);

		switch(gameStatus.code){
			case TURN_PLAY:
				//Nos toca jugar - realizamos un movimiento
				printf("TE TOCA!\n");

				int turn = TRUE;
				while(turn){
					playerMove = readOption();

					allocClearBlock(&soap, &gameStatus);

					//llamamos a playerMove 
					printf("Enviando movimiento al servidor..\n");

					if(soap_call_blackJackns__playerMove(&soap, argv[1], NULL, playerName, gameId, playerMove, &gameStatus) == SOAP_OK){
						printf("Movimiento\n");
						printStatus(&gameStatus, TRUE);

						if(gameStatus.code == GAME_WIN){
							printf("Has ganado\n");
							gameFinish = TRUE;
							turn = FALSE;
						}
						else if(gameStatus.code == GAME_LOSE){
							printf("Has perdido\n");
							gameFinish = TRUE;
							turn = FALSE;
						}
						else if(gameStatus.code == TURN_WAIT){
							printf("Te has plantaado. Ahora le toca al rival\n");
							turn = FALSE;
						}
						else if(gameStatus.code == TURN_PLAY){
							printf("Has pedido una carta. Sigue siendo tu turno\n");
							turn = TRUE;
						}
					}
					else{
						printf("ERROR al llamar a playerMove\n");
						soap_print_fault(&soap, stderr);
						turn = FALSE;
						gameFinish = TRUE;
					}
				}
				break;
			case TURN_WAIT:
				//NO te toca, esperamos
				printf("Esperando al rival\n");
				//printStatus(&gameStatus, TRUE);
				sleep(5); //esperar 5 segundos 
				break;
			case GAME_WIN:
				printf("Has ganado\n");
				//printStatus(&gameStatus, TRUE);
				gameFinish = TRUE;
				break;
			case GAME_LOSE:
				printf("Has pedido\n");
				//printStatus(&gameStatus, TRUE);
				gameFinish = TRUE;
				break;
			case ERROR_PLAYER_NOT_FOUND:
				printf("ERROR: Este jugador no se ha encontrado en la partida\n");
				gameFinish = TRUE;
				break;
		}
	}
	allocClearBlock (&soap, &gameStatus);
			
	

	// Clean the environment
	soap_destroy(&soap);
  	soap_end(&soap);
  	soap_done(&soap);
  	return 0;
}
