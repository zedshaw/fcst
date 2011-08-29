/*************************************************************************************************
 * The utitlity API of QDBM
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


#ifndef _CABIN_H                         /* duplication check */
#define _CABIN_H

#if defined(__cplusplus)                 /* export for C++ */
extern "C" {
#endif


#include <stdlib.h>
#include <time.h>



/*************************************************************************************************
 * API
 *************************************************************************************************/


typedef struct {                         /* type of structure for a basic datum */
  char *dptr;                            /* pointer to the region */
  int dsize;                             /* size of the region */
  int asize;                             /* size of the allocated region */
} CBDATUM;

typedef struct {                         /* type of structure for an element of a list */
  char *dptr;                            /* pointer to the region */
  int dsize;                             /* size of the effective region */
} CBLISTDATUM;

typedef struct {                         /* type of structure for a list */
  CBLISTDATUM *array;                    /* array of data */
  int anum;                              /* number of the elements of the array */
  int start;                             /* start index of using elements */
  int num;                               /* number of using elements */
} CBLIST;

typedef struct {                         /* type of structure for an element of a map */
  char *kbuf;                            /* pointer to the region of the key */
  int ksiz;                              /* size of the region of the key */
  char *vbuf;                            /* pointer to the region of the value */
  int vsiz;                              /* size of the region of the value */
  int hash;                              /* second hash value */
  char *left;                            /* pointer to the left child */
  char *right;                           /* pointer to the right child */
  char *prev;                            /* pointer to the previous element */
  char *next;                            /* pointer to the next element */
} CBMAPDATUM;

typedef struct {                         /* type of structure for a map */
  CBMAPDATUM **buckets;                  /* bucket array */
  CBMAPDATUM *first;                     /* pointer to the first element */
  CBMAPDATUM *last;                      /* pointer to the last element */
  CBMAPDATUM *cur;                       /* pointer to the current element */
  int bnum;                              /* number of buckets */
  int rnum;                              /* number of records */
} CBMAP;


/* Call back function for handling a fatal error.
   The argument specifies the error message.  The initial value of this variable is `NULL'.
   If the value is `NULL', the default function is called when a fatal error occurs. A fatal
   error occurs when memory allocation is failed. */
extern void (*cbfatalfunc)(const char *);


/* Allocate a region on memory.
   `size' specifies the size of the region.
   The return value is the pointer to the allocated region.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
void *cbmalloc(size_t size);


/* Re-allocate a region on memory.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.
   The return value is the pointer to the re-allocated region.
   Because the region of the return value is allocated with the `realloc' call, it should be
   released with the `free' call if it is no longer in use. */
void *cbrealloc(void *ptr, size_t size);


/* Duplicate a region on memory.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the pointer to the allocated region of the duplicate.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if
   it is no longer in use. */
char *cbmemdup(const char *ptr, int size);


/* Register the pointer or handle of an object to the global garbage collector.
   `ptr' specifies the pointer or handle of an object.
   `func' specifies the pointer to a function to release resources of the object.  Its argument
   is the pointer or handle of the object to release.
   This function assures that resources of an object are released when the process exits
   normally by returning from the `main' function or calling the `exit' function. */
void cbglobalgc(void *ptr, void (*func)(void *));


/* Exercise the global garbage collector explicitly.
   Note that you should not use objects registered to the global garbage collector any longer
   after calling this function.  Because the global garbage collecter is initialized and you
   can register new objects into it. */
void cbggcsweep(void);


/* Sort an array using insert sort.
   `base' spacifies the pointer to an array.
   `nmemb' specifies the number of elements of the array.
   `size' specifies the size of each element.
   `compar' specifies the pointer to comparing function.  The two arguments specify the pointers
   of elements.  The comparing function should returns positive if the former is big, negative
   if the latter is big, 0 if both are equal.
   Insert sort is useful only if most elements have been sorted already. */
void cbisort(void *base, int nmemb, int size, int(*compar)(const void *, const void *));


/* Sort an array using shell sort.
   `base' spacifies the pointer to an array.
   `nmemb' specifies the number of elements of the array.
   `size' specifies the size of each element.
   `compar' specifies the pointer to comparing function.  The two arguments specify the pointers
   of elements.  The comparing function should returns positive if the former is big, negative
   if the latter is big, 0 if both are equal.
   If most elements have been sorted, shell sort may be faster than heap sort or quick sort. */
void cbssort(void *base, int nmemb, int size, int(*compar)(const void *, const void *));


/* Sort an array using heap sort.
   `base' spacifies the pointer to an array.
   `nmemb' specifies the number of elements of the array.
   `size' specifies the size of each element.
   `compar' specifies the pointer to comparing function.  The two arguments specify the pointers
   of elements.  The comparing function should returns positive if the former is big, negative
   if the latter is big, 0 if both are equal.
   Although heap sort is robust against bias of input, quick sort is faster in most cases. */
void cbhsort(void *base, int nmemb, int size, int(*compar)(const void *, const void *));


/* Sort an array using quick sort.
   `base' spacifies the pointer to an array.
   `nmemb' specifies the number of elements of the array.
   `size' specifies the size of each element.
   `compar' specifies the pointer to comparing function.  The two arguments specify the pointers
   of elements.  The comparing function should returns positive if the former is big, negative
   if the latter is big, 0 if both are equal.
   Being sensitive to bias of input, quick sort is the fastest sorting algorithm. */
void cbqsort(void *base, int nmemb, int size, int(*compar)(const void *, const void *));


/* Compare two strings with case insensitive evaluation.
   `astr' specifies the pointer of one string.
   `astr' specifies the pointer of the other string.
   The return value is positive if the former is big, negative if the latter is big, 0 if both
   are equivalent.
   Upper cases and lower cases of alphabets in ASCII code are not distinguished. */
int cbstricmp(const char *astr, const char *bstr);


/* Check whether a string begins with a key.
   `str' specifies the pointer of a target string.
   `key' specifies the pointer of a forward matching key string.
   The return value is true if the target string begins with the key, else, it is false. */
int cbstrfwmatch(const char *str, const char *key);


/* Check whether a string begins with a key, with case insensitive evaluation.
   `str' specifies the pointer of a target string.
   `key' specifies the pointer of a forward matching key string.
   The return value is true if the target string begins with the key, else, it is false.
   Upper cases and lower cases of alphabets in ASCII code are not distinguished. */
int cbstrfwimatch(const char *str, const char *key);


/* Check whether a string ends with a key.
   `str' specifies the pointer of a target string.
   `key' specifies the pointer of a backward matching key string.
   The return value is true if the target string ends with the key, else, it is false. */
int cbstrbwmatch(const char *str, const char *key);


/* Check whether a string ends with a key, with case insensitive evaluation.
   `str' specifies the pointer of a target string.
   `key' specifies the pointer of a backward matching key string.
   The return value is true if the target string ends with the key, else, it is false.
   Upper cases and lower cases of alphabets in ASCII code are not distinguished. */
int cbstrbwimatch(const char *str, const char *key);


/* Convert the letters of a string to upper case.
   `str' specifies the pointer of a string to convert.
   The return value is the pointer to the string. */
char *cbstrtoupper(char *str);


/* Convert the letters of a string to lower case.
   `str' specifies the pointer of a string to convert.
   The return value is the pointer to the string. */
char *cbstrtolower(char *str);


/* Cut space characters at head or tail of a string.
   `str' specifies the pointer of a string to convert.
   The return value is the pointer to the string. */
char *cbstrtrim(char *str);


/* Squeeze space characters in a string and trim it.
   `str' specifies the pointer of a string to convert.
   The return value is the pointer to the string. */
char *cbstrsqzspc(char *str);


/* Get a datum handle.
   `ptr' specifies the pointer to the region of the initial content.  If it is `NULL', an empty
   datum is created.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is a datum handle. */
CBDATUM *cbdatumopen(const char *ptr, int size);


/* Copy a datum.
   `datum' specifies a datum handle.
   The return value is a new datum handle. */
CBDATUM *cbdatumdup(const CBDATUM *datum);


/* Free a datum handle.
   `datum' specifies a datum handle.
   Because the region of a closed handle is released, it becomes impossible to use the handle. */
void cbdatumclose(CBDATUM *datum);


/* Concatenate a datum and a region.
   `datum' specifies a datum handle.
   `ptr' specifies the pointer to the region to be appended.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'. */
void cbdatumcat(CBDATUM *datum, const char *ptr, int size);


/* Get the pointer of the region of a datum.
   `datum' specifies a datum handle.
   The return value is the pointer of the region of a datum.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string. */
const char *cbdatumptr(const CBDATUM *datum);


/* Get the size of the region of a datum.
   `datum' specifies a datum handle.
   The return value is the size of the region of a datum. */
int cbdatumsize(const CBDATUM *datum);


/* Change the size of the region of a datum.
   `datum' specifies a datum handle.
   `size' specifies the new size of the region.
   If the new size is bigger than the one of old, the surplus region is filled with zero codes. */
void cbdatumsetsize(CBDATUM *datum, int size);


/* Convert a datum to an allocated region.
   `datum' specifies a datum handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the datum.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  Because the region of the original datam is released, it should not be
   released again. */
char *cbdatumtomalloc(CBDATUM *datum, int *sp);


/* Get a list handle.
   The return value is a list handle. */
CBLIST *cblistopen(void);


/* Copy a list.
   `list' specifies a list handle.
   The return value is a new list handle. */
CBLIST *cblistdup(const CBLIST *list);


/* Close a list handle.
   `list' specifies a list handle.
   Because the region of a closed handle is released, it becomes impossible to use the handle. */
void cblistclose(CBLIST *list);


/* Get the number of elements of a list.
   `list' specifies a list handle.
   The return value is the number of elements of the list. */
int cblistnum(const CBLIST *list);


/* Get the pointer to the region of an element.
   `list' specifies a list handle.
   `index' specifies the index of an element.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the value.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  If `index' is equal to or more than
   the number of elements, the return value is `NULL'. */
const char *cblistval(const CBLIST *list, int index, int *sp);


/* Add an element at the end of a list.
   `list' specifies a list handle.
   `ptr' specifies the pointer to the region of an element.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'. */
void cblistpush(CBLIST *list, const char *ptr, int size);


/* Remove an element of the end of a list.
   `list' specifies a list handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the value.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  If the list is empty, the return value is `NULL'. */
char *cblistpop(CBLIST *list, int *sp);


/* Add an element at the top of a list.
   `list' specifies a list handle.
   `ptr' specifies the pointer to the region of an element.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'. */
void cblistunshift(CBLIST *list, const char *ptr, int size);


/* Remove an element of the top of a list.
   `list' specifies a list handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the value.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  If the list is empty, the return value is `NULL'. */
char *cblistshift(CBLIST *list, int *sp);


/* Add an element at the specified location of a list.
   `list' specifies a list handle.
   `index' specifies the index of an element.
   `ptr' specifies the pointer to the region of the element.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'. */
void cblistinsert(CBLIST *list, int index, const char *ptr, int size);


/* Remove an element at the specified location of a list.
   `list' specifies a list handle.
   `index' specifies the index of an element.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the value.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  If `index' is equal to or more than the number of elements, no element
   is removed and the return value is `NULL'. */
char *cblistremove(CBLIST *list, int index, int *sp);


/* Overwrite an element at the specified location of a list.
   `list' specifies a list handle.
   `index' specifies the index of an element.
   `ptr' specifies the pointer to the region of the new content.
   `size' specifies the size of the new content.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   If `index' is equal to or more than the number of elements, this function has no effect. */
void cblistover(CBLIST *list, int index, const char *ptr, int size);


/* Sort elements of a list in lexical order.
   `list' specifies a list handle.
   Quick sort is used for sorting. */
void cblistsort(CBLIST *list);


/* Search a list for an element using liner search.
   `list' specifies a list handle.
   `ptr' specifies the pointer to the region of a key.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the index of a corresponding element or -1 if there is no corresponding
   element.  If two or more elements corresponds, the former returns. */
int cblistlsearch(const CBLIST *list, const char *ptr, int size);


/* Search a list for an element using binary search.
   `list' specifies a list handle.  It should be sorted in lexical order.
   `ptr' specifies the pointer to the region of a key.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the index of a corresponding element or -1 if there is no corresponding
   element.  If two or more elements corresponds, which returns is not defined. */
int cblistbsearch(const CBLIST *list, const char *ptr, int size);


/* Serialize a list into a byte array.
   `list' specifies a list handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.
   The return value is the pointer to the region of the result serial region.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cblistdump(const CBLIST *list, int *sp);


/* Redintegrate a serialized list.
   `ptr' specifies the pointer to a byte array.
   `size' specifies the size of the region.
   The return value is a new list handle. */
CBLIST *cblistload(const char *ptr, int size);


/* Get a map handle.
   The return value is a map handle. */
CBMAP *cbmapopen(void);


/* Copy a map.
   `map' specifies a map handle.
   The return value is a new map handle.
   The iterator of the source map is initialized. */
CBMAP *cbmapdup(CBMAP *map);


/* Close a map handle.
   `map' specifies a map handle.
   Because the region of a closed handle is released, it becomes impossible to use the handle. */
void cbmapclose(CBMAP *map);


/* Store a record.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.  If it is negative, the size is
   assigned with `strlen(vbuf)'.
   `over' specifies whether the value of the duplicated record is overwritten or not.
   If `over' is false and the key is duplicated, the return value is false, else, it is true. */
int cbmapput(CBMAP *map, const char *kbuf, int ksiz, const char *vbuf, int vsiz, int over);


/* Concatenate a value at the end of the value of the existing record.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `vbuf' specifies the pointer to the region of a value.
   `vsiz' specifies the size of the region of the value.  If it is negative, the size is
   assigned with `strlen(vbuf)'.
   If there is no corresponding record, a new record is created. */
void cbmapputcat(CBMAP *map, const char *kbuf, int ksiz, const char *vbuf, int vsiz);


/* Delete a record.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   If successful, the return value is true.  False is returned when no record corresponds to
   the specified key. */
int cbmapout(CBMAP *map, const char *kbuf, int ksiz);


/* Retrieve a record.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the value of the
   corresponding record.  `NULL' is returned when no record corresponds.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string. */
const char *cbmapget(const CBMAP *map, const char *kbuf, int ksiz, int *sp);


/* Move a record to the edge.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.  If it is negative, the size is assigned
   with `strlen(kbuf)'.
   `head' specifies the destination which is head if it is true or tail if else.
   If successful, the return value is true.  False is returned when no record corresponds to
   the specified key. */
int cbmapmove(CBMAP *map, const char *kbuf, int ksiz, int head);


/* Initialize the iterator of a map handle.
   `map' specifies a map handle.
   The iterator is used in order to access the key of every record stored in a map. */
void cbmapiterinit(CBMAP *map);


/* Get the next key of the iterator.
   `map' specifies a map handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the region of the next key, else, it is
   `NULL'.  `NULL' is returned when no record is to be get out of the iterator.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  The order of iteration is assured
   to be the same of the one of storing. */
const char *cbmapiternext(CBMAP *map, int *sp);


/* Get the number of the records stored in a map.
   `map' specifies a map handle.
   The return value is the number of the records stored in the map. */
int cbmaprnum(const CBMAP *map);


/* Get the list handle contains all keys in a map.
   `map' specifies a map handle.
   The return value is the list handle contains all keys in the map.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbmapkeys(CBMAP *map);


/* Get the list handle contains all values in a map.
   `map' specifies a map handle.
   The return value is the list handle contains all values in the map.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbmapvals(CBMAP *map);


/* Serialize a map into a byte array.
   `map' specifies a map handle.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.
   The return value is the pointer to the region of the result serial region.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbmapdump(CBMAP *map, int *sp);


/* Redintegrate a serialized map.
   `ptr' specifies the pointer to a byte array.
   `size' specifies the size of the region.
   The return value is a new map handle. */
CBMAP *cbmapload(const char *ptr, int size);


/* Allocate a formatted string on memory.
   `format' specifies a printf-like format string.  The conversion character `%' can be used
   with such flag characters as `d', `o', `u', `x', `X', `e', `E', `f', `g', `G', `c', `s', and
   `%'.  Specifiers of the field length and the precision can be put between the conversion
   characters and the flag characters.  The specifiers consist of decimal characters, `.', `+',
   `-', and the space character.
   The other arguments are used according to the format string.
   The return value is the pointer to the allocated region of the result string.  Because the
   region of the return value is allocated with the `malloc' call, it should be released with
   the `free' call if it is no longer in use. */
char *cbsprintf(const char *format, ...);


/* Replace some patterns in a string.
   `str' specifies the pointer to a source string.
   `pairs' specifies the handle of a map composed of pairs of replacement.  The key of each pair
   specifies a pattern before replacement and its value specifies the pattern after replacement.
   The return value is the pointer to the allocated region of the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbreplace(const char *str, CBMAP *pairs);


/* Make a list by splitting a serial datum.
   `ptr' specifies the pointer to the region of the source content.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   `delim' specifies a string containing delimiting characters.  If it is `NULL', zero code is
   used as a delimiter.
   The return value is a list handle.
   If two delimiters are successive, it is assumed that an empty element is between the two.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose'. */
CBLIST *cbsplit(const char *ptr, int size, const char *delim);


/* Read whole data of a file.
   `name' specifies the name of a file.  If it is `NULL', the standard input is specified.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the allocated region of the read data.  Because an
   additional zero code is appended at the end of the region of the return value, the return
   value can be treated as a character string.  Because the region of the return value is
   allocated with the `malloc' call, it should be released with the `free' call if it is no
   longer in use.  */
char *cbreadfile(const char *name, int *sp);


/* Write a serial datum into a file.
   `name specifies the name of a file.  If it is `NULL', the standard output is specified.
   `ptr' specifies the pointer to the region of the source content.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   If successful, the return value is true, else, it is false.
   If the file exists, it is overwritten.  Else, a new file is created. */
int cbwritefile(const char *name, const char *ptr, int size);


/* Read every line of a file.
   `name' specifies the name of a file.  If it is `NULL', the standard input is specified.
   The return value is a list handle of the lines if successful, else it is NULL.  Line
   separators are cut out.  Because the handle of the return value is opened with the function
   `cblistopen', it should be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbreadlines(const char *name);


/* Read names of files in a directory.
   `name' specifies the name of a directory.
   The return value is a list handle of names if successful, else it is NULL.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbdirlist(const char *name);


/* Get the status of a file or a directory.
   `name' specifies the name of a file or a directory.
   `dirp' specifies the pointer to a variable to which whether the file is a directory is
   assigned.  If it is `NULL', it is not used.
   `sizep' specifies the pointer to a variable to which the size of the file is assigned.  If it
   is `NULL', it is not used.
   `mtimep' specifies the pointer to a variable to which the last modified time of the file is
   assigned.  If it is `NULL', it is not used.
   If successful, the return value is true, else, false.  False is returned when the file does
   not exist or the permission is denied. */
int cbfilestat(const char *name, int *isdirp, int *sizep, int *mtimep);


/* Break up a URL into elements.
   `str' specifies the pointer to a string of URL.
   The return value is a map handle.  Each key of the map is the name of an element.  The key
   "self" specifies the URL itself.  The key "scheme" specifies the scheme.  The key "host"
   specifies the host of the server.  The key "port" specifies the port number of the server.
   The key "authority" specifies the authority information.  The key "path" specifies the path
   of the resource.  The key "file" specifies the file name without the directory section.  The
   key "query" specifies the query string.  The key "fragment" specifies the fragment string.
   Supported schema are HTTP, HTTPS, FTP, and FILE.  Absolute URL and relative URL are supported.
   Because the handle of the return value is opened with the function `cbmapopen', it should
   be closed with the function `cbmapclose' if it is no longer in use. */
CBMAP *cburlbreak(const char *str);


/* Encode a serial object with URL encoding.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the pointer to the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cburlencode(const char *ptr, int size);


/* Decode a string encoded with URL encoding.
   `str' specifies the pointer to an encoded string.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the result.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if
   it is no longer in use. */
char *cburldecode(const char *str, int *sp);


/* Encode a serial object with Base64 encoding.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the pointer to the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbbaseencode(const char *ptr, int size);


/* Decode a string encoded with Base64 encoding.
   `str' specifies the pointer to an encoded string.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the result.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if
   it is no longer in use. */
char *cbbasedecode(const char *str, int *sp);


/* Encode a serial object with quoted-printable encoding.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the pointer to the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbquoteencode(const char *ptr, int size);


/* Decode a string encoded with quoted-printable encoding.
   `str' specifies the pointer to an encoded string.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer to the region of the result.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if
   it is no longer in use. */
char *cbquotedecode(const char *str, int *sp);


/* Split a string of MIME into headers and the body.
   `ptr' specifies the pointer to the region of MIME data.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   `attrs' specifies a map handle to store attributes.  If it is `NULL', it is not used.  Each
   key of the map is an attribute name uncapitalized.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   The return value is the pointer of the body data.
   If the content type is defined, the attribute map has the key "TYPE" specifying the type.  If
   the character encoding is defined, the key "CHARSET" specifies the encoding name.  If the
   boundary string of multipart is defined, the key "BOUNDARY" specifies the string.  If the
   content disposition is defined, the key "DISPOSITION" specifies the direction.  If the file
   name is defined, the key "FILENAME" specifies the name.  If the attribute name is defined,
   the key "NAME" specifies the name.  Because the region of the return value is allocated with
   the `malloc' call, it should be released with the `free' call if it is no longer in use. */
char *cbmimebreak(const char *ptr, int size, CBMAP *attrs, int *sp);


/* Split multipart data of MIME into its parts.
   `ptr' specifies the pointer to the region of multipart data of MIME.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   `boundary' specifies the pointer to the region of the boundary string.
   The return value is a list handle.  Each element of the list is the string of a part.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbmimeparts(const char *ptr, int size, const char *boundary);


/* Encode a string with MIME encoding.
   `str' specifies the pointer to a string.
   `encname' specifies a string of the name of the character encoding.
   The return value is the pointer to the result string.
   `base' specifies whether to use Base64 encoding.  If it is false, quoted-printable is used.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbmimeencode(const char *str, const char *encname, int base);


/* Decode a string encoded with MIME encoding.
   `str' specifies the pointer to an encoded string.
   `enp' specifies the pointer to a region into which the name of encoding is written.  If it is
   `NULL', it is not used.  The size of the buffer should be equal to or more than 32 bytes.
   The return value is the pointer to the result string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbmimedecode(const char *str, char *enp);


/* Split a string of CSV into rows.
   `str' specifies the pointer to the region of an CSV string.
   The return value is a list handle.  Each element of the list is a string of a row.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use.  The character encoding
   of the input string should be US-ASCII, UTF-8, ISO-8859-*, EUC-*, or Shift_JIS.  Being
   compatible with MS-Excel, these functions for CSV can handle cells including such meta
   characters as comma, between double quotation marks. */
CBLIST *cbcsvrows(const char *str);


/* Split the string of a row of CSV into cells.
   `str' specifies the pointer to the region of a row of CSV.
   The return value is a list handle.  Each element of the list is the unescaped string of a
   cell of the row.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use. */
CBLIST *cbcsvcells(const char *str);


/* Escape a string with the meta characters of CSV.
   `str' specifies the pointer to the region of a string.
   The return value is the pointer to the escaped string sanitized of meta characters.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbcsvescape(const char *str);


/* Unescape a string with the escaped meta characters of CSV.
   `str' specifies the pointer to the region of a string with meta characters.
   The return value is the pointer to the unescaped string.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbcsvunescape(const char *str);


/* Split a string of XML into tags and text sections.
   `str' specifies the pointer to the region of an XML string.
   `cr' specifies whether to remove comments.
   The return value is a list handle.  Each element of the list is the string of a tag or a
   text section.
   Because the handle of the return value is opened with the function `cblistopen', it should
   be closed with the function `cblistclose' if it is no longer in use.  The character encoding
   of the input string should be US-ASCII, UTF-8, ISO-8859-*, EUC-*, or Shift_JIS.  Because
   these functions for XML are not XML parser with validation check, it can handle also HTML
   and SGML. */
CBLIST *cbxmlbreak(const char *str, int cr);


/* Get the map of attributes of an XML tag.
   `str' specifies the pointer to the region of a tag string.
   The return value is a map handle.  Each key of the map is the name of an attribute.  Each
   value is unescaped.  You can get the name of the tag with the key of an empty string.
   Because the handle of the return value is opened with the function `cbmapopen', it should
   be closed with the function `cbmapclose' if it is no longer in use. */
CBMAP *cbxmlattrs(const char *str);


/* Escape a string with the meta characters of XML.
   `str' specifies the pointer to the region of a string.
   The return value is the pointer to the escaped string sanitized of meta characters.
   This function converts only `&', `<', `>', and `"'.  Because the region of the return value
   is allocated with the `malloc' call, it should be released with the `free' call if it is no
   longer in use. */
char *cbxmlescape(const char *str);


/* Unescape a string with the entity references of XML.
   `str' specifies the pointer to the region of a string with meta characters.
   The return value is the pointer to the unescaped string.
   This function restores only `&amp;', `&lt;', `&gt;', and `&quot;'.  Because the region of the
   return value is allocated with the `malloc' call, it should be released with the `free' call
   if it is no longer in use. */
char *cbxmlunescape(const char *str);


/* Compress a serial object with ZLIB.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.
   If successful, the return value is the pointer to the result object, else, it is `NULL'.
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use.  This function is available only if
   QDBM was built with ZLIB enabled. */
char *cbdeflate(const char *ptr, int size, int *sp);


/* Decompress a serial object compressed with ZLIB.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the result object, else, it is `NULL'.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  This function is available only if QDBM was built with ZLIB enabled. */
char *cbinflate(const char *ptr, int size, int *sp);


/* Get the CRC32 checksum of a serial object.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the CRC32 checksum of the object.
   This function is available only if QDBM was built with ZLIB enabled. */
unsigned int cbgetcrc(const char *ptr, int size);


/* Convert the character encoding of a string.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   `icode' specifies the name of encoding of the input string.
   `ocode' specifies the name of encoding of the output string.
   `sp' specifies the pointer to a variable to which the size of the region of the return
   value is assigned.  If it is `NULL', it is not used.
   `mp' specifies the pointer to a variable to which the number of missing characters by failure
   of conversion is assigned.  If it is `NULL', it is not used.
   If successful, the return value is the pointer to the result object, else, it is `NULL'.
   Because an additional zero code is appended at the end of the region of the return value,
   the return value can be treated as a character string.  Because the region of the return
   value is allocated with the `malloc' call, it should be released with the `free' call if it
   is no longer in use.  This function is available only if QDBM was built with ICONV enabled. */
char *cbiconv(const char *ptr, int size, const char *icode, const char *ocode, int *sp, int *mp);


/* Detect the encoding of a string automatically.
   `ptr' specifies the pointer to a region.
   `size' specifies the size of the region.  If it is negative, the size is assigned with
   `strlen(ptr)'.
   The return value is the string of the encoding name of the string.
   As it stands, US-ASCII, ISO-2022-JP, Shift_JIS, CP932, EUC-JP, UTF-8, UTF-16, UTF-16BE,
   and UTF-16LE are supported.  If none of them matches, ISO-8859-1 is selected.  This function
   is available only if QDBM was built with ICONV enabled. */
const char *cbencname(const char *ptr, int size);


/* Get the jet lag of the local time in seconds.
   The return value is the jet lag of the local time in seconds. */
int cbjetlag(void);


/* Get the Gregorian calendar of a time.
   `t' specifies a source time.  If it is negative, the current time is specified.
   `jl' specifies the jet lag of a location in seconds.
   `yearp' specifies the pointer to a variable to which the year is assigned.  If it is `NULL',
   it is not used.
   `monp' specifies the pointer to a variable to which the month is assigned.  If it is `NULL',
   it is not used.  1 means January and 12 means December.
   `dayp' specifies the pointer to a variable to which the day of the month is assigned.  If it
   is `NULL', it is not used.
   `hourp' specifies the pointer to a variable to which the hours is assigned.  If it is `NULL',
   it is not used.
   `minp' specifies the pointer to a variable to which the minutes is assigned.  If it is `NULL',
   it is not used.
   `secp' specifies the pointer to a variable to which the seconds is assigned.  If it is `NULL',
   it is not used. */
void cbcalendar(time_t t, int jl, int *yearp, int *monp, int *dayp,
                int *hourp, int *minp, int *secp);


/* Get the day of week of a date.
   `year' specifies the year of a date.
   `mon' specifies the month of the date.
   `day' specifies the day of the date.
   The return value is the day of week of the date.  0 means Sunday and 6 means Saturday. */
int cbdayofweek(int year, int mon, int day);


/* Get the string for a date in W3CDTF.
   `t' specifies a source time.  If it is negative, the current time is specified.
   `jl' specifies the jet lag of a location in seconds.
   The return value is the string of the date in W3CDTF (YYYY-MM-DDThh:mm:ddTZD).
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbdatestrwww(time_t t, int jl);


/* Get the string for a date in RFC 1123 format.
   `t' specifies a source time.  If it is negative, the current time is specified.
   `jl' specifies the jet lag of a location in seconds.
   The return value is the string of the date in RFC 1123 format (Wdy, DD-Mon-YYYY hh:mm:dd TZD).
   Because the region of the return value is allocated with the `malloc' call, it should be
   released with the `free' call if it is no longer in use. */
char *cbdatestrhttp(time_t t, int jl);


/* Get the time value of a date string in decimal, hexadecimal, W3CDTF, or RFC 822 (1123).
   `str' specifies a date string in decimal, hexadecimal, W3CDTF, or RFC 822 (1123).
   The return value is the time value of the date or -1 if the format is invalid. */
time_t cbstrmktime(const char *str);


/* Get user and system processing times.
   `usrp' specifies the pointer to a variable to which the user processing time is assigned.
   If it is `NULL', it is not used.  The unit of time is seconds.
   `sysp' specifies the pointer to a variable to which the system processing time is assigned.
   If it is `NULL', it is not used.  The unit of time is seconds. */
void cbproctime(double *usrp, double *sysp);


/* Ensure that the standard I/O is binary mode.
   This function is useful for applications on dosish file systems. */
void cbstdiobin(void);



/*************************************************************************************************
 * features for experts
 *************************************************************************************************/


/* Show error message on the standard error output and exit.
   `message' specifies an error message. */
void cbmyfatal(const char *message);


/* Add an allocated element at the end of a list.
   `list' specifies a list handle.
   `ptr' specifies the pointer to the region of an element.  The region should be allocated with
   malloc and it is released by the function.
   `size' specifies the size of the region. */
void cblistpushbuf(CBLIST *list, char *ptr, int size);


/* Get a map handle with specifying the number of buckets.
   `bnum' specifies the number of buckets.
   The return value is a map handle. */
CBMAP *cbmapopenex(int bnum);


/* Store a record with an allocated region.
   `map' specifies a map handle.
   `kbuf' specifies the pointer to the region of a key.
   `ksiz' specifies the size of the region of the key.
   `vbuf' specifies the pointer to the region of a value.  The region should be allocated with
   malloc and it is released by the function.
   `vsiz' specifies the size of the region of the value.
   If the key overlaps, the existing record is overwritten. */
void cbmapputvbuf(CBMAP *map, const char *kbuf, int ksiz, char *vbuf, int vsiz);


/* Alias of `cbmalloc'. */
#define CB_MALLOC(ptr, size) \
  (((ptr) = malloc(size)) ? (ptr) : \
    ((cbfatalfunc ? cbfatalfunc("out of memory") : cbmyfatal("out of memory")), NULL))


/* Alias of `cbrealloc'. */
#define CB_REALLOC(ptr, size) \
  (((ptr) = realloc((ptr), (size))) ? (ptr) : \
    ((cbfatalfunc ? cbfatalfunc("out of memory") : cbmyfatal("out of memory")), NULL))


/* Alias of `cbdatumptr'. */
#define CB_DATUMPTR(datum) ((const char *)(datum->dptr))


/* Alias of `cbdatumsize'. */
#define CB_DATUMSIZE(datum) ((int)(datum->dsize))


/* Alias of `cblistnum'. */
#define CB_LISTNUM(list) ((int)(list->num))


/* Alias of `cblistval'.
   However, `sp' is ignored. */
#define CB_LISTVAL(list,index,sp) ((const char *)(list->array[list->start+index].dptr))


/* Alias of `cblistval'.
   However, `sp' is needed. */
#define CB_LISTVAL2(list,index,sp) (*sp = list->array[list->start+index].dsize,\
  (const char *)(list->array[list->start+index].dptr))



#if defined(__cplusplus)                 /* export for C++ */
}
#endif

#endif                                   /* duplication check */


/* END OF FILE */
