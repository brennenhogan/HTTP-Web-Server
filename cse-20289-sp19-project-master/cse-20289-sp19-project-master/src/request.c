/* request.c: HTTP Request Functions */

#include "spidey.h"

#include <errno.h>
#include <string.h>

#include <unistd.h>

int parse_request_method(Request *r);
int parse_request_headers(Request *r);

/**
 * Accept request from server socket.
 *
 * @param   sfd         Server socket file descriptor.
 * @return  Newly allocated Request structure.
 *
 * This function does the following:
 *
 *  1. Allocates a request struct initialized to 0.
 *  2. Initializes the headers list in the request struct.
 *  3. Accepts a client connection from the server socket.
 *  4. Looks up the client information and stores it in the request struct.
 *  5. Opens the client socket stream for the request struct.
 *  6. Returns the request struct.
 *
 * The returned request struct must be deallocated using free_request.
 **/
Request * accept_request(int sfd) {
    Request *r;
    struct sockaddr raddr;
    socklen_t rlen = sizeof(struct sockaddr);

    /* Allocate request struct (zeroed) */

    r = calloc(1, sizeof(Request));

    /* Accept a client */

    int client_fd = accept(sfd, &raddr, &rlen);
    if(client_fd < 0){
        fprintf(stderr, "accept failed: %s\n", strerror(errno));
        goto fail;
    }

    r->fd = client_fd;

    /* Lookup client information */

    if(getnameinfo(&raddr, rlen, r->host, NI_MAXHOST, r->port, NI_MAXSERV, 0) != 0){
        fprintf(stderr, "getnameinfo failed: %s\n", strerror(errno));
        goto fail;
    }

    /* Open socket stream */

    FILE *client_file = fdopen(client_fd, "w+");
    if (!client_file){
        fprintf(stderr, "Unable to fdopen: %s\n", strerror(errno));
        close(client_fd);
        goto fail;
    }
    
    r->file = client_file;

    /* Initialize headers to null */
    r->headers = NULL;

    log("Accepted request from %s:%s", r->host, r->port);
    return r;

fail:
    /* Deallocate request struct */
    free(r);
    return NULL;
}

/**
 * Deallocate request struct.
 *
 * @param   r           Request structure.
 *
 * This function does the following:
 *
 *  1. Closes the request socket stream or file descriptor.
 *  2. Frees all allocated strings in request struct.
 *  3. Frees all of the headers (including any allocated fields).
 *  4. Frees request struct.
 **/
void free_request(Request *r) {
    if (!r) {
    	return;
    }

    /* Close socket or fd */
    close(r->fd);

    /* Free allocated strings */
    free(r->uri);
    free(r->method);
    free(r->query);
    free(r->path);

    /* Free headers */
    Header *current = r->headers;
    Header *next;

    while(current) {            //Goes through the headers LL and frees each of them
        free(current->name);
        free(current->value);
        next = current->next;
        free(current);
        current = next;
    }

    /* Free request */
    free(r);
}

/**
 * Parse HTTP Request.
 *
 * @param   r           Request structure.
 * @return  -1 on error and 0 on success.
 *
 * This function first parses the request method, any query, and then the
 * headers, returning 0 on success, and -1 on error.
 **/
int parse_request(Request *r) {
    /* Parse HTTP Request Method */
    int status = parse_request_method(r);

    /* Parse HTTP Requet Headers*/
    if(status != -1){
        status = parse_request_headers(r); 
    }

    return status;
}

/**
 * Parse HTTP Request Method and URI.
 *
 * @param   r           Request structure.
 * @return  -1 on error and 0 on success.
 *
 * HTTP Requests come in the form
 *
 *  <METHOD> <URI>[QUERY] HTTP/<VERSION>
 *
 * Examples:
 *
 *  GET / HTTP/1.1
 *  GET /cgi.script?q=foo HTTP/1.0
 *
 * This function extracts the method, uri, and query (if it exists).
 **/
int parse_request_method(Request *r) {
    char buffer[BUFSIZ];
    char *method;
    char *uri;
    char *query;
    /* Read line from socket */
    if(fgets(buffer, BUFSIZ, r->file) == NULL){
        debug("fgets failed");
        goto fail;
    }
    chomp(buffer);

    /* Parse method and uri */
    method  = strtok(buffer, WHITESPACE);
    uri     = strtok(NULL,   WHITESPACE);

    if(uri == NULL){
        r->method   = strdup(" "); 
        r->uri      = strdup(" ");
        r->query    = strdup(" ");
        goto fail;
    }

    /* Parse query from uri */
    if(strchr(uri,  '?')){
        uri     = strtok(uri,   "?");
        query   = strtok(NULL,  WHITESPACE);
    } else
        query   = ""; //Query does not exist

    r->method   = strdup(method); 
    r->uri      = strdup(uri);
    r->query    = strdup(query);
    
    /* Record method, uri, and query in request struct */
    debug("HTTP METHOD: %s", r->method);
    debug("HTTP URI:    %s", r->uri);
    debug("HTTP QUERY:  %s", r->query);

    return 0;

fail:
    return -1;
}

/**
 * Parse HTTP Request Headers.
 *
 * @param   r           Request structure.
 * @return  -1 on error and 0 on success.
 *
 * HTTP Headers come in the form:
 *
 *  <NAME>: <VALUE>
 *
 * Example:
 *
 *  Host: localhost:8888
 *  User-Agent: Mozilla/5.0 (X11; Linux x86_64; rv:29.0) Gecko/20100101 Firefox/29.0
 *  Accept: text/html,application/xhtml+xml
 *  Accept-Language: en-US,en;q=0.5
 *  Accept-Encoding: gzip, deflate
 *  Connection: keep-alive
 *
 * This function parses the stream from the request socket using the following
 * pseudo-code:
 *
 *  while (buffer = read_from_socket() and buffer is not empty):
 *      name, value = buffer.split(':')
 *      header      = new Header(name, value)
 *      headers.append(header)
 **/
int parse_request_headers(Request *r) {
    char buffer[BUFSIZ];
    char *name;
    char *value;

    /* Parse headers from socket */
    while(fgets(buffer, BUFSIZ, r->file) && strlen(buffer) > 2){
        chomp(buffer);
        name    = strtok(buffer, ":");
        value   = strtok(NULL, WHITESPACE);

        if(name == NULL || value == NULL){
            goto fail;
        }

        struct header *new  = calloc(1, sizeof(struct header));
        new->value   = strdup(value);
        new->name    = strdup(name);
        new->next    = r->headers;
        r->headers   = new;
    }

#ifndef NDEBUG
    for (struct header *header = r->headers; header; header = header->next) {
    	debug("HTTP HEADER %s = %s", header->name, header->value);
    }
#endif
    return 0;

fail:
    return -1;
}

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
