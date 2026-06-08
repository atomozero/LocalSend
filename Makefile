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

# Ricevente (L1 + L2 scoperta + L3 HTTPS).
RECV_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/net/MulticastAnnouncer.cpp \
	src/net/TlsContext.cpp \
	src/net/haiku/SocketHttpServer.cpp \
	src/server/FileSink.cpp \
	src/server/ReceiveSession.cpp \
	src/app/main_receive.cpp

SSL_LIBS = -lssl -lcrypto

# GUI (L4: applicazione nativa Haiku).
GUI_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/net/MulticastAnnouncer.cpp \
	src/net/TlsContext.cpp \
	src/net/haiku/SocketHttpClient.cpp \
	src/net/haiku/SocketHttpServer.cpp \
	src/client/FileSource.cpp \
	src/client/UploadSession.cpp \
	src/server/FileSink.cpp \
	src/server/ReceiveSession.cpp \
	src/app/main_gui.cpp

ADDON_SRC = src/addon/TrackerAddon.cpp

SEND_OBJ = $(SEND_SRC:.cpp=.o)
RECV_OBJ = $(RECV_SRC:.cpp=.o)
GUI_OBJ  = $(GUI_SRC:.cpp=.o)
SEND_BIN = localsend-send
RECV_BIN = localsend-receive
GUI_BIN  = LocalSend
ADDON_BIN = Send_with_LocalSend

all: $(SEND_BIN) $(RECV_BIN) $(GUI_BIN) addon

$(SEND_BIN): $(SEND_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(SEND_OBJ) $(LDLIBS) $(SSL_LIBS)

$(RECV_BIN): $(RECV_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(RECV_OBJ) $(LDLIBS) $(SSL_LIBS) -lbe

$(GUI_BIN): $(GUI_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(GUI_OBJ) $(LDLIBS) $(SSL_LIBS) -lbe -ltracker
	rc -o $(GUI_BIN).rsrc LocalSend.rdef
	xres -o $(GUI_BIN) $(GUI_BIN).rsrc
	mimeset -f $(GUI_BIN)

addon: $(ADDON_SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o "Send with LocalSend" $(ADDON_SRC) -lbe
	rc -o addon.rsrc LocalSend.rdef
	xres -o "Send with LocalSend" addon.rsrc

install-addon: addon
	mkdir -p "/boot/home/config/non-packaged/add-ons/Tracker"
	cp "Send with LocalSend" "/boot/home/config/non-packaged/add-ons/Tracker/"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(SEND_OBJ) $(RECV_OBJ) $(GUI_OBJ) $(SEND_BIN) $(RECV_BIN) $(GUI_BIN) $(GUI_BIN).rsrc "Send with LocalSend" addon.rsrc test-receive-bin

# Test host-side del lato ricevente (L1-prep): logica di protocollo pura, nessun
# socket, compilabile e runnabile ovunque (non solo Haiku). Niente -lnetwork.
RECV_TEST_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/server/ReceiveSession.cpp \
	tools/check/test_receive.cpp

test-receive:
	$(CXX) $(CXXFLAGS) -o test-receive-bin $(RECV_TEST_SRC)
	./test-receive-bin

.PHONY: all clean test-receive addon install-addon
