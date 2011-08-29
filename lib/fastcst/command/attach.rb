require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'


module Repository

    class AttachCommand < Command
        def initialize(argv)
            super(argv, [
                ["-f", "--file NAME", "File's name", :@name],
                ["-p", "--purpose STRING", "This file's purpose", :@purpose]
            ])
            
            @repo_dir = Repository.search
        end
        
        def validate
            valid? @repo_dir, "Could not find the repository directory"
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid? @repo['Current Revision'], "You cannot attach until you start a new revision with 'begin'"
            end
            
            valid? @name, "Provide a file name to include"
            valid? @purpose, "All files must have a purpose"
            valid_exists? @name, "File #{@name} doesn't exist"
            valid_file? @name, "File #{@name} is not a file"
            
            return @valid
        end
        
        def run
            repo = Repository.new @repo_dir
            
            # check that a revision is in progress
            if not repo['Current Revision']
                UI.failure :constraint, "You have not used begin to start a revision yet."
                return
            end
            
            md_file = File.join(repo.work_dir, MetaData::META_DATA_FILE)
            
            MetaData.add_file(md_file, @name, @purpose)
        end
    
    end


end

