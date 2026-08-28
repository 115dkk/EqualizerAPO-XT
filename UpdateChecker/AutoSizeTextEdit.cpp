/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

#include "stdafx.h"
#include "AutoSizeTextEdit.h"

AutoSizeTextEdit::AutoSizeTextEdit(QWidget* parent)
	: QTextEdit(parent)
{
}

AutoSizeTextEdit::~AutoSizeTextEdit()
{
}

QSize AutoSizeTextEdit::sizeHint() const
{
	QSize s(document()->size().toSize());
	return QSize(s.width() + contentsMargins().left() + contentsMargins().right(), s.height() + contentsMargins().top() + contentsMargins().bottom());
}
