#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "socket.h"
#include "ui.h"
#define MAX_CLIENTS 100
#define MAX_MESSAGE_LENGTH 2048
// Keep the username in a global so we can access it from the callback
const char *username;
void *clientThread(void *data);
int send_message(int fd, char *message);
char *receive_message(int fd);

// Global variable declaration
int acceptClient = 0; //number of clients connected
pthread_t clients[MAX_CLIENTS]; //array of threads for every client

typedef struct client{
  int fd; //storing each client fd
  const char *username;  //storing each client username
} client_t;

client_t neighbors_accept[MAX_CLIENTS]; //array of all clients connected

unsigned short server_port;


/// @brief Void* function for a listening thread. 
/// Listens through the server socket and creates a new client thread for every connection. 
/// @param fd :server we are passing it to
/// @return void
void *listening(void *fd)
{
  // listen for any connections
  while (1)
  {
    int *server_fd = ((int *)fd); //cast into the server fd
    if (listen(*server_fd, 1)) //keep listening
    {
      perror("listen failed");
      exit(EXIT_FAILURE);
    }
    // waiting for connections
    int client_socket_fd = server_socket_accept(*server_fd);
    if (client_socket_fd == -1) //error check
    {
      perror("accept failed");
      exit(EXIT_FAILURE);
    }

    //set each neighbor fd to the client fd
    neighbors_accept[acceptClient].fd = client_socket_fd;
    //creating threads for every client
    if (pthread_create(&clients[acceptClient], NULL, clientThread, &neighbors_accept[acceptClient]) != 0)
    {
      perror("Error creating threads\n"); 
      exit(3);
    }
    acceptClient++;
  }
}
/**
 * @brief Receives messages from the client socket and echoes them to other neighbors.
 * 
 * @param data : client socket
 * @return void* data
 */
void *clientThread(void *data)
{
  int *client_socket_fd = ((int *)data);
  // receiving a message
  while (1)
  {
    // receiving and sending messages
    size_t nbytes = 128;
    char *message = (char *)malloc(nbytes + 1);
    // Read a message from the client
    neighbors_accept[acceptClient].username = receive_message(*client_socket_fd); //receiving username
    message = receive_message(*client_socket_fd); //receiving fd
    if(neighbors_accept[acceptClient].username == NULL || message == NULL){ //checking if a client quit
      free(message); //freeing memory
      close(*client_socket_fd); //closing socket
      break;
    }
    for (int i = 0; i < acceptClient; i++)
    {
      if(neighbors_accept[i].fd == *client_socket_fd){
        continue; //not sending messages to the port where they came from
      }
      // Send a message to the client
      int rc1 = send_message(neighbors_accept[i].fd, ((char *)neighbors_accept[acceptClient].username));
      if (rc1 == -1)
      {
        continue; //handle connection errors by continuing 
      }

      int rc2 = send_message(neighbors_accept[i].fd, ((char *)message));
      if (rc2 == -1)
      {
        continue; //handle connection errors by continuing 
      }
    }
    
    ui_display(neighbors_accept[acceptClient].username, message);
  }
  return data;
}

// This function is run whenever the user hits enter after typing a message
/**
 * @brief Displays the message whenever we press enter, if message is ":quit" we exit
 * 
 * @param message : user input
 */
void input_callback(const char *message)
{
  if (strcmp(message, ":quit") == 0 || strcmp(message, ":q") == 0)
  {
    ui_exit();
  }
  else
  {
    ui_display(username, message);
    for (int i = 0; i < acceptClient; i++)
    {
      // Send a message to the client
      int rc1 = send_message(neighbors_accept[i].fd, ((char *)username));
      if (rc1 == -1)
      {
        continue;
      }

      int rc2 = send_message(neighbors_accept[i].fd, ((char *)message));
      if (rc2 == -1)
      {
        continue;
      }
    }
  }
}

int main(int argc, char **argv)
{
  // Make sure the arguments include a username
  if (argc != 2 && argc != 4)
  {
    fprintf(stderr, "Usage: %s <username> [<peer> <port number>]\n", argv[0]);
    exit(1);
  }

  // Save the username in a global
  username = argv[1];

  //Set up a server socket to accept incoming connections
  server_port = 0;
  int server_socket_fd = server_socket_open(&server_port);
  printf("Server fd: %d\n", server_socket_fd);
  if (server_socket_fd == -1)
  {
    perror("Server socket was not opened");
    exit(EXIT_FAILURE);
  }
  // create a thread to constantly listen to new connections
  pthread_t listenThread;
  if (pthread_create(&listenThread, NULL, listening, &server_socket_fd) != 0)
  {
    perror("Error creating threads\n");
    exit(3);
  }

  // Client behavior
  // Did the user specify a peer we should connect to?
  if (argc == 4)
  {
    // Unpack arguments
    char *peer_hostname = argv[2];
    unsigned short peer_port = atoi(argv[3]);

    //Connect to another peer in the chat network
    int peer_socket_fd = socket_connect(peer_hostname, peer_port);
    if (peer_socket_fd == -1)
    {
      perror("Failed to connect");
      exit(EXIT_FAILURE);
    }

    neighbors_accept[acceptClient].fd = peer_socket_fd; //putting it in the neighbors network
    acceptClient++;
    if (pthread_create(&clients[acceptClient], NULL, clientThread, &peer_socket_fd) != 0)
    {
      perror("Error creating threads\n");
      exit(3);
    }
  }

  // Set up the user interface. The input_callback function will be called
  // each time the user hits enter to send a message.
  ui_init(input_callback);

  // Once the UI is running, you can use it to display log messages

  ui_display("INFO", "This is a handy log message.");
  // Displaying the port number
  int length = snprintf(NULL, 0, "%d", server_port);
  char *str = malloc(length + 1);
  snprintf(str, length + 1, "%d", server_port);
  ui_display("PORT", str);
  printf("\n");

  // Run the UI loop. This function only returns once we call ui_stop() somewhere in the program.
  ui_run();

  return 0;
}

/**
 * @brief Provided code in the exercise. Reads the message sent in the inputed socket.
 * 
 * @param fd 
 * @return char* 
 */
char *receive_message(int fd)
{
  // First try to read in the message length
  size_t len;
  if (read(fd, &len, sizeof(size_t)) != sizeof(size_t))
  {
    // Reading failed. Return an error
    return NULL;
  }

  // Now make sure the message length is reasonable
  if (len > MAX_MESSAGE_LENGTH)
  {
    errno = EINVAL;
    return NULL;
  }

  // Allocate space for the message with a null terminator
  char *result = malloc(len + 1);

  // Try to read the message. Loop until the entire message has been read.
  size_t bytes_read = 0;
  while (bytes_read < len)
  {
    // Try to read the entire remaining message
    ssize_t rc = read(fd, result + bytes_read, len - bytes_read);

    // Did the read fail? If so, return an error
    if (rc <= 0)
    {
      free(result);
      return NULL;
    }

    // Update the number of bytes read
    bytes_read += rc;
  }

  // Add a null terminator to the message
  result[len] = '\0';

  return result;
}


/**
 * @brief Provided code in the exercise. Send a message across a socket with a header that includes the message length.
 * 
 * @param fd 
 * @param message 
 * @return int 
 */
int send_message(int fd, char *message)
{
  // If the message is NULL, set errno to EINVAL and return an error
  if (message == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  // First, send the length of the message in a size_t
  size_t len = strlen(message);
  if (write(fd, &len, sizeof(size_t)) != sizeof(size_t))
  {
    // Writing failed, so return an error
    return -1;
  }

  // Now we can send the message. Loop until the entire message has been written.
  size_t bytes_written = 0;
  while (bytes_written < len)
  {
    // Try to write the entire remaining message
    ssize_t rc = write(fd, message + bytes_written, len - bytes_written);

    // Did the write fail? If so, return an error
    if (rc <= 0)
      return -1;

    // If there was no error, write returned the number of bytes written
    bytes_written += rc;
  }

  return 0;
}
