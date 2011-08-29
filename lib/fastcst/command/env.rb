require 'fastcst/ui'
require 'fastcst/repo'
require 'yaml'


module Repository

    class EnvCommand < Command
        def initialize(argv)
            @regex = ".*"
        
            super(argv, [
            # set options
            ["-s", "--set KEY", "Key to set (no parameter means delete)", :@key],
            ["-v", "--value VALUE", "Value used with the -s (set) command", :@value],
            
            # get options
            ["-g", "--get REGEX", "Get any values that match the regex", :@regex],
            ["-t", "--type", "Get the type of the variable", :@type]
            ])
            
            @repo_dir = Repository.search
            
        end
    
        def validate
            # check that the regex is good
            begin
                @regex = Regexp.new(@regex) if @regex
            rescue
                UI.failure :input, "#$!"
                @valid = false
            end
            
            valid? @repo_dir, "Could not find a .fastcst directory"
            
            return @valid
        end
    
    
        def run
            repo = Repository.new @repo_dir

            if @key
                orig_val = repo[@key]
            
                if @value
                    repo[@key] = @value
                else
                    repo.delete @key
                    UI.event :env, "#@key deleted."
                end
            
                UI.event :env, "Original value: #{orig_val}" if orig_val
            else
                # load it directly so we can treat it like a hash
                env = YAML.load_file(repo.env_yaml)
                
                env.keys.sort.each do |k|
                    if @regex.match(k) or @regex.match(env[k].inspect)
                        printf "%20 s => %s", k, env[k].inspect
                        
                        if @type
                            print "   (#{env[k].class})\n"
                        else
                            print "\n"
                        end
                    end
                end
            end
        end
    end
end

