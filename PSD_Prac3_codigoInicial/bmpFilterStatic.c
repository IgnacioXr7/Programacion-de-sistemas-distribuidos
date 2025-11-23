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
	unsigned char *outputBuffer;					/** BUFFER PARA PIXELES FILTRADOS */
	unsigned char *inputBuffer;						/** BUFFER PARA PIXELES ORIGINALES */
	unsigned char *auxPtr;							/** PUNTERO AUXILIAR PARA MOVERME POR EL BUFFER */
	unsigned int rowSize;							/** BYTER POR FILA (INCLUYE PADDING BMP) */
	unsigned int rowsPerProcess;					/** FILAS QUE PROCESA CADA WORKER */
	unsigned int rowsSentToWorker;					/** Number of rows to be sent to a worker process */
	unsigned int threshold;							/** Threshold */
	unsigned int currentRow;						/** Current row being processed */
	unsigned int currentPixel;						/** Current pixel being processed */
	unsigned int outputPixel;						/** Output pixel */
	unsigned int readBytes;							/** Number of bytes read from input file */
	unsigned int writeBytes;						/** Number of bytes written to output file */
	unsigned int totalBytes;						/** Total number of bytes to send/receive a message */
	unsigned int numPixels;							/** Number of neighbour pixels (including current pixel) */
	unsigned int currentWorker;						/** Current worker process */
	tPixelVector vector;							/** Vector of neighbour pixels */
	int imageDimensions[2];							/** [0] = rowSize [1] = height */
	double timeStart, timeEnd;						/** Time stamps to calculate the filtering time */
	int size, rank, tag;							/** NUMERO DE PROCESOS QUE PASAMOS POR ARGV, rank and tag */
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
		if (argc != 4){

			if (rank == 0)
				printf ("Usage: ./bmpFilterStatic sourceFile destinationFile threshold\n");

			MPI_Finalize();
			exit(0);
		}

		// Get input arguments...
		sourceFileName = argv[1];
		destinationFileName = argv[2];
		threshold = atoi(argv[3]);


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
		
			// Show headers...
			if (SHOW_BMP_HEADERS){
				printf ("Source BMP headers:\n");
				printBitmapHeaders (&imgFileHeaderInput, &imgInfoHeaderInput);
				printf ("Destination BMP headers:\n");
				printBitmapHeaders (&imgFileHeaderOutput, &imgInfoHeaderOutput);
			}

			// Open source image
			if((inputFile = open(sourceFileName, O_RDONLY)) < 0){
				printf("ERROR: Source file cannot be opened: %s\n", sourceFileName);
				exit(1);
			}

			// Open target image
			if((outputFile = open(destinationFileName, O_WRONLY | O_APPEND, 0777)) < 0){
				printf("ERROR: Target file cannot be open to append data: %s\n", destinationFileName);
				exit(1);
			}

			// Allocate memory to copy the bytes between the header and the image data
			outputBuffer = (unsigned char*) malloc ((imgFileHeaderInput.bfOffBits-BIMAP_HEADERS_SIZE) * sizeof(unsigned char));

			// Copy bytes between headers and pixels
			lseek (inputFile, BIMAP_HEADERS_SIZE, SEEK_SET);
			read (inputFile, outputBuffer, imgFileHeaderInput.bfOffBits-BIMAP_HEADERS_SIZE);
			write (outputFile, outputBuffer, imgFileHeaderInput.bfOffBits-BIMAP_HEADERS_SIZE);

			//IMPORTANTE - la altura puede ser negativa 
			unsigned int imagenHeight = abs(imgInfoHeaderInput.biHeight);
			unsigned int totalImageBytes = imagenHeight * rowSize;// esto es fijo 

			//send imagen dimensions to workers 
			imageDimensions[0] = rowSize;
			imageDimensions[1] = imagenHeight;
			MPI_Bcast(imageDimensions, 2, MPI_INT, 0, MPI_COMM_WORLD);
			if(SHOW_LOG_MESSAGES){
				printf("Proceso master ha hecho BROADCAST las dimensiones de la imagen a los workers: %d , %d\n", imageDimensions[0], imageDimensions[1]);
			}

			//malloc para los buffers (row * col de la imagen)
			unsigned int totalBytes = imagenHeight * rowSize;
			inputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
			outputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));

			if(inputBuffer == NULL || outputBuffer == NULL){
				printf("ERROR: No se ha hecho bien el malloc()\n");
				MPI_Finalize();
				exit(1);
			}

			//calculamos las filas que procesa cada worker
			rowsPerProcess = imagenHeight / (size - 1);
			unsigned int resto = imagenHeight % (size - 1);
			
			if(SHOW_LOG_MESSAGES){
				printf("%d workers van a procesas %d filas cada uno\n", size - 1, rowsPerProcess);
				if(resto > 0){
					printf("%d extra filas\n", resto);
				}
			}
			
			//leer la imagen SIN LA CABECERA, inicio de los pixeles
			lseek(inputFile, imgFileHeaderInput.bfOffBits, SEEK_SET);
			readBytes = read(inputFile, inputBuffer, totalBytes);

			if(SHOW_LOG_MESSAGES){
				printf("Se han leido %d bytes de la imagen. Se esperaban: %d\n", readBytes, totalBytes);
				if(readBytes != totalBytes){
					printf("Los bytes leidos NO son iguales a los esperados\n");
				}
			}

			//ENVIAMOS DATOS A LOS WORKERS 
			auxPtr = inputBuffer; //puntero al inicio de buffer de entrada
			currentRow = 0;

			for(currentWorker = 1; currentWorker < size; currentWorker++){
				rowsSentToWorker = rowsPerProcess;

				if(currentWorker <= resto){
					rowsSentToWorker++;
				}

				totalBytes = rowsSentToWorker * rowSize;

				if(SHOW_LOG_MESSAGES){
					printf("Enviando %d filas (%d bytes) al worker %d (filas %d - %d)\n", 
						rowsSentToWorker, totalBytes, currentWorker, currentRow, currentRow + rowsSentToWorker - 1);
				}

				//ENVIAR
				MPI_Send(&rowsSentToWorker, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
				MPI_Send(&threshold, 1, MPI_UNSIGNED, currentWorker, tag, MPI_COMM_WORLD);
				MPI_Send(auxPtr, totalBytes, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD);

				auxPtr += totalBytes;
				currentRow += rowsSentToWorker;
			}

			//RECIBIMOS LOS DATOS 
			auxPtr = outputBuffer; //reset del puntero al inicio del buffer de salida
			currentRow = 0;

			for(currentWorker = 1; currentWorker < size; currentWorker++){
				rowsSentToWorker = rowsPerProcess;

				if(currentWorker <= resto){
					rowsSentToWorker++;
				}

				totalBytes = rowsSentToWorker * rowSize;

				//RECIBIR
				MPI_Recv(auxPtr, totalBytes, MPI_UNSIGNED_CHAR, currentWorker, tag, MPI_COMM_WORLD, &status);

				if(SHOW_LOG_MESSAGES){
					printf("Recibidos %d filas del worker %d (filas %d - %d)\n", rowsSentToWorker, currentWorker, currentRow, currentRow + rowsSentToWorker - 1);
				}

				auxPtr += totalBytes;
				currentRow += rowsSentToWorker;
			}

			writeBytes = write(outputFile, outputBuffer, totalImageBytes);

			if(SHOW_LOG_MESSAGES){
				printf("Se han escrito %d bytes en el outputFile (esperados: %d)\n", writeBytes, totalImageBytes);
				if(writeBytes != totalImageBytes){
					printf("Los bytes escritos NO son iguales a los esperados\n");
				}
			}

			//liberar men
			free(inputBuffer);
			free(outputBuffer);

			// Close files
			close (inputFile);
			close (outputFile);

			// Process ends
			timeEnd = MPI_Wtime();

			// Show processing time
			printf("Filtering time: %f\n",timeEnd-timeStart);
		}


		// Worker process
		else{
			//RECIBE Bcast
			MPI_Bcast(imageDimensions, 2, MPI_INT, 0, MPI_COMM_WORLD);
			rowSize = imageDimensions[0];
			unsigned int imagenHeight = abs(imageDimensions[1]);
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha recibido las dimensiones: rowSize = %d, height = %d\n", 
				rank, rowSize, imagenHeight);
			}

			//RECIBE numero de filas a filtrar
			MPI_Recv(&rowsPerProcess, 1, MPI_UNSIGNED, 0, tag, MPI_COMM_WORLD, &status);
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d va a procesar %d filas\n", rank, rowsPerProcess);
			}

			//RECIBE threshold
			MPI_Recv(&threshold, 1, MPI_UNSIGNED, 0, tag, MPI_COMM_WORLD, &status);
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha recibido el threshold: %d\n", rank, threshold);
			}

			//malloc para los buffers (row * col de la imagen)
			totalBytes = rowsPerProcess * rowSize;
			inputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
			outputBuffer = (unsigned char*) malloc(totalBytes * sizeof(unsigned char));
			if(inputBuffer == NULL || outputBuffer == NULL){
				printf("ERROR: No se ha hecho bien el malloc() en el worker %d\n", rank);
				MPI_Finalize();
				exit(1);
			}
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha hecho el malloc() para los buffers de %d bytes\n", rank, totalBytes);
			}

			//RECIBE los datos a filtrar
			MPI_Recv(inputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD, &status);
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha recibido %d bytes de pixeles\n", rank, totalBytes);
			}

			//AQUI VA EL FILTRADO
			auxPtr = inputBuffer;
			unsigned char *outputPtr = outputBuffer; //ptr para escribir en el buffer de salida
			for(currentRow = 0; currentRow < rowsPerProcess; currentRow++){
				//para cada fila
				for(currentPixel = 0; currentPixel < rowSize; currentPixel++){
					//para cada pixel de la fila
					//construir vector, array con el pixel ANTERIOR, el actual y el SIGUIENTE
					numPixels = 0;
					//pixel actual 
					vector[numPixels] = *auxPtr;
					numPixels++;
					//pixel anterior, comprobar si no estoy al principio de la fila
					if(currentPixel > 0){
						vector[numPixels] = *(auxPtr - 1);
						numPixels++;
					}
					//pixel siguiente, comprobar si no estoy al final de la fila
					if(currentPixel < rowSize - 1){
						vector[numPixels] = *(auxPtr + 1);
						numPixels++;
					}
					//calcular el valor del pixel filtrado
					*outputPtr = calculatePixelValue(vector, numPixels, threshold, DEBUG_FILTERING);

					auxPtr++;
					outputPtr++;
				}
			}
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha terminado de filtrar las %d filas\n", rank, rowsPerProcess);
			}

			//ENVIA el cacho filtrados al master
			MPI_Send(outputBuffer, totalBytes, MPI_UNSIGNED_CHAR, 0, tag, MPI_COMM_WORLD);
			if(SHOW_LOG_MESSAGES){
				printf("--Worker %d ha enviado %d bytes de pixeles filtrados al master\n", rank, totalBytes);
			}

			free(inputBuffer);
			free(outputBuffer);
		}

		// Finish MPI environment
		MPI_Finalize();
}
