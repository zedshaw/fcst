require 'test/unit'
require 'fastcst/command'


module UnitTest
    class QuickAllCommandsTest < Test::Unit::TestCase
    
        def run_all args
            cr = CommandRegistry.instance
            commands = cr.commands
            log = StringIO.new
        
            commands.each do |cmd|
                res = cr.run_redirect(log, [cmd] + args)
            end
            
            return log
        end
        
        def test_run_all_fails
            run_all ['--failfailfail']
        end
        
        def test_run_all_help
            run_all ['-h']
        end
        
        def test_run_all_version
            run_all ['-v']
        end
    end
end