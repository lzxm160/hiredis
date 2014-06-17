writetoredis:CFLAGS=-std=c99 -g -o0 -I/usr/local/mysql/include -L/usr/local/mysql/lib -lmysqlclient

writetoredis:ae.o async.o dict.o hiredis.o net.o sds.o conf.o zmalloc.o main.c
	gcc $(CFLAGS) ae.o async.o dict.o hiredis.o net.o sds.o conf.o zmalloc.o main.c -owritetoredis 
	rm *.o
async.o:async.c
	gcc -c async.c
dict.o:dict.c
	gcc -c dict.c
hiredis.o:hiredis.c
	gcc -c hiredis.c
net.o:net.c
	gcc -c net.c
sds.o:sds.c
	gcc -c sds.c
conf.o:conf.c
	gcc -c conf.c
ae.o:ae.c
	gcc -c ae.c
zmalloc.o:zmalloc.c
	gcc -c zmalloc.c
clean:
	rm *.o
