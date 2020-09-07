CC = gcc
CFLAGS = -Wall -pedantic-errors -std=c90 -D_XOPEN_SOURCE=500

all: server.x client.x

server.x: send_packet.o common.o protocol.o server.o
	$(CC) $(CFLAGS) send_packet.o common.o protocol.o server.o -o server.x

client.x: send_packet.o common.o protocol.o client.o packet_list.o
	$(CC) $(CFLAGS) send_packet.o common.o protocol.o client.o packet_list.c -o client.x

send_packet.o: send_packet.c send_packet.h
	$(CC) $(CFLAGS) -c send_packet.c

common.o: common.c common.h
	$(CC) $(CFLAGS) -c common.c

protocol.o: protocol.c protocol.h common.h
	$(CC) $(CFLAGS) -c protocol.c

packet_list.o: packet_list.c packet_list.h common.h
	$(CC) $(CFLAGS) -c packet_list.c

server.o: server.c send_packet.h protocol.h common.h
	$(CC) $(CFLAGS) -c server.c

client.o: client.c send_packet.h protocol.h common.h
	$(CC) $(CFLAGS) -c client.c

clean: 
	rm -f *.o *.x
