require 'pluginfactory'
require 'fastcst/repo'


# Implements the base class for the Trigger framework.  A Trigger is an
# object which is loaded and then various methods are called at different
# stages of a command run.  Configuring a Trigger involves putting the class
# into the .fastcst/plugins directory named after the command it should work with.
# The class in the .rb file should also have the same name as the command but
# ending in Trigger rather than Command.  If you have a command and a trigger
# plugin then putting them both in the same file will work for you.
#
# At the moment it only supports two simple events:  before_run and after_run
# which are called at the appropriate times.  The default action for the
# events is to just do nothing.
#
# The tools/triggers directory has two examples of triggers.  One just prints
# a message when you run the list command.  The other one (apply.rb) will make
# a dated backup of your current source (Unix only since it uses the cp command)
# in the .fastcst/backups directory.  I actually use this one quite often.
#
class Trigger
    include PluginFactory
    
    
    # Tells the PluginFactory where to look for additional triggers.
    def self.derivativeDirs
        repo_dir = Repository::Repository.search
        if repo_dir
            plugin_dir = File.join(File.expand_path(repo_dir), "plugins")
            # we should now have a working plugin dir, return it
            return [plugin_dir]
        end
        
        return []
    end
    
    # The before event is called after the arguments have been processed and the 
    # command is loaded, but before the command's run method is called.
    def before_run(command, args)
        
    end
    
    
    # The after event is called after a command runs
    def after_run(command, args)
        
    end
    
end