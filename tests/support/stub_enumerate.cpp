#include "stub_enumerate.h"

namespace {

// File-scope anonymous namespace, not nested inside `namespace alienfx_test` --
// visible unqualified from both the alienfx_test:: scripting API below and the
// alienfx_hid:: free functions at the bottom of this file, with no forwarding
// declarations needed. Same convention hid_backend_linux.cpp uses for the same
// reason (see that file's comment).

std::vector<alienfx_hid::HidNode> g_nodes;
bool g_openSucceeds = false;
std::vector<std::string> g_unopenablePaths;
unsigned g_openSuccessCount = 0;
uint16_t g_lastVid = 0, g_lastPid = 0;

} // namespace

namespace alienfx_test {

void SetEnumNodes(std::vector<alienfx_hid::HidNode> nodes) {
	g_nodes = std::move(nodes);
}

void SetOpenNodeSucceeds(bool succeeds) {
	g_openSucceeds = succeeds;
}

void SetUnopenablePaths(std::vector<std::string> paths) {
	g_unopenablePaths = std::move(paths);
}

void ResetEnumStub() {
	g_nodes.clear();
	g_openSucceeds = false;
	g_unopenablePaths.clear();
	g_openSuccessCount = 0;
	g_lastVid = g_lastPid = 0;
}

unsigned OpenNodeSuccessCount() { return g_openSuccessCount; }
uint16_t LastRegisteredVid() { return g_lastVid; }
uint16_t LastRegisteredPid() { return g_lastPid; }

} // namespace alienfx_test

// --- hid_enumerate.h's seam, delegating to the file-scope state above. ------------

namespace alienfx_hid {

bool EnumerateNodes(std::vector<HidNode>* out, uint16_t vidFilter, uint16_t pidFilter) {
	out->clear();
	for (const auto& node : g_nodes)
		if ((!vidFilter || node.vid == vidFilter) && (!pidFilter || node.pid == pidFilter))
			out->push_back(node);
	return true;
}

HANDLE OpenNode(const HidNode& node) {
	if (!g_openSucceeds)
		return nullptr;
	for (const auto& path : g_unopenablePaths)
		if (path == node.path)
			return nullptr;

	g_lastVid = node.vid;
	g_lastPid = node.pid;
	++g_openSuccessCount;
	// A fresh, distinct non-null HANDLE per call -- its value is otherwise
	// meaningless (this stub owns no real resource behind it). Each detection_test
	// case that opens more than one node needs handles that compare unequal, e.g.
	// to prove AlienFXInitialize's re-init path closes the *previous* handle rather
	// than losing track of it.
	static uintptr_t counter = 0;
	return reinterpret_cast<HANDLE>(++counter);
}

} // namespace alienfx_hid
