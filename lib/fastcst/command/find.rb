require 'fastcst/ui'
require 'fastcst/repo'
require 'find'
require 'odeum_index'
require 'set'



class FindCommand < Command
    
    def initialize(argv)
        @lines = nil
        @catalog = "current"
        @summarize = false
        @default_op = "&"
        
        super(argv, [
        ["-r", "--revisions", "Search through revisions", :@revisions],
        ["-f", "--files REGEX", "Only search files matching REGEX (default all)", :@files_regex],
        ["-l", "--lines OP", "Display lines where OP is: first, any, all. (not in revisions)", :@lines],
        ["-g", "--grep REGEX", "Only show results which match this regex", :@line_grep],
        ["-s", "--summarize INT", "Show a word summary for each word requested of count COUNT", :@summarize],
        ["-D", "--default_op OP", "Use the given operator as the default for sequences of words", :@default_op],
        ])
            
        @catalog = "revisions" if @revisions
        @search = argv.join(" ").split(" ")
        @repo_dir = Repository::Repository.search
    end
    
    def validate
        valid? @repo_dir, "Could not find repository directory"
        @index_dir = File.join(@repo_dir, "index")

        if @repo_dir
            repo = Repository::Repository.new @repo_dir
            @catalog_map = { "current" => File.dirname(repo.path), "revisions" => repo.root_dir }
        end
        
        
        # check the regex and lines of context count
        begin
            @files_regex = Regexp.new @files_regex if @files_regex
            @line_grep = Regexp.new @line_grep if @line_grep

            if @summarize 
                @summarize = @summarize.to_i
                valid?(@summarize > 0, "You indicated a summary of 0 words.  You probably forgot to give -s a number.")
            end            
        rescue
            UI.failure :input, $!
            @valid = false
        end
        
        # make sure they give the correct stuff for lines
        if @lines
            valid?(["first", "any", "all"].include?(@lines), "You can only specify first, any, or all for -l argument.")
            valid?((not (@lines and @revisions)), "You can't get lines from revisions yet.  Try summary.")
        end
        
        return @valid
    end
    
    # Decides whether to show the document or not, and returns true if it did, or
    # false if it didn't.
    def show_doc(catalog, doc, words)
        doc_shown = false
        
        if @summarize
            doc_words = doc.normal_words
            
            summary = []
            words.each do |w| 
                i = doc_words.index(Odeum::normalizeword(w))
                summary << doc_words[i, @summarize].join(" ") if i
            end
            
            summary = summary.grep @line_grep if @line_grep
            
            if summary.length > 0
                puts "#{doc.uri}: #{summary.join(' ... ')}"
                doc_shown = true
            end

        elsif @lines and not @revisions
            # listing lines out of revision entries isn't quite supported yet            
            # show some contextual information
            location = @catalog_map[catalog]
            fname = doc.uri
            begin
                File.open(File.join(location, fname)) do |file|
                    # what we do is kind of voodo magic, but we're basically building a single regex
                    regex = nil
                    if @lines == "any" || @lines == "first"
                        regex = Regexp.new "(" + words.join("|") + ")"
                    elsif @lines == "all"
                        regex = Regexp.new "(" + words.join(").*(") + ")"
                    end
                    
                    line_num = 0
                    file.readlines.each do |line|
                        line_num += 1
                        # a little complicated, but we need to match the regex and the line_grep if given

                        if line =~ regex and (not @line_grep or line =~ @line_grep)
                            puts "#{fname}:#{line_num}: #{line.strip}"
                            doc_shown = true
                            break if @lines == "first"
                        end
                    end
                end
            rescue
                puts "#{fname}: #$!"
            end
        else
            # just print the file name
            puts doc.uri
            doc_shown = true
        end
        
        return doc_shown
    end
    
    
    
    def parse!(catalog, words)
        odeum = Odeum::Index.new(File.join(@index_dir, catalog), Odeum::OREADER)

        puts "WORDS: #{words.inspect}"
        results = odeum.query(words.join(" "))

        scan_count = 0
        show_count = 0
        while doc = results.next_doc(odeum)
            if doc and @files_regex == nil or doc.uri =~ @files_regex
                shown = show_doc(catalog, doc, words)
                scan_count += 1
                show_count +=1 if shown
            end
            
            doc.close
        end

        puts "\n#{results.length} result sets. #{show_count}/#{scan_count} shown/scanned.  #{odeum.doc_count} documents searched."
        
        return results
    end
    
    
    def run
        puts "Searching #@catalog"
        parse!(@catalog, @search)
    end

end

