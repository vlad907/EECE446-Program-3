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
#include <string.h>
#include <dirent.h>

/*
 * Lookup a host IP address and connect to it using service. Arguments match the first two
 * arguments to getaddrinfo(3).
 *
 * Returns a connected socket descriptor or -1 on error. Caller is responsible for closing
 * the returned socket.
 */

/*
 *   Resources:
 * https://beej.us/guide/bgnet/html/split/slightly-advanced-techniques.html#sendall
 * https://www.ibm.com/docs/en/i/7.4.0?topic=functions-strstr-locate-substring
 * https://stackoverflow.com/questions/35747707/null-terminate-string-use-0-or-just-0
 * https://www.ibm.com/docs/en/cobol-zos/6.3.0?topic=options-buf
 * 
 */
int sendall(int s, const char *buf, size_t len);
int recvall(int s, char *buf, size_t len);
int lookup_and_connect( const char *host, const char *service );


int main(int argc, char *argv[]) {
        int s;                                                          
        char *host = ""; // Host address
        char *port = ""; // Port
        uint32_t id; // Current id.
        char usrin[255]; // User input

        DIR *pubdir = opendir("SharedFiles");
        uint32_t pubidx = 5; // The current index in the buffer that we iterate over when adding a new filename.
        struct dirent *pubfile; // Current shared file.

        uint32_t pid, pubcnt; // Peer id and publish count. 

        uint16_t pp; // Peer port (search)
        uint8_t pip1, pip2, pip3, pip4; // Peer ip.

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
        if ( ( s = lookup_and_connect( host, port ) ) < 0 ) {
                exit( 1 );
        }

        char buf[1205]; // Buf used to send requests. I insisted on making it 1205 because our longest possible buffer is for the PUBLISH command, with 1 byte action id, 4 byte count, and the 1200 limit on PUBLISH.

        while(1) {
                printf("Enter a command: ");
                scanf("%s", usrin);
                if(strcmp(usrin, "JOIN")==0) {
                        // Create message with byte 0 as the action id, and the next 4 bytes as id.
                        buf[0]=0;
                        id = htonl(id);
                        /* Alternative method of copying into buf.
                           buf[1] = (id) & 0xFF;
                           buf[2] = (id >> 8) & 0xFF;
                           buf[3] = (id >> 16) & 0xFF;
                           buf[4] = (id >> 24) & 0xFF;
                           */
                        memcpy(buf+1, &id, 4);
                        sendall(s, buf, sizeof(int)+1); // And then send.
                } else if(strcmp(usrin, "PUBLISH")==0) {
                        buf[0]=1;
                        while((pubfile = readdir(pubdir))!=NULL) {
                                // Do not iterate over files starting with . 
                                if(pubfile->d_name[0] != '.') {
                                        pubcnt++;
                                        strcpy(buf+pubidx, pubfile->d_name);
                                        pubidx += strlen(pubfile->d_name)+1;
                                }
                        }
                        pubcnt = htonl(pubcnt);
                        memcpy(buf+1, &pubcnt, 4);
                        sendall(s, buf, pubidx);
                } else if(strcmp(usrin, "SEARCH")==0) {
                        buf[0]=2; // Action id.
                        printf("Enter a file name: ");
                        scanf("%s", usrin);
                        strcpy(buf + 1, usrin);
                        sendall(s, buf, 2+strlen(usrin)); // And then send. String + action id + null terminator.
                        recvall(s, buf, 10); // and recieve returned data.

                        memcpy(&pid, buf, 4); // Peer id.
                        memcpy(&pip1, buf+4, 4); // Peer ip 1.
                        memcpy(&pip2, buf+5, 4); // Peer ip 2.
                        memcpy(&pip3, buf+6, 4); // Peer ip 3.
                        memcpy(&pip4, buf+7, 4); // Peer ip 4.
                        memcpy(&pp, buf+8, 2); // Peer port.

                        pid = ntohl(pid);
                        pp = ntohs(pp);

                        if(buf[0]==0) {
                                printf("File not indexed by registry\n");
                        } else {
                                printf("File found at\nPeer %u\n%d.%d.%d.%d:%d\n", pid, pip1, pip2, pip3, pip4, pp);
                        }
                } else if(strcmp(usrin, "EXIT")==0) {
                        return 0;
                }
        }
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
