require 'rake'
require 'rake/testtask'
require 'rake/gempackagetask'
require 'rake/clean'
require 'rake/rdoctask'
require 'tools/rakehelp'
require 'fileutils'
include FileUtils

BUILD = "build"


setup_tests
setup_clean ["ext/**/Makefile", 'build/fcst.rb', 'build/**/*', 'pkg',
             'test/test.out', 'test/test.nstd', 
             'software/rubymail-0.17', 'software/PluginFactory-1.0.1',
             'software/ruby-guid-0.0.1', 'ext/**/mkmf.log']
setup_rdoc ['README', 'LICENSE', 'COPYING', 'lib/**/*.rb', 
            'doc/**/*.rdoc', 'test/*.rb', 'ext/sarray/suffix_array.c', 'ext/odeum_index/odeum_index.c']

desc "Does a full compile, test, tar2rubyscript run"
task :default => [:compile, :test, :tar]

desc "Compiles all extensions"
task :compile => [:suffix_array, :odeum_index]

task :package => [:clean]

setup_extension "sarray", "suffix_array"
setup_extension "odeum_index", "odeum_index"

desc "Extracts required software from the software directory"
task :extract_software do
    `tar -C software -xzf software/rubymail-0.17.tar.gz`
    `tar -C software -xzf software/ruby-guid-0.0.1.tar.gz ruby-guid-0.0.1/lib/guid.rb`
    `tar -C software -xzf software/PluginFactory-1.0.1.tar.gz PluginFactory-1.0.1/lib/pluginfactory.rb`
end

desc "Packages all files into one single ruby script in build/fcst.rb"
task :tar => [:extract_software, :compile] do
    rm_rf "build/fcst"
    cp_r "software/rubymail-0.17/lib/rmail", "build"
    cp_r "software/rubymail-0.17/lib/rmail.rb", "build"
    cp_r "lib/fastcst", "build"
    cp "software/PluginFactory-1.0.1/lib/pluginfactory.rb", "build"
    cp "software/ruby-guid-0.0.1/lib/guid.rb", "build"
    cp "lib/sadelta.rb", "build"
    cp "lib/suffix_array.#{Config::CONFIG['DLEXT']}", "build"
    cp "lib/odeum_index.#{Config::CONFIG['DLEXT']}", "build"
    cp "app/init.rb", "build"
    `chmod -R u+rw build/`
    `ruby tools/tar2rubyscript.rb build build/fcst LICENSE`
    # need to fix up the script so it runs with the right header
    file = File.read("build/fcst")
    File.open("build/fcst", "w") do |f|
        f.write("#!/usr/bin/env ruby\n")
        f.write(file)
    end
    File.chmod(0755, "build/fcst")
end


task :prep_finish => [:clean] do
    `sudo ruby setup.rb clean`
    `rm -rf doc/rdoc`
end

summary = "FastCST is a revision control system (VCS, RCS, SCM tool)."
test_file = "test/test_commands.rb"
setup_gem("fastcst", "0.6.6",  "Zed A. Shaw", summary, ["fcst"], test_file)

