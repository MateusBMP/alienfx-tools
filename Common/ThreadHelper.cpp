#include "ThreadHelper.h"

#ifdef _WIN32

DWORD WINAPI ThreadFunc(LPVOID lpParam);

ThreadHelper::ThreadHelper(LPVOID function, LPVOID param, int delay, int prt) {
	this->delay = delay;
	priority = prt;
	func = (void (*)(LPVOID))function;
	this->param = param;
	tEvent = CreateEvent(NULL, true, false, NULL);
	Start();
}

ThreadHelper::~ThreadHelper()
{
	Stop();
	CloseHandle(tEvent);
}

void ThreadHelper::Stop()
{
	if (tHandle) {
		SetEvent(tEvent);
		WaitForSingleObject(tHandle, delay << 2);
		CloseHandle(tHandle);		
		tHandle = NULL;
	}
}

void ThreadHelper::Start()
{
	if (!tHandle) {
		tHandle = CreateThread(NULL, 0, ThreadFunc, this, 0, NULL);
		if (tHandle)
			SetThreadPriority(tHandle, priority);
	}
}

DWORD WINAPI ThreadFunc(LPVOID lpParam) {
	ThreadHelper* src = (ThreadHelper*)lpParam;
	do {
		src->func(src->param);
	} while (WaitForSingleObject(src->tEvent, src->delay) == WAIT_TIMEOUT);
	return 0;
}

#else  // !_WIN32

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace {

// Stand-in for a Win32 manual-reset event: CreateEvent(NULL, true, false, NULL)
// -- manual-reset, initially unsignaled. Nothing in this class ever calls
// ResetEvent, so once Set() fires, WaitTimedOut() never blocks again -- this is
// deliberate (see the ThreadHelper.h header comment and
// Doc/linux_roadmap/03-platform-abstraction.md): a Start() after a Stop() must
// reproduce the same "first tick fires, then the loop exits immediately"
// behavior the Windows implementation has always had.
struct LinuxEvent {
	std::mutex m;
	std::condition_variable cv;
	bool signaled = false;

	// Mirrors `WaitForSingleObject(tEvent, ms) == WAIT_TIMEOUT`: returns true if
	// the event was still NOT signaled when the wait ended (i.e. "keep
	// looping"), false if it was (or already had been) signaled.
	bool WaitTimedOut(int ms) {
		std::unique_lock<std::mutex> lk(m);
		if (signaled) return false;
		const bool becameSignaled =
			cv.wait_for(lk, std::chrono::milliseconds(ms), [&] { return signaled; });
		return !becameSignaled;
	}

	void Set() {
		std::lock_guard<std::mutex> lk(m);
		signaled = true;
		cv.notify_all();
	}
};

// Owns the running std::thread. A fresh instance is created per Start()/Stop()
// cycle (mirroring CreateThread/CloseHandle's per-cycle handle), while the
// LinuxEvent above is created once in the constructor and lives for the
// ThreadHelper's whole lifetime (mirroring the single CreateEvent call).
struct LinuxThreadState {
	std::thread thread;
};

// Best-effort only -- there is no portable C++ equivalent of
// THREAD_PRIORITY_LOWEST/NORMAL/etc (Doc/linux_roadmap/03-platform-abstraction.md,
// "Calling conventions and export macros"), and getting this wrong must never
// affect correctness, only how nicely the calling thread shares the CPU with the
// rest of the process (avoiding e.g. the ambient-capture thread starving the
// UI). Called from *inside* the new thread so setpriority() can target its own
// kernel TID directly -- std::thread::native_handle() is a pthread_t, which is
// not the same value setpriority(2) needs, and there is no portable way to
// convert one to the other from outside the thread.
void ApplyThreadPriorityBestEffort(int winPriority) {
#ifdef __linux__
	int niceVal = 0;
	if (winPriority <= THREAD_PRIORITY_LOWEST) niceVal = 10;
	else if (winPriority <= THREAD_PRIORITY_BELOW_NORMAL) niceVal = 5;
	if (niceVal != 0) {
		setpriority(PRIO_PROCESS, static_cast<id_t>(syscall(SYS_gettid)), niceVal);
		// Failure (e.g. no CAP_SYS_NICE headroom to lower niceness further) is
		// deliberately ignored -- this thread still runs, just without the hint.
	}
#else
	(void)winPriority;
#endif
}

}  // namespace

ThreadHelper::ThreadHelper(LPVOID function, LPVOID param, int delay, int prt) {
	this->delay = delay;
	priority = prt;
	func = (void (*)(LPVOID))function;
	this->param = param;
	tEvent = new LinuxEvent();
	Start();
}

ThreadHelper::~ThreadHelper()
{
	Stop();
	delete static_cast<LinuxEvent*>(tEvent);
}

void ThreadHelper::Stop()
{
	if (tHandle) {
		auto* ev = static_cast<LinuxEvent*>(tEvent);
		ev->Set();
		auto* th = static_cast<LinuxThreadState*>(tHandle);
		// Deliberate divergence from Windows: Stop() there waits `delay << 2` ms
		// then closes the handle regardless of whether the thread actually
		// exited, which leaks a still-running thread on timeout (CloseHandle
		// does not kill it). std::thread offers no such ambiguous middle ground
		// -- the destructor of a joinable std::thread that is never joined calls
		// std::terminate. Joining unconditionally is the only option that isn't
		// worse (a detach() would leave the callback running against a
		// ThreadHelper that may already be destroyed). See
		// Doc/linux_roadmap/03-platform-abstraction.md for this decision.
		if (th->thread.joinable()) {
			th->thread.join();
		}
		delete th;
		tHandle = NULL;
	}
}

void ThreadHelper::Start()
{
	if (!tHandle) {
		auto* ev = static_cast<LinuxEvent*>(tEvent);
		auto* th = new LinuxThreadState();
		const int prt = priority;
		th->thread = std::thread([this, ev, prt] {
			ApplyThreadPriorityBestEffort(prt);
			// do/while, matching the Windows ThreadFunc exactly: the callback
			// fires once *before* the first wait, not after. Some consumers
			// (e.g. CaptureHelper's first-frame paint) rely on this.
			do {
				func(param);
			} while (ev->WaitTimedOut(delay));
		});
		tHandle = th;
	}
}

#endif  // _WIN32

