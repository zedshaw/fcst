require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'

module Repository

    class DispCommand < Command
        def initialize(argv)
            super(argv, [
                ["-t", "--type TYPE", "Disposition type", :@type],
                ["-i", "--id ID", "The 'id' of the disposition (like URL)", :@id],
                ["-r", "--relation TEXT", "This disposition's relationship to the revision", :@relation]
                
            ])
            
            @repo_dir = Repository.search
        end
        
        def validate
            valid? @repo_dir, "Could not find the repository directory"
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid? @repo['Current Revision'], "You cannot add disposition until you start a new revision with 'begin'"
            end
            
            valid? @type, "You must specify a type"
            valid? @id, "The disposition must have an ID of some kind"
            valid? @relation, "You must specify a relation to this revision"
            
            return @valid
        end
        
        def run
            
            # check that a revision is in progress
            if not @repo['Current Revision']
                UI.failure :constraint, "You have not used begin to start a revision yet."
                return
            end
            
            md_file = File.join(@repo.work_dir, MetaData::META_DATA_FILE)
            
            MetaData.add_disposition(md_file, @type, @id, @relation)
        end
    
    end
end

