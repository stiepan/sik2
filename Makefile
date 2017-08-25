CXX=g++
CXXFLAGS=-Wall -O2 -std=c++11
ALL = siktacka_server

all: $(ALL)

%.o: %.c %.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

siktacka_server: server.o utils.o game_state.o generator.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lrt

.PHONY: clean

clean:
	rm *.o $(ALL)
