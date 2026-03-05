#pragma once

#include <cstdio>
#include <cstdlib>

// ─── DAEDALUS_ASSERT ──────────────────────────────────────────────────────────
// Evaluates `cond`. If false, prints a diagnostic to stderr and aborts.
// Compiled away entirely in release builds (NDEBUG defined).
//
// Usage:
//   DAEDALUS_ASSERT(ptr != nullptr, "pointer must not be null");

#ifdef NDEBUG

#define DAEDALUS_ASSERT(cond, msg)  ((void)(cond))
#define DAEDALUS_ASSERT_FAIL(msg)   ((void)0)

#else

#define DAEDALUS_ASSERT(cond, msg)                                              \
    do                                                                          \
    {                                                                           \
        if (!(cond))                                                            \
        {                                                                       \
            std::fprintf(stderr,                                                \
                "[Daedalus] ASSERT FAILED: %s\n"                               \
                "  Condition : %s\n"                                            \
                "  File      : %s:%d\n",                                       \
                (msg), #cond, __FILE__, __LINE__);                             \
            std::abort();                                                       \
        }                                                                       \
    } while (false)

#define DAEDALUS_ASSERT_FAIL(msg)                                               \
    do                                                                          \
    {                                                                           \
        std::fprintf(stderr,                                                    \
            "[Daedalus] ASSERT FAILED: %s\n"                                   \
            "  File      : %s:%d\n",                                           \
            (msg), __FILE__, __LINE__);                                         \
        std::abort();                                                           \
    } while (false)

#endif // NDEBUG
