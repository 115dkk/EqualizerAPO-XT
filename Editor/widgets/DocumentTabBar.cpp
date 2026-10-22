/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer forked from Equalizer APO.
	Copyright (C) 2017 Jonas Thedering (Equalizer APO)
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "DocumentTabBar.h"

#include <QCoreApplication>
#include <QStyle>
#include <QToolButton>

#include "Editor/SkinManager.h"
#include "Editor/helpers/GUIHelper.h"

namespace
{
// Qt's catalog already translates its close button's tooltip (bundled
// qtbase_*.qm); reusing the entry keeps the wording identical. Kept out of
// string literals at the call site so lupdate does not copy it into the
// Editor catalogs.
const char* const closeTabContext = "CloseButton";
const char* const closeTabText = "Close Tab";

const int glyphSize = 12;

QIcon makeCloseIcon(const SkinTokens& tokens)
{
	const QString glyph = QStringLiteral(":/icons/modern/tab-close.svg");
	// Muted at rest, full ink under the pointer (QIcon::Active, which an
	// auto-raise tool button uses while hovered).
	QIcon icon = GUIHelper::tintedIcon(glyph, QColor(tokens.mutedText), glyphSize);
	icon.addPixmap(GUIHelper::tintedIcon(glyph, QColor(tokens.text), glyphSize).pixmap(QSize(glyphSize, glyphSize)),
		QIcon::Active);
	return icon;
}
}

DocumentTabBar::DocumentTabBar(QWidget* parent)
	: QTabBar(parent), closeIcon(makeCloseIcon(SkinManager::instance()->tokens()))
{
	connect(SkinManager::instance(), &SkinManager::skinChanged, this, [this](const SkinTokens& tokens) {
		applySkinIcons(tokens);
	});
}

QTabBar::ButtonPosition DocumentTabBar::closeSide() const
{
	return static_cast<ButtonPosition>(style()->styleHint(QStyle::SH_TabBar_CloseButtonPosition, nullptr, this));
}

void DocumentTabBar::tabInserted(int index)
{
	QTabBar::tabInserted(index);
	if (!tabsClosable() || SkinManager::instance()->isHeritage())
		return;

	// insertTab has just mounted Qt's button; swap ours in on the same side.
	const ButtonPosition side = closeSide();
	QWidget* stock = tabButton(index, side);

	QToolButton* button = new QToolButton(this);
	button->setObjectName(QStringLiteral("TabCloseButton"));
	button->setFocusPolicy(Qt::NoFocus);
	button->setAutoRaise(true);
	button->setFixedSize(QSize(18, 18));
	button->setIconSize(QSize(glyphSize, glyphSize));
	button->setIcon(closeIcon);
	const QString text = QCoreApplication::translate(closeTabContext, closeTabText);
	button->setToolTip(text);
	button->setAccessibleName(text);
	connect(button, &QToolButton::clicked, this, [this, button]() {
		closeTabOf(button);
	});
	setTabButton(index, side, button);
	if (stock != nullptr)
		stock->deleteLater();
}

void DocumentTabBar::closeTabOf(const QToolButton* button)
{
	const ButtonPosition side = closeSide();
	for (int index = 0; index < count(); index++)
	{
		if (tabButton(index, side) == button)
		{
			emit tabCloseRequested(index);
			return;
		}
	}
}

void DocumentTabBar::applySkinIcons(const SkinTokens& tokens)
{
	closeIcon = makeCloseIcon(tokens);
	const ButtonPosition side = closeSide();
	for (int index = 0; index < count(); index++)
	{
		QToolButton* button = qobject_cast<QToolButton*>(tabButton(index, side));
		if (button != nullptr && button->objectName() == QLatin1String("TabCloseButton"))
			button->setIcon(closeIcon);
	}
}
