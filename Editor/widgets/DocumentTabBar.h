/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2017 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The tab bar of the open configuration files. Each tab's close button is a
	QToolButton (#TabCloseButton) whose X is tinted from the active skin's
	tokens, like the title bar's caption buttons, and whose shape and hover
	come from the skin's style sheet. Qt's own close button takes its glyph
	from the style sheet's QTabBar::close-button image; the skins set none
	(the stock bitmap matched no skin), so it painted only the hover
	background: a red square without an X. The heritage presentation keeps
	Qt's button. Middle-click closing is QTabBar's own while tabsClosable.
*/

#pragma once

#include <QIcon>
#include <QTabBar>

class QToolButton;
struct SkinTokens;

class DocumentTabBar : public QTabBar
{
public:
	explicit DocumentTabBar(QWidget* parent = nullptr);

protected:
	void tabInserted(int index) override;

private:
	void closeTabOf(const QToolButton* button);
	void applySkinIcons(const SkinTokens& tokens);
	ButtonPosition closeSide() const;

	QIcon closeIcon;
};
