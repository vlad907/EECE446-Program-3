# Name of your executable
EXE = peer

# All your source files
SRC = peer.c

# C/C++ flags
CFLAGS = -Wall
CXXFLAGS = -Wall

# Any additional required libraries
LDLIBS =

# C/C++ compiler to use
CC = gcc
CXX = g++

.PHONY: all
all: $(EXE)

# Implicit rules defined by Make if you name your source file
# the same as the executable file, but you can redefine if needed
#
#$(EXE): $(SRC)
#	$(CC) $(CFLAGS) $(SRC) $(LDLIBS) -o $(EXE)
#
# OR
#
#$(EXE): $(SRC)
#	$(CXX) $(CXXFLAGS) $(SRC) $(LDLIBS) -o $(EXE)

.PHONY: clean
clean:
	rm -f $(EXE)
