/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

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

#pragma once

#include <string>
#include <vector>
#include <QWidget>
#include <QVariantMap>

class IFilterGUI : public QWidget
{
	Q_OBJECT

public:
	IFilterGUI(QWidget* parent = 0);
	virtual ~IFilterGUI();

	virtual void configureChannels(std::vector<std::wstring>& channelNames) {}

	// The channels SELECTED at this row, walked top-down right after
	// configureChannels: a Channel row replaces the vector with its own
	// resolved selection for the rows below it (mirroring the engine's
	// getSelectChannels flow), a consumer copies it, everyone else leaves it
	// alone. Distinct from configureChannels on purpose - that vector is the
	// names IN SCOPE (Copy keeps adding to it), this one is what the engine
	// would actually hand a filter on this line.
	virtual void configureSelectedChannels(std::vector<std::wstring>& selectedChannels) {}

	virtual void store(QString& command, QString& parameters) = 0;

	virtual void loadPreferences(const QVariantMap& prefs) {}
	virtual void storePreferences(QVariantMap& prefs) {}

signals:
	void updateModel();
	void updateChannels();
};
