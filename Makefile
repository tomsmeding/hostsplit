CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++14 -O2
LDFLAGS =
BIN = main

obj_files = $(patsubst %.cpp,%.o,$(wildcard *.cpp))

.PHONY: all clean remake

all: $(BIN)

clean:
	rm -f $(BIN) *.o

remake: clean all

$(BIN): $(obj_files)
	$(CXX) -o $@ $^ $(LDFLAGS)

%.o: %.c *.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<
