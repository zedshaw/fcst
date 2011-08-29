require 'fastcst/ui'
require 'fastcst/repo'
require 'fastcst/metadata'


module Repository

     class AbortCommand < Command
        def initialize(argv)
            super(argv, [
            ["-f", "--full", "Do a full abort which will also revert your source", :@full],
            ])
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find a repository directory."
            return @valid
        end
    
    
        def run
            repo = Repository.new @repo_dir
            md_file = File.join(repo.work_dir, MetaData::META_DATA_FILE)
            
            abort = UI.ask("Are you sure you want to abort this revision? [Y/n]")
            if abort.downcase == "y"
                if File.exist?(md_file)
                    File.unlink(md_file)
                end
                
                repo.delete 'Current Revision'
                
                if @full 
                    UI.event :warn, "Alright, doing a full abort as well.  Goodbye work."
                    changes = nil
                    
                    Dir.chdir repo.work_dir do
                        # relative to work directory
                        source = File.join("..","..")
                        originals = File.join("..", "originals")
                        
                        # remember we are trying to do an inverted changeset, so source comes first
                        changes = ChangeSet.make_changeset("undo", source, originals)
                    end
                    
                    # back in the source directory    
                    if changes.has_changes?
                        # now apply this special undo to the current directory
                        UI.start_finish("Aborting changes to source directory") do
                            journal_file = File.join(repo.work_dir, Repository::UNDO_JOURNAL)
                            data_file = File.join(repo.work_dir, Repository::UNDO_DATA)
                            journal = Zlib::GzipReader.new(File.open(journal_file))
                            data = Zlib::GzipReader.new(File.open(data_file))
                            
                            ChangeSet.apply_changeset(journal, data, ".")
                            
                            File.unlink(journal_file)
                            File.unlink(data_file)
                        end
                    end
                end
            end
        end
    
    end


end

