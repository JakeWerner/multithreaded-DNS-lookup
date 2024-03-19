//Jacob Werner
//https://github.com/JakeWerner/


#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <stdbool.h>
#include <sys/time.h>
#include "util.h"

#define ARRAY_SIZE 20
#define MAX_INPUT_FILES 10
#define MAX_RESOLVER_THREADS 10
#define MAX_REQUESTER_THREADS 5
#define MAX_NAME_LENGTH 1025
#define MAX_IP_LENGTH INET6_ADDRSTRLEN

typedef struct gloThread{
	//Code to handle input files and keeps track of the counts
	int inputFileNum; //Keeps track of the number of input files
	int inputCounter; //Keeps track of the number of inputs
	int filesProcessed; //Counts the number of files currently processed and ready for resolvers
	int buffCount; //Keeps track of the size of the buffer
	char inputFiles[MAX_INPUT_FILES][25]; //Storage for the input file directory
	char sharedBuff[ARRAY_SIZE][MAX_NAME_LENGTH]; //The buffer
	bool buffFull[ARRAY_SIZE]; //An array that keeps track if the buffer at an index is filled or not
	bool writeDone; //Shows whether writing to the buffer is complete
	bool reqDone; //Shows whether the requestors have completed or not, so the resolvers know whether to flush the buffer or not
	FILE *serviced; //Service file
	FILE *output; //Output file
	FILE *performance; //Performance file
	pthread_mutex_t inLock; //Lock for input files
	pthread_mutex_t buffWriteLock; //Lock for writing to the buffer
	pthread_mutex_t buffCountLock; //Lock for buffCount
	pthread_mutex_t fileLock; //Lock for writing to the output file
	pthread_mutex_t buffReadLock; //Lock for resolver's reading from the buffer
	pthread_mutex_t servLock; //Lock for the service and performance files
	
} gloThread;