#include "CustomMutex.h"

#ifdef _WIN32

CustomMutex::CustomMutex()
{
	//InitializeCriticalSection(&mHandle);
	InitializeSRWLock(&mHandle);
}

//CustomMutex::~CustomMutex()
//{
//	//DeleteCriticalSection(&mHandle);
//}



void CustomMutex::lockRead()
{
	AcquireSRWLockShared(&mHandle);
}

void CustomMutex::lockWrite() {
	//EnterCriticalSection(&mHandle);
	AcquireSRWLockExclusive(&mHandle);
}

void CustomMutex::unlockRead()
{
	ReleaseSRWLockShared(&mHandle);
}

void CustomMutex::unlockWrite() {
	//LeaveCriticalSection(&mHandle);
	ReleaseSRWLockExclusive(&mHandle);
}

#else

CustomMutex::CustomMutex()
{
	// std::shared_mutex is default-constructed unlocked -- nothing to do.
}

void CustomMutex::lockRead()
{
	mHandle.lock_shared();
}

void CustomMutex::lockWrite() {
	mHandle.lock();
}

void CustomMutex::unlockRead()
{
	mHandle.unlock_shared();
}

void CustomMutex::unlockWrite() {
	mHandle.unlock();
}

#endif