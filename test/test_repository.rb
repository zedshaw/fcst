require 'test/unit'
require 'fastcst/repo'
require 'fileutils'
require 'fastcst/command'
require 'benchmark'


module UnitTest
    
    # The repository uses commands and the CommandRegistry so that it can run
    # the commands and redirect the output to a test/test.log file without
    # cluttering the output.
    class RepositoryTest < Test::Unit::TestCase
    
        CHANGESET_COUNT = 50

    
        def setup
            @repo_dir = "test/.fastcst"
            FileUtils.rm_rf @repo_dir
            repo = Repository::Repository.create(@repo_dir, env={ 'test' => "stuff" })
        end
    
    
        def teardown
            FileUtils.rm_rf @repo_dir
        end
    
    
        def test_new
            repo = Repository::Repository.new @repo_dir

            assert_not_nil repo
            assert_not_nil repo.path
            assert_not_nil repo.env_yaml
            assert_not_nil repo.originals_dir
            assert_not_nil repo.pending_mbox
            assert_not_nil repo.root_dir
        
            # check that all the right files are there
            assert File.exists?(repo.path)
            assert File.exists?(repo.env_yaml)
            assert File.exists?(repo.originals_dir)
            assert File.exists?(repo.pending_mbox)
            assert File.exists?(repo.root_dir)
        end
    
        
        def test_changeset

            # put it in the repository
            repo = Repository::Repository.new @repo_dir

            # test out building the revision tree
            tree = repo.revision_tree
            assert_not_nil tree
        
            # list out the changesets and make sure there's 2
            list = repo.list_changesets
        
            # go through each changeset, find it, and then delete it
            list.each do |id|
                # finding a changeset                
                path, md = repo.find_changeset id
                assert_not_nil path
                assert_not_nil md
            end

            list.each { |id| repo.find_parent_of(id) } 
            list.each { |id| repo.find_all_children(id) } 
            list.each { |id| 
                res = repo.build_readable_name(id) 
                assert_not_nil res, "Readable name is empty, should never happen"
            }
            list.each { |id| 
                prev_tree = repo.revision_tree
                repo.delete_changeset id
                assert_not_equal prev_tree, repo.revision_tree, "Tree didn't change"
            }

        
            list.each do |id|
                # make sure it's gone
                path, md = repo.find_changeset id
            
                assert_equal path, nil
                assert_equal md, nil
            end

            # and finally make sure there's nothing left
            list = repo.list_changesets
            assert_equal list.length, 0, "There should be no changesets left"
        end
        
    
        def test_env
            repo = Repository::Repository.new @repo_dir
        
            repo["setting"] = "something"
            res = repo["setting"]
            assert_equal res, "something", "Repository didn't properly store a variable in env"
            
            # there is a weird bug that pops up where changing an empty path doesn't happen
            repo['Path'] = []
            path = repo['Path']
            assert path.empty?, "Path was not empty when it should be"
            
            repo['Path'] = repo['Path'] << 'TEST'
            path = repo['Path']
            assert_equal 1, path.length, "Path did not have one element"
            assert_equal 'TEST', path.pop, "Path element didn't match expected"
            
        end
       
    
        def test_search
            FileUtils.rm_rf @repo_dir
            repo = Repository::Repository.create(@repo_dir)
            assert_not_nil repo
            path = Repository::Repository.search(".fastcst", from="test")
            assert_equal File.join(Dir.getwd, "test", ".fastcst"), path
        end
    end
end