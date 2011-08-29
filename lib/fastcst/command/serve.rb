require 'fastcst/ui'
require 'fastcst/repo'
require 'webrick'
require 'webrick/httputils'


include WEBrick


module Repository

    class ServeCommand < Command
        def initialize(argv)
            @port = 3040
            
            super(argv, [
            ["-p", "--port PORT", "Port to use for serving (3040 by default)", :@port],
            ])
            
            @repo_dir = Repository.search
        end
    
        def validate
            valid? @repo_dir, "Could not find a repository directory"
            
            return @valid
        end
    
    
        def run
            repo = Repository.new @repo_dir
            
            @mime_table = HTTPUtils::DefaultMimeTypes
            @mime_table.update({"yaml" => "text/plain"})
            
            @server = HTTPServer.new(
                :Port => @port,
                :MimeTypes => @mime_table
                )
            
            @server.mount("/root", HTTPServlet::FileHandler, repo.root_dir, {:FancyIndexing => true})
            @server.mount_proc("/index.yaml") {|req, resp|
                # handles generating the index.yaml file that is normally published
                repo = Repository.new Repository.search
                local_index = {'Changesets' => repo.list_changesets.sort, 'Revision Path' => repo['Path']}
                resp.body = YAML.dump(local_index)
            }
            
            trap("INT") {
                puts "Shutting down..."
                @server.shutdown 
            }
            
            @server.start
        end
    end
end


