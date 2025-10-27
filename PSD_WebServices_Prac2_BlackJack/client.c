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
	}

	printf("Consultando estado...\n");

    if (soap_call_blackJackns__getStatus(&soap, argv[1], NULL, playerName, gameId, &gameStatus) == SOAP_OK) {
        printf("Mensaje del servidor: %s\n", gameStatus.msgStruct.msg);
    } else {
        soap_print_fault(&soap, stderr);
    }


	allocClearBlock (&soap, &gameStatus);
			
	

	// Clean the environment
	soap_destroy(&soap);
  	soap_end(&soap);
  	soap_done(&soap);
  	return 0;
}
