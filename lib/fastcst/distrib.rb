require 'yaml'
require 'open-uri'
require 'net/http'
require 'net/ftp'
require 'fastcst/ui'

# Handles the distribution of changesets

module Distribution

    X_FASTCST_ID = 'X-FastCST-ID'
    X_FASTCST_MD_NAME = 'X-FastCST-MD-NAME'
    X_FASTCST_PROJECT_NAME = 'X-FastCST-Project-Name'
    

    # Uploads files using FTP that are mentioned in the md_file.
    def Distribution.upload(ftp, md_file)
            
        # load the meta-data file to get what we need to upload
        md = YAML.load_file(md_file)
            
        contents = md['Contents']
        if not contents
            UI.failure :input, "Your meta-data file does not have a Contents spec, it's malformed"
        end
            
        contents.each do |file|
            name, purpose, digest = file['Name'], file['Purpose'], file['Digest']
            UI.start_finish "#{name} - #{purpose} - #{digest}" do
                ftp.putbinaryfile(name)
            end
        end

        # now upload the meta-data file and we're done
        UI.start_finish "Uploading meta-data" do
            ftp.putbinaryfile(md_file)
        end
    end



    # Downloads a meta-data file from the url.  It uses the open-uri stuff
    # so url can be anything which open-uri can handle.
    def Distribution.download_meta_data(url, md_file)
        md_url = url + "/" + md_file
        md = nil
    
        # we need to download the requested stuff and store it someplace
        UI.start_finish("Downloading changeset meta-data") do
            open(md_url) do |f| 
                data = f.read
                md = YAML.load(data)
                open(md_file, "w") { |f| f.write data }
            end
        end
    
        return md
    end

    
    # Downloads the contents of a meta-data structure (it must be the structure,
    # not the file see MetaData.load_metadata).  It uses open-uri so the url can
    # be pretty much anything that open-uri understands.
    def Distribution.download_md_contents(url, md)
        # now we need to download each file mentioned in the changeset
        data_file = nil
        journal_file = nil
    
        md['Contents'].each do |file|
            UI.start_finish("Downloading #{file['Name']}") do
                data = nil
                if File.exists? file['Name']
                    UI.event :info, "Already exists, will just digest and use the existing one"
                    data = File.read file['Name']
                else
                    open(URI.parse(url + "/" + file['Name'])) { |f| data = f.read }
                end
                    
                digest = Digest::MD5.hexdigest(data)
                if digest == file['Digest']
                    # file is probably good
                    File.open(file['Name'], "wb") {|f| f.write(data) }
                else
                    UI.failure :security, "Digest of downloaded file #{file['Name']} failed, digests do not match"
                end
            end

            if file['Purpose'] == 'data'
                data_file = file['Name']
            elsif file['Purpose'] == 'journal'
                journal_file = file['Name']
            else
                UI.event :warn, "Ignoring file #{file['Name']} since not needed"
            end
        end
    
        return [data_file, journal_file]
    end    

    
    def Distribution.send_changeset(md_file, to_addr, server, port)
        # load the meta-data so we can process the contents
        md_contents = File.read(md_file)
        md = YAML.load(md_contents)
        
        # get just the dir and file separator
        md_dir, md_file = File.dirname(md_file), File.basename(md_file)

        # setup the from if the user didn't specify it
        from = md['Created By']['E-Mail']
        
        message = RMail::Message.new
        
        UI.event :sending, "#{to_addr} -- #{from} -- #{md['ID']}"
        message.header['To'] = to_addr
        message.header['From'] = from
        message.header['Subject'] = "[FCST] #{md['Project']} #{md['Revision']} -- #{md['Purpose']}"
        message.header[X_FASTCST_ID] = md['ID']
        message.header[X_FASTCST_PROJECT_NAME] = md['Project']
        
        # add the meta-data contents as the first contents inline
        part = RMail::Message.new
        part.header['Content-Disposition'] = 'inline'
        part.header[X_FASTCST_MD_NAME] = md_file
        part.body = md_contents
        message.add_part(part)
        
        Dir.chdir md_dir do
            # load the request files and make messages for them
            UI.event :encoding, "Adding #{md_file} specified contents:"
            
            md['Contents'].each do |info|
                name, digest, purpose = info['Name'], info['Digest'], info['Purpose']
                
                UI.event :encoding, "#{name} - #{digest} - #{purpose}"
                # encode the contents as a base64 chunk
                part = RMail::Message.new
                part.header['Content-Disposition'] = "attachment; filename=#{name}"
                part.header['Content-Type'] = "x-application/fastcst; name=#{name}"
                part.header['Content-Transfer-Encoding'] = "base64"
                part.body = Base64.encode64(File.read(name))
                
                message.add_part(part)
            end
        end
        
        to_send = RMail::Serialize.write('', message)

        # and now we just send, MAGIC!
        UI.start_finish("Sending message") do
            Net::SMTP.start(server, port) do |smtp|
                smtp.send_message to_send, from, to_addr
            end
        end
    end
    
end