require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'


module Repository

    class LogCommand < Command
        def initialize(argv)
            super(argv, [
                ["-s", "--show", "Show the current log", :@show]
            ])
            
            @repo_dir = Repository.search
            @message = argv.join(" ")
        end
        
        def validate
            valid? @repo_dir, "Could not find the repository directory"
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid? @repo['Current Revision'], "You cannot log until you start a new revision with 'begin'"
            end
            
            return @valid
        end
        
        def run
            
            # check that a revision is in progress
            if not @repo['Current Revision']
                UI.failure :constraint, "You have not used begin to start a revision yet."
                return
            end

            
            md_file = File.join(@repo.work_dir, MetaData::META_DATA_FILE)
            
            if @show
                # they want to see the current log
                md = MetaData.load_metadata(md_file)
                journal = md['Journal']
                
                journal.each do |log|
                    print "[#{log['Date']}] :  #{log['Message']}\n"
                end
            else
                MetaData.log_message(md_file, @message)
            end
        end
    
    end


end

