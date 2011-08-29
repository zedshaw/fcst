require 'fastcst/ui'
require 'fastcst/repo'
require 'zlib'
require 'yaml'
require 'fastcst/changeset'
require 'fastcst/metadata'
require 'set'
require 'digest/md5'




# The merge algorithm does a union of the set of files in the journal and in
# the source directory to find out which paths are common between them.
# This eliminates the sitations in the above 
class MergeCommand < Command
    
    def initialize(argv)
        @directory = "."
        @conflicts = []
        
        super(argv, [
        ["-r", "--revision NAME", "The name of a revision to merge", :@rev],
        ["-i", "--id UUID", "The ID of the changeset to merge", :@id]
        ])
        
        @repo_dir = Repository::Repository.search
    end
    
    def validate
        valid? (@rev or @id), "You must give either a revision or id to merge"
        valid? @repo_dir, "Could not find the repository directory"
        if @repo_dir
            @repo = Repository::Repository.new @repo_dir
            @id = @repo.resolve_id(@rev, @id)
            valid? @repo['Current Revision'], "You cannot merge until you start a new revision with 'begin'"
        end
        
        valid? @id, "Could not find a suitable ID."
        
        return @valid
    end
    
    def run
        cs_path, md = @repo.find_changeset(@id)
        journal_file, data_file = MetaData.extract_journal_data(md)
        journal_path = File.join(cs_path, journal_file)
        puts "Loading journal #{journal_path}"
        
        journal = Zlib::GzipReader.new(File.open(journal_path))

        # build the inverted list of files and things done to them, and the set of files
        apply_count = 0   # used later to figure out if we need to do anything
        YAML.each_document(journal) do |data|
            op = ChangeSet::Operation.create(data, ".")
            res = op.merge
            if res < 0
                @conflicts << op
                puts "CONFLICT #{op.class::TYPE}: #{op.info[:path]}"
            elsif res == 0
                puts "SKIP #{op.class::TYPE}: #{op.info[:path]}"
            elsif res == 1
                puts "APPLY #{op.class::TYPE}: #{op.info[:path]}"
                apply_count += 1
            else
                puts "ERROR: invalid response #{res} for #{op.class::TYPE}"
            end
        end
                
        if @conflicts.empty?
            # No point in continuing if everything was skipped and there were no conflicts
            if apply_count == 0
                puts "Seems like this changeset is already merged."
                return
            end
            
            good = UI.ask("Looks like there are no conflicts.  Want to merge now? [yN]")
            
            if good.downcase == "y"
                # since there are no conflicts we can just apply this thing like normal
                journal.rewind
                
                data_path = File.join(cs_path, data_file)
                data = Zlib::GzipReader.new(File.open(data_path))
                
                YAML.each_document(journal) do |info|
                    op = ChangeSet::Operation.create(info, ".")
                    # having merge return 1 means that this operation is "greater" (has
                    # more information) so it should be applied
                    if op.merge == 1
                        op.run(data)
                    elsif op.merge == -1
                        puts "WHAT! No conflicts were detected, yet now there's conflicts!"
                    end
                end
                
                # and now we add a disposition record saying that this revision was merged in
                md_file = File.join(@repo.work_dir, MetaData::META_DATA_FILE)
            
                MetaData.add_disposition(md_file, "revision", @id, "merge")
            end
        else
            puts "#{@conflicts.length} conflicts with this merge." if not @conflicts.empty?
        end
    end
end

