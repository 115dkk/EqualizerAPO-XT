/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2014  Jonas Thedering

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

#include "FilterListUndo.h"

void FilterListUndo::reset(const QList<QString>& lines)
{
	undoStack.clear();
	redoStack.clear();
	current = lines;
	lastEditIndex = -1;
}

void FilterListUndo::commit(const QList<QString>& lines)
{
	if (lines == current)
		return;

	int editIndex = singleEditIndex(current, lines);
	if (editIndex != -1 && editIndex == lastEditIndex && !undoStack.isEmpty())
	{
		// Same-line edit run (knob drag, typing): fold into the open step.
		// When the run lands back on the step's starting state, the step
		// cancelled itself out - drop it instead of recording a no-op.
		if (lines == undoStack.last())
		{
			undoStack.removeLast();
			lastEditIndex = -1;
		}
		current = lines;
		redoStack.clear();
		return;
	}

	undoStack.append(current);
	if (undoStack.size() > maxDepth)
		undoStack.removeFirst();
	current = lines;
	lastEditIndex = editIndex;
	redoStack.clear();
}

bool FilterListUndo::canUndo() const
{
	return !undoStack.isEmpty();
}

bool FilterListUndo::canRedo() const
{
	return !redoStack.isEmpty();
}

QList<QString> FilterListUndo::undo()
{
	if (undoStack.isEmpty())
		return current;

	redoStack.append(current);
	current = undoStack.takeLast();
	// The next edit starts a fresh step even when it touches the same line.
	lastEditIndex = -1;
	return current;
}

QList<QString> FilterListUndo::redo()
{
	if (redoStack.isEmpty())
		return current;

	undoStack.append(current);
	current = redoStack.takeLast();
	lastEditIndex = -1;
	return current;
}

int FilterListUndo::singleEditIndex(const QList<QString>& before, const QList<QString>& after)
{
	if (before.size() != after.size())
		return -1;

	int editIndex = -1;
	for (int i = 0; i < before.size(); i++)
	{
		if (before[i] != after[i])
		{
			if (editIndex != -1)
				return -1;
			editIndex = i;
		}
	}
	return editIndex;
}
