# Build di localsend-send (mittente L0) e localsend-receive (ricevente L1).
# Su Haiku: serve -lnetwork per i socket BSD.
#   make
# Su Linux (solo per verifica di compilazione): i socket sono in libc.
#   make LDLIBS=

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc
LDLIBS   ?= -lnetwork

# Sorgenti condivisi (protocollo, indipendenti dal trasporto).
PROTO = \
	src/protocol/Json.cpp \
	src/protocol/Models.cpp \
	src/protocol/Fingerprint.cpp

# Mittente (L0).
SEND_SRC = $(PROTO) \
	src/net/haiku/SocketHttpClient.cpp \
	src/client/FileSource.cpp \
	src/client/UploadSession.cpp \
	src/app/main_send.cpp

# Ricevente (L1).
RECV_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/net/haiku/SocketHttpServer.cpp \
	src/server/FileSink.cpp \
	src/server/ReceiveSession.cpp \
	src/app/main_receive.cpp

SEND_OBJ = $(SEND_SRC:.cpp=.o)
RECV_OBJ = $(RECV_SRC:.cpp=.o)
SEND_BIN = localsend-send
RECV_BIN = localsend-receive

all: $(SEND_BIN) $(RECV_BIN)

$(SEND_BIN): $(SEND_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(SEND_OBJ) $(LDLIBS)

$(RECV_BIN): $(RECV_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(RECV_OBJ) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SEND_OBJ) $(RECV_OBJ) $(SEND_BIN) $(RECV_BIN) test-receive-bin

# Test host-side del lato ricevente (L1-prep): logica di protocollo pura, nessun
# socket, compilabile e runnabile ovunque (non solo Haiku). Niente -lnetwork.
RECV_TEST_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/server/ReceiveSession.cpp \
	tools/check/test_receive.cpp

test-receive:
	$(CXX) $(CXXFLAGS) -o test-receive-bin $(RECV_TEST_SRC)
	./test-receive-bin

.PHONY: all clean test-receive
