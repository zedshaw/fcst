require 'mkmf'

dir_config("odeum")

have_library("c", "main")
have_library("pthread", "main")
have_library("z", "main")
have_library("iconv", "main")

create_makefile("odeum_index")
