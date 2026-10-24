# RetroShare Git Plugin

RetroGit is a decentralized Git collaboration plugin for RetroShare.

# Features

- Create and initialize local Git repositories.
- Clone repositories from RetroShare peers.
- Publish commits and branches through GXS.
- Synchronize repository updates without a central server.
- Fork another user’s repository.
- Create branches and commit local changes.
- Rebase a fork onto the upstream repository.
- Create, review, merge, and close pull requests.
- View commit history, changed files, and diffs.
- Download repository packs and update working folders.
- Show repository, commit, pull-request, and notification status.
- Display author identities and avatars from RetroShare.
- Transfer repositories through decentralized GXS tunnels.
- Optional debug logging for diagnosing synchronization and pull-request issues.

# Dependencies 

### Install package dependencies:

#### Debian / Ubuntu / Linux Mint
	sudo apt install libgit2 libgit2-dev

#### Windows
	pacman -S mingw-w64-x86_64-libgit2


# build & install:

put/clone `RetroGit` to `RetroShare/plugins/` recommend

	cd ${YOUR_DIR}/RetroShare/plugins/RetroGit/
	qmake 
	make 

Copy your RetroGit.dll to "Data/extensions6" (Windows)
Then restart your RetroShare. You'll see a git logo in you main tool-bar.

# Usage:

RetroGit must be installed and enabled on both clients to clone or pull repositories through secure tunnels.
The owner must commit and Push / Publish before subscribers clone into an empty
folder. Repository responses are split into small packets, with download progress
and a two-minute inactivity timeout.
Large repositories may exceed the tunnel transfer limit. If this happens, RetroGit displays an error.
Repository packs can be downloaded separately from the Pushes / Packs tab.
When the repository owner publishes new commits, RetroGit notifies you. Select the repository and click Sync / Pull to update your local files.
