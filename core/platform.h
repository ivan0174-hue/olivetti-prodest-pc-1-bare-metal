// Prodest PC1 Emulator - astrazione piattaforma
// Il core e' "freestanding": compila sia con Circle (bare metal) sia su Linux.
#pragma once

#ifdef __circle__
#include <circle/types.h>  // u8 u16 u32 u64 s8 s16 s32
#include <circle/util.h>   // memset, memcpy, memcmp...
#else
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint64_t u64;
#endif

#ifndef PRODEST_INLINE
#define PRODEST_INLINE inline __attribute__((always_inline))
#endif

// Funzioni di utilita' senza libc
static inline int pc_strlen(const char *s) { int n = 0; while (s && s[n]) n++; return n; }
static inline void pc_strncpy(char *d, const char *s, int max)
{
	int i = 0;
	if (s) for (; s[i] && i < max - 1; i++) d[i] = s[i];
	d[i] = 0;
}
static inline char pc_toupper(char c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static inline bool pc_endswith_ci(const char *s, const char *ext)
{
	int a = pc_strlen(s), b = pc_strlen(ext);
	if (b > a) return false;
	for (int i = 0; i < b; i++) if (pc_toupper(s[a - b + i]) != pc_toupper(ext[i])) return false;
	return true;
}
