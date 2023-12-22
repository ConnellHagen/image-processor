#include "client.h"

#define PORT 8686 // x500: hage0686
#define BUFFER_SIZE 1024

request_queue_t *requests;

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
        // pthread_mutex_unlock(&request_queue_lock);
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
    packet_t *packet = malloc(sizeof(char) * PACKET_SIZE); // (packet_t *)malloc(sizeof(packet_t));
    memset(packet, 0, PACKET_SIZE);
    memcpy(packet, serialized_data, PACKET_SIZE);

    packet->size = ntohs(packet->size);

    return packet;
}

int terminate_connection(int socket)
{
    packet_t packet;
    packet.operation = IMG_OP_EXIT;
    packet.flags = 0;
    packet.size = 0;

    char *serialized_data = serialize_packet(&packet);

    // Send the file data
    if (send(socket, serialized_data, PACKET_SIZE, 0) == -1)
    {
        fprintf(stderr, "Error: Could not send request data");
        return -1;
    }

    return 0;
}

int send_file(int socket, char *input_dir, request_t *request)
{
    const int IMG_PATH_LENGTH = strlen(input_dir) + strlen(request->file_name) + 1;
    char img_location[IMG_PATH_LENGTH];
    sprintf(img_location, "%s/%s", input_dir, request->file_name);

    // Open the file
    FILE *img;
    if((img = fopen(img_location, "r")) == NULL)
    {
        fprintf(stderr, "ERROR: Could not open file\n");
        return -1;
    }

    fseek(img, 0, SEEK_END);
    const int SIZE = ftell(img);
    fseek(img, 0, SEEK_SET);

    // Set up the request packet for the server and send it
    packet_t packet;
    packet.operation = IMG_OP_ROTATE;
    if (request->angle == 180)
        packet.flags = IMG_FLAG_ROTATE_180;
    else if (request->angle == 270)
        packet.flags = IMG_FLAG_ROTATE_270;
    packet.size = SIZE;

    char *serialized_data = serialize_packet(&packet);

    // Send the file data
    if (send(socket, serialized_data, PACKET_SIZE, 0) == -1)
    {
        fprintf(stderr, "Error: Could not send request data");
        return -1;
    }

    free(serialized_data);

    // prepare image data
    char img_data[SIZE];
    memset(img_data, '\0', SIZE);
    int num_bytes = fread(img_data, sizeof(char), SIZE, img);
    write(socket, img_data, num_bytes);  

    return 0;
}

int receive_file(int socket, char *output_dir, request_t *request)
{
    // Open the file
    const int IMG_PATH_LENGTH = strlen(output_dir) + strlen(request->file_name) + 1;
    char img_location[IMG_PATH_LENGTH];
    sprintf(img_location, "%s/%s", output_dir, request->file_name);

    // Receive response packet
    char recv_data[PACKET_SIZE];
    memset(recv_data, '\0', PACKET_SIZE);

    if (recv(socket, recv_data, PACKET_SIZE, 0) == -1)
    {
        fprintf(stderr, "ERROR: Could not receive packet\n");
        return -1;
    }

    packet_t *recv_packet = deserialize_data(recv_data);

    if (recv_packet->operation == IMG_OP_ACK) { }
    else if (recv_packet->operation == IMG_OP_NAK) { return -1; } 
    else 
    {
        fprintf(stderr, "Received invalid operation\n");
        return -1;
    }

    // Receive the file data
    const int SIZE = recv_packet->size;

    free(recv_packet);

    // Write the data to the file
    FILE *testfile;
    if ((testfile = fopen(img_location, "w")) == NULL)
    {
        fprintf(stderr, "ERROR: Cannot open file location %s\n", img_location);
        return -1;
    }

    int num_bytes = 0;
    char buf[BUFF_SIZE];

    while (num_bytes < SIZE)
    {
        int new_bytes = read(socket, buf, BUFF_SIZE);
        num_bytes += new_bytes;
        fwrite(buf, sizeof(char), new_bytes, testfile);
    }

    fclose(testfile);
    return 0;
}

int main(int argc, char* argv[])
{
    if(argc != 4)
    {
        fprintf(stderr, "Usage: ./client File_Path_to_images File_Path_to_output_dir Rotation_angle. \n");
        return 1;
    }

    char *img_dir = argv[1];
    char *output_dir = argv[2];
    int rotation_angle = atoi(argv[3]);

    init_request_queue();

    // Read the directory for all the images to rotate
    DIR *dir = opendir(img_dir);

    int current = 0;
    struct dirent *entry;
    while((entry = readdir(dir)) != NULL)
    {
        if(current < 2)
        {
            current++;
            continue;
        }
        add_request(requests, entry->d_name, rotation_angle);
    }

    // closedir(dir);

    // print_request_queue();

    // Set up socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if(sockfd == -1)
    {
        fprintf(stderr, "Socket Error\n");
        exit(1);
    }

    // Connect the socket
    struct sockaddr_in servaddr;
    servaddr.sin_family = AF_INET; // IPv4
    servaddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // server IP (localhost)
    servaddr.sin_port = htons(PORT);

    if(connect(sockfd, (struct sockaddr *) &servaddr, sizeof(servaddr)) == -1)
    {
        fprintf(stderr, "Error: could not connect\n");
        exit(1);
    }

    while (!request_queue_empty())
    {
        request_t *request = get_request(); 

        // printf("filename: %s\n", request->file_name);

        // Send the image data to the server
        if (send_file(sockfd, img_dir, request) == -1)
        {
            fprintf(stderr, "Error: Could not send file\n");
            exit(1);
        }

        // Check that the request was acknowledged
        if (receive_file(sockfd, output_dir, request) == -1)
        {
            fprintf(stderr, "Error: File receiving error\n");
            exit(1);
        }

        free(request);
    }

    if (terminate_connection(sockfd) == -1)
    {
        fprintf(stderr, "Error: Couldn't terminate connection\n");
        exit(1);
    }
    

    // Receive the processed image and save it in the output dir

    // Terminate the connection once all images have been processed

    // Release any resources
    closedir(dir);
    return 0;
}
