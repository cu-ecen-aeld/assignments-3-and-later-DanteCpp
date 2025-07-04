#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <sys/types.h> 

#define PORT      9000
#define FILEPATH  "/var/tmp/aesdsocketdata"
#define BACKLOG   1

static volatile sig_atomic_t stop = 0;

static void sig_handler(int sig) { stop = 1; }

static int daemonise(void)
{
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid) _exit(EXIT_SUCCESS);          /* parent */
    if (setsid() < 0) return -1;
    if (chdir("/") < 0) return -1;
    fclose(stdin); fclose(stdout); fclose(stderr);
    return 0;
}

int main(int argc, char **argv)
{
    int daemon = (argc == 2 && !strcmp(argv[1], "-d"));
    openlog("aesdsocket", LOG_PID, LOG_USER);

    /* Setup signal handlers BEFORE we start work */
    struct sigaction sa = {.sa_handler = sig_handler};
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    /* Create, bind, listen */
    int lsock = socket(AF_INET, SOCK_STREAM, 0);
    if (lsock < 0) { 
        syslog(LOG_ERR, "socket: %m"); 
        exit(EXIT_FAILURE); 
    }

    struct sockaddr_in addr = {.sin_family = AF_INET,
                               .sin_addr.s_addr = htonl(INADDR_ANY),
                               .sin_port = htons(PORT)};
    if (bind(lsock, (struct sockaddr *)&addr, sizeof(addr)) < 0 || 
        listen(lsock, BACKLOG) < 0) 
    {   
        syslog(LOG_ERR, "bind/listen: %m"); 
        exit(EXIT_FAILURE); 
    }

    // Daemonise if requested
    if (daemon && daemonise() < 0) { 
        syslog(LOG_ERR, "daemonise: %m"); 
        exit(EXIT_FAILURE); 
    }

    // Start of the service
    while (!stop) {
        struct sockaddr_in caddr;
        socklen_t clen = sizeof(caddr);
        
        int csock = accept(lsock, (struct sockaddr *)&caddr, &clen);
        if (csock < 0) {
            if (errno == EINTR) break;
            syslog(LOG_ERR, "accept: %m");
            continue;
        }

        char cip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &caddr.sin_addr, cip, sizeof(cip));
        syslog(LOG_INFO, "Accepted connection from %s", cip);

        /* ---- receive until newline ---- */
        size_t cap = 1024, len = 0;
        char *buf = malloc(cap);
        if (!buf) { 
            syslog(LOG_ERR, "malloc: %m"); 
            close(csock); 
            continue; 
        }

        while (!stop) {
            ssize_t r = recv(csock, buf + len, cap - len, 0);
            if (r <= 0) { len = 0; break; }          /* peer closed or error */
            len += r;
            /* grow if needed */
            if (len == cap) { cap *= 2; buf = realloc(buf, cap); }

            if (memchr(buf, '\n', len)) break;       /* packet complete */
        }

        /* ---- append and echo file ---- */
        if (len) {
            int fd = open(FILEPATH, O_CREAT | O_WRONLY | O_APPEND, 0644);
            write(fd, buf, len);
            close(fd);

            fd = open(FILEPATH, O_RDONLY);
            off_t off = 0;
            struct stat st; fstat(fd, &st);
            while (off < st.st_size)
                off += sendfile(csock, fd, &off, st.st_size - off);
            close(fd);
        }
        free(buf);

        syslog(LOG_INFO, "Closed connection from %s", cip);
        close(csock);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(lsock);
    remove(FILEPATH);
    closelog();
    return 0;
}
