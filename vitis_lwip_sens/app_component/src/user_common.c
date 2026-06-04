#include "user_common.h"

#define RADIX_MIN 2
#define RADIX_MAX 36

static char const __numchr[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static char *strrev_local(char *buf)
{
    char c, *p = buf, *s = buf;
    while (*s != '\0') ++s;
    while (--s > p) {
        c = *p; *p = *s; *s = c;
        ++p;
    }
    return buf;
}

static char *ultoa_local(unsigned long val, char *buf, int radix)
{
    char *p = buf;
    if (RADIX_MIN <= radix && radix <= RADIX_MAX) {
        do {
            *p++ = __numchr[val % radix];
        } while (val /= radix);
    }
    *p = '\0';
    return strrev_local(buf);
}

static char *ltoa_local(long val, char *buf, int radix)
{
    char *p = buf;
    if (radix == 10 && val < 0) {
        val = -val;
        *p++ = '-';
    }
    ultoa_local((unsigned long)val, p, radix);
    return buf;
}

void *my_ltoa(long value, void *asc, unsigned short keta)
{
    unsigned char i, hi, lo;
    unsigned char *p = (unsigned char *)asc + keta;

    for (i = 0; i < 8; i++, value >>= 8) {
        if (keta == 0) break;
        keta--;
        p--;
        lo = (unsigned char)((unsigned char)value & 15);
        *p = (unsigned char)(lo >= 10 ? ('A' + lo - 10) : ('0' + lo));
        if (keta == 0) break;
        keta--;
        p--;
        hi = (unsigned char)((unsigned char)value >> 4);
        *p = (unsigned char)(hi >= 10 ? ('A' + hi - 10) : ('0' + hi));
    }
    return asc;
}

void *my_itoa(long value, void *asc, unsigned short keta)
{
    unsigned char temp[22];
    unsigned short size;
    unsigned char *s = asc;

    size = (unsigned short)strlen(ltoa_local(value, (char *)temp, 10));
    memset(s, 0x30, keta);
    strcpy((char *)&s[keta - size], (char *)temp);
    return asc;
}

void dlong_div(DLONG *s, unsigned long dst, DLONG *a)
{
    UW temp = 0;
    int i = 31;
    DLONG ss;

    if (dst & 0x80000000) {
        ss.lower  = s->lower >> 1;
        ss.lower |= s->upper << 31;
        ss.upper  = s->upper >> 1;
        dst >>= 1;
    } else {
        ss = *s;
    }

    a->upper = ss.upper / dst;
    temp = ss.upper % dst;
    a->lower = 0;
    while (i >= 0) {
        a->lower <<= 1;
        if (temp < dst) {
            temp = temp << 1 | ((ss.lower >> i) & 0x01L);
            i--;
        }
        if (temp >= dst) {
            a->lower |= temp / dst;
            temp %= dst;
        }
    }
}

void dlong_mul(unsigned long src, unsigned long dst, DLONG *a)
{
    UW temp;
    UW carry;

    a->upper = (src >> 16) * (dst >> 16);
    a->lower = (src & 0xffffL) * (dst & 0xffffL);
    carry = a->lower;

    temp = (src & 0xffffL) * (dst >> 16);
    a->upper += temp >> 16;
    a->lower += (temp & 0xffffL) << 16;
    if (carry > a->lower) a->upper++;
    carry = a->lower;

    temp = (src >> 16) * (dst & 0xffffL);
    a->upper += temp >> 16;
    a->lower += (temp & 0xffffL) << 16;
    if (carry > a->lower) a->upper++;
}
