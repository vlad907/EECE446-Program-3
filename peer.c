/*
 *  Vladimir Avdeev
 * EECE 446 - 01
 * Fall 2025
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <limits.h>

/*
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */


int sendall(int s, const char *buf, size_t len);
int recvall(int s, char *buf, size_t len);
int lookup_and_connect( const char *host, const char *service );

static int handle_join(int sock, uint32_t id);
static int handle_publish(int sock);
static int handle_search(int sock);
static int handle_fetch(int sock);

struct search_result {
        uint32_t peer_id;
        uint8_t ip[4];
        uint16_t port;
        int found;
};

static int perform_search(int sock, const char *filename, struct search_result *result);

static int read_command(char *buffer, size_t size);
static int read_line(char *buffer, size_t size);


int main(int argc, char *argv[]) {
        const char *host = ""; // Host address
        const char *port = ""; // Port
        uint32_t id; // Current id.
        char command[255]; // User input

        // Get arguments.
        if (argc == 4 ) {
                host = argv[1];
                port = argv[2];
                id = atoi(argv[3]);
        } else {
                printf("Usage: ./peer <server> <port> <id> \n");
                exit(0);
        }

        /* Lookup IP and connect to server */
                
        int sockfd = lookup_and_connect( host, port );
        if (sockfd < 0) {
                fprintf(stderr, "Failed to connect to %s:%s\n", host, port);
                return EXIT_FAILURE;
        }

        while(1) {
                printf("Enter a command: ");
                fflush(stdout);
                if (read_command(command, sizeof(command)) != 0) {
                        printf("Failed to read command. Exiting.\n");
                        break;
                }

                if(strcmp(command, "JOIN")==0) {
                        handle_join(sockfd, id);
                } else if(strcmp(command, "PUBLISH")==0) {
                        handle_publish(sockfd);
                } else if(strcmp(command, "FETCH")==0) {
                        handle_fetch(sockfd);
                } else if(strcmp(command, "SEARCH")==0) {
                        handle_search(sockfd);
                } else if(strcmp(command, "EXIT")==0) {
                        break;
                } else {
                        printf("Unrecognized command: %s\n", command);
                }
        }

        close(sockfd);
        return 0;
}

static int handle_join(int sock, uint32_t id) {
        char buf[1 + sizeof(uint32_t)];
        uint32_t net_id = htonl(id);

        buf[0] = 0;
        memcpy(buf + 1, &net_id, sizeof(net_id));

        if (sendall(sock, buf, sizeof(buf)) < 0) {
                perror("JOIN failed");
                return -1;
        }

        return 0;
}

static int handle_publish(int sock) {
        char buf[1205]; // 1 byte action, 4 bytes count, 1200 bytes payload
        size_t buf_idx = 5;
        uint32_t count = 0;
        DIR *dir;
        struct dirent *entry;

        buf[0] = 1;

        dir = opendir("SharedFiles");
        if (dir == NULL) {
                perror("Failed to open SharedFiles directory");
                return -1;
        }

        while ((entry = readdir(dir)) != NULL) {
                if (entry->d_name[0] == '.') {
                        continue;
                }

                int is_regular = 0;
                if (entry->d_type == DT_REG) {
                        is_regular = 1;
                } else if (entry->d_type == DT_UNKNOWN) {
                        char path[PATH_MAX];
                        struct stat st;
                        if (snprintf(path, sizeof(path), "SharedFiles/%s", entry->d_name) >= (int)sizeof(path)) {
                                fprintf(stderr, "Path too long for file: %s\n", entry->d_name);
                                continue;
                        }
                        if (stat(path, &st) == 0 && S_ISREG(st.st_mode)) {
                                is_regular = 1;
                        }
                }

                if (!is_regular) {
                        continue;
                }

                size_t name_len = strlen(entry->d_name);
                if (buf_idx + name_len + 1 > sizeof(buf)) {
                        fprintf(stderr, "Publish buffer too small for file: %s\n", entry->d_name);
                        closedir(dir);
                        return -1;
                }

                memcpy(buf + buf_idx, entry->d_name, name_len + 1);
                buf_idx += name_len + 1;
                count++;
        }

        closedir(dir);

        uint32_t net_count = htonl(count);
        memcpy(buf + 1, &net_count, sizeof(net_count));

        if (sendall(sock, buf, buf_idx) < 0) {
                perror("PUBLISH failed");
                return -1;
        }

        return 0;
}

static int handle_search(int sock) {
        char filename[255];
        struct search_result result;

        printf("Enter a file name: ");
        fflush(stdout);
        if (read_line(filename, sizeof(filename)) != 0 || filename[0] == '\0') {
                fprintf(stderr, "Failed to read filename\n");
                return -1;
        }

        if (perform_search(sock, filename, &result) < 0) {
                fprintf(stderr, "SEARCH operation failed\n");
                return -1;
        }

        if (!result.found) {
                printf("File not indexed by registry\n");
                return 0;
        }

        printf("File found at\nPeer %u\n%d.%d.%d.%d:%d\n",
               result.peer_id,
               result.ip[0], result.ip[1], result.ip[2], result.ip[3],
               result.port);

        return 0;
}

static int handle_fetch(int sock) {
        char filename[255];
        struct search_result result;
        struct in_addr peer_addr;
        char peer_ip_str[INET_ADDRSTRLEN];
        char port_str[6];
        int peer_fd = -1;
        FILE *file = NULL;
        unsigned char response_code;
        unsigned char request[1205];
        char file_buffer[4096];
        ssize_t bytes_received;

        printf("Filename: ");
        fflush(stdout);
        if (read_line(filename, sizeof(filename)) != 0 || filename[0] == '\0') {
                fprintf(stderr, "Failed to read filename\n");
                return -1;
        }

        if (perform_search(sock, filename, &result) < 0) {
                fprintf(stderr, "FETCH search failed\n");
                return -1;
        }

        if (!result.found) {
                printf("File not indexed by registry\n");
                return 0;
        }

        memcpy(&peer_addr, result.ip, sizeof(result.ip));
        if (inet_ntop(AF_INET, &peer_addr, peer_ip_str, sizeof(peer_ip_str)) == NULL) {
                perror("inet_ntop");
                return -1;
        }

        printf("Peer %u has %s at %s:%u\n",
               result.peer_id,
               filename,
               peer_ip_str,
               result.port);

        snprintf(port_str, sizeof(port_str), "%u", result.port);
        peer_fd = lookup_and_connect(peer_ip_str, port_str);
        if (peer_fd < 0) {
                perror("Failed to connect to peer");
                return -1;
        }

        size_t name_len = strlen(filename);
        if (name_len + 2 > sizeof(request)) {
                fprintf(stderr, "Filename too long\n");
                close(peer_fd);
                return -1;
        }

        request[0] = 3;
        memcpy(request + 1, filename, name_len + 1);

        if (sendall(peer_fd, (char *)request, name_len + 2) < 0) {
                perror("Error sending FETCH request");
                close(peer_fd);
                return -1;
        }

        if (recvall(peer_fd, (char *)&response_code, 1) <= 0) {
                perror("Failed to receive FETCH response code");
                close(peer_fd);
                return -1;
        }

        if (response_code != 0) {
                printf("Peer reported error fetching %s\n", filename);
                close(peer_fd);
                return 0;
        }

        file = fopen(filename, "wb");
        if (file == NULL) {
                perror("Failed to open output file");
                close(peer_fd);
                return -1;
        }

        while ((bytes_received = recv(peer_fd, file_buffer, sizeof(file_buffer), 0)) > 0) {
                if (fwrite(file_buffer, 1, (size_t)bytes_received, file) != (size_t)bytes_received) {
                        perror("Failed to write to file");
                        fclose(file);
                        close(peer_fd);
                        return -1;
                }
        }

        if (bytes_received < 0) {
                perror("Error receiving file data");
                fclose(file);
                close(peer_fd);
                return -1;
        }

        fclose(file);
        close(peer_fd);
        printf("File received and saved as %s\n", filename);

        return 0;
}

static int perform_search(int sock, const char *filename, struct search_result *result) {
        char buf[1205];
        size_t name_len = strlen(filename);
        int bytes;

        if (name_len + 2 > sizeof(buf)) {
                fprintf(stderr, "Filename too long\n");
                return -1;
        }

        memset(result, 0, sizeof(*result));

        buf[0] = 2;
        memcpy(buf + 1, filename, name_len + 1);

        if (sendall(sock, buf, name_len + 2) < 0) {
                perror("SEARCH send failed");
                return -1;
        }

        bytes = recvall(sock, buf, 10);
        if (bytes < 0) {
                perror("SEARCH receive failed");
                return -1;
        }

        if (bytes < 10) {
                fprintf(stderr, "SEARCH response truncated\n");
                return -1;
        }

        memcpy(&result->peer_id, buf, sizeof(result->peer_id));
        memcpy(result->ip, buf + 4, sizeof(result->ip));
        memcpy(&result->port, buf + 8, sizeof(result->port));

        result->peer_id = ntohl(result->peer_id);
        result->port = ntohs(result->port);
        result->found = result->peer_id != 0;

        return 0;
}

static int read_command(char *buffer, size_t size) {
        if (read_line(buffer, size) != 0) {
                return -1;
        }
        return 0;
}

static int read_line(char *buffer, size_t size) {
        if (size == 0 || buffer == NULL) {
                return -1;
        }

        if (fgets(buffer, (int)size, stdin) == NULL) {
                return -1;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        return 0;
}


int sendall(int s, const char *buf, size_t len)
{
        size_t total = 0;        // how many bytes we've sent
        ssize_t n;

        while (total < len) {
                n = send(s, buf + total, len - total, 0);
                if (n <= 0) {
                        return -1; // error
                }
                total += (size_t)n;
        }

        return (int)total; // total bytes sent
}

int recvall(int s, char *buf, size_t len)
{
        size_t total = 0;
        while (total < len) {
                ssize_t n = recv(s, buf + total, len - total, 0);
                if (n < 0) {
                        return -1; // error
                }
                if (n == 0) {
                        break; // connection closed
                }
                total += (size_t)n;
        }
        return (int)total;
} 


int lookup_and_connect( const char *host, const char *service ) {
        struct addrinfo hints;
        struct addrinfo *rp, *result;
        int s;

        /* Translate host name into peer's IP address */
        memset( &hints, 0, sizeof( hints ) );
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = 0;
        hints.ai_protocol = 0;

        if ( ( s = getaddrinfo( host, service, &hints, &result ) ) != 0 ) {
                fprintf( stderr, "stream-talk-client: getaddrinfo: %s\n", gai_strerror( s ) );
                return -1;
        }

        /* Iterate through the address list and try to connect */
        for ( rp = result; rp != NULL; rp = rp->ai_next ) {
                if ( ( s = socket( rp->ai_family, rp->ai_socktype, rp->ai_protocol ) ) == -1 ) {
                        continue;
                }

                if ( connect( s, rp->ai_addr, rp->ai_addrlen ) != -1 ) {
                        break;
                }

                close( s );
        }
        if ( rp == NULL ) {
                perror( "stream-talk-client: connect" );
                return -1;
        }
        freeaddrinfo( result );

        return s;
}
