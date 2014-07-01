/*
Copyright 2014 Google Inc. All rights reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
*/

// This is a helper to support mlock on non page aligned data. It will simply
// lock the whole page covering the data.

#ifndef MLOCK_PAGE_H
#define MLOCK_PAGE_H

#include <limits.h>
#include <stdint.h>
#include <sys/mman.h>
#include <unistd.h>

#if defined(_POSIX_MEMLOCK_RANGE)
#ifndef PAGESIZE
#define PAGESIZE sysconf(_SC_PAGESIZE)
#endif
/*! \brief Lock the memory area given by a pointer and a size.
 *
 * The area is expanded to fill whole memory pages.
 */
#define MLOCK_PAGE(ptr, size)                                                  \
  mlock(                                                                       \
      (void*)((((uintptr_t)ptr) / (uintptr_t)PAGESIZE) * (uintptr_t)PAGESIZE), \
      (((uintptr_t)ptr + size - 1) / (uintptr_t)PAGESIZE -                     \
       ((uintptr_t)ptr) / (uintptr_t)PAGESIZE + 1) *                           \
          (uintptr_t)PAGESIZE)
#elif _POSIX_MEMLOCK > 0
#warning mlock() is unavailable. More memory will be locked than necessary.
/*! \brief Lock the memory area given by a pointer and a size.
 *
 * The area is expanded to fill whole memory pages.
 */
#define MLOCK_PAGE(ptr, size) mlockall(MCL_CURRENT)
#else
#warning mlock() and mlockall() are unavailable. Passwords might leak to swap.
/*! \brief Lock the memory area given by a pointer and a size.
 *
 * The area is expanded to fill whole memory pages.
 */
#define MLOCK_PAGE(ptr, size) 0
#endif

#endif
