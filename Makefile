CXX = g++
CXXFLAGS = -Wall -Wextra -std=c++11 -O2
ifeq ($(shell uname),Linux)
       LDFLAGS = -pthread
else
       LDFLAGS =
endif

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
