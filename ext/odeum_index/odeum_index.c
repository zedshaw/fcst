/*************************************************************************************************
 * Implementation of Odeum for Ruby
 *                                                      Copyright (C) 2000-2005 Mikio Hirabayashi
 *
 * This file was written by Zed A. Shaw as an extension to Odeum for Ruby.
 *
 * The original QDBM license statement is:
 *
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


#include "ruby.h"
#include <assert.h>
#include <odeum.h>


#define FALSE 0

static VALUE mOdeum;
static VALUE cIndex;
static VALUE cDocument;
static VALUE cPair;
static VALUE cResultSet;


/**
 * Contains data used by the ResultSet type.
 */
typedef struct ResultSetData {
    ODPAIR *data;
    int length;
    int index;
} ResultSetData;


/** Convenience macors to get at the different types we store in T_DATA stuff. */
#define RAISE_NOT_NULL(T) if(T == NULL) rb_raise(rb_eStandardError, "NULL found for " # T " when shouldn't be.");
#define DATA_GET(from,type,name) Data_Get_Struct(from,type,name); RAISE_NOT_NULL(name);
#define REQUIRE_TYPE(V, T) if(TYPE(V) != T) rb_raise(rb_eTypeError, "Wrong argument type for " # V " required " # T);


VALUE Document_create(ODDOC *doc);


/* Converts from a CBLIST of char * strings to a Ruby Array of Strings. */
VALUE CBLIST_2_array(const CBLIST *list) 
{
    int count = cblistnum(list);
    int i = 0;
    VALUE ary = rb_ary_new2(count);
    
    for(i = 0; i < count;  i++) {
        int sp = 0;
        const char *val = cblistval(list, i, &sp);
        rb_ary_push(ary, rb_str_new(val, sp));
    }
    
    return ary;
}

/** Converts an array of strings to a CBLIST. */
CBLIST *array_2_CBLIST(VALUE ary) 
{
    long i = 0;
    CBLIST *result = cblistopen();
    VALUE str;
    
    for(i = 0; (str = rb_ary_entry(ary, i)) != Qnil; i++) {
        cblistpush(result, RSTRING(str)->ptr, RSTRING(str)->len);
    }
    
    return result;
}

/** Converts a CBMAP of char * strings into a Ruby Hash of Strings. */
VALUE CBMAP_2_hash(CBMAP *map) 
{
    int key_size = 0;
    int val_size = 0;
    const char *map_key = NULL;
    const char *map_val = NULL;
    VALUE hash = rb_hash_new();
    VALUE key;
    VALUE val;
    
    cbmapiterinit(map);
    while((map_key = cbmapiternext(map, &key_size)) != NULL) {
        map_val = cbmapget(map, map_key, key_size, &val_size);
        
        key = rb_str_new(map_key, key_size);
        val = rb_str_new(map_val, val_size);
        
        rb_hash_aset(hash, key, val);
    }
    
    return hash;
}


/**
 * ResultSets can't really be created with .new and are instead created
 * internally only.  This basically builds one up from the boot straps
 * so it can get returned.
 */
VALUE ResultSet_create(ODPAIR *pair, int length, CBLIST *errors) {
    VALUE rs;
    ResultSetData *result_set = NULL;
    rs = rb_class_new_instance(0, NULL,cResultSet);
    result_set = (ResultSetData *)DATA_PTR(rs);

    result_set->data = pair;
    result_set->length = length;
    result_set->index = 0;  // starts off at the first one always

    // setup the initial errors
    if(errors) {
        rb_iv_set(rs, "@errors", CBLIST_2_array(errors));
    } else {
        /* make it an empty array so that they don't need special handling of no errors */
        rb_iv_set(rs, "@errors", rb_ary_new2(0));
    }
    
    return rs;
}



/**
 * Frees both the OdeumWrapper union and the enclosed ODPAIRS *data pointer.
 */
void ResultSet_free(void *ptr) {
    
    ResultSetData *result_set = (ResultSetData *)ptr;
    if(result_set->data) {
        free(result_set->data);
    }
    
    free(result_set);
}

/**
 * Allocates just the OdeumWrapper class so that it can be created later on.
 * ResultSet does not have an initialize method since it's not intended to be
 * created outside of the Index_search and Index_query methods.
 */
VALUE ResultSet_alloc(VALUE klass) {
    ResultSetData *result_set = NULL;
    VALUE obj = Data_Make_Struct(klass, ResultSetData, NULL, ResultSet_free, result_set);
    return obj;
}


/**
 * call-seq:
 *    rs.next -> id
 *
 * Used to iterate through a list of results from Index.search or Index.query calls.
 * It already starts off at the first element, so you can call ResultSet.id, ResultSet.score
 * and other methods to find out about the first element.  This is handy when you only want
 * the first element as you can then do index.query("stuff").id to just get the id without
 * having to call next.
 *
 * It returns nil if there are no more elements.
 */
VALUE ResultSet_next(VALUE self)
{

    VALUE result;
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    if(rs->index < 0 || rs->index >= rs->length) {
        /* at the end or wrapped to a negative so don't increment anymore and return nil */
        result = Qnil;
    } else {
        /* get the current one, then increment so that we can start at 0 for index. */
        result = INT2FIX(rs->data[rs->index].id);
        rs->index++;
    }
    
    return result;
}



/**
 * call-seq:
 *    rs.next_doc(Index) -> Document
 *
 * This is a convenience method for doing the most common operation which is simply 
 * iterate through all of the documents in the ResultSet.  It requires the Index used
 * during querying so that it can perform the look-up and get the Document from it.
 *
 * This is similar to doing index.get_by_id(rs.next) but it performs better since there's
 * less of a round-trip between Ruby and the C extension.  My tests show that it's only
 * slightly faster ResultSet.next followed by Odeum.get_by_id (not statistically significant).
 * It is faster than the other methods with order of performance being: ResultSet.next_n_docs,
 * ResultSet.to_a, and finally ResultSet.[] being the lowest performer.  The others are simply
 * provided so people have the flexibility to use ResultSet in the way that makes most sense
 * to them.
 *
 * Returns nil if there are no more documents in the result set.
 */
VALUE ResultSet_next_doc(VALUE self, VALUE odeum_index)
{
    VALUE doc;
    ODDOC *oddoc = NULL;
    ODEUM *odeum = NULL;
    ResultSetData *rs = NULL;
    
    DATA_GET(odeum_index, ODEUM, odeum);
    DATA_GET(self, ResultSetData, rs);
    
    if(rs->index >= 0 && rs->index < rs->length) {
        oddoc = odgetbyid(odeum, rs->data[rs->index].id);

        if(oddoc == NULL)
            doc = Qnil;
        else
            doc = Document_create(oddoc);
        rs->index++;
    } else {
        /* at the end or wrapped to a negative so don't increment anymore and return nil */
        doc = Qnil;
    }
    
    return doc;
}



/**
 * call-seq:
 *    rs.move(count) -> index back by count (if negative) and forward by count (if positive)
 *    rs.move(ResultSet::BEGINNING) -> reset the index to the beginning (ResultSet::BEGINNING == 0 actually)
 *    rs.move(rs.length) -> moves to the end (it compensates for giving much by stopping at the end)
 *
 * Rewinds the ResultSet by the given amount, ensuring that the internal index stops
 * at the beginning properly.  If you pass in a 0 (or less) for the count value then it
 * well rewind to the beginning of the result set.  This functionality was handled by
 * a reset function but that was redundant.
 */
VALUE ResultSet_move(VALUE self, VALUE count)
{
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    int move = FIX2INT(count);
    
    if(move == 0) {
        rs->index = 0;
    } else {
        /* move by count, and then adjust based on the results.  rs->index is an int so we have negative values */
        rs->index += move - 1;  /* must be by -1 to compensate for how the index is always +1 */
        if(rs->index < 0) {
            rs->index = 0;
        } else if(rs->index > rs->length) {
            rs->index = rs->length;
        }
    }
    
    return Qnil;
}


/**
 * call-seq:
 *    rs.score -> Fixnum
 *
 * Gets the score for the currently selected element of the ResultSet.  Returns nil if
 * there are no more.  You should call it after using ResultSet.next or ResultSet.next_doc
 * to increment the index.
 */
VALUE ResultSet_score(VALUE self)
{
    VALUE score;
    int i = 0;
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    /* This weirdness is because we start off ready to do index 0.
     * They can ask for the score before calling the first next, so index==0 then.
     * Must compensate and just use 0 for i or index - 1 to get the last index.
     */    
    i = rs->index == 0 ? 0 : rs->index - 1;
    
    if(i >= 0 && i < rs->length) {
        score = INT2FIX(rs->data[i].score);
    } else {
        /* at the end or wrapped to a negative so don't increment anymore and return nil */
        score = Qnil;
    }
    
    return score;
}

/**
 * call-seq:
 *    ResultSet.id -> Fixnum
 *
 * Same as ResultSet.score except it returns the current id.  This is redundant if you
 * just use ResultSet.next (that returns the id), but it is needed if you want the id
 * after usig ResultSet.next_doc.
 * 
 * Returns nil if there are no more documents.
 */
VALUE ResultSet_id(VALUE self)
{
    VALUE id;
    int i = 0;
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    /* This weirdness is because we start off ready to do index 0.
     * They can ask for the score before calling the first next, so index==0 then.
     * Must compensate and just use 0 for i or index - 1 to get the last index.
     */    
    i = rs->index == 0 ? 0 : rs->index - 1;
    
    if(i >= 0 && i < rs->length) {
        id = INT2FIX(rs->data[i].id);
    } else {
        /* at the end or wrapped to a negative so don't increment anymore and return nil */
        id = Qnil;
    }
    
    return id;
}


/**
 * call-seq:
 *    rs.index -> Fixnum
 *
 *
 * Returns the current index which should always be less than the ResultSet.length.
 * It always returns a number and never returns nil.
 */
VALUE ResultSet_index(VALUE self)
{
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    return INT2FIX(rs->index == 0 ? 0 : rs->index - 1);
}



/**
 * call-seq:
 *    rs.length -> Fixnum
 *
 * Returns the length of the result set.
 */
VALUE ResultSet_length(VALUE self)
{
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    return INT2FIX(rs->length);
}


/**
 * call-seq:
 *     ResultSet[index] -> [id, score]
 *
 * Simply gives you the [id, score] pair at the requested index in the
 * result set.  If the index is past the length then you are given nil.
 * You can safely perform indexed access while using iteration, which means
 * you can loop through the result set with next or next_doc, and then use
 * ResultSet[index] to get any other element randomly.
 *
 * This is the slowest of all the access methods you could use.  It's actually
 * faster to just to to_a, but if you have a large document set then that will
 * waste lots of memory copying stuff into an array.
 */
VALUE ResultSet_at(VALUE self, VALUE index)
{
    VALUE ary;
    int i = 0;
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);

    
    i = FIX2INT(index);
    
    if(i < 0 || i >= rs->length) {
        ary = Qnil;
    } else {
        ary = rb_ary_new2(2);
        rb_ary_push(ary, INT2FIX(rs->data[i].id));
        rb_ary_push(ary, INT2FIX(rs->data[i].score));
    }
    
    return ary;
}


/**
 * call-seq:
 *   ResultSet.to_a -> Array
 *
 * Returns a copy of all [id,score] pairs held internally by the ResultSet.  This
 * lets you do more "rubyish" things with the results if you don't mind lots of
 * data copying.  You can call this method directly without having to do any ResultSet.next
 * calls first.  This allows you to do Index.query("stuff").to_a to get the entire ResultSet
 * as an array immediately.
 */
VALUE ResultSet_to_a(VALUE self)
{
    int i = 0;
    VALUE results;
    VALUE ary;
    ResultSetData *rs = NULL;
    DATA_GET(self, ResultSetData, rs);
    
    results = rb_ary_new2(rs->length);
    
    for(i = 0; i < rs->length; i++) {
        ary = rb_ary_new2(2);
        rb_ary_push(ary, INT2FIX(rs->data[i].id));
        rb_ary_push(ary, INT2FIX(rs->data[i].score));
        rb_ary_push(results, ary);
    }
    
    return results;
}

/**
 * call-seq:
 *    rs.query_errors -> Array
 *
 * Returns a list of errors, which will be empty if there are no errors.
 */
VALUE ResultSet_errors(VALUE self)
{
    return rb_iv_get(self, "@errors");
}


/**
 * call-seq:
 *   rs.marshal_dump -> data
 *
 * Constructs an opaque string that will let you store the ResultSet for later
 * reloading.  The ResultSet will be frozen at the exact last index and with the
 * same document ids, so if the index changes later then you'll have to deal with
 * the resulting nil returns when you try to get the documents.
 *
 * The main purpose for this is so that you can temporarily store a user's ResultSet
 * for their searches, and implement paging, but there's probably other uses.  Another
 * use could be for clustering the retrieval of documents by blasting the ResultSet out
 * to a bunch of nodes with the index incremented to different places.
 */
VALUE ResultSet_marshal_dump(VALUE self)
{
    ResultSetData *rs = NULL;
    VALUE result;
    VALUE pairs;
    long pairs_len;
    
    DATA_GET(self, ResultSetData, rs);

    result = rb_ary_new2(3);
    rb_ary_push(result, INT2FIX(rs->length));
    rb_ary_push(result, INT2FIX(rs->index));
    
    pairs_len = rs->length * sizeof(ODPAIR);
    pairs = rb_str_buf_new(pairs_len);
    RSTRING(pairs)->len = pairs_len;
    memcpy(RSTRING(pairs)->ptr, rs->data, pairs_len);
    
    rb_ary_push(result, pairs);
    
    return result;
}

/**
 * call-seq:
 *   rs.marshal_load(data) -> nil
 *
 * This is actually called by the Marshal.load function to re-construct 
 */
VALUE ResultSet_marshal_load(VALUE self, VALUE data)
{
    ResultSetData *rs = NULL;
    VALUE len;
    VALUE ind;
    VALUE pairs;
    
    DATA_GET(self, ResultSetData, rs);

    assert(rs->data == NULL);
    
    len = rb_ary_entry(data, 0); REQUIRE_TYPE(len, T_FIXNUM);
    ind = rb_ary_entry(data, 1); REQUIRE_TYPE(ind, T_FIXNUM);
    pairs = rb_ary_entry(data, 2); REQUIRE_TYPE(pairs, T_STRING);
    
    rs->length = FIX2INT(len);
    rs->index = FIX2INT(ind);
    rs->data = malloc(RSTRING(pairs)->len);
    RAISE_NOT_NULL(rs->data);
    memcpy(rs->data, RSTRING(pairs)->ptr, RSTRING(pairs)->len);
    
    return Qnil;
}


/** 
 * Frees the ODEUM struct contained in the wrapper, closing it if it is not already
 * NULL.  This is the best we can do for automatically closing the ODEUM if it wasn't
 * done explicitly with Odeum.close.
 */
void Index_free(void *ptr)
{
    ODEUM *od = (ODEUM *)ptr;
    
    if(od) {
        // didn't explicitly close, so do it for them
        odclose(od);
    }
}


/** Allocates only the OdeumWrapper we use to wrap the pointers we need for internal
 * operation.  This is needed because we can't create the ODEUM struct until we know
 * the name of the catalog, so we have to "defer" until Odeum.initialize.
 */
VALUE Index_alloc(VALUE klass) 
{
    ODEUM *odeum = NULL;
    VALUE obj = Data_Wrap_Struct(klass, NULL, Index_free, odeum);
    return obj;
}


/**
 * call-seq:
 *   Index.new(name, mode) -> Index
 * 
 * Creates an Index with the given name according to mode.  The name will be used
 * as the basis for a local directory which will contain the database for the documents.
 * 
 * Possible modes might be:
 * 
 * - Odeum::OWRITER -- Opens as a writer.
 * - Odeum::OREADER -- Read-only.
 * - Odeum::OCREAT -- Or'd in to OWRITER to indicate that you want it created if not existing.
 * - Odeum::ONOLOCK -- Opens without locking on the directory.
 *
 * Opening as OWRITER creates an exclusive lock on the database dir, but OREADER
 * opens with a shared lock.  A thread will block until the lock is achieved, but
 * none of this has been tested in Ruby with Ruby's in-process threads.
 */
VALUE Index_initialize(VALUE self, VALUE name, VALUE mode) 
{
    REQUIRE_TYPE(self, T_DATA);
    REQUIRE_TYPE(name, T_STRING);
    REQUIRE_TYPE(mode, T_FIXNUM);
    
    DATA_PTR(self) = odopen(RSTRING(name)->ptr, FIX2INT(mode));
    if(DATA_PTR(self) == NULL) {
        // there was an error, find out what it was
        rb_raise(rb_eStandardError, "Failed to open requested database.");
    }
    
    return self;
}

/**
 * call-seq:
 *    Index.close -> true/false
 *
 * Closes the Index explicitly.  It will be closed by the GC when Index_free
 * is finally called, but don't rely on this as it is not reliable enough.
 * Don't use the index after this, it will throw an exception or possibly crash.
 */
VALUE Index_close(VALUE self) 
{
    int result = 0;
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    
    // must set the wrapper->odeum to NULL so that Index_free does not try to close it again
    result = odclose(odeum);
    DATA_PTR(self) = NULL;
    
    return result == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    index.put(doc, wmax, over) -> true/false
 *
 * Puts the Document doc into the Index, and indexes a maximum of wmax
 * words in the document.  If over is true than the document is overwritten
 * in the database.  Otherwise, if the document already exists in the 
 * database and over== nil/false then the method will return false as
 * an error.
 */
VALUE Index_put(VALUE self, VALUE doc, VALUE wmax, VALUE over) 
{
    int res = 0;
    ODEUM *odeum = NULL;
    ODDOC *oddoc = NULL;
    
    DATA_GET(self, ODEUM, odeum);
    DATA_GET(doc, ODDOC, oddoc);  
    
    REQUIRE_TYPE(wmax, T_FIXNUM);
    
    res = odput(odeum, oddoc, FIX2INT(wmax), !(over == Qnil || over == Qfalse));
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *   index.delete(uri) -> true/false
 *
 * Deletes the document given by the uri.  The Index must be opened
 * as a writer, and the call will return false if no such document exists.
 */
VALUE Index_delete(VALUE self, VALUE uri) {
    ODEUM *odeum = NULL;
    
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(uri, T_STRING);
    
    int res = odout(odeum, RSTRING(uri)->ptr);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *   index.delete_by_id(id) -> true/false
 *
 * Deletes a document based on its id.
 */
VALUE Index_delete_by_id(VALUE self, VALUE id) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);

    REQUIRE_TYPE(id, T_FIXNUM);
    
    int res = odoutbyid(odeum, FIX2INT(id));
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *   index.get(uri) -> Document
 *
 * Gets a Document based on the uri, or returns nil.
 */
VALUE Index_get(VALUE self, VALUE uri) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(uri, T_STRING);
    
    ODDOC *oddoc = odget(odeum, RSTRING(uri)->ptr);
    if(oddoc == NULL)
        return Qnil;
    else
        return Document_create(oddoc);
}


/**
 * call-seq:
 *   index.get_by_id(id) -> Document
 *
 * Gets a Document based on its id, or nil if that document isn't there.
 */
VALUE Index_get_by_id(VALUE self, VALUE id) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(id, T_FIXNUM);
    
    ODDOC *oddoc = odgetbyid(odeum, FIX2INT(id));

    if(oddoc == NULL)
        return Qnil;
    else
        return Document_create(oddoc);
}


/**
 * call-seq:
 *   index.get_id_by_uri(id)
 *
 * Returns just the id of the document with the given uri.
 */
VALUE Index_get_id_by_uri(VALUE self, VALUE uri) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(uri, T_STRING);
    
    int res = odgetidbyuri(odeum, RSTRING(uri)->ptr);
    return INT2FIX(res);
}


/**
 * call-seq:
 *   index.check(id)
 *
 * Checks if a document with the given id is in the database.
 */
VALUE Index_check(VALUE self, VALUE id) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(id, T_FIXNUM);
    
    int res = odcheck(odeum, FIX2INT(id));
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    index.search(word, max) -> ResultSet
 *
 * The big payoff method which actually searches for the documents
 * that have the given word mentioned.  The result of the search is
 * a ResultSet object which you can use to get at the results either
 * through iteration or direct access with ResultSet#[].  
 *
 * If the search attempt fails for some reason then an exception is thrown,
 * but an empty result is NOT a failure (that returns a ResultSet with nothing).
 *
 * If you don't want to the ResultSet and would rather have an array of the [id,score]
 * pairs, then simply call the ResultSet.to_a method right away:  index.search(word, max).to_a
 *
 */
VALUE Index_search(VALUE self, VALUE word, VALUE max) 
{
    int num_returned = 0;

    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(word, T_STRING);
    REQUIRE_TYPE(max, T_FIXNUM);
    
    
    ODPAIR *pairs = odsearch(odeum, RSTRING(word)->ptr, FIX2INT(max), &num_returned);
    if(pairs == NULL) {
        // nothing found
        rb_raise(rb_eStandardError, "Search failure.");
    }

    return ResultSet_create(pairs, num_returned, NULL);
}



/**
 * call-seq:
 *    index.query(query) -> [[id,score], ... ]
 *
 * An implementation of a basic query language for Odeum.  The query language
 * allows boolean expressions of search terms and '&', '|', '!' with parenthesis
 * as sub-expressions.  The '!' operator implements NOTAND so that you can say, 
 * "this AND NOT that" using "this ! that".  Consecutive words are assumed to 
 * have an implicit '&' between them.
 *
 * An example expression is:  "Zed & shaw ! (frank blank)".  The (frank blank) 
 * part actually is interpreted as (frank & blank).
 *
 * It returns the same ResultSet as Index.search does.
 */
VALUE Index_query(VALUE self, VALUE word) {
    CBLIST *errors = NULL;
    int num_returned = 0;
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);

    REQUIRE_TYPE(word, T_STRING);
    
    errors = cblistopen();
    
    ODPAIR *pairs = odquery(odeum, RSTRING(word)->ptr, &num_returned, errors);
    if(pairs == NULL) {
        // nothing found
        rb_raise(rb_eStandardError, "Query failure.");
    } 
    
    return ResultSet_create(pairs, num_returned, errors);
}


/**
 * call-seq:
 *    index.search_doc_count(word) -> Fixnum
 *
 * Returns the number of documents matching the given word.  If the word
 * does not match anything then it returns -1.
 */
VALUE Index_search_doc_count(VALUE self, VALUE word) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    REQUIRE_TYPE(word, T_STRING);
    
    int res = odsearchdnum(odeum, RSTRING(word)->ptr);
    return INT2FIX(res);
}


/**
 * call-seq:
 *    index.iterator -> true/false
 *
 * Begins an iterator loop to process documents in the system.
 * An iterator/next pattern is used due to the difficulty of getting
 * memory collection correct inside an each/block design.
 */
VALUE Index_iterator(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = oditerinit(odeum);
    
    return res == FALSE ? Qfalse : Qtrue;
}

/**
 * call-seq:
 *   index.next -> Document
 *
 * Returns the next document or nil if there was an error.  Must call
 * Index.iterator first.
 */
VALUE Index_next(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    
    ODDOC *doc = oditernext(odeum);
    if(doc == NULL)
        return Qnil;
    
    VALUE doc_obj = Document_create(doc);
    
    return doc_obj;
}

/**
 * call-seq:
 *   index.sync -> true/false
 *
 * Synchronizes any changes you have made with the database.  If you
 * don't do this every once in a while then the memory load will get
 * to great.  I found that every 1000 documents or so is a good trade-off.
 *
 * Returns true if everything worked, or false otherwise.
 */
VALUE Index_sync(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odsync(odeum);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *   index.optimize -> true/false
 *
 * Purges deleted documents from the index.  I found that if you 
 * call this while you are updating documents then it stops adding
 * documents after the optimize call.
 */
VALUE Index_optimize(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odoptimize(odeum);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    index.name -> String
 */
VALUE Index_name(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    char *name = odname(odeum);
    VALUE result = rb_str_new2(name);
    free(name);
    return result;
}

/**
 * call-seq:
 *   index.size -> Fixnum
 *
 * Returns the size of the database files or -1 if there's a failure.
 */
VALUE Index_size(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    double res = odfsiz(odeum);
    return rb_float_new(res);
}


/**
 * call-seq:
 *   index.bucket_count -> Fixnum
 *
 * Returns the total number of elements of the bucket arrays, or -1 on failure.
 */
VALUE Index_bucket_count(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odbnum(odeum);
    return INT2FIX(res);
}

/**
 * call-seq:
 *   index.buckets_used -> Fixnum
 *
 * The total number of used elements of the bucket arrays, or -1 if failure.
 */
VALUE Index_buckets_used(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odbusenum(odeum);
    return INT2FIX(res);
}


/**
 * call-seq:
 *     index.doc_count -> Fixnum
 *
 * Number of documents stored in the database, or -1 on failure.
 */
VALUE Index_doc_count(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = oddnum(odeum);
    return INT2FIX(res);
}


/**
 * call-seq:
 *   index.word_count -> Fixnum
 */
VALUE Index_word_count(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odwnum(odeum);
    return INT2FIX(res);
}


/**
 * call-seq:
 *   index.writable -> true/false
 */
VALUE Index_writable(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int res = odwritable(odeum);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    index.fatal_error -> Fixnum
 *
 * Returns true if there's a fatal error or false otherwise.
 */
VALUE Index_fatal_error(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int err = odfatalerror(odeum);
    return err == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    index.inode -> Fixnum
 *
 * The inode number of the database directory.
 */
VALUE Index_inode(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int inode = odinode(odeum);
    return INT2FIX(inode);
}


/**
 * call-seq:
 *   index.mtime -> Fixnum
 * 
 * The mtime of the database directory.
 */
VALUE Index_mtime(VALUE self) {
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    int mtime = odmtime(odeum);
    return INT2FIX(mtime);
}



/**
 * call-seq:
 *    Odeum::merge(new_name, other_databases) -> true/false
 *
 * Merges the databases listed in other_databases (Array of Strings)
 * into the new database new_name.
 * If two or more documents have the same URI then the first one is
 * adopted and the others are ignored.
 */
VALUE Odeum_merge(VALUE self, VALUE name, VALUE elemnames) {
    REQUIRE_TYPE(name, T_STRING);
    REQUIRE_TYPE(elemnames, T_ARRAY);
    
    CBLIST *elems = array_2_CBLIST(elemnames);
    int res = odmerge(RSTRING(name)->ptr, elems);
    cblistclose(elems);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *   Odeum::remove(name) -> true/false
 * 
 * Removes the database directory and everything in it.
 */
VALUE Odeum_remove(VALUE self, VALUE name) {
    REQUIRE_TYPE(name, T_STRING);
    
    int res = odremove(RSTRING(name)->ptr);
    return res == FALSE ? Qfalse : Qtrue;
}


/**
 * call-seq:
 *    Odeum::breaktext(test) -> [word1, word2, word3]
 *
 * Breaks a string into an array of words that are separated by
 * space characters and such delimiters as period, comma, etc.
 * You should also check out StringScanner as a more flexible
 * alternative.  This function must do a lot of data copying and
 * other things in order to convert from Odeum internal types to Ruby
 * types.
 */
VALUE Odeum_breaktext(VALUE self, VALUE text) {
    REQUIRE_TYPE(text, T_STRING);
    
    CBLIST *result = odbreaktext(RSTRING(text)->ptr);
    VALUE list = CBLIST_2_array(result);
    cblistclose(result);
    return list;
}


/**
 * call-seq:
 *   Odeum::normalizeword(asis) -> normal
 *
 * Given a word from breaktext (which is considered "as-is") 
 * it will "normalize" it in a consistent way which is suitable
 * for searching.  The normalization effectively strips puntuation
 * and spacing, and then lowercases the word.  If there is nothing
 * but "removed" chars in the asis string then the return is empty.
 * Check for this so you don't try to search for nothing.
 */
VALUE Odeum_normalizeword(VALUE self, VALUE asis) {
    REQUIRE_TYPE(asis, T_STRING);
    
    char *result = odnormalizeword(RSTRING(asis)->ptr);
    VALUE res_str = rb_str_new2(result);
    free(result);
    return res_str;
}


/**
 * call-seq:
 *   Odeum::settuning(ibnum, idnum, cbnum, csiz) -> nil
 *
 * ibnum=32749: Number of buckets for inverted indexes. 
 * idnum=7: Division number of inverted index. 
 * cbnum=262139:  Number of buckets for dirty buffers. 
 * csiz=8388608: Maximum bytes to use memory for dirty buffers.
 *
 * This is set globally for all Indexes.  Not sure what would happen
 * if you changed this mid-stream, so don't.  Make sure everything is closed.
 */
VALUE Odeum_settuning(VALUE self, VALUE ibnum, VALUE idnum, VALUE cbnum, VALUE csiz) {
    REQUIRE_TYPE(ibnum, T_FIXNUM);
    REQUIRE_TYPE(idnum, T_FIXNUM);
    REQUIRE_TYPE(cbnum, T_FIXNUM);
    REQUIRE_TYPE(csiz, T_FIXNUM);
    
    odsettuning(FIX2INT(ibnum), FIX2INT(idnum), FIX2INT(cbnum), FIX2INT(csiz));
    return Qnil;
}


/**
 * call-seq:
 *   Index::setcharclass(space, delim, glue) -> nil
 *
 * Changes the definition of a SPACE, DELIM, and GLUE char for this index.
 * This will alter how text is broken up in Document::add_content in cases where
 * you wish to index content differently.
 */
VALUE Index_setcharclass(VALUE self, VALUE spacechars, VALUE delimchars, VALUE gluechars)
{
    ODEUM *odeum = NULL;
    DATA_GET(self, ODEUM, odeum);
    
    REQUIRE_TYPE(spacechars, T_STRING);
    REQUIRE_TYPE(delimchars, T_STRING);
    REQUIRE_TYPE(gluechars, T_STRING);
    

    odsetcharclass(odeum, RSTRING(spacechars)->ptr, RSTRING(delimchars)->ptr, RSTRING(gluechars)->ptr);
    
    return Qnil;
}




/** Builds a new document from a created ODDOC. This is needed since the Index_* functions
 * will return an ODDOC pointer, but the only function to construct a Document normally is
 * with oddocopen which requires a URI.  This solves the problem by using the "naked"
 * Document_initialize, and then attaches the doc to it.
 */
VALUE Document_create(ODDOC *doc) 
{
    VALUE uri[1]; 
    VALUE new_doc;
    
    uri[0] = Qnil;
    new_doc = rb_class_new_instance(1, uri, cDocument);
    DATA_PTR(new_doc) = doc;
     
    return new_doc;
}



/**
 * Frees the internal wrapper and properly cleans up the ODDOC.
 */
void Document_free(void *ptr) {
    ODDOC *oddoc = (ODDOC *)ptr;

    if(oddoc) {
        // didn't explicitly close, so do it for them
        oddocclose(oddoc);
    }
}


/**
 * Allocates the wrapper only, leaving the actual allocation for Document_initialize.
 */
VALUE Document_alloc(VALUE klass) {
    ODDOC *oddoc = NULL;
    VALUE obj = Data_Wrap_Struct(klass, NULL, Document_free, oddoc);
    return obj;
}

/**
 * call-seq:
 *   Document.new uri -> Document
 *
 * The uri should be specified if you're calling this.  Internally the
 * Ruby/Odeum library kind of "cheats" and passes a Qnil for the uri 
 * so that the ODDOC can be assigned externally.  You should not
 * (and probably cannot) do this from Ruby.
 */
VALUE Document_initialize(VALUE self, VALUE uri) {    
    if(!NIL_P(uri)) {
        REQUIRE_TYPE(uri, T_STRING);
        DATA_PTR(self) = oddocopen(RSTRING(uri)->ptr);
    }
        
    return self;
}


/**
 * call-seq:
 *   doc.close -> nil
 *
 * Explicitly closes a document.  Because of what I can only decide is a bug
 * in how an each iterator works, you must explicitly close a document
 * if you are not storing it and you are in an each.  There are probably
 * subtle things about Ruby memory management I'm missing, but my tests 
 * show that all Document objects created with Index.get do not get 
 * garbage collected until they exit a block.
 */
VALUE Document_close(VALUE self) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC, oddoc);
    
    oddocclose(oddoc);
    DATA_PTR(self) = NULL;  // must set to null to prevent double free
    return Qnil;
}
    

/**
 * call-seq:
 *   doc[attr] = value
 *
 * Adds meta-data to the document.  They should be Strings only.
 */
VALUE Document_addattr(VALUE self, VALUE name, VALUE value) {    
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    REQUIRE_TYPE(name, T_STRING);
    REQUIRE_TYPE(value, T_STRING);
    
    oddocaddattr(oddoc, RSTRING(name)->ptr, RSTRING(value)->ptr);
    return self;
}


/**
 * call-seq:
 *   document.add_content(index, content) -> document
 *
 * Takes the contents, breaks the words up, and then puts them in the document
 * in normalized form.  This is the common pattern that people use a Document
 * with.  You may also use Document.addword to add one word a time, and
 * Document.add_word_list to add a list of words.
 *
 * It uses the default odanalyzetext method to break up the text,
 * which means you can use the Index::setcharclass method to configure
 * what is a DELIM, GLUE, and SPACE character.  The default is the same
 * as Odeum::breaktext.
 *
 * If the process of normalizing a word creates an empty word, then it
 * is not added to the document's words.  This usually happens for
 * punctation that isn't usualy searched for anyway.
 *
 * The Index used with this document is now required since that object holds
 * the information about how text is broken via the Index::setcharclass method.
 */
VALUE Document_add_content(VALUE self, VALUE index, VALUE content) {
    CBLIST *asis_words = NULL;
    CBLIST *norm_words = NULL;
    const char *asis = NULL;
    const char *norm = NULL;
    int asis_len = 0;
    int norm_len = 0;
    int i = 0;
    int count = 0;
    ODDOC *oddoc = NULL;
    ODEUM *odeum = NULL;
    
    DATA_GET(self,ODDOC, oddoc);
    DATA_GET(index,ODEUM, odeum);
    
    
    REQUIRE_TYPE(content, T_STRING);
    
    asis_words = cblistopen();
    norm_words = cblistopen();
    
    odanalyzetext(odeum, RSTRING(content)->ptr, asis_words, norm_words);
    
    // go through words and add them
    count = cblistnum(asis_words);
    
    for(i = 0; i < count;  i++) {
        asis = cblistval(asis_words, i, &asis_len);
        norm = cblistval(norm_words, i, &norm_len);
        
        // only add words that normalize to some content
        oddocaddword(oddoc, norm, asis);
    }
    
    cblistclose(asis_words);
    cblistclose(norm_words);
    
    return self;
}

/**
 * call-seq:
 *   document.add_word_list(asis) -> document
 *
 * Takes an array of "as-is" words, normalizes them, and puts them in the document.
 * It assumes that the array is composed of asis words and normalizes them
 * before putting them in the document.
 */
VALUE Document_add_word_list(VALUE self, VALUE asis) {
    VALUE str;
    int i = 0;
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    REQUIRE_TYPE(asis, T_ARRAY);
    
    for(i = 0; (str = rb_ary_entry(asis, i)) != Qnil; i++) {
        char *result = odnormalizeword(RSTRING(str)->ptr);
        oddocaddword(oddoc, result, RSTRING(str)->ptr);
        free(result);
    }
    
    return self;
}


/**
 * call-seq:
 *   document.addword(normal, asis)
 *
 * The basic call to add a normal and asis version of a word to the 
 * document for indexing.
 */
VALUE Document_addword(VALUE self, VALUE normal, VALUE asis) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    REQUIRE_TYPE(normal, T_STRING);
    REQUIRE_TYPE(asis, T_STRING);
    
    oddocaddword(oddoc, RSTRING(normal)->ptr, RSTRING(asis)->ptr);
    return self;
}


/**
 * call-seq:
 *   document.id -> Fixnum
 *
 * Gives you the Odeum::Index id used to for the document.
 */
VALUE Document_id(VALUE self) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    int id = oddocid(oddoc);
    return INT2FIX(id);
}


/**
 * call-seq:
 *   document.uri -> String
 *
 * Gets the uri that this document represents.
 */
VALUE Document_uri(VALUE self) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    VALUE uri = rb_str_new2(oddocuri(oddoc));
    return uri;
}


/**
 * call-seq:
 *   document[name] -> String
 *
 * Gets the meta-data attribute for the given name.  The name must
 * be a String.
 */
VALUE Document_getattr(VALUE self, VALUE name) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    REQUIRE_TYPE(name, T_STRING);
    
    const char *value = oddocgetattr(oddoc, RSTRING(name)->ptr);

    return value == NULL ? Qnil : rb_str_new2(value);
}


/**
 * call-seq:
 *    document.normal_words -> [word1, word2, ... ]
 *
 * Returns the list of "normal" words in this document.
 */
VALUE Document_normal_words(VALUE self) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    const CBLIST *list = oddocnwords(oddoc);
    return CBLIST_2_array(list);
}


/**
 * call-seq:
 *   document.asis_words -> [word1, word2, ...]
 *
 * Returns all of the asis or "appearance form" words in the document.
 */
VALUE Document_asis_words(VALUE self) {
    ODDOC *oddoc = NULL;
    DATA_GET(self, ODDOC,oddoc);
    const CBLIST *list = oddocawords(oddoc);
    return CBLIST_2_array(list);
}


/**
 * call-seq:
 *   document.scores(max, index) -> { word => score, word => score, ...}
 *
 * Get the normalized words and their scores in the document.  The
 * strange thing is that the scores are returned as Strings, but they
 * are decimal strings.
 */
VALUE Document_scores(VALUE self, VALUE max, VALUE odeum_obj) {
    ODDOC *oddoc = NULL;
    ODEUM *odeum = NULL;
    
    DATA_GET(self, ODDOC, oddoc);
    DATA_GET(odeum_obj, ODEUM, odeum);
    REQUIRE_TYPE(max, T_FIXNUM);
    
    CBMAP *scores = oddocscores(oddoc, FIX2INT(max), odeum);
    VALUE map = CBMAP_2_hash(scores);
    cbmapclose(scores);
    return map;
}


void Init_odeum_index() {
    
    mOdeum = rb_define_module("Odeum");
    rb_define_module_function(mOdeum, "merge", Odeum_merge, 2);
    rb_define_module_function(mOdeum, "remove", Odeum_remove, 1);
    rb_define_module_function(mOdeum, "breaktext", Odeum_breaktext, 1);
    rb_define_module_function(mOdeum, "normalizeword", Odeum_normalizeword, 1);
    rb_define_module_function(mOdeum, "settuning", Odeum_settuning, 4);
    rb_define_const(mOdeum, "OREADER", INT2FIX(OD_OREADER));
    rb_define_const(mOdeum, "OWRITER", INT2FIX(OD_OWRITER));
    rb_define_const(mOdeum, "OCREAT", INT2FIX(OD_OCREAT));
    rb_define_const(mOdeum, "OTRUNC", INT2FIX(OD_OTRUNC));
    rb_define_const(mOdeum, "ONOLCK", INT2FIX(OD_ONOLCK));

    
    cIndex = rb_define_class_under(mOdeum, "Index", rb_cObject);
    rb_define_alloc_func(cIndex, Index_alloc);
    
    rb_define_method(cIndex, "initialize", Index_initialize, 2);
    rb_define_method(cIndex, "close", Index_close, 0);
    rb_define_method(cIndex, "put", Index_put, 3);
    rb_define_method(cIndex, "delete", Index_delete, 1);
    rb_define_method(cIndex, "delete_by_id", Index_delete_by_id, 1);
    rb_define_method(cIndex, "get", Index_get, 1);
    rb_define_method(cIndex, "get_by_id", Index_get_by_id, 1);
    rb_define_method(cIndex, "get_id_by_uri", Index_get_id_by_uri, 1);
    rb_define_method(cIndex, "check", Index_check, 1);
    rb_define_method(cIndex, "search", Index_search, 2);
    rb_define_method(cIndex, "search_doc_count", Index_search_doc_count, 1);
    rb_define_method(cIndex, "query", Index_query, 1);
    rb_define_method(cIndex, "iterator", Index_iterator, 0);
    rb_define_method(cIndex, "next", Index_next, 0);
    rb_define_method(cIndex, "sync", Index_sync, 0);
    rb_define_method(cIndex, "optimize", Index_optimize, 0);
    rb_define_method(cIndex, "name", Index_name, 0);
    rb_define_method(cIndex, "size", Index_size, 0);
    rb_define_method(cIndex, "bucket_count", Index_bucket_count, 0);
    rb_define_method(cIndex, "buckets_used", Index_buckets_used, 0);
    rb_define_method(cIndex, "doc_count", Index_doc_count, 0);
    rb_define_method(cIndex, "word_count", Index_word_count, 0);
    rb_define_method(cIndex, "writable", Index_writable, 0);
    rb_define_method(cIndex, "fatal_error", Index_fatal_error, 0);
    rb_define_method(cIndex, "inode", Index_inode, 0);
    rb_define_method(cIndex, "mtime", Index_mtime, 0);
    rb_define_method(cIndex, "setcharclass", Index_setcharclass, 0);
    
    cDocument = rb_define_class_under(mOdeum, "Document", rb_cObject);
    rb_define_alloc_func(cDocument, Document_alloc);
    rb_define_method(cDocument, "initialize", Document_initialize, 1);
    rb_define_method(cDocument, "[]=", Document_addattr, 2);
    rb_define_method(cDocument, "[]", Document_getattr, 1);
    rb_define_method(cDocument, "addword", Document_addword, 2);
    rb_define_method(cDocument, "add_word_list", Document_add_word_list, 1);
    rb_define_method(cDocument, "add_content", Document_add_content, 2);
    rb_define_method(cDocument, "id", Document_id, 0);
    rb_define_method(cDocument, "uri", Document_uri, 0);
    rb_define_method(cDocument, "normal_words", Document_normal_words, 0);
    rb_define_method(cDocument, "asis_words", Document_asis_words, 0);
    rb_define_method(cDocument, "scores", Document_scores, 2);
    rb_define_method(cDocument, "close", Document_close, 0);

    
    cResultSet = rb_define_class_under(mOdeum, "ResultSet", rb_cObject);
    rb_define_alloc_func(cResultSet, ResultSet_alloc);
    
    rb_define_method(cResultSet, "[]", ResultSet_at, 1);
    rb_define_method(cResultSet, "next", ResultSet_next,0);
    rb_define_method(cResultSet, "next_doc", ResultSet_next_doc,1);
    rb_define_method(cResultSet, "move", ResultSet_move,1);
    rb_define_const(cResultSet, "BEGINNING", 0);
    rb_define_method(cResultSet, "score", ResultSet_score, 0);
    rb_define_method(cResultSet, "id", ResultSet_id, 0);
    rb_define_method(cResultSet, "index", ResultSet_index, 0);
    rb_define_method(cResultSet, "length", ResultSet_length, 0);
    rb_define_method(cResultSet, "to_a", ResultSet_to_a, 0);
    rb_define_method(cResultSet, "errors", ResultSet_errors, 0);
    rb_define_method(cResultSet, "marshal_dump", ResultSet_marshal_dump, 0);
    rb_define_method(cResultSet, "marshal_load", ResultSet_marshal_load, 1);
}
