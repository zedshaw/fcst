require 'fastcst/trigger'

# Simple trigger that will print a before/after message when you run the list command.
# See the apply.rb file for something more useful.
class ListTrigger < Trigger
    
    def before_run(command, args)
        puts "before:  #{command.class.name} :  #{args.join(',')}"
    end
    
    def after_run(command, args)
        puts "after: #{command.class.name} : #{args.join(',')}"
    end
end