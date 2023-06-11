#include <stdio.h>
#include <unistd.h>
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
#define HEADSIZE 8192
#define SEND_MESSAGE_BUFSIZE 1024
#define MAX_CONN 256

int req_handler(void *, char *);
void GET_handler(char *, char *, char *, char *, int);

int is_persistent[MAX_CONN];

int main(int argc, char **argv)
{
    int sockfd, new_fd;
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr;
    int sin_size;
    int opt = 1;

    fd_set master_fds, temp_fds;
    int fdmax;

    for (int i = 0; i < MAX_CONN; i++) is_persistent[i] = 0;

    if (argc != 3)
    {
        printf("usage: %s <port#> <rootdir>\n", argv[0]);
        exit(1);
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
    
    int optval = 1;

    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1)
    {
        printf("err: sockopt\n");
        exit(1);
    }
    
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
                    printf("dbg: got connection from %s, conn #%d\n\n", inet_ntoa(their_addr.sin_addr), new_fd);
                    FD_SET(new_fd, &master_fds);
                    if (fdmax < new_fd)
                    {
                        fdmax = new_fd;
                    }
                }
                else
                {
                    int rval = req_handler(&fd, argv[2]);

                    if (is_persistent[fd] == 0 || rval <= 0)
                    {
                        FD_CLR(fd, &master_fds);
                        close(fd);
                        printf("dbg: conn #%d closed\n\n", fd);
                        is_persistent[fd] = 0;
                    }
                }
            }
        }
        usleep(1000);
    }
    close(new_fd);
    return 0;
}

int req_handler(void *req, char *rootdir)
{
    char msg[BUFSIZE];
    char *firstline[3], *msghead;

    int sd = *(int *)req;

    printf("dbg: waiting msg from conn #%d...\n", sd);
    int rcvd = recv(sd, msg, BUFSIZE - 1, 0);
    if (rcvd == 0)
    {
        printf("dbg: client closed the connection\n");
        return 0;
    }
    if (rcvd < 0)
    {
        printf("err: rcv\n");
        return -1;
    }
    printf("dbg: received a request\n", msg);

    char METHOD[4], VERSION[10], URL[SEND_MESSAGE_BUFSIZE];

    msghead = msg;
    char *connection_header = strstr(msghead, "Connection:");
    if (connection_header != NULL)
    {
        if (strstr(connection_header, "keep-alive") != NULL)
        {
            printf("dbg: use persistent connection\n");
            is_persistent[sd] = 1;
        }
        else
        {
            printf("dbg: use non-persistent connection\n");
        }
    }

    strcpy(METHOD, strtok(msg, " \t\n"));
    strcpy(URL, strtok(NULL, " \t"));
    strcpy(VERSION, strtok(NULL, " \t\n"));

    if (!strncmp(METHOD, "GET", 3)) GET_handler(VERSION, msg, URL, rootdir, sd);

    if (is_persistent[sd] == 1)
    {
        printf("dbg: successfully handled request to conn #%d\n\n", sd);
    }
    return 1;
}

void GET_handler(char *ver, char *msg, char *url, char *rootdir, int client)
{
    int fd, len;
    char SEND_DATA[SEND_MESSAGE_BUFSIZE], FINAL_PATH[BUFSIZE], VERSION[10], URL[SEND_MESSAGE_BUFSIZE];
    char headbuf[HEADSIZE];

    strcpy(VERSION, ver);
    strcpy(URL, url);

    if (strncmp(VERSION, "HTTP/1.0", 8) != 0 && strncmp(VERSION, "HTTP/1.1", 8) != 0)
    {
        send(client, "HTTP/1.1 400 Bad Request\n\n", 26, 0);
        return;
    }
    
    strcpy(FINAL_PATH, rootdir);
    if (strlen(URL) == 1 && !strncmp(URL, "/", 1))
    {
        strcat(FINAL_PATH, "/index.html");
    }
    else
    {
        strcat(FINAL_PATH, url);
    }

    printf("dbg: send %s\n", FINAL_PATH);

    if ((fd = open(FINAL_PATH, O_RDONLY)) != -1)
    {
        if (is_persistent[client] == 1)
        {
            printf("dbg: use keep-alive method\n");

            sprintf(headbuf, "HTTP/1.1 200 OK\nConnection: keep-alive\n");

            FILE *fp = fopen(FINAL_PATH, "r");
            fseek(fp, 0, SEEK_END);
            printf("dbg: file length: %d\n", ftell(fp));
            sprintf(headbuf + strlen(headbuf), "Content-Length: %d\n", ftell(fp));
            fclose(fp);

            sprintf(headbuf + strlen(headbuf), "\n");
            send(client, headbuf, strlen(headbuf), 0);
        }
        else
        {
            send(client, "HTTP/1.1 200 OK\n\n", 17, 0);
        }
        while (1)
        {
            len = read(fd, SEND_DATA, SEND_MESSAGE_BUFSIZE);
            if (len <= 0) break;
            write(client, SEND_DATA, len);
        }
        printf("dbg: completely sent %s\n", FINAL_PATH);
    }
    else
    {
        send(client, "HTTP/1.1 404 Not Found\n", 23, 0);
    }
}