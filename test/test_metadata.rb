require 'test/unit'
require 'fastcst/metadata'

include SuffixArrayDelta

module UnitTest
    class MetaDataTest < Test::Unit::TestCase
    
        def setup
            @md_file = "test/test.md"
            md = MetaData.create_metadata(@md_file, "test", "test", "test", "Zed A. Shaw", "zedshaw@zedshaw.com")
        end
    
        def teardown
            File.unlink(@md_file)
        end
    
        def test_add_file
            MetaData.add_file(@md_file, "test/case3.h", "test")
            md = MetaData.load_metadata(@md_file)
            assert_not_nil md
        end
    
        def finish_metadata
            parent_id = "112112212122112"
            MetaData.finish_metadata(@md_file, parent_id, "test/case3.h", "test/case4.h")
            md = MetaData.load_metadata(@md_file)
            assert_not_nil md
            assert md['Parent ID'] == "112112212122112"
        end
    
        def log_message
            MetaData.log_message(@md_file, "Test test test")
            md = MetaData.load_metadata(@md_file)
            assert_not_nil md
        end
    
        def add_disposition
            MetaData.add_disposition(@md_file, "test", "1111112", "tested")
            md = MetaData.load_metadata(@md_file)
            assert_not_nil md
        end
    end
end