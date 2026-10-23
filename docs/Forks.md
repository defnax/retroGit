# Forks and cross-repository pull requests

## Contributor workflow

1. Subscribe to the original repository and choose **Fork Repository** in Working Directory.
2. Enter a name, select an owned RetroShare identity, and choose an existing empty folder.
3. RetroGit copies downloaded history, or starts a secure clone from the original owner first. Keep the app open until the fork is created. Download failures preserve files and do not create an empty published fork.
4. Open the fork under **My Repositories**. Create a branch, edit files, commit, and publish as usual.
5. Open **Code > Pull Requests > New pull request**. A fork targets its upstream automatically. Opening this dialog on the original also offers owned forks as source repositories.
6. Choose the source and target branches and enter a title and description. The proposal contains an exact commit snapshot; subsequent commits do not silently alter it. Submit a new PR for a revised proposal.

Forks preserve the original distribution/circle settings. They store the immediate upstream group ID, retain branch history, and configure `origin` to the fork's local serving repository and `upstream` to the original's local serving repository. Forking does not change the original clone's working-directory association.

## Review and merge

Open the proposal in the original repository. Small packs travel in the PR; larger packs use RetroShare file attachments. The details tab requests missing attachments and retries while open. Objects are imported without applying incoming branch updates, then the proposed commit is pinned under `refs/retrogit/pull/<message-id>`.

Only a target administrator can use Merge. Fast-forward merges check ancestry and use a compare-and-swap update, so a divergent or concurrently changed target cannot be overwritten. Divergent proposals must incorporate upstream changes and be submitted again. Automatic conflict resolution and merge commits are not part of this implementation.

PRs are author-signed children of published upstream updates, allowing contributors without the upstream publishing key to submit proposals. Code updates remain owner-signed root messages. Closure replies are accepted from the original PR author or the repository owner; merged status requires the repository owner identity.

## Compatibility

The service advertises version 1.1. Existing repositories and legacy same-repository PR records remain readable. Non-fork groups and legacy PRs retain their original serialized layout. Forks and snapshot PRs append metadata; both contributor and reviewer need the updated plugin to use the new workflow. Older peers are not expected to understand extended records.


