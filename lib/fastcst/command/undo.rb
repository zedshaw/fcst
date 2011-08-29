require 'fastcst/ui'
require 'fastcst/repo'


module Repository

    class UndoCommand < Command
        def initialize(argv)
            @directory = "."
        
            super(argv, [])
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find the repository directory"
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid?(@repo['Current Revision']==nil, "You cannot undo while you are making a new revision.  Use abort.")
            end            
            
            return @valid
        end
    
    
        def run
            rev_path = @repo['Path']
            id = rev_path.pop
            cs_path, md = @repo.find_changeset(id)
            
            undo_journal = File.join(cs_path, Repository::UNDO_JOURNAL)
            undo_data = File.join(cs_path, Repository::UNDO_DATA)
            
            if not File.exist?(undo_journal)
                UI.failure :constraint, "No #{Repository::UNDO_JOURNAL} file so there's no undo possible."
            elsif not File.exist?(undo_data)
                UI.failure :constraint, "No #{Repository::UNDO_DATA} file so there's no undo possible."
            elsif cs_path
                # get the undo revision and apply it to both directories
                
                UI.start_finish("Applying undo revision for #{md['Revision']}") do
                    journal_in = Zlib::GzipReader.new(File.open(undo_journal))
                    data_in = Zlib::GzipReader.new(File.open(undo_data))
                    
                    ChangeSet.apply_changeset(journal_in, data_in, ".")
                end
                
                UI.start_finish("Applying undo revision to originals directory") do
                    journal_in = Zlib::GzipReader.new(File.open(undo_journal))
                    data_in = Zlib::GzipReader.new(File.open(undo_data))
                    
                    ChangeSet.apply_changeset(journal_in, data_in, @repo.originals_dir)
                end
                
                # now update the path to have the new 
                @repo['Path'] = rev_path
            else
                UI.failure :constraint, "It appears that you have a revision listed in your path with a missing directory."
            end
        end
    end
end 
