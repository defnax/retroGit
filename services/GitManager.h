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
     * @brief Initialize a new bare Git repository at the given path.
     */
    static bool initRepository(const std::string& repoPath);

    /**
     * @brief Get the absolute bare repository path inside RsAccounts::AccountDirectory.
     */
    static std::string getBareRepoPath(const std::string& groupId);

    /**
     * @brief Clone a bare repository to a local working directory.
     */
    static bool cloneRepository(const std::string& bareRepoPath, const std::string& localPath);

    /**
     * @brief Commit all changes in the working directory.
     */
    static bool commitChanges(const std::string& repoPath, const std::string& commitMessage, const std::string& authorName, const std::string& authorEmail);

    /**
     * @brief Retrieve the commit log from the repository.
     */
    static bool getCommitLog(const std::string& repoPath, std::vector<GitCommitInfo>& commits);

    /**
     * @brief Retrieve a flat list of all files in the HEAD commit tree.
     */
    static bool getRepoFiles(const std::string& repoPath, std::vector<std::string>& files);

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
};

#endif // GITMANAGER_H
