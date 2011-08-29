require 'fastcst/ui'
require 'fastcst/repo'
require 'find'
require 'odeum_index'
require 'set'
require 'zlib'

class IndexCommand < Command
    MAX_WORDS = 20
    
    def initialize(argv)

        super(argv, [
        ["-r", "--remove", "Remove the catalogs and start over", :@remove],
        ["-c", "--cull", "Clears out documents which don't exist in the current source anymore", :@cull],
        ["-x", "--excludes", "List of file/directory regex that are to be excluded from the indexing (separate with commas).", :@exclude_string],
        ])

        @repo_dir = Repository::Repository.search
    end
    
    def validate
        valid? @repo_dir, "Could not find repository directory"
        @repo = Repository::Repository.new @repo_dir if @repo_dir
        @excludes = @repo['Excludes'] || []
        
        @index_dir = File.join(@repo_dir, "index")
        
        if @exclude_string
            @exclude_string.split(",").each do |ex|
                @excludes << Regexp.new(ex)
            end
        end
        
        # always add .fastcst
        @excludes << Regexp.new("^\./\.fastcst$")
        
        return @valid
    end
    
    
    def setup_new_doc(odeum, uri, file)
        doc = Odeum::Document.new uri
        contents = File.read(file)
        doc.add_content(odeum, contents)
        doc["Date"] = File.mtime(file).to_s
        
        return doc
    end

    def create_index(catalog)
        odeum_dir = File.join(@index_dir, catalog)

        if @remove
            puts "Removing index #{catalog}"
            Odeum::remove(odeum_dir)
        end
        
        return Odeum::Index.new(odeum_dir, Odeum::OWRITER | Odeum::OCREAT)
    end
    
    # Goes through the catalog and removes any files from it which are not
    # in the current directory.  Not intended to be used with the revisions
    # indexing as those are expected to never change.
    def cull_index(dir, catalog)
        puts "Culling #{catalog} index"
        odeum = create_index(catalog)
        odeum.iterator
        Dir.chdir dir do
            while doc = odeum.next
                uri = doc.uri
                if not File.exist? uri 
                    puts "- #{uri}"
                    odeum.delete(doc.uri)
                end
                doc.close
            end
        end
        odeum.close
    end
    
    
    def excluded(file)
        matched = @excludes.select { |r| file =~ r }
        
        if matched.length > 0
            puts "#{file}: excluded"
            return matched
        else
            return nil
        end
    end
    
    
    def build_index(dir, catalog)
        odeum = create_index(catalog)
        
        i = 0
        Dir.chdir(dir) do
            Find.find("./") do |file|
                if File.directory? file and excluded(file)
                    puts "Skipping directory #{file}"
                    Find.prune
                elsif File.file? file and not excluded(file)
                    doc = odeum.get(file)
                    if not doc or doc["Date"] != File.mtime(file).to_s
                        puts "#{file}"
                        doc = setup_new_doc(odeum, file, file)
                        odeum.put(doc, MAX_WORDS, true)
                        
                        if (i += 1) % 1000 == 0 
                            print "Crunching index...."
                            $stdout.flush
                            odeum.sync
                            print "DONE."
                            $stdout.flush
                        end
                    end
                    
                    doc.close if doc
                end
            end
        end
        
        if @cull
            print "Optimizing #{catalog} index..."
            $stdout.flush
            odeum.optimize
            puts "DONE"
            $stdout.flush
        end
        
        odeum.close
    end

    def build_revision_index(dir, catalog)
        cs_ids = @repo.list_changesets
        odeum = create_index(catalog)
        
        cs_ids.each do |id|
            path, md = @repo.find_changeset(id)
            md_file = @repo.find_meta_data(id)
            md_uri = File.join(id, File.basename(md_file))

            doc = odeum.get(md_uri)
            
            if not doc
                puts "changeset: #{id}"

                # index the meta-data file with a fake path as the uri
                doc = setup_new_doc(odeum, md_uri, md_file)
                odeum.put(doc, MAX_WORDS, true)
                doc.close

                # index the journal and fcs contents
                Dir.chdir path do
                    journal_file, data_file = MetaData.extract_journal_data(md)
                    journal_in = Zlib::GzipReader.new(File.open(journal_file))
                    data_in = Zlib::GzipReader.new(File.open(data_file))
                    
                    # each document 
                    YAML.each_document(journal_in) do |record|
                        type, info = record

                        rev_uri = nil
                        if type == "directory"
                            # the directory entry is a special case
                            rev_uri = File.join(id, journal_file, "directory")
                        else
                            rev_uri = File.join(id, journal_file, info[:path])
                        end
                        
                        # make this record's meta-data the contents
                        doc = Odeum::Document.new rev_uri
                        doc.add_content(odeum, YAML.dump(record))
                        odeum.put(doc, MAX_WORDS, true)
                        doc.close

                        ### setup for the data portion
                        if info[:path]
                            doc = Odeum::Document.new File.join(id, data_file, info[:path])
                        
                            # put some of the meta-data into the document
                            doc["operation"] = type.to_s
                            doc["digest"] = info[:digest].to_s
                            doc["path"] = info[:path].to_s
                            doc["mtime"] = info[:mtime].to_s if info[:mtime]
                            
                            doc.add_content(odeum, data_in.read(info[:length])) if info[:length]
                            odeum.put(doc, MAX_WORDS, true)
                            doc.close
                        end
                    end
                    
                    journal_in.close
                    data_in.close
                end
            else
                doc.close
            end
        end
        
        if @cull
            print "Optimizing #{catalog} index..." ; $stdout.flush
            odeum.optimize
            puts "DONE"
        end
        
        odeum.close
    end
    
    def run
        Dir.mkdir @index_dir if not File.exist? @index_dir
        
        cull_index(File.dirname(@repo.path), "current") if @cull
        
        build_index(File.dirname(@repo.path), "current")
        
        build_revision_index(@repo.root_dir, "revisions")
    end

end

