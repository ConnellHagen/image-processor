#ifndef IMAGE_ROTATION_H_
#define IMAGE_ROTATION_H_

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <pthread.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/time.h>
#include <time.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <limits.h>
#include <stdint.h>
#include "utils.h"

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define CHANNEL_NUM 1

#include "stb_image.h"
#include "stb_image_write.h"



/********************* [ Helpful Macro Definitions ] **********************/
#define BUFF_SIZE 1024 
#define LOG_FILE_NAME "request_log"               //Standardized log file name
#define INVALID -1                                  //Reusable int for marking things as invalid or incorrect
#define MAX_THREADS 100                             //Maximum number of threads
#define MAX_QUEUE_LEN 100                           //Maximum queue length



/********************* [ Helpful Typedefs        ] ************************/

/**
 * request_t stores the name of a file being rotated
 * and the angle it is being rotated by 
 */
typedef struct request
{
    char *file_name;
    int   angle;
} request_t;

/**
 * request_queue_t is a Queue of `request`s being maintained
 * as a linked list 
 */
typedef struct request_node
{
    struct request_node *next;
    request_t           *this_request;
} request_queue_t;

typedef struct processing_args
{
    int   num_workers;
    int   angle;
} processing_args_t;

/**
 * initialize global request queue `requests` with an empty node
 */
void init_request_queue();

/**
 * returns 1 if the global request queue is empty
 * otherwise returns 0 
 */
int request_queue_empty();

/**
 * prints a line for each item in the request queue in order
 * consisting of the rotation angle of the request, and the
 * path to the image
 */
void print_request_queue();

/**
 * adds a new request to the end of the request queue
 * queue -> pointer to the request queue being added to
 * file_name -> path to image the request is representing
 * angle -> angle that the image is being rotate by
 */
void add_request(request_queue_t *queue, char *file_name, int angle);

/**
 * returns the first `request` in the request queue and
 * removes the `request` from the queue
 * returns `NULL` if queue is empty
 * NOTE: The request returned must be `free()`d when no
 * longer in use 
 */
request_t *get_request();

/**
 * converts the images within `img_dir_path` into `request`s in the
 * request queue using the specific `angle` for the angle of
 * rotation specified in the `request` 
 */
void extract_requests(int angle);

/*
    The Function takes:
    to_write: A file pointer of where to write the logs. 
    requestNumber: the request number that the thread just finished.
    file_name: the name of the file that just got processed. 

    The function output: 
    it should output the threadId, requestNumber, file_name into the logfile and stdout.
*/
void log_pretty_print(FILE* to_write, int thread_id, int request_number, char * file_name);

/*
1: The processing function takes a void* argument called args. It is expected to be a pointer to a structure processing_args_t 
that contains information necessary for processing.

2: The processing thread need to traverse a given dictionary and add its files into the shared queue while maintaining synchronization using lock and unlock. 

3: The processing thread should pthread_cond_signal/broadcast once it finish the traversing to wake the worker up from their wait.

4: The processing thread will block(pthread_cond_wait) for a condition variable until the workers are done with the processing of the requests and the queue is empty.

5: The processing thread will cross check if the condition from step 4 is met and it will signal to the worker to exit and it will exit.
*/
void *processing(void *args); 

/*
    1: The worker threads takes an int ID as a parameter

    2: The Worker thread will block(pthread_cond_wait) for a condition variable that there is a requests in the queue. 

    3: The Worker threads will also block(pthread_cond_wait) once the queue is empty and wait for a signal to either exit or do work.

    4: The Worker thread will processes request from the queue while maintaining synchronization using lock and unlock. 

    5: The worker thread will write the data back to the given output dir as passed in main. 

    6:  The Worker thread will log the request from the queue while maintaining synchronization using lock and unlock.  

    8: Hint the worker thread should be in a While(1) loop since a worker thread can process multiple requests and It will have two while loops in total
        that is just a recommendation feel free to implement it your way :) 
    9: You may need different lock depending on the job.  
*/
void *worker(void *args); 



#endif
