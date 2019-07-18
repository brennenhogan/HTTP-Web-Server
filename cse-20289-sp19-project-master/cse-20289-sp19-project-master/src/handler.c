/* handler.c: HTTP Request Handlers */

#include "spidey.h"

#include <errno.h>
#include <limits.h>
#include <string.h>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

/* Internal Declarations */
Status handle_browse_request(Request *request);
Status handle_file_request(Request *request);
Status handle_cgi_request(Request *request);
Status handle_error(Request *request, Status status);

/**
 * Handle HTTP Request.
 *
 * @param   r           HTTP Request structure
 * @return  Status of the HTTP request.
 *
 * This parses a request, determines the request path, determines the request
 * type, and then dispatches to the appropriate handler type.
 *
 * On error, handle_error should be used with an appropriate HTTP status code.
 **/
Status  handle_request(Request *r) {
    Status result;

    /* Parse request */
    int parseStat = parse_request(r);
    if(parseStat < 0){  // This means there was a bad request or header
        fprintf(stderr, "parse_request failed\n");
        result = HTTP_STATUS_BAD_REQUEST;
        handle_error(r, result);
        r->path = strdup(" ");
        return result;
    }

    /* Determine request path */
    r->path = determine_request_path(r->uri);
    if(r->path == NULL){
        fprintf(stderr, "determine_request_path failed\n");
        result = HTTP_STATUS_NOT_FOUND;
        handle_error(r, result);
        return result;
    }

    debug("HTTP REQUEST PATH: %s", r->path);
    /* Dispatch to appropriate request handler type based on file type */
    
    /* Stat items */
    struct stat statusTemp;
    if(stat(r->path, &statusTemp) < 0 ) {                       //stat the file to get information regarding the type
        fprintf(stderr, "stat failure %s\n", strerror(errno));
        result = HTTP_STATUS_NOT_FOUND;
        handle_error(r, result);
        return result;
    }
    int RWFlag = access(r->path, R_OK);                         //access also tells more information regarding the type, read and execute
    int XFlag = access(r->path, X_OK);
    
    if((statusTemp.st_mode & S_IFMT) == S_IFDIR){
        log("HTTP REQUEST TYPE: BROWSE");
        result = handle_browse_request(r);                      //If a directory, browse
    } else if(XFlag == 0){
        log("HTTP REQUEST TYPE: CGI");
        result = handle_cgi_request(r);                         //If a CGI script, handle accordingly
    } else if (RWFlag == 0){
        log("HTTP REQUEST TYPE: FILE");
        result = handle_file_request(r);                        //If a file, output its contents
    } else{
        log("HTTP REQUEST TYPE: ERROR");
        result = HTTP_STATUS_NOT_FOUND;                         //If none, error
        result = handle_error(r, result);
    }
    log("HTTP REQUEST STATUS: %s", http_status_string(result));

    return result;
}
 
/**
 * Handle browse request.
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP browse request.
 *
 * This lists the contents of a directory in HTML.
 *
 * If the path cannot be opened or scanned as a directory, then handle error
 * with HTTP_STATUS_NOT_FOUND.
 **/
Status  handle_browse_request(Request *r) {
    struct dirent **entries;
    int n;

    /* Open a directory for reading or scanning */
    n = scandir(r->path, &entries, NULL, alphasort);
    if(n == -1){
        fprintf(stderr, "scandir failure \n");
        handle_error(r, HTTP_STATUS_NOT_FOUND);
        return HTTP_STATUS_NOT_FOUND; 
    }

    /* Write HTTP Header with OK Status and text/html Content-Type */
    fprintf(r->file, "HTTP/1.0 200 OK\r\n");
    fprintf(r->file, "Content-Type: text/html\r\n");
    fprintf(r->file, "\r\n");

    /* For each entry in directory, emit HTML list item */
    fprintf(r->file, "<ul>\n");

    int i = 0;
    while(n > i){
        if(!streq(".", entries[i]->d_name)){ //Prints all direcories outside of the current
             fprintf(r->file, "<li><a href=\"%s/%s\">%s</li>\n",streq(r->uri,"/") ? "": r->uri, entries[i]->d_name ,entries[i]->d_name);
        }
        free(entries[i]);
        i++;
    }

    fprintf(r->file, "</ul>\n");
    /* Flush socket, return OK */
    free(entries);
    fflush(r->file);

    return HTTP_STATUS_OK;
}

/**
 * Handle file request.
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP file request.
 *
 * This opens and streams the contents of the specified file to the socket.
 *
 * If the path cannot be opened for reading, then handle error with
 * HTTP_STATUS_NOT_FOUND.
 **/
Status  handle_file_request(Request *r) {
    debug("WE ARE IN HANDLE_FILE_REQUEST RIGHT NOW!");
    FILE *fs;
    char buffer[BUFSIZ];
    char *mimetype = NULL;
    size_t nread;

    /* Open file for reading */
    fs = fopen(r->path, "r"); 

    if(fs == NULL){
        fprintf(stderr, "fopen failed: %s", strerror(errno));
        goto fail;
    }

    /* Determine mimetype */
    mimetype = determine_mimetype(r->path);                             //Determines the content type of the file

    /* Write HTTP Headers with OK status and determined Content-Type */
    fprintf(r->file, "HTTP/1.0 200 OK\r\n");
    fprintf(r->file, "Content-Type: %s\r\n", mimetype);
    fprintf(r->file, "\r\n");

    /* Read from file and write to socket in chunks */
    while((nread = fread(buffer,sizeof(char), BUFSIZ, fs)) > 0){
       if( fwrite(buffer, sizeof(char), nread, r->file) != nread){
            goto fail;
        }
    }


    /* Close file, flush socket, deallocate mimetype, return OK */
    fclose(fs);
    fflush(r->file);
    free(mimetype);

    return HTTP_STATUS_OK;

fail:
    /* Close file, free mimetype, return INTERNAL_SERVER_ERROR */
    fclose(fs);
    free(mimetype);

    return HTTP_STATUS_INTERNAL_SERVER_ERROR;
}

/**
 * Handle CGI request
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP file request.
 *
 * This popens and streams the results of the specified executables to the
 * socket.
 *
 * If the path cannot be popened, then handle error with
 * HTTP_STATUS_INTERNAL_SERVER_ERROR.
 **/
Status  handle_cgi_request(Request *r) {
    FILE *pfs;
    char buffer[BUFSIZ];

    /* Export CGI environment variables from request:
     * http://en.wikipedia.org/wiki/Common_Gateway_Interface */
    setenv("DOCUMENT_ROOT", RootPath, 1);
    setenv("QUERY_STRING", r->query, 1);
    setenv("REMOTE_ADDR", r->host, 1);
    setenv("REMOTE_PORT", r->port, 1);
    setenv("REQUEST_METHOD", r->method, 1);
    setenv("REQUEST_URI", r->uri, 1);
    setenv("SCRIPT_FILENAME", r->path, 1);
    setenv("SERVER_PORT", Port, 1);

    /* Export CGI environment variables from request headers */
    Header * curr = r->headers;

    while(curr){
        if(streq(curr->name,"Host"))
            setenv("HTTP_HOST", curr->value, 1);
        if(streq(curr->name,"User-Agent"))
            setenv("HTTP_USER_AGENT", curr->value ,1);
        if(streq(curr->name,"Accept"))
            setenv("HTTP_ACCEPT", curr->value, 1);
        if(streq(curr->name,"Accept-Language"))
            setenv("HTTP_ACCEPT_LANGUAGE", curr->value ,1);
        if(streq(curr->name,"Accept-Encoding"))
            setenv("HTTP_ACCEPT_ENCODING", curr->value, 1);
        if(streq(curr->name,"Connection"))
            setenv("HTTP_CONNECTION", curr->value, 1);

        curr = curr->next;
        
    }
    /* POpen CGI Script */
    pfs = popen(r->path, "r");              //
    if(pfs == NULL){
        fprintf(stderr, "popen failure");
        return HTTP_STATUS_INTERNAL_SERVER_ERROR;
    }

    /* Copy data from popen to socket */
    while((fgets(buffer, BUFSIZ, pfs))){
        fputs(buffer, r->file);
    }

    /* Close popen, flush socket, return OK */
    pclose(pfs);
    fflush(r->file);
    return HTTP_STATUS_OK;
}

/**
 * Handle displaying error page
 *
 * @param   r           HTTP Request structure.
 * @return  Status of the HTTP error request.
 *
 * This writes an HTTP status error code and then generates an HTML message to
 * notify the user of the error.
 **/
Status  handle_error(Request *r, Status status) {
    const char *status_string = http_status_string(status);

    /* Write HTTP Header */
    fprintf(r->file, "HTTP/1.0 %s\r\n", status_string);
    fprintf(r->file, "Content-Type: text/html\r\n");
    fprintf(r->file, "\r\n");

    /* Write HTML Description of Error*/
    
    fprintf(r->file, "<h1>%s</h1>\r\n", status_string);
    fprintf(r->file, "<h1>Stuff's all borked. I blame nargels</h1>\r\n");

    /* Return specified status */
    fflush(r->file);
    return status;
}

/* vim: set expandtab sts=4 sw=4 ts=8 ft=c: */
