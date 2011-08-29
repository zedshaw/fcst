#ifndef sarray_h
#define sarray_h


typedef unsigned char uchar;

int sarray(int *a, int n);
int bsarray(const uchar *b, int *a, int n);
int *lcp(const int *a, const char *s, int n);
int lcpa(const int *a, const char *s, int *b, int n);

#endif

