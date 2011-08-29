require 'set'
require 'find'
require 'digest/md5'
require 'yaml'
require 'sadelta'
require 'stringio'
require 'fileutils'
require 'fastcst/ui'
require 'zlib'
require 'fastcst/operation'

include SuffixArrayDelta


module ChangeSet

    JOURNAL_FILE_SUFFIX = ".yaml.gz"
    DATA_FILE_SUFFIX = ".fcs"
    
    # = Introduction
    # 
    # ChangeSet performs an analysis of a source and target directory and then allows you
    # to write the changeset to two files.  To apply a ChangeSet you simply use the 
    # ChangeSet#apply_changeset function.  Applying a changeset is incredibly simple because
    # of the way a changeset file is designed.
    #
    # The initialize function does most of the work, but leaves the moved files detection to
    # detect_moved_files.  This is done since moved files detection is really optional, and may
    # not be requested.
    #
    # = Design
    #
    # A ChangeSet object performs an analysis of the source and target directory.  It then
    # writes a YAML dump of a series of Operation objects (MoveOperation, DeltaOperation,
    # DeleteOperation, and CreateOperation) and write necessary data to a raw data output.
    # This makes creating a changeset file incredibly easy and makes it easy to create new
    # operations.  Once all the operations are written to disk then the changeset is finished.
    #
    # Operations which need to store data use a given data output stream (which writes to a file)
    # and keep track of how much they write.  When an Operation is applied it then knows
    # how much to read from the data stream to do its job.  The consequence of this is that
    # the operations must be run in order.  Each operation supports a skip method in order
    # to allow it to be skipped.
    #
    # Applying a changeset becomes incredibly easy then:  just load each operation from the
    # YAML file and call its run method.  The run method knows what it needs to do, and only
    # needs the directory to do it in and the data stream to read stuff from (if necessary).
    #
    # One key thing is that, if skip is called, and the operation expects to read a certain 
    # amount of data from the data stream, then it should seek ahead that much so the next
    # operation is not messed up.
    #
    # = Usage
    #
    # For more information on how it is used refer to MakeChangeSetCommand#run.
    #
    # = Future Directions
    #
    #   * It doesn't record directory change information yet.  This is coming soon.
    #   * No crypto yet.  That's a requirement in the future.
    #
    # = Weirdness
    #
    # One thing that you will notice is that this class uses the UI module in ui.rb to give
    # all it's presentation to the user.  This module is an abstraction away from the actual
    # display method which will eventually be designed to support different UI systems 
    # depending on user preference or build options.
    #

    class ChangeSetBuilder
        attr_reader :deleted, :created, :common, :moved, :changed
    
        # Does the majority of the change detection using the Set class.  It basically
        # scans both source and target, and then determines the deleted, created, and common
        # files in each by full pathname (this does not include directories yet).  Then
        # it scans the common files to see which ones have changed mtimes and assumes these
        # are changed.  Even if the files didn't really change, they did have an attribute
        # (mtime) change so they need to be recorded.  The DeltaOperation class will
        # figure out what really changed and record appropriately.
        # 
        # Moved file detection is done with detect_moved_files since this is optional.
        def initialize(source, target)
            @source = source
            @target = target

            src_files, src_dirs = ChangeSetBuilder.scan(@source)
            tgt_files, tgt_dirs = ChangeSetBuilder.scan(@target)

            # use Set to figure out what has possibly changed
            @deleted = src_files - tgt_files
            @created = tgt_files - src_files
            @common = src_files & tgt_files
            @moved = {}  # initially empty until dected_moved_files is requested
            @changed = {}
            
            # directories are handled after everything else
            @deleted_dirs = src_dirs - tgt_dirs
            @created_dirs = tgt_dirs - src_dirs
            
            @common.each do |file|
                otime = File.mtime(File.join(source,file))
                ntime = File.mtime(File.join(target,file))
                if otime != ntime
                    # file has changed, but we if the old file is 0 length then its actually a create
                    if File.size(File.join(source,file)) == 0
                        @created << file
                    else
                        @changed[file] = [otime, ntime]
                    end
                end
            end
        end
    
    
        # Performs a simple moved file detection operation.  A moved file is defined as:
        # 
        #   1.  Has same file name.
        #   2.  Has different base path.
        #   3.  Has same file size.
        #   4.  Has same md5 digest.
        #
        # The algorithm first indexes the basenames of the @deleted and @created files and then
        # goes through the deleted files and created files looking for basenames with only 1 element
        # in the value array.  When it finds one then it checks their sizes and md5 digests.  If
        # everything checks out then it records this as a move and removes the files from the
        # @created and @deleted lists.
    
        def detect_moved_files
            md5sums = {}  # use as a reverse lookup
        
            del_basenames = ChangeSetBuilder.index_base_names(@deleted)
            tgt_basenames = ChangeSetBuilder.index_base_names(@created)
            
            del_basenames.each do |basename, files|
                # we only process files that have unique locations mentioned, this is a direct possible move
                tgt_files = tgt_basenames[basename]
                if tgt_files && tgt_files.length == 1 && files.length == 1
                    del_digest = ""
                    tgt_digest = ""
                    from_file = files[0]
                    to_file = tgt_files[0]
                    
                    # next test is to simply compare files sizes, can't be same file if different size
                    if File.size?(@source + '/' + from_file) == File.size?(@target + '/' + to_file)
                        
                        # now generate the hashes for both files as the final confirmation of same file
                        Dir.chdir(@source) { del_digest = Digest::MD5.digest(File.read(from_file)) }
                        Dir.chdir(@target) { tgt_digest = Digest::MD5.digest(File.read(to_file)) }
                        
                        if del_digest == tgt_digest
                            # digests match so the basenames are the same and the digests are the same, it's a move
                            @moved[from_file] = [to_file, File.stat(File.join(@target,to_file)).mtime]
                            
                            # and clean the files from either side to eliminate them
                            @deleted.delete from_file
                            @created.delete to_file
                        end
                    end
                end
            end
        end
    
    
        # Returns true if there are detected changes.
        def has_changes?
            @deleted.size > 0 || @created.size > 0 || @changed.size > 0 || @moved.size > 0
        end

        
    
        # Write journal and data to the two output streams.  It basically
        # just creates the correct operation objects in order and writes
        # them to the journal output stream as a series of YAML documents.
        def write_changeset(journal_out, data_out)
        
            @deleted.sort.each do |path|
                digest = Digest::MD5.hexdigest(File.read(File.join(@source, path)))
                op = DeleteOperation.new({:path => path, :digest => digest}, @target)
                op.store(journal_out, data_out)
            end
        
            @moved.sort.each do |from, to_info|
                digest = Digest::MD5.hexdigest(File.read(File.join(@source, from)))
                op = MoveOperation.new({:path => from, :digest => digest, :to_path => to_info[0], :mtime => to_info[1]}, @target)
                op.store(journal_out, data_out)
            end
        
            @created.sort.each do |path|
                digest = Digest::MD5.hexdigest(File.read(File.join(@target, path)))
                op = CreateOperation.new({:path => path, :digest => digest}, @target)
                op.store(journal_out, data_out)
            end
        
            @changed.sort.each do |file, info|
                digest = Digest::MD5.hexdigest(File.read(File.join(@source, file)))
                op = DeltaOperation.new({:source => @source, :digest => digest, :path => file}, @target)
                op.store(journal_out, data_out)
            end
            
            # finally we write a DirectoryOperation that is responsible for intelligently
            # deleting any directories which no longer exist in the target
            op = DirectoryOperation.new({:deleted_dirs => @deleted_dirs.sort, :created_dirs => @created_dirs.sort}, @target)
            op.store(journal_out, data_out)
        end
    
    
    
        # Private method that does an inverse indexing of the basenames in the given hash.
        def self.index_base_names(filenames)
            basenames = {}
            filenames.each do |list|
                list.each do |file|
                    base = File.basename file
                    basenames[base] ||=[]
                    basenames[base] << file
                end
            end
        
            return basenames
        end
    
        # Scans a directory for all the files, but skips anything that isn't a file or directory
        # It also skips unix style "hidden" files which start with a "." so that it avoids
        # files that usually aren't wanted.  There is a real need to create an excludes
        # list of some sort for this.
        # 
        # It returns two Set objects, one for the files and one for the directories it finds.
        def self.scan(dir)
            file_results = Set.new
            dir_results = Set.new
            
            Dir.chdir(dir) do
                Find.find(".") do |file|
                    base = File.basename(file)

                    # TODO: allow for configurable exclude lists
                    if base != "." and base[0,1] == "." or base == "CVS"
                        if File.directory?(file)
                            # skip this directory
                            Find.prune
                            # otherwise we just ignore the hidden file
                        end
                    elsif File.file? file
                        file_results << file
                    elsif File.directory? file
                        dir_results << file
                    end
                end
            end
            
            return file_results, dir_results
        end
    end



    # Analyzes the journal file (input IO) and produces a hash with some statistics
    # in it.
    def ChangeSet.statistics(journal_in)
        stats = {"moves" => 0, "creates" => 0, "deletes" => 0, "deltas" => 0}
        # no need to run skip since we're not doing anything other than counting them
        YAML.each_document(journal_in) do |info|
            case info[0]
            when DeleteOperation::TYPE:
                stats["deletes"] += 1
            when CreateOperation::TYPE:
                stats["creates"] += 1
            when MoveOperation::TYPE:
                stats["moves"] += 1
            when DeltaOperation::TYPE:
                stats["deltas"] += 1
            when DirectoryOperation::TYPE:
                stats["deleted directories"] = info[1][:deleted_dirs]
                stats["created directories"] = info[1][:created_dirs]
            else
                UI.failure :input, "Unknown operation #{info[0]}"
            end
        end

        return stats
    end


    # A function that reads a journal and data input stream,
    # and runs the operations against the target directory.
    # It closes the input streams for the caller to ensure that
    # everything is cleaned up properly.
    #
    # Setting test_run==true will run all operations in test mode where they don't actually
    # do anything to the target directory, but will report on any errors and return
    # true/false if they work or not.
    def ChangeSet.apply_changeset(journal_in, data_in, target_dir, test_run=false)
        begin
            failure_count = 0

            YAML.each_document(journal_in) do |info|
                op = Operation.create(info, target_dir)
                
                # add one to the failure count unless the operation runs fine
                if test_run
                    failure_count += 1 unless op.test(data_in)
                else
                    failure_count += 1 unless op.run(data_in)
                end
            end
        ensure
            journal_in.close if journal_in
            data_in.close if data_in
        end

        return failure_count
    end


    # A utility method to easily create a changeset given just the
    # changeset name (it adds the ChangeSet::JOURNAL_FILE_SUFFIX and ChangeSet::DATA_FILE_SUFFIX for the journal
    # and data files).  It returns the ChangeSetBuilder for you to
    # analyze, and it will not make the changeset if there are
    # no changes reported.
    def ChangeSet.make_changeset(cs_name, source, target)
        changes = ChangeSetBuilder.new(source, target)

        if not changes.has_changes?
            UI.event :exit, "Nothing changed.  Exiting."
        else
            begin
                changes.detect_moved_files

                md_out = Zlib::GzipWriter.new(File.open(cs_name + JOURNAL_FILE_SUFFIX, "w"))
                data_out = Zlib::GzipWriter.new(File.open(cs_name + DATA_FILE_SUFFIX, "w"))

                changes.write_changeset(md_out, data_out)
            ensure
                md_out.close
                data_out.close
            end
        end

        return changes
    end
end