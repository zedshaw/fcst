require 'fastcst/trigger'
require 'fastcst/repo'
require 'fileutils'

include FileUtils

# This will make a dated backup of your current source before you do any apply
# command, and put it in the .fastcst/backups/ directory.
class ApplyTrigger < Trigger
    
    def before_run(command, args)
        date = Time.now
        puts "Creating a backup dated #{date}"
        
        repo_dir = Repository::Repository.search
        if not repo_dir
            puts "Could not find the repository directory"
            return
        end
        repo = Repository::Repository.new repo_dir
        
        backup_dir = File.join(repo.path, "backups", date.to_s)
        mkdir_p backup_dir
        `cp -r * '#{backup_dir}'`
    end
end

