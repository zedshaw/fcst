require 'suffix_array'

# = Introduction
# A Suffix Array Delta (or Suffix Tree Delta as well) is a method of producing a delta
# which is reasonably small, favors small changes, and is fairly fast by searching for
# matching/non-matching regions using the suffix array.
#
# The Suffix Array implementation is a C extension which wraps a suffix array construction
# algorithm written by Sean Quinlan and Sean Doward.  Their library is licensed under the 
# Plan 9 Open Source license (see ext/sarray/sarray.c for details).
#
# If you want to quickly make and read deltas then use the SuffixArrayDelta#make_delta and 
# SuffixArrayDelta#apply_delta functions.  Also look at the ApplyDeltaCommand, MakeDeltaCommand,
# and ShowDeltaCommand for alternative ways to use this module.
#
# = Design
#
# The SuffixArrayDelta module consists of four classes:
#
#   [SuffixArray]  C extension that does the grunt work of creating a suffix array and doing searches.
#   [Emitter]  Responsible for taking INSERT and MATCH events and "doing something" with them.
#   [DeltaGenerator]  Takes a Suffix Array, a target, and an Emitter, and then generates INSERT and
#       MATCH events until finished.  This makes delta files, but the emitter allows other options.
#   [DeltaReader]  Reads in a delta "file" (input source) and generates INSERT/MATCH events to an
#       Emitter.  This allows applying the delta (ApplyEmitter), or printing it out (LogEmitter).
#
# The SuffixArray is not defined in this module, but in the suffix_array.c extension.
#
# This design allows for flexible configuration of the delta processing operations, and let's
# people use them in new situations, but most of the time you'll just want to make and apply
# deltas without having to worry about how to "wire" the classes together.  The two functions
# SuffixArrayDelta#make_delta and SuffixArray#apply_delta both do this for you and are used in the
# ApplyChangeSetCommand and MakeChangeSetCommand classes.
#
# = Algorithm
#
# The actual delta creation algorithm is very simple since the SuffixArray class does all the 
# heavy lifting.  The key to how it works is the fact that we can make a suffix array fairly quickly,
# and then use that suffix array to find matching/non-matching regions between two strings.  The
# delta creation simply involves taking the SuffixArray and calling SuffixArray#longest_nonmatch
# until we've exhausted the target string's data.
#
# Each call to SuffixArray#longest_nonmatch returns a triplet of [non-match length, 
# match start, match-length] which is used to send INSERT and MATCH events to the Emitter.
#
# Refer to SuffixArrayDelta#generate for more details, and SuffixArray#longest_nonmatch for how
# matching/non-matching is done.
#
# Once a series of INSERT/MATCH records is recorded, we can reconstruct the target file given only
# the delta and the source.  We simply process each record by sending it to an ApplyEmitter which
# either INSERTs the required block of data, or writes/copies the MATCH region from the source.
# The end result is a (hopefully) exact replica of the target file.
#
# = Optimizations
#
# Right now the algorithm is written to be as correct as possible, but not as fast as possible.
# Some possible improvements are:
#
#   * Use some of the more recent suffix array construction algorithms which are possibly faster.
#   * Implement a better search algorithm.  Currently the search algorithm is a traditional binary search
#     and must rescan the target until it finds a full match.
#   * Use a smaller delta encoding.  Currently uses a byte followed by a set of 32 bit integers and 
#     possible INSERT data.  BER encoding would work, but the current Array#pack and String#unpack 
#     functions don't handle streaming very well.
#   * Experiment with different caching options, pre-generating the suffix array, and maybe mmap files.
#
# = Formats
#
# The only format that matters at the moment is the delta file format created by the 
# SuffixArrayDelta::FileEmitter, and read by the SuffixArray::DeltaReader.  The file
# consists of a sequence of INSERT and MATCH records.  Each records has the format:
#
#   [INSERT] byte=0 uint32(length) string(data)  -- string is not 0 terminated.
#   [MATCH] byte=1 uint32(start) uint32(length)
#
# The uint32 is a little-endian (think Intel) byte order.  This is only an artifact of
# my using an Intel machine to make the program, and also a choice based on the fact that
# most of the entire world uses little-endian machines, so converting to network byte-order
# is retarded.  Future versions may change this.
#

module SuffixArrayDelta
    
    
    
    # Base class used by all emitters.  It mostly handles the statistics part of 
    # the emit process.  Implementing classes should call update_insert_stats
    # and update_match_stats to help keep track of the stats.
    #
    # Implementing classes should also have insert, match, and finished functions.
    # these aren't included here since Ruby doesn't enforce any kind of abstract
    # functions (doesn't need them anyway).
    class BaseEmitter
        attr_reader :match_count, :insert_count, :match_total, :insert_total
        
        def initialize
            @insert_count = 0
            @match_count = 0
            @insert_total = 0
            @match_total = 0
        end

        def update_insert_stats(start, length)
            @insert_count += 1
            @insert_total += length
        end
        
        def update_match_stats(start, length)
            @match_count += 1
            @match_total += length
        end
    end
    
    
    # A simple emitter that just prints out information on each record as it recieves their
    # events.  Useful for debugging and analyzing a delta.
    class LogEmitter < BaseEmitter
    
        def insert(start, length, from)
            puts "I: #{start},#{length}"
            update_insert_stats(start, length)
        end
    
        def match(start, length)
            puts "M: #{start},#{length}"
            update_match_stats(start, length)
        end
    
        def finished
            puts "Match Count: #{@match_count}, Insert Count: #{@insert_count}"
        end
    end


    # And emitter which writes the INSERT and MATCH records to a delta file for 
    # storage.  The current output encoding is not the most efficient since it
    # uses platform standard "cV" and "cVV" packing for the INSERT and MATCH 
    # headers respectively.  I'll revisit this choice at a later date.
    class FileEmitter < BaseEmitter
        MATCH = 1
        INSERT = 0
    
        def initialize(file, should_close=true)
            @file = file
            @should_close = should_close
            super()
        end

        def insert(start, length, from)
            header = [INSERT, length].pack("cV")
            data = from[start, length]
            @file.write(header)
            @file.write(data)
            update_insert_stats(start, length)
        end
    
        def match(start, length)
            header = [MATCH, start, length].pack("cVV")
            @file.write(header)
            update_match_stats(start, length)
        end
        
        def finished
            if @should_close
                @file.close
            end
        end
    
    end


    # An emitter which uses a source and the INSERT/MATCH events to reconstruct a target
    # output stream.  By default the ApplyEmitter will close the target output stream,
    # unless the should_close=false option is set.
    class ApplyEmitter < BaseEmitter
        def initialize(source, file, should_close=true)
            @source = source
            @file = file
            @should_close = should_close
            super()
        end
    
        def insert(start, length, from)
            if start == 0 && length == from.length
                @file.write from
            else
                @file.write from[start, length]
            end
            update_insert_stats(start, length)
        end
    
        def match(start, length)
            data = @source[start, length]
            @file.write data
            update_match_stats(start, length)
        end
    
        def finished
            if @should_close
                @file.close
            end
        end
    end


    # Uses a SuffixArray, a source, a target, and an Emitter to create a sequence of INSERT/MATCH
    # events.  The emitter is responsible for using these events to do something useful.
    class DeltaGenerator
        attr_reader :short_match_threshold
        attr_writer :short_match_threshold
        SHORT_MATCH_THRESHOLD=30
    
    
        # Initializes the generator so that generate can do it's thing.
        # It defaults to a short match threshold (see generate) of 30 which
        # informally seemed to produce the best overall deltas.  Allowing the
        # short_match_threshold to be changed hasn't been fully tested so it's
        # not allowed right now.  It might be a good idea in the future to make
        # this adaptable based on the input.
        def initialize(sary, source)
            @sary = sary
            @source = source
            @short_match_threshold = SHORT_MATCH_THRESHOLD
        end
    
    
        # Does the actual work of generating the INSERT/MATCH events.  The algorithm is dead simple
        # and involves nothing more than a while loop that repeatedly calles SuffixArray#longest_nonmatch
        # producing the required events.  It continues this until it exhausts the target data.
        #
        # The only strange part is the use of the @shortest_match_threshold as the third parameter
        # of the SuffixArray#longest_nonmatch target.  The shortest match threshold is a setting that
        # helps create more efficient deltas by including any MATCH that is smaller than this threshold
        # in the non-match region.  The longest_nonmatch basically considers any bytes not found and
        # any MATCH less than this threshold as the non-matching region.  It stops looking for a
        # non-match once it finds a MATCH greater than the short match threshold.
        #
        # Currently it defaults to 30, which in my quick tests seemed to be a good limit on the
        # size of a match. A more adaptive algorithm would be better where the shortest_match_threshold
        # is adjusted either based on the size of the file, or the size of each match found.
        def generate(target, emit)
            start = 0
            while start < target.length
                non_len, match_start, match_len = @sary.longest_nonmatch target, start, @short_match_threshold
            
                if non_len > 0
                    # an insert of good non_len was found
                    emit.insert start, non_len, target
                end
            
                if match_len > 0
                    emit.match match_start, match_len
                end
            
                start += non_len + match_len
            end
        
            emit.finished
        end
    
    end


    # Simply reads in a delta from the a data source (IO like) and then sends the events to 
    # an emitter.  One limitation of the DeltaReader is that the String#unpack function does
    # not allow an efficient streaming input.  This means it has to use fixed size records instead
    # of smaller variable sized records.  For example, if I used BER encoded integers (with a 
    # pack argument of "cww" or "cw" for MATCH and INSERT) then I could save quite a lot of space.
    # But, when I'd try to read this in I'd have no idea how much of the input stream is required
    # to reconstruct a BER integer.
    #
    # I plan to write a "streamable" pack/unpack library that will get around this problem, or
    # create a tighter custom format.  For now the format is pretty good and has the advantage
    # possibly being faster.
    class DeltaReader
        def apply(delta, emitter)
            while not delta.eof?
                # there is always at least a character identifying the record and an integer following it
                c,i = delta.read(5).unpack("cV")
            
                # decide which record we have
                if c == FileEmitter::MATCH
                    length = delta.read(4).unpack("V")[0]
                    emitter.match i,length
                elsif c == FileEmitter::INSERT
                    data = delta.read(i)
                    emitter.insert 0,i,data
                else
                    raise "Invalid delta, the delta is probably corrupt."
                end
            end
        
            emitter.finished
        end
    end
    
    
    # A Convenience method that takes a source data set (String like), a target data set (String like)
    # and an output target (IO like).  It then wires together all of the objects in SuffixArrayDelta
    # required to create a delta and write it to output.
    ### @export "resume"
    def make_delta(source, target, output)
        sa = SuffixArray.new(source)
        gen = DeltaGenerator.new(sa, source)
        emitter = FileEmitter.new(output, should_close=false)
        gen.generate(target, emitter)
        return [emitter.match_count, emitter.match_total, 
            emitter.insert_count, emitter.insert_total]
    end
    ### @end
    

    # A Convenience method that takes a source data set (String like), a delta input source (IO like),
    # and an output source (IO like).  It then wires together the necessary SuffixArrayDelta objects
    # to re-create a file based on the source and delta, writing the results to out.
    def apply_delta(source, delta, out)
        apply = ApplyEmitter.new(source, out, should_close=false)
        reader = DeltaReader.new
        reader.apply(delta, apply)
    end
        
end
