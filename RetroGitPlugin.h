/*******************************************************************************
 * RetroGitPlugin.h                                                            *
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

#include <retroshare/rsplugin.h>
#include "gxs/rsgenexchange.h"

#include <QObject>

class p3Git ;
class RetroGitNotify ;
class RetroGitGUIHandler ;
class MainWidget ;
class RsGxsNetService;

class RetroGitPlugin: public RsPlugin
{
public:
	RetroGitPlugin() ;
	virtual ~RetroGitPlugin() ;

	virtual void setInterfaces(RsPlugInInterfaces& interfaces);
	virtual void getPluginVersion(int& major, int& minor, int& build, int& svn_rev) const;
	virtual p3Service  *p3_service() const;
	virtual p3Config   *p3_config()  const;
	virtual std::string configurationFileName() const { return "retrogit.cfg"; }
	virtual uint16_t rs_service_id() const;

	virtual void setPlugInHandler(RsPluginHandler *pgHandler);
	virtual void stop();

	virtual QIcon *qt_icon() const;
	virtual MainPage *qt_page() const;
	virtual ConfigPage *qt_config_page() const;
	virtual QDialog *qt_about_page() const;
	virtual std::string getShortPluginDescription() const;
	virtual std::string getPluginName() const;
	virtual QTranslator* qt_translator(QApplication *app, const QString& languageCode, const QString& externalDir) const;
	virtual void qt_sound_events(SoundEvents &events) const;

private:
	mutable p3Git *mRetroGit;
    mutable RsGxsNetService *mRetroGitNetService;
	RsPluginHandler *mPlugInHandler;
	RsPeers *mPeers;
	std::string mGxsDir;

	mutable MainWidget *mainpage ;
	mutable ConfigPage *mConfigPage ;
	mutable QIcon *mIcon ;

	RetroGitNotify *mRetroGitNotify ;
	RetroGitGUIHandler *mRetroGitGUIHandler ;

    mutable RsGeneralDataService* mGds;
    RsNxsNetMgr* mNxsMgr;
    RsGixs* mGixs;
    RsReputations* mReputations;
    RsGcxs* mGxsCircles;
    RsGxsIdExchange* mGxsIdService;
    PgpAuxUtils* mPgpAuxUtils;
};
