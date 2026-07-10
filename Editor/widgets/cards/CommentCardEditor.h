/*
    This file is part of EqualizerAPO-XT, a system-wide equalizer.

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

#include "Editor/IFilterGUI.h"

class QLineEdit;

// Modern card body for a pure comment line (the neutral base shared by
// every skin): the note's text without the leading '#', editable in place.
// A comment line has no "command: parameters" shape, so store() uses the
// card-path contract that FilterCardRow::updateModel understands: command
// "#" means "reassemble as '# <parameters>' with no colon".
class CommentCardEditor : public IFilterGUI
{
	Q_OBJECT

public:
	explicit CommentCardEditor(const QString& line, QWidget* parent = nullptr);

	void store(QString& command, QString& parameters) override;

private:
	QLineEdit* textEdit = nullptr;
};
