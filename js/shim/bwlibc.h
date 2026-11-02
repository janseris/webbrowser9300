/* Browser 9300: the small C library MuJS needs on the phone.
 *
 * EKA1 apps have no C library (and estlib would bring writable static data), so the libc names
 * MuJS uses are mapped onto bwc_ functions: js/bwlibc.c (C: strings, numbers, exact printf
 * formatting, setjmp) and src/jsplat.cpp (C++: heap, Math::, the clock and the time zone).
 * Every standard header in this folder just includes this one. */
#ifndef BW_LIBC_H
#define BW_LIBC_H
#include <stddef.h>
#include <stdarg.h>
#include <limits.h>
#include <float.h>

#ifdef __cplusplus
extern "C" {
#endif

/* stdio: formatting only, no files */
typedef struct BwFile { int dummy; } FILE;
#define stdout ((FILE*)0)
#define stderr ((FILE*)0)
#define EOF (-1)
int bwc_vsnprintf(char* b, size_t n, const char* f, va_list a);
int bwc_snprintf(char* b, size_t n, const char* f, ...);
int bwc_sprintf(char* b, const char* f, ...);
#define vsnprintf bwc_vsnprintf
#define snprintf bwc_snprintf
#define sprintf bwc_sprintf
#define printf(args...) ((void)0)
#define fprintf(args...) ((void)0)
#define putchar(c) ((void)0)
#define fputs(s, f) ((void)0)
#define fputc(c, f) ((void)0)
#define fopen(p, m) ((FILE*)0)
#define fclose(f) (0)
#define fseek(f, o, w) (-1)
#define ftell(f) (-1L)
#define fread(p, s, n, f) ((size_t)0)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

/* stdlib */
void* bwc_malloc(size_t n);
void* bwc_realloc(void* p, size_t n);
void bwc_free(void* p);
void bwc_abort(void);
long bwc_strtol(const char* s, char** e, int b);
#define malloc bwc_malloc
#define realloc bwc_realloc
#define free bwc_free
#define abort bwc_abort
#define strtol bwc_strtol
#define atoi(s) ((int)bwc_strtol((s), 0, 10))
#define abs(x) ((x) < 0 ? -(x) : (x))

/* string */
void* bwc_memcpy(void* d, const void* s, size_t n);
void* bwc_memmove(void* d, const void* s, size_t n);
void* bwc_memset(void* d, int c, size_t n);
int bwc_memcmp(const void* a, const void* b, size_t n);
size_t bwc_strlen(const char* s);
int bwc_strcmp(const char* a, const char* b);
int bwc_strncmp(const char* a, const char* b, size_t n);
char* bwc_strchr(const char* s, int c);
char* bwc_strrchr(const char* s, int c);
char* bwc_strstr(const char* h, const char* n);
char* bwc_strcpy(char* d, const char* s);
char* bwc_strcat(char* d, const char* s);
#define memcpy bwc_memcpy
#define memmove bwc_memmove
#define memset bwc_memset
#define memcmp bwc_memcmp
#define strlen bwc_strlen
#define strcmp bwc_strcmp
#define strncmp bwc_strncmp
#define strchr bwc_strchr
#define strrchr bwc_strrchr
#define strstr bwc_strstr
#define strcpy bwc_strcpy
#define strcat bwc_strcat
#define strerror(e) ("error")

/* errno: MuJS only writes it in js_strtod (patched out) and reads it for file errors */
#define errno 0
#define ERANGE 34
#define EINVAL 22

/* assert: off */
#define assert(x) ((void)0)

/* math: the IEEE helpers in C, the transcendental ones from Math:: */
int bwc_isnan(double x);
int bwc_isinf(double x);
int bwc_signbit(double x);
double bwc_trunc(double x);
double bwc_floor(double x);
double bwc_ceil(double x);
double bwc_fmod(double x, double y);
double bwc_fabs(double x);
double bwc_sqrt(double x);
double bwc_exp(double x);
double bwc_log(double x);
double bwc_pow(double x, double y);
double bwc_sin(double x);
double bwc_cos(double x);
double bwc_tan(double x);
double bwc_asin(double x);
double bwc_acos(double x);
double bwc_atan(double x);
double bwc_atan2(double y, double x);
#define isnan(x) bwc_isnan(x)
#define isinf(x) bwc_isinf(x)
#define isfinite(x) (!bwc_isnan(x) && !bwc_isinf(x))
#define signbit(x) bwc_signbit(x)
#define trunc bwc_trunc
#define floor bwc_floor
#define ceil bwc_ceil
#define fmod bwc_fmod
#define fabs bwc_fabs
#define sqrt bwc_sqrt
#define exp bwc_exp
#define log bwc_log
#define pow bwc_pow
#define sin bwc_sin
#define cos bwc_cos
#define tan bwc_tan
#define asin bwc_asin
#define acos bwc_acos
#define atan bwc_atan
#define atan2 bwc_atan2
#define HUGE_VAL (1.0/0.0)
#define INFINITY (1.0/0.0)
#define NAN (0.0/0.0)

/* setjmp: r4-r11, sp, lr (soft float: no FP registers) */
typedef int jmp_buf[12];
int bwc_setjmp(jmp_buf b);
void bwc_longjmp(jmp_buf b, int v);
#define setjmp bwc_setjmp
#define longjmp bwc_longjmp
#define _setjmp bwc_setjmp
#define _longjmp bwc_longjmp

/* time: only time() is used (jsdate.c's Now), the zone comes from js_platform_tza */
typedef long time_t;
time_t bwc_time(time_t* t);
#define time bwc_time

/* stdint */
typedef signed char int8_t; typedef unsigned char uint8_t;
typedef short int16_t; typedef unsigned short uint16_t;
typedef int int32_t; typedef unsigned int uint32_t;
typedef long long int64_t; typedef unsigned long long uint64_t;
typedef int intptr_t; typedef unsigned int uintptr_t;
#define INT32_MAX 2147483647
#define INT32_MIN (-2147483647-1)
#define UINT32_MAX 4294967295u
#define INT64_MAX 9223372036854775807LL
#define INT64_MIN (-9223372036854775807LL-1)
#define UINT64_MAX 18446744073709551615ULL
#define SIZE_MAX 4294967295u
#define INT16_MAX 32767
#define INT16_MIN (-32768)
#define UINT16_MAX 65535
#define INT8_MAX 127
#define INT8_MIN (-128)
#define UINT8_MAX 255

#ifdef __cplusplus
}
#endif
#endif
