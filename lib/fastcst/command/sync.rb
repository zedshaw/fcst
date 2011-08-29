require 'fastcst/ui'
require 'fastcst/distrib'
require 'fastcst/changeset'
require 'fastcst/repo'
require 'yaml'
require 'set'


module Distribution

    
    class GetCommand < Command
        def initialize(argv)
            @directory = "."
            
            super(argv, [
            ["-u", "--url URL", "URL of the base location when downloading", :@url],
            ])
            
            @repo_dir = Repository::Repository.search
        end
        
        
        def validate
            valid? @repo_dir, "Could not find repository directory"
            
            if @repo_dir
                @repo = Repository::Repository.new @repo_dir
                @url = @repo.env_default_value('Get URL', @url)
            end
            
            valid? @url, "A URL containing to the repository is required (you can set 'Get URL' in env)"
            
            return @valid
        end

        
        def display_meta_data(md)
            puts "Purpose: #{md['Purpose']}"
            puts "Revision: #{md['Revision']}"
            puts "Developer: #{md['Created By']['Name']} - #{md['Created By']['E-Mail']}"
            puts "Changeset ID: #{md['ID']}"
        end
        
        def run

            remote_index = nil
            open(@url + "/index.yaml") { |f| remote_index = YAML.load(f) }

            remote_cs = Set.new(remote_index['Changesets'])
            local_cs = Set.new(@repo.list_changesets)
            
            # use the two sets to find out what is new in the remote repository
            new_cs = remote_cs - local_cs

            if new_cs.empty?
                UI.event :exit, "No new changesets at #@url.  Done."
                return
            end
            
            # download all of the changesets to the work directory first
            work_dir = File.join(@repo.work_dir, "get_results")
            Dir.mkdir(work_dir) if not File.exist?(work_dir)
            
            root_url = @url + "/root"
            Dir.chdir work_dir do 
                new_cs.each do |id|
                    md_url = root_url + "/#{id}"
                    md = Distribution.download_meta_data(md_url, MetaData::META_DATA_FILE)
                    display_meta_data(md)
                    
                    # get the meta-data contents
                    data_file, journal_file = Distribution.download_md_contents(md_url, md)
                    
                    # then store it in the repository with a move
                    @repo.store_changeset ".", MetaData::META_DATA_FILE, move=true
                end
            end
        end
    end
    
    

    class PublishCommand < Command
       def initialize(argv)
           super(argv, [
           ["-u", "--user NAME", "Username to use during upload", :@user],
           ["-s", "--site ADDR", "FTP host/site to upload contents", :@site],
           ["-p", "--pass WORD", "Password to use during upload", :@password],
           ["-d", "--dir PATH", "Directory to cd into before upload (defaults to '.')", :@directory],
           ["-P", "--[no]-passive", "Turn on PASSIVE FTP mode (default to off)", @passive]
           ])
           
           @repo_dir = Repository::Repository.search
       end

       
       def validate
           valid? @repo_dir, "Could not find repository directory"
           
           if @repo_dir
               @repo = Repository::Repository.new @repo_dir
               @site = @repo.env_default_value('Publish Site', @site)
               @user = @repo.env_default_value('Publish User', @user)
               @directory = @repo.env_default_value('Publish Directory', @directory)
           end
           
           valid? @site, "You must supply an FTP site/host to access (set 'Publish Site' in env)"
           valid? @user, "Need a user to login as (if you're retarded you can set 'Publish User' in env)"
           valid? @password, "Users need passwords, if your's is blank then specify -p '' (sorry, you can't be that retarded)"
           valid? @directory, "You need to give a directory (set 'Publish Directory' in env)"
           
           return @valid
       end
       
       def connect(site, user, password, directory)
           ftp = nil
           UI.start_finish "Connecting to #{site}" do
               ftp = Net::FTP.open(site, user, password)
               UI.event :info, "Changing to directory #{directory}"
               ftp.passive = @passive
               ftp.chdir directory
           end
           
           return ftp
       end
       
       def run
           
           begin
               ftp = connect(@site, @user, @password, @directory)

               remote_index = nil
               begin
                   # attempt to get the yaml file, if it fails then we need to upload the works
                   Dir.chdir @repo.work_dir do
                       ftp.getbinaryfile("index.yaml")
                       remote_index = YAML.load_file("index.yaml")
                       File.unlink("index.yaml")
                   end
               rescue
                   UI.event :warn, "No index.yaml file found at #@directory on #@site (#$!)" 
                   remote_index = {'Changesets' => [], 'Revision Path' => nil}
                   
                   # we also need to make the root directory now
                   UI.event :warn, "Creating 'root' directory to hold changesets"
                   ftp.mkdir "root"
               end
               
               remote_cs = Set.new(remote_index['Changesets'])
               local_cs = Set.new(@repo.list_changesets)
               
               # find out what is new in our repository vs the remote
               new_cs = local_cs - remote_cs
               
               if new_cs.length == 0
                   UI.event :exit, "Remote repository is the same. Done."
                   return
               end
               
               ftp.chdir "root"
               new_cs.each do |id|
                   cs_path, md = @repo.find_changeset(id)
                   UI.event :upload, "#{md['Revision']} -- #{md['ID']}"
                   Dir.chdir cs_path do
                       ftp.mkdir id
                       ftp.chdir id
                       Distribution.upload(ftp, MetaData::META_DATA_FILE)
                       ftp.chdir ".."
                   end
               end
               
               ftp.chdir ".."
               
               # build the index.yaml and upload it
               UI.start_finish("Uploading index.yaml") do
                   Dir.chdir @repo.work_dir do
                       local_index = {'Changesets' => local_cs.sort, 'Revision Path' => @repo['Path']}
                       File.open("index.yaml", "w") { |f| YAML.dump(local_index, f) }
                       ftp.putbinaryfile("index.yaml", "index.yaml")
                       File.unlink "index.yaml"
                   end
               end
               
           ensure
               ftp.close if ftp
           end
       end
    end
end


