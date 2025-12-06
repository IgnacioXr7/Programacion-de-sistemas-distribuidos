#include "bmpBlackWhite.h"
#include "mpi.h"

/** Show log messages */
#define SHOW_LOG_MESSAGES 1

/** Enable output for filtering information */
#define DEBUG_FILTERING 0

/** Show information of input and output bitmap headers */
#define SHOW_BMP_HEADERS 0


int main(int argc, char** argv){

	tBitmapFileHeader imgFileHeaderInput;			/** BMP file header for input image */
	tBitmapInfoHeader imgInfoHeaderInput;			/** BMP info header for input image */
	tBitmapFileHeader imgFileHeaderOutput;			/** BMP file header for output image */
	tBitmapInfoHeader imgInfoHeaderOutput;			/** BMP info header for output image */
	char* sourceFileName;							/** Name of input image file */
	char* destinationFileName;						/** Name of output image file */
	int inputFile, outputFile;						/** File descriptors */
	unsigned char *outputBuffer;					/** Output buffer for filtered pixels */
	unsigned char *inputBuffer;						/** Input buffer to allocate original pixels */
	unsigned char *auxPtr;							/** Auxiliary pointer */
	unsigned int rowSize;							/** Number of pixels per row */
	unsigned int rowsPerProcess;					/** Number of rows to be processed (at most) by each worker */
	unsigned int rowsSentToWorker;					/** Number of rows to be sent to a worker process */
	unsigned int receivedRows;						/** Total number of received rows */
	unsigned int threshold;							/** Threshold */
	unsigned int currentRow;						/** Current row being processed */
	unsigned int currentPixel;						/** Current pixel being processed */
	unsigned int outputPixel;						/** Output pixel */
	unsigned int readBytes;							/** Number of bytes read from input file */
	unsigned int writeBytes;						/** Number of bytes written to output file */
	unsigned int totalBytes;						/** Total number of bytes to send/receive a message */
	unsigned int numPixels;							/** Number of neighbour pixels (including current pixel) */
	unsigned int currentWorker;						/** Current worker process */
	unsigned int *processIDs;
	tPixelVector vector;							/** Vector of neighbour pixels */
	int imageDimensions[2];							/** Dimensions of input image */
	double timeStart, timeEnd;						/** Time stamps to calculate the filtering time */
	int size, rank, tag;							/** Number of process, rank and tag */
	MPI_Status status;								/** Status information for received messages */

	// Init
	MPI_Init(&argc, &argv);
	MPI_Comm_size(MPI_COMM_WORLD, &size);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	tag = 1;
	srand(time(NULL));

	// Check the number of processes
	if (size<=2){
		if (rank == 0)
			printf ("This program must be launched with (at least) 3 processes\n");
		MPI_Finalize();
		exit(0);
	}

	// Check arguments
	if (argc != 5){
		if (rank == 0)
			printf ("Usage: ./bmpFilterDynamic sourceFile destinationFile threshold numRows\n");
		MPI_Finalize();
		exit(0);
	}

	// Get input arguments...
	sourceFileName = argv[1];
	destinationFileName = argv[2];
	threshold = (unsigned int) atoi(argv[3]);
	rowsPerProcess = (unsigned int) atoi(argv[4]); // tamaño del grano

	// Allocate memory for process IDs vector (reservado)
	processIDs = (unsigned int *) malloc (size * sizeof(unsigned int));

	// MASTER process
	if (rank == 0){

		// Process starts
		timeStart = MPI_Wtime();

		// Read headers from input file
		readHeaders (sourceFileName, &imgFileHeaderInput, &imgInfoHeaderInput);
		readHeaders (sourceFileName, &imgFileHeaderOutput, &imgInfoHeaderOutput);

		// Write header to the output file
		writeHeaders (destinationFileName, &imgFileHeaderOutput, &imgInfoHeaderOutput);

		// Calculate row size for input and output images
		rowSize = (((imgInfoHeaderInput.biBitCount * imgInfoHeaderInput.biWidth) + 31) / 32 ) * 4;

		// Show info before processing
		if (SHOW_LOG_MESSAGES){
			printf ("[MASTER] Applying dynamic filter to image %s (rowSize=%d, height=%d) with threshold %d. Generating image %s\n",
				sourceFileName, rowSize, imgInfoHeaderInput.biHeight, threshold, destinationFileName);
			printf ("[MASTER] Number of workers: %d -> Grain (rowsPerProcess) = %d\n", size - 1, rowsPerProcess);
		}

		// Show headers...
		if (SHOW_BMP_HEADERS){
			printf ("Source BMP headers:\n");
			printBitmapHeaders (&imgFileHeaderInput, &imgInfoHeaderInput);
			printf ("Destination BMP headers:\n");
			printBitmapHeaders (&imgFileHeaderOutput, &imgInfoHeaderOutput);
		}

		// Open source image
		if ((inputFile = open(sourceFileName, O_RDONLY)) < 0) {
			printf("ERROR: Source file cannot be opened: %s\n", sourceFileName);
			MPI_Finalize();
			exit(1);
		}

		// Open target image
		if ((outputFile = open(destinationFileName, O_WRONLY | O_APPEND, 0777)) < 0) {
			printf("ERROR: Target file cannot be open to append data: %s\n", destinationFileName);
			close(inputFile);
			MPI_Finalize();
			exit(1);
		}

		// Allocate memory to copy the bytes between the header and the image data (cabeceras)
		outputBuffer = (unsigned char*) malloc ((imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE) * sizeof(unsigned char));
		if (outputBuffer == NULL) {
			printf("ERROR: malloc header buffer failed\n");
			close(inputFile);
			close(outputFile);
			MPI_Finalize();
			exit(1);
		}

		// Copy bytes between headers and pixels
		lseek (inputFile, BIMAP_HEADERS_SIZE, SEEK_SET);
		read (inputFile, outputBuffer, imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE);
		write (outputFile, outputBuffer, imgFileHeaderInput.bfOffBits - BIMAP_HEADERS_SIZE);
		free(outputBuffer); // liberamos buffer de cabeceras porque lo volveremos a usar más abajo

		// IMPORTANTE - la altura puede ser negativa
		unsigned int imagenHeight = abs(imgInfoHeaderInput.biHeight);
		unsigned int totalImageBytes = imagenHeight * rowSize; // bytes totales de la imagen

		// send imagen dimensions to workers (broadcast)
		imageDimensions[0] = (int) rowSize;
		imageDimensions[1] = (int) imagenHeight;
		MPI_Bcast(imageDimensions, 2, MPI_INT, 0, MPI_COMM_WORLD);
		if (SHOW_LOG_MESSAGES){
			printf("[MASTER] Broadcast de dimensiones: rowSize=%d, height=%d\n", imageDimensions[0], imageDimensions[1]);
		}

		MPI_Bcast(&threshold, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
		if (SHOW_LOG_MESSAGES){
			printf("[MASTER] Broadcast threshold=%u\n", threshold);
		}

		// malloc para los buffers (imagen completa)
		totalBytes = imagenHeight * rowSize;
		inputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
		outputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));

		if (inputBuffer == NULL || outputBuffer == NULL) {
			printf("ERROR: No se ha hecho bien el malloc() para buffers de imagen\n");
			if (inputBuffer) free(inputBuffer);
			if (outputBuffer) free(outputBuffer);
			close(inputFile);
			close(outputFile);
			MPI_Finalize();
			exit(1);
		}

		// leer la imagen sin cabecera, inicio de los pixeles
		lseek(inputFile, imgFileHeaderInput.bfOffBits, SEEK_SET);
		readBytes = read(inputFile, inputBuffer, totalBytes);

		if (SHOW_LOG_MESSAGES){
			printf("[MASTER] Se han leido %d bytes de la imagen. Se esperaban: %d\n", readBytes, totalBytes);
			if ((unsigned int)readBytes != totalBytes){
				printf("[MASTER] Los bytes leidos NO son iguales a los esperados\n");
			}
		}

		// dynamic, asignación por granos hasta terminar
		unsigned int nextRow = 0;          // siguiente fila no asignada
		unsigned int remaining = imagenHeight; // filas restantes
		receivedRows = 0;                 // filas procesadas y recibidas
		currentWorker = 1;

		// Enviar un primer grano a cada worker 
		for (currentWorker = 1; currentWorker < size && remaining > 0; currentWorker++) {
			rowsSentToWorker = (remaining >= rowsPerProcess) ? rowsPerProcess : remaining;
			// Enviamos a worker: rowsSentToWorker, startRow y los bytes correspondientes
			unsigned int startRow = nextRow;
			unsigned int bytesToSend = rowsSentToWorker * rowSize;

			if (SHOW_LOG_MESSAGES){
				printf("[MASTER] Enviando inicial %d filas (%d bytes) al worker %d (filas %d - %d)\n",
					rowsSentToWorker, bytesToSend, currentWorker, startRow, startRow + rowsSentToWorker - 1);
			}

			// Enviar número de filas
			MPI_Send(&rowsSentToWorker, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
			// Enviar fila inicial  para que worker sepa posición
			MPI_Send(&startRow, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
			// Enviar bloque de píxeles
			MPI_Send(inputBuffer + ((size_t)startRow * rowSize), bytesToSend, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD);

			nextRow += rowsSentToWorker;
			remaining -= rowsSentToWorker;
		}

		// si hay más workers que bloques iniciales, a esos workers enviar rows=0 para terminar
		for (; currentWorker < size; currentWorker++) {
			unsigned int zero = 0;
			MPI_Send(&zero, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
		}

		// bucle principal, recibir bloques procesados y asignar nuevos bloques hasta terminar
		while (receivedRows < imagenHeight) {
			// Primero recibimos meta: startRow y rowsProcessed desde cualquier worker
			unsigned int meta[2];
			MPI_Recv(meta, 2, MPI_UNSIGNED, MPI_ANY_SOURCE, tag, MPI_COMM_WORLD, &status);
			currentWorker = status.MPI_SOURCE;
			unsigned int blockStart = meta[0];   // fila inicial del bloque procesado
			unsigned int blockRows = meta[1];    // filas procesadas en este bloque
			unsigned int blockBytes = blockRows * rowSize;

			// recibimos los bytes filtrados del worker
			MPI_Recv(outputBuffer + ((size_t)blockStart * rowSize), blockBytes, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD, &status);

			receivedRows += blockRows;
			if (SHOW_LOG_MESSAGES) {
				printf("[MASTER] Recibidos %u filas (start=%u) del worker %d. Total recibidas: %u / %u\n",
					blockRows, blockStart, currentWorker, receivedRows, imagenHeight);
			}

			// si quedan filas por asignar, enviamos otro grano a este mismo worker
			if (remaining > 0) {
				rowsSentToWorker = (remaining >= rowsPerProcess) ? rowsPerProcess : remaining;
				unsigned int startRow = nextRow;
				unsigned int bytesToSend = rowsSentToWorker * rowSize;

				if (SHOW_LOG_MESSAGES) {
					printf("[MASTER] Enviando siguiente %d filas (%d bytes) al worker %d (filas %d - %d)\n",
						rowsSentToWorker, bytesToSend, currentWorker, startRow, startRow + rowsSentToWorker - 1);
				}

				MPI_Send(&rowsSentToWorker, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
				MPI_Send(&startRow, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
				MPI_Send(inputBuffer + ((size_t)startRow * rowSize), bytesToSend, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD);

				nextRow += rowsSentToWorker;
				remaining -= rowsSentToWorker;
			} else {
				// No quedan filas, worker termina
				unsigned int zero = 0;
				MPI_Send(&zero, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
				if (SHOW_LOG_MESSAGES) {
					printf("[MASTER] Enviado rows=0 (terminacion) a worker %d\n", currentWorker);
				}
			}
		}

		// todos los datos procesados, se escribe en un buffer de salida en fichero
		lseek(outputFile, imgFileHeaderInput.bfOffBits, SEEK_SET);
		writeBytes = write(outputFile, outputBuffer, totalBytes);

		if (SHOW_LOG_MESSAGES) {
			printf("[MASTER] Se han escrito %d bytes en el outputFile (esperados: %d)\n", writeBytes, totalBytes);
			if ((unsigned int)writeBytes != totalBytes) {
				printf("[MASTER] Los bytes escritos NO son iguales a los esperados\n");
			}
		}

		// liberar memoria y cerrar ficheros
		free(inputBuffer);
		free(outputBuffer);

		close(inputFile);
		close(outputFile);

		// process ends
		timeEnd = MPI_Wtime();
		printf("Filtering time (dynamic): %f\n",timeEnd-timeStart);
	}

	// worker process
	else {
		// recibe Bcast de dimensiones y threshold
		MPI_Bcast(imageDimensions, 2, MPI_INT, 0, MPI_COMM_WORLD);
		MPI_Bcast(&threshold, 1, MPI_UNSIGNED, 0, MPI_COMM_WORLD);
		rowSize = (unsigned int) imageDimensions[0];
		unsigned int imagenHeight = (unsigned int) abs(imageDimensions[1]);

		if (SHOW_LOG_MESSAGES){
			printf("--Worker %d ha recibido las dimensiones: rowSize = %d, height = %d\n", rank, rowSize, imagenHeight);
			printf("--Worker %d ha recibido threshold = %u\n", rank, threshold);
		}

		while (1) {
			// recibe numero de filas a filtrar desde master
			MPI_Recv(&rowsSentToWorker, 1, MPI_UNSIGNED, 0, tag, MPI_COMM_WORLD, &status);

			// Si rowsSentToWorker acaba
			if (rowsSentToWorker == 0) {
				if (SHOW_LOG_MESSAGES) {
					printf("--Worker %d recibe rows=0 -> Terminando\n", rank);
				}
				break;
			}

			// recibe la fila inicial de este bloque
			unsigned int startRow;
			MPI_Recv(&startRow, 1, MPI_UNSIGNED, 0, tag, MPI_COMM_WORLD, &status);

			// malloc para los buffers del bloque
			totalBytes = rowsSentToWorker * rowSize;
			inputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
			outputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
			if (inputBuffer == NULL || outputBuffer == NULL) {
				printf("ERROR: No se ha hecho bien el malloc() en el worker %d\n", rank);
				if (inputBuffer) free(inputBuffer);
				if (outputBuffer) free(outputBuffer);
				MPI_Finalize();
				exit(1);
			}
			if (SHOW_LOG_MESSAGES){
				printf("--Worker %d ha hecho el malloc() para los buffers de %d bytes (startRow=%d)\n", rank, totalBytes, startRow);
			}

			// recibe los datos a filtrar 
			MPI_Recv(inputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD, &status);
			if (SHOW_LOG_MESSAGES){
				printf("--Worker %d ha recibido %d bytes de pixeles (filas: %d - %d)\n", rank, totalBytes, startRow, startRow + rowsSentToWorker - 1);
			}

			// filtrado igual que en el estatico por pixel
			auxPtr = inputBuffer;
			unsigned char *outputPtr = outputBuffer; // puntero para escribir en el buffer de salida
			for (currentRow = 0; currentRow < rowsSentToWorker; currentRow++) {
				// para cada fila
				for (currentPixel = 0; currentPixel < rowSize; currentPixel++) {
					// construir vector: pixel anterior, actual, siguiente
					numPixels = 0;
					// pixel actual
					vector[numPixels] = *auxPtr;
					numPixels++;
					// pixel anterior si no estamos al principio de la fila
					if (currentPixel > 0) {
						vector[numPixels] = *(auxPtr - 1);
						numPixels++;
					}
					// pixel siguiente si no estamos al final de la fila
					if (currentPixel < rowSize - 1) {
						vector[numPixels] = *(auxPtr + 1);
						numPixels++;
					}
					// calcular valor filtrado
					*outputPtr = calculatePixelValue(vector, numPixels, threshold, DEBUG_FILTERING);

					auxPtr++;
					outputPtr++;
				}
			}

			if (SHOW_LOG_MESSAGES){
				printf("--Worker %d ha terminado de filtrar las %d filas (start=%d)\n", rank, rowsSentToWorker, startRow);
			}

			// envia metadata y luego datos filtrados al master
			unsigned int meta[2];
			meta[0] = startRow;
			meta[1] = rowsSentToWorker;
			MPI_Send(meta, 2, MPI_UNSIGNED, 0, tag, MPI_COMM_WORLD);
			MPI_Send(outputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD);

			if (SHOW_LOG_MESSAGES){
				printf("--Worker %d ha enviado %d bytes filtrados al master (start=%d)\n", rank, totalBytes, startRow);
			}

			// liberar buffers temporales del bloque
			free(inputBuffer);
			free(outputBuffer);
		} 
	} 

	if (processIDs) free(processIDs);
	MPI_Finalize();
	return 0;
}