#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#define SSIZE( m, s )   sizeof( ((m*)0)->s )
#define Bcnv( a )       ( (a + 1) / 2 )
#define _byte( a )      ( (a + 1) / 2 )
#define DIM( x )        ( sizeof(x)/sizeof(x[0]) )

#define GET_1_OF_WORD(a)    (((unsigned short)a & 0xFF00) >> 8)
#define GET_2_OF_WORD(a)    ((unsigned short)a & 0xFF)
#define GET_WORD(a)         ((((unsigned char *)a)[0] << 8) + ((unsigned char *)a)[1])
#define SET_WORD(a,b)       {((unsigned char *)a)[0]=GET_1_OF_WORD((unsigned short)b); ((unsigned char *)a)[1]=GET_2_OF_WORD((unsigned short)b);}

#define GET_1_OF_DWORD(a)   (((unsigned int)a & 0xFF000000) >> 24)
#define GET_2_OF_DWORD(a)   (((unsigned int)a & 0xFF0000) >> 16)
#define GET_3_OF_DWORD(a)   (((unsigned int)a & 0xFF00) >> 8)
#define GET_4_OF_DWORD(a)   ((unsigned int)a & 0xFF)
#define GET_DWORD(a)        ((((unsigned char *)a)[0] << 24) + (((unsigned char *)a)[1] << 16) + (((unsigned char *)a)[2] << 8) + ((unsigned char *)a)[3])
#define SET_DWORD(a,b)      {((unsigned char *)a)[0]=GET_1_OF_DWORD((unsigned int)b); ((unsigned char *)a)[1]=GET_2_OF_DWORD((unsigned int)b); ((unsigned char *)a)[2]=GET_3_OF_DWORD((unsigned int)b); ((unsigned char *)a)[3]=GET_4_OF_DWORD((unsigned int)b);}

typedef uint8_t BOOL;

#ifndef TRUE
#define TRUE  (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

typedef uint8_t   UB;
typedef int8_t    B;
typedef uint16_t  UH;
typedef int16_t   H;
typedef uint32_t  UW;
typedef int32_t   W;

typedef struct {
    uint32_t  upper;
    uint32_t  lower;
} DLONG;

void *my_itoa( long value, void *asc, unsigned short keta );
void *my_ltoa( long value, void *asc, unsigned short keta );
void dlong_div( DLONG *s, unsigned long dst, DLONG *a );
void dlong_mul( unsigned long src, unsigned long dst, DLONG *a );

#ifdef __cplusplus
}
#endif
