#define _GNU_SOURCE
#include "fmacros.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <assert.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include "hiredis.h"
#include "conf.h"
#include <mysql.h>
#include "async.h"
#include "adapters/ae.h"
#include <signal.h>
/* Put event loop in the global scope, so it can be explicitly stopped */
static aeEventLoop *loop;

void getCallback(redisAsyncContext *c, void *r, void *privdata) {
    redisReply *reply = r;
    if (reply == NULL) return;
    printf("argv[%s]: %s\n", (char*)privdata, reply->str);

    /* Disconnect after receiving the reply to GET */
    redisAsyncDisconnect(c);
}

void connectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(loop);
        return;
    }

    printf("Connected...\n");
}

void disconnectCallback(const redisAsyncContext *c, int status) {
    if (status != REDIS_OK) {
        printf("Error: %s\n", c->errstr);
        aeStop(loop);
        return;
    }

    printf("Disconnected...\n");
    aeStop(loop);
}
enum connection_type {
    CONN_TCP,
    CONN_UNIX
};

struct config {
    enum connection_type type;

    struct {
        const char *host;
        int port;
        struct timeval timeout;
    } tcp;

    struct {
        const char *path;
    } unixl;
};

/* The following lines make up our testing "framework" :) */
static int tests = 0, fails = 0;
#define test(_s) { printf("#%02d ", ++tests); printf(_s); }
#define test_cond(_c) if(_c) printf("\033[0;32mPASSED\033[0;0m\n"); else {printf("\033[0;31mFAILED\033[0;0m\n"); fails++;}

static long long usec(void) {
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (((long long)tv.tv_sec)*1000000)+tv.tv_usec;
}

static redisContext *select_database(redisContext *c) {
    redisReply *reply;

    /* Switch to DB 9 for testing, now that we know we can chat. */
    reply = redisCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Make sure the DB is emtpy */
    reply = redisCommand(c,"DBSIZE");
    assert(reply != NULL);
    if (reply->type == REDIS_REPLY_INTEGER && reply->integer == 0) {
        /* Awesome, DB 9 is empty and we can continue. */
        freeReplyObject(reply);
    } else {
        printf("Database #9 is not empty, test can not continue\n");
        exit(1);
    }

    return c;
}

static void disconnect(redisContext *c) {
    redisReply *reply;

    /* Make sure we're on DB 9. */
    reply = redisCommand(c,"SELECT 9");
    assert(reply != NULL);
    freeReplyObject(reply);
    reply = redisCommand(c,"FLUSHDB");
    assert(reply != NULL);
    freeReplyObject(reply);

    /* Free the context as well. */
    redisFree(c);
}

static redisContext *connect(struct config config) {
    redisContext *c = NULL;

    if (config.type == CONN_TCP) {
        c = redisConnect(config.tcp.host, config.tcp.port);
    } else if (config.type == CONN_UNIX) {
        c = redisConnectUnix(config.unixl.path);
    } else {
        assert(NULL);
    }

    if (c == NULL) {
        printf("Connection error: can't allocate redis context\n");
        exit(1);
    } else if (c->err) {
        printf("Connection error: %s\n", c->errstr);
        exit(1);
    }

    return select_database(c);
}

static void test_format_commands(void) {
    char *cmd;
    int len;

    test("Format command without interpolation: ");
    len = redisFormatCommand(&cmd,"SET foo bar");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    test("Format command with %%s string interpolation: ");
    len = redisFormatCommand(&cmd,"SET %s %s","foo","bar");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    test("Format command with %%s and an empty string: ");
    len = redisFormatCommand(&cmd,"SET %s %s","foo","");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    free(cmd);

    test("Format command with an empty string in between proper interpolations: ");
    len = redisFormatCommand(&cmd,"SET %s %s","","foo");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$0\r\n\r\n$3\r\nfoo\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(0+2)+4+(3+2));
    free(cmd);

    test("Format command with %%b string interpolation: ");
    len = redisFormatCommand(&cmd,"SET %b %b","foo",(size_t)3,"b\0r",(size_t)3);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nb\0r\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    test("Format command with %%b and an empty string: ");
    len = redisFormatCommand(&cmd,"SET %b %b","foo",(size_t)3,"",(size_t)0);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$0\r\n\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(0+2));
    free(cmd);

    test("Format command with literal %%: ");
    len = redisFormatCommand(&cmd,"SET %% %%");
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$1\r\n%\r\n$1\r\n%\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(1+2)+4+(1+2));
    free(cmd);

    /* Vararg width depends on the type. These tests make sure that the
     * width is correctly determined using the format and subsequent varargs
     * can correctly be interpolated. */
#define INTEGER_WIDTH_TEST(fmt, type) do {                                                \
    type value = 123;                                                                     \
    test("Format command with printf-delegation (" #type "): ");                          \
    len = redisFormatCommand(&cmd,"key:%08" fmt " str:%s", value, "hello");               \
    test_cond(strncmp(cmd,"*2\r\n$12\r\nkey:00000123\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    free(cmd);                                                                            \
} while(0)

#define FLOAT_WIDTH_TEST(type) do {                                                       \
    type value = 123.0;                                                                   \
    test("Format command with printf-delegation (" #type "): ");                          \
    len = redisFormatCommand(&cmd,"key:%08.3f str:%s", value, "hello");                   \
    test_cond(strncmp(cmd,"*2\r\n$12\r\nkey:0123.000\r\n$9\r\nstr:hello\r\n",len) == 0 && \
        len == 4+5+(12+2)+4+(9+2));                                                       \
    free(cmd);                                                                            \
} while(0)

    INTEGER_WIDTH_TEST("d", int);
    INTEGER_WIDTH_TEST("hhd", char);
    INTEGER_WIDTH_TEST("hd", short);
    INTEGER_WIDTH_TEST("ld", long);
    INTEGER_WIDTH_TEST("lld", long long);
    INTEGER_WIDTH_TEST("u", unsigned int);
    INTEGER_WIDTH_TEST("hhu", unsigned char);
    INTEGER_WIDTH_TEST("hu", unsigned short);
    INTEGER_WIDTH_TEST("lu", unsigned long);
    INTEGER_WIDTH_TEST("llu", unsigned long long);
    FLOAT_WIDTH_TEST(float);
    FLOAT_WIDTH_TEST(double);

    test("Format command with invalid printf format: ");
    len = redisFormatCommand(&cmd,"key:%08p %b",(void*)1234,"foo",(size_t)3);
    test_cond(len == -1);

    const char *argv[3];
    argv[0] = "SET";
    argv[1] = "foo\0xxx";
    argv[2] = "bar";
    size_t lens[3] = { 3, 7, 3 };
    int argc = 3;

    test("Format command by passing argc/argv without lengths: ");
    len = redisFormatCommandArgv(&cmd,argc,argv,NULL);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(3+2)+4+(3+2));
    free(cmd);

    test("Format command by passing argc/argv with lengths: ");
    len = redisFormatCommandArgv(&cmd,argc,argv,lens);
    test_cond(strncmp(cmd,"*3\r\n$3\r\nSET\r\n$7\r\nfoo\0xxx\r\n$3\r\nbar\r\n",len) == 0 &&
        len == 4+4+(3+2)+4+(7+2)+4+(3+2));
    free(cmd);
}

static void test_reply_reader(void) {
    redisReader *reader;
    void *reply;
    int ret;
    int i;

    test("Error handling in reply parser: ");
    reader = redisReaderCreate();
    redisReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = redisReaderGetReply(reader,NULL);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Protocol error, got \"@\" as reply type byte") == 0);
    redisReaderFree(reader);

    /* when the reply already contains multiple items, they must be free'd
     * on an error. valgrind will bark when this doesn't happen. */
    test("Memory cleanup in reply parser: ");
    reader = redisReaderCreate();
    redisReaderFeed(reader,(char*)"*2\r\n",4);
    redisReaderFeed(reader,(char*)"$5\r\nhello\r\n",11);
    redisReaderFeed(reader,(char*)"@foo\r\n",6);
    ret = redisReaderGetReply(reader,NULL);
    test_cond(ret == REDIS_ERR &&
              strcasecmp(reader->errstr,"Protocol error, got \"@\" as reply type byte") == 0);
    redisReaderFree(reader);

    test("Set error on nested multi bulks with depth > 7: ");
    reader = redisReaderCreate();

    for (i = 0; i < 9; i++) {
        redisReaderFeed(reader,(char*)"*1\r\n",4);
    }

    ret = redisReaderGetReply(reader,NULL);
    test_cond(ret == REDIS_ERR &&
              strncasecmp(reader->errstr,"No support for",14) == 0);
    redisReaderFree(reader);

    test("Works with NULL functions for reply: ");
    reader = redisReaderCreate();
    reader->fn = NULL;
    redisReaderFeed(reader,(char*)"+OK\r\n",5);
    ret = redisReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
    redisReaderFree(reader);

    test("Works when a single newline (\\r\\n) covers two calls to feed: ");
    reader = redisReaderCreate();
    reader->fn = NULL;
    redisReaderFeed(reader,(char*)"+OK\r",4);
    ret = redisReaderGetReply(reader,&reply);
    assert(ret == REDIS_OK && reply == NULL);
    redisReaderFeed(reader,(char*)"\n",1);
    ret = redisReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK && reply == (void*)REDIS_REPLY_STATUS);
    redisReaderFree(reader);

    test("Don't reset state after protocol error: ");
    reader = redisReaderCreate();
    reader->fn = NULL;
    redisReaderFeed(reader,(char*)"x",1);
    ret = redisReaderGetReply(reader,&reply);
    assert(ret == REDIS_ERR);
    ret = redisReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_ERR && reply == NULL);
    redisReaderFree(reader);

    /* Regression test for issue #45 on GitHub. */
    test("Don't do empty allocation for empty multi bulk: ");
    reader = redisReaderCreate();
    redisReaderFeed(reader,(char*)"*0\r\n",4);
    ret = redisReaderGetReply(reader,&reply);
    test_cond(ret == REDIS_OK &&
        ((redisReply*)reply)->type == REDIS_REPLY_ARRAY &&
        ((redisReply*)reply)->elements == 0);
    freeReplyObject(reply);
    redisReaderFree(reader);
}

static void test_blocking_connection_errors(void) {
    redisContext *c;

    test("Returns error when host cannot be resolved: ");
    c = redisConnect((char*)"idontexist.local", 6379);
    test_cond(c->err == REDIS_ERR_OTHER &&
        (strcmp(c->errstr,"Name or service not known") == 0 ||
         strcmp(c->errstr,"Can't resolve: idontexist.local") == 0 ||
         strcmp(c->errstr,"nodename nor servname provided, or not known") == 0 ||
         strcmp(c->errstr,"no address associated with name") == 0));
    redisFree(c);

    test("Returns error when the port is not open: ");
    c = redisConnect((char*)"localhost", 1);
    test_cond(c->err == REDIS_ERR_IO &&
        strcmp(c->errstr,"Connection refused") == 0);
    redisFree(c);

    test("Returns error when the unix socket path doesn't accept connections: ");
    c = redisConnectUnix((char*)"/tmp/idontexist.sock");
    test_cond(c->err == REDIS_ERR_IO); /* Don't care about the message... */
    redisFree(c);
}

static void test_blocking_connection(struct config config) {
    redisContext *c;
    redisReply *reply;

    c = connect(config);

    test("Is able to deliver commands: ");
    reply = redisCommand(c,"PING");
    test_cond(reply->type == REDIS_REPLY_STATUS &&
        strcasecmp(reply->str,"pong") == 0)
    freeReplyObject(reply);

    test("Is a able to send commands verbatim: ");
    reply = redisCommand(c,"SET foo bar");
    test_cond (reply->type == REDIS_REPLY_STATUS &&
        strcasecmp(reply->str,"ok") == 0)
    freeReplyObject(reply);

    test("%%s String interpolation works: ");
    reply = redisCommand(c,"SET %s %s","foo","hello world");
    freeReplyObject(reply);
    reply = redisCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        strcmp(reply->str,"hello world") == 0);
    freeReplyObject(reply);

    test("%%b String interpolation works: ");
    reply = redisCommand(c,"SET %b %b","foo",(size_t)3,"hello\x00world",(size_t)11);
    freeReplyObject(reply);
    reply = redisCommand(c,"GET foo");
    test_cond(reply->type == REDIS_REPLY_STRING &&
        memcmp(reply->str,"hello\x00world",11) == 0)

    test("Binary reply length is correct: ");
    test_cond(reply->len == 11)
    freeReplyObject(reply);

    test("Can parse nil replies: ");
    reply = redisCommand(c,"GET nokey");
    test_cond(reply->type == REDIS_REPLY_NIL)
    freeReplyObject(reply);

    /* test 7 */
    test("Can parse integer replies: ");
    reply = redisCommand(c,"INCR mycounter");
    test_cond(reply->type == REDIS_REPLY_INTEGER && reply->integer == 1)
    freeReplyObject(reply);

    test("Can parse multi bulk replies: ");
    freeReplyObject(redisCommand(c,"LPUSH mylist foo"));
    freeReplyObject(redisCommand(c,"LPUSH mylist bar"));
    reply = redisCommand(c,"LRANGE mylist 0 -1");
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              !memcmp(reply->element[0]->str,"bar",3) &&
              !memcmp(reply->element[1]->str,"foo",3))
    freeReplyObject(reply);

    /* m/e with multi bulk reply *before* other reply.
     * specifically test ordering of reply items to parse. */
    test("Can handle nested multi bulk replies: ");
    freeReplyObject(redisCommand(c,"MULTI"));
    freeReplyObject(redisCommand(c,"LRANGE mylist 0 -1"));
    freeReplyObject(redisCommand(c,"PING"));
    reply = (redisCommand(c,"EXEC"));
    test_cond(reply->type == REDIS_REPLY_ARRAY &&
              reply->elements == 2 &&
              reply->element[0]->type == REDIS_REPLY_ARRAY &&
              reply->element[0]->elements == 2 &&
              !memcmp(reply->element[0]->element[0]->str,"bar",3) &&
              !memcmp(reply->element[0]->element[1]->str,"foo",3) &&
              reply->element[1]->type == REDIS_REPLY_STATUS &&
              strcasecmp(reply->element[1]->str,"pong") == 0);
    freeReplyObject(reply);

    disconnect(c);
}

static void test_blocking_io_errors(struct config config) {
    redisContext *c;
    redisReply *reply;
    void *_reply;
    int major, minor;

    /* Connect to target given by config. */
    c = connect(config);
    {
        /* Find out Redis version to determine the path for the next test */
        const char *field = "redis_version:";
        char *p, *eptr;

        reply = redisCommand(c,"INFO");
        p = strstr(reply->str,field);
        major = strtol(p+strlen(field),&eptr,10);
        p = eptr+1; /* char next to the first "." */
        minor = strtol(p,&eptr,10);
        freeReplyObject(reply);
    }

    test("Returns I/O error when the connection is lost: ");
    reply = redisCommand(c,"QUIT");
    if (major >= 2 && minor > 0) {
        /* > 2.0 returns OK on QUIT and read() should be issued once more
         * to know the descriptor is at EOF. */
        test_cond(strcasecmp(reply->str,"OK") == 0 &&
            redisGetReply(c,&_reply) == REDIS_ERR);
        freeReplyObject(reply);
    } else {
        test_cond(reply == NULL);
    }

    /* On 2.0, QUIT will cause the connection to be closed immediately and
     * the read(2) for the reply on QUIT will set the error to EOF.
     * On >2.0, QUIT will return with OK and another read(2) needed to be
     * issued to find out the socket was closed by the server. In both
     * conditions, the error will be set to EOF. */
    assert(c->err == REDIS_ERR_EOF &&
        strcmp(c->errstr,"Server closed the connection") == 0);
    redisFree(c);

    c = connect(config);
    test("Returns I/O error on socket timeout: ");
    struct timeval tv = { 0, 1000 };
    assert(redisSetTimeout(c,tv) == REDIS_OK);
    test_cond(redisGetReply(c,&_reply) == REDIS_ERR &&
        c->err == REDIS_ERR_IO && errno == EAGAIN);
    redisFree(c);
}

static void test_invalid_timeout_errors(struct config config) {
    redisContext *c;

    test("Set error when an invalid timeout usec value is given to redisConnectWithTimeout: ");

    config.tcp.timeout.tv_sec = 0;
    config.tcp.timeout.tv_usec = 10000001;

    c = redisConnectWithTimeout(config.tcp.host, config.tcp.port, config.tcp.timeout);

    test_cond(c->err == REDIS_ERR_IO);

    test("Set error when an invalid timeout sec value is given to redisConnectWithTimeout: ");

    config.tcp.timeout.tv_sec = (((LONG_MAX) - 999) / 1000) + 1;
    config.tcp.timeout.tv_usec = 0;

    c = redisConnectWithTimeout(config.tcp.host, config.tcp.port, config.tcp.timeout);

    test_cond(c->err == REDIS_ERR_IO);

    redisFree(c);
}

static void test_throughput(struct config config) {
    redisContext *c = connect(config);
    redisReply **replies;
    int i, num;
    long long t1, t2;

    test("Throughput:\n");
    for (i = 0; i < 500; i++)
        freeReplyObject(redisCommand(c,"LPUSH mylist foo"));

    num = 1000;
    replies = malloc(sizeof(redisReply*)*num);
    t1 = usec();
    for (i = 0; i < num; i++) {
        replies[i] = redisCommand(c,"PING");
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_STATUS);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(%dx PING: %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = malloc(sizeof(redisReply*)*num);
    t1 = usec();
    for (i = 0; i < num; i++) {
        replies[i] = redisCommand(c,"LRANGE mylist 0 499");
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_ARRAY);
        assert(replies[i] != NULL && replies[i]->elements == 500);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(%dx LRANGE with 500 elements: %.3fs)\n", num, (t2-t1)/1000000.0);

    num = 10000;
    replies = malloc(sizeof(redisReply*)*num);
    for (i = 0; i < num; i++)
        redisAppendCommand(c,"PING");
    t1 = usec();
    for (i = 0; i < num; i++) {
        assert(redisGetReply(c, (void*)&replies[i]) == REDIS_OK);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_STATUS);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(%dx PING (pipelined): %.3fs)\n", num, (t2-t1)/1000000.0);

    replies = malloc(sizeof(redisReply*)*num);
    for (i = 0; i < num; i++)
        redisAppendCommand(c,"LRANGE mylist 0 499");
    t1 = usec();
    for (i = 0; i < num; i++) {
        assert(redisGetReply(c, (void*)&replies[i]) == REDIS_OK);
        assert(replies[i] != NULL && replies[i]->type == REDIS_REPLY_ARRAY);
        assert(replies[i] != NULL && replies[i]->elements == 500);
    }
    t2 = usec();
    for (i = 0; i < num; i++) freeReplyObject(replies[i]);
    free(replies);
    printf("\t(%dx LRANGE with 500 elements (pipelined): %.3fs)\n", num, (t2-t1)/1000000.0);

    disconnect(c);
}

// static long __test_callback_flags = 0;
// static void __test_callback(redisContext *c, void *privdata) {
//     ((void)c);
//     /* Shift to detect execution order */
//     __test_callback_flags <<= 8;
//     __test_callback_flags |= (long)privdata;
// }
//
// static void __test_reply_callback(redisContext *c, redisReply *reply, void *privdata) {
//     ((void)c);
//     /* Shift to detect execution order */
//     __test_callback_flags <<= 8;
//     __test_callback_flags |= (long)privdata;
//     if (reply) freeReplyObject(reply);
// }
//
// static redisContext *__connect_nonblock() {
//     /* Reset callback flags */
//     __test_callback_flags = 0;
//     return redisConnectNonBlock("127.0.0.1", port, NULL);
// }
//
// static void test_nonblocking_connection() {
//     redisContext *c;
//     int wdone = 0;
//
//     test("Calls command callback when command is issued: ");
//     c = __connect_nonblock();
//     redisSetCommandCallback(c,__test_callback,(void*)1);
//     redisCommand(c,"PING");
//     test_cond(__test_callback_flags == 1);
//     redisFree(c);
//
//     test("Calls disconnect callback on redisDisconnect: ");
//     c = __connect_nonblock();
//     redisSetDisconnectCallback(c,__test_callback,(void*)2);
//     redisDisconnect(c);
//     test_cond(__test_callback_flags == 2);
//     redisFree(c);
//
//     test("Calls disconnect callback and free callback on redisFree: ");
//     c = __connect_nonblock();
//     redisSetDisconnectCallback(c,__test_callback,(void*)2);
//     redisSetFreeCallback(c,__test_callback,(void*)4);
//     redisFree(c);
//     test_cond(__test_callback_flags == ((2 << 8) | 4));
//
//     test("redisBufferWrite against empty write buffer: ");
//     c = __connect_nonblock();
//     test_cond(redisBufferWrite(c,&wdone) == REDIS_OK && wdone == 1);
//     redisFree(c);
//
//     test("redisBufferWrite against not yet connected fd: ");
//     c = __connect_nonblock();
//     redisCommand(c,"PING");
//     test_cond(redisBufferWrite(c,NULL) == REDIS_ERR &&
//               strncmp(c->error,"write:",6) == 0);
//     redisFree(c);
//
//     test("redisBufferWrite against closed fd: ");
//     c = __connect_nonblock();
//     redisCommand(c,"PING");
//     redisDisconnect(c);
//     test_cond(redisBufferWrite(c,NULL) == REDIS_ERR &&
//               strncmp(c->error,"write:",6) == 0);
//     redisFree(c);
//
//     test("Process callbacks in the right sequence: ");
//     c = __connect_nonblock();
//     redisCommandWithCallback(c,__test_reply_callback,(void*)1,"PING");
//     redisCommandWithCallback(c,__test_reply_callback,(void*)2,"PING");
//     redisCommandWithCallback(c,__test_reply_callback,(void*)3,"PING");
//
//     /* Write output buffer */
//     wdone = 0;
//     while(!wdone) {
//         usleep(500);
//         redisBufferWrite(c,&wdone);
//     }
//
//     /* Read until at least one callback is executed (the 3 replies will
//      * arrive in a single packet, causing all callbacks to be executed in
//      * a single pass). */
//     while(__test_callback_flags == 0) {
//         assert(redisBufferRead(c) == REDIS_OK);
//         redisProcessCallbacks(c);
//     }
//     test_cond(__test_callback_flags == 0x010203);
//     redisFree(c);
//
//     test("redisDisconnect executes pending callbacks with NULL reply: ");
//     c = __connect_nonblock();
//     redisSetDisconnectCallback(c,__test_callback,(void*)1);
//     redisCommandWithCallback(c,__test_reply_callback,(void*)2,"PING");
//     redisDisconnect(c);
//     test_cond(__test_callback_flags == 0x0201);
//     redisFree(c);
// }
int GetRequestLogNewName(char *RequestLogNewName)
{//这个是获得requestlog位置及名字：/F5_log/request_log/history_log/201401060920.log
	  struct tm  *ptm;
		long   ts;
		int    y,m,d,h,n,s;
		ts = time(NULL);
		ptm = localtime(&ts);
		y = ptm->tm_year+1900;  //年
		m = ptm->tm_mon+1;      //月
		d = ptm->tm_mday;       //日
		h = ptm->tm_hour;       //时
		n = ptm->tm_min;        //分
		s = ptm->tm_sec;        //秒
		//requestlog位置及名字：/F5_log/request_log/history_log/201401060920.log
		if(n%5==0)//每5分钟
		{
			sprintf(RequestLogNewName,"%d%02d%02d%02d%02d.log",y,m,d,h,n);	
			return 0;
	  }
	  else
	  {
	  	return -1;  		
	  }
	
}
int GetF5LogLocationNewName(char* F5LogNewLocation,char* F5LogNewName)
{//这个是获得f5日志位置及名字：
	///F5_log/2014_5m/20140109/2014010916/caccess_f5_20140109_1625.log
	struct tm *ptm=NULL;
    struct tm *ptm1=NULL;
    long ts=0;
    long ts1=0;
    int year=0,mon=0,day=0,hour=0,min=0,sec=0;
    int year1=0,mon1=0,day1=0,hour1=0,min1=0,sec1=0;
    ts=time(NULL);
    ts1=ts-10;
		ptm = localtime(&ts);

   		year = ptm->tm_year+1900;  //年
		mon = ptm->tm_mon+1;      //月
		day = ptm->tm_mday;       //日
		hour = ptm->tm_hour;       //时
		min = ptm->tm_min;        //分
		sec = ptm->tm_sec;        //秒

		ptm1 = localtime(&ts1);
   		year1 = ptm1->tm_year+1900;  //年
		mon1 = ptm1->tm_mon+1;      //月
		day1 = ptm1->tm_mday;       //日
		hour1 = ptm1->tm_hour;       //时
		min1 = ptm1->tm_min;        //分
		sec1 = ptm1->tm_sec;        //秒

		if(min%5==0)//每5分钟
		{		//用10s之前的时间拼起文件目录	
			sprintf(F5LogNewLocation,"%d_5m/%d%02d%02d/%d%02d%02d%02d",year1,year1,mon1,day1,year1,mon1,day1,hour1);	
			sprintf(F5LogNewName,"caccess_f5_%d%02d%02d_%02d%02d.log",year,mon,day,hour,min);	
			return 0;
		}
		else
		{
	  		return -1;  		
		}
	
}
int del_str_line(char *str)
{
    for(;*str!=0;str++)
    	{if(*str=='\n'||*str=='\r')
    		{	
    			*str='\0';
  				return 0;
  			}
    	}
    	return 0;
}
void GetUrlBeforeQuestionMark(char *buf)
{
	while(*buf!='\0')
	{
		if(*buf=='?')
			{
				*buf='\0';
				break;
			}
		buf++;
	}	
}
void GetSqlBeforeWhere(char *buf)
{
	char *temp=strcasestr(buf,"where");
	//char *temp=strstr(buf,"where");
	if(temp)
		*temp='\0';
}
int main(int argc, char **argv) {
	
	//程序成为Daemon
	//1、读配置，找到需要分析ip的日志，及F5的日志位置
	//if ip module ipmodule处理
	//if sql模块  sql模块处理   //后面加什么模块就转到相应模块处理
	//ip和url需要联动，首先找出ip访问最多的，然后再在其中找出哪个url最多，在mysql中找到相应的表
	//2、解析日志
	//3、存入redis中
	//4、redis信息定时放入mysql中
	
	char Debug[10]={0};
	char Daemon[10]={0};
	if(GetProfileString("./cls.conf", "system", "debug", Debug)==-1)
	{	 
		printf("get Debug error\n");
	   	exit(-1);
	}
	else if(strcmp(Debug,"on")==0)
	{	
		printf("Debug:%s\n",Debug);	
		#define DEBUG		
	}
	if(GetProfileString("./cls.conf", "system", "daemon", Daemon)==-1)
	{	 
		printf("get Daemon error\n");
	   	exit(-1);
	}
	else if(strcmp(Daemon,"on")==0)
	{
		printf("Daemon:%s\n",Daemon);
		//#define DAEMON
	}

#ifdef DAEMON
	 if(daemon(1,1)<0)
        exit(-1);  
#endif
	char RequestLogLocation[50]={0};//5分钟切一次，可以分析出ip访问量，然后存入到redis中
	char F5LogLocation[50]={0};//分析更多详细信息
	char ip[16]={0};
	//1、读取配置///////////////////////////////////////////
	
	if(GetProfileString("./cls.conf", "loglocation", "request_log", RequestLogLocation)==-1)
	{	 
		printf("request_log location error\n");
	   	exit(-1);
	}
#ifdef DEBUG
	printf("request_log location:%s\n",RequestLogLocation);
#endif
	if(GetProfileString("./cls.conf", "loglocation", "F5_log", F5LogLocation)==-1)
	{		
		printf("F5Log Location error\n");
	   	exit(-1);
	}
#ifdef DEBUG
	printf("F5_log location:%s\n",F5LogLocation);
#endif
   if(GetProfileString("./cls.conf", "ip", "top", ip)==-1)
	{
		printf("ip error\n");
		exit(-1);
	}
#ifdef DEBUG
   printf("ip:%s\n",ip);
#endif
  
   ////////////////////////////////连接redis开始
    unsigned int j;
    redisContext *c;
    redisReply *reply;
    struct config cfg = {
        .tcp = {
            .host = "127.0.0.1",
            .port = 6379
        },
        .unixl = {
            .path = "/tmp/redis.sock"
        }
    };
    int throughput = 1;
    // Ignore broken pipe signal (for I/O error tests). 
    signal(SIGPIPE, SIG_IGN);

    // Parse command line options. 
    argv++; argc--;
    while (argc) {
        if (argc >= 2 && !strcmp(argv[0],"-h")) {//host
            argv++; argc--;
            cfg.tcp.host = argv[0];
        } else if (argc >= 2 && !strcmp(argv[0],"-p")) {//port
            argv++; argc--;
            cfg.tcp.port = atoi(argv[0]);
        } else if (argc >= 2 && !strcmp(argv[0],"-s")) {//unix path
            argv++; argc--;
            cfg.unixl.path = argv[0];
        } else if (argc >= 1 && !strcmp(argv[0],"--skip-throughput")) {
            throughput = 0;
        } else {
            fprintf(stderr, "Invalid argument: %s\n", argv[0]);
            exit(1);
        }
        argv++; argc--;
    }
    
    struct timeval timeout = { 1, 500000 }; // 1.5 seconds
    c = redisConnectWithTimeout(cfg.tcp.host, cfg.tcp.port, timeout);
    if (c == NULL || c->err) {
        if (c) {
            printf("Connection error: %s\n", c->errstr);
            redisFree(c);
        } else {
            printf("Connection error: can't allocate redis context\n");
        }
        exit(1);
    }
 		// PING server
    reply = redisCommand(c,"PING");
    printf("PING: %s\n", reply->str);//返回pong为连接成功
    freeReplyObject(reply);    
///////////////////////连接redis结束///////////////////////////////////////////////

//////////////////////连接mysql开始////////////////////////////////////////////////
char Server[20]={0};
char DataBase[30]={0};
char User[20]={0};
char Password[20]={0};
char Table[20]={0};
	if(GetProfileString("./cls.conf", "mysql", "server", Server)==-1)
	{	 
		printf("Server error\n");
	   	exit(-1);
	}
	#ifdef DEBUG
	printf("Server:%s\n",Server);
	#endif
	
	if(GetProfileString("./cls.conf", "mysql", "database", DataBase)==-1)
	{		
		printf("DataBase error\n");
	   	exit(-1);
	}
	#ifdef DEBUG
	printf("DataBase:%s\n",DataBase);
	#endif
	
	if(GetProfileString("./cls.conf", "mysql", "table", Table)==-1)
	{		
		printf("Table error\n");
	   	exit(-1);
	}
	#ifdef DEBUG
	printf("Table:%s\n",Table);
	#endif
	
	if(GetProfileString("./cls.conf", "mysql", "user", User)==-1)
	{
		printf("User error\n");
		exit(-1);
	}
	#ifdef DEBUG
	printf("User:%s\n",User);
	#endif
	
	if(GetProfileString("./cls.conf", "mysql", "password", Password)==-1)
	{
		printf("Password error\n");
		exit(-1);
	}
	#ifdef DEBUG
	printf("Password:%s\n",Password);
	#endif
	
///////////////////开始连接mysql
    MYSQL Conn_ptr={0};
    MYSQL *conn_ptr=&Conn_ptr;  
    MYSQL_RES *res_ptr=NULL;  
    MYSQL_ROW sqlrow=0;  
    MYSQL_FIELD *fd=NULL;  
    int res=0, i=0, jj=0;  
      
    if (!mysql_init(NULL)) 
	{  
    		printf("mysql init error\n");
        return EXIT_FAILURE;  
    }       
    if (mysql_real_connect(conn_ptr, Server, User, Password, DataBase, 0, NULL, 0)) 
	{  
    	#ifdef DEBUG
	     printf("Connection mysql success\n");
	    #endif    		       
    } 
	else 
	{  
        printf("Connection mysql failed:%s\n",mysql_error(conn_ptr));  
    	#ifdef DEBUG
        printf("host:%s\n",conn_ptr->host);
        printf("user:%s\n",conn_ptr->user);
        printf("passwd:%s\n",conn_ptr->passwd);
        printf("db:%s\n",conn_ptr->db);
        printf("info:%s\n",conn_ptr->info);
	    #endif    
    }       			
//////////////////////连接mysql结束//////////////////////////////////////////////////  

	//变量及配置解析放大循环外面
	char RequestLogNewName[30]={0};
	char RequestLogName[30]={0};
	char F5LogNewLocation[50]={0};
	char F5LogNewName[40]={0};
	char cmdip[100]={0};//request log bash
	FILE *stream;//用于获得bash脚本的输出
	FILE *streamSearchF5;
	char SmallBuf[30]={0};
	char streamSearchF5buf[1024]={0};//用于存储从stream中读取的数据，即search脚本执行的结果
	char SelectBuf[100]={0};
	char streamSearchF5bufTemp[1024]={0};
	char InsertToList[1024]={0};
	for(;;)//整个大循环
	{
	//2、解析日志///////////////////////////////////
		//先拼凑一个文件名RequestLogNewName，判断是否跟上次解析的文件名RequestLogName相同，
		//相同就不再第二次解析，不相同则解析，最后把RequestLogNewName strcpy给RequestLogName
		memset(RequestLogNewName,0,sizeof(RequestLogNewName));
		memset(F5LogNewName,0,sizeof(F5LogNewName));
		if(!GetRequestLogNewName(RequestLogNewName))
		{
#ifdef DEBUG
			printf("RequestLogNewName=%s\n",RequestLogNewName);
#endif
		}
		else
		{
			printf("get RequestLogNewName error,sleep 5 sec!\n");
			sleep(5);
			continue;
		}
		if(!GetF5LogLocationNewName(F5LogNewLocation,F5LogNewName))
		{
#ifdef DEBUG
			printf("F5LogNewName=%s%s/%s\n",F5LogLocation,F5LogNewLocation,F5LogNewName);
#endif
		}
		else
		{
			printf("get F5LogNewName error,sleep 5 sec!\n");
			sleep(5);
			continue;
		}
		if(strcmp(RequestLogNewName,RequestLogName)==0)//还是老文件并已经解析过
			continue;
		else{ //新文件名生成，但实际文件有可能还未生成，需sleep(4),实际文件是5分或10分过2、3秒后生成的。
			sleep(4);
			memset(cmdip,0,sizeof(cmdip));
			sprintf(cmdip, "bash ip.sh %s%s",RequestLogLocation,RequestLogNewName);//解析出ip及count
#ifdef DEBUG
			printf("cmdip=%s\n",cmdip);	
#endif
    		stream=popen(cmdip,"r");
			if(stream==NULL)//命令未执行成功进入下一次循环
			{
#ifdef DEBUG
				printf("stream popen:%s\n",strerror(errno));
#endif
				continue;
			}
			char *buff[2] = {0}; 
			for (int i = 0; i < atoi(ip); i++) 
			{  
     			memset(SmallBuf,0,sizeof(SmallBuf)); 
     			if(fgets(SmallBuf,30,stream) != NULL)//从stream中读取一行，992 222.73.133.32
				{
					//去掉换行符
					del_str_line(SmallBuf);
					//将992 222.73.133.32拆分成两个字符串              
					buff[0] = strtok(SmallBuf," " );  
					buff[1] = strtok(NULL," ");
#ifdef DEBUG
					printf("zadd ipset%s %s %s\n",RequestLogNewName,buff[0],buff[1]);
#endif
					//使用已经连接好的redis上下文,将count 和ip写入redis的sorted set中
					reply = redisCommand(c,"zadd ipset%s %s %s",RequestLogNewName,buff[0],buff[1]);
#ifdef DEBUG
					printf("zadd: %s\n", reply->str);
#endif
					freeReplyObject(reply);

					//将前10ip对应的f5日志条目筛选出来。直接分析access_f5.log
					memset(cmdip,0,sizeof(cmdip));
					//获得系统时间，拼出目录和文件名，
					///F5_log/2014_5m/20140109/2014010916/caccess_f5_20140109_1615.log
					sprintf(cmdip, "nohup bash SearchIpInF5.sh %s%s/%s %s",F5LogLocation,F5LogNewLocation,F5LogNewName,buff[1]);//解析出ip及count,buff[1]为ip地址
#ifdef DEBUG
					printf("searchipinf5=%s\n",cmdip);	
#endif
    				streamSearchF5=popen(cmdip,"r");
    				if(streamSearchF5==NULL)//查找ip失败，继续下次循环
					{
#ifdef DEBUG
						printf("streamSearchF5 popen:%s\n",strerror(errno));
#endif
						continue;
					}
				   //得到的流是基于前20个ip的url信息，例子如下： 
				   // 101.226.180.132 [08/Jan/2014:11:11:22 +0800] 
				   ///login?URL=%2Fdiskall%2Fdownloadfile.php%3Ffileid%3D2707567%26type%3DEclass%26diskid%3D46783 
				   //http://www.yiban.cn/eclass/home.php?id=46783
					//将信息放入redis里保存，最终存储到mysql中
				  if(fgets(streamSearchF5buf,1024,streamSearchF5) != NULL)//只取出一条分析
				  {
      				del_str_line(streamSearchF5buf);//去掉回车换行
        
					//在streamSearchF5buf中增加url对应的数据库表信息,

					///////////mysql中查找url对应表信息-start
					//memset(SmallBuf,0,sizeof(SmallBuf));
					memset(streamSearchF5bufTemp,0,sizeof(streamSearchF5bufTemp));
					strcpy(streamSearchF5bufTemp,streamSearchF5buf);//复制一份取出url
					buff[0] = strtok(streamSearchF5bufTemp," " );  
					buff[0] = strtok(NULL," ");
					buff[0] = strtok(NULL," ");
					buff[0] = strtok(NULL," ");//第四个字段是url
			#ifdef DEBUG
					printf("url:%s\n",buff[0]);		
			#endif
					/////////////这里处理下url，截取问号前面的部分，若没有问号则不变//////////////////////
					GetUrlBeforeQuestionMark(buff[0]);
			#ifdef DEBUG		
					printf("url after del question mark:%s\n",buff[0]);
			#endif		
					/////////////////////////////////////////////////////////////////////////////////////
					//组成查询语句
					sprintf(SelectBuf,"select `sql` from mysql_log where url like \'%%%s%%\'",buff[0]);
			#ifdef DEBUG
					printf("select:%s\n",SelectBuf);
			#endif
					res = mysql_query(conn_ptr, SelectBuf); //查询语句  
					if(res)
					{	
						printf("SELECT error:%s\n",mysql_error(conn_ptr));     
					} 
					else 
					{        
						res_ptr = mysql_store_result(conn_ptr);             //取出结果集  
						if(res_ptr) 
						{      
#ifdef DEBUG         			
							printf("%lu Rows\n",(unsigned long)mysql_num_rows(res_ptr)); //输出行数 
#endif
							sqlrow=mysql_fetch_row(res_ptr);
							if(mysql_num_rows(res_ptr)==0)
							{
								sprintf(InsertToList,"%s %s",buff[1],streamSearchF5buf);
								#ifdef DEBUG  
								printf("didn't find any match in mysql\n");

								printf("lpush iplist%s %s\n",RequestLogNewName,InsertToList);
								#endif
								////////////写入redis
								reply = redisCommand(c,"lpush iplist%s %s",RequestLogNewName,InsertToList);	
								#ifdef DEBUG  
								printf("lpush: %s\n", reply->str);
								#endif
							} 
							else
							{
								//只读一行就可以了
								GetSqlBeforeWhere(sqlrow[0]);
								sprintf(InsertToList,"%s %s %s",buff[1],streamSearchF5buf,sqlrow[0]);
								////////////写入redis
								reply = redisCommand(c,"lpush iplist%s %s",RequestLogNewName,InsertToList);
								#ifdef DEBUG
								printf("lpush iplist%s %s\n",RequestLogNewName,InsertToList);
								printf("lpush: %s\n", reply->str);
								#endif
							} 								
							freeReplyObject(reply);
          
						}              
			if (mysql_errno(conn_ptr)) 
			{                      
				fprintf(stderr,"Retrive error:s\n",mysql_error(conn_ptr));               
			}                          
            mysql_free_result(res_ptr);    
		}
        pclose(streamSearchF5); 
         ///////////mysql中查找url对应表信息-end
      }  
   }
}     	
    	pclose(stream);   
    	strcpy(RequestLogName,RequestLogNewName);//解析过的新文件名复制到旧的里面保存
#ifdef DEBUG
    	printf("RequestLogNewName=%s\n",RequestLogNewName);
    	printf("RequestLogName=%s\n",RequestLogName);
#endif
    	sleep(250);//解析完一次，250s后再继续    	
    }	
}
 
	mysql_close(conn_ptr); 	
    /* Disconnects and frees the context */
    redisFree(c);
		
    return 0;
}
