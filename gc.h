#ifndef GC_H_
#define GC_H_

#include "gc-types.h"

#if defined(GC_BDW)
#include "bdw.h"
#elif defined(GC_SEMI)
#include "semi.h"
#elif defined(GC_WHIPPET)
#include "whippet.h"
#elif defined(GC_PARALLEL_WHIPPET)
#define GC_PARALLEL_TRACE 1
#include "whippet.h"
#else
#error unknown gc
#endif

#endif // GC_H_
