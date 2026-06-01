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
#include <QDir>
#include <iostream>
#include <fstream>
#include <string.h>
#include <ctime>
#include <retroshare/rsinit.h>
#include <algorithm>

static std::string normalizePath(const std::string& path)
{
    std::string norm = path;
    std::replace(norm.begin(), norm.end(), '\\', '/');
    return norm;
}


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

bool GitManager::initRepository(const std::string& repoPath, bool isBare)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_init(&repo, normPath.c_str(), isBare ? 1 : 0);
    
    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "Error " << error << " initRepository: " << (e ? e->message : "Unknown") << std::endl;
        return false;
    }
    
    git_repository_free(repo);
    return true;
}
 
bool GitManager::isValidRepository(const std::string& repoPath)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
    if (error == 0) {
        git_repository_free(repo);
        return true;
    }
    return false;
}
 
std::string GitManager::getBareRepoPath(const std::string& groupId)
{
    return RsAccounts::AccountDirectory() + "/retrogit_repos/" + groupId + ".git";
}
 
bool GitManager::unpackPackfile(const std::string& repoPath, const std::string& packfileData, const std::map<std::string, std::string>& refUpdates)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
    if (error < 0) {
        std::cout << "unpackPackfile: Repository not found. Initializing bare repo at: " << normPath << std::endl;
        if (!initRepository(normPath)) {
            std::cerr << "unpackPackfile failed to initialize bare repository" << std::endl;
            return false;
        }
        error = git_repository_open(&repo, normPath.c_str());
        if (error < 0) {
            std::cerr << "unpackPackfile failed to open repo after initialization" << std::endl;
            return false;
        }
    }

    // Index the incoming packfile data directly from memory
    git_indexer *idx = NULL;
    std::string packDir = repoPath + "/objects/pack";
    
    // Ensure the pack directory exists, otherwise git_indexer_new fails
    QDir().mkpath(QString::fromStdString(packDir));
    
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
        git_repository_free(repo);
        return false;
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
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
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
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
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
        // Fallback: try to push refs/heads/master or refs/heads/main if HEAD is not resolved
        git_oid branch_oid;
        if (git_reference_name_to_id(&branch_oid, repo, "refs/heads/master") == 0) {
            git_revwalk_push(walker, &branch_oid);
        } else if (git_reference_name_to_id(&branch_oid, repo, "refs/heads/main") == 0) {
            git_revwalk_push(walker, &branch_oid);
        } else {
            git_revwalk_free(walker);
            git_repository_free(repo);
            return true;
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
            info.full_hash = std::string(oid_str);
            
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
    std::string normBare = normalizePath(bareRepoPath);
    std::string normLocal = normalizePath(localPath);
    git_repository *cloned_repo = nullptr;
    git_clone_options clone_opts = GIT_CLONE_OPTIONS_INIT;
    clone_opts.bare = 0;
    
    int error = git_clone(&cloned_repo, normBare.c_str(), normLocal.c_str(), &clone_opts);
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
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    git_oid head_oid;
    if (git_reference_name_to_id(&head_oid, repo, "HEAD") != 0) {
        // Fallback: try to resolve refs/heads/master or refs/heads/main if HEAD is not resolved
        if (git_reference_name_to_id(&head_oid, repo, "refs/heads/master") != 0) {
            if (git_reference_name_to_id(&head_oid, repo, "refs/heads/main") != 0) {
                git_repository_free(repo);
                return true;
            }
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
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
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
    error = git_signature_new(&sig, authorName.c_str(), authorEmail.c_str(), time(NULL), 0);
    if (error < 0) {
        git_tree_free(tree);
        git_repository_free(repo);
        return false;
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

bool GitManager::getCommitDetails(const std::string& repoPath, const std::string& commitHash,
                                 std::string& authorName, std::string& authorEmail,
                                 std::string& summary, std::string& body,
                                 std::string& date, std::vector<std::string>& changedFiles)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_oid oid;
    if (git_oid_fromstr(&oid, commitHash.c_str()) != 0) {
        git_repository_free(repo);
        return false;
    }

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &oid) != 0) {
        git_repository_free(repo);
        return false;
    }

    // Author info
    const git_signature *sig = git_commit_author(commit);
    if (sig) {
        authorName = sig->name ? sig->name : "";
        authorEmail = sig->email ? sig->email : "";
        
        std::time_t t = sig->when.time;
        char buf[64];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", std::localtime(&t));
        date = buf;
    }

    // Message
    const char *sum_ptr = git_commit_summary(commit);
    summary = sum_ptr ? sum_ptr : "";

    const char *body_ptr = git_commit_body(commit);
    body = body_ptr ? body_ptr : "";

    // Changed files (Diff with parent)
    git_tree *commit_tree = nullptr;
    if (git_commit_tree(&commit_tree, commit) == 0) {
        git_tree *parent_tree = nullptr;
        unsigned int parent_count = git_commit_parentcount(commit);
        if (parent_count > 0) {
            git_commit *parent = nullptr;
            if (git_commit_parent(&parent, commit, 0) == 0) {
                git_commit_tree(&parent_tree, parent);
                git_commit_free(parent);
            }
        }

        git_diff *diff = nullptr;
        if (git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, nullptr) == 0) {
            size_t num_deltas = git_diff_num_deltas(diff);
            for (size_t i = 0; i < num_deltas; ++i) {
                const git_diff_delta *delta = git_diff_get_delta(diff, i);
                if (delta) {
                    std::string path = delta->new_file.path ? delta->new_file.path : "";
                    if (path.empty() && delta->old_file.path) {
                        path = delta->old_file.path;
                    }
                    if (!path.empty()) {
                        changedFiles.push_back(path);
                    }
                }
            }
            git_diff_free(diff);
        }

        if (parent_tree) {
            git_tree_free(parent_tree);
        }
        git_tree_free(commit_tree);
    }

    git_commit_free(commit);
    git_repository_free(repo);
    return true;
}

struct DiffCallbackPayload {
    std::vector<GitDiffLine> *lines;
};

static int file_diff_line_cb(const git_diff_delta *delta, const git_diff_hunk *hunk, const git_diff_line *line, void *payload)
{
    (void)delta;
    (void)hunk;
    DiffCallbackPayload *p = static_cast<DiffCallbackPayload*>(payload);
    GitDiffLine diffLine;
    diffLine.origin = line->origin;
    diffLine.text = std::string(line->content, line->content_len);
    p->lines->push_back(diffLine);
    return 0;
}

bool GitManager::getFileDiff(const std::string& repoPath, const std::string& commitHash,
                            const std::string& relativePath, std::vector<GitDiffLine>& diffLines)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_diff *diff = nullptr;
    bool success = false;
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    char *pathspec_arr[1];
    if (!relativePath.empty()) {
        pathspec_arr[0] = const_cast<char*>(relativePath.c_str());
        opts.pathspec.strings = pathspec_arr;
        opts.pathspec.count = 1;
    }

    if (commitHash == "LOCAL_CHANGES") {
        git_tree *head_tree = nullptr;
        git_oid head_oid;
        bool has_head = false;
        if (git_reference_name_to_id(&head_oid, repo, "HEAD") == 0) {
            has_head = true;
        } else if (git_reference_name_to_id(&head_oid, repo, "refs/heads/master") == 0) {
            has_head = true;
        } else if (git_reference_name_to_id(&head_oid, repo, "refs/heads/main") == 0) {
            has_head = true;
        }

        if (has_head) {
            git_commit *commit = nullptr;
            if (git_commit_lookup(&commit, repo, &head_oid) == 0) {
                git_commit_tree(&head_tree, commit);
                git_commit_free(commit);
            }
        }

        if (git_diff_tree_to_workdir_with_index(&diff, repo, head_tree, &opts) == 0) {
            DiffCallbackPayload payload;
            payload.lines = &diffLines;
            if (git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, file_diff_line_cb, &payload) == 0) {
                success = true;
            }
            git_diff_free(diff);
        }

        if (head_tree) {
            git_tree_free(head_tree);
        }
    } else {
        git_oid oid;
        if (git_oid_fromstr(&oid, commitHash.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }

        git_commit *commit = nullptr;
        if (git_commit_lookup(&commit, repo, &oid) != 0) {
            git_repository_free(repo);
            return false;
        }

        git_tree *commit_tree = nullptr;
        if (git_commit_tree(&commit_tree, commit) != 0) {
            git_commit_free(commit);
            git_repository_free(repo);
            return false;
        }

        git_tree *parent_tree = nullptr;
        unsigned int parent_count = git_commit_parentcount(commit);
        if (parent_count > 0) {
            git_commit *parent = nullptr;
            if (git_commit_parent(&parent, commit, 0) == 0) {
                git_commit_tree(&parent_tree, parent);
                git_commit_free(parent);
            }
        }

        if (git_diff_tree_to_tree(&diff, repo, parent_tree, commit_tree, &opts) == 0) {
            DiffCallbackPayload payload;
            payload.lines = &diffLines;

            if (git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, file_diff_line_cb, &payload) == 0) {
                success = true;
            }
            git_diff_free(diff);
        }

        if (parent_tree) {
            git_tree_free(parent_tree);
        }
        git_tree_free(commit_tree);
        git_commit_free(commit);
    }

    git_repository_free(repo);
    return success;
}

bool GitManager::getLocalChanges(const std::string& repoPath, std::vector<GitLocalChange>& changes)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    int open_err = git_repository_open(&repo, normPath.c_str());
    if (open_err != 0) {
        const git_error *e = git_error_last();
        std::cerr << "RetroGit debug: getLocalChanges failed to open repository at: " << normPath
                  << " Error: " << open_err << " / " << (e ? e->message : "None") << std::endl;
        return false;
    }

    if (git_repository_is_bare(repo)) {
        std::cerr << "RetroGit debug: getLocalChanges - repository is BARE: " << repoPath << std::endl;
        git_repository_free(repo);
        return false;
    }

    git_status_list *status_list = nullptr;
    git_status_options opts = GIT_STATUS_OPTIONS_INIT;
    opts.show = GIT_STATUS_SHOW_INDEX_AND_WORKDIR;
    opts.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS | GIT_STATUS_OPT_RENAMES_HEAD_TO_INDEX;

    bool success = false;
    int status_err = git_status_list_new(&status_list, repo, &opts);
    if (status_err == 0) {
        success = true;
        size_t count = git_status_list_entrycount(status_list);
        std::cerr << "RetroGit debug: getLocalChanges status list count: " << count << " for path: " << repoPath << std::endl;
        for (size_t i = 0; i < count; ++i) {
            const git_status_entry *entry = git_status_byindex(status_list, i);
            if (!entry) continue;

            char status_char = ' ';
            std::string color_hex = "#000000";
            unsigned int status = entry->status;

            if (status & GIT_STATUS_IGNORED) {
                continue;
            }

            if (status & GIT_STATUS_INDEX_NEW) {
                status_char = '+';
                color_hex = "#d35400"; // Orange
            } else if (status & GIT_STATUS_WT_NEW) {
                status_char = '?';
                color_hex = "#7f8c8d"; // Grey
            } else if (status & (GIT_STATUS_INDEX_DELETED | GIT_STATUS_WT_DELETED)) {
                status_char = '-';
                color_hex = "#27ae60"; // Green/Olive
            } else if (status & (GIT_STATUS_INDEX_MODIFIED | GIT_STATUS_WT_MODIFIED | GIT_STATUS_INDEX_RENAMED | GIT_STATUS_WT_RENAMED)) {
                status_char = '~';
                color_hex = "#2980b9"; // Blue
            } else {
                continue;
            }

            const char *path = nullptr;
            if (entry->head_to_index) {
                if (entry->head_to_index->new_file.path) {
                    path = entry->head_to_index->new_file.path;
                } else if (entry->head_to_index->old_file.path) {
                    path = entry->head_to_index->old_file.path;
                }
            }
            if (!path && entry->index_to_workdir) {
                if (entry->index_to_workdir->new_file.path) {
                    path = entry->index_to_workdir->new_file.path;
                } else if (entry->index_to_workdir->old_file.path) {
                    path = entry->index_to_workdir->old_file.path;
                }
            }

            if (path) {
                GitLocalChange change;
                change.path = path;
                change.status = status_char;
                change.color_hex = color_hex;
                changes.push_back(change);
            }
        }
        git_status_list_free(status_list);
    }

    git_repository_free(repo);
    return success;
}

bool GitManager::extractFile(const std::string& repoPath, const std::string& relativePath, const std::string& destPath)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    git_oid head_oid;
    if (git_reference_name_to_id(&head_oid, repo, "HEAD") != 0) {
        if (git_reference_name_to_id(&head_oid, repo, "refs/heads/master") != 0) {
            if (git_reference_name_to_id(&head_oid, repo, "refs/heads/main") != 0) {
                git_repository_free(repo);
                return false;
            }
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

    git_tree_entry *entry = nullptr;
    if (git_tree_entry_bypath(&entry, tree, relativePath.c_str()) != 0) {
        git_tree_free(tree);
        git_commit_free(commit);
        git_repository_free(repo);
        return false;
    }

    git_object *obj = nullptr;
    if (git_tree_entry_to_object(&obj, repo, entry) != 0) {
        git_tree_entry_free(entry);
        git_tree_free(tree);
        git_commit_free(commit);
        git_repository_free(repo);
        return false;
    }

    git_blob *blob = (git_blob*)obj;
    const void *content = git_blob_rawcontent(blob);
    size_t size = git_blob_rawsize(blob);

    // Write to destPath
    std::ofstream outfile(destPath.c_str(), std::ios::binary);
    if (!outfile) {
        git_object_free(obj);
        git_tree_entry_free(entry);
        git_tree_free(tree);
        git_commit_free(commit);
        git_repository_free(repo);
        return false;
    }

    outfile.write((const char*)content, size);
    outfile.close();

    git_object_free(obj);
    git_tree_entry_free(entry);
    git_tree_free(tree);
    git_commit_free(commit);
    git_repository_free(repo);
    return true;
}

bool GitManager::pullRepository(const std::string& localPath)
{
    std::string normPath = normalizePath(localPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_remote *remote = nullptr;
    if (git_remote_lookup(&remote, repo, "origin") != 0) {
        git_repository_free(repo);
        return false;
    }

    // Fetch from origin remote
    git_fetch_options fetch_opts = GIT_FETCH_OPTIONS_INIT;
    if (git_remote_fetch(remote, nullptr, &fetch_opts, nullptr) != 0) {
        const git_error *e = git_error_last();
        if (e) {
            std::cerr << "Git remote fetch failed: " << e->message << std::endl;
        }
        git_remote_free(remote);
        git_repository_free(repo);
        return false;
    }
    git_remote_free(remote);

    // Resolve current active branch reference name
    std::string remoteBranch = "refs/remotes/origin/master"; // default fallback
    git_reference *head_ref = nullptr;
    if (git_reference_lookup(&head_ref, repo, "HEAD") == 0) {
        if (git_reference_type(head_ref) == GIT_REF_SYMBOLIC) {
            const char *target = git_reference_symbolic_target(head_ref);
            if (target) {
                std::string targetStr(target);
                if (targetStr.rfind("refs/heads/", 0) == 0) {
                    remoteBranch = "refs/remotes/origin/" + targetStr.substr(11);
                }
            }
        }
        git_reference_free(head_ref);
    }

    git_reference *remote_ref = nullptr;
    int error = git_reference_lookup(&remote_ref, repo, remoteBranch.c_str());
    if (error != 0) {
        error = git_reference_lookup(&remote_ref, repo, "refs/remotes/origin/master");
        if (error != 0) {
            error = git_reference_lookup(&remote_ref, repo, "refs/remotes/origin/main");
        }
    }

    if (error != 0) {
        std::cerr << "Failed to find remote tracking branch for pull reset." << std::endl;
        git_repository_free(repo);
        return false;
    }

    git_object *target_commit = nullptr;
    if (git_reference_peel(&target_commit, remote_ref, GIT_OBJECT_COMMIT) != 0) {
        git_reference_free(remote_ref);
        git_repository_free(repo);
        return false;
    }
    git_reference_free(remote_ref);

    // Perform hard reset to force update the working directory and index
    git_checkout_options checkout_opts = GIT_CHECKOUT_OPTIONS_INIT;
    checkout_opts.checkout_strategy = GIT_CHECKOUT_FORCE;

    if (git_reset(repo, target_commit, GIT_RESET_HARD, &checkout_opts) != 0) {
        const git_error *e = git_error_last();
        if (e) {
            std::cerr << "Git reset hard failed: " << e->message << std::endl;
        }
        git_object_free(target_commit);
        git_repository_free(repo);
        return false;
    }

    git_object_free(target_commit);
    git_repository_free(repo);
    return true;
}
