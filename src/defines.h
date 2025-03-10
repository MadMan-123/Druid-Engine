
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>  // For size_t
#include <cstdlib>
#include <cassert>
// Unsigned int types.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

// Signed int types.
typedef signed char i8;
typedef signed short i16;
typedef signed int i32;
typedef signed long long i64;

// Floating point types
typedef float f32;
typedef double f64;

// Boolean types
typedef int b32;
typedef bool b8;

// Compile-time assertion macro
#define STATIC_ASSERT(COND, MSG) static_assert(COND, MSG)

// Ensure all types are of the correct size.
STATIC_ASSERT(sizeof(u8) == 1, "Expected u8 to be 1 byte.");
STATIC_ASSERT(sizeof(u16) == 2, "Expected u16 to be 2 bytes.");
STATIC_ASSERT(sizeof(u32) == 4, "Expected u32 to be 4 bytes.");
STATIC_ASSERT(sizeof(u64) == 8, "Expected u64 to be 8 bytes.");

STATIC_ASSERT(sizeof(i8) == 1, "Expected i8 to be 1 byte.");
STATIC_ASSERT(sizeof(i16) == 2, "Expected i16 to be 2 bytes.");
STATIC_ASSERT(sizeof(i32) == 4, "Expected i32 to be 4 bytes.");
STATIC_ASSERT(sizeof(i64) == 8, "Expected i64 to be 8 bytes.");

STATIC_ASSERT(sizeof(f32) == 4, "Expected f32 to be 4 bytes.");
STATIC_ASSERT(sizeof(f64) == 8, "Expected f64 to be 8 bytes.");



// Platform detection
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__)
    #define PLATFORM_WINDOWS 1
    #ifndef _WIN32 
        #error "64-bit is required on Windows!"
    #endif
#elif defined(__linux__) || defined(__gnu_linux__)
    // Linux OS
    #define PLATFORM_LINUX 1
    #if defined(__ANDROID__)
        #define PLATFORM_ANDROID 1
    #endif
#elif defined(__unix__)
    // Catch anything not caught by the above.
    #define PLATFORM_UNIX 1
#elif defined(_POSIX_VERSION)
    // Posix
    #define PLATFORM_POSIX 1
#elif __APPLE__
    // Apple platforms
    #define PLATFORM_APPLE 1
    #include <TargetConditionals.h>
#if TARGET_IPHONE_SIMULATOR
    // iOS Simulator
    #define PLATFORM_IOS 1
    #define PLATFORM_IOS_SIMULATOR 1
#elif TARGET_OS_IPHONE
    #define PLATFORM_IOS 1
    // iOS device
#elif TARGET_OS_MAC
// Other kinds of Mac OS
#else
    #error "Unknown Apple platform"
#endif
#else
#error "Unknown platform!"
#endif

#ifdef _WIN32
#ifdef DRUID_EXPORT
        #define DAPI __declspec(dllexport)
    #else
        #define DAPI __declspec(dllimport)
    #endif
#else
#ifdef DRUID_EXPORT
#define DAPI __attribute__((visibility("default")))
#else
#define DAPI
#endif
#endif
