#include "server.h"

#define PORT 8686
#define MAX_CLIENTS 5
#define BUFFER_SIZE 1024

worker_thread_t worker_thread_list[MAX_CLIENTS];

sem_t thread_number; // allows access to the thread scheduler if any are available

void close_thread(int worker_num, int sockfd)
{
    worker_thread_list[worker_num].available = true;
    close(sockfd);
    sem_post(&thread_number);
    pthread_exit(NULL);
}

char *serialize_packet(packet_t *packet)
{
    packet->size = htons(packet->size);

    char *serialized_data = malloc(sizeof(char) * PACKET_SIZE);
    memset(serialized_data, '\0', PACKET_SIZE);
    memcpy(serialized_data, packet, PACKET_SIZE);

    return serialized_data;
}

packet_t *deserialize_data(char *serialized_data)
{
    packet_t *packet = malloc(sizeof(char) * PACKET_SIZE);
    memset(packet, 0, PACKET_SIZE);
    memcpy(packet, serialized_data, PACKET_SIZE);

    packet->size = ntohs(packet->size);

    return packet;
}

int acknowledge(int sockfd, int new_size)
{
    packet_t packet;
    packet.operation = IMG_OP_ACK;
    packet.flags = 0;
    packet.size = new_size;

    char *serialized_packet = serialize_packet(&packet);
    if(send(sockfd, serialized_packet, PACKET_SIZE, 0) == -1)
    {
        free(serialized_packet);
        return -1;
    }
    free(serialized_packet);
    return 0;
}

void neg_acknowledge(int sockfd)
{
    packet_t packet;
    packet.operation = IMG_OP_NAK;
    packet.flags = 0;
    packet.size = 0;

    char *serialized_packet = serialize_packet(&packet);
    if(send(sockfd, serialized_packet, PACKET_SIZE, 0) == -1)
    {
        fprintf(stderr, "ERROR: Could not send negative acknowledgement\n");
    }
    free(serialized_packet);
}

int process_image(char *file_name, char *new_name, int angle)
{
    int width;
    int height;
    int channels;
    uint8_t *image_result = stbi_load(file_name, &width, &height, &channels, CHANNEL_NUM);

    if (image_result == NULL)
    {
        printf("Could not load image_result\n");
        return -1;
    }
    
    uint8_t **result_matrix = (uint8_t **)malloc(sizeof(uint8_t*) * width);
    uint8_t **img_matrix    = (uint8_t **)malloc(sizeof(uint8_t*) * width);
    for(int i = 0; i < width; i++)
    {
        result_matrix[i] = (uint8_t *)malloc(sizeof(uint8_t) * height);
        img_matrix[i]    = (uint8_t *)malloc(sizeof(uint8_t) * height);
    }
    
    // load image into matrix
    linear_to_image(image_result, img_matrix, width, height);

    // flip image corresponding to angle requested
    if (angle == 270)
    {
        flip_upside_down(img_matrix, result_matrix ,width, height);
    }
    else if (angle == 180)
    {
        flip_left_to_right(img_matrix, result_matrix, width, height);
    }

    uint8_t *img_array = malloc(sizeof(uint8_t) * width * height);

    flatten_mat(result_matrix, img_array, width, height);

    // write image matrix to png
    stbi_write_png(new_name, width, height, CHANNEL_NUM, img_array, width * CHANNEL_NUM);

    // data clean up
    for (int i = 0; i < width; i++)
    {
        free(result_matrix[i]);
        free(img_matrix[i]);
    }
    free(img_matrix);
    free(result_matrix);
    free(img_array);

    return 0;
}

void *client_handler(void *pargs)
{
    processing_args_t *args = (processing_args_t *)pargs;
    int sockfd = args->sockfd;

    int worker_num = args->worker_num;

    char recv_data[PACKET_SIZE];

    while (true) // connection continues until terminated by client
    {
        memset(recv_data, '\0', PACKET_SIZE);

        if(recv(sockfd, recv_data, PACKET_SIZE, 0) == -1)
        {
            fprintf(stderr, "ERROR: Could not receive packet\n");
            neg_acknowledge(sockfd);
            close_thread(worker_num, sockfd);
        }

        // extract data from packet
        packet_t *recv_packet = deserialize_data(recv_data);

        if (recv_packet->operation == IMG_OP_EXIT)
            break;

        const int SIZE = recv_packet->size;
        int rotation;

        if (recv_packet->flags == (recv_packet->flags & IMG_FLAG_ROTATE_180))
            rotation = 180;
        else if (recv_packet->flags == (recv_packet->flags & IMG_FLAG_ROTATE_270))
            rotation = 270;
        else
        { 
            fprintf(stderr, "ERROR: Invalid request\n");
            neg_acknowledge(sockfd);
            free(recv_packet);
            close_thread(worker_num, sockfd);
        }

        free(recv_packet);

        // create temporary (work) directory for storing temp files
        char temp_dir_name[8];
        memset(temp_dir_name, '\0', 8);
        sprintf(temp_dir_name, "%d", worker_num);
        if(mkdir(temp_dir_name, 0777) == -1)
        {
            fprintf(stderr, "ERROR: Could not create temp directory\n");
        }

        // prepare temporary file names
        char temp_file_name[32];
        char new_file_name[32];
        memset(temp_file_name, '\0', 32);
        memset(new_file_name, '\0', 32);
        sprintf(temp_file_name, "%d/temp.png", worker_num);
        sprintf(new_file_name, "%d/processed.png", worker_num);

        FILE *img_recv;
        if ((img_recv = fopen(temp_file_name, "w+")) == NULL)
        {
            fprintf(stderr, "ERROR: Could not open output file location\n");
            neg_acknowledge(sockfd);
            rmdir(temp_dir_name);
            close_thread(worker_num, sockfd);
        }

        // read input image data from socket
        char buf[BUFF_SIZE];
        int num_bytes = 0;
        while (num_bytes < SIZE)
        {
            int new_bytes = read(sockfd, buf, BUFF_SIZE);
            num_bytes += new_bytes;
            fwrite(buf, sizeof(char), new_bytes, img_recv);
        }

        fclose(img_recv);

        // do image processing
        if (process_image(temp_file_name, new_file_name, rotation) == -1)
        {
            fprintf(stderr, "ERROR: could not process image\n");
            neg_acknowledge(sockfd);
            rmdir(temp_dir_name);
            close_thread(worker_num, sockfd);
        }

        FILE *processed_image;
        if ((processed_image = fopen(new_file_name, "r")) == NULL)
        {
            fprintf(stderr, "ERROR: Could not open processed image\n");
            neg_acknowledge(sockfd);
            rmdir(temp_dir_name);
            close_thread(worker_num, sockfd);
        }

        // get size of processed image
        fseek(processed_image, 0, SEEK_END);
        int new_size = ftell(processed_image);
        fseek(processed_image, 0, SEEK_SET);

        if (acknowledge(sockfd, new_size) == -1)
        {
            fprintf(stderr, "ERROR: Could not send acknowledgement\n");
            fclose(processed_image);
            remove(temp_file_name);
            remove(new_file_name);
            rmdir(temp_dir_name);
            close_thread(worker_num, sockfd);
        }

        // write processed image data to socket
        num_bytes = 0;
        while (num_bytes < new_size)
        {
            int new_bytes = fread(buf, sizeof(char), BUFF_SIZE, processed_image);
            num_bytes += new_bytes;
            write(sockfd, buf, new_bytes);
        }

        // memory clean up
        fclose(processed_image);

        if (remove(temp_file_name) != 0)
        {
            fprintf(stderr, "ERROR: Could not delete pre-processed temp image\n");
        }

        if (remove(new_file_name) != 0)
        {
            fprintf(stderr, "ERROR: Could not delete processed temp image\n");
        }

        if (rmdir(temp_dir_name) == -1)
        {
            fprintf(stderr, "ERROR: Couldn't remove temp dir\n");
            perror("rmdir");
        }
    }

    close_thread(worker_num, sockfd);
    return NULL;
}

int main(int argc, char* argv[])
{
    // worker thread struct initialization
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        worker_thread_list[i].available = true;
        worker_thread_list[i].pargs.worker_num = i;
    }

    sem_init(&thread_number, 0, MAX_CLIENTS);

    // Creating socket file descriptor
    int listen_fd, conn_fd; // passive listening socket, 

    // create listening socket
    if((listen_fd = socket(AF_INET, SOCK_STREAM, 0)) == -1) 
    {
        fprintf(stderr, "ERROR: Socket error\n");
        exit(1);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, '\0', sizeof(servaddr));
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); // Listen to any of the network interface (INADDR_ANY)
    servaddr.sin_port = htons(PORT); // Port number

    // bind address, port to socket
    if(bind(listen_fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1)
    {
        fprintf(stderr, "ERROR: Bind error\n");
        exit(1);
    }

    // listen on the listen_fd
    if(listen(listen_fd, MAX_CLIENTS) == -1)
    {
        fprintf(stderr, "ERROR: Listen error\n");
        exit(1);
    }

    while (true)
    {
        struct sockaddr_in clientaddr;
        socklen_t clientaddr_len = sizeof(clientaddr);
        conn_fd = accept(listen_fd, (struct sockaddr *) &clientaddr, &clientaddr_len); // accept a request from a client
        if(conn_fd == -1)
        {
            fprintf(stderr, "ERROR: Accepting error\n");
            exit(1);
        }

        // wait for there to be an available thread
        sem_wait(&thread_number);

        for (int i = 0; i < MAX_CLIENTS; i++)
        {
            if (worker_thread_list[i].available)
            {
                worker_thread_list[i].available = false;
                worker_thread_list[i].pargs.sockfd = conn_fd;
                if ((worker_thread_list[i].thread = pthread_create(&worker_thread_list[i].thread, NULL, client_handler, &worker_thread_list[i].pargs)) != 0)
                {
                    fprintf(stderr, "ERROR: Thread creation error\n");
                    exit(1);
                }
                pthread_detach(worker_thread_list[i].thread);
                break;
            }
        }

    }

    // data cleanup
    sem_destroy(&thread_number);
    close(listen_fd);
    return 0;
}
