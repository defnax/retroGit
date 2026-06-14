!include("../Common/retroshare_plugin.pri"): error("Could not include file ../Common/retroshare_plugin.pri")

greaterThan(QT_MAJOR_VERSION, 4) {
	# Qt 5
	QT += widgets
}

exists($$[QMAKE_MKSPECS]/features/mobility.prf) {
  CONFIG += mobility
} else {
  QT += multimedia
}
CONFIG += qt uic qrc resources
MOBILITY = multimedia
DESTDIR = lib
TARGET = RetroGit
CONFIG += c++17

DEPENDPATH  += ../../retroshare-gui/src/temp/ui ../../libretroshare/src
INCLUDEPATH += ../../retroshare-gui/src/temp/ui ../../libretroshare/src
INCLUDEPATH += ../../../Build/retroshare-gui/src/temp/ui
INCLUDEPATH += ../../retroshare-gui/src/retroshare-gui

INCLUDEPATH += ../../rapidjson-1.1.0

#################################### Linux ########################################

linux-* {
	#INCLUDEPATH += /usr/include
	CONFIG += link_pkgconfig
	PKGCONFIG *= libgit2
}

#################################### Windows #####################################

win32 {
	# Use MSYS2 MinGW64 paths for libgit2
	isEmpty(PREFIX_MSYS2) {
		PREFIX_MSYS2 = C:/msys64/mingw64
	}
	
	message(Linking libgit2 from $${PREFIX_MSYS2})
	
	INCLUDEPATH += $${PREFIX_MSYS2}/include
	LIBS        += -L$${PREFIX_MSYS2}/lib -lgit2 -lws2_32 -lwldap32
}

	QMAKE_CXXFLAGS *= -Wall

################################### HEADERS & SOURCES #############################

SOURCES = RetroGitPlugin.cpp               \
          services/p3Git.cc           \
          services/rsGitItems.cc \
          services/GitManager.cpp \
          gui/MainWidget.cpp \
          gui/RetroGitNotify.cpp \
          gui/GitGroupDialog.cpp \
          gui/GitCommitDialog.cpp \
          gui/GitBranchDialog.cpp \
          gui/GitNewPRDialog.cpp \
          gui/GitUserNotify.cpp \
          gui/GitWidget.cpp \
          gui/CodeWidget.cpp \
          gui/PullRequestsWidget.cpp \
          gui/PushesWidget.cpp

HEADERS = RetroGitPlugin.h                 \
          services/p3Git.h            \
          services/rsGitItems.h       \
          services/GitManager.h       \
          interface/rsGit.h \
          gui/MainWidget.h \
          gui/RetroGitNotify.h \
          gui/GitGroupDialog.h \
          gui/GitCommitDialog.h \
          gui/GitBranchDialog.h \
          gui/GitNewPRDialog.h \
          gui/GitUserNotify.h \
          gui/GitWidget.h \
          gui/CodeWidget.h \
          gui/PullRequestsWidget.h \
          gui/PushesWidget.h

FORMS += \
          gui/MainWidget.ui \
          gui/GitWidget.ui \
          gui/CodeWidget.ui \
          gui/PullRequestsWidget.ui \
          gui/PushesWidget.ui \

RESOURCES = gui/images.qrc
