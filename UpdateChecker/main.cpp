/*
	This file is part of EqualizerAPO, a system-wide equalizer.
	Copyright (C) 2024  Jonas Dahlinger

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License along
	with this program; if not, write to the Free Software Foundation, Inc.,
	51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "stdafx.h"
#include <chrono>
#include <QDir>
#include <QStyleHints>
#include <QCommandLineParser>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtWidgets/QApplication>
#include "UpdateChecker.h"
#include "UpdateInfoFormatter.h"
#include "VelopackUpdateInfo.h"
#include "services/logging/LogHelper.h"
#include "version.h"
#include "Editor/helpers/QtAppBootstrap.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>



QByteArray readUpdateUrl(QNetworkAccessManager& manager, const QString& url, bool autoMode, bool* ok, QString* errorMessage);
void showFailureMessage(QString message, QString title);

int main(int argc, char* argv[])
{
	// Shared bootstrap: plugin-path anchoring plus the Editor's language
	// preference.
	QtAppBootstrap::addExecutableRelativePluginPath();

	QApplication app(argc, argv);
	if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark)
		app.setStyle("fusion");

	QtAppBootstrap::applyUserLocale();

	QTranslator qtTranslator;
	QTranslator updateCheckerTranslator;
	QtAppBootstrap::installTranslators(app, QStringLiteral("UpdateChecker"), qtTranslator, updateCheckerTranslator);

	QCommandLineParser parser;
	QCommandLineOption autoOption("a", "Automatic mode (no dialog if no new version, respect skip version, only check every 24 hours)");
	parser.addOptions(QList<QCommandLineOption>() << autoOption);
	parser.process(app);
	bool autoMode = parser.isSet(autoOption);

	// Audit #250 F019: one display-version rule for all binaries (version.h).
	QString version = QString::fromStdWString(eapoDisplayVersionW());

	QString channel = VelopackUpdateInfo::defaultChannel();
	QString url = VelopackUpdateInfo::githubLatestReleaseUrl(EAPO_REPO_SLUG);
	QString skipVersion;
	if (autoMode)
	{
		QSettings settings(QString::fromWCharArray(UPDATE_CHECKER_REGPATH), QSettings::NativeFormat);
		QDateTime lastCheckDate = settings.value("lastCheckDate").toDateTime();

		if (lastCheckDate.isValid() && lastCheckDate.toUTC().daysTo(QDateTime::currentDateTimeUtc()) < 1)
			return 1;

		skipVersion = settings.value("skipVersion").toString();
	}

	// Audit #250 F039: the date used to be stamped before the request, so a
	// failed check also suppressed retries for 24 hours. Stamp it only once
	// a check actually completed.
	auto markCheckCompleted = [&]()
	{
		if (!autoMode)
			return;
		QSettings settings(QString::fromWCharArray(UPDATE_CHECKER_REGPATH), QSettings::NativeFormat);
		settings.setValue("lastCheckDate", QDateTime::currentDateTime(QTimeZone::systemTimeZone()).toString(Qt::DateFormat::ISODate));
	};

	QNetworkAccessManager manager;
	int result = 0;
	auto showNoUpdateMessage = [&]()
	{
		if (!autoMode)
			QMessageBox::information(nullptr, UpdateChecker::tr("No update available"), UpdateChecker::tr("The installed version %0 of Equalizer APO is up to date.").arg(version));
	};

	bool requestOk = false;
	QString requestError;
	QByteArray json = readUpdateUrl(manager, url, autoMode, &requestOk, &requestError);
	if (!requestOk)
	{
		// Audit #250 F039: with the failure dialog dismissed forever, a
		// broken check used to be completely silent. The log survives it.
		LogFStatic(L"Update check failed: %s", requestError.toStdWString().c_str());
		showFailureMessage(requestError, UpdateChecker::tr("Error while checking for update"));
	}
	else if (json.isEmpty())
	{
		markCheckCompleted();
		showNoUpdateMessage();
	}
	else
	{
		QJsonParseError error;
		QJsonDocument rawDoc = QJsonDocument::fromJson(json, &error);
		if (error.error != QJsonParseError::NoError)
		{
			LogFStatic(L"Update response could not be parsed: %s", error.errorString().toStdWString().c_str());
			showFailureMessage(error.errorString(), UpdateChecker::tr("Error while reading response of update check"));
		}
		else
		{
			markCheckCompleted();
			QJsonDocument updateDoc = rawDoc;
			if (VelopackUpdateInfo::isGitHubRelease(rawDoc))
			{
				updateDoc = QJsonDocument();

				QString feedUrl = VelopackUpdateInfo::feedAssetUrl(rawDoc, channel);
				if (!feedUrl.isEmpty())
				{
					bool feedRequestOk = false;
					QString feedRequestError;
					QByteArray feedJson = readUpdateUrl(manager, feedUrl, autoMode, &feedRequestOk, &feedRequestError);
					if (feedRequestOk && !feedJson.isEmpty())
					{
						QJsonParseError feedError;
						QJsonDocument feedDoc = QJsonDocument::fromJson(feedJson, &feedError);
						if (feedError.error == QJsonParseError::NoError)
							updateDoc = VelopackUpdateInfo::fromVelopackFeed(feedDoc, rawDoc, channel, version);
					}
				}

				if (updateDoc.isEmpty())
					updateDoc = VelopackUpdateInfo::fromGitHubRelease(rawDoc, channel, version);
			}

			if (autoMode && !skipVersion.isEmpty() && !updateDoc.isEmpty())
			{
				QString newestVersion;
				UpdateInfoFormatter::releaseHtml(updateDoc, &newestVersion);
				if (newestVersion == skipVersion)
					updateDoc = QJsonDocument();
			}

			if (updateDoc.isEmpty())
			{
				showNoUpdateMessage();
			}
			else
			{
				UpdateChecker dialog(nullptr, updateDoc);
				dialog.show();
				result = app.exec();
			}
		}
	}

	return result;
}

QByteArray readUpdateUrl(QNetworkAccessManager& manager, const QString& url, bool autoMode, bool* ok, QString* errorMessage)
{
	if (ok != nullptr)
		*ok = false;
	if (errorMessage != nullptr)
		errorMessage->clear();

	QNetworkReply* reply = nullptr;
	int tries = autoMode ? 10 : 1;
	while (tries-- > 0)
	{
		QNetworkRequest request(QUrl(url, QUrl::StrictMode));
		request.setHeader(QNetworkRequest::UserAgentHeader, "EqualizerAPO-XT UpdateChecker");
		reply = manager.get(request);
		QEventLoop loop;
		QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
		QTimer timer;
		timer.setInterval(std::chrono::seconds{10});
		timer.setSingleShot(true);
		QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
		loop.exec();

		if (reply->isFinished() && reply->error() != QNetworkReply::HostNotFoundError)
			break;

		reply->abort();
		reply->deleteLater();
		reply = nullptr;
		if (tries > 0)
			QThread::sleep(5);
	}

	if (reply == nullptr || !reply->isFinished())
	{
		// Audit #250 F041: the last attempt's still-running reply used to be
		// abandoned without abort/deleteLater.
		if (reply != nullptr)
		{
			reply->abort();
			reply->deleteLater();
		}
		if (errorMessage != nullptr)
			*errorMessage = UpdateChecker::tr("The update check timed out.");
		return QByteArray();
	}

	if (reply->error() != QNetworkReply::NoError)
	{
		if (errorMessage != nullptr)
			*errorMessage = reply->errorString();
		reply->deleteLater();
		return QByteArray();
	}

	QByteArray body = reply->readAll();
	reply->deleteLater();
	if (ok != nullptr)
		*ok = true;
	return body;
}

void showFailureMessage(QString message, QString title)
{
	QSettings settings(QString::fromWCharArray(UPDATE_CHECKER_REGPATH), QSettings::NativeFormat);
	bool hideFailureMessage = settings.value("hideFailureMessage").toBool();
	if (!hideFailureMessage)
	{
		QMessageBox messageBox;
		messageBox.setText(message);
		messageBox.setWindowTitle(title);
		messageBox.setIcon(QMessageBox::Icon::Critical);
		QCheckBox* hideCheckBox = new QCheckBox(UpdateChecker::tr("Don't show message for failed update check again"));
		messageBox.setCheckBox(hideCheckBox);
		messageBox.exec();

		if (hideCheckBox->isChecked())
			settings.setValue("hideFailureMessage", true);
	}
}
