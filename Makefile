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
	src/app/DeskbarItem.cpp \
	src/replicant/DesktopReplicant.cpp \
	src/app/main_gui.cpp

ADDON_SRC = src/addon/TrackerAddon.cpp
REPLICANT_SRC = src/replicant/DeskbarReplicant.cpp
DESKTOP_SRC = src/replicant/DesktopReplicant.cpp

SEND_OBJ = $(SEND_SRC:.cpp=.o)
RECV_OBJ = $(RECV_SRC:.cpp=.o)
GUI_OBJ  = $(GUI_SRC:.cpp=.o)
SEND_BIN = localsend-send
RECV_BIN = localsend-receive
GUI_BIN  = LocalSend
ADDON_BIN = Send_with_LocalSend
REPLICANT_BIN = LocalSendDeskbar
DESKTOP_BIN = LocalSendDesktop

all: $(SEND_BIN) $(RECV_BIN) $(GUI_BIN) addon $(REPLICANT_BIN) $(DESKTOP_BIN) catalogs

$(SEND_BIN): $(SEND_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(SEND_OBJ) $(LDLIBS) $(SSL_LIBS)

$(RECV_BIN): $(RECV_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(RECV_OBJ) $(LDLIBS) $(SSL_LIBS) -lbe

# -llocalestub: fornisce BLocaleRoster::GetCatalog() per-immagine, usato dai
# B_TRANSLATE (Locale Kit). Le traduzioni vivono in catalogs (target catalogs).
$(GUI_BIN): $(GUI_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(GUI_OBJ) $(LDLIBS) $(SSL_LIBS) -lbe -ltracker -lqrencode -llocalestub
	rc -o $(GUI_BIN).rsrc LocalSend.rdef
	xres -o $(GUI_BIN) $(GUI_BIN).rsrc
	mimeset -f $(GUI_BIN)

addon: $(ADDON_SRC)
	$(CXX) $(CXXFLAGS) -fPIC -shared -o "Send with LocalSend" $(ADDON_SRC) -lbe
	rc -o addon.rsrc LocalSend.rdef
	xres -o "Send with LocalSend" addon.rsrc

# Replicant Deskbar (.so caricato dalla Deskbar). L'icona HVIF e' presa dal
# MIME database a runtime. Il .so ha la SUA signature
# (application/x-vnd.LocalSend-Deskbar) cosi' che la Deskbar possa archiviare
# correttamente la view nel BShelf e ricaricare l'add-on al ripristino.
$(REPLICANT_BIN): $(REPLICANT_SRC) src/replicant/Replicant.rdef
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $(REPLICANT_SRC) -lbe -ltracker
	rc -o replicant.rsrc src/replicant/Replicant.rdef
	xres -o $@ replicant.rsrc
	mimeset -f $@

# Replicant desktop (.so caricato dallo shelf del desktop di Tracker).
# Stessa meccanica del Deskbar replicant: signature propria nel campo
# "add_on" dell'archive, cosi' Tracker ricarica l'add-on al ripristino.
# La stessa classe e' compilata anche nella GUI (GUI_SRC) per l'anteprima
# trascinabile nelle Impostazioni.
$(DESKTOP_BIN): $(DESKTOP_SRC) src/replicant/DesktopReplicant.rdef
	$(CXX) $(CXXFLAGS) -fPIC -shared -o $@ $(DESKTOP_SRC) -lbe
	rc -o desktop.rsrc src/replicant/DesktopReplicant.rdef
	xres -o $@ desktop.rsrc
	mimeset -f $@

install-addon: addon
	mkdir -p "/boot/home/config/non-packaged/add-ons/Tracker"
	cp "Send with LocalSend" "/boot/home/config/non-packaged/add-ons/Tracker/"
	addattr -t "'VICN'" -f ApeCar.hvif BEOS:ICON \
		"/boot/home/config/non-packaged/add-ons/Tracker/Send with LocalSend"

# --- Localizzazione (Locale Kit) -------------------------------------------
# La lingua segue le preferenze di sistema. Le stringhe traducibili sono i
# B_TRANSLATE in src/app/main_gui.cpp; le traduzioni vivono in locales/*.catkeys
# (editabili dai traduttori) e vengono compilate in .catalog installabili.
CATALOG_SIG   = x-vnd.LocalSend
CATALOG_LANGS = it ja zh es
CATALOG_DIR   = data/locale/catalogs/$(CATALOG_SIG)

# Rigenera SOLO il template inglese (locales/en.catkeys) dal sorgente: da
# eseguire quando si aggiungono o cambiano stringhe B_TRANSLATE. I traduttori
# allineano poi i locales/<lang>.catkeys (stesso fingerprint e stesse chiavi).
catkeys:
	$(CXX) $(CXXFLAGS) -DB_COLLECTING_CATKEYS -E -P src/app/main_gui.cpp \
		-o locales/_pre.cpp
	collectcatkeys -s $(CATALOG_SIG) -l en -o locales/en.catkeys locales/_pre.cpp
	rm -f locales/_pre.cpp

# Compila i .catalog dai .catkeys tradotti.
catalogs:
	mkdir -p $(CATALOG_DIR)
	for lang in $(CATALOG_LANGS); do \
		linkcatkeys -o $(CATALOG_DIR)/$$lang.catalog \
			-s $(CATALOG_SIG) -l $$lang locales/$$lang.catkeys; \
	done

# Installa i cataloghi nella dir locale dell'utente (per test senza pacchetto).
install-catalogs: catalogs
	mkdir -p "/boot/home/config/non-packaged/data/locale/catalogs/$(CATALOG_SIG)"
	cp $(CATALOG_DIR)/*.catalog \
		"/boot/home/config/non-packaged/data/locale/catalogs/$(CATALOG_SIG)/"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

# Ricompila automaticamente i .cpp quando cambiano gli header inclusi.
# I .d sono generati da -MMD -MP accanto ai rispettivi .o.
-include $(SEND_OBJ:.o=.d) $(RECV_OBJ:.o=.d) $(GUI_OBJ:.o=.d)

clean:
	rm -f $(SEND_OBJ) $(RECV_OBJ) $(GUI_OBJ) $(SEND_BIN) $(RECV_BIN) $(GUI_BIN) $(GUI_BIN).rsrc "Send with LocalSend" addon.rsrc $(REPLICANT_BIN) replicant.rsrc $(DESKTOP_BIN) desktop.rsrc test-receive-bin test-board-bin
	rm -f $(SEND_OBJ:.o=.d) $(RECV_OBJ:.o=.d) $(GUI_OBJ:.o=.d)
	rm -f $(CATALOG_DIR)/*.catalog

# Test host-side del lato ricevente (L1-prep): logica di protocollo pura, nessun
# socket, compilabile e runnabile ovunque (non solo Haiku). Niente -lnetwork.
RECV_TEST_SRC = $(PROTO) \
	src/net/HttpServerSupport.cpp \
	src/server/ReceiveSession.cpp \
	tools/check/test_receive.cpp

test-receive:
	$(CXX) $(CXXFLAGS) -o test-receive-bin $(RECV_TEST_SRC)
	./test-receive-bin

# Test host-side della bacheca (L6): estensione boardRev dell'annuncio e
# simmetria prepare-download pubblicante <-> osservatore. Solo protocollo,
# nessun socket: gira ovunque.
BOARD_TEST_SRC = $(PROTO) tools/check/test_board.cpp

test-board:
	$(CXX) $(CXXFLAGS) -o test-board-bin $(BOARD_TEST_SRC)
	./test-board-bin

# Tutti i test host-side.
check: test-receive test-board

.PHONY: all clean test-receive test-board check addon install-addon catkeys catalogs install-catalogs
