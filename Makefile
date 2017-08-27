CXX=g++
CXXFLAGS=-Wall -O2 -std=c++11
ALL = siktacka-server siktacka-client

all: $(ALL)

%.o: %.c %.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

siktacka-server: server.o utils.o game_state.o generator.o events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lrt -lz

siktacka-client: client.o utils.o events.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lrt -lz

.PHONY: clean

clean:
	rm *.o $(ALL)
