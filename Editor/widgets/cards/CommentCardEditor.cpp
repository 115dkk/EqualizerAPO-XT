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

#include "CommentCardEditor.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>

CommentCardEditor::CommentCardEditor(const QString& line, QWidget* parent)
	: IFilterGUI(parent)
{
	setObjectName(QStringLiteral("CommentCardEditor"));
	setAttribute(Qt::WA_StyledBackground, true);

	QHBoxLayout* layout = new QHBoxLayout(this);
	layout->setContentsMargins(4, 2, 4, 2);
	layout->setSpacing(8);

	// The note text is everything after the '#'; one separating space is part
	// of the marker, anything beyond it is the author's own indentation.
	QString text = line.trimmed();
	if (text.startsWith(QLatin1Char('#')))
		text = text.mid(1);
	if (text.startsWith(QLatin1Char(' ')))
		text = text.mid(1);

	QLabel* glyph = new QLabel(QStringLiteral("#"), this);
	glyph->setObjectName(QStringLiteral("CommentCardGlyph"));
	layout->addWidget(glyph);

	textEdit = new QLineEdit(text, this);
	textEdit->setObjectName(QStringLiteral("CommentCardText"));
	textEdit->setPlaceholderText(tr("Write a note"));
	connect(textEdit, SIGNAL(editingFinished()), this, SIGNAL(updateModel()));
	layout->addWidget(textEdit, 1);
}

void CommentCardEditor::store(QString& command, QString& parameters)
{
	// "#" is the card-path sentinel for a colon-less line; FilterCardRow
	// writes "# <parameters>" (or a bare "#" when the note is empty).
	command = QStringLiteral("#");
	parameters = textEdit->text().trimmed();
}
