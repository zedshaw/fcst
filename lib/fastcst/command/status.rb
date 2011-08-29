require 'fastcst/changeset'

module Repository
    # Uses ChangeSet to create the set of changeset files.  It writes a
    # .yaml, .new.fcs, and .chg.fcs file beginning with the "--changeset NAME"
    # argument.  This will change in the future.
    class StatusCommand < Command
    
        def initialize(argv)
            @full_check = false
        
            super(argv, [
            ["-f", "--[no-]full", "Full check (defaults to quick check)", :@full_check]
            ])
            
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find a repository directory"
            return @valid
        end
    
        def run
            repo = Repository.new @repo_dir
            
            base_dir = File.dirname(repo.path)
            
            changes = ChangeSet::ChangeSetBuilder.new(repo.originals_dir, base_dir)
            
            if not changes.has_changes?
                UI.event :exit, "Nothing changed.  Exiting."
            else
                changes.detect_moved_files
            
                # now we just print out the results
                if @full_check
                    UI.event :info, "--- Deleted Files:"
                    changes.deleted.sort.each { |path| UI.event :delete, path }
                
                    UI.event :info, "--- Moved Files:"
                    changes.moved.sort.each do |from, to_info|
                        UI.event :moved, "#{from} -> #{to_info[0]}"
                    end
                
                    UI.event :info, "--- Created Files:"
                    changes.created.sort.each { |path| UI.event :created, path }
                
                    UI.event :info, "--- Changed Files:"
                    changes.changed.sort.each { |file, info| UI.event :changed, file }
                end
            
                # print the summary
                UI.event :info, "Deleted: #{changes.deleted.length}, Moved: #{changes.moved.length}, Created: #{changes.created.length}, Changed: #{changes.changed.length}"
            
                if repo['Current Revision']
                    md = MetaData.load_metadata(File.join(repo.work_dir, MetaData::META_DATA_FILE))
                    puts "Current Revision: #{md['Revision']} -- #{repo['Current Revision']}"
                end
                
            end
        end
    end

end


