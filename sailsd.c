#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <inttypes.h>
#include <resolv.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <getopt.h>

#include <jansson.h>

#define SAILSD_PORT 3333
#define SAILSD_MAX_MESSAGE_LENGTH 2048

#define COLOR_RED     "\x1b[31m"
#define COLOR_GREEN   "\x1b[32m"
#define COLOR_YELLOW  "\x1b[33m"
#define COLOR_BLUE    "\x1b[34m"
#define COLOR_MAGENTA "\x1b[35m"
#define COLOR_CYAN    "\x1b[36m"
#define COLOR_RESET   "\x1b[0m"

enum log_level { ERROR, WARNING, INFO, DEBUG };

/* print a giant boat to the screen */
void put_boat(void) {
    puts("                            \n"
         "                ,\x1b[31m~\x1b[0m          \n"
         "                |\\         \n"
         "               /| \\        \n"
         "         \x1b[34m~ ^~ \x1b[0m/_|__\\\x1b[34m~^~    \x1b[0m\n"
         "      \x1b[34m~^~^ ~ \x1b[0m'======' \x1b[34m~^ ~^~  \x1b[0m\n"
         "    \x1b[34m~^  ^~ ~^ ~\x1b[0m_\x1b[34m~\x1b[0m_\x1b[34m~^~ ~^~\x1b[0m_\x1b[34m ~^~  \x1b[0m\n"
         "   \x1b[34m~^~ \x1b[0m___\x1b[34m^\x1b[0m___|_| |___\x1b[34m~\x1b[0m_| | \x1b[34m ^~  \x1b[0m\n"
         "    \x1b[34m~ \x1b[0m|_ -| .'| | |_ -| . |\x1b[34m ~ \x1b[0m\n"
         "      |___|__,|_|_|___|___| \n");
}

/* logging function in the vein of vprintf, taking a va_list of arguments */
static void vlog_msg(const enum log_level level,
                     const char *format,
                     va_list argp) {
    char *level_str = "";
    switch(level) {
        case ERROR:
            level_str = COLOR_RED "error" COLOR_RESET;
            break;
        case WARNING:
            level_str = COLOR_YELLOW "warning" COLOR_RESET;
            break;
        case INFO:
            level_str = COLOR_BLUE "info" COLOR_RESET;
            break;
        case DEBUG:
            level_str = "debug";
            break;
    }

    char timestamp[32];

    time_t t = time(NULL);
    struct tm *p = localtime(&t);
    strftime(timestamp, 32, "%c", p);

    printf("[%s] %s:\t", timestamp, level_str);
    vprintf(format, argp);
    printf("\n");
}

static void log_msg(const enum log_level level, const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vlog_msg(level, format, arglist);
    va_end(arglist);
}

/* TODO: add log_error etc */
static void log_info(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vlog_msg(INFO, format, arglist);
    va_end(arglist);
}

static void log_error(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vlog_msg(ERROR, format, arglist);
    va_end(arglist);
}

static void log_warning(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vlog_msg(WARNING, format, arglist);
    va_end(arglist);
}

static void log_debug(const char *format, ...) {
    va_list arglist;
    va_start(arglist, format);
    vlog_msg(DEBUG, format, arglist);
    va_end(arglist);
}

void parse_request(const char *request_str) {
    json_error_t error;

    json_t *root = json_loads(request_str, 0, &error);

    if (!root) {
        log_error("request is not valid json at line %d: %s",
                  error.line,
                  error.text);
        goto error;
    }

    if (!json_is_object(root)) {
        log_error("request is not a json object");
        goto error;
    }

    json_t *request = json_object_get(root, "request");
    if (!json_is_string(request)) {
        log_error("\"request\" is not a string");
        goto error;
    }

    if (strcmp(json_string_value(request), "version") == 0) {
        log_info("version");
    }

error:
    json_decref(root);
    return;
}

void *worker(void *arg) {
    char *line = calloc(1, SAILSD_MAX_MESSAGE_LENGTH);
    int bytes_read;
    int client = *(int *)arg;

    log_debug("started thread");

    bytes_read = recv(client, line, SAILSD_MAX_MESSAGE_LENGTH, 0);
    if (bytes_read == -1) {
        perror("error reading from socket");
    } else {
        log_debug("request: \"%s\"", line);
        log_debug("bytes read: %i", bytes_read);
        parse_request(line);
        send(client, line, bytes_read, 0);
    }

    /* clean up and return */
    close(client);
    free(line);
    log_debug("closed thread");
    return arg;
}

int main(int argc, char *argv[]) {
    int c;
    const char *short_opt = "h";
    struct option long_opt[] = {
        {"help", no_argument, NULL, 'h'},
        {NULL,   0,           NULL, 0  }
    };

    while((c = getopt_long(argc, argv, short_opt, long_opt, NULL)) != -1) {
        switch(c) {
            case -1: /* no more arguments */
            case 0:  /* long options toggles */
                break;
            case 'h':
                printf("Usage: %s [OPTIONS]\n", argv[0]);
                printf("  -h, --help            print this help and exit\n");
                printf("\n");
                return 0;
            default:
                return 0;
        }
    }

    put_boat();
    log_info("started sailsd");
    log_msg(WARNING, "warning log");
    log_warning("warning log");

    /* start up socket server */
    struct sockaddr_in addr;

    int sd = socket(PF_INET, SOCK_STREAM, 0);
    if (sd < 0) {
        log_msg(ERROR, "cannot start socket");
    }

    addr.sin_family = AF_INET;
    addr.sin_port = htons(SAILSD_PORT);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");

    if (bind(sd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        log_msg(ERROR, "failed to listen on port");
    }

    if (listen(sd, 20) != 0) {
        log_msg(ERROR, "failed to listen on port");
    }
    log_info("listening on port %i", SAILSD_PORT);

    for (;;) {
        socklen_t addr_size = sizeof(addr);
        pthread_t child;

        int client = accept(sd, (struct sockaddr*)&addr, &addr_size);
        log_info("connected: %s:%d",
                 inet_ntoa(addr.sin_addr),
                 ntohs(addr.sin_port));
        if (pthread_create(&child, NULL, worker, &client) != 0) {
            perror("error creating thread");
        } else {
            pthread_detach(child);
        }
    }

    return 0;
}
