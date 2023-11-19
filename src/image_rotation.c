#include <dirent.h>
#include <pthread.h>
#include "image_rotation.h"

char *img_dir_path;
char *output_dir_path;

pthread_mutex_t request_log_lock   = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t request_queue_lock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t  request_queue_add  = PTHREAD_COND_INITIALIZER;

request_queue_t *requests;
pthread_t       *workers;

FILE* request_log;

int request_queue_finished = 0;


void init_request_queue()
{
    requests               = malloc(sizeof(request_queue_t));
    requests->next         = NULL;
    requests->this_request = NULL;
}

int request_queue_empty()
{
    return (requests->this_request == NULL);
}

void print_request_queue()
{
    if (request_queue_empty())
    {
        printf("Empty\n");
        return;
    }

    request_queue_t *curr = requests;
    while (curr != NULL)
    {
        printf("Angle: %d, Image Path: %s -> ",
            curr->this_request->angle,
            curr->this_request->file_name);
        curr = curr->next;
    }
    printf("NULL\n");
}

void add_request(request_queue_t *queue, char *file_name, int angle)
{
    // prepare request
    request_t *req = malloc(sizeof(request_t));
    req->file_name = file_name;
    req->angle     = angle;

    // if the queue is empty, add the request to the current node instead of making a new one
    if (request_queue_empty())
    {
        queue->this_request = req;
        pthread_mutex_unlock(&request_queue_lock);
        return;
    }

    request_queue_t *new_node = malloc(sizeof(request_queue_t)); 
    new_node->next            = NULL;
    new_node->this_request    = req;

    // traverse to end of linked-list, and append new request
    request_queue_t *curr = queue;
    while (curr->next != NULL)
    {
        curr = curr->next;
    }
    curr->next = new_node;
}

request_t *get_request()
{
    if (request_queue_empty()) return NULL;
    
    request_t *req = requests->this_request;

    /* ensures that an empty queue does not make the queue
       pointer itself `NULL`, but rather its content */
    if (requests->next == NULL)
        requests->this_request = NULL;
    else 
    {
        request_queue_t *old_req = requests;
        requests = requests->next;
        free(old_req);
    }
    
    return req;
}

void extract_requests(int angle)
{
    DIR *dir = opendir(img_dir_path);
    if (dir == NULL)
    {
        fprintf(stderr, "Error opening img_dir\n");
        exit(1);
    }

    int current = 0;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(current < 2)
        {
            current++;
            continue;
        }

        pthread_mutex_lock(&request_queue_lock);
        add_request(requests, entry->d_name, angle);
        pthread_cond_signal(&request_queue_add);
        pthread_mutex_unlock(&request_queue_lock);
    }

    // closedir(dir); // causes corruption in the image names ?

    /* send signal to all s that there are no more
       requests left */
    request_queue_finished = 1;
    pthread_cond_broadcast(&request_queue_add);

    sleep(1); //
    closedir(dir);
}

void log_pretty_print(FILE* to_write, int thread_id, int request_number, char *file_name)
{
    const int MAX_NUM_SIZE_ALLOWED = 8;
    const int NUM_BRACKETS = 6;
    const int WRITE_SIZE = 2 * MAX_NUM_SIZE_ALLOWED + strlen(file_name) + NUM_BRACKETS;

    char buf[WRITE_SIZE];
    memset(buf, '\0', WRITE_SIZE);

    pthread_mutex_lock(&request_log_lock);
    sprintf(buf, "[%d][%d][%s]\n", thread_id, request_number, file_name);
    pthread_mutex_unlock(&request_log_lock);

    fwrite(buf, sizeof(char), strlen(buf), to_write);
}

void *processing(void *args)
{
    processing_args_t *pargs = (processing_args_t *)args;
    extract_requests(pargs->angle);

    pthread_exit(NULL);
}

void *worker(void *args)
{
    int id = *((int *)args);

    int num_reqs = 0;

    request_t *curr_request = NULL;

    while (1)
    {
        pthread_mutex_lock(&request_queue_lock);

        while (request_queue_empty()) {
            if (request_queue_finished) {
                pthread_mutex_unlock (&request_queue_lock);
                pthread_exit(NULL);
            }

            pthread_cond_wait(&request_queue_add, &request_queue_lock);
        }

        curr_request = get_request();
        log_pretty_print(request_log, id, ++num_reqs, curr_request->file_name);
        // printf("REQUEST OBTAINED: %s ID: %d Angle: %d\n", curr_request->file_name, id, curr_request->angle);

        pthread_mutex_unlock(&request_queue_lock);

        // preparing the file name for the input image
        const int INPUT_FILENAME_LENGTH = strlen(curr_request->file_name) + strlen(img_dir_path) + 1;
        char input_file_name[INPUT_FILENAME_LENGTH];
        memset(input_file_name, '\0', INPUT_FILENAME_LENGTH);

        sprintf(input_file_name, "%s/%s", img_dir_path, curr_request->file_name);

        int width;
        int height;
        int channels;
        uint8_t *image_result = stbi_load(input_file_name, &width, &height, &channels, CHANNEL_NUM);
        
        uint8_t **result_matrix = (uint8_t **)malloc(sizeof(uint8_t*) * width);
        uint8_t **img_matrix    = (uint8_t **)malloc(sizeof(uint8_t*) * width);
        for(int i = 0; i < width; i++)
        {
            result_matrix[i] = (uint8_t *)malloc(sizeof(uint8_t) * height);
            img_matrix[i]    = (uint8_t *)malloc(sizeof(uint8_t) * height);
        }
        
        linear_to_image(image_result, img_matrix, width, height);
    
        // flip image corresponding to angle requested
        if (curr_request->angle == 270)
        {
            flip_upside_down(img_matrix, result_matrix ,width, height);
        }
        else if (curr_request->angle == 180)
        {
            flip_left_to_right(img_matrix, result_matrix, width, height);
        }
        else
        {
            fprintf(stderr, "ERROR: Rotation must be 180 or 270 degrees\n");
            exit(1);
        }

        uint8_t *img_array = malloc(sizeof(uint8_t) * width * height);

        flatten_mat(result_matrix, img_array, width, height);

        // preparing the file name for the new, rotated, image
        const int ROT_FILENAME_LENGTH = strlen(curr_request->file_name) + strlen(output_dir_path) + 10;
        char rotated_file_name[ROT_FILENAME_LENGTH];
        memset(rotated_file_name, '\0', ROT_FILENAME_LENGTH);

        sprintf(rotated_file_name, "./%s/rotated%s", output_dir_path, curr_request->file_name);

        // printf("%s\n", rotated_file_name);
        stbi_write_png(rotated_file_name, width, height, CHANNEL_NUM, img_array, width * CHANNEL_NUM);


        for (int i = 0; i < width; i++)
        {
            free(result_matrix[i]);
            free(img_matrix[i]);
        }
        free(img_matrix);
        free(result_matrix);
        free(img_array);
        free(curr_request);
    }
}

int main(int argc, char* argv[])
{
    if(argc != 5)
    {
        fprintf(stderr, "Usage: File Path to image directory, File path to output directory, number of worker thread, and Rotation angle\n");
    }

        img_dir_path    = argv[1];
        output_dir_path = argv[2];
    int num_workers     = atoi(argv[3]);
    int angle           = atoi(argv[4]);

    workers = malloc(sizeof(pthread_t*) * num_workers);

    if((request_log = fopen("request_log", "w")) == NULL)
    {
        fprintf(stderr, "ERROR opening request_log");
        exit(1);
    }

    init_request_queue();

    // create processing thread
    pthread_t processing_thread;
    processing_args_t p_args = {num_workers, angle};
    pthread_create(&processing_thread, NULL, (void *)processing, &p_args);

    // create worker threads
    int worker_ids[num_workers];
    for (int i = 0; i < num_workers; i++) {
        worker_ids[i] = i;
        pthread_create(&workers[i], NULL, (void *)worker, (void *)(&worker_ids[i]));
    }
    
    // join all threads
    pthread_join(processing_thread, NULL);
    for (int i = 0; i < num_workers; i++) {
        pthread_join(workers[i], NULL);
    }

    free(workers);
    free(requests);
    fclose(request_log);
}