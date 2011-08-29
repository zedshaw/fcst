require 'test/unit'
require 'fastcst/operation'
require 'yaml'
require 'stringio'
require 'fileutils'
require 'digest/md5'

include ChangeSet

module UnitTest
    class OperationTest < Test::Unit::TestCase

        def setup
            @journal_out = StringIO.new
            @data_out = StringIO.new
            @test_file = "temp.file"
            @test_dir = "test"
            @test_file_path = File.join(@test_dir,@test_file)
            @data_string = "THE TEST FILE!"
            
            File.open(@test_file_path, "w") { |f| f.write(@data_string) }
        end
        
        
        def teardown
            File.unlink(@test_file_path) if File.exist?(@test_file_path)
            
            FileUtils.rm_rf("test/delta") if File.exist?("test/delta")
        end

        # runs the operation making sure it is of the type op_class.
        # It rewinds the @data_out, but leaves it in the state after op.run
        # is called.
        def run_operation(op, op_class)
            @data_out.rewind
            assert op.class == op_class
                
            assert op.test(@data_out)
                
            @data_out.rewind
            op.skip(@data_out)

            @data_out.rewind
            assert op.run(@data_out)
        end
        
        def test_delete
            op = DeleteOperation.new({:path => @test_file }, @test_dir)
            
            op.store(@journal_out, @data_out)
            assert @journal_out.pos > 0
            @journal_out.rewind
            
            YAML.each_document(@journal_out) do |info|
                op = Operation.create(info, @test_dir)
                run_operation(op, DeleteOperation)
                assert !File.exist?(@test_file_path)
            end
        end
        
        def test_create
            op = CreateOperation.new({:path => @test_file}, @test_dir)
            
            op.store(@journal_out, @data_out)
            assert @journal_out.pos > 0
            assert @data_out.pos > 0

            @journal_out.rewind
            @data_out.rewind

            data = @data_out.read
            assert data == @data_string
            
            # need to delete the file now that it should be stored in our data
            File.unlink(@test_file_path)
            YAML.each_document(@journal_out) do |info|
                op = Operation.create(info, @test_dir)
                run_operation(op, CreateOperation)
                assert File.exist?(@test_file_path)
            end
        end
        
        
        def test_move
            mtime = File.mtime(@test_file_path)
            info = {:path => @test_file, :to_path => "test.move", :mtime => mtime}
            op = MoveOperation.new(info, @test_dir)
            
            op.store(@journal_out, @data_out)
            assert @journal_out.pos > 0
            
            @journal_out.rewind
            @data_out.rewind

            YAML.each_document(@journal_out) do |info|
                op = Operation.create(info, @test_dir)
                run_operation(op, MoveOperation)
                assert File.exist?("test/test.move")
                assert !File.exist?(@test_file)
                File.unlink("test/test.move")
            end
        end
        
        
        def test_delta
            test_data = "I'M DIFFERENT YEAH!"

            Dir.mkdir("test/delta")
            File.open("test/delta/temp.file", "w") { |f| f.write(test_data) }
            
            info = {:source => @test_dir, :path => @test_file, :digest => Digest::MD5.hexdigest(File.read(@test_file_path)) }
            op = DeltaOperation.new(info, "test/delta")
            
            op.store(@journal_out, @data_out)
            assert @journal_out.pos > 0
            assert @data_out.pos > 0

            @journal_out.rewind
            @data_out.rewind
            
            YAML.each_document(@journal_out) do |info|
                op = Operation.create(info, @test_dir)
                run_operation(op, DeltaOperation)
                result_data = File.read(@test_file_path)
                puts "test_data: #{test_data} and file after delta: #{result_data}"
                assert result_data == test_data
            end
        end
        
        
        def test_directory
            FileUtils.mkdir_p("test/dirs1/deleted")
            FileUtils.mkdir_p("test/dirs2/created")
            
            info = {:created_dirs => ["dirs2/created"], :deleted_dirs => ["dirs1/deleted"]}
            op = DirectoryOperation.new(info, @test_dir)
            
            op.store(@journal_out, @data_out)
            assert @journal_out.pos > 0
            @journal_out.rewind
            
            YAML.each_document(@journal_out) do |info|
                op = Operation.create(info, @test_dir)
                p op
                FileUtils.rm_rf("test/dirs2/created")  # should be recreated
                run_operation(op, DirectoryOperation)
                assert File.exists?("test/dirs2/created")
                assert !File.exists?("test/dirs1/deleted")
            end

            FileUtils.rm_rf("test/dirs1/deleted")
            FileUtils.rm_rf("test/dirs2/created")
            
        end
    end
end