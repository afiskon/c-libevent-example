/* vim: set ai et ts=4 sw=4: */

#include <arpa/inet.h>
#include <errno.h>
#include <event2/event.h>
#include <netinet/in.h> // keep this, for FreeBSD
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define READ_BUFF_SIZE 128
#define WRITE_BUFF_SIZE ((READ_BUFF_SIZE)*8)

typedef struct connection_ctx_t {
    struct connection_ctx_t* next;
    struct connection_ctx_t* prev;

    evutil_socket_t fd;
    struct event_base* base;
    struct event* read_event;
    struct event* write_event;

    uint8_t read_buff[READ_BUFF_SIZE];
    uint8_t write_buff[WRITE_BUFF_SIZE];

    ssize_t read_buff_used;
    ssize_t write_buff_used;
} connection_ctx_t;

void error(const char* msg) {
    fprintf(stderr, "%s, errno = %d\n", msg, errno);
    exit(1);
}

// called manually
void on_close(connection_ctx_t* ctx) {
    printf("[%p] on_close called, fd = %d\n", ctx, ctx->fd);

    // remove ctx from the lined list
    ctx->prev->next = ctx->next;
    ctx->next->prev = ctx->prev;

    event_del(ctx->read_event);
    event_free(ctx->read_event);

    if(ctx->write_event) {
        event_del(ctx->write_event);
        event_free(ctx->write_event);
    }

    close(ctx->fd);
    free(ctx);
}

// called manually
void on_string_received(const char* str, int len, connection_ctx_t* ctx) {
    printf("[%p] a complete string received: '%s', length = %d\n", ctx, str, len);

    connection_ctx_t* peer = ctx->next;
    while(peer != ctx) {
        if(peer->write_event == NULL) { // list head, skipping
            peer = peer->next;
            continue;
        }

        printf("[%p] sending a message to %p...\n", ctx, peer);

        // check that there is enough space in the write buffer
        if(WRITE_BUFF_SIZE - peer->write_buff_used < len + 1) {
            // if it's not, call on_close being careful with the links in the linked list
            printf("[%p] unable to send a message to %p - "
                   "not enough space in the buffer; "
                   "closing %p's connection\n",
                   ctx,
                   peer,
                   peer);
            connection_ctx_t* next = peer->next;
            on_close(peer);
            peer = next;
            continue;
        }

        // append data to the buffer
        memcpy(peer->write_buff + peer->write_buff_used, str, len);
        peer->write_buff[peer->write_buff_used + len] = '\n';
        peer->write_buff_used += len + 1;

        // add writing event (it's not a problem to call it multiple times)
        if(event_add(peer->write_event, NULL) < 0)
            error("event_add(peer->write_event, ...) failed");

        peer = peer->next;
    }
}

void on_read(evutil_socket_t fd, short flags, void* arg) {
    connection_ctx_t* ctx = arg;

    printf("[%p] on_read called, fd = %d\n", ctx, fd);

    ssize_t bytes;
    for(;;) {
        bytes = read(fd, ctx->read_buff + ctx->read_buff_used, READ_BUFF_SIZE - ctx->read_buff_used);
        if(bytes == 0) {
            printf("[%p] client disconnected!\n", ctx);
            on_close(ctx);
            return;
        }

        if(bytes < 0) {
            if(errno == EINTR)
                continue;

            printf("[%p] read() failed, errno = %d, closing connection.\n", ctx, errno);
            on_close(ctx);
            return;
        }

        break; // read() succeeded
    }

    ssize_t check = ctx->read_buff_used;
    ssize_t check_end = ctx->read_buff_used + bytes;
    ctx->read_buff_used = check_end;

    while(check < check_end) {
        if(ctx->read_buff[check] != '\n') {
            check++;
            continue;
        }

        int length = (int)check;
        ctx->read_buff[length] = '\0';
        if((length > 0) && (ctx->read_buff[length - 1] == '\r')) {
            ctx->read_buff[length - 1] = '\0';
            length--;
        }

        on_string_received((const char*)ctx->read_buff, length, ctx);

        // shift read_buff (optimize!)
        memmove(ctx->read_buff, ctx->read_buff + check, check_end - check - 1);
        ctx->read_buff_used -= check + 1;
        check_end -= check;
        check = 0;
    }

    if(ctx->read_buff_used == READ_BUFF_SIZE) {
        printf("[%p] client sent a very long string, closing connection.\n", ctx);
        on_close(ctx);
    }
}

void on_write(evutil_socket_t fd, short flags, void* arg) {
    connection_ctx_t* ctx = arg;
    printf("[%p] on_write called, fd = %d\n", ctx, fd);

    ssize_t bytes;
    for(;;) {
        bytes = write(fd, ctx->write_buff, ctx->write_buff_used);
        if(bytes <= 0) {
            if(errno == EINTR)
                continue;

            printf("[%p] write() failed, errno = %d, closing connection.\n", ctx, errno);
            on_close(ctx);
            return;
        }

        break; // write() succeeded
    }

    // shift the write_buffer (optimize!)
    memmove(ctx->write_buff, ctx->write_buff + bytes, ctx->write_buff_used - bytes);
    ctx->write_buff_used -= bytes;

    // if there is nothing to send call event_del
    if(ctx->write_buff_used == 0) {
        printf("[%p] write_buff is empty, calling event_del(write_event)\n", ctx);
        if(event_del(ctx->write_event) < 0)
            error("event_del() failed");
    }
}

void on_accept(evutil_socket_t listen_sock, short flags, void* arg) {
    connection_ctx_t* head_ctx = (connection_ctx_t*)arg;
    evutil_socket_t fd = 0;

    do {
        fd = accept(listen_sock, 0, 0);
    } while((fd < 0) && (errno == EINTR));

    if(fd < 0)
        error("accept() failed");

    // make in nonblocking
    if(evutil_make_socket_nonblocking(fd) < 0)
        error("evutil_make_socket_nonblocking() failed");

    connection_ctx_t* ctx = (connection_ctx_t*)malloc(sizeof(connection_ctx_t));
    if(!ctx)
        error("malloc() failed");

    // add ctx to the linked list
    ctx->prev = head_ctx;
    ctx->next = head_ctx->next;
    head_ctx->next->prev = ctx;
    head_ctx->next = ctx;

    ctx->base = head_ctx->base;

    ctx->read_buff_used = 0;
    ctx->write_buff_used = 0;

    printf("[%p] New connection! fd = %d\n", ctx, fd);

    ctx->fd = fd;
    ctx->read_event = event_new(ctx->base, fd, EV_READ | EV_PERSIST, on_read, (void*)ctx);
    if(!ctx->read_event)
        error("event_new(... EV_READ ...) failed");

    ctx->write_event = event_new(ctx->base, fd, EV_WRITE | EV_PERSIST, on_write, (void*)ctx);
    if(!ctx->write_event)
        error("event_new(... EV_WRITE ...) failed");

    if(event_add(ctx->read_event, NULL) < 0)
        error("event_add(read_event, ...) failed");
}

void run(char* host, int port) {
    // allocate memory for a connection ctx (used a linked list head)
    connection_ctx_t* head_ctx = (connection_ctx_t*)malloc(sizeof(connection_ctx_t));
    if(!head_ctx)
        error("malloc() failed");

    head_ctx->next = head_ctx;
    head_ctx->prev = head_ctx;
    head_ctx->write_event = NULL;
    head_ctx->read_buff_used = 0;
    head_ctx->write_buff_used = 0;

    // create a socket
    head_ctx->fd = socket(AF_INET, SOCK_STREAM, 0);
    if(head_ctx->fd < 0)
        error("socket() failed");

    // make in nonblocking
    if(evutil_make_socket_nonblocking(head_ctx->fd) < 0)
        error("evutil_make_socket_nonblocking() failed");

    // bind and listen
    struct sockaddr_in sin;
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = inet_addr(host);
    if(bind(head_ctx->fd, (struct sockaddr*)&sin, sizeof(sin)) < 0)
        error("bind() failed");

    if(listen(head_ctx->fd, 1000) < 0)
        error("listen() failed");

    // create an event base
    struct event_base* base = event_base_new();
    if(!base)
        error("event_base_new() failed");

    // create a new event
    struct event* accept_event = event_new(base, head_ctx->fd, EV_READ | EV_PERSIST, on_accept, (void*)head_ctx);
    if(!accept_event)
        error("event_new() failed");

    head_ctx->base = base;
    head_ctx->read_event = accept_event;

    // schedule the execution of accept_event
    if(event_add(accept_event, NULL) < 0)
        error("event_add() failed");

    // run the event dispatching loop
    if(event_base_dispatch(base) < 0)
        error("event_base_dispatch() failed");

    // free allocated resources
    on_close(head_ctx);
    event_base_free(base);
}

/*
 * If client will close a connection send() will just return -1
 * instead of killing a process with SIGPIPE.
 */
void ignore_sigpipe() {
    sigset_t msk;
    if(sigemptyset(&msk) < 0)
        error("sigemptyset() failed");

    if(sigaddset(&msk, SIGPIPE) < 0)
        error("sigaddset() failed");

    //    if(pthread_sigmask(SIG_BLOCK, &msk, nullptr) != 0)
    //        error("pthread_sigmask() failed");
}

int main(int argc, char** argv) {
    if(argc < 3) {
        printf("Usage: %s <host> <port>\n", argv[0]);
        exit(1);
    }

    char* host = argv[1];
    int port = atoi(argv[2]);
    printf("Starting chat server on %s:%d\n", host, port);
    ignore_sigpipe();
    run(host, port);
    return 0;
}
