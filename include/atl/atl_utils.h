#ifndef ATL_UTILS_H_
#define ATL_UTILS_H_

// define a `ATL_API` macro to define visibility for API functions
#ifdef _WIN32
  #ifdef ATL_BUILD
    // building the ATL library, as opposed to including it
    #define ATL_API __declspec(dllexport)
  #else
    // using the ATL API
    #define ATL_API __declspec(dllimport)
  #endif
#else
  #if __GNUC__ >= 4
    #define ATL_API __attribute__ ((visibility ("default")))
  #else
    // older compilers - fall back to empty macro def
    #define ATL_API
  #endif
#endif

#endif // ATL_UTILS_H_



