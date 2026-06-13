# RetroShare 0.6 Git Plugin

Distributed Git repository sharing over the GXS (Generic eXchange System) network.

# Dependencies 

### Install package dependencies:

#### Debian / Ubuntu / Linux Mint
	sudo apt install libgit2 libgit2-dev

#### Windows
	pacman -S mingw-w64-x86_64-libgit2


# build & install:

put/clone `RetroGit` to `RetroShare/plugins/` recommend

	cd ${YOUR_DIR}/RetroShare/plugins/RetroGit/
	qmake ..
	make 

Copy your RetroGit.dll to "Data/extensions6" (Windows)
Then restart your RetroShare. You'll see a git logo in you main tool-bar.

# Usage:

