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

#pragma once

#include <QList>
#include <QString>

// Widget-free undo/redo history over the config document (the plain line
// list). FilterTable commits a snapshot after every mutation - all mutations
// already funnel through the linesChanged() signal, so one self-connection
// covers structural edits, text edits and toggles alike - and undo/redo hand
// back a full snapshot to re-apply. Snapshots are cheap: a config document is
// a small list of short strings.
//
// Consecutive single-line edits of the same line coalesce into one step, so a
// knob drag or a typing run (many linesChanged ticks against one row) undoes
// as a single action instead of one step per tick. Any other mutation breaks
// the coalescing run.
//
// QtCore-only on purpose, mirroring FilterListModel: the class is
// unit-testable in EditorLogicTests without a QWidget. (audit #146 TD049)
class FilterListUndo
{
public:
	// Starts a fresh history at the given document state. Clears both stacks;
	// used on document load/replace, which must never be undoable into the
	// previous file's contents.
	void reset(const QList<QString>& lines);

	// Records the document state after a mutation. No-op when nothing
	// changed. Any commit discards the redo branch.
	void commit(const QList<QString>& lines);

	bool canUndo() const;
	bool canRedo() const;

	// Steps the history and returns the document state to re-apply. Calling
	// without canUndo()/canRedo() returns the current state unchanged.
	QList<QString> undo();
	QList<QString> redo();

private:
	// Index of the single differing line between two equal-sized documents,
	// or -1 when the change is not a single-line text edit.
	static int singleEditIndex(const QList<QString>& before, const QList<QString>& after);

	// Oldest-first snapshots of the state BEFORE each recorded step.
	QList<QList<QString>> undoStack;
	// Snapshots of the state undone away from, newest-first at the back.
	QList<QList<QString>> redoStack;
	// The last committed (= currently applied) document state.
	QList<QString> current;
	// Line index of the previous commit's single-line edit, or -1. Drives
	// the coalescing rule above.
	int lastEditIndex = -1;
	// Bounds memory for pathological sessions; oldest steps fall off.
	static const int maxDepth = 200;
};
