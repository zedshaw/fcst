require 'fileutils'


PKG_DIR="/var/cache/pacman/pkg"

files = {}

Dir.entries(PKG_DIR).each do |pkg_file|
    pkg_name = pkg_file.split(".pkg.tar.gz")[0]
    if pkg_name
        parts = pkg_name.split("-")
        rel = parts[-1]
        ver = parts[-2]
        name = parts[0 .. -3].join("-")
        
        files[name] ||= []
        files[name] << [ver, rel]
    end
end


files.sort.each do |name, version|
    puts "#{name}"
    version.each do |ver, rel|
        
    end
end
