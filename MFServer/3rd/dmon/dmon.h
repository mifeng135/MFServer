#ifndef __DMON_H__
#define __DMON_H__

//
// Copyright 2023 Sepehr Taghdisian (septag@github). All rights reserved.
// License: https://github.com/septag/dmon#license-bsd-2-clause
//
//  Portable directory monitoring library
//  watches directories for file or directory changes.
//
// Usage:
//      Include this header anywhere you need the API. Link the translation unit that
//      compiles dmon.c (add dmon.c to your build once).
//
//      dmon_init():
//          Call this once at the start of your program.
//          This will start a low-priority monitoring thread
//      dmon_deinit():
//          Call this when your work with dmon is finished, usually on program terminate
//          This will free resources and stop the monitoring thread
//      dmon_watch:
//          Watch for directories
//          You can watch multiple directories by calling this function multiple times
//              rootdir: root directory to monitor
//              watch_cb: callback function to receive events.
//                        NOTE that this function is called from another thread, so you should
//                        beware of data races in your application when accessing data within this
//                        callback
//              flags: watch flags, see dmon_watch_flags_t
//              user_data: user pointer that is passed to callback function
//          Returns the Id of the watched directory after successful call, or returns Id=0 if error
//      dmon_unwatch:
//          Remove the directory from watch list
//
//      see test.c for the basic example
//
// Configuration:
//      You can customize some low-level functionality like malloc and logging by overriding macros:
//
//      DMON_MALLOC, DMON_FREE, DMON_REALLOC:
//          define these macros to override memory allocations
//          default is 'malloc', 'free' and 'realloc'
//      DMON_ASSERT:
//          define this to provide your own assert
//          default is 'assert'
//      DMON_LOG_ERROR:
//          define this to provide your own logging mechanism
//          default implementation logs to stdout and breaks the program
//      DMON_LOG_DEBUG
//          define this to provide your own extra debug logging mechanism
//          default implementation logs to stdout in DEBUG and does nothing in other builds
//      DMON_API_DECL, DMON_API_IMPL
//          define these to provide your own API declarations. (for example: static)
//          default is nothing (which is extern in C language )
//      DMON_MAX_PATH
//          Maximum size of path characters
//          default is 260 characters
//      DMON_MAX_WATCHES
//          Maximum number of watch directories
//          default is 64
//      DMON_SLEEP_INTERVAL
//          Number of milliseconds to pause between polling for file changes
//          default is 10 ms
//
// TODO:
//      - DMON_WATCHFLAGS_FOLLOW_SYMLINKS does not resolve files
//      - implement DMON_WATCHFLAGS_OUTOFSCOPE_LINKS
//
// History:
//      1.0.0       First version. working Win32/Linux backends
//      1.1.0       MacOS backend
//      1.1.1       Minor fixes, eliminate gcc/clang warnings with -Wall
//      1.1.2       Eliminate some win32 dead code
//      1.1.3       Fixed select not resetting causing high cpu usage on linux
//      1.2.1       inotify (linux) fixes and improvements, added extra functionality header for linux
//                  to manually add/remove directories manually to the watch handle, in case of large file sets
//      1.2.2       Name refactoring
//      1.3.0       Fixing bugs and proper watch/unwatch handles with freelists. Lower memory consumption, especially on Windows backend
//      1.3.1       Fix in MacOS event grouping
//      1.3.2       Fixes and improvements for Windows backend
//      1.3.3       Fixed thread sanitizer issues with Linux backend
//      1.3.4       Fixed thread sanitizer issues with MacOS backend
//      1.3.5       Got rid of volatile for quit variable
//      1.3.6       Fix deadlock when watch/unwatch API is called from the OnChange callback
//      1.3.7       Fix deadlock caused by constantly locking the mutex in the thread loop (recent change)
//      1.3.8       Fix a cpp compatiblity compiler bug after recent changes
//      1.3.9       Switch from deprecated FSEventStreamScheduleWithRunLoop to FSEventStreamSetDispatchQueue on macOS
//      1.3.10      Reduced memory usage for Linux backend from 4MB to 256KB
// 

#include <stdbool.h>
#include <stdint.h>

#ifndef DMON_API_DECL
#   define DMON_API_DECL
#endif

#ifndef DMON_API_IMPL
#   define DMON_API_IMPL
#endif

typedef struct { uint32_t id; } dmon_watch_id;

// Pass these flags to `dmon_watch`
typedef enum dmon_watch_flags_t {
    DMON_WATCHFLAGS_RECURSIVE = 0x1,            // monitor all child directories
    DMON_WATCHFLAGS_FOLLOW_SYMLINKS = 0x2,      // resolve symlinks (linux only)
    DMON_WATCHFLAGS_OUTOFSCOPE_LINKS = 0x4      // TODO: not implemented yet
} dmon_watch_flags;

// Action is what operation performed on the file. this value is provided by watch callback
typedef enum dmon_action_t {
    DMON_ACTION_CREATE = 1,
    DMON_ACTION_DELETE,
    DMON_ACTION_MODIFY,
    DMON_ACTION_MOVE
} dmon_action;

#ifdef __cplusplus
extern "C" {
#endif

DMON_API_DECL void dmon_init(void);
DMON_API_DECL void dmon_deinit(void);

DMON_API_DECL  dmon_watch_id dmon_watch(const char* rootdir,
                         void (*watch_cb)(dmon_watch_id watch_id, dmon_action action,
                                          const char* rootdir, const char* filepath,
                                          const char* oldfilepath, void* user),
                         uint32_t flags, void* user_data);
DMON_API_DECL void dmon_unwatch(dmon_watch_id id);

#ifdef __cplusplus
}
#endif

#endif    // __DMON_H__
