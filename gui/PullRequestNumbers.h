/*******************************************************************************
 * gui/PullRequestNumbers.h                                                    *
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
 ********************************************************************************/

#ifndef RETROGIT_PULL_REQUEST_NUMBERS_H
#define RETROGIT_PULL_REQUEST_NUMBERS_H

#include <algorithm>
#include <map>
#include <vector>
#include "interface/rsGit.h"

// View-independent pull request numbers: the oldest published pull request is
// #1. Every view must number from the complete list, never from a filtered or
// re-sorted one, so a pull request keeps the same number everywhere.
inline std::map<RsGxsMessageId, int> pullRequestNumbers(const std::vector<RsGitPullRequest> &pullRequests)
{
    std::vector<const RsGitPullRequest *> ordered;
    ordered.reserve(pullRequests.size());
    for (const auto &pr : pullRequests) ordered.push_back(&pr);
    std::sort(ordered.begin(), ordered.end(), [](const RsGitPullRequest *a, const RsGitPullRequest *b) {
        if (a->mMeta.mPublishTs != b->mMeta.mPublishTs) return a->mMeta.mPublishTs < b->mMeta.mPublishTs;
        return a->mMeta.mMsgId < b->mMeta.mMsgId;
    });
    std::map<RsGxsMessageId, int> numbers;
    int next = 1;
    for (const auto *pr : ordered)
        if (numbers.emplace(pr->mMeta.mMsgId, next).second) ++next;
    return numbers;
}

#endif
