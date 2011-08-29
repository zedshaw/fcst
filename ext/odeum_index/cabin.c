/*************************************************************************************************
 * Implementation of Cabin
 *                                                      Copyright (C) 2000-2005 Mikio Hirabayashi
 * This file is part of QDBM, Quick Database Manager.
 * QDBM is free software; you can redistribute it and/or modify it under the terms of the GNU
 * Lesser General Public License as published by the Free Software Foundation; either version
 * 2.1 of the License or any later version.  QDBM is distributed in the hope that it will be
 * useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 * You should have received a copy of the GNU Lesser General Public License along with QDBM; if
 * not, write to the Free Software Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
 * 02111-1307 USA.
 *************************************************************************************************/


#include "cabin.h"
#include "myconf.h"

#define CB_GCUNIT      64                /* allocation unit size of a buffer in gc */
#define CB_SPBUFSIZ    32                /* size of a buffer for sprintf */
#define CB_SPMAXWIDTH  128               /* max width of a column for sprintf */
#define CB_DATUMUNIT   16                /* allocation unit size of a datum handle */
#define CB_LISTUNIT    64                /* allocation unit number of a list handle */
#define CB_MAPBNUM     4093              /* bucket size of a map handle */
#define CB_MAPPBNUM    251               /* bucket size of a petit map handle */
#define CB_MAPCSUNIT   64                /* small allocation unit size of map concatenation */
#define CB_MAPCBUNIT   256               /* big allocation unit size of map concatenation */
#define CB_MSGBUFSIZ   256               /* size of a buffer for log message */
#define CB_IOBUFSIZ    4096              /* size of an I/O buffer */
#define CB_FILEMODE    00644             /* permission of a creating file */
#define CB_ENCBUFSIZ   32                /* size of a buffer for encoding name */
#define CB_DATEBUFSIZ  64                /* size of a buffer for date expression */
#define CB_VNUMBUFSIZ  8                 /* size of a buffer for variable length number */


/* private function prototypes */
static void cbggchandler(void);
static void cbggckeeper(void *ptr, void (*func)(void *));
static void cbqsortsub(char *bp, int nmemb, int size, char *pswap, char *vswap,
                       int(*compar)(const void *, const void *));
static int cblistelemcmp(const void *a, const void *b);
static int cbfirsthash(const char *kbuf, int ksiz);
static int cbsecondhash(const char *kbuf, int ksiz);
static int cbkeycmp(const char *abuf, int asiz, const char *bbuf, int bsiz);
static int cbsetvnumbuf(char *buf, int num);
static int cbreadvnumbuf(const char *buf, int size, int *sp);



/*************************************************************************************************
 * public objects
 *************************************************************************************************/


/* Call back function for handling a fatal error. */
void (*cbfatalfunc)(const char *message) = NULL;


/* Allocate a region on memory. */
void *cbmalloc(size_t size){
  char *p;
  assert(size > 0 && size < INT_MAX);
  if(!(p = malloc(size))){
    if(cbfatalfunc){
      cbfatalfunc("out of memory");
    } else {
      cbmyfatal("out of memory");
    }
  }
  return p;
}


/* Re-allocate a region on memory. */
void *cbrealloc(void *ptr, size_t size){
  char *p;
  assert(size > 0);
  if(!(p = realloc(ptr, size))){
    if(cbfatalfunc){
      cbfatalfunc("out of memory");
    } else {
      cbmyfatal("out of memory");
    }
  }
  return p;
}


/* Duplicate a region on memory. */
char *cbmemdup(const char *ptr, int size){
  char *p;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  CB_MALLOC(p, size + 1);
  memcpy(p, ptr, size);
  p[size] = '\0';
  return p;
}


/* Register the pointer or handle of an object to the global garbage collector. */
void cbglobalgc(void *ptr, void (*func)(void *)){
  assert(ptr && func);
  cbggckeeper(ptr, func);
}


/* Exercise the global garbage collector explicitly. */
void cbggcsweep(void){
  cbggckeeper(NULL, NULL);
}


/* Sort an array using insert sort. */
void cbisort(void *base, int nmemb, int size, int(*compar)(const void *, const void *)){
  char *bp, *swap;
  int i, j;
  assert(base && nmemb >= 0 && size > 0 && compar);
  bp = (char *)base;
  CB_MALLOC(swap, size);
  for(i = 1; i < nmemb; i++){
    if(compar(bp + (i - 1) * size, bp + i * size) > 0){
      memcpy(swap, bp + i * size, size);
      for(j = i; j > 0; j--){
        if(compar(bp + (j - 1) * size, swap) < 0) break;
        memcpy(bp + j * size, bp + (j - 1) * size, size);
      }
      memcpy(bp + j * size, swap, size);
    }
  }
  free(swap);
}


/* Sort an array using shell sort. */
void cbssort(void *base, int nmemb, int size, int(*compar)(const void *, const void *)){
  char *bp, *swap;
  int step, bottom, i, j;
  assert(base && nmemb >= 0 && size > 0 && compar);
  bp = (char *)base;
  CB_MALLOC(swap, size);
  for(step = (nmemb - 1) / 3; step >= 0; step = (step - 1) / 3){
    if(step < 5) step = 1;
    for(bottom = 0; bottom < step; bottom++){
      for(i = bottom + step; i < nmemb; i += step){
        if(compar(bp + (i - step) * size, bp + i * size) > 0){
          memcpy(swap, bp + i * size, size);
          for(j = i; j > step - 1; j -= step){
            if(compar(bp + (j - step) * size, swap) < 0) break;
            memcpy(bp + j * size, bp + (j - step) * size, size);
          }
          memcpy(bp + j * size, swap, size);
        }
      }
    }
    if(step < 2) break;
  }
  free(swap);
}


/* Sort an array using heap sort. */
void cbhsort(void *base, int nmemb, int size, int(*compar)(const void *, const void *)){
  char *bp, *swap;
  int top, bottom, mybot, i;
  assert(base && nmemb >= 0 && size > 0 && compar);
  bp = (char *)base;
  nmemb--;
  bottom = nmemb / 2 +1;
  top = nmemb;
  CB_MALLOC(swap, size);
  while(bottom > 0){
    bottom--;
    mybot = bottom;
    i = 2 * mybot;
    while(i <= top) {
      if(i < top && compar(bp + (i + 1) * size, bp + i * size) > 0) i++;
      if(compar(bp + mybot * size, bp + i * size) >= 0) break;
      memcpy(swap, bp + mybot * size, size);
      memcpy(bp + mybot * size, bp + i * size, size);
      memcpy(bp + i * size, swap, size);
      mybot = i;
      i = 2 * mybot;
    }
  }
  while(top > 0){
    memcpy(swap, bp, size);
    memcpy(bp, bp + top * size, size);
    memcpy(bp + top * size, swap, size);
    top--;
    mybot = bottom;
    i = 2 * mybot;
    while(i <= top) {
      if(i < top && compar(bp + (i + 1) * size, bp + i * size) > 0) i++;
      if(compar(bp + mybot * size, bp + i * size) >= 0) break;
      memcpy(swap, bp + mybot * size, size);
      memcpy(bp + mybot * size, bp + i * size, size);
      memcpy(bp + i * size, swap, size);
      mybot = i;
      i = 2 * mybot;
    }
  }
  free(swap);
}


/* Sort an array using quick sort. */
void cbqsort(void *base, int nmemb, int size, int(*compar)(const void *, const void *)){
  char *pswap, *vswap;
  assert(base && nmemb >= 0 && size > 0 && compar);
  CB_MALLOC(pswap, size);
  CB_MALLOC(vswap, size);
  cbqsortsub(base, nmemb, size, pswap, vswap, compar);
  free(vswap);
  free(pswap);
}


/* Compare two strings with case insensitive evaluation. */
int cbstricmp(const char *astr, const char *bstr){
  int ac, bc;
  assert(astr && bstr);
  while(*astr != '\0'){
    if(*bstr == '\0') return 1;
    ac = (*astr >= 'A' && *astr <= 'Z') ? *astr + ('a' - 'A') : *(unsigned char *)astr;
    bc = (*bstr >= 'A' && *bstr <= 'Z') ? *bstr + ('a' - 'A') : *(unsigned char *)bstr;
    if(ac != bc) return ac - bc;
    astr++;
    bstr++;
  }
  return *bstr == '\0' ? 0 : -1;
}


/* Check whether a string begins with a key. */
int cbstrfwmatch(const char *str, const char *key){
  assert(str && key);
  while(*key != '\0'){
    if(*str != *key || *str == '\0') return FALSE;
    key++;
    str++;
  }
  return TRUE;
}


/* Check whether a string begins with a key, with case insensitive evaluation. */
int cbstrfwimatch(const char *str, const char *key){
  int sc, kc;
  assert(str && key);
  while(*key != '\0'){
    if(*str == '\0') return FALSE;
    sc = *str;
    if(sc >= 'A' && sc <= 'Z') sc += 'a' - 'A';
    kc = *key;
    if(kc >= 'A' && kc <= 'Z') kc += 'a' - 'A';
    if(sc != kc) return FALSE;
    key++;
    str++;
  }
  return TRUE;
}


/* Check whether a string ends with a key. */
int cbstrbwmatch(const char *str, const char *key){
  int slen, klen, i;
  assert(str && key);
  slen = strlen(str);
  klen = strlen(key);
  for(i = 1; i <= klen; i++){
    if(str[slen-i] != key[klen-i] || i > slen) return FALSE;
  }
  return TRUE;
}


/* Check whether a string ends with a key, with case insensitive evaluation. */
int cbstrbwimatch(const char *str, const char *key){
  int slen, klen, i, sc, kc;
  assert(str && key);
  slen = strlen(str);
  klen = strlen(key);
  for(i = 1; i <= klen; i++){
    sc = str[slen-i];
    if(sc >= 'A' && sc <= 'Z') sc += 'a' - 'A';
    kc = key[klen-i];
    if(kc >= 'A' && kc <= 'Z') kc += 'a' - 'A';
    if(sc != kc || i > slen) return FALSE;
  }
  return TRUE;
}


/* Convert the letters of a string to upper case. */
char *cbstrtoupper(char *str){
  int i;
  assert(str);
  for(i = 0; str[i] != '\0'; i++){
    if(str[i] >= 'a' && str[i] <= 'z') str[i] -= 'a' - 'A';
  }
  return str;
}


/* Convert the letters of a string to lower case. */
char *cbstrtolower(char *str){
  int i;
  assert(str);
  for(i = 0; str[i] != '\0'; i++){
    if(str[i] >= 'A' && str[i] <= 'Z') str[i] += 'a' - 'A';
  }
  return str;
}


/* Cut space characters at head or tail of a string. */
char *cbstrtrim(char *str){
  char *wp;
  int i, head;
  assert(str);
  wp = str;
  head = TRUE;
  for(i = 0; str[i] != '\0'; i++){
    if((str[i] >= 0x07 && str[i] <= 0x0d) || str[i] == 0x20){
      if(!head) *(wp++) = str[i];
    } else {
      *(wp++) = str[i];
      head = FALSE;
    }
  }
  *wp = '\0';
  while(wp > str && ((wp[-1] >= 0x07 && wp[-1] <= 0x0d) || wp[-1] == 0x20)){
    *(--wp) = '\0';
  }
  return str;
}


/* Squeeze space characters in a string and trim it. */
char *cbstrsqzspc(char *str){
  char *wp;
  int i, spc;
  assert(str);
  wp = str;
  spc = TRUE;
  for(i = 0; str[i] != '\0'; i++){
    if(str[i] > 0 && str[i] <= ' '){
      if(!spc) *(wp++) = str[i];
      spc = TRUE;
    } else {
      *(wp++) = str[i];
      spc = FALSE;
    }
  }
  *wp = '\0';
  for(wp--; wp >= str; wp--){
    if(*wp > 0 && *wp <= ' '){
      *wp = '\0';
    } else {
      break;
    }
  }
  return str;
}


/* Get a datum handle. */
CBDATUM *cbdatumopen(const char *ptr, int size){
  CBDATUM *datum;
  CB_MALLOC(datum, sizeof(*datum));
  CB_MALLOC(datum->dptr, CB_DATUMUNIT);
  datum->dptr[0] = '\0';
  datum->dsize = 0;
  datum->asize = CB_DATUMUNIT;
  if(ptr) cbdatumcat(datum, ptr, size);
  return datum;
}


/* Copy a datum. */
CBDATUM *cbdatumdup(const CBDATUM *datum){
  assert(datum);
  return cbdatumopen(datum->dptr, datum->dsize);
}


/* Free a datum handle. */
void cbdatumclose(CBDATUM *datum){
  assert(datum);
  free(datum->dptr);
  free(datum);
}


/* Concatenate a datum and a region. */
void cbdatumcat(CBDATUM *datum, const char *ptr, int size){
  assert(datum && ptr);
  if(size < 0) size = strlen(ptr);
  if(datum->dsize + size >= datum->asize){
    datum->asize = datum->asize * 2 + size + 1;
    CB_REALLOC(datum->dptr, datum->asize);
  }
  memmove(datum->dptr + datum->dsize, ptr, size);
  datum->dsize += size;
  datum->dptr[datum->dsize] = '\0';
}


/* Get the pointer of the region of a datum. */
const char *cbdatumptr(const CBDATUM *datum){
  assert(datum);
  return datum->dptr;
}


/* Get the size of the region of a datum. */
int cbdatumsize(const CBDATUM *datum){
  assert(datum);
  return datum->dsize;
}


/* Set the size of the region of a datum. */
void cbdatumsetsize(CBDATUM *datum, int size){
  assert(datum && size >= 0);
  if(size <= datum->dsize){
    datum->dsize = size;
    datum->dptr[size] = '\0';
  } else {
    if(size >= datum->asize){
      datum->asize = datum->asize * 2 + size + 1;
      CB_REALLOC(datum->dptr, datum->asize);
    }
    memset(datum->dptr + datum->dsize, 0, (size - datum->dsize) + 1);
    datum->dsize = size;
  }
}


/* Convert a datum to an allocated region. */
char *cbdatumtomalloc(CBDATUM *datum, int *sp){
  char *ptr;
  assert(datum);
  ptr = datum->dptr;
  if(sp) *sp = datum->dsize;
  free(datum);
  return ptr;
}


/* Get a list handle. */
CBLIST *cblistopen(void){
  CBLIST *list;
  CB_MALLOC(list, sizeof(*list));
  list->anum = CB_LISTUNIT;
  CB_MALLOC(list->array, sizeof(list->array[0]) * list->anum);
  list->start = 0;
  list->num = 0;
  return list;
}


/* Copy a list. */
CBLIST *cblistdup(const CBLIST *list){
  CBLIST *newlist;
  int i, size;
  const char *val;
  assert(list);
  newlist = cblistopen();
  for(i = 0; i < cblistnum(list); i++){
    val = cblistval(list, i, &size);
    cblistpush(newlist, val, size);
  }
  return newlist;
}


/* Close a list handle. */
void cblistclose(CBLIST *list){
  int i, end;
  assert(list);
  end = list->start + list->num;
  for(i = list->start; i < end; i++){
    free(list->array[i].dptr);
  }
  free(list->array);
  free(list);
}


/* Get the number of elements of a list. */
int cblistnum(const CBLIST *list){
  assert(list);
  return list->num;
}


/* Get the pointer to the region of an element. */
const char *cblistval(const CBLIST *list, int index, int *sp){
  assert(list && index >= 0);
  if(index >= list->num) return NULL;
  index += list->start;
  if(sp) *sp = list->array[index].dsize;
  return list->array[index].dptr;
}


/* Add an element at the end of a list. */
void cblistpush(CBLIST *list, const char *ptr, int size){
  int index;
  assert(list && ptr);
  if(size < 0) size = strlen(ptr);
  index = list->start + list->num;
  if(index >= list->anum){
    list->anum *= 2;
    CB_REALLOC(list->array, list->anum * sizeof(list->array[0]));
  }
  CB_MALLOC(list->array[index].dptr, (size < CB_DATUMUNIT ? CB_DATUMUNIT : size) + 1);
  memcpy(list->array[index].dptr, ptr, size);
  list->array[index].dptr[size] = '\0';
  list->array[index].dsize = size;
  list->num++;
}


/* Remove an element of the end of a list. */
char *cblistpop(CBLIST *list, int *sp){
  int index;
  assert(list);
  if(list->num < 1) return NULL;
  index = list->start + list->num - 1;
  list->num--;
  if(sp) *sp = list->array[index].dsize;
  return list->array[index].dptr;
}


/* Add an element at the top of a list. */
void cblistunshift(CBLIST *list, const char *ptr, int size){
  int index;
  assert(list && ptr);
  if(size < 0) size = strlen(ptr);
  if(list->start < 1){
    if(list->start + list->num >= list->anum){
      list->anum *= 2;
      CB_REALLOC(list->array, list->anum * sizeof(list->array[0]));
    }
    list->start = list->anum - list->num;
    memmove(list->array + list->start, list->array, list->num * sizeof(list->array[0]));
  }
  index = list->start - 1;
  CB_MALLOC(list->array[index].dptr, (size < CB_DATUMUNIT ? CB_DATUMUNIT : size) + 1);
  memcpy(list->array[index].dptr, ptr, size);
  list->array[index].dptr[size] = '\0';
  list->array[index].dsize = size;
  list->start--;
  list->num++;
}


/* Remove an element of the top of a list. */
char *cblistshift(CBLIST *list, int *sp){
  int index;
  assert(list);
  if(list->num < 1) return NULL;
  index = list->start;
  list->start++;
  list->num--;
  if(sp) *sp = list->array[index].dsize;
  return list->array[index].dptr;
}


/* Add an element at the specified location of a list. */
void cblistinsert(CBLIST *list, int index, const char *ptr, int size){
  assert(list && index >= 0);
  if(index > list->num) return;
  if(size < 0) size = strlen(ptr);
  index += list->start;
  if(list->start + list->num >= list->anum){
    list->anum *= 2;
    CB_REALLOC(list->array, list->anum * sizeof(list->array[0]));
  }
  memmove(list->array + index + 1, list->array + index,
          sizeof(list->array[0]) * (list->start + list->num - index));
  list->array[index].dptr = cbmemdup(ptr, size);
  list->array[index].dsize = size;
  list->num++;
}


/* Remove an element at the specified location of a list. */
char *cblistremove(CBLIST *list, int index, int *sp){
  char *dptr;
  assert(list && index >= 0);
  if(index >= list->num) return NULL;
  index += list->start;
  dptr = list->array[index].dptr;
  if(sp) *sp = list->array[index].dsize;
  list->num--;
  memmove(list->array + index, list->array + index + 1,
          sizeof(list->array[0]) * (list->start + list->num - index));
  return dptr;
}


/* Overwrite an element at the specified location of a list. */
void cblistover(CBLIST *list, int index, const char *ptr, int size){
  assert(list && index >= 0);
  if(index >= list->num) return;
  if(size < 0) size = strlen(ptr);
  index += list->start;
  if(size > list->array[index].dsize)
    CB_REALLOC(list->array[index].dptr, size + 1);
  memcpy(list->array[index].dptr, ptr, size);
  list->array[index].dsize = size;
  list->array[index].dptr[size] = '\0';
}


/* Sort elements of a list in lexical order. */
void cblistsort(CBLIST *list){
  assert(list);
  cbqsort(list->array + list->start, list->num, sizeof(list->array[0]), cblistelemcmp);
}


/* Search a list for an element using liner search. */
int cblistlsearch(const CBLIST *list, const char *ptr, int size){
  int i, end;
  assert(list && ptr);
  if(size < 0) size = strlen(ptr);
  end = list->start + list->num;
  for(i = list->start; i < end; i++){
    if(list->array[i].dsize == size && !memcmp(list->array[i].dptr, ptr, size))
      return i - list->start;
  }
  return -1;
}


/* Search a list for an element using binary search. */
int cblistbsearch(const CBLIST *list, const char *ptr, int size){
  CBLISTDATUM key, *res;
  assert(list && ptr);
  if(size < 0) size = strlen(ptr);
  key.dptr = cbmemdup(ptr, size);
  key.dsize = size;
  res = bsearch(&key, list->array + list->start, list->num, sizeof(list->array[0]), cblistelemcmp);
  free(key.dptr);
  return res ? (res - list->array - list->start) : -1;
}


/* Serialize a list into a byte array. */
char *cblistdump(const CBLIST *list, int *sp){
  char *buf, vnumbuf[CB_VNUMBUFSIZ];
  const char *vbuf;
  int i, bsiz, vnumsiz, ln, vsiz;
  assert(list && sp);
  ln = cblistnum(list);
  vnumsiz = cbsetvnumbuf(vnumbuf, ln);
  CB_MALLOC(buf, vnumsiz + 1);
  memcpy(buf, vnumbuf, vnumsiz);
  bsiz = vnumsiz;
  for(i = 0; i < ln; i++){
    vbuf = cblistval(list, i, &vsiz);
    vnumsiz = cbsetvnumbuf(vnumbuf, vsiz);
    CB_REALLOC(buf, bsiz + vnumsiz + vsiz + 1);
    memcpy(buf + bsiz, vnumbuf, vnumsiz);
    bsiz += vnumsiz;
    memcpy(buf + bsiz, vbuf, vsiz);
    bsiz += vsiz;
  }
  *sp = bsiz;
  return buf;
}


/* Redintegrate a serialized list. */
CBLIST *cblistload(const char *ptr, int size){
  CBLIST *list;
  const char *rp;
  int i, step, ln, vsiz;
  assert(ptr && size >= 0);
  list = cblistopen();
  rp = ptr;
  ln = cbreadvnumbuf(rp, size, &step);
  rp += step;
  size -= step;
  if(ln > size) return list;
  for(i = 0; i < ln; i++){
    if(size < 1) break;
    vsiz = cbreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(vsiz > size) break;
    cblistpush(list, rp, vsiz);
    rp += vsiz;
  }
  return list;
}


/* Get a map handle. */
CBMAP *cbmapopen(void){
  return cbmapopenex(CB_MAPBNUM);
}


/* Copy a map. */
CBMAP *cbmapdup(CBMAP *map){
  CBMAP *newmap;
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  assert(map);
  cbmapiterinit(map);
  newmap = map->rnum > CB_MAPPBNUM ? cbmapopen() : cbmapopenex(CB_MAPPBNUM);
  while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
    vbuf = cbmapget(map, kbuf, ksiz, &vsiz);
    cbmapput(newmap, kbuf, ksiz, vbuf, vsiz, FALSE);
  }
  cbmapiterinit(map);
  return newmap;
}


/* Close a map handle. */
void cbmapclose(CBMAP *map){
  CBMAPDATUM *datum, *next;
  datum = map->first;
  while(datum){
    next = (CBMAPDATUM *)(datum->next);
    free(datum->kbuf);
    free(datum->vbuf);
    free(datum);
    datum = next;
  }
  free(map->buckets);
  free(map);
}


/* Store a record. */
int cbmapput(CBMAP *map, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int over){
  CBMAPDATUM *datum, **entp;
  int bidx, hash, kcmp;
  assert(map && kbuf && vbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  bidx = cbfirsthash(kbuf, ksiz) % map->bnum;
  datum = map->buckets[bidx];
  entp = map->buckets + bidx;
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      entp = (CBMAPDATUM **)&(datum->left);
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      entp = (CBMAPDATUM **)&(datum->right);
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        entp = (CBMAPDATUM **)&(datum->left);
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        entp = (CBMAPDATUM **)&(datum->right);
        datum = (CBMAPDATUM *)datum->right;
      } else {
        if(!over) return FALSE;
        if(vsiz > datum->vsiz){
          free(datum->vbuf);
          datum->vbuf = cbmemdup(vbuf, vsiz);
        } else {
          memcpy(datum->vbuf, vbuf, vsiz);
          datum->vbuf[vsiz] = '\0';
        }
        datum->vsiz = vsiz;
        return TRUE;
      }
    }
  }
  CB_MALLOC(datum, sizeof(*datum));
  datum->kbuf = cbmemdup(kbuf, ksiz);
  datum->ksiz = ksiz;
  datum->vbuf = cbmemdup(vbuf, vsiz);
  datum->vsiz = vsiz;
  datum->hash = hash;
  datum->left = NULL;
  datum->right = NULL;
  datum->prev = (char *)map->last;
  datum->next = NULL;
  *entp = datum;
  if(!map->first) map->first = datum;
  if(map->last) map->last->next = (char *)datum;
  map->last = datum;
  map->rnum++;
  return TRUE;
}


/* Concatenate a value at the end of the value of the existing record. */
void cbmapputcat(CBMAP *map, const char *kbuf, int ksiz, const char *vbuf, int vsiz){
  CBMAPDATUM *datum, **entp;
  int bidx, hash, kcmp, asiz, unit;
  assert(map && kbuf && vbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  if(vsiz < 0) vsiz = strlen(vbuf);
  bidx = cbfirsthash(kbuf, ksiz) % map->bnum;
  datum = map->buckets[bidx];
  entp = map->buckets + bidx;
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      entp = (CBMAPDATUM **)&(datum->left);
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      entp = (CBMAPDATUM **)&(datum->right);
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        entp = (CBMAPDATUM **)&(datum->left);
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        entp = (CBMAPDATUM **)&(datum->right);
        datum = (CBMAPDATUM *)datum->right;
      } else {
        asiz = datum->vsiz + vsiz;
        unit = asiz <= CB_MAPCSUNIT ? CB_MAPCSUNIT : CB_MAPCBUNIT;
        asiz = (asiz - 1) + unit - (asiz - 1) % unit;
        CB_REALLOC(datum->vbuf, asiz + 1);
        memcpy(datum->vbuf + datum->vsiz, vbuf, vsiz);
        *(datum->vbuf + datum->vsiz + vsiz) = '\0';
        datum->vsiz += vsiz;
        return;
      }
    }
  }
  unit = vsiz <= CB_MAPCSUNIT ? CB_MAPCSUNIT : CB_MAPCBUNIT;
  asiz = (vsiz - 1) + unit - (vsiz - 1) % unit;
  CB_MALLOC(datum, sizeof(*datum));
  datum->kbuf = cbmemdup(kbuf, ksiz);
  datum->ksiz = ksiz;
  CB_MALLOC(datum->vbuf, asiz + 1);
  memcpy(datum->vbuf, vbuf, vsiz);
  *(datum->vbuf + vsiz) = '\0';
  datum->vsiz = vsiz;
  datum->hash = hash;
  datum->left = NULL;
  datum->right = NULL;
  datum->prev = (char *)map->last;
  datum->next = NULL;
  *entp = datum;
  if(!map->first) map->first = datum;
  if(map->last) map->last->next = (char *)datum;
  map->last = datum;
  map->rnum++;
}


/* Delete a record. */
int cbmapout(CBMAP *map, const char *kbuf, int ksiz){
  CBMAPDATUM *datum, **entp, *tmp;
  int bidx, hash, kcmp;
  assert(map && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  bidx = cbfirsthash(kbuf, ksiz) % map->bnum;
  datum = map->buckets[bidx];
  entp = map->buckets + bidx;
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      entp = (CBMAPDATUM **)&(datum->left);
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      entp = (CBMAPDATUM **)&(datum->right);
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        entp = (CBMAPDATUM **)&(datum->left);
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        entp = (CBMAPDATUM **)&(datum->right);
        datum = (CBMAPDATUM *)datum->right;
      } else {
        if(datum->prev) ((CBMAPDATUM *)(datum->prev))->next = datum->next;
        if(datum->next) ((CBMAPDATUM *)(datum->next))->prev = datum->prev;
        if(datum == map->first) map->first = (CBMAPDATUM *)datum->next;
        if(datum == map->last) map->last = (CBMAPDATUM *)datum->prev;
        if(datum->left && !datum->right){
          *entp = (CBMAPDATUM *)datum->left;
        } else if(!datum->left && datum->right){
          *entp = (CBMAPDATUM *)datum->right;
        } else if(!datum->left && !datum->left){
          *entp = NULL;
        } else {
          *entp = (CBMAPDATUM *)datum->left;
          tmp = *entp;
          while(TRUE){
            if(hash > tmp->hash){
              if(tmp->left){
                tmp = (CBMAPDATUM *)tmp->left;
              } else {
                tmp->left = datum->right;
                break;
              }
            } else if(hash < tmp->hash){
              if(tmp->right){
                tmp = (CBMAPDATUM *)tmp->right;
              } else {
                tmp->right = datum->right;
                break;
              }
            } else {
              kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
              if(kcmp < 0){
                if(tmp->left){
                  tmp = (CBMAPDATUM *)tmp->left;
                } else {
                  tmp->left = datum->right;
                  break;
                }
              } else {
                if(tmp->right){
                  tmp = (CBMAPDATUM *)tmp->right;
                } else {
                  tmp->right = datum->right;
                  break;
                }
              }
            }
          }
        }
        free(datum->kbuf);
        free(datum->vbuf);
        free(datum);
        map->rnum--;
        return TRUE;
      }
    }
  }
  return FALSE;
}


/* Retrieve a record. */
const char *cbmapget(const CBMAP *map, const char *kbuf, int ksiz, int *sp){
  CBMAPDATUM *datum;
  int hash, kcmp;
  assert(map && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  datum = map->buckets[cbfirsthash(kbuf, ksiz)%map->bnum];
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        datum = (CBMAPDATUM *)datum->right;
      } else {
        if(sp) *sp = datum->vsiz;
        return datum->vbuf;
      }
    }
  }
  return NULL;
}


/* Move a record to the edge. */
int cbmapmove(CBMAP *map, const char *kbuf, int ksiz, int head){
  CBMAPDATUM *datum;
  int hash, kcmp;
  assert(map && kbuf);
  if(ksiz < 0) ksiz = strlen(kbuf);
  datum = map->buckets[cbfirsthash(kbuf, ksiz)%map->bnum];
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        datum = (CBMAPDATUM *)datum->right;
      } else {
        if(head){
          if(map->first == datum) return TRUE;
          if(map->last == datum) map->last = (CBMAPDATUM *)(datum->prev);
          if(datum->prev) ((CBMAPDATUM *)(datum->prev))->next = datum->next;
          if(datum->next) ((CBMAPDATUM *)(datum->next))->prev = datum->prev;
          datum->prev = NULL;
          datum->next = (char *)(map->first);
          map->first->prev = (char *)datum;
          map->first = datum;
        } else {
          if(map->last == datum) return TRUE;
          if(map->first == datum) map->first = (CBMAPDATUM *)(datum->next);
          if(datum->prev) ((CBMAPDATUM *)(datum->prev))->next = datum->next;
          if(datum->next) ((CBMAPDATUM *)(datum->next))->prev = datum->prev;
          datum->prev = (char *)(map->last);
          datum->next = NULL;
          map->last->next = (char *)datum;
          map->last = datum;
        }
        return TRUE;
      }
    }
  }
  return FALSE;
}


/* Initialize the iterator of a map handle. */
void cbmapiterinit(CBMAP *map){
  assert(map);
  map->cur = map->first;
}


/* Get the next key of the iterator. */
const char *cbmapiternext(CBMAP *map, int *sp){
  CBMAPDATUM *datum;
  assert(map);
  if(!map->cur) return NULL;
  datum = map->cur;
  map->cur = (CBMAPDATUM *)datum->next;
  if(sp) *sp = datum->ksiz;
  return datum->kbuf;
}


/* Get the number of the records stored in a map. */
int cbmaprnum(const CBMAP *map){
  assert(map);
  return map->rnum;
}


/* Get the list handle contains all keys in a map. */
CBLIST *cbmapkeys(CBMAP *map){
  CBLIST *list;
  const char *kbuf;
  int ksiz;
  assert(map);
  list = cblistopen();
  cbmapiterinit(map);
  while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
    cblistpush(list, kbuf, ksiz);
  }
  return list;
}


/* Get the list handle contains all values in a map. */
CBLIST *cbmapvals(CBMAP *map){
  CBLIST *list;
  const char *kbuf, *vbuf;
  int ksiz, vsiz;
  assert(map);
  list = cblistopen();
  cbmapiterinit(map);
  while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
    vbuf = cbmapget(map, kbuf, ksiz, &vsiz);
    cblistpush(list, vbuf, vsiz);
  }
  return list;
}


/* Serialize a map into a byte array. */
char *cbmapdump(CBMAP *map, int *sp){
  char *buf, vnumbuf[CB_VNUMBUFSIZ];
  const char *kbuf, *vbuf;
  int bsiz, vnumsiz, rn, ksiz, vsiz;
  assert(map && sp);
  rn = cbmaprnum(map);
  vnumsiz = cbsetvnumbuf(vnumbuf, rn);
  CB_MALLOC(buf, vnumsiz + 1);
  memcpy(buf, vnumbuf, vnumsiz);
  bsiz = vnumsiz;
  cbmapiterinit(map);
  while((kbuf = cbmapiternext(map, &ksiz)) != NULL){
    vbuf = cbmapget(map, kbuf, ksiz, &vsiz);
    vnumsiz = cbsetvnumbuf(vnumbuf, ksiz);
    CB_REALLOC(buf, bsiz + vnumsiz + ksiz + 1);
    memcpy(buf + bsiz, vnumbuf, vnumsiz);
    bsiz += vnumsiz;
    memcpy(buf + bsiz, kbuf, ksiz);
    bsiz += ksiz;
    vnumsiz = cbsetvnumbuf(vnumbuf, vsiz);
    CB_REALLOC(buf, bsiz + vnumsiz + vsiz + 1);
    memcpy(buf + bsiz, vnumbuf, vnumsiz);
    bsiz += vnumsiz;
    memcpy(buf + bsiz, vbuf, vsiz);
    bsiz += vsiz;
  }
  *sp = bsiz;
  return buf;
}


/* Redintegrate a serialized map. */
CBMAP *cbmapload(const char *ptr, int size){
  CBMAP *map;
  const char *rp, *kbuf, *vbuf;
  int i, step, rn, ksiz, vsiz;
  assert(ptr && size >= 0);
  map = cbmapopenex(CB_MAPPBNUM);
  rp = ptr;
  rn = cbreadvnumbuf(rp, size, &step);
  rp += step;
  size -= step;
  if(rn > size) return map;
  for(i = 0; i < rn; i++){
    if(size < 1) break;
    ksiz = cbreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(ksiz > size) break;
    kbuf = rp;
    rp += ksiz;
    if(size < 1) break;
    vsiz = cbreadvnumbuf(rp, size, &step);
    rp += step;
    size -= step;
    if(vsiz > size) break;
    vbuf = rp;
    rp += vsiz;
    cbmapput(map, kbuf, ksiz, vbuf, vsiz, TRUE);
  }
  return map;
}


/* Allocate a formatted string on memory. */
char *cbsprintf(const char *format, ...){
  va_list ap;
  char *buf, cbuf[CB_SPBUFSIZ], *str;
  int len, cblen, num, slen;
  unsigned int unum;
  double dnum;
  va_start(ap, format);
  assert(format);
  CB_MALLOC(buf, 1);
  len = 0;
  while(*format != '\0'){
    if(*format == '%'){
      cbuf[0] = '%';
      cblen = 1;
      format++;
      while(strchr("0123456789 .+-", *format) && *format != '\0' && cblen < CB_SPBUFSIZ - 1){
        cbuf[cblen++] = *format;
        format++;
      }
      cbuf[cblen] = '\0';
      if(atoi(cbuf + 1) > CB_SPMAXWIDTH - 16){
        sprintf(cbuf, "(err)");
      } else {
        cbuf[cblen++] = *format;
        cbuf[cblen] = '\0';
      }
      switch(*format){
      case 'd':
        num = va_arg(ap, int);
        CB_REALLOC(buf, len + CB_SPMAXWIDTH + 2);
        len += sprintf(buf + len, cbuf, num);
        break;
      case 'o': case 'u': case 'x': case 'X': case 'c':
        unum = va_arg(ap, unsigned int);
        CB_REALLOC(buf, len + CB_SPMAXWIDTH + 2);
        len += sprintf(buf + len, cbuf, unum);
        break;
      case 'e': case 'E': case 'f': case 'g': case 'G':
        dnum = va_arg(ap, double);
        CB_REALLOC(buf, len + CB_SPMAXWIDTH + 2);
        len += sprintf(buf + len, cbuf, dnum);
        break;
      case 's':
        str = va_arg(ap, char *);
        slen = strlen(str);
        CB_REALLOC(buf, len + slen + 2);
        memcpy(buf + len, str, slen);
        len += slen;
        break;
      case '%':
        CB_REALLOC(buf, len + 2);
        buf[len++] = '%';
        break;
      default:
        break;
      }
    } else {
      CB_REALLOC(buf, len + 2);
      buf[len++] = *format;
    }
    format++;
  }
  buf[len] = '\0';
  va_end(ap);
  return buf;
}


/* Replace some patterns in a string. */
char *cbreplace(const char *str, CBMAP *pairs){
  int i, bsiz, wi, rep, ksiz, vsiz;
  char *buf;
  const char *key, *val;
  assert(str && pairs);
  bsiz = CB_DATUMUNIT;
  CB_MALLOC(buf, bsiz);
  wi = 0;
  while(*str != '\0'){
    rep = FALSE;
    cbmapiterinit(pairs);
    while((key = cbmapiternext(pairs, &ksiz)) != NULL){
      for(i = 0; i < ksiz; i++){
        if(str[i] == '\0' || str[i] != key[i]) break;
      }
      if(i == ksiz){
        val = cbmapget(pairs, key, ksiz, &vsiz);
        if(wi + vsiz >= bsiz){
          bsiz = bsiz * 2 + vsiz;
          CB_REALLOC(buf, bsiz);
        }
        memcpy(buf + wi, val, vsiz);
        wi += vsiz;
        str += ksiz;
        rep = TRUE;
        break;
      }
    }
    if(!rep){
      if(wi + 1 >= bsiz){
        bsiz = bsiz * 2 + 1;
        CB_REALLOC(buf, bsiz);
      }
      buf[wi++] = *str;
      str++;
    }
  }
  CB_REALLOC(buf, wi + 1);
  buf[wi] = '\0';
  return buf;
}


/* Make a list by split a serial datum. */
CBLIST *cbsplit(const char *ptr, int size, const char *delim){
  CBLIST *list;
  int bi, step;
  assert(ptr);
  list = cblistopen();
  if(size < 0) size = strlen(ptr);
  if(delim){
    for(bi = 0; bi < size; bi += step){
      step = 0;
      while(bi + step < size && !strchr(delim, ptr[bi+step])){
        step++;
      }
      cblistpush(list, ptr + bi, step);
      step++;
    }
    if(size > 0 && strchr(delim, ptr[size-1])) cblistpush(list, "", 0);
  } else {
    for(bi = 0; bi < size; bi += step){
      step = 0;
      while(bi + step < size && ptr[bi+step]){
        step++;
      }
      cblistpush(list, ptr + bi, step);
      step++;
    }
    if(size > 0 && ptr[size-1] == 0) cblistpush(list, "", 0);
  }
  return list;
}


/* Read whole data of a file. */
char *cbreadfile(const char *name, int *sp){
  int fd, size, rv;
  char iobuf[CB_IOBUFSIZ], *buf;
  if(name){
    if((fd = open(name, O_RDONLY, 0)) == -1) return NULL;
  } else {
    fd = 0;
  }
  CB_MALLOC(buf, 1);
  size = 0;
  while((rv = read(fd, iobuf, CB_IOBUFSIZ)) > 0){
    CB_REALLOC(buf, size + rv + 1);
    memcpy(buf + size, iobuf, rv);
    size += rv;
  }
  buf[size] = '\0';
  if(close(fd) == -1 || rv == -1){
    free(buf);
    return NULL;
  }
  if(sp) *sp = size;
  return buf;
}


/* Write data of a region into a file. */
int cbwritefile(const char *name, const char *ptr, int size){
  int fd, err, wb;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  if(name){
    if((fd = open(name, O_WRONLY | O_CREAT | O_TRUNC, CB_FILEMODE)) == -1) return FALSE;
  } else {
    fd = 1;
  }
  err = FALSE;
  wb = 0;
  do {
    wb = write(fd, ptr, size);
    switch(wb){
    case -1: err = errno != EINTR ? TRUE : FALSE; break;
    case 0: break;
    default:
      ptr += wb;
      size -= wb;
      break;
    }
  } while(size > 0);
  if(close(fd) == -1) err = TRUE;
  return err ? FALSE : TRUE;
}


/* Read every line of a file. */
CBLIST *cbreadlines(const char *name){
  char *buf, *tmp;
  int vsiz;
  CBMAP *pairs;
  CBLIST *list;
  if(!(buf = cbreadfile(name, NULL))) return NULL;
  pairs = cbmapopenex(3);
  cbmapput(pairs, "\r\n", 2, "\n", 1, TRUE);
  cbmapput(pairs, "\r", 1, "\n", 1, TRUE);
  tmp = cbreplace(buf, pairs);
  list = cbsplit(tmp, strlen(tmp), "\n");
  free(tmp);
  cbmapclose(pairs);
  free(buf);
  if(cblistnum(list) > 0){
    cblistval(list, cblistnum(list) - 1, &vsiz);
    if(vsiz < 1) free(cblistpop(list, NULL));
  }
  return list;
}


/* Read names of files in a directory. */
CBLIST *cbdirlist(const char *name){
  DIR *DD;
  struct dirent *dp;
  CBLIST *list;
  assert(name);
  if(!(DD = opendir(name))) return NULL;
  list = cblistopen();
  while((dp = readdir(DD)) != NULL){
    cblistpush(list, dp->d_name, -1);
  }
  if(closedir(DD) == -1){
    cblistclose(list);
    return NULL;
  }
  return list;
}


/* Get the status of a file or a directory. */
int cbfilestat(const char *name, int *isdirp, int *sizep, int *mtimep){
  struct stat sbuf;
  assert(name);
  if(stat(name, &sbuf) == -1) return FALSE;
  if(isdirp) *isdirp = S_ISDIR(sbuf.st_mode);
  if(sizep) *sizep = (int)sbuf.st_size;
  if(mtimep) *mtimep = (int)sbuf.st_mtime;
  return TRUE;
}


/* Break up a URL into elements. */
CBMAP *cburlbreak(const char *str){
  CBMAP *map;
  char *tmp, *ep;
  const char *rp;
  int i, serv;
  assert(str);
  map = cbmapopenex(CB_MAPPBNUM);
  rp = str;
  while(strchr(" \t\r\n\v\f", *rp)){
    rp++;
  }
  tmp = cbmemdup(rp, -1);
  for(i = 0; tmp[i] != '\0'; i++){
    if(strchr(" \t\r\n\v\f", tmp[i])){
      tmp[i] = '\0';
      break;
    }
  }
  rp = tmp;
  cbmapput(map, "self", -1, rp, -1, TRUE);
  serv = FALSE;
  if(cbstrfwimatch(rp, "http://")){
    cbmapput(map, "scheme", -1, "http", -1, TRUE);
    rp += 7;
    serv = TRUE;
  } else if(cbstrfwimatch(rp, "https://")){
    cbmapput(map, "scheme", -1, "https", -1, TRUE);
    rp += 8;
    serv = TRUE;
  } else if(cbstrfwimatch(rp, "ftp://")){
    cbmapput(map, "scheme", -1, "ftp", -1, TRUE);
    rp += 6;
    serv = TRUE;
  } else if(cbstrfwimatch(rp, "file://")){
    cbmapput(map, "scheme", -1, "file", -1, TRUE);
    rp += 7;
  }
  if((ep = strchr(rp, '#')) != NULL){
    cbmapput(map, "fragment", -1, ep + 1, -1, TRUE);
    *ep = '\0';
  }
  if((ep = strchr(rp, '?')) != NULL){
    cbmapput(map, "query", -1, ep + 1, -1, TRUE);
    *ep = '\0';
  }
  if(serv){
    if((ep = strchr(rp, '/')) != NULL){
      cbmapput(map, "path", -1, ep, -1, TRUE);
      *ep = '\0';
    } else {
      cbmapput(map, "path", -1, "/", -1, TRUE);
    }
    if((ep = strchr(rp, '@')) != NULL){
      *ep = '\0';
      if(rp[0] != '\0') cbmapput(map, "authority", -1, rp, -1, TRUE);
      rp = ep + 1;
    }
    if((ep = strchr(rp, ':')) != NULL){
      if(ep[1] != '\0') cbmapput(map, "port", -1, ep + 1, -1, TRUE);
      *ep = '\0';
    }
    if(rp[0] != '\0') cbmapput(map, "host", -1, rp, -1, TRUE);
  } else {
    cbmapput(map, "path", -1, rp, -1, TRUE);
  }
  free(tmp);
  if((rp = cbmapget(map, "path", -1, NULL)) != NULL){
    if((ep = strrchr(rp, '/')) != NULL){
      if(ep[1] != '\0') cbmapput(map, "file", -1, ep + 1, -1, TRUE);
    } else {
      cbmapput(map, "file", -1, rp, -1, TRUE);
    }
  }
  if((rp = cbmapget(map, "file", -1, NULL)) != NULL && (!strcmp(rp, ".") || !strcmp(rp, "..")))
    cbmapout(map, "file", -1);
  return map;
}


/* Encode a serial object with URL encoding. */
char *cburlencode(const char *ptr, int size){
  char *buf, *wp;
  int i, c;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  CB_MALLOC(buf, size * 3 + 1);
  wp = buf;
  for(i = 0; i < size; i++){
    c = ((unsigned char *)ptr)[i];
    if((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
       (c >= '0' && c <= '9') || (c != '\0' && strchr("_-.", c))){
      *(wp++) = c;
    } else {
      wp += sprintf(wp, "%%%02X", c);
    }
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with URL encoding. */
char *cburldecode(const char *str, int *sp){
  const char *hex = "1234567890abcdefABCDEF";
  char *buf, *wp;
  unsigned char c;
  buf = cbmemdup(str, -1);
  wp = buf;
  while(*str != '\0'){
    if(*str == '%'){
      str++;
      if(strchr(hex, *str) && strchr(hex, *(str + 1))){
        c = *str;
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c >= 'a' && c <= 'z'){
          *wp = c - 'a' + 10;
        } else {
          *wp = c - '0';
        }
        *wp *= 0x10;
        str++;
        c = *str;
        if(c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if(c >= 'a' && c <= 'z'){
          *wp += c - 'a' + 10;
        } else {
          *wp += c - '0';
        }
        str++;
        wp++;
      } else {
        break;
      }
    } else if(*str == '+'){
      *wp = ' ';
      str++;
      wp++;
    } else {
      *wp = *str;
      str++;
      wp++;
    }
  }
  *wp = '\0';
  if(sp) *sp = wp - buf;
  return buf;
}


/* Encode a serial object with Base64 encoding. */
char *cbbaseencode(const char *ptr, int size){
  char *tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  char *buf, *wp;
  const unsigned char *obj;
  int i;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  CB_MALLOC(buf, 4 * (size + 2) / 3 + 1);
  obj = (const unsigned char *)ptr;
  wp = buf;
  for(i = 0; i < size; i += 3){
    switch(size - i){
    case 1:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[(obj[0] & 3) << 4];
      *wp++ = '=';
      *wp++ = '=';
      break;
    case 2:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[((obj[0] & 3) << 4) + (obj[1] >> 4)];
      *wp++ = tbl[(obj[1] & 0xf) << 2];
      *wp++ = '=';
      break;
    default:
      *wp++ = tbl[obj[0] >> 2];
      *wp++ = tbl[((obj[0] & 3) << 4) + (obj[1] >> 4)];
      *wp++ = tbl[((obj[1] & 0xf) << 2) + (obj[2] >> 6)];
      *wp++ = tbl[obj[2] & 0x3f];
      break;
    }
    obj += 3;
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with Base64 encoding. */
char *cbbasedecode(const char *str, int *sp){
  unsigned char *obj, *wp;
  int len, cnt, bpos, i, bits, eqcnt;
  assert(str);
  cnt = 0;
  bpos = 0;
  eqcnt = 0;
  len = strlen(str);
  CB_MALLOC(obj, len + 1);
  wp = obj;
  while(bpos < len && eqcnt == 0){
    bits = 0;
    for(i = 0; bpos < len && i < 4; bpos++){
      if(str[bpos] >= 'A' && str[bpos] <= 'Z'){
        bits = (bits << 6) | (str[bpos] - 'A');
        i++;
      } else if(str[bpos] >= 'a' && str[bpos] <= 'z'){
        bits = (bits << 6) | (str[bpos] - 'a' + 26);
        i++;
      } else if(str[bpos] >= '0' && str[bpos] <= '9'){
        bits = (bits << 6) | (str[bpos] - '0' + 52);
        i++;
      } else if(str[bpos] == '+'){
        bits = (bits << 6) | 62;
        i++;
      } else if(str[bpos] == '/'){
        bits = (bits << 6) | 63;
        i++;
      } else if(str[bpos] == '='){
        bits <<= 6;
        i++;
        eqcnt++;
      }
    }
    if(i == 0 && bpos >= len) continue;
    switch(eqcnt){
    case 0:
      *wp++ = (bits >> 16) & 0xff;
      *wp++ = (bits >> 8) & 0xff;
      *wp++ = bits & 0xff;
      cnt += 3;
      break;
    case 1:
      *wp++ = (bits >> 16) & 0xff;
      *wp++ = (bits >> 8) & 0xff;
      cnt += 2;
      break;
    case 2:
      *wp++ = (bits >> 16) & 0xff;
      cnt += 1;
      break;
    }
  }
  obj[cnt] = '\0';
  if(sp) *sp = cnt;
  return (char *)obj;
}


/* Encode a serial object with quoted-printable encoding. */
char *cbquoteencode(const char *ptr, int size){
  const unsigned char *rp;
  char *buf, *wp;
  int i, cols;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  rp = (const unsigned char *)ptr;
  CB_MALLOC(buf, size * 3 + 1);
  wp = buf;
  cols = 0;
  for(i = 0; i < size; i++){
    if(rp[i] == '=' || (rp[i] < 0x20 && rp[i] != '\r' && rp[i] != '\n' && rp[i] != '\t') ||
       rp[i] > 0x7e){
      wp += sprintf(wp, "=%02X", rp[i]);
      cols += 3;
    } else {
      *(wp++) = rp[i];
      cols++;
    }
  }
  *wp = '\0';
  return buf;
}


/* Decode a string encoded with quoted-printable encoding. */
char *cbquotedecode(const char *str, int *sp){
  char *buf, *wp;
  assert(str);
  CB_MALLOC(buf, strlen(str) + 1);
  wp = buf;
  for(; *str != '\0'; str++){
    if(*str == '='){
      str++;
      if(*str == '\0'){
        break;
      } else if(str[0] == '\r' && str[1] == '\n'){
        str++;
      } else if(str[0] != '\n' && str[0] != '\r'){
        if(*str >= 'A' && *str <= 'Z'){
          *wp = (*str - 'A' + 10) * 16;
        } else if(*str >= 'a' && *str <= 'z'){
          *wp = (*str - 'a' + 10) * 16;
        } else {
          *wp = (*str - '0') * 16;
        }
        str++;
        if(*str == '\0') break;
        if(*str >= 'A' && *str <= 'Z'){
          *wp += *str - 'A' + 10;
        } else if(*str >= 'a' && *str <= 'z'){
          *wp += *str - 'a' + 10;
        } else {
          *wp += *str - '0';
        }
        wp++;
      }
    } else {
      *wp = *str;
      wp++;
    }
  }
  *wp = '\0';
  if(sp) *sp = wp - buf;
  return buf;
}


/* Split a string of MIME into headers and the body. */
char *cbmimebreak(const char *ptr, int size, CBMAP *attrs, int *sp){
  CBLIST *list;
  const char *head, *line, *pv, *ep;
  char *hbuf, *name;
  int i, j, wi, hlen;
  assert(ptr);
  if(size < 0) size = strlen(ptr);
  head = NULL;
  hlen = 0;
  for(i = 0; i < size; i++){
    if(i < size - 4 && ptr[i] == '\r' && ptr[i+1] == '\n' &&
       ptr[i+2] == '\r' && ptr[i+3] == '\n'){
      head = ptr;
      hlen = i;
      ptr += i + 4;
      size -= i + 4;
      break;
    } else if(i < size - 2 && ptr[i] == '\n' && ptr[i+1] == '\n'){
      head = ptr;
      hlen = i;
      ptr += i + 2;
      size -= i + 2;
      break;
    }
  }
  if(head && attrs){
    CB_MALLOC(hbuf, hlen + 1);
    wi = 0;
    for(i = 0; i < hlen; i++){
      if(head[i] == '\r') continue;
      if(i < hlen - 1 && head[i] == '\n' && (head[i+1] == ' ' || head[i+1] == '\t')){
        hbuf[wi++] = ' ';
        i++;
      } else {
        hbuf[wi++] = head[i];
      }
    }
    list = cbsplit(hbuf, wi, "\n");
    for(i = 0; i < cblistnum(list); i++){
      line = cblistval(list, i, NULL);
      if((pv = strchr(line, ':')) != NULL){
        name = cbmemdup(line, pv - line);
        for(j = 0; name[j] != '\0'; j++){
          if(name[j] >= 'A' && name[j] <= 'Z') name[j] -= 'A' - 'a';
        }
        pv++;
        while(*pv == ' ' || *pv == '\t'){
          pv++;
        }
        cbmapput(attrs, name, -1, pv, -1, TRUE);
        free(name);
      }

    }
    cblistclose(list);
    free(hbuf);
    if((pv = cbmapget(attrs, "content-type", -1, NULL)) != NULL){
      if((ep = strchr(pv, ';')) != NULL){
        cbmapput(attrs, "TYPE", -1, pv, ep - pv, TRUE);
        do {
          ep++;
          while(ep[0] == ' '){
            ep++;
          }
          if(cbstrfwimatch(ep, "charset=")){
            ep += 8;
            if(ep[0] == '"') ep++;
            pv = ep;
            while(ep[0] != '\0' && ep[0] != ' ' && ep[0] != '"' && ep[0] != ';'){
              ep++;
            }
            cbmapput(attrs, "CHARSET", -1, pv, ep - pv, TRUE);
          } else if(cbstrfwimatch(ep, "boundary=")){
            ep += 9;
            if(ep[0] == '"') ep++;
            pv = ep;
            while(ep[0] != '\0' && ep[0] != ' ' && ep[0] != '"' && ep[0] != ';'){
              ep++;
            }
            cbmapput(attrs, "BOUNDARY", -1, pv, ep - pv, TRUE);
          }
        } while((ep = strchr(ep, ';')) != NULL);
      } else {
        cbmapput(attrs, "TYPE", -1, pv, -1, TRUE);
      }
    }
    if((pv = cbmapget(attrs, "content-disposition", -1, NULL)) != NULL){
      if((ep = strchr(pv, ';')) != NULL){
        cbmapput(attrs, "DISPOSITION", -1, pv, ep - pv, TRUE);
        do {
          ep++;
          while(ep[0] == ' '){
            ep++;
          }
          if(cbstrfwimatch(ep, "filename=")){
            ep += 9;
            if(ep[0] == '"') ep++;
            pv = ep;
            while(ep[0] != '\0' && ep[0] != '"'){
              ep++;
            }
            cbmapput(attrs, "FILENAME", -1, pv, ep - pv, TRUE);
          } else if(cbstrfwimatch(ep, "name=")){
            ep += 5;
            if(ep[0] == '"') ep++;
            pv = ep;
            while(ep[0] != '\0' && ep[0] != '"'){
              ep++;
            }
            cbmapput(attrs, "NAME", -1, pv, ep - pv, TRUE);
          }
        } while((ep = strchr(ep, ';')) != NULL);
      } else {
        cbmapput(attrs, "DISPOSITION", -1, pv, -1, TRUE);
      }
    }
  }
  if(sp) *sp = size;
  return cbmemdup(ptr, size);
}


/* Split multipart data in MIME into its parts. */
CBLIST *cbmimeparts(const char *ptr, int size, const char *boundary){
  CBLIST *list;
  const char *pv, *ep;
  int i, blen;
  assert(ptr && boundary);
  if(size < 0) size = strlen(ptr);
  list = cblistopen();
  blen = strlen(boundary);
  pv = NULL;
  for(i = 0; i < size; i++){
    if(ptr[i] == '-' && ptr[i+1] == '-' && i + 2 + blen < size &&
       cbstrfwmatch(ptr + i + 2, boundary)){
      pv = ptr + i + 2 + blen;
      if(*pv == '\r') pv++;
      if(*pv == '\n') pv++;
      size -= pv - ptr;
      ptr = pv;
      break;
    }
  }
  if(!pv) return list;
  for(i = 0; i < size; i++){
    if(ptr[i] == '-' && ptr[i+1] == '-' && i + 2 + blen < size &&
       cbstrfwmatch(ptr + i + 2, boundary)){
      ep = ptr + i;
      if(ep > ptr && ep[-1] == '\n') ep--;
      if(ep > ptr && ep[-1] == '\r') ep--;
      cblistpush(list, pv, ep - pv);
      pv = ptr + i + 2 + blen;
      if(*pv == '\r') pv++;
      if(*pv == '\n') pv++;
    }
  }
  return list;
}


/* Encode a string with MIME encoding. */
char *cbmimeencode(const char *str, const char *encname, int base){
  char *buf, *wp, *enc;
  int len;
  assert(str && encname);
  len = strlen(str);
  CB_MALLOC(buf, len * 3 + strlen(encname) + 16);
  wp = buf;
  wp += sprintf(wp, "=?%s?%c?", encname, base ? 'B' : 'Q');
  enc = base ? cbbaseencode(str, len) : cbquoteencode(str, len);
  wp += sprintf(wp, "%s?=", enc);
  free(enc);
  return buf;
}


/* Decode a string encoded with MIME encoding. */
char *cbmimedecode(const char *str, char *enp){
  char *buf, *wp, *tmp, *dec;
  const char *pv, *ep;
  int quoted;
  assert(str);
  if(enp) sprintf(enp, "US-ASCII");
  CB_MALLOC(buf, strlen(str) + 1);
  wp = buf;
  while(*str != '\0'){
    if(cbstrfwmatch(str, "=?")){
      str += 2;
      pv = str;
      if(!(ep = strchr(str, '?'))) continue;
      if(enp && ep - pv < CB_ENCBUFSIZ){
        memcpy(enp, pv, ep - pv);
        enp[ep-pv] = '\0';
      }
      pv = ep + 1;
      quoted = (*pv == 'Q' || *pv == 'q');
      if(*pv != '\0') pv++;
      if(*pv != '\0') pv++;
      if(!(ep = strchr(pv, '?'))) continue;
      tmp = cbmemdup(pv, ep - pv);
      dec = quoted ? cbquotedecode(tmp, NULL) : cbbasedecode(tmp, NULL);
      wp += sprintf(wp, "%s", dec);
      free(dec);
      free(tmp);
      str = ep + 1;
      if(*str != '\0') str++;
    } else {
      *(wp++) = *str;
      str++;
    }
  }
  *wp = '\0';
  return buf;
}


/* Split a string of CSV into rows. */
CBLIST *cbcsvrows(const char *str){
  CBLIST *list;
  const char *pv;
  int quoted;
  assert(str);
  list = cblistopen();
  pv = str;
  quoted = FALSE;
  while(TRUE){
    if(*str == '"') quoted = !quoted;
    if(!quoted && (*str == '\r' || *str == '\n')){
      cblistpush(list, pv, str - pv);
      if(str[0] == '\r' && str[1] == '\n') str++;
      str++;
      pv = str;
    } else if(*str == '\0'){
      if(str > pv) cblistpush(list, pv, str - pv);
      break;
    } else {
      str++;
    }
  }
  return list;
}


/* Split a string of a row of CSV into cells. */
CBLIST *cbcsvcells(const char *str){
  CBLIST *list, *uelist;
  const char *pv;
  char *tmp;
  int i, quoted;
  assert(str);
  list = cblistopen();
  pv = str;
  quoted = FALSE;
  while(TRUE){
    if(*str == '"') quoted = !quoted;
    if(!quoted && *str == ','){
      cblistpush(list, pv, str - pv);
      str++;
      pv = str;
    } else if(*str == '\0'){
      cblistpush(list, pv, str - pv);
      break;
    } else {
      str++;
    }
  }
  uelist = cblistopen();
  for(i = 0; i < cblistnum(list); i++){
    tmp = cbcsvunescape(cblistval(list, i, NULL));
    cblistpush(uelist, tmp, -1);
    free(tmp);
  }
  cblistclose(list);
  return uelist;
}


/* Escape a string with the meta characters of CSV. */
char *cbcsvescape(const char *str){
  char *buf, *wp;
  int i;
  assert(str);
  CB_MALLOC(buf, strlen(str) * 2 + 3);
  wp = buf;
  *(wp++) = '"';
  for(i = 0; str[i] != '\0'; i++){
    if(str[i] == '"') *(wp++) = '"';
    *(wp++) = str[i];
  }
  *(wp++) = '"';
  *wp = '\0';
  return buf;
}


/* Unescape a string with the escaped meta characters of CSV. */
char *cbcsvunescape(const char *str){
  char *buf, *wp;
  int i, len;
  assert(str);
  len = strlen(str);
  if(str[0] == '"'){
    str++;
    len--;
    if(str[len-1] == '"') len--;
  }
  CB_MALLOC(buf, len + 1);
  wp = buf;
  for(i = 0; i < len; i++){
    if(str[i] == '"'){
      if(str[i+1] == '"') *(wp++) = str[i++];
    } else {
      *(wp++) = str[i];
    }
  }
  *wp = '\0';
  return buf;
}


/* Split a string of XML into tags and text sections. */
CBLIST *cbxmlbreak(const char *str, int cr){
  CBLIST *list;
  CBDATUM *datum;
  int i, pv, tag;
  char *ep;
  assert(str);
  list = cblistopen();
  i = 0;
  pv = 0;
  tag = FALSE;
  while(TRUE){
    if(str[i] == '\0'){
      if(i > pv) cblistpush(list, str + pv, i - pv);
      break;
    } else if(!tag && str[i] == '<'){
      if(str[i+1] == '!' && str[i+2] == '-' && str[i+3] == '-'){
        if(i > pv) cblistpush(list, str + pv, i - pv);
        if((ep = strstr(str + i, "-->")) != NULL){
          if(!cr) cblistpush(list, str + i, ep - str - i + 3);
          i = ep - str + 2;
          pv = i + 1;
        }
      } else if(str[i+1] == '!' && str[i+2] == '[' && cbstrfwimatch(str + i, "<![CDATA[")){
        if(i > pv) cblistpush(list, str + pv, i - pv);
        if((ep = strstr(str + i, "]]>")) != NULL){
          i += 9;
          datum = cbdatumopen(NULL, 0);
          while(str + i < ep){
            if(str[i] == '&'){
              cbdatumcat(datum, "&amp;", 5);
            } else if(str[i] == '<'){
              cbdatumcat(datum, "&lt;", 4);
            } else if(str[i] == '>'){
              cbdatumcat(datum, "&gt;", 4);
            } else {
              cbdatumcat(datum, str + i, 1);
            }
            i++;
          }
          if(cbdatumsize(datum) > 0) cblistpush(list, cbdatumptr(datum), cbdatumsize(datum));
          cbdatumclose(datum);
          i = ep - str + 2;
          pv = i + 1;
        }
      } else {
        if(i > pv) cblistpush(list, str + pv, i - pv);
        tag = TRUE;
        pv = i;
      }
    } else if(tag && str[i] == '>'){
      if(i > pv) cblistpush(list, str + pv, i - pv + 1);
      tag = FALSE;
      pv = i + 1;
    }
    i++;
  }
  return list;
}


/* Get the map of attributes of a XML tag. */
CBMAP *cbxmlattrs(const char *str){
  CBMAP *map;
  const unsigned char *rp, *key, *val;
  char *copy, *raw;
  int ksiz, vsiz;
  assert(str);
  map = cbmapopenex(CB_MAPPBNUM);
  rp = (unsigned char *)str;
  while(*rp == '<' || *rp == '/' || *rp == '?' || *rp == '!' || *rp == ' '){
    rp++;
  }
  key = rp;
  while(*rp > 0x20 && *rp != '/' && *rp != '>'){
    rp++;
  }
  cbmapput(map, "", -1, (char *)key, rp - key, FALSE);
  while(*rp != '\0'){
    while(*rp != '\0' && (*rp <= 0x20 || *rp == '/' || *rp == '?' || *rp == '>')){
      rp++;
    }
    key = rp;
    while(*rp > 0x20 && *rp != '/' && *rp != '>' && *rp != '='){
      rp++;
    }
    ksiz = rp - key;
    while(*rp != '\0' && (*rp == '=' || *rp <= 0x20)){
      rp++;
    }
    if(*rp == '"'){
      rp++;
      val = rp;
      while(*rp != '\0' && *rp != '"'){
        rp++;
      }
      vsiz = rp - val;
    } else if(*rp == '\''){
      rp++;
      val = rp;
      while(*rp != '\0' && *rp != '\''){
        rp++;
      }
      vsiz = rp - val;
    } else {
      val = rp;
      while(*rp > 0x20 && *rp != '"' && *rp != '\'' && *rp != '/' && *rp != '>'){
        rp++;
      }
      vsiz = rp - val;
    }
    if(*rp != '\0') rp++;
    if(ksiz > 0){
      copy = cbmemdup((char *)val, vsiz);
      raw = cbxmlunescape(copy);
      cbmapput(map, (char *)key, ksiz, raw, -1, FALSE);
      free(raw);
      free(copy);
    }
  }
  return map;
}


/* Escape a string with the meta characters of XML. */
char *cbxmlescape(const char *str){
  CBDATUM *datum;
  assert(str);
  datum = cbdatumopen("", 0);
  while(*str != '\0'){
    switch(*str){
    case '&':
      cbdatumcat(datum, "&amp;", 5);
      break;
    case '<':
      cbdatumcat(datum, "&lt;", 4);
      break;
    case '>':
      cbdatumcat(datum, "&gt;", 4);
      break;
    case '"':
      cbdatumcat(datum, "&quot;", 6);
      break;
    case '\'':
      cbdatumcat(datum, "&apos;", 6);
      break;
    default:
      cbdatumcat(datum, str, 1);
      break;
    }
    str++;
  }
  return cbdatumtomalloc(datum, NULL);
}


/* Unescape a string with the entity references of XML. */
char *cbxmlunescape(const char *str){
  CBDATUM *datum;
  assert(str);
  datum = cbdatumopen("", 0);
  while(*str != '\0'){
    if(*str == '&'){
      if(cbstrfwmatch(str, "&amp;")){
        cbdatumcat(datum, "&", 1);
        str += 5;
      } else if(cbstrfwmatch(str, "&lt;")){
        cbdatumcat(datum, "<", 1);
        str += 4;
      } else if(cbstrfwmatch(str, "&gt;")){
        cbdatumcat(datum, ">", 1);
        str += 4;
      } else if(cbstrfwmatch(str, "&quot;")){
        cbdatumcat(datum, "\"", 1);
        str += 6;
      } else if(cbstrfwmatch(str, "&apos;")){
        cbdatumcat(datum, "'", 1);
        str += 6;
      } else {
        cbdatumcat(datum, str, 1);
        str++;
      }
    } else {
      cbdatumcat(datum, str, 1);
      str++;
    }
  }
  return cbdatumtomalloc(datum, NULL);
}


/* Compress a serial object with ZLIB. */
char *cbdeflate(const char *ptr, int size, int *sp){
  assert(ptr && sp);
  if(!_qdbm_deflate) return NULL;
  return _qdbm_deflate(ptr, size, sp);
}


/* Decompress a serial object compressed with ZLIB. */
char *cbinflate(const char *ptr, int size, int *sp){
  assert(ptr && size >= 0);
  if(!_qdbm_inflate) return NULL;
  return _qdbm_inflate(ptr, size, sp);
}


/* Get the CRC32 checksum of a serial object. */
unsigned int cbgetcrc(const char *ptr, int size){
  assert(ptr);
  if(!_qdbm_inflate) return 0;
  return _qdbm_getcrc(ptr, size);
}


/* Convert the character encoding of a string. */
char *cbiconv(const char *ptr, int size, const char *icode, const char *ocode, int *sp, int *mp){
  assert(ptr && icode && ocode);
  if(!_qdbm_iconv) return NULL;
  return _qdbm_iconv(ptr, size, icode, ocode, sp, mp);
}


/* Detect the encoding of a string automatically. */
const char *cbencname(const char *ptr, int size){
  assert(ptr);
  if(!_qdbm_encname) return "ISO-8859-1";
  return _qdbm_encname(ptr, size);
}


/* Get the jet lag of the local time in seconds. */
int cbjetlag(void){
  struct tm ts, *tp;
  time_t t, gt, lt;
  if((t = time(NULL)) < 0) return 0;
  if(!(tp = _qdbm_gmtime(&t, &ts))) return 0;
  if((gt = mktime(tp)) < 0) return 0;
  if(!(tp = _qdbm_localtime(&t, &ts))) return 0;
  if((lt = mktime(tp)) < 0) return 0;
  return lt - gt;
}


/* Get the Gregorian calendar of a time. */
void cbcalendar(time_t t, int jl, int *yearp, int *monp, int *dayp,
                int *hourp, int *minp, int *secp){
  struct tm ts, *tp;
  if(t < 0) t = time(NULL);
  t += jl;
  if(!(tp = _qdbm_gmtime(&t, &ts))) return;
  if(yearp) *yearp = tp->tm_year + 1900;
  if(monp) *monp = tp->tm_mon + 1;
  if(dayp) *dayp = tp->tm_mday;
  if(hourp) *hourp = tp->tm_hour;
  if(minp) *minp = tp->tm_min;
  if(secp) *secp = tp->tm_sec;
}


/* Get the day of week of a date. */
int cbdayofweek(int year, int mon, int day){
  if(mon < 3){
    year--;
    mon += 12;
  }
  return (day + ((8 + (13 * mon)) / 5) + (year + (year / 4) - (year / 100) + (year / 400))) % 7;
}


/* Get the string for a date in W3CDTF. */
char *cbdatestrwww(time_t t, int jl){
  char date[CB_DATEBUFSIZ], tzone[CB_DATEBUFSIZ];
  int year, mon, day, hour, min, sec;
  cbcalendar(t, jl, &year, &mon, &day, &hour, &min, &sec);
  jl /= 60;
  if(jl == 0){
    sprintf(tzone, "Z");
  } else if(jl < 0){
    jl *= -1;
    sprintf(tzone, "-%02d:%02d", jl / 60, jl % 60);
  } else {
    sprintf(tzone, "+%02d:%02d", jl / 60, jl % 60);
  }
  sprintf(date, "%04d-%02d-%02dT%02d:%02d:%02d%s", year, mon, day, hour, min, sec, tzone);
  return cbmemdup(date, -1);
}


/* Get the string for a date in RFC 1123 format. */
char *cbdatestrhttp(time_t t, int jl){
  char date[CB_DATEBUFSIZ], *wp;
  int year, mon, day, hour, min, sec;
  cbcalendar(t, jl, &year, &mon, &day, &hour, &min, &sec);
  jl /= 60;
  wp = date;
  switch(cbdayofweek(year, mon, day)){
  case 0: wp += sprintf(wp, "Sun, "); break;
  case 1: wp += sprintf(wp, "Mon, "); break;
  case 2: wp += sprintf(wp, "Tue, "); break;
  case 3: wp += sprintf(wp, "Wed, "); break;
  case 4: wp += sprintf(wp, "Thu, "); break;
  case 5: wp += sprintf(wp, "Fri, "); break;
  case 6: wp += sprintf(wp, "Sat, "); break;
  }
  wp += sprintf(wp, "%02d ", day);
  switch(mon){
  case 1: wp += sprintf(wp, "Jan "); break;
  case 2: wp += sprintf(wp, "Feb "); break;
  case 3: wp += sprintf(wp, "Mar "); break;
  case 4: wp += sprintf(wp, "Apr "); break;
  case 5: wp += sprintf(wp, "May "); break;
  case 6: wp += sprintf(wp, "Jun "); break;
  case 7: wp += sprintf(wp, "Jul "); break;
  case 8: wp += sprintf(wp, "Aug "); break;
  case 9: wp += sprintf(wp, "Sep "); break;
  case 10: wp += sprintf(wp, "Oct "); break;
  case 11: wp += sprintf(wp, "Nov "); break;
  case 12: wp += sprintf(wp, "Dec "); break;
  }
  wp += sprintf(wp, "%04d %02d:%02d:%02d ", year, hour, min, sec);
  if(jl == 0){
    wp += sprintf(wp, "GMT");
  } else if(jl < 0){
    jl *= -1;
    wp += sprintf(wp, "-%02d%02d", jl / 60, jl % 60);
  } else {
    wp += sprintf(wp, "+%02d%02d", jl / 60, jl % 60);
  }
  return cbmemdup(date, -1);
}


/* Get the time value of a date string in decimal, W3CDTF, or RFC 1123. */
time_t cbstrmktime(const char *str){
  const char *crp;
  char *pv, *rp;
  int len, clen;
  time_t t;
  struct tm ts;
  assert(str);
  if(cbstrfwimatch(str, "0x")) return (time_t)strtol(str + 2, NULL, 16);
  memset(&ts, 0, sizeof(struct tm));
  ts.tm_year = 70;
  ts.tm_mon = 0;
  ts.tm_mday = 1;
  ts.tm_hour = 0;
  ts.tm_min = 0;
  ts.tm_sec = 0;
  ts.tm_isdst = 0;
  len = strlen(str);
  t = (time_t)strtol(str, &pv, 10);
  if(*pv == '\0') return t;
  if(len > 4 && str[4] == '-'){
    ts.tm_year = atoi(str) - 1900;
    if((pv = strchr(str, '-')) != NULL && pv - str == 4){
      rp = pv + 1;
      ts.tm_mon = atoi(rp) - 1;
      if((pv = strchr(rp, '-')) != NULL && pv - str == 7){
        rp = pv + 1;
        ts.tm_mday = atoi(rp);
        if((pv = strchr(rp, 'T')) != NULL && pv - str == 10){
          rp = pv + 1;
          ts.tm_hour = atoi(rp);
          if((pv = strchr(rp, ':')) != NULL && pv - str == 13){
            rp = pv + 1;
            ts.tm_min = atoi(rp);
          }
          if((pv = strchr(rp, ':')) != NULL && pv - str == 16){
            rp = pv + 1;
            ts.tm_sec = atoi(rp);
          }
          if((pv = strchr(rp, '.')) != NULL && pv - str == 19) rp = pv + 1;
          strtol(rp, &pv, 10);
          if((*pv == '+' || *pv == '-') && strlen(pv) == 6 && pv[3] == ':')
            ts.tm_sec -= (atoi(pv + 1) * 3600 + atoi(pv + 4) * 60) * (pv[0] == '+' ? 1 : -1);
        }
      }
    }
    ts.tm_sec += cbjetlag();
    return mktime(&ts);
  }
  if(len > 4 && str[4] == '/'){
    ts.tm_year = atoi(str) - 1900;
    if((pv = strchr(str, '/')) != NULL && pv - str == 4){
      rp = pv + 1;
      ts.tm_mon = atoi(rp) - 1;
      if((pv = strchr(rp, '/')) != NULL && pv - str == 7){
        rp = pv + 1;
        ts.tm_mday = atoi(rp);
        if((pv = strchr(rp, ' ')) != NULL && pv - str == 10){
          rp = pv + 1;
          ts.tm_hour = atoi(rp);
          if((pv = strchr(rp, ':')) != NULL && pv - str == 13){
            rp = pv + 1;
            ts.tm_min = atoi(rp);
          }
          if((pv = strchr(rp, ':')) != NULL && pv - str == 16){
            rp = pv + 1;
            ts.tm_sec = atoi(rp);
          }
          if((pv = strchr(rp, '.')) != NULL && pv - str == 19) rp = pv + 1;
          strtol(rp, &pv, 10);
          if((*pv == '+' || *pv == '-') && strlen(pv) == 6 && pv[3] == ':')
            ts.tm_sec -= (atoi(pv + 1) * 3600 + atoi(pv + 4) * 60) * (pv[0] == '+' ? 1 : -1);
        }
      }
    }
    ts.tm_sec += cbjetlag();
    return mktime(&ts);
  }
  crp = str;
  if(len >= 4 && str[3] == ',') crp = str + 4;
  while(*crp == ' '){
    crp++;
  }
  ts.tm_mday = atoi(crp);
  while((*crp >= '0' && *crp <= '9') || *crp == ' '){
    crp++;
  }
  if(cbstrfwimatch(crp, "Jan")){
    ts.tm_mon = 0;
  } else if(cbstrfwimatch(crp, "Feb")){
    ts.tm_mon = 1;
  } else if(cbstrfwimatch(crp, "Mar")){
    ts.tm_mon = 2;
  } else if(cbstrfwimatch(crp, "Apr")){
    ts.tm_mon = 3;
  } else if(cbstrfwimatch(crp, "May")){
    ts.tm_mon = 4;
  } else if(cbstrfwimatch(crp, "Jun")){
    ts.tm_mon = 5;
  } else if(cbstrfwimatch(crp, "Jul")){
    ts.tm_mon = 6;
  } else if(cbstrfwimatch(crp, "Aug")){
    ts.tm_mon = 7;
  } else if(cbstrfwimatch(crp, "Sep")){
    ts.tm_mon = 8;
  } else if(cbstrfwimatch(crp, "Oct")){
    ts.tm_mon = 9;
  } else if(cbstrfwimatch(crp, "Nov")){
    ts.tm_mon = 10;
  } else if(cbstrfwimatch(crp, "Dec")){
    ts.tm_mon = 11;
  } else {
    ts.tm_mon = -1;
  }
  if(ts.tm_mon >= 0) crp += 3;
  while(*crp == ' '){
    crp++;
  }
  ts.tm_year = atoi(crp);
  if(ts.tm_year >= 1969) ts.tm_year -= 1900;
  while(*crp >= '0' && *crp <= '9'){
    crp++;
  }
  while(*crp == ' '){
    crp++;
  }
  if(ts.tm_mday > 0 && ts.tm_mon >= 0 && ts.tm_year >= 0){
    clen = strlen(crp);
    if(clen >= 8 && crp[2] == ':' && crp[5] == ':'){
      ts.tm_hour = atoi(crp + 0);
      ts.tm_min = atoi(crp + 3);
      ts.tm_sec = atoi(crp + 6);
      if(clen == 14 && crp[8] == ' ' && (crp[9] == '+' || crp[9] == '-')){
        ts.tm_sec -= ((crp[10] - '0') * 36000 + (crp[11] - '0') * 3600 +
                      (crp[12] - '0') * 600 + (crp[13] - '0') * 60) * (crp[9] == '+' ? 1 : -1);
      } else if(clen > 9){
        if(!strcmp(crp + 9, "JST")){
          ts.tm_sec -= 9 * 3600;
        } else if(!strcmp(crp + 9, "CCT")){
          ts.tm_sec -= 8 * 3600;
        } else if(!strcmp(crp + 9, "KST")){
          ts.tm_sec -= 9 * 3600;
        } else if(!strcmp(crp + 9, "EDT")){
          ts.tm_sec -= -4 * 3600;
        } else if(!strcmp(crp + 9, "EST")){
          ts.tm_sec -= -5 * 3600;
        } else if(!strcmp(crp + 9, "CDT")){
          ts.tm_sec -= -5 * 3600;
        } else if(!strcmp(crp + 9, "CST")){
          ts.tm_sec -= -6 * 3600;
        } else if(!strcmp(crp + 9, "MDT")){
          ts.tm_sec -= -6 * 3600;
        } else if(!strcmp(crp + 9, "MST")){
          ts.tm_sec -= -7 * 3600;
        } else if(!strcmp(crp + 9, "PDT")){
          ts.tm_sec -= -7 * 3600;
        } else if(!strcmp(crp + 9, "PST")){
          ts.tm_sec -= -8 * 3600;
        } else if(!strcmp(crp + 9, "HDT")){
          ts.tm_sec -= -9 * 3600;
        } else if(!strcmp(crp + 9, "HST")){
          ts.tm_sec -= -10 * 3600;
        }
      }
    }
    ts.tm_sec += cbjetlag();
    return mktime(&ts);
  }
  return -1;
}


/* Get user and system processing times. */
void cbproctime(double *usrp, double *sysp){
  struct tms buf;
  times(&buf);
  if(usrp) *usrp = (double)buf.tms_utime / sysconf(_SC_CLK_TCK);
  if(sysp) *sysp = (double)buf.tms_stime / sysconf(_SC_CLK_TCK);
}


/* Ensure that the standard I/O is binary mode. */
void cbstdiobin(void){
  if(setmode(0, O_BINARY) == -1 || setmode(1, O_BINARY) == -1 || setmode(2, O_BINARY) == -1){
    if(cbfatalfunc){
      cbfatalfunc("setmode failed");
    } else {
      cbmyfatal("setmode failed");
    }
  }
}



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Show error message on the standard error output and exit. */
void cbmyfatal(const char *message){
  char buf[CB_MSGBUFSIZ];
  assert(message);
  sprintf(buf, "fatal error: %s\n", message);
  write(2, buf, strlen(buf));
  exit(1);
}


/* Add an allocated element at the end of a list. */
void cblistpushbuf(CBLIST *list, char *ptr, int size){
  int index;
  assert(list && ptr && size >= 0);
  index = list->start + list->num;
  if(index >= list->anum){
    list->anum *= 2;
    CB_REALLOC(list->array, list->anum * sizeof(list->array[0]));
  }
  list->array[index].dptr = ptr;
  list->array[index].dsize = size;
  list->num++;
}


/* Get a map handle with specifying the number of buckets. */
CBMAP *cbmapopenex(int bnum){
  CBMAP *map;
  int i;
  assert(bnum > 0);
  CB_MALLOC(map, sizeof(*map));
  CB_MALLOC(map->buckets, sizeof(map->buckets[0]) * bnum);
  for(i = 0; i < bnum; i++){
    map->buckets[i] = NULL;
  }
  map->first = NULL;
  map->last = NULL;
  map->cur = NULL;
  map->bnum = bnum;
  map->rnum = 0;
  return map;
}


/* Store a record with an allocated region. */
void cbmapputvbuf(CBMAP *map, const char *kbuf, int ksiz, char *vbuf, int vsiz){
  CBMAPDATUM *datum, **entp;
  int bidx, hash, kcmp;
  assert(map && kbuf && ksiz >= 0 && vbuf && vsiz >= 0);
  bidx = cbfirsthash(kbuf, ksiz) % map->bnum;
  datum = map->buckets[bidx];
  entp = map->buckets + bidx;
  hash = cbsecondhash(kbuf, ksiz);
  while(datum){
    if(hash > datum->hash){
      entp = (CBMAPDATUM **)&(datum->left);
      datum = (CBMAPDATUM *)datum->left;
    } else if(hash < datum->hash){
      entp = (CBMAPDATUM **)&(datum->right);
      datum = (CBMAPDATUM *)datum->right;
    } else {
      kcmp = cbkeycmp(kbuf, ksiz, datum->kbuf, datum->ksiz);
      if(kcmp < 0){
        entp = (CBMAPDATUM **)&(datum->left);
        datum = (CBMAPDATUM *)datum->left;
      } else if(kcmp > 0){
        entp = (CBMAPDATUM **)&(datum->right);
        datum = (CBMAPDATUM *)datum->right;
      } else {
        free(datum->vbuf);
        datum->vbuf = vbuf;
        datum->vsiz = vsiz;
        return;
      }
    }
  }
  CB_MALLOC(datum, sizeof(*datum));
  datum->kbuf = cbmemdup(kbuf, ksiz);
  datum->ksiz = ksiz;
  datum->vbuf = vbuf;
  datum->vsiz = vsiz;
  datum->hash = hash;
  datum->left = NULL;
  datum->right = NULL;
  datum->prev = (char *)map->last;
  datum->next = NULL;
  *entp = datum;
  if(!map->first) map->first = datum;
  if(map->last) map->last->next = (char *)datum;
  map->last = datum;
  map->rnum++;
}



/*************************************************************************************************
 * private objects
 *************************************************************************************************/


/* Handler to invoke the global garbage collector. */
static void cbggchandler(void){
  cbggckeeper(NULL, NULL);
}


/* Manage resources of the global garbage collector.
   `ptr' specifies the pointer to add to the collection.  If it is `NULL', all resources are
   released.
   `func' specifies the pointer to the function to release the resources. */
static void cbggckeeper(void *ptr, void (*func)(void *)){
  static void **parray = NULL;
  static void (**farray)(void *) = NULL;
  static int onum = 0;
  static int asiz = CB_GCUNIT;
  int i;
  if(!ptr){
    if(!parray) return;
    for(i = onum - 1; i >= 0; i--){
      farray[i](parray[i]);
    }
    free(parray);
    free(farray);
    parray = NULL;
    farray = NULL;
    onum = 0;
    asiz = CB_GCUNIT;
    return;
  }
  if(!parray){
    CB_MALLOC(parray, sizeof(void *) * asiz);
    CB_MALLOC(farray, sizeof(void *) * asiz);
    if(atexit(cbggchandler) != 0){
      if(cbfatalfunc){
        cbfatalfunc("gc failed");
      } else {
        cbmyfatal("gc failed");
      }
    }
  }
  if(onum >= asiz){
    asiz *= 2;
    CB_REALLOC(parray, sizeof(void *) * asiz);
    CB_REALLOC(farray, sizeof(void *) * asiz);
  }
  parray[onum] = ptr;
  farray[onum] = func;
  onum++;
}


/* Utility function for quick sort.
   `bp' specifies the pointer to the pointer to an array.
   `nmemb' specifies the number of elements of the array.
   `size' specifies the size of each element.
   `pswap' specifies the pointer to the swap region for a pivot.
   `vswap' specifies the pointer to the swap region for elements.
   `compar' specifies the pointer to comparing function. */
static void cbqsortsub(char *bp, int nmemb, int size, char *pswap, char *vswap,
                       int(*compar)(const void *, const void *)){
  int top, bottom;
  assert(bp && nmemb >= 0 && size > 0 && pswap && vswap && compar);
  if(nmemb < 10){
    if(nmemb > 1) cbisort(bp, nmemb, size, compar);
    return;
  }
  top = 0;
  bottom = nmemb - 1;
  memcpy(pswap, bp + (nmemb / 2) * size, size);
  while(top - 1 < bottom){
    if(compar(bp + top * size, pswap) < 0){
      top++;
    } else if(compar(bp + bottom * size, pswap) > 0){
      bottom--;
    } else {
      if(top != bottom){
        memcpy(vswap, bp + top * size, size);
        memcpy(bp + top * size, bp + bottom * size, size);
        memcpy(bp + bottom * size, vswap, size);
      }
      top++;
      bottom--;
    }
  }
  cbqsortsub(bp, top, size, pswap, vswap, compar);
  cbqsortsub(bp + (bottom + 1) * size, nmemb - bottom - 1, size, pswap, vswap, compar);
}


/* Compare two list elements.
   `a' specifies the pointer to one element.
   `b' specifies the pointer to the other element.
   The return value is positive if a is big, negative if b is big, else, it is 0. */
static int cblistelemcmp(const void *a, const void *b){
  int i, size;
  CBLISTDATUM *ap, *bp;
  char *ao, *bo;
  assert(a && b);
  ap = (CBLISTDATUM *)a;
  bp = (CBLISTDATUM *)b;
  ao = ap->dptr;
  bo = bp->dptr;
  size = ap->dsize < bp->dsize ? ap->dsize : bp->dsize;
  for(i = 0; i < size; i++){
    if(ao[i] > bo[i]) return 1;
    if(ao[i] < bo[i]) return -1;
  }
  return ap->dsize - bp->dsize;
}


/* Get the first hash value.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the key.
   The return value is 31 bit hash value of the key. */
static int cbfirsthash(const char *kbuf, int ksiz){
  const unsigned char *p;
  unsigned int sum;
  int i;
  assert(kbuf && ksiz >= 0);
  p = (const unsigned char *)kbuf;
  sum = 751;
  for(i = 0; i < ksiz; i++){
    sum = sum * 31 + p[i];
  }
  return (sum * 87767623) & 0x7FFFFFFF;
}


/* Get the second hash value.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the key.
   The return value is 31 bit hash value of the key. */
static int cbsecondhash(const char *kbuf, int ksiz){
  const unsigned char *p;
  unsigned int sum;
  int i;
  assert(kbuf && ksiz >= 0);
  p = (const unsigned char *)kbuf;
  sum = 19780211;
  for(i = ksiz - 1; i >= 0; i--){
    sum = sum * 37 + p[i];
  }
  return (sum * 43321879) & 0x7FFFFFFF;
}


/* Compare two keys.
   `abuf' specifies the pointer to the region of the former.
   `asiz' specifies the size of the region.
   `bbuf' specifies the pointer to the region of the latter.
   `bsiz' specifies the size of the region.
   The return value is 0 if two equals, positive if the formar is big, else, negative. */
static int cbkeycmp(const char *abuf, int asiz, const char *bbuf, int bsiz){
  assert(abuf && asiz >= 0 && bbuf && bsiz >= 0);
  if(asiz > bsiz) return 1;
  if(asiz < bsiz) return -1;
  return memcmp(abuf, bbuf, asiz);
}


/* Set a buffer for a variable length number.
   `buf' specifies the pointer to the buffer.
   `num' specifies the number.
   The return value is the size of valid region. */
static int cbsetvnumbuf(char *buf, int num){
  div_t d;
  int len;
  assert(buf && num >= 0);
  if(num == 0){
    ((signed char *)buf)[0] = 0;
    return 1;
  }
  len = 0;
  while(num > 0){
    d = div(num, 128);
    num = d.quot;
    ((signed char *)buf)[len] = d.rem;
    if(num > 0) ((signed char *)buf)[len] = -(((signed char *)buf)[len]) - 1;
    len++;
  }
  return len;
}


/* Read a variable length buffer.
   `buf' specifies the pointer to the buffer.
   `size' specifies the limit size to read.
   `sp' specifies the pointer to a variable to which the size of the read region assigned.
   The return value is the value of the buffer. */
static int cbreadvnumbuf(const char *buf, int size, int *sp){
  int i, num, base;
  assert(buf && size > 0 && sp);
  num = 0;
  base = 1;
  if(size < 2){
    *sp = 1;
    return ((signed char *)buf)[0];
  }
  for(i = 0; i < size; i++){
    if(((signed char *)buf)[i] >= 0){
      num += ((signed char *)buf)[i] * base;
      break;
    }
    num += base * (((signed char *)buf)[i] + 1) * -1;
    base *= 128;
  }
  *sp = i + 1;
  return num;
}



/* END OF FILE */
