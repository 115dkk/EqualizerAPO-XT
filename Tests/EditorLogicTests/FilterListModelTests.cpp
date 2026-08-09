/*
	This file is part of EqualizerAPO-XT.

	FilterListModel and FilterListUndo: the widget-free document/selection
	model behind FilterTable and the undo/redo history it commits to on
	every linesChanged tick.
*/

#include <stdexcept>
#include <string>

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariant>

#include "Editor/widgets/FilterListModel.h"
#include "Editor/widgets/FilterListUndo.h"

#include "EditorLogicTestSupport.h"

// FilterListModel: the widget-free document/selection model behind
// FilterTable, so its mutation and selection logic is testable without a
// QWidget.
static void requireFilterListModelInvariants(const FilterListModel& model, const char* stage)
{
	QSet<FilterListItem*> documentItems;
	for (FilterListItem* item : model.items())
	{
		if (item == nullptr || documentItems.contains(item))
			throw std::runtime_error(std::string("FilterListModel projection invariant failed after ") + stage);
		documentItems.insert(item);
	}
	for (FilterListItem* item : model.selected())
	{
		if (!documentItems.contains(item))
			throw std::runtime_error(std::string("FilterListModel selection invariant failed after ") + stage);
	}
	if (model.focused() != nullptr && !documentItems.contains(model.focused()))
		throw std::runtime_error(std::string("FilterListModel focus invariant failed after ") + stage);
	if (model.selectionStart() != nullptr && !documentItems.contains(model.selectionStart()))
		throw std::runtime_error(std::string("FilterListModel anchor invariant failed after ") + stage);
}

void testFilterListModel()
{
	FilterListModel model;

	// setLines resets the document, focuses/anchors the first line and starts
	// with an empty selection.
	model.setLines(QList<QString>() << "Preamp: -6 dB" << "Include: a.txt" << "Delay: 10 ms");
	expectEqual((int)model.items().size(), 3, "setLines creates one item per line");
	expectEqual(model.lines().join('\n'), "Preamp: -6 dB\nInclude: a.txt\nDelay: 10 ms", "lines() round-trips setLines");
	expectTrue(model.focused() == model.items()[0], "setLines focuses the first line");
	expectTrue(model.selectionStart() == model.items()[0], "setLines anchors the selection on the first line");
	expectTrue(model.selected().isEmpty(), "setLines starts with an empty selection");
	requireFilterListModelInvariants(model, "setLines");

	// addLine inserts before an anchor item, or appends without one.
	FilterListItem* added = model.addLine("Filter: ON PK Fc 1000 Hz Gain -3 dB Q 0.71", model.items()[1]);
	expectEqual((int)model.items().indexOf(added), 1, "addLine inserts before the anchor");
	FilterListItem* appended = model.addLine("GraphicEQ: 20 -1; 1000 2");
	expectEqual((int)model.items().indexOf(appended), (int)model.items().size() - 1, "addLine without anchor appends");
	requireFilterListModelInvariants(model, "addLine");

	// Range selection math (the Shift-click/Shift-arrow logic): anchor..target
	// in either direction; a missing anchor leaves the selection untouched.
	model.setSelectionStart(model.items()[1]);
	model.selectRangeFromAnchor(model.items()[3]);
	expectEqual((int)model.selected().size(), 3, "range selection spans anchor..target");
	expectTrue(model.selected().contains(model.items()[1]) && model.selected().contains(model.items()[2])
		&& model.selected().contains(model.items()[3]),
		"range selection selects exactly the rows between anchor and target");
	model.selectRangeFromAnchor(model.items()[0]);
	expectEqual((int)model.selected().size(), 2, "a reversed range selects target..anchor");
	expectTrue(model.selected().contains(model.items()[0]) && model.selected().contains(model.items()[1]),
		"the reversed range selects rows 0..1");
	model.setSelectionStart(nullptr);
	model.selectRangeFromAnchor(model.items()[2]);
	expectEqual((int)model.selected().size(), 2, "a missing anchor leaves the selection unchanged");

	// Copy payload: selected lines in document order joined with \n, one prefs
	// map per line, regardless of the selection's insertion order.
	model.items()[0]->prefs.insert("expanded", true);
	model.items()[1]->prefs.insert("expanded", false);
	model.clearSelection();
	model.select(model.items()[1]);
	model.select(model.items()[0]);
	FilterListModel::CopyPayload payload = model.copyPayload();
	expectEqual(payload.text, model.items()[0]->text + '\n' + model.items()[1]->text,
		"copy payload joins selected lines in document order");
	expectEqual((int)payload.prefsList.size(), 2, "copy payload carries one prefs map per line");
	expectTrue(payload.prefsList[0].value("expanded").toBool() && !payload.prefsList[1].value("expanded").toBool(),
		"copy payload prefs align with their lines");
	expectEqual(model.firstSelectedIndex(), 0, "firstSelectedIndex finds the topmost selected row");

	// insertLines at a position replaces the selection with the inserted items
	// and focuses/anchors the first inserted line (the paste semantics).
	QList<FilterListItem*> inserted = model.insertLines(
		QStringList() << "Channel: L R" << "Preamp: -2 dB",
		QList<QVariantMap>() << QVariantMap({ { "expanded", true } }), 2);
	expectEqual((int)inserted.size(), 2, "insertLines returns the inserted items");
	expectEqual((int)model.items().indexOf(inserted[0]), 2, "insertLines inserts at the drop row");
	expectEqual((int)model.items().indexOf(inserted[1]), 3, "insertLines keeps the pasted order");
	expectEqual((int)model.selected().size(), 2, "insertLines replaces the selection with the inserted lines");
	expectTrue(model.selected().contains(inserted[0]) && model.selected().contains(inserted[1]),
		"insertLines selects exactly the inserted lines");
	expectTrue(model.focused() == inserted[0], "insertLines focuses the first inserted line");
	expectTrue(model.selectionStart() == inserted[0], "insertLines anchors on the first inserted line");
	expectTrue(inserted[0]->prefs.value("expanded").toBool() && inserted[1]->prefs.isEmpty(),
		"insertLines aligns prefs by index and leaves extra lines without prefs");
	requireFilterListModelInvariants(model, "insertLines");

	// deleteSelected removes exactly the selection, clears it and drops the
	// focus/anchor when they pointed at deleted rows.
	const int countBefore = (int)model.items().size();
	QList<QString> expectedRemaining;
	for (FilterListItem* item : model.items())
	{
		if (!model.selected().contains(item))
			expectedRemaining.append(item->text);
	}
	model.deleteSelected();
	expectEqual((int)model.items().size(), countBefore - 2, "deleteSelected removes exactly the selected rows");
	expectEqual(model.lines().join('\n'), expectedRemaining.join('\n'), "deleteSelected keeps the other rows in order");
	expectTrue(model.selected().isEmpty(), "deleteSelected clears the selection");
	expectTrue(model.focused() == nullptr && model.selectionStart() == nullptr,
		"deleteSelected drops focus/anchor pointing at deleted rows");
	requireFilterListModelInvariants(model, "deleteSelected");

	// selectAll selects every row.
	model.selectAll();
	expectEqual((int)model.selected().size(), (int)model.items().size(), "selectAll selects every row");

	// removeItem moves the selection, focus and anchor onto the neighbouring
	// row (the item now at the removed index).
	model.clearSelection();
	FilterListItem* victim = model.items()[1];
	model.select(victim);
	model.setFocused(victim);
	model.setSelectionStart(victim);
	expectTrue(model.removeItem(victim), "removeItem removes a known item");
	FilterListItem* replacement = model.items()[1];
	expectTrue(model.selected().contains(replacement), "removeItem re-selects the neighbouring row");
	expectTrue(model.focused() == replacement && model.selectionStart() == replacement,
		"removeItem moves focus and anchor to the neighbouring row");
	expectFalse(model.removeItem(nullptr), "removeItem rejects items outside the document");
	requireFilterListModelInvariants(model, "removeItem");

	// Batch removal has no individual replacement semantics, but must commit
	// ownership, projection and observer state together.
	FilterListModel batchModel;
	batchModel.setLines(QList<QString>() << "one" << "two" << "three");
	FilterListItem* removed = batchModel.items()[1];
	batchModel.select(removed);
	batchModel.setFocused(removed);
	batchModel.setSelectionStart(removed);
	batchModel.removeItems(QSet<FilterListItem*>() << removed);
	requireFilterListModelInvariants(batchModel, "removeItems");
	if (batchModel.lines() != (QList<QString>() << "one" << "three"))
		throw std::runtime_error("FilterListModel removeItems order invariant failed");

	// Invalid external anchors and drop rows must be normalized before the
	// owner vector is indexed; this is both a public-API guard and a regression
	// test for the former begin() - 1 undefined behavior.
	FilterListModel boundaryModel;
	boundaryModel.setLines(QList<QString>() << "middle");
	FilterListItem externalAnchor("external");
	const FilterListItem* appendedAtBoundary = boundaryModel.addLine("end", &externalAnchor);
	boundaryModel.insertLines(QStringList() << "front", {}, -100);
	boundaryModel.insertLines(QStringList() << "tail", {}, 100);
	requireFilterListModelInvariants(boundaryModel, "boundary normalization");
	if (boundaryModel.lines() != (QList<QString>() << "front" << "middle" << "end" << "tail")
		|| boundaryModel.items()[2] != appendedAtBoundary)
	{
		throw std::runtime_error("FilterListModel boundary normalization failed");
	}

	// moveItems: the drag-move document mutation. Same final order as
	// insert-copies-then-remove-originals, but the items survive - the card
	// path re-seats their widgets on this guarantee.
	{
		FilterListModel moveModel;
		moveModel.setLines(QList<QString>() << "a" << "b" << "c" << "d");
		FilterListItem* movedDown = moveModel.items()[1];

		// One row down: dropRow counts pre-move rows, so "before d" lands b
		// behind c.
		expectTrue(moveModel.moveItems({ movedDown }, 3), "moveItems accepts a document item");
		expectEqual(moveModel.lines(), QStringList() << "a" << "c" << "b" << "d",
			"moveItems moves one row down");
		expectTrue(moveModel.items()[2] == movedDown, "moveItems keeps the moved item alive");
		expectEqual((int)moveModel.selected().size(), 1, "moveItems selects exactly the moved rows");
		expectTrue(moveModel.selected().contains(movedDown), "moveItems selects the moved row");
		expectTrue(moveModel.focused() == movedDown && moveModel.selectionStart() == movedDown,
			"moveItems focuses and anchors the first moved row");
		requireFilterListModelInvariants(moveModel, "moveItems down");

		// Back up: drop before the row above.
		expectTrue(moveModel.moveItems({ movedDown }, 1), "moveItems accepts the move back up");
		expectEqual(moveModel.lines(), QStringList() << "a" << "b" << "c" << "d",
			"moveItems moves one row back up");
		requireFilterListModelInvariants(moveModel, "moveItems up");

		// A non-contiguous multi-selection lands as one block, in document
		// order, with the insertion point shifted for rows that sat above it.
		FilterListItem* first = moveModel.items()[0];
		FilterListItem* third = moveModel.items()[2];
		expectTrue(moveModel.moveItems({ first, third }, 1), "moveItems accepts a non-contiguous block");
		expectEqual(moveModel.lines(), QStringList() << "a" << "c" << "b" << "d",
			"moveItems lands a non-contiguous selection as one ordered block");
		expectTrue(moveModel.items()[0] == first && moveModel.items()[1] == third,
			"moveItems keeps non-contiguous items alive in document order");
		expectEqual((int)moveModel.selected().size(), 2, "moveItems selects the whole moved block");
		requireFilterListModelInvariants(moveModel, "moveItems non-contiguous");

		// Equivalence with the old copy splice for the same mutation.
		FilterListModel referenceModel;
		referenceModel.setLines(QList<QString>() << "a" << "b" << "c" << "d");
		QList<FilterListItem*> referenceMoved
			= { referenceModel.items()[0], referenceModel.items()[2] };
		QStringList referenceTexts;
		for (FilterListItem* item : referenceMoved)
			referenceTexts.append(item->text);
		referenceModel.insertLines(referenceTexts, {}, 1);
		referenceModel.removeItems(QSet<FilterListItem*>(referenceMoved.cbegin(), referenceMoved.cend()));
		expectEqual(moveModel.lines(), referenceModel.lines(),
			"moveItems matches the insert-copies-then-remove-originals order");

		// Boundary drops clamp; a drop inside the moved block is an order
		// no-op but still commits (selection, focus, anchor).
		moveModel.setLines(QList<QString>() << "a" << "b" << "c");
		expectTrue(moveModel.moveItems({ moveModel.items()[2] }, -5), "moveItems clamps a negative drop row");
		expectEqual(moveModel.lines(), QStringList() << "c" << "a" << "b", "a clamped drop lands at the front");
		expectTrue(moveModel.moveItems({ moveModel.items()[0] }, 99), "moveItems clamps an oversized drop row");
		expectEqual(moveModel.lines(), QStringList() << "a" << "b" << "c", "a clamped drop lands at the end");
		FilterListItem* stationary = moveModel.items()[1];
		expectTrue(moveModel.moveItems({ stationary }, 1), "a drop inside the moved block still commits");
		expectEqual(moveModel.lines(), QStringList() << "a" << "b" << "c", "a same-place drop keeps the order");
		expectTrue(moveModel.selected().contains(stationary) && moveModel.focused() == stationary,
			"a same-place drop still selects and focuses the row");
		requireFilterListModelInvariants(moveModel, "moveItems boundaries");

		// Rejections mutate nothing.
		FilterListItem outsider("outsider");
		expectFalse(moveModel.moveItems({ &outsider }, 0), "moveItems rejects items outside the document");
		expectFalse(moveModel.moveItems({}, 0), "moveItems rejects an empty list");
		expectFalse(moveModel.moveItems({ stationary, stationary }, 0), "moveItems rejects duplicates");
		expectEqual(moveModel.lines(), QStringList() << "a" << "b" << "c", "rejected moves leave the document unchanged");
		requireFilterListModelInvariants(moveModel, "moveItems rejections");
	}
}

// FilterListUndo: the widget-free undo/redo history FilterTable commits to on
// every linesChanged tick.
void testFilterListUndo()
{
	FilterListUndo history;
	QList<QString> doc = QList<QString>() << "Preamp: -6 dB" << "Include: a.txt";

	// A fresh history has nothing to step to, and stepping anyway returns the
	// current state unchanged.
	history.reset(doc);
	expectFalse(history.canUndo(), "reset starts without undo steps");
	expectFalse(history.canRedo(), "reset starts without redo steps");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "undo without steps returns the current state");
	expectEqual(history.redo().join('\n'), doc.join('\n'), "redo without steps returns the current state");

	// A structural change records one step; undo returns the prior state and
	// redo returns to the mutated state.
	QList<QString> withDelay = doc;
	withDelay.append("Delay: 10 ms");
	history.commit(withDelay);
	expectTrue(history.canUndo(), "a commit records an undo step");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "undo returns the state before the commit");
	expectTrue(history.canRedo(), "undo arms redo");
	expectEqual(history.redo().join('\n'), withDelay.join('\n'), "redo returns the undone state");

	// Committing an unchanged document records nothing.
	history.commit(withDelay);
	expectTrue(history.canUndo() && !history.canRedo(), "a no-op commit records nothing but keeps history");

	// A fresh commit after undo discards the redo branch (the standard linear
	// history rule).
	history.undo();
	QList<QString> withPreamp2 = doc;
	withPreamp2.append("Preamp: -2 dB");
	history.commit(withPreamp2);
	expectFalse(history.canRedo(), "a commit after undo discards the redo branch");
	expectEqual(history.undo().join('\n'), doc.join('\n'), "the new branch undoes to the shared ancestor");
	history.redo();

	// Single-line edit runs (knob drag / typing against one row) coalesce
	// into one step per row, and a different mutation breaks the run.
	history.reset(doc);
	QList<QString> drag = doc;
	drag[0] = "Preamp: -5 dB";
	history.commit(drag);
	drag[0] = "Preamp: -4 dB";
	history.commit(drag);
	drag[0] = "Preamp: -3 dB";
	history.commit(drag);
	expectEqual(history.undo().join('\n'), doc.join('\n'), "an edit run against one line undoes as a single step");
	expectFalse(history.canUndo(), "the coalesced run is exactly one step");
	history.redo();
	QList<QString> secondRow = drag;
	secondRow[1] = "Include: b.txt";
	history.commit(secondRow);
	QList<QString> firstRowAgain = secondRow;
	firstRowAgain[0] = "Preamp: -1 dB";
	history.commit(firstRowAgain);
	expectEqual(history.undo().join('\n'), secondRow.join('\n'), "an edit of a different line starts a new step");

	// An edit run that lands back on its starting state cancels the step
	// instead of recording a no-op.
	history.reset(doc);
	QList<QString> toggled = doc;
	toggled[0] = "# Preamp: -6 dB";
	history.commit(toggled);
	history.commit(doc);
	expectFalse(history.canUndo(), "an edit run returning to the start cancels its step");

	// After an undo the same line starts a fresh step, so redo history and
	// the restored state are not silently folded together.
	history.reset(doc);
	QList<QString> editA = doc;
	editA[0] = "Preamp: -5 dB";
	history.commit(editA);
	history.undo();
	QList<QString> editB = doc;
	editB[0] = "Preamp: -4 dB";
	history.commit(editB);
	expectEqual(history.undo().join('\n'), doc.join('\n'), "an edit after undo is a fresh step, not a fold-in");
}
