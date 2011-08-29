require 'test/unit'
require 'sadelta'
require 'fileutils'
require 'zlib'
require 'digest/md5'

include SuffixArrayDelta

module UnitTest
    
    class SADeltaTest < Test::Unit::TestCase

        def setup
            @source_file = "test/case3.h"
            @target_file = "test/case4.h"
            @result_file = "test/test.nstd"
            @apply_file = "test/test.out"
        end

        def teardown
            FileUtils.rm @result_file
            FileUtils.rm @apply_file
        end
    
        def test_make_apply_delta        
            source = File.read(@source_file)
            target = File.read(@target_file)
            result = Zlib::GzipWriter.open(@result_file)
        
            sa = SuffixArray.new(source)
            gen = DeltaGenerator.new(sa, source)
            emit = FileEmitter.new(result)
            gen.generate(target, emit)

            # testing apply
            input = Zlib::GzipReader.open(@result_file)
            out = File.open(@apply_file, "w")
        
            apply = ApplyEmitter.new(source, out)
            reader = DeltaReader.new
            reader.apply(input, apply)
            input.close
        
            tgt_md5 = Digest::MD5.hexdigest(target)
            ap_md5 = Digest::MD5.hexdigest(File.read(@apply_file))
        
            assert_equal ap_md5, tgt_md5, "Applied delta digest #{ap_md5} != target digest #{tgt_md5}"
        end
    end
end
