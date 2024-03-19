//Jacob Werner
//https://github.com/JakeWerner/


#include "multi-lookup.h"

//Helper function that the requester threads use to output the serviced information to performance.txt and serviced.txt
void reqServiced(int serviceCount, void *inStruct){
	char serviced[100];
	gloThread *bigStruct = inStruct;
	snprintf(serviced, 100, "Thread %ld serviced %d files\n", pthread_self(), serviceCount); //snprintf allows us to limit the number of bytes transferred
	//Put thread service count into each file
	fputs(serviced, bigStruct->performance);
	fputs(serviced, bigStruct->serviced);
}

void *requester(void *inStruct){
	int i;
	int processed = 0;
	char hostname[MAX_NAME_LENGTH];
	FILE *input;
	gloThread *bigStruct = inStruct;

	//Loop until there are no input files left
	while (bigStruct->inputCounter < bigStruct->inputFileNum){
		pthread_mutex_lock(&bigStruct->inLock);
		//Loop to catch any loose threads
		if(bigStruct->inputCounter >= bigStruct->inputFileNum){
			pthread_mutex_unlock(&bigStruct->inLock);

			pthread_mutex_lock(&bigStruct->servLock);
			reqServiced(processed, bigStruct);
			pthread_mutex_unlock(&bigStruct->servLock);
			return 0;
		}

		//Open the input file and ensure it opened correctly. If it did, increment the processed counter
		input = fopen(bigStruct->inputFiles[bigStruct->inputCounter], "r");
		if(!input){
			fprintf(stderr, "Input file cannot open");
			return 0;
		}
		bigStruct->inputCounter++;
		processed++;
		pthread_mutex_unlock(&bigStruct->inLock);

		//Once the thread has a file, this loop will allow the thread to traverse the file
		while(fgets(hostname, MAX_NAME_LENGTH, input)){
			//If buffer is full, wait
			while(bigStruct->buffCount == ARRAY_SIZE){
				//If the arrayis not full, break
				if(bigStruct->buffCount < ARRAY_SIZE) break;
			}

			pthread_mutex_lock(&bigStruct->buffWriteLock);
			//Check if the buffer is full, if not then loop through all the elements of buffFull and find one that is false, then get the buffer index of the next not full slot
			if(bigStruct->buffCount < ARRAY_SIZE){
				for(i = 0; i < ARRAY_SIZE; i++){
					if(bigStruct->buffFull[i] == false) break;
				}
				//Copy hostname and and add it the buffer at the index
				strcpy(bigStruct->sharedBuff[i], hostname);
				bigStruct->buffCount++;
				bigStruct->buffFull[i] = true;
				
				
			}
			pthread_mutex_unlock(&bigStruct->buffWriteLock);
		}
		fclose(input);
	} 
	//Write the service information to serviced
	pthread_mutex_lock(&bigStruct->servLock);
	reqServiced(processed, bigStruct);
	pthread_mutex_unlock(&bigStruct->servLock);

	return 0;
}

void *resolver(void *inStruct){
	int i;
	int length;
	char hostname[MAX_NAME_LENGTH];
	char host[MAX_NAME_LENGTH];
	char ip[INET6_ADDRSTRLEN];
	gloThread *bigStruct = inStruct;
	//Loop until the requester threads are done
	while(!bigStruct->writeDone){
		//If the buffer is empty, wait until a requester thread puts hostnames into the buffer
		while(bigStruct->buffCount == 0){
			//wait
			if(bigStruct->writeDone) break;
		}
		if(bigStruct->writeDone) break;

		//Increment filesProcessed. Must be in a mutex lock so it scales linearly 
		pthread_mutex_lock(&bigStruct->buffCountLock);
		bigStruct->filesProcessed++;
		//If filesProcessed == 1, meaning this is the first resolver thread, lock the writing until all reads are done
		if(bigStruct->filesProcessed == 1){
			pthread_mutex_lock(&bigStruct->buffWriteLock);
		}
		pthread_mutex_unlock(&bigStruct->buffCountLock);

		//Find a buffer location that is full and take the data, lock to make sure only one thread can do that at a time to prevent duplication
		pthread_mutex_lock(&bigStruct->buffReadLock);
		if(bigStruct->buffCount > 0){
			for(i = 0; i < ARRAY_SIZE; i++){
				if(bigStruct->buffFull[i] == true){
					break;
				}
			}

			//Update the buffer info after the thread finds and takes the data
			bigStruct->buffCount--;
			bigStruct->buffFull[i] = false;
			strcpy(hostname, bigStruct->sharedBuff[i]);
		}
		pthread_mutex_unlock(&bigStruct->buffReadLock);

		//Perform the DNS lookup, format the string to get rid of the newline and then do it
		//This part can be performed simultaneously among threads as the threads are dealing with their own data and no chance of race condition at this point
		length = strlen(hostname);
		hostname[length - 1] = '\0';
		strcpy(host, hostname); //Did this cause my threads were mysteriously getting rid of the name when trying to put it into results.txt
		if(dnslookup(hostname, ip, MAX_NAME_LENGTH) != UTIL_SUCCESS){
			fprintf(stderr, "Could not resolve %s\n", hostname);
			strcat(hostname, ",\n");
		}
		else{ //If dnslookup is a success
			strcat(hostname, host); //strcat appends to a string
			strcat(hostname, ",");
			strcat(hostname, ip);
			strcat(hostname, "\n");
		}

		//Write the result to the results file, lock to prevent race condition or other weird behavior
		pthread_mutex_lock(&bigStruct->fileLock);
		fputs(hostname, bigStruct->output);
		pthread_mutex_unlock(&bigStruct->fileLock);

		//Decrement filesProcessed to communicate to other threads what is done and what isnt
		pthread_mutex_lock(&bigStruct->buffCountLock);
		bigStruct->filesProcessed--;
		//If filesProcessed == 0, meaning that the thread is the last resolver, then the thread will allow writing to the buffer, meaning requesters can start back up
		if(bigStruct->filesProcessed == 0){
			pthread_mutex_unlock(&bigStruct->buffWriteLock);
		}
		pthread_mutex_unlock(&bigStruct->buffCountLock);
	}

	//This chunk of code deals with when the requester threads are all done and it is just the resolver threads working.
	//Code is mostly identical minus the mutex locks that communicate with the requester threads since there are none
	pthread_mutex_lock(&bigStruct->buffReadLock);
	if(!bigStruct->reqDone){
		for(i = 0; i < ARRAY_SIZE; i++){
			if(bigStruct->buffFull[i] == true){
				strcpy(hostname, bigStruct->sharedBuff[i]);
				length = strlen(hostname);
				//hostname[strcspn(hostname, "\n")] = 0;
				hostname[length-1] = '\0';
				strcpy(host, hostname);
				if(dnslookup(hostname, ip, MAX_NAME_LENGTH) != UTIL_SUCCESS){
				fprintf(stderr, "Could not resolve %s\n", hostname);
				strcat(hostname, ",\n");
				}
				else{
					strcat(hostname, host);
					strcat(hostname, ",");
					strcat(hostname, ip);
					strcat(hostname, "\n");
				}
				fputs(hostname, bigStruct->output);
			}
		}
		bigStruct->reqDone = true;
	}	
	pthread_mutex_unlock(&bigStruct->buffReadLock);
	return 0;
}

//
//------------------------------------------------------------------
//MAIN

int main(int argc, char *argv[]){
	//Stuff for keeping track of da time
	struct timeval startTime;
	gettimeofday(&startTime, NULL);
	time_t sTime = startTime.tv_sec; //Use this to grab the seconds data from the timeval struct

	if(argc > 5 + MAX_INPUT_FILES){
		fprintf(stderr, "Too many arguments\n");
		return 1;
	}
	int i = 0;
	gloThread *bigStruct = calloc(1, sizeof(gloThread)); //Use calloc cause I like it better and it is less sketch

	//Initializing variables
	bigStruct->inputFileNum = 0;
	bigStruct->inputCounter = 0;
	bigStruct->buffCount = 0;
	bigStruct->writeDone = 0;
	bigStruct->reqDone = false;
	bigStruct->filesProcessed = 0;
	for(i = 0; i < ARRAY_SIZE; i++){
		bigStruct->buffFull[i] = false;
	}
	pthread_mutex_init(&bigStruct->inLock, NULL);
	pthread_mutex_init(&bigStruct->buffWriteLock, NULL);
	pthread_mutex_init(&bigStruct->buffCountLock, NULL);
	pthread_mutex_init	

	(&bigStruct->fileLock, NULL);
	pthread_mutex_init(&bigStruct->buffReadLock, NULL);
	pthread_mutex_init(&bigStruct->servLock, NULL);

	//Get the desired requester and resolver thread counts and ensure they are below the maximuum threshhold
	int requesterCount = atoi(argv[1]);
	if(requesterCount > MAX_REQUESTER_THREADS){
		fprintf(stderr, "Can only have up to %d request threads\n", MAX_REQUESTER_THREADS);
		return 1;
	}

	int resolverCount = atoi(argv[2]);
	if(resolverCount > MAX_RESOLVER_THREADS){
		fprintf(stderr, "Can only have up to %d resolve threads\n", MAX_RESOLVER_THREADS);
		return 1;
	}

	//Open the files that will be written to and check that they are valid
	bigStruct->serviced = fopen(argv[3], "w");
	if(!bigStruct->serviced){
		fprintf(stderr, "Invalid service file path\n");
		return 1;
	}
	bigStruct->output = fopen(argv[4], "w");
	if(!bigStruct->output){
		fprintf(stderr, "Invalid output file path\n");
		return 1;
	}
	bigStruct->performance = fopen("performanceOutput.txt", "w");
	if(!bigStruct->performance){
		fprintf(stderr, "Failed to open performanceOutput.txt\n");
		return 1;
	}
	fprintf(bigStruct->performance, "Number of requester threads is %d\n", requesterCount);
	fprintf(bigStruct->performance, "Number of resolver threads is %d\n", resolverCount);

	//Create a folder directory where we will story all of the input files and we can get a count of how many input files we have
	DIR *directory;
	struct dirent *directoryStruct;
	i = 0;
	
	directory = opendir(argv[5]); //Open the file labeled at argument 5
	if(directory){
		while((directoryStruct = readdir(directory)) != NULL){ //While the directory can be read, read the files and put them into the inputFiles array
			if(strcmp(directoryStruct->d_name, ".") && strcmp(directoryStruct->d_name, "..")) {
				strcpy(bigStruct->inputFiles[i], argv[5]); //Append argv5 to filepath
				strcat(bigStruct->inputFiles[i], "/"); //Append / to filepath
				strcat(bigStruct->inputFiles[i], directoryStruct->d_name); //Append file name to file path
				i++; //Increment
			}
		}
		//Get the final inputFileCount and then close the directory
		bigStruct->inputFileNum = i;
		closedir(directory);
	}
	else{
		fprintf(stderr, "Input directory invalid\n");
		return 1;
	}
	
	//Create threadtype arrays
	pthread_t requesterThreads[requesterCount];
	pthread_t resolverThreads[resolverCount];

	//Create requester threads
	for(i = 0; i < requesterCount; i++){
		pthread_create(&requesterThreads[i], NULL, requester, (void*) bigStruct);
	}

	//Create resolver threads
	for(i = 0; i < resolverCount; i++){
		pthread_create(&resolverThreads[i], NULL, resolver, (void*) bigStruct);
	}

	//Join requester threads
	for(i = 0; i < requesterCount; i++){
		pthread_join(requesterThreads[i], NULL);
	}

	//Once all of the requesters are done, change this value so the resolvers know there is no more additional files coming
	bigStruct->writeDone = true;

	//Join resolver threads
	for(i = 0; i < resolverCount; i++){
		pthread_join(resolverThreads[i], NULL);
	}

	//Close serviced and result files
	fclose(bigStruct->serviced);
	fclose(bigStruct->output);
	
	//DESTROY THE MUTICES
	pthread_mutex_destroy(&bigStruct->inLock);
	pthread_mutex_destroy(&bigStruct->buffWriteLock);
	pthread_mutex_destroy(&bigStruct->buffCountLock);
	pthread_mutex_destroy(&bigStruct->fileLock);
	pthread_mutex_destroy(&bigStruct->buffReadLock);
	pthread_mutex_destroy(&bigStruct->servLock);

	
	//Get the time of day at the end of the program and calculate the time
	gettimeofday(&startTime, NULL);
	time_t eTime = startTime.tv_sec;
	//Print execution times to console and to performance.txt
	printf("Execution Time: %ld seconds\n", eTime-sTime);
	fprintf(bigStruct->performance, "./multi-lookup: total time is %lu seconds\n\n", eTime-sTime);

	//Close performance and free bigStruct
	fclose(bigStruct->performance);
	free(bigStruct);
	return 0;
