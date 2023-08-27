#include <string.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>

#define MAX_CLIENT_ID_SIZE 21
#define MAX_INPUT_OUTPUT_SIZE 300
#define SERV_TCP_PORT 23 /* server's port */


char INPUT_PROMPT[5] = ">>> ";


struct server_data {
    int socketfd;
    struct sockaddr_in server_address;
} server;

int reprint_prompt = 1;
pthread_mutex_t print_prompt_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t print_prompt_condition_variable = PTHREAD_COND_INITIALIZER;


void* handle_server(void* args) {
    char string[MAX_INPUT_OUTPUT_SIZE];
    int len;
    char server_address_string[30];

    // Get the client's address in dot notation and store it into client_address_string
    inet_ntop(AF_INET, &server.server_address.sin_addr, server_address_string, sizeof(server_address_string));

    char backspaces[50];
    for(int i = 0; i < 50; i++) {
        backspaces[i] = '\b';
    }

    while (1) {
        len = read(server.socketfd, string, MAX_INPUT_OUTPUT_SIZE);
        /* make sure it's a proper string */
        string[len] = 0;

        if (len == 0) {
            printf("%s", backspaces);
            fflush(stdout);

            printf("Server with address %s has disconnected from the client.\n", server_address_string);

            raise(SIGINT);
            return NULL;
        }

        if(len > 0) {
            printf("%s", backspaces);
            fflush(stdout);

            printf("%s\n", string);
            fflush(stdout);

            pthread_mutex_lock(&print_prompt_mutex);

            reprint_prompt = 1;
            pthread_cond_signal(&print_prompt_condition_variable);

            pthread_mutex_unlock(&print_prompt_mutex);
        }
        
    } 
    
}

void* input_and_send(void* args) {

    while (1) {
        char input_string[MAX_INPUT_OUTPUT_SIZE];

        fgets(input_string, MAX_INPUT_OUTPUT_SIZE, stdin);
        input_string[strcspn(input_string, "\n")] = 0;

        pthread_mutex_lock(&print_prompt_mutex);

        reprint_prompt = 1;
        pthread_cond_signal(&print_prompt_condition_variable);

        pthread_mutex_unlock(&print_prompt_mutex);

        write(server.socketfd, input_string, sizeof(input_string));
    }

}

void* prompter(void* args) {
    char backspaces[50];

    for (int i = 0; i < 50; i++) {
        backspaces[i] = *"\b";
    }
    
    while (1) {
        pthread_mutex_lock(&print_prompt_mutex);

        while (reprint_prompt == 0) {
            pthread_cond_wait(&print_prompt_condition_variable, &print_prompt_mutex);
        }

        reprint_prompt = 0;

        printf("%s%s", backspaces, INPUT_PROMPT);
        fflush(stdout);

        pthread_mutex_unlock(&print_prompt_mutex);
    }
}

int main(int argc, char *argv[])
{
  char client_id[MAX_CLIENT_ID_SIZE];
  int sockfd;
  struct sockaddr_in serv_addr;
  char *serv_host = "localhost";
  struct hostent *host_ptr;
  int port;
  int buff_size = 0;

  // Function to handle SIGINT (Ctrl+C) signal
  void sigint_handler(int signal_id) {
        if(signal_id == SIGINT || signal_id == SIGTERM) {
            printf("\nClosing client...\n");
            
            close(server.socketfd);

            signal(signal_id, SIG_DFL);
            raise(signal_id); // This will trigger the default action for signal_id
        }
  }

  signal(SIGINT, sigint_handler);

  if(argc > 4) {
    printf("Too many arguments! Please try again...\n");
    exit(1);
  }

  if (argc >= 2) {
    memcpy(client_id, argv[1], MAX_CLIENT_ID_SIZE - 1);
    client_id[MAX_CLIENT_ID_SIZE - 1] = '\0';
  }

  if (argc >= 3) {
    serv_host = argv[2];
  }

  if (argc == 4) {
    sscanf(argv[3], "%d", &port);
  }
  else {
    port = SERV_TCP_PORT;
  }

  printf("Welcome to the application, %s! We are connecting to %s:%d...\n", client_id, serv_host, port);

  /* get the address of the host */
  if((host_ptr = gethostbyname(serv_host)) == NULL) {
     perror("gethostbyname error");
     exit(1);
  }

  if(host_ptr->h_addrtype !=  AF_INET) {
     perror("unknown address type");
     exit(1);
  }

  bzero((char *) &serv_addr, sizeof(serv_addr));
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_addr.s_addr =
     ((struct in_addr *)host_ptr->h_addr_list[0])->s_addr;
  serv_addr.sin_port = htons(port);


  /* open a TCP socket */
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
     perror("can't open stream socket");
     exit(1);
  }

  /* connect to the server */
  if(connect(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
     perror("can't connect to server");
     exit(1);
  }


  server.socketfd = sockfd;
  server.server_address = serv_addr;

  // Send client name to server
  write(server.socketfd, client_id, sizeof(client_id));

  pthread_t server_thread;
  pthread_t prompter_thread;
  pthread_t input_and_send_thread;


  pthread_create(&server_thread, NULL, handle_server, NULL);
  pthread_create(&prompter_thread, NULL, prompter, NULL);
  pthread_create(&input_and_send_thread, NULL, input_and_send, NULL);

  pthread_join(server_thread, NULL);
  pthread_join(prompter_thread, NULL);
  pthread_join(input_and_send_thread, NULL);
}

/*
TO RUN THIS CODE:
	gcc final_project_client.c -lpthread -o client
	./client client1 localhost 7777
	OR
	./client client1 127.0.0.1 7777

    NOTE: client1 is the ID of the client that will be registered with the server
*/
