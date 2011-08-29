require 'rmail'
require 'net/smtp'
require 'net/pop'
require 'base64'
require 'digest/md5'
require 'digest/md5'
require 'fastcst/ui'
require 'fastcst/repo'

include Repository


# Implements commands to support simple changeset distribution using
# e-mail.  It combines SMTP and POP3 access to give an "e-mail like"
# interface to changesets.

module Distribution

    # The send command takes a changeset meta-data file and packs it up
    # into an e-mail.  It will then send it to the address you designate
    # with the right setting so that others can receive it.
    class SendCommand < Command
        
        def initialize(argv)
            super(argv, [
                ["-i", "--id ID", "Specify a changeset ID to send", :@id],
                ["-r", "--rev ID", "Specify a changeset Revision name to send", :@rev],
                ["-t", "--to ADDR", "The to e-mail address", :@to_addr],
                ["-c", "--connect HOST[:PORT]", "The SMTP server host (port optional)", :@host]
            ])
            
            @repo_dir = Repository::Repository.search
            
        end
        
        
        def validate
            valid? @repo_dir, "Could not find repository directory"
            
            if @repo_dir
                @repo = Repository::Repository.new @repo_dir

                # get default values for things
                @host = @repo.env_default_value('SMTP Host', @host)
                @server, @port = @host.split(":") if @host
                
                valid? @server, "Invalid server specification given"
                valid? @port, "Invalid port specification given"
                valid? @to_addr, "Need to set the 'To' address"
                valid?((not (@id and @rev)), "You cannot specify an id (-i) AND a revision name (-r)")
            end
            
            return @valid
        end
        
        
        def run
            @id = @repo.resolve_id(@rev, @id)
            
            if @id
                md_file = @repo.find_meta_data(@id)
                Distribution.send_changeset(md_file, @to_addr, @server, @port)
            else
                UI.failure :search, "Could not find a matching revision"
            end
        end
    end

    
    

    class RecvCommand < Command
        
        
        def initialize(argv)
            @delete = true
            
            super(argv, [
                ["-u", "--username NAME", "Your POP3 username", :@user],
                ["-p", "--pass WORD", "Your POP3 password", :@pass],
                ["-c", "--connect HOST[:PORT]", "The POP3 server host (port optional)", :@host],
                ["-d", "--[no-]delete", "Remove the messages when done", :@delete],
                ["-m", "--mbox FILE", "Alternative mbox file to use (optional)", :@mbox]
            ])

            @repo_dir = Repository::Repository.search
        end
        
        
        def validate
            valid? @repo_dir, "Could not find a repository directory"
            
            if @repo_dir
                @repo = Repository::Repository.new @repo_dir
                @host = @repo.env_default_value('POP3 Host', @host)
                @server, @port = @host.split(":") if @host
                @user = @repo.env_default_value('POP3 Username', @user)

                @mbox = @repo.pending_mbox unless @mbox
                
                valid? @server, "Invalid server specification"
                valid? @port, "Invalid port specification"
                valid? @user, "Give a username for the POP3 server or set 'POP3 Username' in environment"
                valid? @pass, "Give a password for the POP3 server"
                valid? @mbox, "Mailbox not specified (should not happen)"
                valid_exists? @mbox, "Mailbox file #@mbox does not exist"
            end
            
            return @valid
        end
        
        def get_current_ids(inbox)
            ids = {}
            RMail::Mailbox.parse_mbox(inbox) do |text|
                m = RMail::Parser.read(text)
                if m.header[Distribution::X_FASTCST_ID]
                    ids[m.header[Distribution::X_FASTCST_ID]] = true
                end
            end
            
            return ids
        end
        
        
        def run
            pop = Net::POP3.new(@server, @port)
            pop.start(@user, @pass)
            
            fcst_msg_count = 0
            
            if pop.mails.empty?
                UI.event :mail, "No messages available in your POP3 account."
            else
                inbox = File.open(@mbox, "w+")
                
                # get the current FCST changeset ids and then go to the end 
                ids = get_current_ids(inbox)
                inbox.seek(0, IO::SEEK_END)
                
                pop.each_mail do |m|
                    data = m.header
                    msg = RMail::Parser.read(data)
                    
                    if msg.header[Distribution::X_FASTCST_ID]
                        fcst_msg_count += 1

                        # we skip message that we already have downloaded
                        if ids[msg.header[Distribution::X_FASTCST_ID]]
                            UI.event :duplicate, "#{msg.header['Subject']}\n- #{msg.header[Distribution::X_FASTCST_ID]} already in your inbox"
                        elsif @repo.find_meta_data(msg.header[Distribution::X_FASTCST_ID])
                            UI.event :duplicate, "#{msg.header['Subject']}\n- #{msg.header[Distribution::X_FASTCST_ID]} already in your repository"
                        elsif msg.header[Distribution::X_FASTCST_PROJECT_NAME] != @repo['Project']
                            UI.event :project, "#{msg.header['Subject']}\n- For project #{msg.header[Distribution::X_FASTCST_PROJECT_NAME]} not this one."
                            next
                        else
                            UI.event :from, "#{msg.header['From']}"
                            UI.event :subject, "#{msg.header['Subject']}"
                            UI.event :id, "#{msg.header[Distribution::X_FASTCST_ID]}"
                            
                            inbox.write("From #{msg.header['From']} #{Time.now}")
                            inbox.write("\n")
                            inbox.write(m.pop.tr("\r", ''))
                            inbox.write("\n")
                            
                            # must update our ids so we don't add duplicates of ones we just added
                            ids[msg.header[Distribution::X_FASTCST_ID]] = true
                        end
                        
                        if @delete
                            UI.event :warn, "Deleting message."
                            m.delete
                        end
                    else
                        UI.event :skipped, "#{msg.header['From']}"
                    end
                end
                
                inbox.close
                
                puts "#{fcst_msg_count} FastCST messages out of #{pop.mails.size} mails processed."
            end
            
            pop.finish
        end
    end


    class ReadCommand < Command
        
        def initialize(argv)
            super(argv, [
                ["-m", "--mbox FILE", "The mbox formatted file to read", :@mbox]
            ])
            
            @repo_dir = Repository::Repository.search
        end
        
        def validate
            valid? @repo_dir, "Could not find repository directory"
            
            if @repo_dir
                @repo = Repository::Repository.new @repo_dir
                @mbox = @repo.pending_mbox unless @mbox
                
                valid? @mbox, "Mailbox not specified (should not happen)"
                valid_exists? @mbox, "Mailbox file #@mbox does not exist"
            end
            
            return @valid
        end
        
        def compare_digests(md, tgt_name, tgt_digest)
            contents = md['Contents']
            contents.each do |info|
                name, digest, purpose = info['Name'], info['Digest'], info['Purpose']
                if tgt_name == name and tgt_digest == digest
                    return info
                end
            end
            
            return nil
        end

        
        # Loads the message from the mbox, saves all it's meta-data and content files,
        # and then returns the list of files (meta-data, data, journal) so it can be processed further.
        def load_save_changeset(msg)
            md_fname = nil
            data_file = nil
            journal_file = nil
            
            # body is an array of Message objects for each part
            parts = msg.body
                        
            # first element should be the meta-data YAML
            md_part = parts.shift
            md = YAML.load(md_part.body)
            md_fname = md_part.header[Distribution::X_FASTCST_MD_NAME]

            UI.event :file, "Saving meta-data file #{md_fname}"
            File.open(md_fname, "w") { |f| f.write(md_part.body) }
                        
            # remaining parts should match with contents
            parts.each do |part|
                ctype, fname = part.header['Content-Type'].split(';')
                #strip of the name= part
                fname.gsub!("name=", "").strip!
                            
                data = Base64.decode64(part.body)
                digest = Digest::MD5.hexdigest(data)
                            
                # confirm that the digest matches the meta-data digest
                info = compare_digests(md, fname, digest)
                if info
                    UI.event :file, "Matched #{info['Name']} #{info['Digest']}"
                    UI.event :file, "Saving file"
                    File.open(info['Name'], "w") { |f| f.write data }
                    
                    case info['Purpose']
                    when 'data':
                        data_file = info['Name']
                    when 'journal':
                        journal_file = info['Name']
                    end
                else
                    UI.failure :file, "NON-MATCHED: #{fname} #{digest}"
                end
            end

            return md_fname, data_file, journal_file
        end
        
        
        def run
            still_pending = []
            mbox_stream = open(@mbox)
            
            RMail::Mailbox.parse_mbox(mbox_stream) do |text|
                
                msg = RMail::Parser.read(text)

                # only process fastcst tagged messages
                if msg.header[Distribution::X_FASTCST_ID]        
                    UI.event :from, "#{msg.header['From']}"
                    UI.event :subject, "#{msg.header['Subject']}"
                    UI.event :id, "#{msg.header[Distribution::X_FASTCST_ID]}"

                    if msg.multipart?
                        answer = UI.ask("Add this changeset to your repository (D=delete)? [D/Y/n]").downcase
                        
                        if answer == "y"
                            # go into the work directory, save them, and then add them to the repo
                            md_file = nil
                            
                            Dir.chdir @repo.work_dir do
                                md_file, data_file, journal_file = load_save_changeset(msg)

                                # check if this one is already in the repository
                                md = MetaData.load_metadata(md_file)
                                
                                if @repo.find_meta_data(md['ID'])
                                    answer = UI.ask("This changeset is already in your repository.  Add anyway? [Y/n]").downcase
                                    
                                    if answer != "y"
                                        UI.failure :constraint, "Will not add this one.  Delete it using the read command again."
                                        return
                                    end
                                end
                            end
                            
                            # store the newly created changeset, doing a move instead of a copy
                            md = @repo.store_changeset @repo.work_dir, md_file, move=true
                        elsif answer != "d"
                            # by the answer not being 'y' and not 'd' they want to keep this one
                            # in this case they answered no so we keep it
                            still_pending << msg
                        end
                    else
                        puts "ERROR: Not Multipart, bad file"
                        still_pending << msg
                    end
                end
            end
            
            # finished processing all messages, we now have to write back 
            # the list of messages still_pending since these were not processed
            mbox_stream.close
            
            UI.start_finish("Writing remaining #{still_pending.length} messages back to inbox") do 
                File.open(@mbox, "w") do |out|
                    still_pending.each do |msg|
                        out.write("From #{msg.header['From']} #{Time.now}")
                        out.write("\n")
                        RMail::Serialize.write(out, msg)
                        out.write("\n")
                    end
                end
            end
            
        end
    end
    
end
