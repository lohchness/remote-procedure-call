#include "rpc.h"
#include <stdlib.h>

// #define _POSIX_C_SOURCE 200112L 
// #define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include<assert.h>

#include<netdb.h>
#include<unistd.h>
#include<sys/types.h>
#include<math.h>
#include<string.h>
#include<stdint.h>

#define MAX_BUFFER 1024
#define MAX_CLIENTS 10

#define NOT_FOUND -1
#define FINDING 0
#define CALLING 1

#define SERVER_FUNCTION_FAILURE 0
#define SERVER_FUNCTION_SUCCESS 1

#define INT_32 32

#define NONBLOCKING

uint64_t htonll(uint64_t value);
uint64_t ntohll(uint64_t value);

struct rpc_server {
    char **function_names;
    rpc_handler *handlers; // function to call when request is received
    int size;
    int capacity;

    int listenfd, connfd; // sockfd, newsockfd
    struct addrinfo hints; // desired socket properties
    struct addrinfo *res; // pointer to first element of addrinfo structs. set by getaddrinfo()
    
    struct sockaddr_in client_addr;
	socklen_t client_addr_size;

    char *buffer;
};

// Convert 64 bit int to network byte order
uint64_t htonll(uint64_t value) {
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) {
        value = ((uint64_t)htonl((uint32_t)(value & 0xFFFFFFFFU))) << INT_32 | htonl((uint32_t)(value >> INT_32));
    }
    return value;
}

// Convert 64 bit int to host byte order
uint64_t ntohll(uint64_t value) {
    if (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__) {
        value = ((uint64_t)ntohl((uint32_t)(value & 0xFFFFFFFFU))) << INT_32 | ntohl((uint32_t)(value >> INT_32));
    }
    return value;
}

rpc_server *rpc_init_server(int port) {
    if (port < 0) return NULL;

    rpc_server *server = malloc(sizeof(rpc_server));

    if (server==NULL) {
        return NULL;
    }

    server->function_names = malloc(sizeof(char*));
    server->handlers = malloc(sizeof(rpc_handler));
    server->buffer = malloc(sizeof(char) * MAX_BUFFER); 
    server->size = 0;
    server->capacity = 1;

    server->listenfd = -1;
    server->connfd = -1;

    // converts port to string for getaddrinfo(). Taken from Quora
    int x = port;
    int numdigits = log10(x) + 1;
    char *service = calloc(numdigits + 1, sizeof(char));
    assert(service != NULL);
    sprintf(service, "%d", x);

    // create listening socket
    memset(&server->hints, 0, sizeof(server->hints));
    server->hints.ai_family = AF_INET6;
    server->hints.ai_socktype = SOCK_STREAM;
    server->hints.ai_flags = AI_PASSIVE;
    int s = getaddrinfo(NULL, service, &server->hints, &server->res);
    if (s!=0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return NULL;
    }

    // create socket
    server->listenfd = socket(server->res->ai_family, server->res->ai_socktype, server->res->ai_protocol);
    if (server->listenfd < 0) {
        perror("socket");
        return NULL;
    }

    // reuse port if possible
    int enable = 1;
    if (setsockopt(server->listenfd, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(int)) < 0) {
        perror("setsockopt");
        return NULL;
    }

    // bind address to socket
    if (bind(server->listenfd, server->res->ai_addr, server->res->ai_addrlen) < 0) {
        perror("bind");
        return NULL;
    }
    freeaddrinfo(server->res);
    // end create listening socket

    // Listen on socket - ready to accept connections
    // incoming requests queued
    // Puts TCP machine in LISTEN state
    if (listen(server->listenfd, MAX_CLIENTS) < 0) {
        perror("listen");
        return NULL;
    }
    free(service);

    return server;
}

// add a function to the server
int rpc_register(rpc_server *srv, char *name, rpc_handler handler) {
    // return -1 on any null arguments
    if (srv==NULL || name==NULL || handler==NULL) return -1;

    if (srv->size == srv->capacity) {
        srv->capacity *= 2;
        srv->function_names = realloc(srv->function_names, srv->capacity * sizeof(*srv->function_names));
        srv->handlers = realloc(srv->handlers, srv->capacity * sizeof(rpc_handler));
        if (srv->function_names == NULL || srv->handlers == NULL) {
            return -1;
        }
    }

    // check if name exists in array. If so, override function pointer
    for (int i=0; i < srv->size; i++) {
        if (strcmp(srv->function_names[i], name) == 0) {
            srv->handlers[i] = handler;
            return 1;
        }
    }

    // Function doesn't exist, add a new function
    srv->function_names[srv->size] = strdup(name);
    srv->handlers[srv->size] = handler;    srv->size++;

    return 1;
}

void rpc_serve_all(rpc_server *srv) {
    while(1) {
        // Accept new connection if not serving a client
        if (srv->connfd == -1) {
            srv->client_addr_size = sizeof(srv->client_addr);
            srv->connfd = accept(srv->listenfd, (struct sockaddr*)&srv->client_addr, &srv->client_addr_size);
            if (srv->connfd < 0) {
                perror("accept");
                continue;
            }
        }

        // determine whether client is calling or finding
        int n;
        uint64_t value;
        n = read(srv->connfd, &value, sizeof(value));
        value = ntohll(value);

        if (n == 0) {
            // Client closed connection
            close(srv->connfd);
            srv->connfd = -1;
            continue;
        }

        // process client request
        if (value == FINDING) {
            // get name of function from client
            n = read(srv->connfd, srv->buffer, MAX_BUFFER-1);
            srv->buffer[n] = '\0';

            // search server for matching function
            uint64_t isfound = htonll(NOT_FOUND);
            for (int i=0; i<srv->size; i++) {
                if (strcmp(srv->function_names[i], srv->buffer) == 0) {
                    isfound = htonll(i);
                    break;
                }
            }

            // Send CONTENT of rpc_handle over, rpc_handle malloc'd on client side
            write(srv->connfd, &isfound, sizeof(isfound));

        } else {
            assert(value == CALLING);

            // receive index of function from client
            uint64_t function_index, val;
            read(srv->connfd, &function_index, sizeof(function_index));
            function_index = ntohll(function_index);
            assert(function_index < srv->size);

            // make new payload
            rpc_data *payload = malloc(sizeof(rpc_data));
            payload->data2 = NULL;

            // READ data1
            read(srv->connfd, &val, sizeof(val));
            payload->data1 = ntohll(val);

            // READ data2_len
            read(srv->connfd, &val, sizeof(val));
            payload->data2_len = ntohll(val);
            
            // READ data2 if data2_len > 0
            if (payload->data2_len > 0) {
                payload->data2 = malloc(sizeof(payload->data2) * payload->data2_len);
                read(srv->connfd, payload->data2, payload->data2_len);
            }

            // search for the function and pass the payload
            rpc_data *out = srv->handlers[function_index](payload);

            // Check *out for bad data
            if (
            out == NULL || 
            (out->data2_len > 0 && out->data2==NULL) ||
            (out->data2_len == 0 && out->data2!=NULL) ||
            out->data2_len < 0) 
            {
                val = htonll(SERVER_FUNCTION_FAILURE);
                write(srv->connfd, &val, sizeof(val));
                rpc_data_free(out);
                rpc_data_free(payload);
                continue;
            }
            val = htonll(SERVER_FUNCTION_SUCCESS);
            write(srv->connfd, &val, sizeof(val));
            
            // send client back the contents of the payload
            // WRITE data1
            val = htonll(out->data1);
            write(srv->connfd, &val, sizeof(val));

            // WRITE data2_len
            val = htonll(out->data2_len);
            write(srv->connfd, &val, sizeof(val));

            // WRITE data2 if data2_len > 0
            if (out->data2_len > 0) {
                write(srv->connfd, out->data2, out->data2_len);                
            }

            // free rpc_datas
            rpc_data_free(out);
            rpc_data_free(payload);
        }
    }

}


/* ------------------------------------------------------------------ */


struct rpc_client {
    /* Add variable(s) for client state */
    int connfd;
    struct addrinfo hints;
    struct addrinfo *servinfo;
    struct addrinfo *rp;

    rpc_handle *handle;
};

struct rpc_handle {
    /* Add variable(s) for handle */
    int32_t index;

};

rpc_client *rpc_init_client(char *addr, int port) {
    if (port < 0) return NULL;

    rpc_client *client = malloc(sizeof(rpc_client));
    if (client==NULL) {
        return NULL;
    }

    memset(&client->hints, 0, sizeof(client->hints));
    client->hints.ai_family = AF_INET6;
    client->hints.ai_socktype = SOCK_STREAM;

    client->handle = malloc(sizeof(rpc_handle));

    // converts port to string for getaddrinfo(). Taken from Quora
    int x = port;
    int numdigits = log10(x) + 1;
    char *service = calloc(numdigits + 1, sizeof(char));
    sprintf(service, "%d", x);

    // get addrinfo of server
    int s = getaddrinfo(addr, service, &client->hints, &client->servinfo);
    if (s != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(s));
		return NULL;
    }

    // Connect to first valid result
    for (client->rp = client->servinfo; client->rp!= NULL; client->rp = client->rp->ai_next) {
        client->connfd = socket(client->rp->ai_family, client->rp->ai_socktype, client->rp->ai_protocol);
        if (client->connfd == -1) {
            continue;
        }
        if (connect(client->connfd, client->rp->ai_addr, client->rp->ai_addrlen) != -1) {
            break; // success
        }

        close(client->connfd);
    }
    // Use sock here if it is >= 0.
    if (client->rp==NULL) {
        fprintf(stderr, "client: failed to connect\n");
        return NULL;
    }
    freeaddrinfo(client->servinfo);
    free(service);
    
    return client;
}

rpc_handle *rpc_find(rpc_client *cl, char *name) {
    if (cl==NULL || name==NULL) {
        return NULL;
    }

    // indicate that it is finding
    uint64_t val = htonll(FINDING);
    write(cl->connfd, &val, sizeof(val));

    // send function name add2 to server to find
    write(cl->connfd, name, strlen(name));
    
    // read output from server if it found a match or not
    read(cl->connfd, &val, sizeof(val));
    val = ntohll(val);
    if (val == NOT_FOUND) {
        return NULL;
    }

    // add inputs of rpc_handle into handle
    rpc_handle *handle = malloc(sizeof(rpc_handle));
    handle->index = val;

    return handle;
}

rpc_data *rpc_call(rpc_client *cl, rpc_handle *h, rpc_data *payload) {
    
    // Safety: check for bad data
    if (h == NULL || cl == NULL || payload == NULL) return NULL;
    if (payload->data2_len > 0 && payload->data2==NULL) return NULL;
    if (payload->data2_len == 0 && payload->data2!=NULL) return NULL;
    if (payload->data2_len < 0) return NULL;

    // indicate that it is calling
    uint64_t val = htonll(CALLING);
    write(cl->connfd, &val, sizeof(val));

    // send index of function stored in handle h over to server
    val = htonll(h->index);
    write(cl->connfd, &val, sizeof(val));

    // send over contents of payload
    // WRITE data1
    val = htonll(payload->data1);
    write(cl->connfd, &val, sizeof(val));

    // WRITE data2_len
    val = htonll(payload->data2_len);
    write(cl->connfd, &val, sizeof(val));

    // WRITE data2 if data2_len > 0
    if (payload->data2_len > 0) {
        assert(payload->data2 != NULL);
        write(cl->connfd, payload->data2, payload->data2_len);
    }

    // READ RESPONSE FROM SERVER
    
    // Check if server function success or not
    read(cl->connfd, &val, sizeof(val));
    val = ntohll(val);
    if (val == SERVER_FUNCTION_FAILURE) {
        return NULL;
    }

    rpc_data *out = malloc(sizeof(rpc_data));
    out->data2_len = 0;
    out->data2 = NULL;

    // READ data1
    read(cl->connfd, &val, sizeof(val));
    out->data1 = ntohll(val);

    // READ data2_len
    read(cl->connfd, &val, sizeof(val));
    out->data2_len = ntohll(val);

    // READ data2 if data2len > 0
    if (out->data2_len > 0) {
        out->data2 = malloc(sizeof(out->data2) * out->data2_len);
        read(cl->connfd, out->data2, out->data2_len);
    }
    
    return out;
}

void rpc_close_client(rpc_client *cl) {
    close(cl->connfd);
    free(cl->handle);
    free(cl);
}

void rpc_data_free(rpc_data *data) {
    if (data == NULL) {
        return;
    }
    if (data->data2 != NULL) {
        free(data->data2);
    }
    free(data);
}