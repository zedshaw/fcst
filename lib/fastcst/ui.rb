require 'logger'



# The beginnings of a User Interface abstraction that will handle events
# from the other components and display something useful to the end user.
module UI
    
    # Takes the given event and a message and then shows it to the user
    # in a meaningful way.
    def UI.event(type, message)
        puts "#{type}: #{message}"
    end
    
    
    def UI.failure(type, message)
        $stderr.print "FAILURE: (#{type}) #{message}\n"
    end
    
    def UI.start_finish(message)
        UI.event :starting, message
        value = yield
        UI.event :finished, message
        return value
    end
    
    def UI.ask(header)
        print "#{header}: "
        STDOUT.flush
        line = gets
        return line[0,line.length - 1]
    end
    
    
    
    require 'fastcst/command'
    require 'shellwords'


    module Shell
   
        def Shell.prompt(string = "")
            print "#{string}> "
            $stdout.flush
        end
    
        def Shell.start
            # trap the INT signl so we can gracefully exit
        
            puts "Welcome to the FastCST shell.  Enter quit to exit or type CTRL-D."
            prompt
        
            $stdin.each do |line|
                begin
                    args = Shellwords.shellwords(line)
                
                    if args.length > 0
                        if args[0] == "quit"
                            exit 0
                        else
                            CommandRegistry.instance.run args
                        end
                    end
                rescue
                    puts "ERROR: #$!"
                end
            
                prompt
            end
        end
    
    end
end

