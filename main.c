#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <memory.h>
#include <fcntl.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#define BACKLOG 10
#define BUFSIZE 65536
#define SEND_MESSAGE_BUFSIZE 1024

void req_handler(void *, char *);
void GET_handler(char *, char *, char *, char *, int);

int main(int argc, char **argv)
{
    int sockfd, new_fd;
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr;
    int sin_size;
    int opt = 1;

    fd_set master_fds, temp_fds;
    int fdmax;

    if (argc != 3)
    {
        printf("usage: %s <port#> <rootdir>\n", argv[0]);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        printf("err: socket\n");
        exit(1);
    }

    my_addr.sin_family = AF_INET;
    my_addr.sin_port = htons(atoi(argv[1]));
    my_addr.sin_addr.s_addr = INADDR_ANY;
    memset(&(my_addr.sin_zero), '\0', 8);

    if (bind(sockfd, (struct sockaddr*)&my_addr, sizeof(struct sockaddr)) == -1)
    {
        printf("err: bind\n");
        exit(1);
    }

    if (listen(sockfd, BACKLOG) == -1)
    {
        printf("err: listen\n");
        exit(1);
    }

    FD_ZERO(&master_fds);
    FD_ZERO(&temp_fds);
    FD_SET(sockfd, &master_fds);
    fdmax = sockfd;

    while (1)
    {
        temp_fds = master_fds;
        
        if (select(fdmax + 1, &temp_fds, NULL, NULL, NULL) == -1)
        {
            printf("err: select\n");
            exit(1);
        }

        for (int fd = 0; fd <= fdmax; fd++)
        {
            if (FD_ISSET(fd, &temp_fds))
            {
                if (fd == sockfd)
                {
                    sin_size = sizeof(struct sockaddr_in);
                    if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1)
                    {
                        printf("err: accept\n");
                        break;
                    }
                    printf("server: got connection from %s\n", inet_ntoa(their_addr.sin_addr));
                    
                    FD_SET(new_fd, &master_fds);
                    if (fdmax < new_fd)
                    {
                        fdmax = new_fd;
                    }
                }
                else
                {
                    req_handler(&fd, argv[2]);

                    FD_CLR(fd, &master_fds);
                    shutdown(fd, SHUT_RDWR);
                    close(fd);
                }
            }
        }
    }
    close(new_fd);
    return 0;
}

void req_handler(void *req, char *rootdir)
{
    char msg[BUFSIZE];
    char *firstline[3];

    int sd = *(int *)req;
    int rcvd = recv(sd, msg, BUFSIZE - 1, 0);
    if (rcvd <= 0)
    {
        printf("err: rcv\n");
        exit(1);
    }
    printf("===Req Message===\n%s\n=================\n", msg);

    char METHOD[4], VERSION[10], URL[SEND_MESSAGE_BUFSIZE];

    strcpy(METHOD, strtok(msg, " \t\n"));
    strcpy(URL, strtok(NULL, " \t"));
    strcpy(VERSION, strtok(NULL, " \t\n"));

    printf("METHOD: %s\nURL: %s\nVER: %s\n", METHOD, URL, VERSION);
    if (!strncmp(METHOD, "GET", 3)) GET_handler(VERSION, msg, URL, rootdir, sd);
}

void GET_handler(char *ver, char *msg, char *url, char *rootdir, int client)
{
    int fd, len;
    char SEND_DATA[SEND_MESSAGE_BUFSIZE];
    char FINAL_PATH[BUFSIZE];
    char VERSION[10], URL[SEND_MESSAGE_BUFSIZE];

    strcpy(VERSION, ver);
    strcpy(URL, url);

    if (strncmp(VERSION, "HTTP/1.0", 8) != 0 && strncmp(VERSION, "HTTP/1.1", 8) != 0)
    {
        send(client, "HTTP/1.1 400 Bad Request\n", 25, 0);
    }
    
    if (strlen(URL) == 1 && !strncmp(URL, "/", 1))
    {
        strcpy(FINAL_PATH, rootdir);
        strcat(FINAL_PATH, "/index.html");
    }
    else
    {
        strcpy(FINAL_PATH, rootdir);
        strcat(FINAL_PATH, url);
    }
    printf("FINAL_PATH: %s\n", FINAL_PATH);

    if ((fd = open(FINAL_PATH, O_RDONLY)) != -1)
    {
        send(client, "HTTP/1.1 200 OK\n\n", 17, 0);
        while (1)
        {
            len = read(fd, SEND_DATA, SEND_MESSAGE_BUFSIZE);
            if (len <= 0) break;
            write(client, SEND_DATA, len);
        }
    }
    else
    {
        send(client, "HTTP/1.1 404 Not Found\n", 23, 0);
    }
}