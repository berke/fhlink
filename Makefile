.PHONY: all clean

CXXFLAGS=-Wall -g -D_GNU_SOURCE

dupfinder: main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

clean:
	rm -f dupfinder *.o
