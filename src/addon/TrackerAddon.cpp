// Tracker add-on: "Send with LocalSend"
// Appare nel menu contestuale di Tracker quando si selezionano file.
// Lancia l'app LocalSend passando i file come argomenti.

#include <Entry.h>
#include <Message.h>
#include <Path.h>
#include <Roster.h>

#include <cstring>
#include <vector>

static const char* kAppSignature = "application/x-vnd.LocalSend";


extern "C" void
process_refs(entry_ref, BMessage* refs, void*)
{
	// Raccogli i path dei file selezionati.
	std::vector<const char*> argv;
	std::vector<BPath> paths;

	entry_ref ref;
	for (int i = 0; refs->FindRef("refs", i, &ref) == B_OK; i++) {
		paths.emplace_back(&ref);
		if (paths.back().InitCheck() != B_OK)
			paths.pop_back();
	}
	if (paths.empty())
		return;

	// Prova a lanciare l'app passando i file come refs.
	// Se l'app e' gia' in esecuzione, riceve RefsReceived.
	BMessage launchMsg(B_REFS_RECEIVED);
	for (auto& p : paths) {
		entry_ref r;
		if (get_ref_for_path(p.Path(), &r) == B_OK)
			launchMsg.AddRef("refs", &r);
	}

	status_t err = be_roster->Launch(kAppSignature, &launchMsg);
	if (err == B_ALREADY_RUNNING) {
		// App gia' in esecuzione: invia i refs via messenger.
		BMessenger messenger(kAppSignature);
		if (messenger.IsValid())
			messenger.SendMessage(&launchMsg);
	}
}
