#include <ruby.h>
#include <assert.h>
#include <sarray.h>

typedef struct SuffixArray {
    int *suffix_index;
    unsigned int ends[256];
    unsigned int starts[256];
} SuffixArray;


#define ERR_NO_ZERO_LENGTH_INPUT "Cannot create a suffix array from a 0 length input source."
#define ERR_NOT_INITIALIZED "Initialization failed, you cannot use this object."
#define ERR_START_IF_ARRAY "You must provide a start argument if you give an array argument."
#define ERR_MISMATCH_LENGTH "The raw array length is different from the source length"
static VALUE cSAError;


inline int scan_string(unsigned char *source, size_t src_len, 
                          unsigned char *target, size_t *tgt_len)
{
    size_t target_i = 0;
    size_t source_i = 0;
    size_t length = 0;
    
    while(target_i < *tgt_len && source_i < src_len && target[target_i] == source[source_i]) {
        length++;
        target_i++;
        source_i++;
    }
    

    if(target_i == *tgt_len) {
        // found a match that's at least as long as the target, so good enough
        *tgt_len = length;  // out parameter for the length that was found
        return 0;
    } else {
        // target and source characters are now different, return that difference 
        *tgt_len = length;  // out parameter for the length that was found
        return  target[target_i] - source[source_i];
    }
    
}

/**
 * Returns the index in the suffix array where where the longest match is found.
 * REMEMBER! It's the suffix array index.  If you want the source string  index
 * then you must do sa[start].
 */
size_t find_longest_match(unsigned char *source, size_t src_len, 
                          unsigned char *target, size_t *tgt_len, 
                          unsigned int starts[], unsigned int ends[], unsigned int sa[]) 
{
    size_t high = ends[*target] + 1;
    size_t low = starts[*target];
    size_t middle = (low + high) / 2;  // middle is pre-calculated so the while loop can exit
    size_t length = 0;
    size_t scan_len = 0;
    size_t src_i = 0;
    int result = 0;
    size_t last_match = 0;
    
    while(low <= high && high <= src_len && middle <= src_len && length != *tgt_len) {
        src_i = sa[middle];
        scan_len = *tgt_len;
        
        result = scan_string(source + src_i, src_len - src_i, target, &scan_len);
        
        if(scan_len > length)  {
            length = scan_len;
            last_match = middle;
        }
        
        if(result == 0)
            // found it so we're done
            break;
        else if(result < 0) {
            // it's less than our current mid-point so drop down
            high = middle - 1;
        } else {
            // it's greater than our current mid-point so push up
            low = middle + 1;
        }

        // recalculate the middle
        middle = (low + high) / 2;
    }
    
    *tgt_len = length;
    return last_match;
}


/*
 * call-seq:
 *    sarray.source -> String
 * 
 * Returns the source that this suffix array was constructed with.
 */
static VALUE SuffixArray_source(VALUE self)
{
    return rb_iv_get(self, "@source");
}
    


static void SuffixArray_free(void *p) {
    SuffixArray *sa = (SuffixArray *)p;
    if(sa->suffix_index) free(sa->suffix_index);
    if(sa) free(sa);
}

static VALUE SuffixArray_alloc(VALUE klass)
{
    SuffixArray *sa = NULL;
    
    // setup our internal memory for the suffix array structure
    return Data_Make_Struct(klass, SuffixArray, 0, SuffixArray_free, sa);
}


/*
 * call-seq:
 *   SuffixArray.new(source, [raw_array], [start]) -> SuffixArray
 * 
 * Given a string (anything like a string really) this will generate a
 * suffix array for the string so that you can work with it.  The
 * source cannot be an empty string since this is a useless operation.
 *
 * Two optional parameters allow you to restore a suffix array without
 * running the construction process again.  You basically give it the
 * String from SuffixArray.raw_array and the start from SuffixArray.suffix_start
 * and it will skip most calculations.  <b>This feature is really experimental
 * and is CPU dependent since the integers in the raw_array are native.</b>
 *
 * As usual, the suffix array is one element larger than the length of the
 * source string.  This is to include the terminal element for the suffix.
 */
static VALUE SuffixArray_initialize(int argc, VALUE *argv, VALUE self)
{
    SuffixArray *sa = NULL;
    size_t i = 0;
    Data_Get_Struct(self, SuffixArray, sa);
    assert(sa != NULL);
    VALUE source;
    VALUE array;
    VALUE start;
    
    // sort out the arguments and such
    rb_scan_args(argc, argv, "12", &source, &array, &start);

    // get the string value of the source given to us, keep it around for later
    VALUE sa_source_str = StringValue(source);
    rb_iv_set(self, "@source", sa_source_str);
    
    // setup temporary variables for the source and length pointers
    unsigned char *sa_source = RSTRING(sa_source_str)->ptr;
    size_t sa_source_len = RSTRING(sa_source_str)->len;  // include the always added \0
    
    // error check the whole thing
    if(sa_source_len == 0) {
        // we can't have this, so throw exception
        rb_raise(cSAError, ERR_NO_ZERO_LENGTH_INPUT);
    }
    
    if(!NIL_P(array) && NIL_P(start)) {
        rb_raise(cSAError, ERR_START_IF_ARRAY);
    } else if (!NIL_P(array) && !NIL_P(start)) {
        // looks like both parameters were given so check out the lengths
        if(RSTRING(array)->len / sizeof(int) != sa_source_len+1) {
            rb_raise(cSAError, ERR_MISMATCH_LENGTH);
        }
    }
        
    // allocate memory for the index integers
    sa->suffix_index = malloc(sizeof(int) * (sa_source_len+1));
    
    if(NIL_P(array)) {
        // create the suffix array from the source
        int st = bsarray(sa_source, sa->suffix_index, sa_source_len);

        if(st == -1) rb_raise(cSAError, "Error building suffix array");
        
        // set the suffix_start in our object
        rb_iv_set(self, "@suffix_start", INT2NUM(st));
    } else {
        // convert the given array and start to the internal structures needed
        memcpy(sa->suffix_index, RSTRING(array)->ptr, (sa_source_len+1) * sizeof(int));
        rb_iv_set(self, "@suffix_start", start);
    }
    
    unsigned char c = sa_source[sa->suffix_index[0]];  // start off with the first char in the sarray list
    sa->starts[c] = 0;
    for(i = 0; i < sa_source_len; i++) {
        // skip characters until we see a new one
        if(sa_source[sa->suffix_index[i]] != c) {
            sa->ends[c] = i-1; // it's -1 since this is a new character, so the end was actually behind this point
            c = sa_source[sa->suffix_index[i]];
            sa->starts[c] = i;
        }
    }
    // set the last valid character to get the tail of the sa, the loop will miss it
    c = sa_source[sa->suffix_index[sa_source_len-1]];
    sa->ends[c] = sa_source_len-1;
    
    return INT2FIX(sa_source_len);
}


/*
 * call-seq:
 *   sarray.longest_match(target, from_index) -> [start, length]
 *
 * Takes a target string and an index inside that string, and then tries
 * to find the longest match from that point in the source string for this
 * SuffixArray object.
 * 
 * It returns an array of [start, length] of where in the source a length
 * string from the target would match.
 *
 * Refer to the unit test for examples of usage.
 */
static VALUE SuffixArray_longest_match(VALUE self, VALUE target, VALUE from_index) 
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);

    VALUE sa_source = SuffixArray_source(self);
    
    if(sa == NULL || sa->suffix_index == NULL || RSTRING(sa_source)->len == 0) {
        rb_raise(cSAError, ERR_NOT_INITIALIZED);
    }
    
    // get the from and for_length arguments as unsigned ints
    size_t from = NUM2UINT(from_index);

    
    // get better pointers for the source (should already be in String form)
    unsigned char *source_ptr = RSTRING(sa_source)->ptr;
    size_t source_len = RSTRING(sa_source)->len;

    // get the target as a string
    VALUE target_str = StringValue(target);
    
    // better pointers again, we also need target_len as an in/out parameter
    unsigned char *target_ptr = RSTRING(target_str)->ptr;
    size_t target_len = RSTRING(target_str)->len;

    // check the input for validity, returning nil like in array operations
    if(from > target_len) {
        return Qnil;
    }
    
    // adjust for the from and for_length settings to be within the target len
    target_ptr += from;
    target_len -= from;
    
    size_t start = find_longest_match(source_ptr, source_len, target_ptr, &target_len, 
                                      sa->starts, sa->ends, sa->suffix_index);
    
    // create the 2 value return array
    VALUE result = rb_ary_new();
    
    rb_ary_push(result, INT2FIX(sa->suffix_index[start]));
    rb_ary_push(result, INT2FIX(target_len));
    
    return result;
}



/*
 * call-seq:
 *   sarray.match(target) -> [index1, index2, ... indexN]
 *
 * Takes a string and returns the indexes where this string is found.  It will only
 * match the complete string and returns an empty array if the string is not found.
 */
static VALUE SuffixArray_match(VALUE self, VALUE target) 
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);

    VALUE sa_source = SuffixArray_source(self);
    
    if(sa == NULL || sa->suffix_index == NULL || RSTRING(sa_source)->len == 0) {
        rb_raise(cSAError, ERR_NOT_INITIALIZED);
    }
    
    // get better pointers for the source (should already be in String form)
    unsigned char *source_ptr = RSTRING(sa_source)->ptr;
    size_t source_len = RSTRING(sa_source)->len;

    // get the target as a string
    VALUE target_str = StringValue(target);
    
    // better pointers again, we also need target_len as an in/out parameter
    unsigned char *target_ptr = RSTRING(target_str)->ptr;
    size_t target_len = RSTRING(target_str)->len;

    size_t start = find_longest_match(source_ptr, source_len, target_ptr, &target_len, 
                                      sa->starts, sa->ends, sa->suffix_index);

    // create the beginning array, and fill it with all matching elements
    VALUE result = rb_ary_new();
    
    if(target_len == RSTRING(target_str)->len) {
        // the result is actually in the middle, so we have to do this weird rollback thing
        // all previous suffix entries are shorter than the middle one, so no size check
        size_t middle = start;  // save the middle for the next step
        while(start-- >= 0) {
            size_t src_i = sa->suffix_index[start];
            if(source_ptr[src_i + target_len - 1] != target_ptr[target_len - 1]) {
                break;
            } else {
                // the last characters match and it's within length, so this is one of them
                rb_ary_unshift(result, INT2FIX(sa->suffix_index[start]));
            }
        }

        // push the middle one on
        rb_ary_push(result, INT2FIX(sa->suffix_index[middle]));
        
        // and then the end of the list as well
        while(middle++ <= source_len) {
            size_t src_i = sa->suffix_index[middle];
            // since the suffix array is sorted, we only need to check that the last possible char
            // is the same, or that the remaining length could fit this string
            if(src_i + target_len > source_len || source_ptr[src_i + target_len - 1] != target_ptr[target_len - 1]) {
                break;
            } else {
                // the last characters match and it's within length, so this is one of them
                rb_ary_push(result, INT2FIX(sa->suffix_index[middle]));
            }
        }
        
    }
    
    return result;
}



/*
 * call-seq:
 *   sarray.longest_nonmatch(target, from_index, min_match) -> [non_match_length, match_start, match_length]
 *
 * Mostly the inverse of longest_match, except that it first tries to find a
 * non-matching region, then a matching region.  The target and from_index are
 * the same as in longest_match.  The min_match argument is the smallest matching
 * region that you'll accept as significant enough to end the non-matching search.
 * Giving non_match=0 will stop at the first matching region.
 *
 * It works by first searching the suffix array for a non-matching region.  When it 
 * hits a character that is in the source (according to the suffix array) it tries
 * to find a matching region.  If it can find a matching region that is longer than min_match
 * then it stops and returns, otherwise it adds this match to the length of the non-matching
 * region and continues.
 *
 * The return value is an Array of [non_match_length, match_start, match_length].
 */
static VALUE SuffixArray_longest_nonmatch(VALUE self, VALUE target, VALUE from_index, VALUE min_match) 
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);

    VALUE sa_source = SuffixArray_source(self);
    
    if(sa == NULL || sa->suffix_index == NULL || RSTRING(sa_source)->len == 0) {
        rb_raise(cSAError, ERR_NOT_INITIALIZED);
    }
    
    // get the from and for_length arguments as unsigned ints
    size_t from = NUM2UINT(from_index);
    size_t min = NUM2INT(min_match);
    
    // get better pointers for the source (should already be in String form)
    unsigned char *source_ptr = RSTRING(sa_source)->ptr;
    size_t source_len = RSTRING(sa_source)->len;

    // get the target as a string
    VALUE target_str = StringValue(target);
    
    // better pointers again, we also need target_len as an in/out parameter
    unsigned char *target_ptr = RSTRING(target_str)->ptr;
    size_t target_len = RSTRING(target_str)->len;

    // check the input for validity, returning nil like in array operations
    if(from > target_len) {
        return Qnil;
    }
    
    
    // adjust for the from and for_length settings to be within the target len
    unsigned char *scan = target_ptr + from;
    unsigned char *end = target_ptr + target_len;
    size_t match_len = 0;
    size_t match_start = 0;
    while(scan < end) {
        if(*scan != source_ptr[sa->suffix_index[sa->starts[*scan]]]) {
            scan ++;
        } else {
            // search remaining stuff for a possible match, which return as a result as well
            match_len = end - scan;
            match_start = find_longest_match(source_ptr, source_len, scan, &match_len, 
                                              sa->starts, sa->ends, sa->suffix_index);
            
            if(match_len == 0) {
                // match not found, which really shouldn't happen
                break;
            } else if(match_len > min) {
                // the match is possibly long enough, drop out
                break;
            } else {
                // the number of possibly matching characters is much too small, so we continue by skipping them
                scan += match_len;
                // reset the match_len and match_start to 0 to signal that a match hasn't been found yet
                match_len = match_start = 0;
            }
        } 
    }

    VALUE result = rb_ary_new();
    
    size_t nonmatch_len = (scan - (target_ptr + from));
    rb_ary_push(result, INT2FIX(nonmatch_len));
    rb_ary_push(result, INT2FIX(sa->suffix_index[match_start]));
    rb_ary_push(result, INT2FIX(match_len));

    return result;
}


/*
 * call-seq:
 *   sarray.array -> Array  
 *
 * Returns a copy of the internal suffix array as an Array of Fixnum objects.  This
 * array is a copy so you're free to mangle it however you wish.
 *
 * A suffix array is the sequence of indices into the source that mark each suffix
 * as if they were sorted.
 */
static VALUE SuffixArray_array(VALUE self) 
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);

    VALUE sa_source = SuffixArray_source(self);
    
    if(sa == NULL || sa->suffix_index == NULL || RSTRING(sa_source)->len == 0) {
        rb_raise(cSAError, ERR_NOT_INITIALIZED);
    }
    
    // get the length of the suffix index
    size_t source_len = RSTRING(sa_source)->len + 1;
    size_t i = 0;
    
    VALUE result = rb_ary_new();
    
    for(i = 0; i < source_len; i++) {
        rb_ary_push(result, INT2FIX(sa->suffix_index[i]));
    }
    
    return result;
}


/*
 * call-seq:
 *     sarray.raw_array -> String
 * 
 * Returns the "raw" internal suffix array which is an array of C int types used 
 * internally as the suffix array.  The purpose of this function is to allow you
 * to store the suffix_array and then very quickly restore it later without having
 * to rebuild the suffix array.
 *
 * The returned String should be treated as an opaque structure.  It is just a 
 * copy of the int[] used internally.  This means that it is dependent on your
 * CPU.  If you want something you can use that is cross platform then use the
 * SuffixArray.array function instead.
 */
static VALUE SuffixArray_raw_array(VALUE self) 
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);

    VALUE sa_source = SuffixArray_source(self);
    size_t sa_source_len = RSTRING(sa_source)->len + 1;
    if(sa == NULL || sa->suffix_index == NULL || RSTRING(sa_source)->len == 0) {
        rb_raise(cSAError, ERR_NOT_INITIALIZED);
    }
    
    // build a string that copies this stuff
    VALUE result = rb_str_new((const char *)sa->suffix_index, sa_source_len * sizeof(int));

    return result;
}

/*
 * call-seq:
 *   sarray.start -> Fixnum
 *
 * Tells you which index in the suffix array is the longest suffix (also known as the
 * start of the source string).  If you want to get the beginning of the source string
 * in a round about way you would do this:
 *
 * source = "abracadabra"
 * sa = SuffixArray.new source
 * first = source[sa.array[sa.start]]]
 *
 * Remember that the start is the index into the suffix array where the source starts,
 * not an index into the source string (that would just be 0).
 */
static VALUE SuffixArray_suffix_start(VALUE self)
{
    return rb_iv_get(self, "@suffix_start");
}




/*
 * call-seq:
 *   sarray.all_starts(character) -> Array
 *
 * Returns an array containing all the indexes into the source that start
 * with the given character.  This is a very fast operation since the 
 * SuffixArray already knows where each character starts and ends in the
 * suffix array structure internally.  All it does is copy the range of
 * the suffix array for that region.
 */
static VALUE SuffixArray_all_starts(VALUE self, VALUE character)
{
    SuffixArray *sa = NULL;
    Data_Get_Struct(self, SuffixArray, sa);
    
    VALUE result = rb_ary_new();
    VALUE char_str = StringValue(character);
    
    // must be at least one length
    if(RSTRING(char_str)->len > 0) {
        size_t ch = (size_t)RSTRING(char_str)->ptr[0];

        // go through all the suffix array indices as indicated by sa->starts and sa->ends
        size_t start = 0;
    
        for(start = sa->starts[ch]; start <= sa->ends[ch]; start++) {
            rb_ary_push(result, INT2FIX(sa->suffix_index[start]));
        }
    }
    
    return result;
}


static VALUE cSuffixArray;

/**
 * Implements a SuffixArray structure with functions to do useful operations
 * quickly such as finding matching and non-matching regions, or finding all
 * the locations of a given character.  The suffix array construction algorithm
 * used was written by Sean Quinlan and Sean Doward and is licensed under the 
 * Plan9 license.  Please refer to the sarray.c file for more information.
 *
 * The suffix array construction algorithm used is not the fastest available,
 * but it was the most correctly implemented.  There is also a lcp.c file 
 * which implements an O(n) Longest Common Prefix algorithm, but it had
 * memory errors and buffer overflows which I decided to avoid for now.
 *
 * This file is licensed under the GPL license (see LICENSE in the root source
 * directory).
 */
void Init_suffix_array()
{
    cSuffixArray = rb_define_class("SuffixArray", rb_cObject);
    cSAError = rb_define_class("SAError", rb_eStandardError);
    rb_define_alloc_func(cSuffixArray, SuffixArray_alloc);

    rb_define_method(cSuffixArray, "initialize", SuffixArray_initialize, -1);
    rb_define_method(cSuffixArray, "longest_match", SuffixArray_longest_match, 2);
    rb_define_method(cSuffixArray, "match", SuffixArray_match, 1);
    rb_define_method(cSuffixArray, "longest_nonmatch", SuffixArray_longest_nonmatch, 3);
    rb_define_method(cSuffixArray, "array", SuffixArray_array, 0);
    rb_define_method(cSuffixArray, "raw_array", SuffixArray_raw_array, 0);
    rb_define_method(cSuffixArray, "suffix_start", SuffixArray_suffix_start, 0);
    rb_define_method(cSuffixArray, "source", SuffixArray_source, 0);
    rb_define_method(cSuffixArray, "all_starts", SuffixArray_all_starts, 1);
    
}
