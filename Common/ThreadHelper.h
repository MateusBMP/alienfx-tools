#pragma once
#ifdef _WIN32
#include <wtypes.h>
#else
#include "win_compat.h"
#endif

// Public API is preserved exactly across both platforms -- same constructor
// signature, same public members, same Stop()/Start() -- so none of this class's
// 8 GUI/mon call sites (CaptureHelper.cpp, GridHelper.cpp, SysMonHelper.cpp,
// WSAudioIn.h, alienfan-mon/MonHelper.cpp) need to change when they're ported in
// M7/M8. See Doc/linux_roadmap/03-platform-abstraction.md for the two behavioral
// quirks (do/while first-tick, manual-reset-never-reset restart) this
// implementation deliberately replicates rather than "fixes".
class ThreadHelper
{
public:
	void (*func)(LPVOID);
	HANDLE tEvent;
	HANDLE tHandle = NULL;
	int delay, priority;
	LPVOID param;
	ThreadHelper(LPVOID function, LPVOID param, int delay = 250, int prt = THREAD_PRIORITY_LOWEST);
	~ThreadHelper();
	void Stop();
	void Start();
};
;
