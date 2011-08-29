#!/usr/bin/env ruby
require 'fastcst/command'
require 'fastcst/ui'

Dir.chdir oldlocation

if ARGV.length == 0
    begin
        UI::Shell.start
    rescue
        UI.failure :exception, $!
	raise
    end
else
    # run the one command
    CommandRegistry.instance.run ARGV
end

