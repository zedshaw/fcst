require 'fastcst/ui'
require 'fastcst/repo'


module Repository

    class ApplyCommand < Command
        def initialize(argv)
            @directory = "."
            @undo = true
            
            super(argv, [
            ["-i", "--id ID", "Specify a changeset ID to send (defaults to current)", :@id],
            ["-r", "--rev ID", "Specify a changeset Revision name to send", :@rev],
            ["-t", "--test", "Run the apply in test mode (does nothing, reports failures)", :@test_run]
            ])
            
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find repository directory"
            valid?((@id or @rev), "You must specify either an id or revision name")
            valid?((not (@id and @rev)), "You cannot specify an id (-i) AND a revision name (-r)")
            
            if @repo_dir
                @repo = Repository.new @repo_dir
                valid?(@repo['Current Revision']==nil, "You cannot apply while you are making a new revision.")
            end

            
            return @valid
        end
    
        
        
    
        def run
            @id = @repo.resolve_id(@rev, @id)
            target = File.dirname(@repo.path)  # need to know the source root to apply at
            originals = File.expand_path(@repo.originals_dir)  # need the full path
            
            if @id
                # found an id, but need to verify that it's a child of the current revision
                parent_id = @repo['Path'].pop
                
                # a special case is if we're bootstrapping an empty dir, in which case
                # the path is empty so there will be no parent.  This is allowed.
                if @repo['Path'].empty? or parent_id == @repo.find_parent_of(@id)
                    # alright, looks like we're in business
                    cs_path, md = @repo.find_changeset(@id)
                    data_file = nil
                    journal_file = nil

                    Dir.chdir cs_path do
                        journal_file, data_file = MetaData.extract_journal_data(md)
                        
                        if not journal_file or not data_file
                            UI.failure :contents, "The meta-data did not contain proper journal and data file contents."
                            return 
                        elsif MetaData.verify_digests(cs_path, md).length > 0
                            UI.failure :security, "The changeset has been tampered with.  Aborting."
                            return
                        end
                            
                        
                        # apply_changeset closes the streams for us
                        UI.start_finish("Applying to main directory") do
                            journal_in = Zlib::GzipReader.new(File.open(journal_file))
                            data_in = Zlib::GzipReader.new(File.open(data_file))
                            ChangeSet.apply_changeset(journal_in, data_in, target, @test_run)
                        end

                        if not File.exist? Repository::UNDO_JOURNAL and not @test_run
                            # the undo changeset has to go in the reverse direction
                            UI.start_finish("Creating 'undo' revision") do
                                changes = ChangeSet.make_changeset("undo", target, originals)
                            end
                        end
                        
                        # and apply it to the originals directory to sync them up
                        UI.start_finish("Applying to the repository originals directory") do
                            journal_in = Zlib::GzipReader.new(File.open(journal_file))
                            data_in = Zlib::GzipReader.new(File.open(data_file))
                            ChangeSet.apply_changeset(journal_in, data_in, @repo.originals_dir, @test_run)
                        end
                        
                        # update our current path (but not if we're testing)
                        @repo['Path'] = @repo['Path'] << @id unless @test_run
                    end
                else
                    UI.failure :constraint, "Sorry, can't apply a revision unless it is a child of the current one."
                end
            else
                UI.failure :search, "Could not find a matching revision"
            end
        end
    end
end 
