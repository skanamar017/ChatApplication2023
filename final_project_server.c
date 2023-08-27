#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>

#define SERV_TCP_PORT 23 /* server's port number */
#define MAX_INPUT_OUTPUT_SIZE 300
#define MAX_CLIENT_ID_SIZE 21
#define MAX_CLIENTS_DEFAULT 20


struct client_data {
    int socketfd;
    struct sockaddr_in client_address;
    char client_id[MAX_CLIENT_ID_SIZE];
};


int main(int argc, char *argv[])
{


    
    int maximum_clients;

    /* command line: server [port_number] */

    int port;
    if(argc >= 2)
        sscanf(argv[1], "%d", &port); /* read the port number if provided */
    else
        port = SERV_TCP_PORT;


    if (argc == 3) {
        sscanf(argv[2], "%d", &maximum_clients); /* read the maximum number of clients if provided */
    } else {
        maximum_clients = MAX_CLIENTS_DEFAULT;
    }

    if (argc > 3) {
        printf("Incorrect Syntax! Usage: ./server <PORT> <MAX_CLIENTS>\n");
        exit(1);
    }
    
    
    int sockfd, new_client_socketfd, clilen;
    struct sockaddr_in cli_addr, serv_addr;
    pthread_t client_threads[maximum_clients];
    struct client_data clients[maximum_clients];
    int client_count = 0;


    // Function to handle SIGINT (Ctrl+C) signal
    void sigint_handler(int signal_id) {
            if(signal_id == SIGINT || signal_id == SIGTERM) {
                printf("\nClosing server...\n");

                for (int i = 0; i < maximum_clients; i++)
                {
                    close(clients[i].socketfd);
                }
                close(sockfd);

                signal(signal_id, SIG_DFL);
                raise(signal_id); // This will trigger the default action for signal_id
            }
    }

    signal(SIGINT, sigint_handler);


    // Clear all socketfds in client array
    for (int i = 0; i < maximum_clients; i++)
    {
        struct client_data empty;
        clients[i] = empty;
    }


    void* handle_client(void* args) {
        int client_index = *(int*) args;
        struct client_data client = clients[client_index];

        for (int i = 0; i < maximum_clients; i++)
        {
            // Broadcast to all clients that a new client has joined the public room!
            char output_message[MAX_INPUT_OUTPUT_SIZE];
            sprintf(output_message, "%s has joined the Public Room!", client.client_id);

            if(clients[i].socketfd != 0) {
                write(clients[i].socketfd, output_message, sizeof(output_message));
            }
        }


        char string[MAX_INPUT_OUTPUT_SIZE];
        char string_copy[MAX_INPUT_OUTPUT_SIZE];
        int len;
        char client_address_string[30];

        // Get the client's address in dot notation and store it into client_address_string
        inet_ntop(AF_INET, &client.client_address.sin_addr, client_address_string, sizeof(client_address_string));

        while (1) {
            len = read(client.socketfd, string, MAX_INPUT_OUTPUT_SIZE);
            /* make sure it's a proper string */
            string[len] = 0;

            if (len == 0) {
                client_count--;

                printf("\nClient with ID %s has disconnected.\n", client.client_id);

                if(client_count == 0) {
                    printf("\nThere are currently no clients connected. Shutting server down...\n");

                    raise(SIGINT);
                    return NULL;
                }

                // Empty the client in the clients array
                struct client_data empty;
                clients[client_index] = empty;

                return NULL;
            }

            if(len > 0) {
                strcpy(string_copy, string);

                int too_many_arguments = 0;
                char* command_components[4];
                char* token;

                /* get the first token */
                char delimiter[2] = "\"";
                token = strtok(string, delimiter);
                
                /* walk through other tokens */
                int i = 0;
                while(token != NULL) {

                    if(i > 3) {
                        // Too many tokens in command
                        char error_message[74] = "The command has too many arguments! Usage: send \"<RECIPIENT>\" \"<MESSAGE>\"";
                        write(client.socketfd, error_message, sizeof(error_message));

                        too_many_arguments = 1;
                        break;
                    }
                    command_components[i] = token;
                    
                    token = strtok(NULL, delimiter);
                    i++;
                }

                if(too_many_arguments != 0) {
                    continue;
                }


                int command_syntax_error = 0;

                // NOTE: the command name will have an extra space at the end
                char* command_name = command_components[0];
                // Remove the extra space
                command_name[strcspn(command_name, " ")] = 0;


                char* recipient = command_components[1];


                if (strcmp(command_components[2], " ") != 0) {
                    // There is no space " " character at this index, which implies
                    // an error in the syntax of the command
                    command_syntax_error = 1;
                }
                char* message = command_components[3];

                if (strcmp(command_name, "send") != 0) {
                    // Command used is not the "send" command
                    command_syntax_error = 1;
                }

                char last_character_of_command = string_copy[strlen(string_copy) - 1];

                if (last_character_of_command != '\"') {
                    // The last character of the command is not a quotation mark, indicating
                    // an error in the syntax of the command
                    command_syntax_error = 1;
                }

                if (command_syntax_error != 0) {
                    // There is an error with the syntax of the command sent to the server
                    char error_message[56] = "Incorrect Syntax! Usage: send \"<RECIPIENT>\" \"<MESSAGE>\"";
                    write(client.socketfd, error_message, sizeof(error_message));

                    continue;
                }

                if (strcmp(recipient, "room") == 0) {
                    // Send the message to all clients

                    for (int i = 0; i < maximum_clients; i++)
                    {
                        char output[MAX_INPUT_OUTPUT_SIZE];
                        sprintf(output, "%s-->[%s]: %s", client.client_id, recipient, message);
                        
                        if(clients[i].socketfd != 0) {
                            write(clients[i].socketfd, output, sizeof(output));
                        }
                    }
                } else {
                    struct client_data receiving_client;
                    int client_found = 0; // 0 for not found, 1 for found

                    for (int i = 0; i < maximum_clients; i++) {
                        if (strcmp(recipient, clients[i].client_id) == 0) {
                            // We have found the client
                            client_found = 1;
                            receiving_client = clients[i];
                        }
                    }

                    if (client_found == 0) {
                        // We did not find the receiving_client
                        char error_message[MAX_INPUT_OUTPUT_SIZE];
                        sprintf(error_message, "Could not find a client with ID %s", recipient);
                        write(client.socketfd, error_message, sizeof(error_message));

                        continue;
                    }

                    if (strcmp(receiving_client.client_id, client.client_id) == 0) {
                        // Recipient's ID is the same as the sender's ID
                        char error_message[38] = "You can't send a message to yourself!";
                        write(client.socketfd, error_message, sizeof(error_message));

                        continue;
                    }

                    // We found the receiving_client
                    char output[MAX_INPUT_OUTPUT_SIZE];
                    sprintf(output, "%s-->[%s]: %s", client.client_id, receiving_client.client_id, message);

                    write(receiving_client.socketfd, output, sizeof(output));
                    write(client.socketfd, output, sizeof(output));
                }

                printf("[COMMAND] from [CLIENT - ID: %s]: %s\n", client.client_id, string_copy);
                
            }
            
        } 
        
    }

    /* open a TCP socket (an Internet stream socket) */
    if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        perror("can't open stream socket");
        exit(1);
    }

    /* bind the local address, so that the client can send to server */
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(port);

    if(bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("can't bind local address");
        exit(1);
    }

    /* listen to the socket */
    listen(sockfd, maximum_clients);


    while (1) {

        /* wait for a connection from a client; this is an iterative server */
        clilen = sizeof(cli_addr);
        new_client_socketfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if(new_client_socketfd < 0) {
            perror("can't bind local address");
        }

        int free_index = 0;
        int found_free_index = 0;

        for (int i = 0; i < maximum_clients; i++)
        {
            if (clients[i].socketfd == 0) {
                free_index = i;
                found_free_index = 1;
                break;
            }
        }

        if (found_free_index == 0) {
            // Didn't find any free index
            printf("A new client tried to connect but there wasn't enough space on the server\n");
            
            char error_message[57] = "There isn't enough space on the server for a new client.";
            write(new_client_socketfd, error_message, sizeof(error_message));

            close(new_client_socketfd);
            continue;
        }


        char new_client_id[MAX_CLIENT_ID_SIZE];
        
        read(
            new_client_socketfd,
            new_client_id,
            MAX_CLIENT_ID_SIZE
        );

        int error_accepting_client = 0;

        if (strcmp(new_client_id, "room") == 0) {
            // The client's ID is "room", which is not allowed.
            // The "room" recipient name is reserved for the public chatroom.

            char error_message[40] = "The client's client ID cannot be \"room\"";
            write(new_client_socketfd, error_message, sizeof(error_message));
            close(new_client_socketfd);

            error_accepting_client = 1;
        }

        for (int i = 0; i < maximum_clients; i++)
        {
            if (strcmp(new_client_id, clients[i].client_id) == 0) {
                // The client's ID is already in use.

                char error_message[27] = "This ID is already in use.";
                write(new_client_socketfd, error_message, sizeof(error_message));
                close(new_client_socketfd);

                error_accepting_client = 1;
            }
        }

        if (error_accepting_client != 0) {
            // There was an error in accepting the client
            continue;
        }

        // Add the client to the clients array
        clients[free_index].socketfd = new_client_socketfd;
        clients[free_index].client_address = cli_addr;
        strcpy(clients[free_index].client_id, new_client_id);

        printf("Connection from client with ID %s\n", clients[free_index].client_id);

        int *arg = malloc(sizeof(*arg));
        if ( arg == NULL ) {
            fprintf(stderr, "Couldn't allocate memory.\n");
            exit(1);
        }

        *arg = free_index;

        pthread_create(&client_threads[free_index], NULL, handle_client, arg);

        client_count += 1;
    }




}

/*
TO RUN THIS CODE:
	gcc final_project_server.c -lpthread -o server
	./server 7777 20


	NOTE: 7777 is the port number
    NOTE: 20 is the maximum number of clients allowed to connect to
    the server at any given time

*/
