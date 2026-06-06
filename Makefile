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
	rm -f $(OBJ) $(BIN) test-receive-bin

# Test host-side del lato ricevente (L1-prep): logica di protocollo pura, nessun
# socket, compilabile e runnabile ovunque (non solo Haiku). Niente -lnetwork.
RECV_TEST_SRC = \
	src/protocol/Json.cpp \
	src/protocol/Models.cpp \
	src/protocol/Fingerprint.cpp \
	src/net/HttpServerSupport.cpp \
	src/server/ReceiveSession.cpp \
	tools/check/test_receive.cpp

test-receive:
	$(CXX) $(CXXFLAGS) -o test-receive-bin $(RECV_TEST_SRC)
	./test-receive-bin

.PHONY: all clean test-receive
