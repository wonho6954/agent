include ../../../../build.config

CFLAGS = $(DEFINES) $(INCLUDES)

CPPFILES = $(wildcard *.cpp)

OBJ = $(CPPFILES:.cpp=.o)

all: $(OBJ)

.cpp.o:
	$(CC) $(CFLAGS) -c $*.cpp

clean:
	rm -f ./*.o
