require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'


module Repository

    class BeginCommand < Command
        def initialize(argv)
            @directory = "."
        
            super(argv, [
                ["-p", "--purpose TEXT", "The purpose of this revision", :@purpose],
                ["-r", "--revision NAME", "A name for the revision (make sure it can be a file name)", :@revision]
            ])
            
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find a repository directory"
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid? @repo['Current Revision']==nil, "You are already working on revision #{@repo['Current Revision']}."
            end
            
            valid? @purpose, "You need to specify a purpose for this revision"

            return @valid
        end
    
    
        def run
            md_file = MetaData::META_DATA_FILE
            user = @repo['Created By']
            md = nil
        
            if not @revision
                # need to make a revision based on the most recent one
                rev_path = @repo['Path']
                path, md = @repo.find_changeset(rev_path.last)
                # ruby is cool, this works for just about anything even characters and other goodies in a revision name
                # we have to break off the ending parts of the revision in order to avoid unexpected
                # behavior where it will convert 0.5.9 to 0.6.0 instead of 0.5.9.10.
                rev = md['Revision'].split(".")
                rev.push(rev.pop.succ)
                @revision = rev.join(".")
                UI.event :info, "Auto-generated revision: #{@revision}. (use -r to set it manually)"
            end
            
            Dir.chdir @repo.work_dir do
                # create the initial meta-data
                MetaData.create_metadata(md_file, @repo['Project'], @revision, @purpose, user['Name'], user['E-Mail'])
                
                md = MetaData.load_metadata(md_file)
            end
            
            # now setup our information to indicate that we're currently working on this revision
            @repo['Current Revision'] = md['ID']
        end
    
    end
end

