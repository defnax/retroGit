/*******************************************************************************
 * gui/GitManager.h                                                          *
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

#ifndef GITMANAGER_H
#define GITMANAGER_H

#include <string>
#include <map>
#include <vector>
#include <git2.h>

struct GitCommitInfo {
    std::string hash;
    std::string message;
    std::string author;
    std::string date;
    std::string full_hash;
};

struct GitDiffLine {
    char origin;
    std::string text;
};

struct GitLocalChange {
    std::string path;
    char status; // '+', '-', '~', '?'
    std::string color_hex;
};

class GitManager
{
public:
    /**
     * @brief Initialize libgit2. Must be called once before using any Git features.
     */
    static bool init();

    /**
     * @brief Shutdown libgit2 and free resources.
     */
    static void shutdown();

    /**
     * @brief Initialize a new Git repository at the given path.
     */
    static bool initRepository(const std::string& repoPath, bool isBare = true);

    /**
     * @brief Check if the directory is a valid Git repository.
     */
    static bool isValidRepository(const std::string& repoPath);

    /**
     * @brief Get the absolute bare repository path inside RsAccounts::AccountDirectory.
     */
    static std::string getBareRepoPath(const std::string& groupId);

    /**
     * @brief Clone a bare repository to a local working directory.
     */
    static bool cloneRepository(const std::string& bareRepoPath, const std::string& localPath);

    /**
     * @brief Pull changes from origin into a local working directory.
     */
    static bool pullRepository(const std::string& localPath);

    /**
     * @brief Commit all changes in the working directory.
     */
    static bool commitChanges(const std::string& repoPath, const std::string& commitMessage, const std::string& authorName, const std::string& authorEmail);

    /**
     * @brief Create a new branch pointing to a source branch.
     */
    static bool createBranch(const std::string& repoPath, const std::string& branchName, const std::string& sourceBranch);

    /**
     * @brief Merge a source branch into a target branch (fast-forward reference update).
     */
    static bool mergeBranch(const std::string& repoPath, const std::string& sourceBranch, const std::string& targetBranch);

    /**
     * @brief Retrieve the commit log from the repository.
     */
    static bool getCommitLog(const std::string& repoPath, std::vector<GitCommitInfo>& commits);

    /**
     * @brief Retrieve a flat list of all files in the HEAD commit tree (or selected refName).
     */
    static bool getRepoFiles(const std::string& repoPath, std::vector<std::string>& files, const std::string& refName = "");

    /**
     * @brief Retrieve the list of all local branches and the current branch name.
     */
    static bool getBranches(const std::string& repoPath, std::vector<std::string>& branches, std::string& currentBranch);

    /**
     * @brief Retrieve the list of all tags.
     */
    static bool getTags(const std::string& repoPath, std::vector<std::string>& tags);

    /**
     * @brief Retrieve detailed info about a specific commit, including author, title, body, date and changed files.
     */
    static bool getCommitDetails(const std::string& repoPath, const std::string& commitHash,
                                 std::string& authorName, std::string& authorEmail,
                                 std::string& summary, std::string& body,
                                 std::string& date, std::vector<std::string>& changedFiles);

    /**
     * @brief Retrieve the diff of a specific file inside a commit.
     */
    static bool getFileDiff(const std::string& repoPath, const std::string& commitHash,
                            const std::string& relativePath, std::vector<GitDiffLine>& diffLines);

    /**
     * @brief Unpack a received packfile into the local bare repository and update references.
     * @param repoPath Path to the local repository
     * @param packfileData The raw bytes of the Git packfile
     * @param refUpdates A map of refnames to new commit SHAs
     */
    static bool unpackPackfile(const std::string& repoPath, const std::string& packfileData, const std::map<std::string, std::string>& refUpdates);

    /**
     * @brief Unpack a received packfile file into the local bare repository and update references.
     */
    static bool unpackPackfileFromFile(const std::string& repoPath, const std::string& packfilePath, const std::map<std::string, std::string>& refUpdates);

    /**
     * @brief Create a packfile from the local repository and return ref updates.
     */
    static bool createPackfile(const std::string &repoPath, std::string &packfileData, std::map<std::string, std::string> &refUpdates);

    /**
     * @brief Retrieve uncommitted changes in the repository.
     */
    static bool getLocalChanges(const std::string& repoPath, std::vector<GitLocalChange>& changes);

    /**
     * @brief Extract a file from the repository's HEAD tree (or selected refName) to a destination path.
     */
    static bool extractFile(const std::string& repoPath, const std::string& relativePath, const std::string& destPath, const std::string& refName = "");
};

#endif // GITMANAGER_H
