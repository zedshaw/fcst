require 'fastcst/repo'
require 'fastcst/ui'
require 'fastcst/metadata'

module Repository

    # Very simple command to list the changesets in your repository.
    class ListCommand < Command
    
        def initialize(argv)
            super(argv, [
                ["-r", "--rev NAME", "A revision name to get the ID", :@rev],
            ])

            @repo_dir = Repository.search
        end

        def validate
            valid? @repo_dir, "Could not find a .fastcst directory"
            return @valid
        end
        
        def print_readable_path(repo)
            path = repo['Path']
            if not path
                UI.failure :env, "Your environment does not have a revision path.  Did you delete that variable?"
                return
            end
            
            # build the readable path first
            readable_path = []
            path.each do |id|
                readable_path << repo.build_readable_name(id)
            end
            
            puts "[ #{readable_path.join('/')} ]"
        end
        
        def print_children(repo, id)
            # and now go through each child and print it tabbed in with the id
            children = nil
            if repo['Path'].empty?
                children = repo.list_changesets
            else
                children = repo.find_all_children(id)
            end
            
            # make a new hash that 
            children.each do |id|
                puts "\t#{repo.build_readable_name(id)} -- #{id}"
            end
        end
        
        
        def run
            repo = Repository.new @repo_dir

            print_readable_path(repo)

            # either print the requested revision (or ones like it) or print the current path top
            if @rev
                # they just want a revision name to get the ID
                possibles = repo.find_revision(@rev)
                possibles.each do |rev, id|
                    path, md = repo.find_changeset(id)
                    puts "#{rev} -- #{id} -- #{md['Created By']['E-Mail']}"
                    print_children(repo, id)
                end
            else
                id = repo['Path'].pop
                print_children(repo, id)
            end
                                    
            if repo['Current Revision']
                md = MetaData.load_metadata(File.join(repo.work_dir, MetaData::META_DATA_FILE))
                puts "Current Revision: #{md['Revision']} -- #{repo['Current Revision']}"
            end
        end
    end
    
end

