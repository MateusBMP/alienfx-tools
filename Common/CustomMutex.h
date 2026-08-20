#pragma once
#ifdef _WIN32
#include <wtypes.h>
//#include <synchapi.h>
#else
#include <shared_mutex>
#endif

class CustomMutex
{
private:
#ifdef _WIN32
	//CRITICAL_SECTION mHandle;
	SRWLOCK mHandle;
#else
	// std::shared_mutex maps directly onto SRWLOCK's semantics: shared (read) and
	// exclusive (write) locking, no recursion, no upgrade path -- matching how
	// this class's four call sites use it. See Doc/linux_roadmap/03-platform-
	// abstraction.md, "Threading and synchronization".
	std::shared_mutex mHandle;
#endif
public:
	CustomMutex();
	//~CustomMutex();
	void lockRead();
	void lockWrite();
	void unlockRead();
	void unlockWrite();
};