/*******************************************************************************
 * RetroGitPlugin.cpp                                                          *
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

#include <QTranslator>
#include <QApplication>
#include <QString>
#include <QIcon>
#include <QMessageBox>

#include "RetroGitPlugin.h"
#include "gui/MainWidget.h"
#include "gui/RetroGitNotify.h"
#include <retroshare-gui/RsAutoUpdatePage.h>
#include <gxs/rsgds.h>
#include <gxs/rsdataservice.h>
#include <gxs/rsnxs.h>
#include <gxs/rsgxsnetservice.h>
#include <util/rsdir.h>
#include "services/p3Git.h"
#include "services/rsGitItems.h"
#include "services/GitManager.h"
#include <interface/rsGit.h>
#include "pqi/p3cfgmgr.h"

#include <retroshare/rsinit.h>
#include <retroshare/rsgxstunnel.h>
#include <retroshare/rsplugin.h>
#include <retroshare/rsversion.h>
#include <retroshare/rsreputations.h>

#define IMAGE_GIT ":/images/git.png"


static void *inited = new RetroGitPlugin() ;

extern "C" {

	// This is *the* functions required by RS plugin system to give RS access to the plugin.
	// Be careful to:
	// - always respect the C linkage convention
	// - always return an object of type RsPlugin*
	//
	void *RETROSHARE_PLUGIN_provide()
	{
		static RetroGitPlugin *p = new RetroGitPlugin() ;

		return (void*)p ;
	}

	// This symbol contains the svn revision number grabbed from the executable.
	// It will be tested by RS to load the plugin automatically, since it is safe to load plugins
	// with same revision numbers, assuming that the revision numbers are up-to-date.
	//
	uint32_t RETROSHARE_PLUGIN_revision = abs(atoi(RS_EXTRA_VERSION)) ;

	// This symbol contains the svn revision number grabbed from the executable.
	// It will be tested by RS to load the plugin automatically, since it is safe to load plugins
	// with same revision numbers, assuming that the revision numbers are up-to-date.
	//
	uint32_t RETROSHARE_PLUGIN_api = RS_PLUGIN_API_VERSION ;
}

void RetroGitPlugin::getPluginVersion(int& major, int& minor, int& build, int& svn_rev) const
{
	major = RS_MAJOR_VERSION ;
	minor = RS_MINOR_VERSION ;
	build = RS_MINI_VERSION ;
	svn_rev = abs(atoi(RS_EXTRA_VERSION)) ;
}

uint16_t RetroGitPlugin::rs_service_id() const
{
	return RS_SERVICE_TYPE_RetroGit_PLUGIN;
}

RetroGitPlugin::RetroGitPlugin()
{
	qRegisterMetaType<RsPeerId>("RsPeerId");
	mainpage = NULL ;
	mRetroGit = NULL ;
	mPlugInHandler = NULL;
	mPeers = NULL;
	mConfigPage = NULL ;
	mIcon = NULL ;

	mRetroGitNotify = new RetroGitNotify;

    mGds = NULL;
    mNxsMgr = NULL;
    mGixs = NULL;
    mReputations = NULL;
    mGxsCircles = NULL;
    mGxsIdService = NULL;
    mPgpAuxUtils = NULL;

    GitManager::init();
}

RetroGitPlugin::~RetroGitPlugin()
{
	delete mRetroGitNotify;
    GitManager::shutdown();
}

void RetroGitPlugin::setInterfaces(RsPlugInInterfaces &interfaces)
{
    mPeers = interfaces.mPeers;
    mNxsMgr = interfaces.mRsNxsNetMgr;
    mGixs = interfaces.mGxsIdService;
    mReputations = interfaces.mReputations;
    mGxsCircles = interfaces.mGxsCirlces;
    mGxsIdService = interfaces.mGxsIdService;
    mPgpAuxUtils = interfaces.mPgpAuxUtils;
    mGxsDir = interfaces.mGxsDir;
}

ConfigPage *RetroGitPlugin::qt_config_page() const
{
	return NULL;
}

QDialog *RetroGitPlugin::qt_about_page() const
{
	static QMessageBox *about_dialog = NULL ;

	if(about_dialog == NULL)
	{
		about_dialog = new QMessageBox() ;

		QString text ;
		text += QObject::tr("<h3>RetroShare RetroGit plugin</h3><br/>  <br/>") ;
		text += QObject::tr("<br/>Decentralized Git Collaboration.<UL>") ;

		about_dialog->setText(text) ;
		about_dialog->setStandardButtons(QMessageBox::Ok) ;
	}

	return about_dialog ;
}

p3Service *RetroGitPlugin::p3_service() const
{
    if(mRetroGit == NULL)
    {
        std::string currGxsDir = RsAccounts::AccountDirectory() + "/gxs";
        RsDirUtil::checkCreateDirectory(currGxsDir);

        mGds = new RsDataService(currGxsDir + "/", "retrogit_db",
                        RS_SERVICE_TYPE_RetroGit_PLUGIN, NULL, "");

        mRetroGit = new p3Git(mGds, NULL, mGixs, mRetroGitNotify);

        mRetroGitNetService = new RsGxsNetService(
                        RS_SERVICE_TYPE_RetroGit_PLUGIN, mGds, mNxsMgr, 
            mRetroGit, mRetroGit->getServiceInfo(), 
            dynamic_cast<RsGixsReputation*>(mReputations), mGxsCircles, mGxsIdService,
            mPgpAuxUtils);

        mRetroGit->setNetworkExchangeService(mRetroGitNetService);
        mRetroGit->start("RetroGit Engine");
        mRetroGitNetService->start("RetroGit NS");
    }
    return mRetroGitNetService;
}

p3Config *RetroGitPlugin::p3_config() const
{
    // ensure mRetroGit is created
    p3_service();
    return mRetroGit;
}

void RetroGitPlugin::setPlugInHandler(RsPluginHandler *pgHandler)
{
	mPlugInHandler = pgHandler;
}

void RetroGitPlugin::stop()
{
	if(mRetroGit)
	{
		mRetroGit->fullstop();
	}
}

QIcon *RetroGitPlugin::qt_icon() const
{
	if (mIcon == NULL)
	{
		Q_INIT_RESOURCE(images);

		mIcon = new QIcon(IMAGE_GIT);
	}

	return mIcon;
}

MainPage *RetroGitPlugin::qt_page() const
{
	// return Gits main page here
	if(mainpage == NULL)
	{
		mainpage = new MainWidget(0, mRetroGitNotify);
	}

	return mainpage ;
}

std::string RetroGitPlugin::getShortPluginDescription() const
{
	return "RetroGit";
}

std::string RetroGitPlugin::getPluginName() const
{
	return "RetroGit";
}

QTranslator* RetroGitPlugin::qt_translator(QApplication */*app*/, const QString& languageCode, const QString& externalDir) const
{
	if (languageCode == "en") {
		return NULL;
	}

	QTranslator* translator = new QTranslator();
	if (translator->load(externalDir + "/RetroGit_" + languageCode + ".qm")) {
		return translator;
	} else if (translator->load(":/lang/RetroGit_" + languageCode + ".qm")) {
		return translator;
	}

	delete(translator);
	return NULL;
}

void RetroGitPlugin::qt_sound_events(SoundEvents &/*events*/) const
{
//	events.addEvent(QApplication::translate("RetroGit", "RetroGit"), QApplication::translate("RetroGit", "Incoming call"), RetroGit_SOUND_INCOMING_CALL);
}

/*ToasterNotify *RetroGitPlugin::qt_toasterNotify(){
	if (!mRetroGitToasterNotify) {
		mRetroGitToasterNotify = new RetroGitToasterNotify(mRetroGit, mRetroGitNotify);
	}
	return mRetroGitToasterNotify;
}*/
