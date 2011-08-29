require 'test/unit'
require 'suffix_array'

require 'benchmark'

module UnitTest
    
    class SuffixArrayTest < Test::Unit::TestCase
    
        def setup
            @source = "abracadabra"
            @sarray = SuffixArray.new @source
        end

        def test_array_roundtrip
            sa2 = SuffixArray.new @source, @sarray.raw_array, @sarray.suffix_start
            
            start1, length1 = @sarray.longest_match "cad", 0
            start2, length2 = sa2.longest_match "cad", 0
            
            assert_equal start1, start2
            assert_equal length1, length2
            assert_equal @sarray.array, sa2.array
        end
        
        def test_longest_match
            # go through a simple permutation of the possible suffices and match on them
            @source.length.times do |i|
                test_case = @source[i ... @source.length]
                start, length = @sarray.longest_match test_case, 0
                assert_equal test_case.length, length, "Match length is wrong"
                assert_equal test_case, @source[start ... start+length], "Match contents are wrong"
            end
        end
    
    
        def test_longest_nonmatch
            # go through the same matching permutation, but use a non-match string at the front
            # to test the nonmatch algorithm, but we turn off short match encoding
            nonmatch = "XXXXXXXXXX"
            short_match = 0
            @source.length.times do |i|
                test_case = nonmatch + @source[i ... @source.length]
                nonmatch_len, match_start, match_len = @sarray.longest_nonmatch test_case, 0, short_match
                nm = test_case[0 ... nonmatch_len]

                assert_equal nm, nonmatch, "Non-match regions not equal"
                assert_equal @source.length-i, match_len, "Match length is wrong #{nm.length}, #{match_len}"
                assert_equal @source[i ... i+match_len], @source[match_start ... match_start+match_len], "Match contents are wrong"
            end
        end
    
        def test_bad_input
            zero_input = ""
            assert_raises SAError do
                blowup = SuffixArray.new zero_input
            end
        end
    
        def test_array
            array = @sarray.array
            assert_equal array.length, @source.length+1, "Suffix array is not the same length as the source"
        end
    
        def test_suffix_start
            start = @sarray.suffix_start
            array = @sarray.array
            assert_equal @source[0], @source[array[start]], "The first character is not the one given in the start"
        end
    
    
        def test_all_starts
            starts = @sarray.all_starts "b"
            assert_not_nil starts
        
            assert_equal starts.length, 2, "Wrong number of starts for 'b' of #@source"
            assert_equal starts[0], @source.rindex("b"), "Index #{starts[0]} should be #{@source.rindex('b')}"
            assert_equal starts[1], @source.index("b"), "Index #{starts[1]} should be #{@source.index('b')}"
        
            starts.each do |i|
                assert_equal @source[i], "b"[0], "Character at starts index is not 'b' it's #{@source[i]}"
            end
        
            # do a bigger test with this source file
            my_source = File.read("test/test_suffix_array.rb")
            bigsa = SuffixArray.new(my_source)
            starts = bigsa.all_starts(' ')
        
            starts.each do |i|
                assert_equal my_source[i], ' '[0], "Invalid character at index (#{my_source[i]} != #{' '[0]})"
            end
        end
        
        
        # Found this bug while trying to use the suffix array to test for
        # files to exclude.  It was missing the |.| sequence at the very
        # end of the suffix array source.
        def test_trailing_find_bug
            sa = SuffixArray.new("|./.fastcst|./index_db|.|")
            res = sa.longest_match("|.|", 0)
            assert_equal res[0], 22, "Wrong position: #{res[0]}"
            assert_equal res[1], 3, "Wrong length: #{res[1]}"
        end
        
        
        def test_match_all
            sa = SuffixArray.new("ab|abc|abcd|abcde|fffffab|abc|ab")
            res = sa.match("ab")
            assert_equal 7, res.length
        end
    end
end