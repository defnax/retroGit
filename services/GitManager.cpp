/*******************************************************************************
 * gui/GitManager.cpp                                                          *
 *                                                                             *
 * Copyright (C) 2026 RetroShare Team <retroshare.project@gmail.com>           *
 *                                                                             *
 * This program is free software: you can redistribute it and/or modify        *
 * it under the terms of the GNU Affero General Public License as              *
 * published by the Free Software Foundation, either version 3 of the          *
 * License, or (at your option) any later version.                             *
 *                                                                             *
 * This program is distributed in the hope that it will be useful,             *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of              *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                *
 * GNU Affero General Public License for more details.                         *
 *                                                                             *
 * You should have received a copy of the GNU Affero General Public License    *
 * along with this program. If not, see <https://www.gnu.org/licenses/>.       *
 *                                                                             *
 *******************************************************************************/

#include "GitManager.h"
#include <iostream>
#include <fstream>
#include <string.h>
#include <ctime>
#include <retroshare/rsinit.h>

bool GitManager::init()
{
    int error = git_libgit2_init();
    if (error < 0) {
        std::cerr << "Failed to init libgit2" << std::endl;
        return false;
    }
    return true;
}

void GitManager::shutdown()
{
    git_libgit2_shutdown();
}

bool GitManager::initRepository(const std::string& repoPath)
{
    git_repository *repo = NULL;
    int error = git_repository_init(&repo, repoPath.c_str(), 1); // 1 = is_bare
    
    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "Error " << error << " initRepository: " << (e ? e->message : "Unknown") << std::endl;
        return false;
    }
    
    git_repository_free(repo);
    return true;
}

std::string GitManager::getBareRepoPath(const std::string& groupId)
{
    return RsAccounts::AccountDirectory() + "/retrogit_repos/" + groupId + ".git";
}

bool GitManager::unpackPackfile(const std::string& repoPath, const std::string& packfileData, const std::map<std::string, std::string>& refUpdates)
{
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, repoPath.c_str());
    if (error < 0) {
        std::cout << "unpackPackfile: Repository not found. Initializing bare repo at: " << repoPath << std::endl;
        if (!initRepository(repoPath)) {
            std::cerr << "unpackPackfile failed to initialize bare repository" << std::endl;
            return false;
        }
        error = git_repository_open(&repo, repoPath.c_str());
        if (error < 0) {
            std::cerr << "unpackPackfile failed to open repo after initialization" << std::endl;
            return false;
        }
    }

    // Index the incoming packfile data directly from memory
    git_indexer *idx = NULL;
    std::string packDir = repoPath + "/objects/pack";
    
    error = git_indexer_new(&idx, packDir.c_str(), 0, NULL, NULL);
    if (error == 0) {
        git_indexer_progress stats;
        memset(&stats, 0, sizeof(stats));
        error = git_indexer_append(idx, packfileData.data(), packfileData.size(), &stats);
        if (error == 0) {
            git_indexer_commit(idx, &stats);
        }
        git_indexer_free(idx);
    }
    
    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "Error indexing packfile: " << (e ? e->message : "Unknown") << std::endl;
    }

    // Update branch references
    for (std::map<std::string, std::string>::const_iterator it = refUpdates.begin(); it != refUpdates.end(); ++it) {
        git_oid oid;
        if (git_oid_fromstr(&oid, it->second.c_str()) == 0) {
            git_reference *ref = NULL;
            int ref_err = git_reference_create(&ref, repo, it->first.c_str(), &oid, 1, "RetroGit network sync");
            if (ref_err < 0) {
                const git_error *e = git_error_last();
                std::cerr << "unpackPackfile: git_reference_create failed: " << (e ? e->message : "Unknown error") << std::endl;
            } else {
                if (ref) git_reference_free(ref);
            }

            // Symbolically point HEAD to this branch if it is refs/heads/master or refs/heads/main
            if (it->first == "refs/heads/master" || it->first == "refs/heads/main") {
                git_reference *head_ref = NULL;
                git_reference_symbolic_create(&head_ref, repo, "HEAD", it->first.c_str(), 1, "RetroGit set HEAD");
                if (head_ref) git_reference_free(head_ref);
            }
        }
    }

    git_repository_free(repo);
    return true;
}

bool GitManager::unpackPackfileFromFile(const std::string& repoPath, const std::string& packfilePath, const std::map<std::string, std::string>& refUpdates)
{
    std::ifstream file(packfilePath.c_str(), std::ios::binary);
    if (!file) {
        std::cerr << "unpackPackfileFromFile failed to open file: " << packfilePath << std::endl;
        return false;
    }
    std::string packData((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    return unpackPackfile(repoPath, packData, refUpdates);
}

static int packbuilder_callback(void *buf, size_t size, void *payload) {
    std::string *outStr = static_cast<std::string*>(payload);
    outStr->append(static_cast<const char*>(buf), size);
    return 0;
}

bool GitManager::createPackfile(const std::string& repoPath, std::string& outPackfileData, std::map<std::string, std::string>& outRefUpdates)
{
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, repoPath.c_str());
    if (error < 0) {
        std::cerr << "createPackfile failed to open repo" << std::endl;
        return false;
    }

    git_packbuilder *pb = NULL;
    error = git_packbuilder_new(&pb, repo);
    if (error == 0) {
        git_revwalk *walk = NULL;
        if (git_revwalk_new(&walk, repo) == 0) {
            git_revwalk_sorting(walk, GIT_SORT_TIME);
            if (git_revwalk_push_head(walk) == 0) {
                error = git_packbuilder_insert_walk(pb, walk);
            }
            git_revwalk_free(walk);
        }
        
        if (error == 0) {
            outPackfileData.clear();
            git_packbuilder_foreach(pb, packbuilder_callback, &outPackfileData);
            
            git_oid head_oid;
            if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
                char oid_str[GIT_OID_HEXSZ + 1];
                git_oid_tostr(oid_str, sizeof(oid_str), &head_oid);
                outRefUpdates["refs/heads/master"] = std::string(oid_str);
            }
        }
        git_packbuilder_free(pb);
    }
    
    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "Error creating packfile: " << (e ? e->message : "Unknown") << std::endl;
    }

    git_repository_free(repo);
    return error == 0;
}

bool GitManager::getCommitLog(const std::string &repoPath, std::vector<GitCommitInfo> &commits) {
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, repoPath.c_str()) != 0) {
        return false;
    }

    git_revwalk *walker = nullptr;
    if (git_revwalk_new(&walker, repo) != 0) {
        git_repository_free(repo);
        return false;
    }

    git_revwalk_sorting(walker, GIT_SORT_TIME);
    
    // Push HEAD to start walking
    if (git_revwalk_push_head(walker) != 0) {
        // Fallback: try to push refs/heads/master if HEAD is not resolved
        git_oid master_oid;
        if (git_reference_name_to_id(&master_oid, repo, "refs/heads/master") == 0) {
            git_revwalk_push(walker, &master_oid);
        } else {
            git_revwalk_free(walker);
            git_repository_free(repo);
            return false;
        }
    }

    git_oid oid;
    while (git_revwalk_next(&oid, walker) == 0 && commits.size() < 100) {
        git_commit *commit = nullptr;
        if (git_commit_lookup(&commit, repo, &oid) == 0) {
            GitCommitInfo info;
            char oid_str[GIT_OID_HEXSZ + 1];
            git_oid_tostr(oid_str, sizeof(oid_str), &oid);
            info.hash = std::string(oid_str, 8); // Short hash
            
            const char *summary = git_commit_summary(commit);
            info.message = summary ? summary : "";
            
            const git_signature *sig = git_commit_author(commit);
            if (sig) {
                info.author = sig->name ? sig->name : "";
                
                std::time_t t = sig->when.time;
                char buf[64];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
                info.date = buf;
            }
            
            commits.push_back(info);
            git_commit_free(commit);
        }
    }

    git_revwalk_free(walker);
    git_repository_free(repo);
    return true;
}

bool GitManager::cloneRepository(const std::string& bareRepoPath, const std::string& localPath)
{
    git_repository *cloned_repo = nullptr;
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.bare = 0;
    
    int error = git_clone(&cloned_repo, bareRepoPath.c_str(), localPath.c_str(), &clone_opts);
    if (error == 0) {
        git_repository_free(cloned_repo);
        return true;
    } else {
        const git_error *e = git_error_last();
        if (e) {
            std::cerr << "Git clone failed: " << error << " / " << e->message << std::endl;
        }
        return false;
    }
}

static int tree_walk_cb(const char *root, const git_tree_entry *entry, void *payload) {
    std::vector<std::string> *file_list = static_cast<std::vector<std::string>*>(payload);
    if (git_tree_entry_type(entry) == GIT_OBJECT_BLOB) { // Only files
        file_list->push_back(std::string(root) + git_tree_entry_name(entry));
    }
    return 0;
}

bool GitManager::getRepoFiles(const std::string& repoPath, std::vector<std::string>& files)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, repoPath.c_str()) != 0) return false;

    git_oid head_oid;
    if (git_reference_name_to_id(&head_oid, repo, "HEAD") != 0) {
        // Fallback: try to resolve refs/heads/master if HEAD is not resolved
        if (git_reference_name_to_id(&head_oid, repo, "refs/heads/master") != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &head_oid) != 0) {
        git_repository_free(repo);
        return false;
    }

    git_tree *tree = nullptr;
    if (git_commit_tree(&tree, commit) != 0) {
        git_commit_free(commit);
        git_repository_free(repo);
        return false;
    }

    git_tree_walk(tree, GIT_TREEWALK_PRE, tree_walk_cb, &files);

    git_tree_free(tree);
    git_commit_free(commit);
    git_repository_free(repo);
    return true;
}

bool GitManager::commitChanges(const std::string& repoPath, const std::string& commitMessage, const std::string& authorName, const std::string& authorEmail)
{
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, repoPath.c_str());
    if (error < 0) {
        std::cerr << "commitChanges: Failed to open repo" << std::endl;
        return false;
    }

    git_index *index = NULL;
    error = git_repository_index(&index, repo);
    if (error < 0) {
        git_repository_free(repo);
        return false;
    }

    error = git_index_add_all(index, NULL, GIT_INDEX_ADD_DEFAULT, NULL, NULL);
    if (error < 0) {
        git_index_free(index);
        git_repository_free(repo);
        return false;
    }

    error = git_index_write(index);
    if (error < 0) {
        git_index_free(index);
        git_repository_free(repo);
        return false;
    }

    git_oid tree_id;
    error = git_index_write_tree(&tree_id, index);
    git_index_free(index);
    if (error < 0) {
        git_repository_free(repo);
        return false;
    }

    git_tree *tree = NULL;
    error = git_tree_lookup(&tree, repo, &tree_id);
    if (error < 0) {
        git_repository_free(repo);
        return false;
    }

    git_signature *sig = NULL;
    if (git_signature_default(&sig, repo) < 0) {
        error = git_signature_new(&sig, authorName.c_str(), authorEmail.c_str(), time(NULL), 0);
        if (error < 0) {
            git_tree_free(tree);
            git_repository_free(repo);
            return false;
        }
    }

    git_oid parent_id;
    git_commit *parent = NULL;
    int parent_count = 0;
    const git_commit *parents[1] = { NULL };

    if (git_reference_name_to_id(&parent_id, repo, "HEAD") == 0) {
        if (git_commit_lookup(&parent, repo, &parent_id) == 0) {
            parents[0] = parent;
            parent_count = 1;
        }
    }

    git_oid commit_id;
    error = git_commit_create(
        &commit_id,
        repo,
        "HEAD",
        sig,
        sig,
        NULL,
        commitMessage.c_str(),
        tree,
        parent_count,
        parents
    );

    if (parent) git_commit_free(parent);
    git_signature_free(sig);
    git_tree_free(tree);
    git_repository_free(repo);

    return error == 0;
}
