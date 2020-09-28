#pragma once

#pragma push_macro("max")
#undef max


#define TRACY_CALLSTACK 1

#ifdef TRACY_ENABLE

#define ALLOCATOR_NAMES
#endif


//#define TRACY_ON_DEMAND // This is defined to avoid deadlocking pc when tracy uses >16 gb of memory

#include "Tracy/Tracy.hpp"
#include "Tracy/TracyC.h"


#ifdef TRACY_ENABLE

#include "Tracy/client/TracyLock.hpp"
#include "Tracy/common/TracySystem.hpp"

#endif

#ifdef TRACY_ENABLE
#define TRACY_THREAD_NAME(name) tracy::SetThreadName(name)
#else
#define TRACY_THREAD_NAME(name)
#endif


#pragma pop_macro("max")



