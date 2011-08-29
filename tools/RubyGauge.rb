#--------------------------------------------------------------------#
#                                                                    #
#  RubyGauge version 1                                               #
#  Copyright (c) 2005,  Harrison Ainsworth.                          #
#                                                                    #
#  http://hxa7241.org/                                               #
#                                                                    #
#--------------------------------------------------------------------#




require 'find'



# Counts lines of code in ruby source files.
#
# just a simple example ruby program, produced as an exercise.
#
# * directories are looked in recursively for source files
# * source files with the following name extensions are recognized:
#   .rb .rbw
# * a line is counted as code if it is not empty and not solely comment
#
# == requirements
# ruby 1.8
#
# == usage
# RubyGauge.rb [-f...] (file|directory)pathname ...
# RubyGauge.rb -help|-?
#
# switches:
#    -f<[l|s][1|2]>  set output format to long/short, linecount only /
#                    linecount and filecount (defaults to -fl2)
#    -help|-?        prints this message
#
# ==acknowledgements
# * ruby: http://ruby-lang.org/
# * the pragmaticprogrammers pickaxe book:
#   http://phrogz.net/ProgrammingRuby/
# * rubygarden: http://rubygarden.org/ruby?CodingInRuby
#
# == license
# this software is too short and insignificant to have a license.

module RubyGauge

	# Entry point if run from the command line.
	#
	# Reads command line args, writes output to stdout.
	#
	def RubyGauge.main

		# check if help message needed
		if $*.empty? || !(RubyGauge.getSwitchs( $*, '(help|\?)' ).empty?)

			puts "\n#{@@BANNER}\n#{@@HELP}"

		else

			# count
			pathnames = RubyGauge.getTokens( $*, '^[^-\/]', ' ' )

			fileCount = []
			lineCount = RubyGauge.countLinesInFileTree( pathnames, fileCount )

			# output counts
			format = RubyGauge.getSwitchs( $*, 'f', '-fl2' ).last[2,2]

			(@@LINES_FORMAT = { 's' => "#{lineCount}" }).default = "\n   #{lineCount} line#{lineCount == 1 ? '' : 's'} of code\n"
			(@@FILES_FORMAT = { 's' => " #{fileCount}" }).default = "   #{fileCount[0]} file#{fileCount[0] == 1 ? '' : 's'}\n"
			print @@LINES_FORMAT[format[0,1]]
			print @@FILES_FORMAT[format[0,1]] unless format[1,1] == '1'

		end

	end


	def RubyGauge.getSwitchs( commandline, pattern, default=nil )

		RubyGauge.getTokens( commandline, '^(-|\/)' + pattern, default )

	end


	def RubyGauge.getTokens( commandline, pattern, default=nil )

		tokens = []

		commandline.each do |token|
			if token =~ /#{pattern}/
				tokens.push token
			end
		end

		if tokens.empty? && default
			tokens.push default
		end

		tokens

	end


	# Counts lines of ruby code in filetree recursively.
	#
	# A line is counted as code if it is not empty and not solely comment.
	#
	# == parameters
	# * pathnames: Array of String of file or directory pathname
	#   (relative or absolute)
	# * fileCount: Array of Numeric, length 1. Just an example of a
	#   'reference' parameter
	# * return: Fixnum of the line count
	#
	def RubyGauge.countLinesInFileTree( pathnames, fileCount=[] )

		fileCount[0] = 0
		lineCount    = 0

		# scan directory tree
		Find.find( *pathnames ) do |fileOrDirName|

			# filter file types (to ruby)
			if FileTest.file?( fileOrDirName ) &&
			   FileTest.readable?( fileOrDirName ) &&
			   fileOrDirName =~ /\.(rb|rbw)\Z/

			   fileCount[0] += 1

				filePathname = File.expand_path( fileOrDirName )

				# read file
				File.open( filePathname, 'r' ) do |file|

					# scan file lines
					file.each_line do |line|
						# select non blank, non comment-only line
						unless line =~ /^\s*(#|\Z)/
							lineCount += 1
						end
					end

				end

			end
		end

		lineCount

	end


	@@BANNER = "-------------------------------------------------------------\n" +
	           "RubyGauge 2005 (v1)\n" +
	           "Copyright (c) 2005,  Harrison Ainsworth.\n\n" +
	           "http://hxa7241.org/\n" +
	           "-------------------------------------------------------------\n"

	@@HELP   = "RubyGauge counts lines of code in ruby source files.\n\n" +
	           "* directories are looked in recursively for source files\n" +
	           "* source files with the following name extensions are recognized: .rb .rbw\n" +
	           "* a line is counted as code if it is not empty and not solely comment\n" +
	           "\nusage:\n" +
	           "   RubyGauge.rb [-f...] (file|directory)pathname ...\n" +
	           "   RubyGauge.rb -help|-?\n" +
	           "\nswitches:\n" +
	           "   -f<[l|s][1|2]>  set output format to long/short, linecount only / linecount and filecount (defaults to -fl2)\n" +
	           "   -help|-?        prints this message\n"

end




RubyGauge.main
