/* Reference code that searches files for a user-specified string. 
 * 
 * Author: Naga Kandasamy, Michael Lui
 * Date: 7/24/2017
 *
 * Ryan Lanphear + Daniel Lopez
 *
 * Compile the code as follows: gcc -o mini_grep mini_grep.c queue_utils.c -std=c99 -lpthread -Wall
 *
*/

#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>
#include <dirent.h>
#include <string.h>
#include "queue.h"
#include <pthread.h>

typedef struct split_queue{
   queue_element_t *new_queue;
   queue_element_t *remainder_queue;
} split_queue;

typedef struct static_data{
   queue_element_t *file_head;
   char* search_string;
} static_data;

typedef struct dynamic_data{
   queue_t *files;
   char* search_string;
} dynamic_data;

split_queue* q_split(queue_element_t*, int);

int serialSearch(char **);
int parallelSearchStatic(char **);
int parallelSearchDynamic(char **);
void* static_thread(void*);
void* dynamic_thread(void*);
void queue_work(DIR*, queue_element_t*, queue_t*, struct dirent*, struct dirent*, int);

pthread_mutex_t mutex;
pthread_mutex_t queue_mutex;

int total_occurrences = 0;

int
main(int argc, char** argv)
{
	if(argc < 5){
		printf("\n %s <search string> <path> <num threads> static (or)\
			   %s <search string> <path> <num threads> dynamic \n", argv[0], argv[0]);
		exit(EXIT_SUCCESS);
	}


	int num_occurrences;

	/* Start the timer. */
   	struct timeval start, stop; 
   	gettimeofday(&start, NULL);

	/* Perform a serial search of the file system. */
   	num_occurrences = serialSearch(argv);
   	printf("\n The string %s was found %d times within the file system.", argv[1], num_occurrences);

   	gettimeofday(&stop, NULL);
   	printf("\n Overall execution time = %fs.", \
   		   (float)(stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/(float)1000000));


   	/* Perform a multi-threaded search of the file system. */
   	if(strcmp(argv[4], "static") == 0){
		printf("\n Performing multi-threaded search using static load balancing.");
	 	num_occurrences = parallelSearchStatic(argv);
	 	printf("\n The string %s was found %d times within the file system.", argv[1], num_occurrences);

  	}
  	else if(strcmp(argv[4], "dynamic") == 0){
		printf("\n Performing multi-threaded search using dynamic load balancing.");
	 	num_occurrences = parallelSearchDynamic(argv);
	 	printf("\n The string %s was found %d times within the file system.", argv[1], num_occurrences);

	}	
  	else{
		printf("\n Unknown load balancing option provided. Defaulting to static load balancing.");
	 	num_occurrences = parallelSearchStatic(argv);
	 	printf("\n The string %s was found %d times within the \file system.", argv[1], num_occurrences);

  	}


	printf("\n");
  	exit(EXIT_SUCCESS);
}


int 
serialSearch(char **argv)			
{
	/* Serial search of the file system starting from the specified path name. */

   	int num_occurrences = 0;
   	queue_element_t *element, *new_element;
   	struct stat file_stats;
   	int status; 
   	DIR *directory = NULL;
   	struct dirent *result = NULL;
   	struct dirent *entry = (struct dirent *)malloc(sizeof(struct dirent) + MAX_LENGTH);

   	/* Create and initialize the queue data structure. */
   	queue_t *queue = createQueue(); 
   	element = (queue_element_t *)malloc(sizeof(queue_element_t));
	if(element == NULL){
		perror("malloc");
		exit(EXIT_FAILURE);
  	}
   	strcpy(element->path_name, argv[2]);
   	element->next = NULL;

   	/* Insert the initial path name into the queue. */
   	insertElement(queue, element);

	/* While there is work in the queue, process it. */
   	while(queue->head != NULL){	
	 	queue_element_t *element = removeElement(queue);

	 	/* Obtain information about the file. */
	 	status = lstat(element->path_name, &file_stats); 
	 	if(status == -1){
			printf("Error obtaining stats for %s \n", element->path_name);
			free((void *)element);
			continue;
	 	}

	 	if(S_ISLNK(file_stats.st_mode)){ 
	 		/* Ignore symbolic links. */
	 	} 
	 	else if(S_ISDIR(file_stats.st_mode)){ 
	 		/* If directory, descend in and post work to queue. */

			printf("%s is a directory. \n", element->path_name);
			directory = opendir(element->path_name);
			if(directory == NULL){
				printf("Unable to open directory %s \n", element->path_name);
				continue;
			}
			while(1){
				/* Read directory entry. */
				status = readdir_r(directory, entry, &result); 
				if(status != 0){
				 	printf("Unable to read directory %s \n", element->path_name);
					break;
				}

				/* End of directory. */
			  	if(result == NULL)				
				 	break;									  
				
				/* Ignore the "." and ".." entries. */
				if(strcmp(entry->d_name, ".") == 0)	
					continue;
				if(strcmp(entry->d_name, "..") == 0)
					continue;

				/* Insert this directory entry in the queue. */
				new_element = (queue_element_t *)malloc(sizeof(queue_element_t));
				if(new_element == NULL){
				 	printf("Error allocating memory. Exiting. \n");
					exit(-1);
				}
				
	  			/* Construct the full path name for the directory item stored in entry. */
			 	strcpy(new_element->path_name, element->path_name);
				strcat(new_element->path_name, "/");
				strcat(new_element->path_name, entry->d_name);
				insertElement(queue, new_element);
			}
			closedir(directory);
	 	} 
	 	else if(S_ISREG(file_stats.st_mode)){
	 		/* Directory entry is a regular file. */
			
			printf("%s is a regular file. \n", element->path_name);
			FILE *file_to_search;
			char buffer[MAX_LENGTH];
			char *bufptr, *searchptr, *tokenptr;
				
			/* Search the file for the search string provided as the command-line argument. */ 
			file_to_search = fopen(element->path_name, "r");
			if(file_to_search == NULL){
				printf("Unable to open file %s \n", element->path_name);
				continue;
			} 
			else{
				while(1){
					/* Read in a line from the file. */
					bufptr = fgets(buffer, sizeof(buffer), file_to_search);
					if(bufptr == NULL){
						if(feof(file_to_search)) break;
						if(ferror(file_to_search)){
							printf("Error reading file %s \n", element->path_name);
							break;
						}
					}
									 
					/* Break up line into tokens and search each token. */ 
					tokenptr = strtok(buffer, " ,.-");
					while(tokenptr != NULL){
						searchptr = strstr(tokenptr, argv[1]);
						if(searchptr != NULL){
							printf("Found string %s within \
								   file %s. \n", argv[1], element->path_name);
							num_occurrences ++;
						}
												
						tokenptr = strtok(NULL, " ,.-");						/* Get next token from the line. */
					}
				}
			}
			fclose(file_to_search);
		}
		else{
			printf("%s is of type other. \n", element->path_name);
		}
		free((void *)element);
	}

  	return num_occurrences;
}

int 
parallelSearchStatic(char **argv)
{
	/* Parallel search with static load balancing accross threads. */

	struct timeval start, stop;
	gettimeofday(&start, NULL);

	/* Initialize variables and memory for queue and total files */
	int num_files = 0;
	queue_element_t *element;
	struct stat file_stats;
	struct dirent *result = NULL;
	struct dirent *entry = (struct dirent *)malloc(sizeof(struct dirent)+MAX_LENGTH);
	int status; 
	DIR *directory = NULL;

	queue_t *dir_queue = createQueue();
	queue_t *files = createQueue();
	element = (queue_element_t *)malloc(sizeof(queue_element_t));

	if(element == NULL){ // malloc error handling
		perror("malloc");
		exit(EXIT_FAILURE);
  	}

   	strcpy(element->path_name, argv[2]);
   	element->next = NULL;
   	insertElement(dir_queue, element);

   	/* Build up the queue */ 
   	while(dir_queue->head != NULL){
	 	queue_element_t *element = removeElement(dir_queue);
	 	status = lstat(element->path_name, &file_stats);

	 	if(status == -1){ 
	 		/* Error handling for file parsing */
			printf("Error obtaining stats for %s \n", element->path_name);
			free((void *)element);
			continue;
	 	}

	 	if(S_ISLNK(file_stats.st_mode)){ 
	 		/* Ignore symbolic links. */
	 	} 

	 	else if(S_ISDIR(file_stats.st_mode)){
	 		/* If directory, descend in and post work to queue. */
        	queue_work(directory, element, dir_queue, entry, result, status);
			
        	free((void *)element);
		} 

	   	else if(S_ISREG(file_stats.st_mode)){
	   		/* A regular file */
        	insertElement(files, element);
			num_files++;
	   	}

	   	else{
		printf("%s is of type other. \n", element->path_name);
	 	}

  	}
  	/* Initialize threads, variables, and division of queue to handle the work */
	int num_threads = atoi(argv[3]);
   	int segment = num_files/num_threads;
   	int ret,t;

   	pthread_t threads[num_threads];
 	split_queue *s_queue = malloc(sizeof(split_queue));
   	queue_element_t *queue_head = files->head;
   	size_t len = strlen(argv[1]);
   	char* sString = malloc(len+1);
   	strcpy(sString, argv[1]);
   	void *tStatus = 0;

   	/* Create threads to handle next available task in queue */
   	for(t = 0; t < num_threads; t++){
    	s_queue = q_split(queue_head, segment);
      	queue_head = s_queue->remainder_queue;
      	static_data *tData = malloc(sizeof(static_data));
      	tData->file_head = s_queue->new_queue;
      	tData->search_string = sString;

      	ret = pthread_create(&threads[t], NULL, static_thread,(void *)tData);

      	if(ret){
         	printf("Error creating static pthread");
         	exit(-1);
      	}
   	}

   	/* Join the individual thread work */
   	for(t = 0; t < num_threads; t++){
      	ret = pthread_join(threads[t], &tStatus);

      	if(ret){
         	printf("Error joining static threads");
         	exit(-1);
      	}
   	}
   	pthread_mutex_destroy(&mutex);

   	gettimeofday(&stop, NULL); // Stop the timer to print time elapsed
    printf("\n Static Overall execution time = %fs.\n\n", (float)(stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/(float)1000000));

   	return total_occurrences;
}

int
parallelSearchDynamic(char **argv)
{
	/* Parallel search with dynamic load balancing. */

	struct timeval start, stop;
	gettimeofday(&start, NULL); // start timer

	/* Initialize variables and memory for queue */
	queue_element_t *element;
   	struct stat file_stats;
   	struct dirent *result = NULL;
   	struct dirent *entry = (struct dirent *)malloc(sizeof(struct dirent)+MAX_LENGTH);
   	int status; 
   	DIR *directory = NULL;

   	queue_t *dir_queue = createQueue();
   	queue_t *files = createQueue();
   	element = (queue_element_t *)malloc(sizeof(queue_element_t));

	if(element == NULL){ // malloc error handling
		perror("malloc");
		 exit(EXIT_FAILURE);
  	}

   	strcpy(element->path_name, argv[2]);
   	element->next = NULL;
   	insertElement(dir_queue, element);

   	/* Build up the queue */ 
   	while(dir_queue->head != NULL){

	 	queue_element_t *element = removeElement(dir_queue);
	 	status = lstat(element->path_name, &file_stats);

	 	if(status == -1){
	 		/* Error handling for file parsing */
			printf("Error obtaining stats for %s \n", element->path_name);
			free((void *)element);
			continue;
	 	}

		/* Ignore symbolic links */
	 	if(S_ISLNK(file_stats.st_mode)){ 
	 	} 

	 	else if(S_ISDIR(file_stats.st_mode)){

         	queue_work(directory, element, dir_queue, entry, result, status);
         	free((void *)element);
		}

	   	else if(S_ISREG(file_stats.st_mode)){
	   		/* A regular file */
			insertElement(files, element);
		}

	   	else{
		   	printf("%s is of type other. \n", element->path_name);
	   	}
  	}

  	/* Initialize threads, variables, and division of queue to handle the work */
   	int num_threads = atoi(argv[3]);
   	int ret,t;

   	pthread_t threads[num_threads];

   	size_t len = strlen(argv[1]);
   	char* sString = malloc(len+1);
   	strcpy(sString, argv[1]);
   	void *tStatus = 0;

   	dynamic_data *tData = malloc(sizeof(dynamic_data));
   	tData->files = files;
   	tData->search_string = sString;

   	/* Create threads to handle next available task in queue */
   	for(t = 0; t < num_threads; t++){
      	ret = pthread_create(&threads[t], NULL, dynamic_thread,(void *)tData);

      	if(ret){
         	printf("Error creating dynamic threads");
         	exit(-1);
      	}
   	}

   	/* Join the individual thread work */
   	for(t = 0; t < num_threads; t++){
     	ret = pthread_join(threads[t], &tStatus);

      	if(ret){
         	printf("Error joining dynamic threads");
         	exit(-1);
      	}
   	}
   	pthread_mutex_destroy(&mutex);
   	pthread_mutex_destroy(&queue_mutex); 

   	gettimeofday(&stop, NULL); // Stop the timer to print time elapsed
    printf("\n Static Overall execution time = %fs.\n\n", (float)(stop.tv_sec - start.tv_sec + (stop.tv_usec - start.tv_usec)/(float)1000000));

   	return total_occurrences;
}

void* static_thread(void *argument){

	/* Initialization and task retrieval from queue */
	static_data *tData = (static_data *)argument;
    queue_element_t *file_head = tData->file_head;
    char *str = tData->search_string;
	FILE *file_to_search;
	char buffer[MAX_LENGTH];
	char *bufptr, *searchptr, *tokenptr;
	int file_occurrences = 0;
			
	queue_element_t *element = malloc(sizeof(queue_element_t));

	/* Recursively search through portion of directory assigned from queue + error handling */ 
    while(file_head != NULL){
        element = file_head;

        if(strcmp(file_head->path_name, "EXIT") == 0){
        	break;
        }

		file_to_search = fopen(element->path_name, "r");

		if(file_to_search == NULL){
			printf("Unable to open %s \n", element->path_name);
            file_head = file_head->next;
			continue;	
		} 

		else{
			/* Read file if found */
			while(1){
				bufptr = fgets(buffer, sizeof(buffer), file_to_search);

				if(bufptr == NULL){

					if(feof(file_to_search)){
						break;
					}

					if(ferror(file_to_search)){
						printf("Error reading %s \n", element->path_name);
						break;
					}
				}   
				/* Search for string and increment if found */
				tokenptr = strtok(buffer, " ,.-");

				while(tokenptr != NULL){
					searchptr = strstr(tokenptr, str);

					if(searchptr != NULL){
						printf("Found string %s within file %s. \n", str, element->path_name);
						file_occurrences++;
					}
												
					tokenptr = strtok(NULL, " ,.-");
				}
			}
		}
		fclose(file_to_search); // leaks are bad
        file_head = file_head->next;
    }
    pthread_mutex_lock(&mutex);
    total_occurrences += file_occurrences;
    pthread_mutex_unlock(&mutex);
    return(NULL);
}

void* dynamic_thread(void *argument){

	/* Initialization and task retrieval from queue */
	dynamic_data *tData = (dynamic_data *)argument;
    queue_t *files = tData->files;
    char *str = tData->search_string;
	FILE *file_to_search;
	char buffer[MAX_LENGTH];
	char *bufptr, *searchptr, *tokenptr;
	int file_occurrences = 0;
    char path[MAX_LENGTH];
			
	queue_element_t *element = malloc(sizeof(queue_element_t));

	/* Recursively search through portion of directory assigned from queue + error handling */ 
    while(files->head != NULL){
      
        pthread_mutex_lock(&queue_mutex);
        element = removeElement(files);
        strcpy(path,element->path_name);
        pthread_mutex_unlock(&queue_mutex);

		file_to_search = fopen(path, "r");
		if(file_to_search == NULL){
		printf("Unable to open %s \n", path);
		continue;
		} 

		else{
			/* Read file if found */
			while(1){
				bufptr = fgets(buffer, sizeof(buffer), file_to_search);

				if(bufptr == NULL){

					if(feof(file_to_search)){
					 	break;
					}

					if(ferror(file_to_search)){
						printf("Error reading %s \n", path);
						break;
					}
				}
				/* Search for string and increment if found */
				tokenptr = strtok(buffer, " ,.-");

				while(tokenptr != NULL){
					searchptr = strstr(tokenptr, str);

					if(searchptr != NULL){
						printf("Found string %s within file %s. \n", str, path);
						file_occurrences++;
					}						
					tokenptr = strtok(NULL, " ,.-");
				}
			}
		}
		fclose(file_to_search); // leaks are still bad
    }
    pthread_mutex_lock(&mutex);
    total_occurrences += file_occurrences; // return times found to global
    pthread_mutex_unlock(&mutex);
    return(NULL);
}

split_queue* q_split(queue_element_t *queue, int segment){

	/* Splits up work in the queue for the threads */
   	queue_element_t *q_pointer = malloc(sizeof(queue_element_t));
   	q_pointer = queue;
   	split_queue *split = malloc(sizeof(split_queue));
   	int i;

   	split->new_queue = queue;

   	for (i = 0; i < (segment); i++){

      	if(q_pointer->next == NULL){
         	break;
      	}

      	else{
         	q_pointer = q_pointer->next;   
         
      	}
   	}
   	/* Split queue and allocate memory for it */ 
   	split->remainder_queue = q_pointer->next;
   	queue_element_t *terminate = malloc(sizeof(queue_element_t));
   	strcpy(terminate->path_name,"EXIT");
   	terminate->next = q_pointer->next;
   	q_pointer->next = terminate;
   	return split;   
}

void queue_work(DIR* dir, queue_element_t* element, queue_t* queue, struct dirent* entry, struct dirent* result, int check){

	/* Queues file paths for threads to search through */
   	queue_element_t *new_element;
	dir = opendir(element->path_name);

	if(dir == NULL){
		printf("Unable to open directory %s \n", element->path_name);
		return;
	}

	while(1){
		check = readdir_r(dir, entry, &result);

		if(check != 0){
		 	printf("Unable to read directory %s \n", element->path_name);
			break;
		}
	  	if(result == NULL){
			break; 										  											  
	  	}

		if(strcmp(entry->d_name, ".") == 0){
			continue;
		}

		if(strcmp(entry->d_name, "..") == 0){
			continue;
		}

		new_element = (queue_element_t *)malloc(sizeof(queue_element_t));

		if(new_element == NULL){
		 	printf("Unable to allocate memory. \n");
			exit(-1);
		}
			
	 	strcpy(new_element->path_name, element->path_name);
		strcat(new_element->path_name, "/");
		strcat(new_element->path_name, entry->d_name);
		insertElement(queue, new_element);
	}
	
	closedir(dir);

	return;
}
