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

DEPENDPATH  += ../../retroshare-gui/src/temp/ui ../../libretroshare/src
INCLUDEPATH += ../../retroshare-gui/src/temp/ui ../../libretroshare/src
INCLUDEPATH += ../../retroshare-gui/src/retroshare-gui

INCLUDEPATH += ../../rapidjson-1.1.0

#################################### Linux ########################################

linux-* {
	#INCLUDEPATH += /usr/include
}

#################################### Windows #####################################

win32 {
	LIBS_DIR = $$PWD/../../../libs
}

	QMAKE_CXXFLAGS *= -Wall

################################### HEADERS & SOURCES #############################

SOURCES = RetroGitPlugin.cpp               \
          services/p3Git.cc           \
          services/rsGitItems.cc \
          gui/MainWidget.cpp \
          gui/RetroGitNotify.cpp \

HEADERS = RetroGitPlugin.h                 \
          services/p3Git.h            \
          services/rsGitItems.h       \
          interface/rsRetroGit.h \
          gui/MainWidget.h \
          gui/RetroGitNotify.h \

FORMS += \
          gui/MainWidget.ui \

RESOURCES = gui/images.qrc
