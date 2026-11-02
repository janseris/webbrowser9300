/* Browser 9300: stddef.h for MuJS on the phone (the app builds with -nostdinc) */
#ifndef BW_STDDEF_H
#define BW_STDDEF_H
typedef unsigned int size_t;
typedef int ptrdiff_t;
#ifndef __cplusplus
typedef unsigned short wchar_t;
#endif
#ifndef NULL
#define NULL ((void*)0)
#endif
#define offsetof(T, m) ((size_t)&((T*)0)->m)
#endif
