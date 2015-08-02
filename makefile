CC=g++
CPPFLAGS=-Wall -g
BIN=main
OBJS=main.o http_conn.o

$(BIN):$(OBJS)
		 $(CC) $(CPPFLAGS) $^ -o $@ -lpthread

main.o:locker.h threadpool.hpp http_conn.h
http_conn.o:http_conn.h locker.h

.PPHONY:clean
clean:
	-rm -f *.o $(BIN)
