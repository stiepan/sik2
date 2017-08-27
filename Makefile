CXX=g++
CXXFLAGS=-Wall -O2 -std=c++11
ALL = siktacka_server siktacka_client

all: $(ALL)

%.o: %.c %.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

siktacka_server: server.o utils.o game_state.o generator.o events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lrt -lz

siktacka_client: client.o utils.o events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lrt -lz

.PHONY: clean

clean:
	rm *.o $(ALL)
