require 'fastcst/ui'
require 'fastcst/distrib'
require 'fastcst/repo'

module Repository

    UNDO_JOURNAL = "undo.yaml.gz"
    UNDO_DATA = "undo.fcs"
    
    
    class FinishCommand < Command
        def initialize(argv)
            super(argv, [
            ])
            
            @repo_dir = Repository.search
        end
        
        def validate
            valid? @repo_dir, "Could not find the repository directory"
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid? @repo['Current Revision'], "You cannot finish until you start a new revision with 'begin'"
            end
            
            return @valid
        end
        
        def run
            
            # check that a revision is in progress
            if not @repo['Current Revision']
                UI.failure :constraint, "You have not used begin to start a revision yet."
                return
            end
            
            parent_id = @repo['Path'].pop
            md_file = File.join(@repo.work_dir, MetaData::META_DATA_FILE)
            md = MetaData.load_metadata(md_file)
            cs_name = @repo['Project'] + '-' + md['Revision']
            
            Dir.chdir @repo.work_dir do
                originals = File.join("..","originals")
                sources = File.join("..","..")
                data_file = cs_name + ChangeSet::DATA_FILE_SUFFIX
                journal_file = cs_name + ChangeSet::JOURNAL_FILE_SUFFIX
                    

                UI.start_finish("Creating revision") do
                    changes = ChangeSet.make_changeset(cs_name, originals, sources)
                    
                    # abort if there were no changes
                    if not changes.has_changes?
                        return
                    end
                end
                
                # create the undo in the reverse direction
                UI.start_finish("Creating 'undo' revision") do
                    changes = ChangeSet.make_changeset("undo", sources, originals)
                end
                
                UI.start_finish("Syncing with the originals directory") do
                    # sync the originals directory
                    journal_in = Zlib::GzipReader.new(File.open(journal_file))
                    data_in = Zlib::GzipReader.new(File.open(data_file))
                    
                    # apply closes the files for us
                    ChangeSet.apply_changeset(journal_in, data_in, originals)
                    
                    # create the meta-data
                    MetaData.finish_metadata(md_file, parent_id, data_file, journal_file)
                end
            end
        
            # store the newly created changeset, doing a move instead of a copy
            md = @repo.store_changeset @repo.work_dir, MetaData::META_DATA_FILE, move=true
            
            # get the new location and move the undo there
            cs_path, md = @repo.find_changeset(md['ID'])
            undo_journal_file = File.join(@repo.work_dir, UNDO_JOURNAL)
            undo_data_file = File.join(@repo.work_dir, UNDO_DATA)
            FileUtils.mv(undo_journal_file, cs_path)
            FileUtils.mv(undo_data_file, cs_path)
            
            # and update the environment to reflect our new revision path
            @repo["Path"] = @repo["Path"] << md['ID']
            
            UI.event :finished, "New revision: #{@repo.build_readable_name md['ID']}"
            
            # and remove the current revision
            @repo.delete 'Current Revision'
        end
    
    end
end
