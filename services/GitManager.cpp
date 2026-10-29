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
#include <filesystem>
#include <functional>
#include <mutex>
#include <string.h>
#include <ctime>
#include <retroshare/rsinit.h>
#include <algorithm>

namespace fs = std::filesystem;

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

bool GitManager::initRepository(const std::string& repoPath, bool isBare, std::string* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_init(&repo, normPath.c_str(), isBare ? 1 : 0);
    
    if (error < 0) {
        const git_error *e = git_error_last();
        if (errorMessage) *errorMessage = e ? e->message : "Unknown error";
        std::cerr << "Error " << error << " initRepository: " << (e ? e->message : "Unknown") << std::endl;
        return false;
    }
    
    git_repository_free(repo);
    return true;
}
 
bool GitManager::isValidRepository(const std::string& repoPath, std::string* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
    if (error == 0) {
        git_repository_free(repo);
        return true;
    }
    const git_error *e = git_error_last();
    if (errorMessage) *errorMessage = e ? e->message : "Unknown error";
    return false;
}
 
std::string GitManager::getBareRepoPath(const std::string& groupId)
{
    return RsAccounts::AccountDirectory() + "/retrogit_repos/" + groupId + ".git";
}
 
namespace {
std::string oidText(const git_oid *oid) {
    char hex[GIT_OID_HEXSZ + 1]; git_oid_tostr(hex, sizeof(hex), oid); return hex;
}

// std::localtime() returns NULL for dates it cannot represent (on Windows any
// time before 1970 or after 3000). Git allows such dates and a remote peer can
// publish them, so never pass the result to strftime() unchecked.
std::string formatGitTime(git_time_t when)
{
    static std::mutex localtimeMutex; // std::localtime uses a shared static buffer
    const std::time_t t = static_cast<std::time_t>(when);
    if (static_cast<git_time_t>(t) != when) return std::string();
    std::tm copy;
    {
        std::lock_guard<std::mutex> lock(localtimeMutex);
        const std::tm *tmv = std::localtime(&t);
        if (!tmv) return std::string();
        copy = *tmv;
    }
    char buf[64];
    if (std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &copy) == 0) return std::string();
    return buf;
}

// Targets of all existing references. RetroGit only creates a reference after
// verifying that its full history is present, and git guarantees the same for
// working repositories, so history below these objects is known to be complete.
std::set<std::string> refTargets(git_repository *repo)
{
    std::set<std::string> tips;
    git_reference_iterator *iter = nullptr;
    if (git_reference_iterator_new(&iter, repo) != 0) return tips;
    git_reference *ref = nullptr;
    while (git_reference_next(&ref, iter) == 0) {
        if (const git_oid *id = git_reference_target(ref)) tips.insert(oidText(id));
        git_reference_free(ref);
    }
    git_reference_iterator_free(iter);
    return tips;
}

// Enumerate the object closure of tip (trees, blobs, tags and commit parents),
// stopping at objects listed in 'complete' whose history is already known to be
// present. Gitlinks name commits in another repository and are excluded.
bool collectObjects(git_repository *repo, const git_oid& tip, std::set<std::string>& objects,
                    const std::set<std::string> *complete = nullptr) {
    std::vector<git_oid> pending{tip};
    while (!pending.empty()) {
        git_oid id = pending.back(); pending.pop_back();
        const std::string hex = oidText(&id);
        if (complete && complete->count(hex)) continue;
        if (!objects.insert(hex).second) continue;
        git_object *object = nullptr;
        if (git_object_lookup(&object, repo, &id, GIT_OBJECT_ANY) != 0) return false;
        switch (git_object_type(object)) {
        case GIT_OBJECT_COMMIT: {
            auto *commit = reinterpret_cast<git_commit*>(object);
            pending.push_back(*git_commit_tree_id(commit));
            for (unsigned i = 0; i < git_commit_parentcount(commit); ++i)
                pending.push_back(*git_commit_parent_id(commit, i));
            break;
        }
        case GIT_OBJECT_TREE: {
            auto *tree = reinterpret_cast<git_tree*>(object);
            for (size_t i = 0; i < git_tree_entrycount(tree); ++i) {
                const auto *entry = git_tree_entry_byindex(tree, i);
                if (git_tree_entry_type(entry) != GIT_OBJECT_COMMIT) pending.push_back(*git_tree_entry_id(entry));
            }
            break;
        }
        case GIT_OBJECT_TAG: pending.push_back(*git_tag_target_id(reinterpret_cast<git_tag*>(object))); break;
        default: break;
        }
        git_object_free(object);
    }
    return true;
}

bool isPublishableRef(const std::string& name)
{
    return name.size() < 1024 &&
        (name.rfind("refs/heads/", 0) == 0 || name.rfind("refs/tags/", 0) == 0);
}

// Feed a pack into the repository's object database.
bool indexPack(git_repository *repo, const std::function<int(git_indexer*, git_indexer_progress*)>& feed)
{
    const std::string packDir = std::string(git_repository_path(repo)) + "objects/pack";
    std::error_code ec;
    fs::create_directories(fs::u8path(packDir), ec); // git_indexer_new fails without it
    git_indexer *idx = nullptr;
    int error = git_indexer_new(&idx, packDir.c_str(), 0, NULL, NULL);
    if (error == 0) {
        git_indexer_progress stats;
        memset(&stats, 0, sizeof(stats));
        error = feed(idx, &stats);
        if (error == 0) error = git_indexer_commit(idx, &stats);
        git_indexer_free(idx);
    }
    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "Error indexing packfile: " << (e ? e->message : "Unknown") << std::endl;
    }
    return error == 0;
}

// Point references at their published commits once their history is complete.
bool applyRefs(git_repository *repo, const std::map<std::string, std::string>& refUpdates)
{
    // Delta packs may arrive before their prerequisites. Retain objects, but
    // never expose incomplete history through branch references.
    const std::set<std::string> complete = refTargets(repo);
    std::set<std::string> visited;
    for (const auto& update : refUpdates) {
        if (!isPublishableRef(update.first)) continue;
        git_oid id;
        if (update.second.size() != GIT_OID_HEXSZ || git_oid_fromstr(&id, update.second.c_str()) != 0 ||
            !collectObjects(repo, id, visited, &complete))
            return false;
    }
    for (const auto& update : refUpdates) {
        if (!isPublishableRef(update.first)) {
            // Only branches and tags may be published; never HEAD, remotes or review refs.
            std::cerr << "RetroGit: ignoring update of non-branch reference " << update.first << std::endl;
            continue;
        }
        git_oid oid;
        if (git_oid_fromstr(&oid, update.second.c_str()) != 0) continue;
        git_reference *ref = NULL;
        if (git_reference_create(&ref, repo, update.first.c_str(), &oid, 1, "RetroGit network sync") < 0) {
            const git_error *e = git_error_last();
            std::cerr << "RetroGit: git_reference_create failed: " << (e ? e->message : "Unknown error") << std::endl;
        }
        git_reference_free(ref);
        // Symbolically point HEAD to this branch if it is refs/heads/master or refs/heads/main
        if (update.first == "refs/heads/master" || update.first == "refs/heads/main") {
            git_reference *head_ref = NULL;
            git_reference_symbolic_create(&head_ref, repo, "HEAD", update.first.c_str(), 1, "RetroGit set HEAD");
            git_reference_free(head_ref);
        }
    }
    return true;
}

git_repository *openOrInitRepository(const std::string& normPath)
{
    git_repository *repo = NULL;
    if (git_repository_open(&repo, normPath.c_str()) == 0) return repo;
    std::cout << "RetroGit: Repository not found. Initializing bare repo at: " << normPath << std::endl;
    if (git_repository_init(&repo, normPath.c_str(), 1) != 0) {
        std::cerr << "RetroGit: failed to initialize bare repository " << normPath << std::endl;
        return NULL;
    }
    return repo;
}

// Reject absolute paths and '..' components coming from repository trees.
bool isSafeRelativePath(const std::string& path)
{
    if (path.empty() || path[0] == '/' || path[0] == '\\' || path.find(':') != std::string::npos) return false;
    size_t start = 0;
    while (start <= path.size()) {
        size_t end = path.find_first_of("/\\", start);
        if (end == std::string::npos) end = path.size();
        const std::string part = path.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}
}

std::string GitManager::formatTime(int64_t when) { return formatGitTime(static_cast<git_time_t>(when)); }

bool GitManager::isSafeRelativePath(const std::string& path) { return ::isSafeRelativePath(path); }

bool GitManager::hasCommit(const std::string& path, const std::string& commit) {
    git_repository *repo = nullptr; git_oid id; git_commit *object = nullptr;
    if (commit.size() != GIT_OID_HEXSZ || git_oid_fromstr(&id, commit.c_str()) != 0 ||
        git_repository_open(&repo, normalizePath(path).c_str()) != 0) return false;
    const bool ok = git_commit_lookup(&object, repo, &id) == 0;
    git_commit_free(object); git_repository_free(repo); return ok;
}

std::set<std::string> GitManager::knownTips(const std::string& path) {
    // References are only created once their history is complete, so their
    // targets can be advertised without re-walking the whole history.
    std::set<std::string> tips;
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(path).c_str()) != 0) return tips;
    git_reference_iterator *iter = nullptr;
    if (git_reference_iterator_new(&iter, repo) == 0) {
        git_reference *ref = nullptr;
        while (git_reference_next(&ref, iter) == 0) {
            const std::string name = git_reference_name(ref);
            const git_oid *id = git_reference_target(ref);
            if (id && (name.rfind("refs/heads/", 0) == 0 || name.rfind("refs/tags/", 0) == 0))
                tips.insert(oidText(id));
            git_reference_free(ref);
        }
        git_reference_iterator_free(iter);
    }
    git_repository_free(repo); return tips;
}

std::string GitManager::headCommit(const std::string& path)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(path).c_str()) != 0) return std::string();
    git_oid id;
    std::string result;
    if (git_reference_name_to_id(&id, repo, "HEAD") == 0) result = oidText(&id);
    git_repository_free(repo);
    return result;
}

std::set<std::string> GitManager::filterExistingCommits(const std::string& path, const std::set<std::string>& candidates)
{
    std::set<std::string> present;
    git_repository *repo = nullptr;
    if (candidates.empty() || git_repository_open(&repo, normalizePath(path).c_str()) != 0) return present;
    git_odb *odb = nullptr;
    if (git_repository_odb(&odb, repo) == 0) {
        for (const auto& hex : candidates) {
            git_oid id;
            if (hex.size() == GIT_OID_HEXSZ && git_oid_fromstr(&id, hex.c_str()) == 0 && git_odb_exists(odb, &id))
                present.insert(hex);
        }
        git_odb_free(odb);
    }
    git_repository_free(repo);
    return present;
}

bool GitManager::unpackPackfile(const std::string& repoPath, const std::string& packfileData, const std::map<std::string, std::string>& refUpdates)
{
    git_repository *repo = openOrInitRepository(normalizePath(repoPath));
    if (!repo) return false;
    // An empty pack means every object was already published before.
    bool ok = packfileData.empty() || indexPack(repo, [&](git_indexer *idx, git_indexer_progress *stats) {
        return git_indexer_append(idx, packfileData.data(), packfileData.size(), stats);
    });
    if (ok) ok = applyRefs(repo, refUpdates);
    git_repository_free(repo);
    return ok;
}

bool GitManager::applyRefUpdates(const std::string& repoPath, const std::map<std::string, std::string>& refUpdates)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0) return false;
    const bool ok = applyRefs(repo, refUpdates);
    git_repository_free(repo);
    return ok;
}

bool GitManager::unpackPackfileFromFile(const std::string& repoPath, const std::string& packfilePath, const std::map<std::string, std::string>& refUpdates)
{
    std::ifstream file(fs::u8path(packfilePath), std::ios::binary);
    if (!file) {
        std::cerr << "unpackPackfileFromFile failed to open file: " << packfilePath << std::endl;
        return false;
    }
    git_repository *repo = openOrInitRepository(normalizePath(repoPath));
    if (!repo) return false;
    // Stream the file in blocks instead of loading the whole pack into memory.
    bool ok = indexPack(repo, [&](git_indexer *idx, git_indexer_progress *stats) {
        std::vector<char> buffer(1024 * 1024);
        while (file) {
            file.read(buffer.data(), buffer.size());
            const std::streamsize got = file.gcount();
            if (got > 0) {
                const int error = git_indexer_append(idx, buffer.data(), static_cast<size_t>(got), stats);
                if (error != 0) return error;
            }
        }
        return file.bad() ? -1 : 0;
    });
    if (ok) ok = applyRefs(repo, refUpdates);
    git_repository_free(repo);
    return ok;
}

static int packbuilder_callback(void *buf, size_t size, void *payload) {
    std::string *outStr = static_cast<std::string*>(payload);
    outStr->append(static_cast<const char*>(buf), size);
    return 0;
}

static bool buildObjectPackOnce(git_repository *repo, const std::map<std::string, std::string>& refs,
                                const std::set<std::string>& known, std::string& data)
{
    data.clear();
    git_packbuilder *builder = nullptr;
    git_revwalk *walk = nullptr;
    if (git_packbuilder_new(&builder, repo) != 0) return false;
    if (git_revwalk_new(&walk, repo) != 0) { git_packbuilder_free(builder); return false; }
    bool ok = true, pushed = false;
    for (const auto& ref : refs) {
        git_oid cur;
        if (git_oid_fromstr(&cur, ref.second.c_str()) != 0) { ok = false; break; }
        // Ship annotated tag objects, then the object they finally point to.
        git_object_t type = GIT_OBJECT_INVALID;
        for (int depth = 0; ok && depth < 16; ++depth) {
            git_object *obj = nullptr;
            if (git_object_lookup(&obj, repo, &cur, GIT_OBJECT_ANY) != 0) { ok = false; break; }
            type = git_object_type(obj);
            if (type != GIT_OBJECT_TAG) { git_object_free(obj); break; }
            const git_oid target = *git_tag_target_id(reinterpret_cast<git_tag*>(obj));
            git_object_free(obj);
            // Tag objects the receiver already has are not sent again.
            if (!known.count(oidText(&cur)) && git_packbuilder_insert(builder, &cur, nullptr) != 0) { ok = false; break; }
            cur = target;
        }
        if (!ok) break;
        if (type == GIT_OBJECT_COMMIT) {
            if (git_revwalk_push(walk, &cur) != 0) { ok = false; break; }
            pushed = true;
        } else if (type == GIT_OBJECT_TREE || type == GIT_OBJECT_BLOB) {
            if (git_packbuilder_insert_recur(builder, &cur, nullptr) != 0) { ok = false; break; }
        } else { ok = false; break; }
    }
    // Commits the receiver already has: everything reachable from them is omitted.
    // Unknown baselines cannot justify omitting any objects, so they are skipped.
    for (const auto& tip : known) {
        if (!ok) break;
        git_oid id;
        git_object *obj = nullptr, *peeled = nullptr;
        if (tip.size() != GIT_OID_HEXSZ || git_oid_fromstr(&id, tip.c_str()) != 0 ||
            git_object_lookup(&obj, repo, &id, GIT_OBJECT_ANY) != 0) continue;
        if (git_object_peel(&peeled, obj, GIT_OBJECT_COMMIT) == 0) {
            git_revwalk_hide(walk, git_object_id(peeled));
            git_object_free(peeled);
        }
        git_object_free(obj);
    }
    if (ok && pushed) ok = git_packbuilder_insert_walk(builder, walk) == 0;
    if (ok && git_packbuilder_object_count(builder) > 0)
        ok = git_packbuilder_foreach(builder, packbuilder_callback, &data) == 0;
    git_revwalk_free(walk);
    git_packbuilder_free(builder);
    if (!ok) data.clear();
    return ok;
}

static bool buildObjectPack(git_repository *repo, const std::map<std::string, std::string>& refs,
                            const std::set<std::string>& known, std::string& data)
{
    if (buildObjectPackOnce(repo, refs, known, data)) return true;
    // A baseline whose history is incomplete locally makes the walk fail;
    // fall back to a self-contained pack instead of failing the transfer.
    return !known.empty() && buildObjectPackOnce(repo, refs, {}, data);
}

bool GitManager::createPackfile(const std::string& repoPath, std::string& outPackfileData,
    std::map<std::string, std::string>& outRefUpdates, const std::set<std::string>& known)
{
    outRefUpdates.clear();
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0) return false;
    git_reference_iterator *iter = nullptr;
    bool ok = git_reference_iterator_new(&iter, repo) == 0;
    if (ok) {
        git_reference *ref = nullptr;
        while (git_reference_next(&ref, iter) == 0) {
            const std::string name = git_reference_name(ref);
            const git_oid *id = git_reference_target(ref);
            if (id && (name.rfind("refs/heads/", 0) == 0 || name.rfind("refs/tags/", 0) == 0))
                outRefUpdates[name] = oidText(id);
            git_reference_free(ref);
        }
        git_reference_iterator_free(iter);
        ok = buildObjectPack(repo, outRefUpdates, known, outPackfileData);
    }
    git_repository_free(repo); return ok;
}

bool GitManager::createPackfileForRef(const std::string& repoPath, const std::string& refName,
    std::string& outPackfileData, std::map<std::string, std::string>& outRefUpdates, const std::set<std::string>& known)
{
    outRefUpdates.clear();
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0) return false;
    const std::string ref = refName.rfind("refs/", 0) == 0 ? refName : "refs/heads/" + refName;
    git_oid id;
    bool ok = git_reference_name_to_id(&id, repo, ref.c_str()) == 0;
    if (ok) { outRefUpdates[ref] = oidText(&id); ok = buildObjectPack(repo, outRefUpdates, known, outPackfileData); }
    git_repository_free(repo); return ok;
}

bool GitManager::getCommitLog(const std::string &repoPath, std::vector<GitCommitInfo> &commits, const std::string& branchOrTag) {
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
    
    bool pushed = false;
    if (!branchOrTag.empty()) {
        git_oid oid;
        std::string refName = branchOrTag;
        if (refName.rfind("refs/", 0) != 0) {
            if (git_reference_name_to_id(&oid, repo, ("refs/heads/" + refName).c_str()) == 0) {
                if (git_revwalk_push(walker, &oid) == 0) pushed = true;
            } else if (git_reference_name_to_id(&oid, repo, ("refs/tags/" + refName).c_str()) == 0) {
                if (git_revwalk_push(walker, &oid) == 0) pushed = true;
            } else if (git_reference_name_to_id(&oid, repo, ("refs/remotes/origin/" + refName).c_str()) == 0) {
                if (git_revwalk_push(walker, &oid) == 0) pushed = true;
            }
        } else {
            if (git_reference_name_to_id(&oid, repo, refName.c_str()) == 0) {
                if (git_revwalk_push(walker, &oid) == 0) pushed = true;
            }
        }
    }

    if (!pushed) {
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
                
                info.date = formatGitTime(sig->when.time);
            }
            
            commits.push_back(info);
            git_commit_free(commit);
        }
    }

    git_revwalk_free(walker);
    git_repository_free(repo);
    return true;
}

bool GitManager::getAllCommitShas(const std::string& repoPath, std::set<std::string>& shas)
{
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

    // Push all refs under refs/* to walk everything
    if (git_revwalk_push_glob(walker, "refs/*") != 0) {
        git_revwalk_free(walker);
        git_repository_free(repo);
        return true; // Return true as repo might be empty/has no refs yet
    }

    git_oid oid;
    char oid_str[GIT_OID_HEXSZ + 1];
    while (git_revwalk_next(&oid, walker) == 0) {
        git_oid_tostr(oid_str, sizeof(oid_str), &oid);
        shas.insert(std::string(oid_str));
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

bool GitManager::getRepoFiles(const std::string& repoPath, std::vector<std::string>& files, const std::string& refName)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    git_oid target_oid;
    std::string targetRef = refName.empty() ? "HEAD" : refName;

    // First try to resolve reference name directly (like refs/heads/branch or refs/tags/tag or HEAD)
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        // If it was a short branch/tag name, try refs/heads/ and refs/tags/
        if (git_reference_name_to_id(&target_oid, repo, ("refs/heads/" + targetRef).c_str()) != 0) {
            if (git_reference_name_to_id(&target_oid, repo, ("refs/tags/" + targetRef).c_str()) != 0) {
                // Fallback to HEAD
                if (git_reference_name_to_id(&target_oid, repo, "HEAD") != 0) {
                    if (git_reference_name_to_id(&target_oid, repo, "refs/heads/master") != 0) {
                        if (git_reference_name_to_id(&target_oid, repo, "refs/heads/main") != 0) {
                            git_repository_free(repo);
                            return true;
                        }
                    }
                }
            }
        }
    }

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &target_oid) != 0) {
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

bool GitManager::commitChanges(const std::string& repoPath, const std::string& commitMessage, const std::string& authorName, const std::string& authorEmail, const std::string& targetBranch)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    int error = git_repository_open(&repo, normPath.c_str());
    if (error < 0) {
        std::cerr << "commitChanges: Failed to open repo" << std::endl;
        return false;
    }

    const std::string refName = targetBranch.empty() ? "HEAD" : "refs/heads/" + targetBranch;
    git_oid parent_id;
    int parentResult = git_reference_name_to_id(&parent_id, repo, refName.c_str());
    if (parentResult == GIT_ENOTFOUND && !targetBranch.empty()) {
        // A remote branch can be materialized locally, but a missing branch must
        // never silently become a new, unrelated root commit.
        git_oid remoteOid;
        if (git_reference_name_to_id(&remoteOid, repo, ("refs/remotes/origin/" + targetBranch).c_str()) == 0) {
            git_reference *branch = nullptr;
            if (git_reference_create(&branch, repo, refName.c_str(), &remoteOid, 0, "Track branch") == 0)
                parentResult = git_reference_name_to_id(&parent_id, repo, refName.c_str());
            git_reference_free(branch);
        }
    }
    bool initialCommit = false;
    if (parentResult == GIT_ENOTFOUND && git_repository_is_empty(repo) == 1 && git_repository_head_unborn(repo) == 1) {
        git_reference *head = nullptr;
        if (git_reference_lookup(&head, repo, "HEAD") == 0) {
            const char *unborn = git_reference_symbolic_target(head);
            initialCommit = unborn && (targetBranch.empty() || refName == unborn);
            git_reference_free(head);
        }
    }
    if (parentResult != 0 && !initialCommit) {
        std::cerr << "commitChanges: Target branch is missing or unreadable; create it from an existing branch first." << std::endl;
        git_repository_free(repo);
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

    git_commit *parent = nullptr;
    const git_commit *parents[1] = { nullptr };
    if (!initialCommit) {
        if (git_commit_lookup(&parent, repo, &parent_id) != 0) {
            git_signature_free(sig);
            git_tree_free(tree);
            git_repository_free(repo);
            return false;
        }
        parents[0] = parent;
    }
    const int parent_count = initialCommit ? 0 : 1;

    git_oid commit_id;
    error = git_commit_create(
        &commit_id,
        repo,
        refName.c_str(),
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

bool GitManager::discardLocalChanges(const std::string& repoPath)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
    opts.checkout_strategy = GIT_CHECKOUT_FORCE | GIT_CHECKOUT_RECREATE_MISSING;

    int err = git_checkout_head(repo, &opts);
    git_repository_free(repo);
    return err == 0;
}

bool GitManager::checkoutBranch(const std::string& repoPath, const std::string& branchName)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    std::string refName = "refs/heads/" + branchName;
    git_reference *ref = nullptr;
    if (git_reference_lookup(&ref, repo, refName.c_str()) != 0) {
        git_repository_free(repo);
        return false;
    }
    git_reference_free(ref);

    int err = git_repository_set_head(repo, refName.c_str());
    if (err == 0) {
        git_checkout_options opts = GIT_CHECKOUT_OPTIONS_INIT;
        opts.checkout_strategy = GIT_CHECKOUT_SAFE;
        err = git_checkout_head(repo, &opts);
    }

    git_repository_free(repo);
    return err == 0;
}

bool GitManager::getCommitDetails(const std::string& repoPath, const std::string& commitHash,
                                 std::string& authorName, std::string& authorEmail,
                                 std::string& summary, std::string& body,
                                 std::string& date, std::vector<GitCommitFileChange>& changedFiles)
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
        
        date = formatGitTime(sig->when.time);
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
                        GitCommitFileChange change;
                        change.path = path;
                        change.status = '~'; // modified default
                        if (delta->status == GIT_DELTA_ADDED || delta->status == GIT_DELTA_UNTRACKED) {
                            change.status = '+';
                        } else if (delta->status == GIT_DELTA_DELETED) {
                            change.status = '-';
                        }
                        changedFiles.push_back(change);
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

bool GitManager::extractFile(const std::string& repoPath, const std::string& relativePath, const std::string& destPath, const std::string& refName)
{
    if (!::isSafeRelativePath(relativePath)) return false;
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    git_oid target_oid;
    std::string targetRef = refName.empty() ? "HEAD" : refName;

    // First try to resolve reference name directly (like refs/heads/branch or refs/tags/tag or HEAD)
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        // If it was a short branch/tag name, try refs/heads/ and refs/tags/
        if (git_reference_name_to_id(&target_oid, repo, ("refs/heads/" + targetRef).c_str()) != 0) {
            if (git_reference_name_to_id(&target_oid, repo, ("refs/tags/" + targetRef).c_str()) != 0) {
                // Fallback to HEAD
                if (git_reference_name_to_id(&target_oid, repo, "HEAD") != 0) {
                    if (git_reference_name_to_id(&target_oid, repo, "refs/heads/master") != 0) {
                        if (git_reference_name_to_id(&target_oid, repo, "refs/heads/main") != 0) {
                            git_repository_free(repo);
                            return false;
                        }
                    }
                }
            }
        }
    }

    git_commit *commit = nullptr;
    if (git_commit_lookup(&commit, repo, &target_oid) != 0) {
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

    // Only files can be extracted; directories and submodules are not blobs.
    if (git_object_type(obj) != GIT_OBJECT_BLOB) {
        git_object_free(obj);
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
    std::ofstream outfile(fs::u8path(destPath), std::ios::binary);
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
    const bool written = !outfile.fail();

    git_object_free(obj);
    git_tree_entry_free(entry);
    git_tree_free(tree);
    git_commit_free(commit);
    git_repository_free(repo);
    return written;
}

bool GitManager::pullRepository(const std::string& localPath, const std::string& sourcePath,
    std::string* errorMessage, const std::string& expectedBranch)
{
    if (errorMessage) errorMessage->clear();
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(localPath).c_str()) != 0) {
        if (errorMessage) *errorMessage = "Cannot open the working repository.";
        return false;
    }
    auto fail = [&](const char *message) { if (errorMessage) *errorMessage = message; git_repository_free(repo); return false; };
    git_reference *head = nullptr;
    std::string branch;
    if (git_repository_head(&head, repo) == 0 && git_reference_is_branch(head)) {
        branch = git_reference_shorthand(head);
    }
    git_reference_free(head);
    if (branch.empty()) {
        const std::vector<std::string> candidates = expectedBranch.empty()
            ? std::vector<std::string>{"master", "main"} : std::vector<std::string>{expectedBranch};
        for (const auto& candidate : candidates) {
            git_reference *candidateRef = nullptr;
            if (git_reference_lookup(&candidateRef, repo, ("refs/heads/" + candidate).c_str()) == 0) {
                branch = candidate;
                git_reference_free(candidateRef);
                if (git_repository_set_head(repo, ("refs/heads/" + branch).c_str()) == 0) break;
            }
            git_reference_free(candidateRef);
        }
        // Older fork clones may contain only a detached HEAD or an origin
        // remote branch. Materialize the selected local branch at HEAD.
        if (branch.empty()) {
            git_oid headId;
            if (git_reference_name_to_id(&headId, repo, "HEAD") == 0) {
                const std::string candidate = expectedBranch.empty() ? "master" : expectedBranch;
                git_reference *created = nullptr;
                if (git_reference_create(&created, repo, ("refs/heads/" + candidate).c_str(),
                                         &headId, 0, "Attach fork branch") == 0) {
                    branch = candidate;
                }
                git_reference_free(created);
            }
        }
    }
    if (branch.empty()) return fail("The local fork branch could not be detected.");
    const std::string branchRef = "refs/heads/" + branch;
    git_oid previous;
    if (git_reference_name_to_id(&previous, repo, branchRef.c_str()) != 0)
        return fail("The local fork branch has no commit yet.");
    if (!expectedBranch.empty() && branch != expectedBranch)
        return fail("The merged branch is not checked out. Switch to the target branch and pull to update its working files.");
    git_status_options statusOptions = GIT_STATUS_OPTIONS_INIT;
    statusOptions.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    git_status_list *status = nullptr;
    if (git_status_list_new(&status, repo, &statusOptions) != 0) return fail("Cannot check local changes.");
    const bool dirty = git_status_list_entrycount(status) != 0;
    git_status_list_free(status);
    if (dirty) return fail("Local changes are preserved. Commit or stash them before pulling.");
    git_remote *remote = nullptr;
    const bool localSource = !sourcePath.empty();
    int err = localSource ? git_remote_create_anonymous(&remote, repo, normalizePath(sourcePath).c_str())
                          : git_remote_lookup(&remote, repo, "origin");
    if (err != 0) return fail("No repository source is available for pulling.");
    std::string spec = "+refs/heads/" + branch + ":refs/remotes/retrogit/" + branch;
    char *specPtr = &spec[0]; git_strarray specs{&specPtr, 1};
    git_fetch_options options = GIT_FETCH_OPTIONS_INIT;
    err = git_remote_fetch(remote, &specs, &options, "RetroGit pull");
    git_remote_free(remote);
    if (err != 0) return fail("Could not fetch the current branch from the repository source.");
    git_oid target;
    if (git_reference_name_to_id(&target, repo, ("refs/remotes/retrogit/" + branch).c_str()) != 0)
        return fail("The current branch has not been published. No other branch was substituted.");
    if (git_oid_equal(&target, &previous) || git_graph_descendant_of(repo, &previous, &target) == 1) {
        git_repository_free(repo); return true; // Already current or locally ahead.
    }
    if (git_graph_descendant_of(repo, &target, &previous) != 1)
        return fail("Local and published history have diverged. Your commits are preserved; reconcile the branches before pulling.");
    git_object *object = nullptr;
    if (git_object_lookup(&object, repo, &target, GIT_OBJECT_COMMIT) != 0)
        return fail("The published commit is not available locally.");
    git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
    checkout.checkout_strategy = GIT_CHECKOUT_SAFE;
    err = git_checkout_tree(repo, object, &checkout);
    git_object_free(object);
    if (err != 0) return fail("The working files could not be updated safely. No hard reset was performed.");
    git_reference *updated = nullptr;
    err = git_reference_create_matching(&updated, repo, branchRef.c_str(), &target, 1, &previous, "RetroGit fast-forward pull");
    git_reference_free(updated);
    if (err != 0) return fail("The branch changed during the pull. Check its status before retrying.");
    git_repository_free(repo); return true;
}

bool GitManager::getBranches(const std::string& repoPath, std::vector<std::string>& branches, std::string& currentBranch)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    // 1. Get current active branch name
    currentBranch = "master"; // default fallback
    git_reference *head_ref = nullptr;
    if (git_reference_lookup(&head_ref, repo, "HEAD") == 0) {
        if (git_reference_type(head_ref) == GIT_REF_SYMBOLIC) {
            const char *target = git_reference_symbolic_target(head_ref);
            if (target) {
                std::string targetStr(target);
                if (targetStr.rfind("refs/heads/", 0) == 0) {
                    currentBranch = targetStr.substr(11);
                }
            }
        } else {
            const char *shorthand = git_reference_shorthand(head_ref);
            if (shorthand) {
                currentBranch = shorthand;
            }
        }
        git_reference_free(head_ref);
    }

    // 2. Iterate local branches
    git_branch_iterator *iter = nullptr;
    if (git_branch_iterator_new(&iter, repo, GIT_BRANCH_LOCAL) == 0) {
        git_reference *ref = nullptr;
        git_branch_t type;
        while (git_branch_next(&ref, &type, iter) == 0) {
            const char *branch_name = nullptr;
            if (git_branch_name(&branch_name, ref) == 0) {
                branches.push_back(branch_name);
            }
            git_reference_free(ref);
        }
        git_branch_iterator_free(iter);
    }

    git_repository_free(repo);
    return true;
}

bool GitManager::getTags(const std::string& repoPath, std::vector<std::string>& tags)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    git_strarray tag_names;
    if (git_tag_list(&tag_names, repo) == 0) {
        for (size_t i = 0; i < tag_names.count; ++i) {
            if (tag_names.strings[i]) {
                tags.push_back(tag_names.strings[i]);
            }
        }
        git_strarray_dispose(&tag_names);
    }

    git_repository_free(repo);
    return true;
}

bool GitManager::createBranch(const std::string& repoPath, const std::string& branchName, const std::string& sourceBranch)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        std::cerr << "createBranch: Failed to open repo" << std::endl;
        return false;
    }

    // 1. Resolve the source branch to a commit target object
    std::string sourceRef = sourceBranch;
    if (sourceRef.rfind("refs/heads/", 0) != 0) {
        sourceRef = "refs/heads/" + sourceRef;
    }

    git_reference *ref = nullptr;
    if (git_reference_lookup(&ref, repo, sourceRef.c_str()) != 0) {
        if (git_reference_lookup(&ref, repo, sourceBranch.c_str()) != 0) {
            if (!sourceBranch.empty() || git_reference_lookup(&ref, repo, "HEAD") != 0) {
                std::cerr << "createBranch: Failed to lookup source ref " << sourceBranch << std::endl;
                git_repository_free(repo);
                return false;
            }
        }
    }

    git_commit *commit = nullptr;
    if (git_reference_peel((git_object **)&commit, ref, GIT_OBJECT_COMMIT) != 0) {
        std::cerr << "createBranch: Failed to peel reference to commit" << std::endl;
        git_reference_free(ref);
        git_repository_free(repo);
        return false;
    }
    git_reference_free(ref);

    // 2. Create the new branch reference pointing to the commit
    git_reference *new_branch_ref = nullptr;
    int error = git_branch_create(&new_branch_ref, repo, branchName.c_str(), commit, 0); // 0 means do not overwrite

    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "createBranch: Failed to create branch " << branchName << ": " 
                  << (e ? e->message : "Unknown error") << std::endl;
        git_commit_free(commit);
        git_repository_free(repo);
        return false;
    }
    git_reference_free(new_branch_ref);
    git_commit_free(commit);

    // Safely check out the selected source tree before switching HEAD. A dirty
    // working tree must not be silently reassociated with a different commit.
    bool ok = true;
    if (!git_repository_is_bare(repo)) {
        git_object *object = nullptr;
        git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
        options.checkout_strategy = GIT_CHECKOUT_SAFE;
        ok = git_revparse_single(&object, repo, ("refs/heads/" + branchName).c_str()) == 0 &&
            git_checkout_tree(repo, object, &options) == 0;
        git_object_free(object);
    }
    if (ok) ok = git_repository_set_head(repo, ("refs/heads/" + branchName).c_str()) == 0;
    if (!ok) {
        git_reference *created = nullptr;
        if (git_reference_lookup(&created, repo, ("refs/heads/" + branchName).c_str()) == 0)
            git_reference_delete(created);
        git_reference_free(created);
    }
    git_repository_free(repo);
    return ok;
}


bool GitManager::mergeBranch(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) return false;

    // Resolve source commit
    std::string sourceRef = "refs/heads/" + sourceBranch;
    git_oid source_oid;
    if (git_reference_name_to_id(&source_oid, repo, sourceRef.c_str()) != 0) {
        // Try direct lookup
        if (git_reference_name_to_id(&source_oid, repo, sourceBranch.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    // Resolve target branch reference
    std::string targetRef = "refs/heads/" + targetBranch;
    git_reference *target_ref_obj = nullptr;
    if (git_reference_lookup(&target_ref_obj, repo, targetRef.c_str()) != 0) {
        git_repository_free(repo);
        return false;
    }

    // Never discard target history; compare-and-swap also protects against concurrent updates.
    const git_oid *targetOid = git_reference_target(target_ref_obj);
    if (!targetOid || (git_oid_equal(targetOid, &source_oid) == 0 &&
        git_graph_descendant_of(repo, &source_oid, targetOid) != 1)) {
        git_reference_free(target_ref_obj);
        git_repository_free(repo);
        return false;
    }
    git_reference *new_ref = nullptr;
    int error = git_reference_create_matching(&new_ref, repo, targetRef.c_str(),
        &source_oid, 1, targetOid, "Merge pull request");
    git_reference_free(target_ref_obj);

    if (error < 0) {
        const git_error *e = git_error_last();
        std::cerr << "mergeBranch failed: " << (e ? e->message : "Unknown error") << std::endl;
        git_repository_free(repo);
        return false;
    }

    git_reference_free(new_ref);
    git_repository_free(repo);
    return true;
}

bool GitManager::rebaseBranch(const std::string& repoPath, const std::string& upstreamPath,
    const std::string& branchName, const std::string& upstreamBranch, std::string* errorMessage)
{
    if (errorMessage) errorMessage->clear();
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0) {
        if (errorMessage) *errorMessage = "Cannot open the fork repository.";
        return false;
    }
    auto fail = [&](const char *message) { if (errorMessage) *errorMessage = message; git_repository_free(repo); return false; };
    git_status_options status = GIT_STATUS_OPTIONS_INIT;
    status.flags = GIT_STATUS_OPT_INCLUDE_UNTRACKED | GIT_STATUS_OPT_RECURSE_UNTRACKED_DIRS;
    git_status_list *changes = nullptr;
    if (git_status_list_new(&changes, repo, &status) != 0 || git_status_list_entrycount(changes) != 0) {
        git_status_list_free(changes); return fail("Commit or stash local changes before rebasing.");
    }
    git_status_list_free(changes);
    git_reference *head = nullptr;
    bool checkedOut = git_repository_head(&head, repo) == 0 && git_reference_is_branch(head) &&
        branchName == git_reference_shorthand(head);
    git_reference_free(head);
    // Some older forks were cloned with a detached HEAD. If the requested
    // branch points at that HEAD, attach HEAD to it before rebasing.
    if (!checkedOut) {
        git_oid branchId;
        if (git_reference_name_to_id(&branchId, repo, ("refs/heads/" + branchName).c_str()) != 0) {
            if (git_reference_name_to_id(&branchId, repo, "HEAD") != 0)
                return fail("The fork branch could not be selected automatically.");
        }
        // Preserve a detached HEAD as the branch tip. Do not check out an
        // older branch tip over the working files.
        git_oid headId;
        if (git_reference_name_to_id(&headId, repo, "HEAD") == 0 && !git_oid_equal(&headId, &branchId))
            branchId = headId;
        git_reference *attached = nullptr;
        if (git_reference_create(&attached, repo, ("refs/heads/" + branchName).c_str(), &branchId, 1,
                                  "Attach fork branch") != 0 ||
            git_repository_set_head(repo, ("refs/heads/" + branchName).c_str()) != 0) {
            git_reference_free(attached);
            return fail("The fork branch could not be selected automatically.");
        }
        git_reference_free(attached);
        git_object *branchObject = nullptr;
        if (git_object_lookup(&branchObject, repo, &branchId, GIT_OBJECT_COMMIT) != 0) return fail("The fork branch commit is unavailable.");
        git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
        checkout.checkout_strategy = GIT_CHECKOUT_SAFE;
        const int checkoutResult = git_checkout_tree(repo, branchObject, &checkout);
        git_object_free(branchObject);
        if (checkoutResult != 0) return fail("The fork branch working files could not be checked out.");
    }
    git_remote *remote = nullptr;
    if (git_remote_create_anonymous(&remote, repo, normalizePath(upstreamPath).c_str()) != 0)
        return fail("The upstream repository is unavailable.");
    std::string spec = "+refs/heads/" + upstreamBranch + ":refs/remotes/retrogit-upstream/" + upstreamBranch;
    char *specText = &spec[0]; git_strarray specs{&specText, 1};
    git_fetch_options fetch = GIT_FETCH_OPTIONS_INIT;
    if (git_remote_fetch(remote, &specs, &fetch, "RetroGit upstream rebase") != 0) {
        git_remote_free(remote); return fail("Could not fetch the latest upstream branch.");
    }
    git_remote_free(remote);
    git_annotated_commit *branch = nullptr, *upstream = nullptr;
    const std::string upstreamRef = "refs/remotes/retrogit-upstream/" + upstreamBranch;
    bool ok = git_annotated_commit_from_revspec(&branch, repo, ("refs/heads/" + branchName).c_str()) == 0 &&
              git_annotated_commit_from_revspec(&upstream, repo, upstreamRef.c_str()) == 0;
    if (ok) {
        const git_oid *localId = git_annotated_commit_id(branch);
        const git_oid *upstreamId = git_annotated_commit_id(upstream);
        if (git_oid_equal(localId, upstreamId)) {
            git_annotated_commit_free(branch); git_annotated_commit_free(upstream);
            git_repository_free(repo); return true;
        }
        if (git_graph_descendant_of(repo, upstreamId, localId) == 1) {
            git_object *object = nullptr;
            if (git_object_lookup(&object, repo, upstreamId, GIT_OBJECT_COMMIT) != 0) ok = false;
            git_checkout_options checkout = GIT_CHECKOUT_OPTIONS_INIT;
            checkout.checkout_strategy = GIT_CHECKOUT_SAFE;
            if (ok && git_checkout_tree(repo, object, &checkout) != 0) ok = false;
            git_object_free(object);
            git_reference *updated = nullptr;
            if (ok && git_reference_create(&updated, repo, ("refs/heads/" + branchName).c_str(), upstreamId, 1,
                                            "Fast-forward fork to upstream") != 0) ok = false;
            git_reference_free(updated);
            git_annotated_commit_free(branch); git_annotated_commit_free(upstream);
            if (!ok) return fail("The fork working tree could not be fast-forwarded to upstream.");
            git_repository_free(repo); return true;
        }
    }
    // libgit2 requires a committer for every rebased commit.
    git_signature *committer = nullptr;
    if (ok && git_signature_default(&committer, repo) != 0 &&
        git_signature_now(&committer, "RetroGit", "retrogit@localhost") != 0) ok = false;
    git_rebase *rebase = nullptr;
    git_rebase_options options = GIT_REBASE_OPTIONS_INIT;
    if (ok) ok = git_rebase_init(&rebase, repo, branch, upstream, nullptr, &options) == 0;
    git_annotated_commit_free(branch); git_annotated_commit_free(upstream);
    if (!ok) {
        git_signature_free(committer);
        return fail("The fork and upstream history could not be prepared for rebase.");
    }
    git_rebase_operation *operation = nullptr;
    bool conflict = false;
    int step = 0;
    while ((step = git_rebase_next(&operation, rebase)) == 0) {
        git_oid rebased;
        const int result = git_rebase_commit(&rebased, rebase, nullptr, committer, nullptr, nullptr);
        if (result == GIT_EAPPLIED) continue; // change is already upstream
        if (result != 0) { ok = false; conflict = (result == GIT_EUNMERGED); break; }
    }
    if (ok && step != GIT_ITEROVER) ok = false; // git_rebase_next failed
    if (ok && git_rebase_finish(rebase, committer) != 0) ok = false;
    git_signature_free(committer);
    if (!ok) {
        git_rebase_abort(rebase); git_rebase_free(rebase);
        if (errorMessage) *errorMessage = conflict
            ? "Rebase stopped because the upstream changes conflict with your branch."
            : "Rebase failed. Your branch was restored to its previous state.";
        git_repository_free(repo); return false;
    }
    git_rebase_free(rebase); git_repository_free(repo); return true;
}

bool GitManager::isBranchMerged(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = NULL;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_oid target_oid;
    std::string targetRef = "refs/heads/" + targetBranch;
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        if (git_reference_name_to_id(&target_oid, repo, targetBranch.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    git_oid source_oid;
    std::string sourceRef = "refs/heads/" + sourceBranch;
    if (git_reference_name_to_id(&source_oid, repo, sourceRef.c_str()) != 0) {
        if (git_reference_name_to_id(&source_oid, repo, sourceBranch.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    git_oid merge_base_oid;
    int err = git_merge_base(&merge_base_oid, repo, &target_oid, &source_oid);
    bool merged = false;
    if (err == 0) {
        if (git_oid_equal(&merge_base_oid, &source_oid)) {
            merged = true;
        }
    }

    git_repository_free(repo);
    return merged;
}


bool GitManager::getCommitsBetweenBranches(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch, std::vector<GitCommitInfo>& commits)
{
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

    git_oid target_oid;
    git_oid source_oid;

    std::string targetRef = "refs/heads/" + targetBranch;
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        if (git_reference_name_to_id(&target_oid, repo, targetBranch.c_str()) != 0) {
            git_revwalk_free(walker);
            git_repository_free(repo);
            return false;
        }
    }

    std::string sourceRef = "refs/heads/" + sourceBranch;
    if (git_reference_name_to_id(&source_oid, repo, sourceRef.c_str()) != 0) {
        if (git_reference_name_to_id(&source_oid, repo, sourceBranch.c_str()) != 0) {
            git_revwalk_free(walker);
            git_repository_free(repo);
            return false;
        }
    }

    git_revwalk_push(walker, &source_oid);
    git_revwalk_hide(walker, &target_oid);

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
                
                info.date = formatGitTime(sig->when.time);
            }
            
            commits.push_back(info);
            git_commit_free(commit);
        }
    }

    git_revwalk_free(walker);
    git_repository_free(repo);
    return true;
}

bool GitManager::getPRChangedFiles(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch, std::vector<GitCommitFileChange>& changedFiles)
{
    std::cerr << "RetroGit [DEBUG]: getPRChangedFiles started. RepoPath: " << repoPath << ", source: " << sourceBranch << ", target: " << targetBranch << std::endl;
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    int err = git_repository_open(&repo, normPath.c_str());
    if (err != 0) {
        std::cerr << "RetroGit [ERROR]: getPRChangedFiles failed to open repo, err=" << err << std::endl;
        return false;
    }

    git_oid target_oid;
    git_oid source_oid;

    std::string targetRef = "refs/heads/" + targetBranch;
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        if (git_reference_name_to_id(&target_oid, repo, targetBranch.c_str()) != 0) {
            std::cerr << "RetroGit [ERROR]: getPRChangedFiles failed to find target ref " << targetBranch << std::endl;
            git_repository_free(repo);
            return false;
        }
    }

    std::string sourceRef = "refs/heads/" + sourceBranch;
    if (git_reference_name_to_id(&source_oid, repo, sourceRef.c_str()) != 0) {
        if (git_reference_name_to_id(&source_oid, repo, sourceBranch.c_str()) != 0) {
            std::cerr << "RetroGit [ERROR]: getPRChangedFiles failed to find source ref " << sourceBranch << std::endl;
            git_repository_free(repo);
            return false;
        }
    }

    char target_hex[GIT_OID_HEXSZ + 1];
    char source_hex[GIT_OID_HEXSZ + 1];
    git_oid_tostr(target_hex, sizeof(target_hex), &target_oid);
    git_oid_tostr(source_hex, sizeof(source_hex), &source_oid);
    std::cerr << "RetroGit [DEBUG]: target_oid=" << target_hex << ", source_oid=" << source_hex << std::endl;

    git_oid merge_base_oid;
    if (git_merge_base(&merge_base_oid, repo, &target_oid, &source_oid) != 0) {
        std::cerr << "RetroGit [DEBUG]: git_merge_base failed, using target_oid as fallback base" << std::endl;
        merge_base_oid = target_oid;
    } else {
        char base_hex[GIT_OID_HEXSZ + 1];
        git_oid_tostr(base_hex, sizeof(base_hex), &merge_base_oid);
        std::cerr << "RetroGit [DEBUG]: merge_base_oid=" << base_hex << std::endl;
    }

    git_commit *base_commit = nullptr;
    git_tree *base_tree = nullptr;
    if (git_commit_lookup(&base_commit, repo, &merge_base_oid) == 0) {
        git_commit_tree(&base_tree, base_commit);
    } else {
        std::cerr << "RetroGit [WARNING]: base commit lookup failed for OID" << std::endl;
    }

    git_commit *source_commit = nullptr;
    git_tree *source_tree = nullptr;
    if (git_commit_lookup(&source_commit, repo, &source_oid) == 0) {
        git_commit_tree(&source_tree, source_commit);
    } else {
        std::cerr << "RetroGit [WARNING]: source commit lookup failed for OID" << std::endl;
    }

    git_diff *diff = nullptr;
    err = git_diff_tree_to_tree(&diff, repo, base_tree, source_tree, nullptr);
    if (err == 0) {
        size_t num_deltas = git_diff_num_deltas(diff);
        std::cerr << "RetroGit [DEBUG]: git_diff_tree_to_tree succeeded, num_deltas=" << num_deltas << std::endl;
        for (size_t i = 0; i < num_deltas; ++i) {
            const git_diff_delta *delta = git_diff_get_delta(diff, i);
            if (delta) {
                std::string path = delta->new_file.path ? delta->new_file.path : "";
                if (path.empty() && delta->old_file.path) {
                    path = delta->old_file.path;
                }
                if (!path.empty()) {
                    GitCommitFileChange change;
                    change.path = path;
                    change.status = '~'; // modified default
                    if (delta->status == GIT_DELTA_ADDED || delta->status == GIT_DELTA_UNTRACKED) {
                        change.status = '+';
                    } else if (delta->status == GIT_DELTA_DELETED) {
                        change.status = '-';
                    }
                    changedFiles.push_back(change);
                }
            }
        }
        git_diff_free(diff);
    }

    if (base_tree) git_tree_free(base_tree);
    if (base_commit) git_commit_free(base_commit);
    if (source_tree) git_tree_free(source_tree);
    if (source_commit) git_commit_free(source_commit);
    git_repository_free(repo);
    return true;
}

bool GitManager::getPRFileDiff(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch, const std::string& relativePath, std::vector<GitDiffLine>& diffLines)
{
    std::string normPath = normalizePath(repoPath);
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normPath.c_str()) != 0) {
        return false;
    }

    git_oid target_oid;
    git_oid source_oid;

    std::string targetRef = "refs/heads/" + targetBranch;
    if (git_reference_name_to_id(&target_oid, repo, targetRef.c_str()) != 0) {
        if (git_reference_name_to_id(&target_oid, repo, targetBranch.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    std::string sourceRef = "refs/heads/" + sourceBranch;
    if (git_reference_name_to_id(&source_oid, repo, sourceRef.c_str()) != 0) {
        if (git_reference_name_to_id(&source_oid, repo, sourceBranch.c_str()) != 0) {
            git_repository_free(repo);
            return false;
        }
    }

    git_oid merge_base_oid;
    if (git_merge_base(&merge_base_oid, repo, &target_oid, &source_oid) != 0) {
        merge_base_oid = target_oid;
    }

    git_commit *base_commit = nullptr;
    git_tree *base_tree = nullptr;
    if (git_commit_lookup(&base_commit, repo, &merge_base_oid) == 0) {
        git_commit_tree(&base_tree, base_commit);
    }

    git_commit *source_commit = nullptr;
    git_tree *source_tree = nullptr;
    if (git_commit_lookup(&source_commit, repo, &source_oid) == 0) {
        git_commit_tree(&source_tree, source_commit);
    }

    git_diff *diff = nullptr;
    git_diff_options opts = GIT_DIFF_OPTIONS_INIT;
    opts.context_lines = 3;
    opts.pathspec.count = 1;
    char *paths[1] = { const_cast<char*>(relativePath.c_str()) };
    opts.pathspec.strings = paths;

    if (git_diff_tree_to_tree(&diff, repo, base_tree, source_tree, &opts) == 0) {
        DiffCallbackPayload payload;
        payload.lines = &diffLines;
        git_diff_print(diff, GIT_DIFF_FORMAT_PATCH, file_diff_line_cb, &payload);
        git_diff_free(diff);
    }

    if (base_tree) git_tree_free(base_tree);
    if (base_commit) git_commit_free(base_commit);
    if (source_tree) git_tree_free(source_tree);
    if (source_commit) git_commit_free(source_commit);
    git_repository_free(repo);
    return true;
}





bool GitManager::setReviewRef(const std::string& repoPath, const std::string& ref, const std::string& commit)
{
    if (ref.rfind("refs/retrogit/pull/", 0) != 0 || commit.size() != GIT_OID_HEXSZ) return false;
    git_oid oid;
    if (git_oid_fromstr(&oid, commit.c_str()) != 0) return false;
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0) return false;
    // Already in place: the reference was only created after a full check.
    git_oid current;
    if (git_reference_name_to_id(&current, repo, ref.c_str()) == 0 && git_oid_equal(&current, &oid)) {
        git_repository_free(repo);
        return true;
    }
    git_commit *object = nullptr;
    git_reference *reference = nullptr;
    std::set<std::string> closure;
    const std::set<std::string> complete = refTargets(repo);
    bool ok = collectObjects(repo, oid, closure, &complete) && git_commit_lookup(&object, repo, &oid) == 0 &&
        git_reference_create(&reference, repo, ref.c_str(), &oid, 1, "RetroGit PR snapshot") == 0;
    git_reference_free(reference);
    git_commit_free(object);
    git_repository_free(repo);
    return ok;
}

bool GitManager::canFastForward(const std::string& repoPath, const std::string& source, const std::string& target, std::string* reason)
{
    if (reason) reason->clear();
    auto fail = [reason](const char *text) { if (reason) *reason = text; return false; };
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(repoPath).c_str()) != 0)
        return fail("The local repository cannot be opened.");
    git_oid src, dst, base;
    const std::string sourceRef = source.rfind("refs/", 0) == 0 ? source : "refs/heads/" + source;
    if (git_reference_name_to_id(&src, repo, sourceRef.c_str()) != 0 ||
        git_reference_name_to_id(&dst, repo, ("refs/heads/" + target).c_str()) != 0) {
        git_repository_free(repo);
        return fail("The source or target branch is missing. Download the repository history and retry.");
    }
    const int result = git_merge_base(&base, repo, &src, &dst);
    git_repository_free(repo);
    if (result == GIT_ENOTFOUND)
        return fail("The proposed commit and target have no common ancestor. Recreate the changes on a branch based on the upstream target and submit a new pull request.");
    if (result != 0)
        return fail("The commit history could not be read. Download the complete repository history and retry.");
    if (git_oid_equal(&base, &src))
        return fail("The target already contains the proposed commit.");
    if (!git_oid_equal(&base, &dst))
        return fail("The branches have diverged. Incorporate upstream changes into the source branch and submit a new pull request.");
    return true;
}

bool GitManager::configureFork(const std::string& localPath, const std::string& forkPath, const std::string& upstreamPath)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(localPath).c_str()) != 0) return false;
    git_remote *remote = nullptr;
    bool ok = git_remote_set_url(repo, "origin", normalizePath(forkPath).c_str()) == 0;
    if (ok) {
        int result = git_remote_create(&remote, repo, "upstream", normalizePath(upstreamPath).c_str());
        ok = result == 0 || (result == GIT_EEXISTS &&
            git_remote_set_url(repo, "upstream", normalizePath(upstreamPath).c_str()) == 0);
    }
    git_remote_free(remote);
    git_repository_free(repo);
    return ok;
}

bool GitManager::materializeForkBranches(const std::string& path)
{
    git_repository *repo = nullptr;
    if (git_repository_open(&repo, normalizePath(path).c_str()) != 0) return false;
    git_reference_iterator *iter = nullptr;
    bool ok = git_reference_iterator_glob_new(&iter, repo, "refs/remotes/origin/*") == 0;
    git_reference *ref = nullptr;
    while (ok && git_reference_next(&ref, iter) == 0) {
        const git_oid *oid = git_reference_target(ref);
        if (oid) {
            const std::string name = "refs/heads/" + std::string(git_reference_name(ref)).substr(20);
            git_reference *local = nullptr;
            const int result = git_reference_create(&local, repo, name.c_str(), oid, 0, "Fork branch");
            ok = result == 0 || result == GIT_EEXISTS;
            git_reference_free(local);
        }
        git_reference_free(ref);
    }
    git_reference_iterator_free(iter);
    git_repository_free(repo);
    return ok;
}
