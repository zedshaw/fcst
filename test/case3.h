#ifndef sarray_h
#define sarray_h


typedef unsigned char uchar;

/* Constructs the suffix array. */
int sarray(int *a, int n);

/* Constructs one from a binary string. */
int bsarray(const uchar *b, int *a, int n);

/* Constructs the lcp. */
int *lcp(const int *a, const char *s, int n);

/* Don't use this. */
int lcpa(const int *a, const char *s, int *b, int n);

#endif

