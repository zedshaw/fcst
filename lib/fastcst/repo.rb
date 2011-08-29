require 'fileutils'
require 'yaml'
require 'fastcst/metadata'


module Repository

    # = Introduction
    #
    # Represents a single directory based repository stored on disk and supports all
    # the operations you can do to a repository.  The Repository's job is to manage 
    # the information and changesets that someone deals with during their daily
    # work with FastCST.  It doesn't do any changeset creation or application.  It 
    # doesn't handle distribution or replication either.  It simply provides an
    # API to control the raw repository.
    #
    # = Repository Layout
    #
    # The repository layout is really very simple:
    #
    # 1.  The top directory is called .fastcst and sits at the top of the files being managed.
    # 2.  Under this directory is:
    #     a.  env.yaml -- holds the current state of the fcst program and any configuration info
    #     c.  originals -- a snapshot of the source tree as it was at the last commit
    #     d.  work -- This is where fastcst does most of its work while doing stuff.  Should be empty.
    #     e.  pending -- any changesets which were received via e-mail and haven't been dealt with
    #     f.  root -- holds all the changesets and their contents
    # 3. Changesets are already uniquely identified by their ID which is a UUID/GUID number.
    # 4. The root directory contains all the changesets in a flat format that's easy to
    #    process, but might be hard to read by humans.
    # 5. Each revision .md file contains information on what revision is its one parent, and what 
    #    other revisions may have been used during a merge to create this one.
    # 6. The pending file is maintained by the send/recv/read commands and is only located
    #    using the Repository object.
    #
    # == Top Level Files
    #
    # The env.yaml contains all the configuration information that fcst and the Repository
    # object needs to operate.  This file maintains information like the developer's name,
    # e-mail, common settings for commands, command history for the shell,  etc.
    # It is updated infrequently in response to user activities and is only kept
    # "live" when it needs to be modified.
    #
    # I originally designed the repository to have an "index file" that was used to
    # keep track of the repository structure in a .yaml file so it could be loaded
    # quickly.  After testing I found that this was entirely unecessary since very large
    # revision trees could be loaded by direct analysis very quickly (less than a second
    # to load a 1000 node repository).  As long as the revision tree is cached and updated
    # when store/delete operations are called then everything works really quick.  This
    # simplifies the design quite a bit and doesn't require any weird maintenance of an
    # external file.
    #
    # The downside to this design is that it makes it nearly impossible for a person to
    # go in and see what's in the repository.  I'll hopefully have a set of commands or 
    # a tool to help with this once I figure out what people need from it.
    #
    # == Root Directory Structure
    #
    # The root directory contains all of the changesets that the user may have downloaded
    # from others.  The root directory is very simple in that it just contains a flat 
    # set of directories named after the lowercase string of each changeset's UUID.
    # Inside each of these directories is the changesets .md meta-data file and any 
    # of it's contents.
    # 
    # Since the revision tree for any repository is determined by the meta-data files
    # and how they reference other changeset UUIDs, it's not necessary to store them
    # in a tree.  It's easier to store them in a way that is convenient for scanning and
    # finding the changesets.
    #
    # Another advantage of this configuration is that it makes synchronizing from multiple
    # external repositories a piece of cake since none of the directories will clash.  It
    # also makes it possible to upload new changesets to a repository without interfering
    # with people currently downloading.
    # 
    # = The "Human Name" For A Changeset
    #
    # Humans don't really work with big numbers very well, so there must be a way of presenting
    # a changeset that is easy for them to digest.  An initial first whack at it might be:
    #
    # [revision_name]-[uuid_chunk](-email)
    #
    # The revision name is just whatever the creator set the revision name as in the meta-data.
    # Since this can easily conflict with other revision names, a small piece of the uuid is
    # added (say 3 from the beginning).  This seems to create unique enough
    # names that don't overload the reader.  In the occaisional rare chance that the revision
    # and uuid_chunk are the same then we just tack on the e-mail address of the creator to
    # make the name fully unique.  It should be really rare that two revisions have the same
    # name, uuid_chunk, at the same place in the revision tree.
    # 
    # = Building A Repository From Scratch
    #
    # I think a good way to understand the repository layout is to describe how someone would
    # build a repository from scratch with just a set of changesets.  This would be necessary
    # in situations where someone damaged their changeset or would like to start from scratch.
    # One of the design goals is that you can create a fully working repository with just
    # a set of changesets.
    #
    # The process would most likely be something like this:
    #
    # 1.  Find a published repository that has the changesets you want or ask a developer
    #     to e-mail his changeset path to you.
    # 2.  Start a new repository for yourself that is "pristine" by running fcst init in an empty
    #     directory.
    # 3.  Run fcst bootstrap which does:
    #     a.  Gets the changesets from either your friend's repository or your pending file.
    #     b.  Stores each changeset in the root directory in it's UUID directory.
    #     d.  Gets rid of the (now orphaned) empty changeset that your init command created.
    #     e.  Successively applies the changesets needed to follow the path up to the latest one.
    #
    # There's a lot of hand-waving in that description, but in theory it should work.
    #
    class Repository
    
        attr_reader :path, :env_yaml, :work_dir, :root_dir, :originals_dir, :pending_mbox, :plugin_dir

        DEFAULT_FASTCST_DIR=".fastcst"
        
        # Opens the repository that is at the given path which should be the
        # full path to the top of the repository (where the env.yaml file is
        # located).
        def initialize(path)
            @path = path
            @env_yaml = File.join(path, "env.yaml")
            @root_dir = File.join(path, "root")
            @originals_dir = File.join(path, "originals")
            @pending_mbox = File.join(path, "pending")
            @work_dir = File.join(path, "work")
            @plugin_dir = File.join(path, "plugins")
            @cached_rev_tree = nil
        end
    
    
        # Searches for repo_dir (like .fastcst) by going up the tree and returns
        # the first full path that it finds.  The given directory must be 
        # properly formed and contain a env.yaml file plus all directories.
        def self.search(repo_dir=DEFAULT_FASTCST_DIR, from = ".")
            orig_path = Dir.getwd
            path = nil
            if File.exist? from
                # go to the from location temporarily
                Dir.chdir from
                
                begin
                    last = nil
                    cur = Dir.getwd
                
                    while last != cur
                        if File.exist? repo_dir and File.directory? repo_dir
                            path = File.join(Dir.getwd, repo_dir)
                            break
                        end
                    
                        # record the last one
                        last = cur
                    
                        # move up the tree
                        Dir.chdir ".."
                        cur = Dir.getwd
                    end
                ensure
                    # go back to where we started
                    Dir.chdir orig_path
                end
            end
        
            return path
        end
    
        # Creates a new repository at the given path so you can use Repository.instance
        # to get the repository information.
        #
        # The path given is created if it does not exist.
        # The newly created repository is completely baren and useless.
        # To get it into a reasonable state you'd then need to add some changesets
        # and fill in the originals dir.
        def self.create(path, env = {})
            if not File.exist? path
                Dir.mkdir path
            end
        
            repo = Repository.new path
            # create a base env.yaml and index.yaml
            File.open(repo.env_yaml, "w") { |out| YAML.dump(env, out) }

            # create the originals and root directory
            Dir.mkdir repo.root_dir
            Dir.mkdir repo.originals_dir
            Dir.mkdir repo.work_dir
            Dir.mkdir repo.plugin_dir
        
            # setup the pending file (just an empty file)
            pending = File.open(repo.pending_mbox, "w")
            pending.close
        
            return repo
        end
    
    
        # Reads the given value out of the env.yaml file.
        def [](key)
            env = YAML.load_file(@env_yaml)
            return env[key]
        end
    
        # Loads the env.yaml file, updates the given key=value,
        # and then writes the env.yaml file back.
        def []=(key, value)
            env = YAML.load_file(@env_yaml)
            env[key] = value
            File.open(@env_yaml, "w") do |out| 
                YAML.dump(env, out)
            end
            value
        end

        # Deletes the key from the environment
        def delete(key)
            env = YAML.load_file(@env_yaml)
            env.delete key
            File.open(@env_yaml, "w") { |out| YAML.dump(env, out) }
        end
    
        # Used to get default values which can be overridden by command line settings,
        # and display a message if there's a failure.
        def env_default_value(key, value)
            if not value
                value = self[key]
                if not value
                    UI.failure :input, "No setting for #{key} in the environment and no setting given"
                    return nil
                end
            end

            return value
        end
        
        
        # Given a path containing files and the meta-data file in that location
        # it will move the files into the root directory in the proper organization
        # and then reset the @cached_rev_tree so it gets recreated.
        # It returns the meta-data so you can analyze it.
        def store_changeset(path, md_file, move=false)
            md_path = File.join(path, md_file)
            md = MetaData.load_metadata(md_path)
        
            # create a directory in root with the UUID as the name
            md_dir = File.join(@root_dir, md['ID'])
            Dir.mkdir md_dir
        
            # copy the md_file there and all the of the meta-data contents
            if move
                FileUtils.move md_path, md_dir
            else
                FileUtils.cp md_path, md_dir
            end
        
            md['Contents'].each do |info|
                name, digest, purpose = info['Name'], info['Digest'], info['Purpose']
                to_copy = File.join(path, name)
                if move
                    FileUtils.mv to_copy, md_dir
                else
                    FileUtils.cp to_copy, md_dir
                end
            end

            # force rebuilding of the revision tree
            @cached_rev_tree = nil
        
            return md
        end
    
    
    
        # Returns the full path to the meta-data file for this UUID.
        def find_meta_data(uuid)
            md_file = File.join(@root_dir, uuid, MetaData::META_DATA_FILE)
        
            # grep for the first file with .md, should only be one
            if File.exists? md_file
                return md_file
            else
                return nil
            end
        end
    
    
        # Builds a list of all changesets which match the given name.
        # It returns an Array of [revision_name, uuid] for each one found.
        def find_revision(rev)
            list = list_changesets
            possibles = []
                
            # build a list of possible revisions to use
            list.each do |id|
                path, md = find_changeset(id)
                if path
                    if md['Revision'] == rev
                        # found a match, add it to the list
                        possibles << [rev, id]
                    end
                else
                    UI.failure :error, "BAD! An ID listed could not be found: #{id}"
                end
            end
                        
            return possibles
        end
    

        # Dynamically resolves an id when given either a revision name
        # or an id (or both).  It will adapt depending on whether rev, id,
        # or both is given.  The logic is that it tries to find a unique
        # id for a revision named after rev, if rev isn't given then it checks
        # that the given id is valid.  Finally, if neither is given then it returns
        # the top of the current path.  If it returns nil then it couldn't find
        # anything useful.
        def resolve_id(rev, id)
            # determine the md_file based on the possible id
            if rev
                possibles = find_revision(rev)
                # done looking for matches, see if there's more than one and warn
                if possibles.length > 1
                    UI.failure :constraint, "More than one revision matches that name:"
                    possibles.each do |rev, id|
                        puts "#{rev} -- #{id}"
                    end
                elsif possibles.length == 1
                    # found the revision
                    id = possibles[0][1]
                else
                    UI.failure :search, "Could not find the requested revision #{rev}"
                end
            elsif id
                if not find_meta_data(id)
                    UI.failure :input, "Given id #{id} is not in the repository"
                    id = nil  # unset id since it's bogus
                end
            else
                id = self['Path'].pop
            end
            
            return id
        end
        
        # Given a uuid it will return an array of [full_path, meta_data]
        # so that you can load the meta-data and any contained files.
        # The full_path is the directory where the meta_data structure's
        # contents reside.
        def find_changeset(uuid)
            # try to load the meta-data file out of the directory
            full_path = File.join(@root_dir, uuid)
        
            md_file = find_meta_data(uuid)
        
            if md_file
                md = MetaData.load_metadata(md_file)
                # return the path to the user
                return full_path, md
            else
                return nil, nil
            end
        end

    
        # A convenience method that gives the UUID of a changeset's parent.
        def find_parent_of(child_uuid)
            path, md = find_changeset child_uuid
            return md['Parent ID']
        end
    
    
        # A convenience method that returns the UUIDs of all children to this
        # changeset UUID.  It's currently horribly inneficient since it rebuilds
        # the revision tree by scanning the changeset directory each time it's called.
        # This is mostly done right now since it is simple and works without needing to
        # update anything fancy.
        #
        # This actually turns out to be reasonably fast.  Initial tests said it can process
        # 1000 randomly structured changesets in less than a second.
        def find_all_children(parent_uuid)
            root = revision_tree
            return root[parent_uuid] || []
        end

    
        # Generates a human readable name from the given uuid, trying to add information
        # as necessary to make it unique among its siblings.
        #
        # The algorithm for this is simply:
        #
        #   1.  Create: [revision_name]
        #   2.  Get this uuid's siblings (parent's immediate children)
        #   3.  If the list of children contains a revision with the same [revision_name]
        #       then add the [uuid_chunk].  If it's still the same then add the e-mail address.
        #
        # This makes sure that the name is unique enough for a person to read, but still avoids
        # clashes between the children.  It doesn't handle different revisions with the same
        # name in other parts of the tree, but the rationale is that this won't be necessary since
        # most operations are done relative to the current revision path.
        def build_readable_name(uuid)
            path, md = find_changeset(uuid)
        
            # abort if not found with a nil
            return nil if not path

            rev_name = md['Revision']
        
            parent = find_parent_of(uuid)
            if parent and parent != "NONE"
                # it has a parent so check the siblings
                find_all_children(parent).each do |sibling|
                    sib_path, sib_md = find_changeset(sibling)
                    
                    # don't process ourself and check for at least one conflict
                    if sib_path != path and rev_name == sib_md['Revision']
                        # same revision name, add uuid chunk
                        uuid_chunk = md['ID'][0,3]
                        rev_name += "-#{uuid_chunk}"
                        
                        # and if the uuid chunk STILL matched then
                        if uuid_chunk == sib_md['ID'][0,3]
                            # yep, a sibling has the same name and stuff
                            rev_name += "-#{md['Created By']['E-Mail']}"
                            break  # don't need more, that's enough
                        end
                    end
                end
            end

            return rev_name
        end
    
    
        # Returns the "revision tree hash" which is a simple two-level representation
        # of all the UUIDs that have children and their children as an array.
        # It caches the results of building the revision tree in @cached_rev_tree
        # and will just return that unless force==true.  Other functions will set
        # the @cached_rev_tree = nil in order to have this function rebuild the
        # tree the next time it is requested.
        def revision_tree(force = false)
            # check to see if we need to rebuild the cached revision tree
            if not @cached_rev_tree or force
                changesets = list_changesets
                @cached_rev_tree = {}
        
                # we only need a simple two level hash based tree structure
                changesets.each do |uuid|
                    pid = find_parent_of(uuid)
                    # every node needs to be entered and given at least an empty list
                    @cached_rev_tree[uuid] ||= []
                    if pid and pid != "NONE"
                        @cached_rev_tree[pid] ||= []
                        @cached_rev_tree[pid] << uuid
                    end
                end
            end

            return @cached_rev_tree
        end
    
    
        # Removes a changeset from the repository based on the uuid.
        # Returns the same information as find_changeset so the caller
        # can analyze the results.
        def delete_changeset(uuid)
            full_path, md = find_changeset(uuid)
    
            # recursively remove the directory
            FileUtils.rm_rf(full_path)
        
            # force rebuilding of the cached_rev_tree the next time it's requested
            @cached_rev_tree = nil
        
            return full_path, md
        end
    
    
    
        # Returns a list of all the changesets in the root directory
        # by loading the root directory contents and grepping for /^[a-zA-Z0-9]/
        # which works since all UUIDs match this format.
        def list_changesets
            dir = Dir.open(@root_dir)
            changesets =  dir.grep(/^[a-zA-Z0-9]/)
            dir.close
            return changesets
        end

    end
end