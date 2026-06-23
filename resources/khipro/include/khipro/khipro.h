#ifndef KHIPRO_H
#define KHIPRO_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) || defined(__CYGWIN__)
#  ifdef KHIPRO_BUILD_SHARED
#    define KHIPRO_API __declspec(dllexport)
#  elif defined(KHIPRO_USE_SHARED)
#    define KHIPRO_API __declspec(dllimport)
#  else
#    define KHIPRO_API
#  endif
#else
#  if defined(KHIPRO_BUILD_SHARED) && __GNUC__ >= 4
#    define KHIPRO_API __attribute__((visibility("default")))
#  else
#    define KHIPRO_API
#  endif
#endif

typedef enum {
  KHIPRO_VARIANT_DEFAULT = 0,
  KHIPRO_VARIANT_TOUCHSCREEN = 1
} khipro_variant;

typedef struct khipro_engine khipro_engine;

typedef struct khipro_result {
  const char* committed_delta;
  const char* preedit;
  const char* committed;
  int consumed;
} khipro_result;

KHIPRO_API khipro_engine* khipro_create(khipro_variant variant);
KHIPRO_API void khipro_destroy(khipro_engine* engine);

KHIPRO_API khipro_result khipro_feed_key(khipro_engine* engine, const char* utf8_key);
KHIPRO_API khipro_result khipro_backspace(khipro_engine* engine);
KHIPRO_API khipro_result khipro_commit(khipro_engine* engine);
KHIPRO_API void khipro_reset(khipro_engine* engine);

KHIPRO_API int khipro_convert(khipro_variant variant, const char* ascii, char* out, int out_cap);

KHIPRO_API const char* khipro_version(void);
KHIPRO_API const char* khipro_library_version(void);

#ifdef __cplusplus
}
#endif

#endif
