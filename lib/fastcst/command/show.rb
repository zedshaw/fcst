require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'


module Repository

    class ShowCommand < Command
        def initialize(argv)
            super(argv, [
            ["-i", "--id ID", "Specify a changeset ID to send (defaults to current)", :@id],
            ["-r", "--rev ID", "Specify a changeset Revision name to send", :@rev],
            ["-c", "--current", "The currently active revision (the one you're building)", :@current],
            ["-l", "--list", "List the operations and file names in the journal.", :@list]
            ])
            
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find repository directory"
            valid?((@id or @rev or @current), "You must specify either an id or revision name (or use -c)")
            valid?((not (@id and @rev)), "You cannot specify an id (-i) AND a revision name (-r)")
            
            return @valid
        end
    
    
        def run
            repo = Repository.new @repo_dir
            
            if @current
                puts "**** You are currently building ****"
                
                puts File.read(File.join(repo.work_dir, MetaData::META_DATA_FILE))
            else
                @id = repo.resolve_id(@rev, @id)
                
                if not @id
                    UI.failure :search, "Could not find the specified revision"
                    return
                end
                
                cs_path, md = repo.find_changeset(@id)
                
                if not cs_path
                    UI.failure :constraint, "Wow, your repository looks hosed since #{id} doesn't exist in the changeset list"
                else
                    puts "**** Revision meta-data ****"
                    YAML.dump(md, $stdout)
                    
                    if @list
                        puts "\n\n----- Revision Journal Contents -----"
                        journal_file, data_file = MetaData.extract_journal_data(md)
                        journal_in = Zlib::GzipReader.new(File.open(File.join(cs_path, journal_file)))
                        
                        YAML.each_document(journal_in) do |type, info|
                            puts "#{type}: #{info[:path]}" if info[:path]
                        end
                        
                        journal_in.close
                    end
                end
            end
        end
    end
end


