CC=g++ -oterm -lpthread
CFLAGS=-c -std=c++11 -Wall
LDFLAGS=
SOURCES=main.cpp torrent_manager.cpp status_manager.cpp file_manager.cpp thread_pool.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=torrent

DEBUG=1

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(LDFLAGS) $(OBJECTS) -o $@

.cpp.o:
	$(CC) $(CFLAGS) $< -o $@

clean:
	\rm -rf *.o torrent
