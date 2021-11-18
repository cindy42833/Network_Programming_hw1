#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <wait.h>
#include <sys/stat.h>

int create_socket(const char *host, const char *port)
{
    printf("Configuring local address...\n");

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    hints.ai_family = AF_INET;       // AF_INET is IPv4
    hints.ai_socktype = SOCK_STREAM; // use TCP
    hints.ai_flags = AI_PASSIVE;     // listening socket, fill in host IP for me

    struct addrinfo *bind_addr;
    getaddrinfo(host, port, &hints, &bind_addr); // if host is not NULL, AI_PASSIVE flag is ignored

    printf("Creating socket...\n");
    int socket_listen = socket(bind_addr->ai_family, bind_addr->ai_socktype, bind_addr->ai_protocol);

    if (socket_listen < 0)
    {
        perror("Create socket error");
        exit(EXIT_FAILURE);
    }
    int socket_buffer[64];
    setsockopt(socket_listen, SOL_SOCKET, SO_REUSEADDR, socket_buffer, sizeof(socket_buffer));
    printf("Binding socket to local address...\n");
    if (bind(socket_listen, bind_addr->ai_addr, bind_addr->ai_addrlen) == -1)
    {
        perror("Bind socket error");
        exit(EXIT_FAILURE);
    }

    freeaddrinfo(bind_addr);
    printf("Listening...\n");
    int waitingQueue = 10; // defines the maximum length of the queue
    if (listen(socket_listen, waitingQueue) == -1)
    {
        perror("Listen socket error");
        exit(EXIT_FAILURE);
    }

    return socket_listen;
}

void send_400(int connectfd)
{
    const char *header = "HTTP/1.1 400 Bad Request\r\n"
                         "Connection: close\r\n"
                         "Content-Length: 11\r\n\r\nBad Request";
    send(connectfd, header, strlen(header), 0);
    printf("Close 400\n");
}

void send_404(int connectfd)
{
    const char *header = "HTTP/1.1 404 Not Found\r\n"
                         "Connection: close\r\n"
                         "Content-Length: 9\r\n\r\nNot Found";
    send(connectfd, header, strlen(header), 0);
    printf("Close 404\n");
}

const char *get_content_type(const char *path)
{
    const char *last_dot = strrchr(path, '.');
    if (last_dot)
    {
        if (strcmp(last_dot, ".css") == 0)
            return "text/css";
        if (strcmp(last_dot, ".csv") == 0)
            return "text/csv";
        if (strcmp(last_dot, ".gif") == 0)
            return "image/gif";
        if (strcmp(last_dot, ".htm") == 0)
            return "text/html";
        if (strcmp(last_dot, ".html") == 0)
            return "text/html";
        if (strcmp(last_dot, ".ico") == 0)
            return "image/x-icon";
        if (strcmp(last_dot, ".jpeg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".jpg") == 0)
            return "image/jpeg";
        if (strcmp(last_dot, ".js") == 0)
            return "application/javascript";
        if (strcmp(last_dot, ".json") == 0)
            return "application/json";
        if (strcmp(last_dot, ".png") == 0)
            return "image/png";
        if (strcmp(last_dot, ".pdf") == 0)
            return "application/pdf";
        if (strcmp(last_dot, ".svg") == 0)
            return "image/svg+xml";
        if (strcmp(last_dot, ".txt") == 0)
            return "text/plain";
    }

    return "application/octet-stream";
}

unsigned char *extractString(unsigned char *request, const unsigned char *find_str, const unsigned char *delimeter, unsigned char *res)
{
    unsigned char *start = strstr(request, find_str) + strlen(find_str);
    unsigned char *end = strstr(start, delimeter);
    memcpy(res, start, end - start);
    return end;
}

void extractPath(unsigned char *request, unsigned char *path, const unsigned char *type)
{
    char *start_path = request + strlen(type) + 1; // extract path from http header
    char *end_path = strstr(start_path, " ");
    memcpy(path, start_path, end_path - start_path);
}

#define Max_Response 1024
#define Max_Request 4096

#define GET_Response "HTTP/1.1 200 OK\r\n"     \
                     "Connection: close\r\n"   \
                     "Content-Length: %lu\r\n" \
                     "Content-Type: %s\r\n\r\n"

void serve_resource(int connectfd, const unsigned char *path)
{
    printf("serve_resource %s\n", path);
    if (strcmp(path, "/") == 0)
        path = "/index.html";

    if (strlen(path) > 100)
        send_400(connectfd);

    if (strstr(path, "..")) // wrong request URL
        send_404(connectfd);

    unsigned char full_path[128];
    sprintf(full_path, "public%s", path); // concatenate the whole path

    FILE *fp = fopen(full_path, "rb");
    if (!fp)
        send_404(connectfd);
    

    fseek(fp, 0, SEEK_END);
    size_t content_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    const unsigned char *content_type = get_content_type(path);
    unsigned char buf[Max_Response];

    sprintf(buf, GET_Response, content_len, content_type);
    send(connectfd, buf, strlen(buf), 0);
    int wordCnt = fread(buf, 1, Max_Response, fp);
    while (wordCnt)
    {
        send(connectfd, buf, wordCnt, 0);
        wordCnt = fread(buf, 1, Max_Response, fp);
    }

    fclose(fp);
}

void UploadFile(int connectfd, unsigned char *buf, int readCnt)
{
    int fileSize = 0;
    unsigned char *start = NULL, *end = NULL;
    unsigned char tmp[64], boundary_buf[128], filename[128], real_path[256];
    
    memset(tmp, '\0', sizeof(tmp));
    memset(boundary_buf, '\0', sizeof(boundary_buf));
    memset(filename, '\0', sizeof(filename));

    /* For Firefox */
    sprintf(boundary_buf, "--");
    end = extractString(buf, "boundary=", "\r\n", boundary_buf + 2); // extract boundary string

    end = extractString(end, "Content-Length: ", "\r\n", tmp); // extract content length
    fileSize = atoi(tmp);
    /* End for Firefox */

    start = strstr(end, boundary_buf);      // get the start of the content
    fileSize -= (strlen(boundary_buf) + 6); // minor the end of the boundary

    end = extractString(end, "filename=\"", "\"", filename); // extract filename
    sprintf(real_path, "./upload/%s", filename);
    FILE *fp = fopen(real_path, "wb");

    if (!fp)
    {
        perror("Open file error");
    }

    end = strstr(end, "\r\n\r\n") + strlen("\r\n\r\n");
    fileSize -= (end - start); // minor the unnecessary part of the content

    readCnt -= (end - buf); // get the size of the read fileContent
    int writeCnt = 0;
    printf("fileSize: %d\n", fileSize);
    if (readCnt < fileSize)
    {
        writeCnt = fwrite(end, sizeof(unsigned char), readCnt, fp);
        printf("ReadCnt: %d WriteCnt: %d\n", readCnt, writeCnt);

        fileSize -= readCnt;
        while (fileSize)
        {
            if (fileSize > Max_Request)
                readCnt = Max_Request;
            else
                readCnt = fileSize;

            if ((readCnt = recv(connectfd, buf, readCnt, 0)) > 0)
            {
                writeCnt = fwrite(buf, sizeof(unsigned char), readCnt, fp);
                printf("ReadCnt: %d WriteCnt: %d\n", readCnt, writeCnt);
            }
            fileSize -= readCnt;
        }
    }
    else
    {
        writeCnt = fwrite(end, sizeof(unsigned char), fileSize, fp);
        printf("ReadCnt: %d WriteCnt: %d\n", fileSize, writeCnt);
    }
    fclose(fp);
    serve_resource(connectfd, "/");
}

void ReadPack(int connectfd)
{
    int fileSize = 0, readCnt = 0;
    unsigned char *start = NULL, *end = NULL;
    unsigned char buf[Max_Request], tmp[64], method[10], path[128], boundary_buf[128], filename[128], data_buf[4096], real_path[256];
    memset(buf, '\0', Max_Request);

    if ((readCnt = recv(connectfd, buf, Max_Request, 0)) > 0)
    {
        memset(method, '\0', sizeof(method));
        memset(path, '\0', sizeof(path));
        end = strstr(buf, " ");
        memcpy(method, buf, end - buf); // extract method
        start = end + 1;
        end = strstr(start, " ");
        memcpy(path, start, end - start); // extract request path

        if (strcmp(method, "GET") == 0)
            serve_resource(connectfd, path);
        else
            UploadFile(connectfd, buf, readCnt);
    }
}

int main()
{
    int listenfd, connectfd, status;
    pid_t pid;

    listenfd = create_socket(NULL, "8080");          // create a socket with host IP and 8080 port
    while (connectfd = accept(listenfd, NULL, NULL)) // ignore client address
    {
        if ((pid = fork()) == 0)
        {
            printf("Connect\n");
            close(listenfd);
            ReadPack(connectfd);
            close(connectfd);
            exit(EXIT_SUCCESS);
        }
        wait(&status);
        shutdown(connectfd, SHUT_WR);
        close(connectfd);
    }
    return 0;
}