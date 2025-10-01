#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>
#include <sys/queue.h>

#define PORT 9000
#define BUFFER_SIZE 1024
#define OUTPUT_FILE "/var/tmp/aesdsocketdata"

// Global flag for signal handling
volatile sig_atomic_t caught_sigint = 0;

// Mutex for file access
pthread_mutex_t file_mutex;

// Thread data structure
struct thread_data {
    pthread_t thread_id;
    int client_fd;
    struct sockaddr_in client_addr;
    int thread_complete;
    SLIST_ENTRY(thread_data) entries;
};

// Linked list head
SLIST_HEAD(thread_list_head, thread_data) thread_list_head;

// Signal handler function
static void signal_handler(int signo) {
    if (signo == SIGINT || signo == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        caught_sigint = 1;
    }
}

// Thread function to handle each client connection
void *handle_client(void *thread_param) {
    struct thread_data *data = (struct thread_data *)thread_param;
    char buffer[BUFFER_SIZE];
    int bytes_received;
    int file_fd;

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(data->client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    file_fd = open(OUTPUT_FILE, O_CREAT | O_RDWR | O_APPEND, 0644);
    if (file_fd == -1) {
        syslog(LOG_ERR, "Failed to open output file");
        close(data->client_fd);
        data->thread_complete = 1;
        return NULL;
    }

    while ((bytes_received = recv(data->client_fd, buffer, sizeof(buffer), 0)) > 0) {
        pthread_mutex_lock(&file_mutex);
        write(file_fd, buffer, bytes_received);
        pthread_mutex_unlock(&file_mutex);

        if (memchr(buffer, '\n', bytes_received) != NULL) {
            lseek(file_fd, 0, SEEK_SET);
            pthread_mutex_lock(&file_mutex);
            while ((bytes_received = read(file_fd, buffer, sizeof(buffer))) > 0) {
                send(data->client_fd, buffer, bytes_received, 0);
            }
            pthread_mutex_unlock(&file_mutex);
        }
    }

    close(file_fd);
    close(data->client_fd);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
    data->thread_complete = 1;

    return NULL;
}

// Thread function for timestamping
void *timestamp_thread_func(void *arg) {
    while (!caught_sigint) {
        sleep(10);
        
        if (caught_sigint) break;

        time_t current_time = time(NULL);
        struct tm *local_time = localtime(&current_time);
        char timestamp[100];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", local_time);

        pthread_mutex_lock(&file_mutex);
        int file_fd = open(OUTPUT_FILE, O_WRONLY | O_APPEND);
        if (file_fd != -1) {
            write(file_fd, timestamp, strlen(timestamp));
            close(file_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}


int main(int argc, char *argv[]) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len;
    struct sigaction sa;
    int daemon_mode = 0;

    openlog("aesdsocket", LOG_PID, LOG_USER);

    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon_mode = 1;
    }

    // Set up signal handling
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Failed to create socket");
        return -1;
    }
    
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }


    // Bind socket
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind");
        close(server_fd);
        return -1;
    }

    // Daemonize if requested
    if (daemon_mode) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Failed to fork");
            exit(EXIT_FAILURE);
        }
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }
        if (setsid() < 0) {
            exit(EXIT_FAILURE);
        }
        // Close standard file descriptors
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }
    
    // Listen for connections
    if (listen(server_fd, 5) == -1) {
        syslog(LOG_ERR, "Failed to listen");
        close(server_fd);
        return -1;
    }
    
    // Initialize mutex and linked list
    pthread_mutex_init(&file_mutex, NULL);
    SLIST_INIT(&thread_list_head);

    // Start timestamp thread
    pthread_t timestamp_thread;
    pthread_create(&timestamp_thread, NULL, timestamp_thread_func, NULL);

    // Main loop
    while (!caught_sigint) {
        client_addr_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_fd == -1) {
            if (caught_sigint) break;
            continue;
        }

        struct thread_data *new_thread = (struct thread_data *)malloc(sizeof(struct thread_data));
        new_thread->client_fd = client_fd;
        new_thread->client_addr = client_addr;
        new_thread->thread_complete = 0;
        pthread_create(&new_thread->thread_id, NULL, handle_client, new_thread);
        SLIST_INSERT_HEAD(&thread_list_head, new_thread, entries);

        // Clean up completed threads
        struct thread_data *current, *tmp;
        SLIST_FOREACH_SAFE(current, &thread_list_head, entries, tmp) {
            if (current->thread_complete) {
                pthread_join(current->thread_id, NULL);
                SLIST_REMOVE(&thread_list_head, current, thread_data, entries);
                free(current);
            }
        }
    }

    syslog(LOG_INFO, "Caught signal, exiting");

    // Cleanup
    struct thread_data *current, *tmp;
    SLIST_FOREACH_SAFE(current, &thread_list_head, entries, tmp) {
        pthread_join(current->thread_id, NULL);
        SLIST_REMOVE(&thread_list_head, current, thread_data, entries);
        free(current);
    }

    // Signal and join the timestamp thread
    pthread_join(timestamp_thread, NULL);


    close(server_fd);
    remove(OUTPUT_FILE);
    pthread_mutex_destroy(&file_mutex);
    closelog();

    return 0;
}