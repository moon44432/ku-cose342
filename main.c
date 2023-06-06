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
#define SEND_MESSAGE_BUFSIZE 1024
#define MAX_CONN 256

#define KEEP_ALIVE_MAX 100
#define KEEP_ALIVE_TIMEOUT 5

void req_handler(void *, char *);
void GET_handler(char *, char *, char *, char *, int);

typedef struct
{
    int is_persistent;
    time_t timestamp;
    int cnt;
} conn_state;

conn_state persistent_conn[MAX_CONN];

int main(int argc, char **argv)
{
    int sockfd, new_fd;
    struct sockaddr_in my_addr;
    struct sockaddr_in their_addr;
    int sin_size;
    int opt = 1;

    fd_set master_fds, temp_fds;
    int fdmax;

    for (int i = 0; i < MAX_CONN; i++)
    {
        persistent_conn[i].is_persistent = 0;
        persistent_conn[i].cnt = KEEP_ALIVE_MAX;
    }

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

        printf("fdmax: %d\n", fdmax);

        for (int fd = 0; fd <= fdmax; fd++)
        {
            printf("%d, time: %d\n", fd, time(NULL) - persistent_conn[fd].timestamp);
            if (FD_ISSET(fd, &temp_fds))
            {
                if (fd == sockfd)
                {
                    int status = fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

                    sin_size = sizeof(struct sockaddr_in);
                    if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size)) == -1)
                    {
                        printf("err: accept\n");
                        break;
                    }
                    printf("server: got connection from %s, Conn [%d]\n", inet_ntoa(their_addr.sin_addr), new_fd);
                    FD_SET(new_fd, &master_fds);
                    if (fdmax < new_fd)
                    {
                        fdmax = new_fd;
                    }
                    persistent_conn[sockfd].timestamp = time(NULL);
                }
                else
                {
                    if (persistent_conn[fd].is_persistent == 1 &&
                        (persistent_conn[fd].cnt == 0 ||
                        time(NULL) - persistent_conn[fd].timestamp > KEEP_ALIVE_TIMEOUT))
                    {
                        FD_CLR(fd, &master_fds);
                        shutdown(fd, SHUT_RDWR);
                        close(fd);
                        printf("\n\n Conn [%d] Closed!!\n\n", fd);
                        persistent_conn[fd].is_persistent = 0;
                    }

                    req_handler(&fd, argv[2]);

                    printf("!!\n");
                    if (persistent_conn[fd].is_persistent == 0)
                    {
                        FD_CLR(fd, &master_fds);
                        shutdown(fd, SHUT_RDWR);
                        close(fd);
                        printf("\n\n Conn [%d] Closed!!\n\n", fd);
                        persistent_conn[fd].is_persistent = 0;
                    }

                    printf("!!!\n");
                }
            }
        }
        usleep(1000);
    }
    close(new_fd);
    return 0;
}

void req_handler(void *req, char *rootdir)
{
    char msg[BUFSIZE];
    char *firstline[3], *msghead;

    int sd = *(int *)req;
    printf("Waiting Msg From %d...\n", sd);

    if (recv(sd, msg, BUFSIZE - 1, MSG_PEEK|MSG_DONTWAIT) == 0)
        return;

    int rcvd = recv(sd, msg, BUFSIZE - 1, 0);
    if (rcvd <= 0)
    {
        printf("err: rcv\n");
        return;
    }
    printf("===Req Message===\n%s\n=================\n", msg);

    char METHOD[4], VERSION[10], URL[SEND_MESSAGE_BUFSIZE];

    msghead = msg;
    char *connection_header = strstr(msghead, "Connection:");
    if (connection_header != NULL)
    {
        if (strstr(connection_header, "keep-alive") != NULL)
        {
            printf("Conn [%d]\n", sd);
            if (persistent_conn[sd].is_persistent == 0)
            {
                printf("\nUsing Persistent Connection\n");
                persistent_conn[sd].is_persistent = 1;
                persistent_conn[sd].cnt = KEEP_ALIVE_MAX;
            }
        }
        else
        {
            printf("\nUsing Non-Persistent Connection\n");
            persistent_conn[sd].is_persistent = 0;
        }
    }

    strcpy(METHOD, strtok(msg, " \t\n"));
    strcpy(URL, strtok(NULL, " \t"));
    strcpy(VERSION, strtok(NULL, " \t\n"));

    printf("METHOD: %s\nURL: %s\nVER: %s\n", METHOD, URL, VERSION);

    if (persistent_conn[sd].is_persistent == 1)
        persistent_conn[sd].cnt--;

    if (!strncmp(METHOD, "GET", 3)) GET_handler(VERSION, msg, URL, rootdir, sd);

    if (persistent_conn[sd].is_persistent == 1)
    {
        persistent_conn[sd].timestamp = time(NULL);
        printf("Done! [%d] timestamp %d cnt %d\n", sd, persistent_conn[sd].timestamp, persistent_conn[sd].cnt);
    }
}

void GET_handler(char *ver, char *msg, char *url, char *rootdir, int client)
{
    int fd, len, is_rootdir = 0;
    char SEND_DATA[SEND_MESSAGE_BUFSIZE];
    char FINAL_PATH[BUFSIZE];
    char VERSION[10], URL[SEND_MESSAGE_BUFSIZE];

    strcpy(VERSION, ver);
    strcpy(URL, url);

    if (strncmp(VERSION, "HTTP/1.0", 8) != 0 && strncmp(VERSION, "HTTP/1.1", 8) != 0)
    {
        send(client, "HTTP/1.1 400 Bad Request\n\n", 26, 0);
        return;
    }
    
    if (strlen(URL) == 1 && !strncmp(URL, "/", 1))
    {
        strcpy(FINAL_PATH, rootdir);
        strcat(FINAL_PATH, "/index.html");
        is_rootdir = 1;
    }
    else
    {
        strcpy(FINAL_PATH, rootdir);
        strcat(FINAL_PATH, url);
    }
    printf("FINAL_PATH: %s\n", FINAL_PATH);

    if ((fd = open(FINAL_PATH, O_RDONLY)) != -1)
    {
        if (persistent_conn[client].is_persistent == 1)
        {
            printf("\nUsing Keep-Alive\n");

            char buf[512] = "";
            sprintf(buf, "HTTP/1.1 200 OK\nConnection: keep-alive\nKeep-Alive: timeout=%d, max=%d\n",
                    KEEP_ALIVE_TIMEOUT, persistent_conn[client].cnt);

            FILE *fp = fopen(FINAL_PATH, "r");
            fseek(fp, 0, SEEK_END);
            printf("Length: %d\n", ftell(fp));
            sprintf(buf + strlen(buf), "Content-Length: %d\n", ftell(fp));
            rewind(fp);
            fclose(fp);

            sprintf(buf + strlen(buf), "\n");
            printf("\n SENDING:\n%s", buf);
            send(client, buf, strlen(buf), 0);
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
        printf(" Completely sent %s!\n", FINAL_PATH);
    }
    else
    {
        send(client, "HTTP/1.1 404 Not Found\n", 23, 0);
    }
}