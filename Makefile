# Build di localsend-send (mittente L0).
# Su Haiku: serve -lnetwork per i socket BSD.
#   make
# Su Linux (solo per verifica di compilazione): i socket sono in libc.
#   make LDLIBS=

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc
LDLIBS   ?= -lnetwork

SRC = \
	src/protocol/Json.cpp \
	src/protocol/Models.cpp \
	src/protocol/Fingerprint.cpp \
	src/net/haiku/SocketHttpClient.cpp \
	src/client/FileSource.cpp \
	src/client/UploadSession.cpp \
	src/app/main_send.cpp

OBJ = $(SRC:.cpp=.o)
BIN = localsend-send

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(BIN)

.PHONY: all clean
