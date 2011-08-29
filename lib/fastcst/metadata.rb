require 'guid'
require 'yaml'
require 'fastcst/changeset'


module MetaData

    META_DATA_FILE = "meta-data.yaml"
    
    
    # A wrapper function that loads the given YAML file, passes the
    # result to a block, and then saves the modified meta-data
    # back to disk.
    def MetaData.update_md(file_name)
        md = YAML.load_file(file_name)
        yield md
        File.open(file_name, "w") { |out| YAML.dump(md, out) }
    end
    
    # Adds a mentioned file to the meta-data along with its
    # digest.  You would do this for all of the files EXCEPT
    # the .fcs data and .yaml journal files.  MetaData.finish_metadata
    # does that.
    def MetaData.add_file(md_file, name, purpose)
        digest = Digest::MD5.hexdigest(File.read(name))
        
        MetaData.update_md(md_file) do |md|
            md["Contents"] ||= []
            md["Contents"] << { "Name" => name, "Digest" => digest, "Purpose" => purpose }
        end
        
    end
    
    
    # Creates an initial meta-data file from the given information.
    def MetaData.create_metadata(md_file, project, revision, purpose, dev_name, dev_email)
        md = {
        "Project" => project,
        "Revision" => revision,
        "Purpose" => purpose,
        "ID" => Guid.new.to_s,
        "Created By" => {"Name" => dev_name, "E-Mail" => dev_email},
        "Journal" => [{ "Date" => Time.now, "Message" => "Created"}],
        "Summary" => "NONE",
        "Contents" => [],
        "Disposition" => []
        }

        
        UI.event :info, "Writing meta-data file to #{md_file}"
        UI.event :info, "ID: #{md['ID']}"
        File.open(md_file, "w") { |out| YAML.dump(md, out) }

        return md
    end
    
    
    # This finishes off a meta-data file by adding the .fcs data file and
    # .yaml journal file and setting a few other required elements.
    def MetaData.finish_metadata(md_file, parent_id, fcs_file, journal)
        MetaData.update_md(md_file) do |md|
            md["Contents"] ||= []
            md["Parent ID"] = parent_id
        end
        
        
        UI.start_finish("Adding data and journal files #{fcs_file}, #{journal}") do
            MetaData.add_file(md_file, fcs_file, "data")
            MetaData.add_file(md_file, journal, "journal")
        end
            
        MetaData.update_md(md_file) do |md|
            UI.start_finish("Calculating summary statistics for #{journal}") do
                open(journal) do |journal_in|
                    md["Summary"] = ChangeSet.statistics(Zlib::GzipReader.new(journal_in))
                end
            end
        end
    end
    
    
    # Adds a log message to the MetaData.
    def MetaData.log_message(md_file, message)
        MetaData.update_md(md_file) do |md|
            md["Journal"] ||= []
            md["Journal"].unshift({ "Date" => Time.now, "Message" => message})
        end
    end
    
    
    # Adds a disposition record to the meta-data.
    def MetaData.add_disposition(md_file, type, id, relation)
        MetaData.update_md(md_file) do |md|
            disp = {}
            disp["Type"] = type
            disp["ID"] = id
            disp["Relation"] = relation
            
            md["Disposition"] ||= []
            md["Disposition"] << disp
        end
    end
    
    # Loads the meta-data from a file.
    def MetaData.load_metadata(md_file)
        return YAML.load_file(md_file)
    end
    
    
    
    # Takes a meta data structure and finds the journal and data files 
    # listed in the Contents section.  It returns them as an array 
    # and will set them to nil if one or both are not found.
    def MetaData.extract_journal_data(md)                   
        journal_file = nil
        data_file = nil
        
        md['Contents'].each do |info|
            name, digest, purpose = info['Name'], info['Digest'], info['Purpose']
            
            if purpose == "data"
                # found the data file
                data_file = name
            elsif purpose == "journal"
                # found the journal file
                journal_file = name
            end
            
            # found them, don't bother looking further
            if data_file and journal_file
                break
            end
        end
        
        return journal_file, data_file
    end
    
    # Verifies that all the files mentioned in the Contents section of the
    # meta-data have valid md5 digests.  It returns an array of the failures
    # and prints an error message.  If you get an empty array then all the files
    # checked out.
    def MetaData::verify_digests(base_path, md)
        failures = []
        
        # verify the digests
        md['Contents'].each do |info|
            name, digest, purpose = info['Name'], info['Digest'], info['Purpose']
            
            target = File.join(base_path, name)
            if digest != Digest::MD5.hexdigest(File.read(target))
                UI.failure :security, "#{name} MD5 digest does not match. Aborting."
                failures << name
            end
        end
        
        return failures
    end
    
end
