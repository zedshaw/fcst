require 'fastcst/repo'
require 'fastcst/changeset'
require 'fastcst/metadata'
require 'fastcst/ui'

# Implements the init command which creates a .fastcst repository for the source
# tree in a given directory.

module Repository

    class InitCommand < Command
    
        def initialize(argv)
            super(argv, [
            ["-e", "--dev-email ADDR", "Your e-mail address", :@email],
            ["-n", "--dev-name NAME", "Your real name", :@name],
            ["-p", "--project NAME", "The name of your project", :@project]
            ])
        end

    
    
        def validate
            valid? @email, "You must provide an e-mail address for yourself"
            valid? @name, "You must provide a name for yourself"
            valid? @project, "Your project name is missing"
            valid?((not File.exists? Repository::DEFAULT_FASTCST_DIR), "A file name #{Repository::DEFAULT_FASTCST_DIR} already exists here.")
        
            return @valid
        end
    
    
    
        def run
            # create the repository
            env = {"Created By" => {"E-Mail" => @email, "Name" => @name},  
            "Project" => @project, "Path" => []}
            Dir.mkdir Repository::DEFAULT_FASTCST_DIR
            repo = Repository.create(Repository::DEFAULT_FASTCST_DIR, env)
        
            # now we make the very first revision which is the root of it all
            cs_name = "root"
            md_file = MetaData::META_DATA_FILE

            Dir.chdir repo.work_dir do
                originals = File.join("..","originals")
                sources = File.join("..","..")
                data_file = cs_name + ChangeSet::DATA_FILE_SUFFIX
                journal_file = cs_name + ChangeSet::JOURNAL_FILE_SUFFIX
                purpose = "Initial repository creation"

                # first we try to make the changeset so we can see if this is pristine or not
                UI.start_finish("Creating initial 'root' revision") do
                    changes = ChangeSet.make_changeset(cs_name, originals, sources)

                    # no changes against the empty originals directory means that this is an empty start
                    if not changes.has_changes?
                        UI.event :info, "Looks like you have a pristine directory.  You'll need to bootsrap this one."
                        return
                    end
                end
                
                
                UI.start_finish("Writing 'root' meta-data") do
                    MetaData.create_metadata(md_file, @project, "root", purpose, @name, @email)
                end
                
                UI.start_finish("Applying revision to originals backup") do
                    journal_in = Zlib::GzipReader.new(File.open(journal_file))
                    data_in = Zlib::GzipReader.new(File.open(data_file))
                    
                    # apply closes the files for us
                    ChangeSet.apply_changeset(journal_in, data_in, originals)
                    
                    MetaData.finish_metadata(md_file, "NONE", data_file, journal_file)
                end
            end
        
            # store the newly created changeset, doing a move instead of a copy
            md = repo.store_changeset repo.work_dir, md_file, move=true
            # and update the environment to reflect our new revision path
            repo["Path"] = repo["Path"] << md['ID']
            
            UI.event :finished, "Root revision: #{repo.build_readable_name md['ID']}"
        end
    end

    
end
