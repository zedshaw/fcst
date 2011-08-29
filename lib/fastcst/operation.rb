require 'fastcst/ui'


module ChangeSet
    
    # The base operation class that all other operations implement.
    # It implements most of the machinery for storing/loading operations
    # to the journal file, and leaves the data part to the subclasses.
    # 
    # Creating an operation and then storing it to the journal is done
    # in two steps:
    #
    # 1.  Instantiate the required operation like normal with the correct hash for settings
    # 2.  Call the store method for the operation with the journal and data file.
    # 
    # Then reading in a series of operations is done using the Operation.load method:
    #
    # 1.  Open the journal file and pass it to Operation.load.  Just keep going until
    #     the method returns nil meaning it's done.
    # 2.  Call either run, test, or skip on the method with the data input stream and 
    #     target directory.
    #
    # When implementing an Operation you should make sure the initialize method can
    # take only the info hashtable, and configures itself correctly from that without
    # needing to store any information.  If it doesn't put itself in a state ready
    # for storing, running, etc. then it will run into trouble when it is loaded.
    #
    # One fundamental motivation for not using the standard YAML serialization is that
    # it opens the loading process to massive security concerns since any object with
    # a run method will be loaded and ran.  Until YAML supports restricted serialization
    # this is the best we can do.
    #
    # Another reason is that the default Ruby serialization for YAML spews out tons of
    # Ruby specific stuff which chokes other languages.  This format should be language
    # agnostic.
    class Operation
        attr_reader :info
        
        # Initializes the base with information needed to load it later.
        # The dir parameter should not be stored in the info by the
        # subclass as it will change.  Instead it should store it 
        # internally as a member, and then use it in the other
        # operations.
        #
        # Usually the subclass will not override this, and instead finish
        # filling in the @info hash with additional information during the
        # store method call.
        def initialize(info, dir)
            @dir = dir
            @info = info
        end

        
        # Stores the operation to the journal output stream so that it can
        # be recovered later using Operation.load.  The data is written
        # as an array of [ class_name, @info ] so that Operation.load 
        # will know what operation to create.
        def store(journal_out, data_out)
            op_data = [ self.class::TYPE, @info ]
            YAML.dump(op_data, journal_out)
            journal_out.write("\n")
        end

        
        # A factory method that takes the information written to the journal
        # by Operation.store and creates the appropriate subclass configured
        # for operation.
        #
        # This method uses an explicit method of determining the class to load
        # rather than introspection.  If introspection were used then attackers
        # could easily inject any class they wanted into the changeset stream
        # and get you to run an undesired operation.
        def self.create(op_data, dir)
            type, info = op_data

            case type
            when DeleteOperation::TYPE:
                DeleteOperation.new(info, dir)
            when MoveOperation::TYPE:
                MoveOperation.new(info, dir)
            when DeltaOperation::TYPE:
                DeltaOperation.new(info, dir)
            when CreateOperation::TYPE:
                CreateOperation.new(info, dir)
            when DirectoryOperation::TYPE:
                DirectoryOperation.new(info, dir)
            else
                raise "An invalid operation type #{class_name} is in this changeset."
            end
        end
        
        
        # Actually runs the operation against the target dir using the given
        # input data.
        def run(data_in)
            raise NotImplemented, "You must implement this method."
        end
    
        
        # Intended to be run when reading in the journal file and this
        # operation needs to be skipped.  It is needed since the data_in
        # file might need to be processed or seeked by a certain amount
        # for the next operation to function correctly.
        def skip(data_in)
            # does nothing
        end
        
        
        # The merge method is responsible for determining whether this
        # operation can be used as-is, should be skipped, or if it
        # conflicts with the current directory's contents and needs
        # to be resolved.  It returns:
        #
        # * -1 == conflict (this operation is LESS desirable if applied)
        # * 0 == skip (this operation is EQUAL to the directory already)
        # * 1 == apply (this operation has MORE than the directory)
        #
        # This is similar to the results of a compare operation.
        # Resolving a conflict is up to something and someone else
        # and they'll most likely need to create a new operation with
        # a new set of data, replacing the conflicting operation.
        def merge
            return -1
        end
        
        # Should perform some verification that this operation will work
        # against the target directory.  By default returns true.
        def test(data_in)
            true
        end
    end
        

    
    # An operation which deletes a given file.  It only needs the path
    # during initialization, and just blindly deletes the file when run.
    # 
    # Required initialize settings:
    #
    # * :path -- The path of the file to delete.
    # * :digest -- The digest of the original file being deleted.  It won't be used
    #   it is not present.
    class DeleteOperation < Operation
    
        TYPE = "delete"
        
        # Changes to the dir and deletes the file.
        def run(data_in)
            path = @info[:path]
            
            begin
                Dir.chdir @dir do
                    FileUtils.rm path
                end
            rescue
                UI.failure :delete, "#{$!}"
                return false
            end
            
            return true
        end
        
        # Performs tests to see if it's safe/possible to perform this operation
        def test(data_in)
            path = @info[:path]
            
            begin
                Dir.chdir @dir do
                    FileUtils::DryRun.rm path
                end
                
            rescue
                UI.failure :delete, "#{$!}"
                skip(data_in)
                return false
            end
            
            return true
        end
        
        #* delete:
        #	* path exists, source different == ASK, BACKUP, DELETE
        #	* path missing == SKIP
        #	* path exists, source same == DELETE
        #
        def merge
            Dir.chdir @dir do
                if File.exist? @info[:path]
                    digest = Digest::MD5.hexdigest(File.read(@info[:path]))
                    if digest == @info[:digest]
                        # APPLY DELETE
                        return 1
                    else
                        # CONFLICT DELETE
                        return -1
                    end
                else
                    # SKIP DELETE
                    return 0
                end
            end
        end
    end



    # An operation for creating files based on the target directory, path to the file,
    # and a data output stream.  It records the file's contents and the mtime of the
    # file so that it can replicate the file as closely as possible.
    #
    # Required info settings:
    #
    # * :path -- path of the file to create
    #
    # It will create information then for :mtime, :symlink_target, and :length
    #
    # The presence or absence of :symlink_target determines if this create operation
    # is to create a symlink or a real file.
    #
    class CreateOperation < Operation
    
        TYPE = "create"
        
        # Stores the file data to the data_out, and then lets the Operation.store
        # do the rest.  It fills in additional information for the @info such
        # as mtime, whether the file is a symlink (:symlink_target), and the length
        # of the file.
        def store(journal_out, data_out)
            path = @info[:path]
            
            Dir.chdir @dir do
                @info[:mtime] = File.mtime path
                
                if not File.symlink?(path)
                    # regular file so go to town and write the data_out
                    data = File.read info[:path]
                    @info[:length] = data.length
                    data_out.write data
                else
                    @info[:symlink_target] = File.readlink(path)
                end
            end

            super(journal_out, data_out)
        end
        
        
        # Creates the file in the target directory, reading the data out of the
        # data_in stream.  After the file is created this function will set the
        # mtime so it matches the original.  This will overwrite the file  it
        # it already exists, so it isn't safe yet.
        def run(data_in)
            path = @info[:path]
            symlink_target = @info[:symlink_target]
            
            begin
                Dir.chdir @dir do
                    ChangeSet.create_target_path(path)
            
                    if symlink_target
                        File.symlink(symlink_target, path)
                    else
                        data = data_in.read(@info[:length])
                        File.open(path, "wb") {|out| out.write data }
                    end
                    
                    File.utime(Time.now, @info[:mtime], path)
                end
            rescue
                UI.failure :create, "Failed to create file: #$!"
                return false
            end
            
            return true
        end
    
        
        # Performs tests to see if it's safe/possible to perform this operation
        def test(data_in)
            path = @info[:path]
            
            begin
                Dir.chdir @dir do
                    # the path exists, so we can test this one
                    if File.exist?(File.dirname(path))
                        FileUtils::DryRun.touch(path)
                    end
                end
            rescue
                UI.failure :create, "Failed to create file: #$!"
                skip(data_in) # skip any data we need
                return false
            end
            
            return true
        end

        #* create:
        #	* path mising == CREATE
        #	* path exists, target different == CONFLICT, DIFF
        #	* path exists, target same == SKIP
        #
        def merge
            Dir.chdir @dir do
                if File.exist? @info[:path]
                    digest = Digest::MD5.hexdigest(File.read(@info[:path]))
                    if digest == @info[:digest]
                        # SKIP CREATE
                        return 0
                    else
                        # CONFLICT CREATE
                        return -1
                    end
                else
                    # APPLY CREATE
                    return 1
                end
            end
        end
        
        
        
        # Seeks ahead the @length.
        def skip(data_in)
            data_in.seek(@info[:length], IO::SEEK_CUR)
        end
    end

    
    

    # Records a move operation that moves a file from one path to another,
    # and then sets it's mtime to the original setting.
    #
    # Required info parameters are:
    #
    # :path -- Where the file is originally.
    # :to_path -- Where the file should be moved to.
    # :mtime -- The original mtime for the source file.
    class MoveOperation < Operation
    
        TYPE = "move"
        
        # Simply performs the move and the mtime set.
        def run(data_in)
            from_path = @info[:path]
            to_path = @info[:to_path]
            
            begin
                Dir.chdir @dir do
                    ChangeSet.create_target_path(to_path)
                    FileUtils.mv from_path, to_path, :force => true
                    File.utime(Time.now, @info[:mtime], to_path)
                end
                
            rescue
                UI.failure :move, "#{$!}"
                return false
            end
            
            return true
        end
    
        # Performs tests to see if it's safe/possible to perform this operation
        def test(data_in)
            from_path = @info[:path]
            to_path = @info[:to_path]
            p info
            begin
                Dir.chdir @dir do
                    return false unless File.exist?(from_path)
                    
                    if File.exist?(File.dirname(to_path))
                        FileUtils::DryRun.mv from_path, to_path, :force => true
                    end
                end
            rescue
               UI.failure :move, "#{$!}"
               raise
               return false
            end
            
            return true
        end
        

        #* move:
        #	* path exists, to_path missing == MOVE
        #	* path exists, to_path exists == CONFLICT, flag
        #	* path missing, to_path exists == SKIP
        #	* path missing, to_path missing == CONFLICT, lost
        #
        def merge
            Dir.chdir @dir do
                if File.exist? @info[:path]
                    digest = Digest::MD5.hexdigest(File.read(@info[:path]))
                    if digest == @info[:digest]
                        # APPLY MOVE
                        return 1
                    else
                        # CONFLICT MOVE
                        return -1
                    end
                else
                    if File.exist?(@info[:to_path])
                        # SKIP MOVE
                        return 0
                    else
                        # CONFLICT LOST
                        # possibly transmogrify into a create after finding the original?
                        return -1
                    end
                end
            end
        end
    end



    # The most complicated operation, it performs a delta between two files
    # and records the delta, it's length, and the mtime.  It will avoid recording
    # a delta if the file's contents haven't changed.
    #
    # Required info options:
    #
    # * :path -- The file path relative to :source and @dir
    # * :source -- The source directory to use for analysis,  @dir is considered target.
    class DeltaOperation < Operation
    
        TYPE = "delta"
        
        def store(journal_out, data_out)
            path, source, target = @info[:path], @info[:source], @dir
            
            # don't do anything if its a symlink
            if File.symlink? path
                # we just ignore symlinks since the real change happens in the target file
                info[:symlink] = true
                UI.event :warn, "Deltas against symlinks are ignored since they are pointless"
            else
                # setup the remaining journal info
                target_path = File.join(target, path)
                source_path = File.join(source, path)

                @info[:mtime] = File.mtime(target_path)

                # read the gear and do the delta
                src_data = File.read(source_path)
                tgt_data = File.read(target_path)

                # write the delta to a string io temporarily
                io_out = StringIO.new
                results = SuffixArrayDelta::make_delta(src_data, tgt_data, io_out)
            
                # don't bother if there's no changes
                if no_changes?(results, src_data.length, tgt_data.length)
                    # no changes actually, so we configure this command that with a 0 length
                    @info[:length] = 0
                else
                    # there's actual changes so write it out after recording the length
                    @info[:length] = io_out.pos
                    io_out.rewind
                    data_out.write io_out.read
                end
            end
            
            # we need to clean out the :source parameter before it gets stored to the journal
            @info.delete :source
            
            super(journal_out, data_out)
        end
    
        
        # Run reads in the delta, changes to the dir, and then applies the delta
        # in order to create the changed file.  It will skip a file if it's missing.
        def run(data_in)
            path, mtime, length, digest = @info[:path], @info[:mtime], @info[:length], @info[:digest]
            
            # don't bother running if this is a symlink
            if @info[:symlink]
                UI.event :warn, "Skipped delta of symlink #{path}"
            else
                outfile = nil
                
                begin
                    Dir.chdir @dir do 
                        # skip the file if it doesn't exist
                        if not File.exist? path
                            UI.failure :missing,  "Can't delta #{path}, file missing"
                            return false
                        end
                        
                        # no need to process the the delta if it's 0 length
                        if length > 0
                            reference = File.read path
                            if digest != Digest::MD5.hexdigest(reference)
                                UI.failure :constraint, "The reference file digests don't match.  Can't apply this delta."
                            else
                                # digest matches, we can continue
                                delta = StringIO.new(data_in.read(length))
                                outfile = File.open(path, "wb")
                                
                                SuffixArrayDelta::apply_delta(reference, delta, outfile)
                                
                                outfile.close
                            end
                        end
                
                        # update the file's mtime
                        File.utime(Time.new, mtime, path)
                    end
                rescue
                    UI.failure :delta, "#$!"
                    return false
                end
            end
            
            return true
        end
    
        # Performs tests to see if it's safe/possible to perform this operation
        def test(data_in)
            path, mtime, length = @info[:path], @info[:mtime], @info[:length]
            
            begin
                Dir.chdir @dir do
                    return true if @info[:symlink]
                    
                    return false unless File.exist? path
                    
                    # do a test run of the delta on the file contents
                    if length > 0
                        delta = StringIO.new(data_in.read(length))
                        reference = File.read path
                        outfile = StringIO.new
                        
                        SuffixArrayDelta::apply_delta(reference, delta, outfile)
                    end
                end
            rescue
                UI.failure :delta, "#$!"
                # no need to skip here since we actually read during this test
                return false
            end
            
            return true
        end
        
        
        #* delta:
        #	* path exists, source different == CONFLICT, make target, DIFF
        #	* path exists, source same == DELTA
        #	* path missing == CONFLICT, CREATE
        #
        def merge
            Dir.chdir @dir do
                if File.exist? @info[:path]
                    digest = Digest::MD5.hexdigest(File.read(@info[:path]))
                    if digest == @info[:digest]
                        # APPLY DELTA
                        return 1
                    else
                        # CONFLICT DELTA
                        return -1
                    end
                else
                    # CONFLICT, CREATE
                    return -1
                end
            end
        end
        
    
        # Skips ahead in the data stream by the @length
        def skip(data_in)
            data_in.seek(@info[:length], IO::SEEK_CUR)
        end
        
        def no_changes?(results, src_length, tgt_length)
            match_count, match_total, insert_count, insert_total = results
            return (src_length == tgt_length and match_count == 1 and match_total == src_length and insert_count == 0 and insert_total == 0)
        end
        
    end

    

    # Responsible for deleting the directories which don't exist anymore
    # but doing it so that there is no damage if the person has some files 
    # in the directory.
    # 
    # Required info parameters are:
    #
    # * :deleted_dirs -- The directories that are deleted from the target.
    # * :created_dirs -- The directories that are created from the target.
    class DirectoryOperation  < Operation  
    
        TYPE = "directory"
        
        # Changes to the dir and deletes the file.
        def run(data_in)
            deleted_dirs = @info[:deleted_dirs].sort.reverse
            created_dirs = @info[:created_dirs].sort
            
            begin
                Dir.chdir @dir do
                    deleted_dirs.each do |path|
                        begin
                            # only delete them if they exist, and use rmdir to avoid deleting full directories
                            if File.exist?(path)
                                UI.event :rmdir, path
                                FileUtils.rmdir path
                            end
                        rescue
                            UI.failure :directory, "Could not remove directory: #$!"
                        end
                    end
                    
                    created_dirs.each do |path|
                        if not File.exist? path
                            UI.event :mkdir, path
                            FileUtils.mkdir_p path
                        end
                    end
                end
            rescue
                UI.failure :directory, "#{$!}"
                return false
            end
            
            return true
        end
        
        # Performs tests to see if it's safe/possible to perform this operation
        def test(data_in)
            deleted_dirs = @info[:deleted_dirs].sort
            created_dirs = @info[:created_dirs].sort.reverse
            
            begin
                Dir.chdir @dir do
                    deleted_dirs.each do |path|
                        FileUtils::DryRun.rmdir path
                    end
                    
                    created_dirs.each do |path|
                        FileUtils::DryRun.mkdir_p path
                    end
                end
                
            rescue
                UI.failure :delta, "#{$!}"
                return false
            end
            
            return true
        end

        # Directory operations are designed to be safe anyway.  They only
        # work on directories which are already empty or create new directories.
        # All that's done here is to compare the directories and see if this
        # operation needs to be run at all.
        def merge
            Dir.chdir @dir do
                @info[:deleted_dirs].each do |dir|
                    if File.exist? dir
                        return 1
                    end
                end
                
                @info[:created_dirs].each do |dir|
                    if not File.exist? dir
                        return 1
                    end
                end
            end
            
            # looks safe to skip it
            return 0
        end
    end
    
    
    
    # A small utility function that safely creates a path leading up to a given file.
    # It does this by splitting off the base path with File#split, and then creating the
    # path.
    #
    # If the path exists, then it checks to see if it is a directory.  In the event that the
    # target path is not a directory it will delete it and then re-create it as such.  This
    # means that if you have a file there, then the file will be destroyed.
    def ChangeSet.create_target_path(filename)
        path = File.split(filename)[0]

        if File.exist? path
            # target path exists, but sometimes the target path is a file
            # this doesn't get picked up by the delete operations so we delete it here
            # and recreate the file as a directory for further operations
            if not File.directory? path
                UI.event :warn, "#{path} exists but is not a directory, deleting and recreating as a directory (will backup with ~ ending)"
                # create a backup of the file with a ~ ending
                FileUtils::mv(path, path +"~")
                FileUtils::rm_rf(path)

                FileUtils::mkdir_p(path)
            end
        else
            FileUtils::mkdir_p(path)
        end
    end
    
end    
