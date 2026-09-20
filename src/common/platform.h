/**
 * @file platform.h
 * @brief Cross-platform compatibility layer for Windows and POSIX.
 *
 * Include this header instead of platform-specific headers.
 * It detects the OS at compile time and provides unified macros.
 */

#ifndef TH_PLATFORM_H
#define TH_PLATFORM_H

#ifdef __cplusplus
extern "C" {
#endif

/* =====================================================================
 *  OS Detection
 * ===================================================================== */

#if defined(_WIN32) || defined(_WIN64)
    #define TH_PLATFORM_WINDOWS 1
    #define TH_PLATFORM_POSIX   0
#else
    #define TH_PLATFORM_WINDOWS 0
    #define TH_PLATFORM_POSIX   1
#endif

/* =====================================================================
 *  Windows-specific includes and defines
 * ===================================================================== */

#if TH_PLATFORM_WINDOWS

    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOGDI
        #define NOGDI
    #endif
    #ifndef NOUSER
        #define NOUSER
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif

    #include <windows.h>

    /* Undef Win32 symbols that conflict with raylib */
    #ifdef Rectangle
        #undef Rectangle
    #endif
    #ifdef CloseWindow
        #undef CloseWindow
    #endif
    #ifdef ShowCursor
        #undef ShowCursor
    #endif
    #ifdef LoadImage
        #undef LoadImage
    #endif
    #ifdef DrawText
        #undef DrawText
    #endif
    #ifdef DrawTextEx
        #undef DrawTextEx
    #endif

    #include <direct.h>     /* _getcwd, _mkdir */
    #include <io.h>         /* _access, _unlink */
    #include <process.h>    /* _getpid */
    #include <sys/stat.h>
    #include <sys/types.h>

    /* String functions */
    #define th_strcasecmp   _stricmp
    #define th_strncasecmp  _strnicmp

    /* File functions */
    #define th_getcwd       _getcwd
    #define th_access       _access
    #define th_unlink       _unlink
    #define th_popen        _popen
    #define th_pclose       _pclose
    #define th_getpid       _getpid

    /* access() mode flags */
    #ifndef F_OK
        #define F_OK 0
    #endif
    #ifndef R_OK
        #define R_OK 4
    #endif
    #ifndef W_OK
        #define W_OK 2
    #endif

    /* stat macros */
    #ifndef S_ISDIR
        #define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
    #endif
    #ifndef S_ISREG
        #define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
    #endif

    /* Path separator */
    #define TH_PATH_SEP     '\\'
    #define TH_PATH_SEP_STR "\\"

    /* Sleep */
    #define th_usleep(us) Sleep((us) / 1000)

    /**
     * @brief Case-insensitive substring search (Windows replacement for strcasestr).
     */
    static inline const char *th_strcasestr(const char *haystack, const char *needle) {
        if (!haystack || !needle) return NULL;
        if (!*needle) return haystack;
        size_t needle_len = 0;
        const char *n = needle;
        while (*n++) needle_len++;

        for (; *haystack; haystack++) {
            if (_strnicmp(haystack, needle, needle_len) == 0) {
                return haystack;
            }
        }
        return NULL;
    }

    /* Mutex abstraction */
    typedef CRITICAL_SECTION th_mutex_t;
    #define th_mutex_init(m)    InitializeCriticalSection(m)
    #define th_mutex_destroy(m) DeleteCriticalSection(m)
    #define th_mutex_lock(m)    EnterCriticalSection(m)
    #define th_mutex_unlock(m)  LeaveCriticalSection(m)

    /* Dynamic library loading */
    typedef HMODULE th_dl_handle_t;
    #define th_dlopen(name)       LoadLibraryA(name)
    #define th_dlsym(handle, sym) ((void *)GetProcAddress((handle), (sym)))
    #define th_dlclose(handle)    FreeLibrary(handle)

    /* Process handle type */
    typedef HANDLE th_process_handle_t;
    typedef HANDLE th_pipe_handle_t;
    #define TH_INVALID_PIPE INVALID_HANDLE_VALUE

#else /* POSIX */

    #include <unistd.h>
    #include <dirent.h>
    #include <dlfcn.h>
    #include <fcntl.h>
    #include <signal.h>
    #include <errno.h>
    #include <sys/stat.h>
    #include <sys/types.h>
    #include <sys/wait.h>
    #include <strings.h>
    #include <pthread.h>

    /* String functions */
    #define th_strcasecmp   strcasecmp
    #define th_strncasecmp  strncasecmp
    #define th_strcasestr   strcasestr

    /* File functions */
    #define th_getcwd       getcwd
    #define th_access       access
    #define th_unlink       unlink
    #define th_popen        popen
    #define th_pclose       pclose
    #define th_getpid       getpid

    /* Path separator */
    #define TH_PATH_SEP     '/'
    #define TH_PATH_SEP_STR "/"

    /* Sleep */
    #define th_usleep(us) usleep(us)

    /* Mutex abstraction */
    typedef pthread_mutex_t th_mutex_t;
    #define th_mutex_init(m)    pthread_mutex_init(m, NULL)
    #define th_mutex_destroy(m) pthread_mutex_destroy(m)
    #define th_mutex_lock(m)    pthread_mutex_lock(m)
    #define th_mutex_unlock(m)  pthread_mutex_unlock(m)

    /* Dynamic library loading */
    typedef void *th_dl_handle_t;
    #define th_dlopen(name)       dlopen(name, RTLD_LAZY)
    #define th_dlsym(handle, sym) dlsym(handle, sym)
    #define th_dlclose(handle)    dlclose(handle)

    /* Process handle type */
    typedef pid_t th_process_handle_t;
    typedef int   th_pipe_handle_t;
    #define TH_INVALID_PIPE (-1)

#endif /* TH_PLATFORM_WINDOWS */

/* =====================================================================
 *  Common path utilities
 * ===================================================================== */

/**
 * @brief Check if a character is a path separator (handles both / and \ on Windows).
 */
static inline int th_is_path_sep(char c) {
#if TH_PLATFORM_WINDOWS
    return (c == '/' || c == '\\');
#else
    return (c == '/');
#endif
}

/**
 * @brief Find the last path separator in a string.
 */
static inline const char *th_find_last_sep(const char *path) {
    if (!path) return NULL;
    const char *last = NULL;
    for (const char *p = path; *p; p++) {
        if (th_is_path_sep(*p)) last = p;
    }
    return last;
}

#ifdef __cplusplus
}
#endif

#endif /* TH_PLATFORM_H */
