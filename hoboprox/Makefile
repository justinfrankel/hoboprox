
CXX=g++
CC=gcc

CFLAGS = -g -O

.cpp.o:
	$(CXX) -c $(CFLAGS) $(CPPFLAGS) $< -o $@

JNLOBJS = ../WDL/jnetlib/connection.o ../WDL/jnetlib/listen.o ../WDL/jnetlib/asyncdns.o ../WDL/jnetlib/util.o ../WDL/jnetlib/httpserv.o ../WDL/jnetlib/webserver.o

OBJS = $(JNLOBJS) hobo_hobocon.o hobo_server.o hobosrv.o hobo_worldcon.o hobo_worldcon_irc.o hobo_web.o

hoboprox: $(OBJS)
	$(CXX) -g -o hoboprox $(OBJS) -lpthread

clean:
	-rm $(OBJS) hoboprox
