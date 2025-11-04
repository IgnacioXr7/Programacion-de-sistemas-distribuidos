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
		// Bucle de registro hasta que sea exitoso
	do {
		printf("Introduce tu nombre: ");
		fgets(playerName.msg, STRING_LENGTH - 1, stdin);
		playerName.__size = strlen(playerName.msg);
		playerName.msg[playerName.__size - 1] = '\0'; // eliminar salto de línea

		printf("Registrando el nombre en servidor...\n");
		resCode = soap_call_blackJackns__register(&soap, serverURL, "", playerName, &gameId);

		// Interpretar el código devuelto por el servidor
		if (gameId == ERROR_SERVER_FULL) {
			printf("Servidor lleno. Por favor, espere o intente más tarde.\n");
		} 
		else if (gameId == ERROR_NAME_REPEATED) {
			printf("Nombre ya usado. Elija otro nombre.\n");
		} 
	} while (gameId == ERROR_SERVER_FULL || gameId == ERROR_NAME_REPEATED);
	printf("Se ha registrado el jugador %s en el juego %d\n", playerName.msg, gameId);
	printf("Bienvenido %s\n", playerName.msg);
	while(!gameFinish){
		//bucle principal del cliente 
		allocClearBlock(&soap, &gameStatus);

		if (soap_call_blackJackns__getStatus(&soap, argv[1], NULL, playerName, gameId, &gameStatus) == SOAP_OK) {
			printf("%s\n", gameStatus.msgStruct.msg);
		} else {
			soap_print_fault(&soap, stderr);
			gameFinish = TRUE;
			break;
		}
		
		printStatus(&gameStatus, TRUE);

        switch (gameStatus.code) {
            case TURN_PLAY:
                do {
                    playerMove = readOption();
                    if (soap_call_blackJackns__playerMove(&soap, serverURL, "", playerName, gameId, playerMove, &gameStatus) != SOAP_OK) {
                        soap_print_fault(&soap, stderr);
                        gameFinish = TRUE;
                        break;
                    }
                    printStatus(&gameStatus, TRUE);
                } while (gameStatus.code == TURN_PLAY);
                break;

            case TURN_WAIT:
                printf("Esperando al rival...\n");
                break;

            case GAME_WIN:
                printf("¡Has ganado!\n");
                gameFinish = TRUE;
                break;

            case GAME_LOSE:
                printf("Has perdido.\n");
                gameFinish = TRUE;
                break;

            default:
                printf("Código desconocido: %d\n", gameStatus.code);
                gameFinish = TRUE;
                break;
        }
    }

    soap_destroy(&soap);
    soap_end(&soap);
    soap_done(&soap);
    return 0;
}
