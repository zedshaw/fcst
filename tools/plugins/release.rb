require 'fastcst/command'
require 'fastcst/repo'
require 'fileutils'
require 'find'

include FileUtils
include Find

class ReleaseCommand < Command
    
    def initialize(argv)
        @build_dir = "/tmp"
        
        super(argv, [
        ["-p", "--project NAME", "The name to use for the project rather than env settings", :@project],
        ["-v", "--version NAME", "A name for the version (defaults to current revision)", :@version],
        ["-b", "--build-dir PATH", "Where to build the release (defaults to /tmp)", :@build_dir]
        ])
        
        @repo_dir = Repository::Repository.search
    end
    
    def validate
        valid? @repo_dir, "Could not find a repository directory"
        
        if @repo_dir
            @repo = Repository::Repository.new @repo_dir
            @project = @repo.env_default_value('Project', @project)
            if not @version
                # they didn't give a version so use top revision
                @id = @repo['Path'].pop
                cd_path, @md = @repo.find_changeset(@id)
                @version = @md['Revision']
            end
        end
        
        return @valid
    end
    
    def run
        release_name = "#{@project}-#{@version}"
        release_dir = File.join(@build_dir, release_name)
        tar_name = "#{release_name}.tar.bz2"
        begin
            puts "Running clean-up stuff"
            `rake clean`
            `ruby setup.rb clean`
            
            puts "Building a release in #{release_dir}"
            cp_r "./", release_dir
            
            puts "Removing the .fastcst directory from the release"
            rm_rf File.join(release_dir, ".fastcst")

            puts "Creating tar file #{tar_name}"
            Dir.chdir @build_dir do
                `tar -cjf #{tar_name} #{release_name}`
            end
        rescue
            puts "FAILURE, there's junk left in #{release_dir}"
            raise
        end
        
        rm_rf release_dir
        puts "Release package is in #{File.join(@build_dir, tar_name)}"
    end
end


