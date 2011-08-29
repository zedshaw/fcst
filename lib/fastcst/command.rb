require 'singleton'
require 'optparse'
require 'pluginfactory'
require 'fastcst/changeset'
require 'fastcst/repo'
require 'fastcst/trigger'



# A Command pattern implementation used to create the set of command available to the user
# from the fcst script.  The script uses objects which implement this interface to do the
# user's bidding.
#
# To implement a Command you must do the following:
#
#   1.  Subclass Command and create an initialize message that does not take an opt argument.
#   2.  After you create your own OptionParser, you pass it to the super() method.
#   3.  Implement a validate function that returns a true/false depending on whether the command
#       is configured properly.
#   4.  By default the help function just wraps OptionParser#help.
#   5.  Implement your run function to do the actual work based on the arguments parsed in argv.
#
class Command
    include PluginFactory
    
    attr_reader :valid, :done_validating
    
    # Called by the subclass to setup the command and parse the argv arguments.
    # The call is destructive on argv since it uses the OptionParser#parse! function.
    def initialize(argv, options)
        @opt = OptionParser.new
        @valid = true
        # this is retarded, but it has to be done this way because -h and -v exit
        @done_validating = false

        # process the given options array
        options.each do |short, long, help, variable|
            @opt.on(short, long, help) do |arg|
                self.instance_variable_set(variable, arg)
            end
        end
        
        # I need to add my own -h definition to prevent the -h by default from exiting.
        @opt.on_tail("-h", "--help", "Show this message") do
            @done_validating = true
            puts @opt
        end

        # I need to add my own -v definition to prevent the -h from exiting by default as well.
        @opt.on_tail("--version", "Show version") do
            @done_validating = true
            puts "No version yet."
        end

        @opt.parse! argv
        
    end

    # Tells the PluginFactory where to look for additional commands.  We solve
    # the problem of locating new commands by using the Repository object to get
    # the .fastcst directory, and then use the plugins directory there.
    def self.derivativeDirs
        repo_dir = Repository::Repository.search
        if repo_dir
            plugin_dir = File.join(File.expand_path(repo_dir), "plugins")
            # we should now have a working plugin dir, return it
            return [plugin_dir]
        end
        
        return []
    end
    
    # Returns true/false depending on whether the command is configured properly.
    def validate
        return @valid
    end
    
    # Returns a help message.  Defaults to OptionParser#help which should be good.
    def help
        @opt.help
    end
    
    # Runs the command doing it's job.  You should implement this otherwise it will
    # throw a NotImplementedError as a reminder.
    def run
        raise NotImplementedError
    end
    
    
    def valid?(variable, message)
        if not @done_validating and (not variable)
            UI.failure :input, message
            @valid = false
            @done_validating = true
        end
    end
    
    def valid_exists?(file, message)
        if not @done_validating and (not file or not File.exist? file)
            UI.failure :input, message
            @valid = false
            @done_validating = true
        end
    end
    
    
    def valid_file?(file, message)
        if not @done_validating and (not file or not File.file? file)
            UI.failure :input, message
            @valid = false
            @done_validating = true
        end
    end

    def valid_dir?(file, message)
        if not @done_validating and (not file or not File.directory? file)
            UI.failure :input, message
            @valid = false
            @done_validating = true
        end
    end
end



# A Singleton class that manages all of the available commands
# and handles running them.
class CommandRegistry
    include Singleton

    # Builds a list of possible commands from the Command derivates list
    def commands
        list = Command.derivatives()
        match = Regexp.new("(.*::.*)|(.*command.*)", Regexp::IGNORECASE)
        
        results = []
        list.keys.each do |key|
            results << key unless match.match(key.to_s)
        end
        
        return results.sort
    end
    
    def print_command_list
        # oops, that's not valid, show them the message
        puts "Available commands are:\n"
            
        self.commands.each do |name|
            puts " - #{name}\n"
        end
            
        puts "Each command takes -h as an option to get help."
        
    end
    
    
    # Runs the args against the first argument as the command name.
    # If it has any errors it returns a false, otherwise it return true.
    def run(args)
        # find the command and change the program's name to reflect it
        cmd_name = args.shift
        $0 = "#{cmd_name}"
        
        if cmd_name == "?" or cmd_name == "help"
            print_command_list
            return true
        end
        
        # command exists, set it up and validate it
        begin
            command = Command.create(cmd_name, args)
        rescue FactoryError
            UI.failure :command, "INVALID COMMAND."
            print_command_list
            return
        end
        
        # Normally the command is NOT valid right after being created
        # but sometimes (like with -h or -v) there's no further processing
        # needed so the command is already valid so we can skip it.
        if not command.done_validating
            if not command.validate
                UI.failure :command, "#{cmd_name} reported an error. Use -h to get help."
                return false
            else
                # try to load a trigger for this command
                begin
                    trigger = Trigger.create(cmd_name)
                    trigger.before_run(command, args)
                    command.run
                    trigger.after_run(command, args)
                rescue FactoryError
                    # no trigger found, so just run the command like usual
                    command.run
                end
            end
        end
        return true
    end
    
    # Runs the command like normal, but redirects $stdout and $stderr to the
    # requested log file (which should be a file like object opened by you).
    # It also marks the start and end times in the log file.
    def run_redirect(log, args)
        res = false
        
        begin
            oldstdout = $stdout
            oldstderr = $stderr
            
            log.write ">>>>>> #{Time.now}\n"
            $stdout = log
            $stderr = log

            res = run(args)
            
            log.write "<<<<<< #{Time.now}\n"
            
        ensure
            $stdout = oldstdout
            $stderr = oldstderr
            return res
        end
    end
end



require 'fastcst/command/abort'
require 'fastcst/command/attach'
require 'fastcst/command/begin'
require 'fastcst/command/apply'
require 'fastcst/command/undo'
require 'fastcst/command/disp'
require 'fastcst/command/finish'
require 'fastcst/command/init'
require 'fastcst/command/list'
require 'fastcst/command/log'
require 'fastcst/command/mail'
require 'fastcst/command/show'
require 'fastcst/command/status'
require 'fastcst/command/sync'
require 'fastcst/command/env'
require 'fastcst/command/serve'
require 'fastcst/command/merge'
require 'fastcst/command/index'
require 'fastcst/command/find'

