/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.
	Copyright (C) 2026 115dkk
	SPDX-License-Identifier: GPL-2.0-or-later
*/

/*
	The head of a card's routing section, for the cards whose routing is one
	part of a larger body (Send, MultiConvolution). From the left: the fold
	button, the section's caption, the controls that only make sense while
	the section is open, and the summary a folded section leaves in its
	place.

	Nothing here is a new look. The fold button is the card header's own
	(FilterCardIconButton with headerAction, "v" open and ">" folded), so a
	section folds by the sign a card folds by, and the caption keeps the
	object name each skin already dresses.

	Folding is a view state. It never changes the line: the head hides the
	body it was given and shows the routing in the line's own grammar
	instead, so a folded section still says everything it routes. The board
	itself stays constructed under the fold, which is what lets the gates and
	the gallery reach it in either state.

	The owning editor decides the default (open) and remembers a fold the
	user chose through its row preferences; the head only reports the choice.
*/

#pragma once

#include <QList>
#include <QPointer>
#include <QString>
#include <QWidget>

class ElidedLabel;
class QAbstractButton;
class QHBoxLayout;
class QLabel;
class QToolButton;

class RoutingSectionHead : public QWidget
{
	Q_OBJECT

public:
	// captionObjectName is the name the skins' sheets style the caption by
	// (MultiConvolutionMappingCaption, SendFieldCaption).
	RoutingSectionHead(const QString& caption, const QString& captionObjectName, QWidget* parent = nullptr);

	// The widget the fold button folds away: the routing view, or the scroll
	// around it. Not owned. The editors rebuild their view when the channels
	// in scope change, so they hand the new one over here and it takes the
	// current fold state.
	void setBody(QWidget* body);

	// A control that belongs to the open section only (MultiConvolution's
	// add-output button). It sits right after the caption.
	void addOpenControl(QWidget* control);

	// What a folded section shows in place of its body: the routing as the
	// line writes it. An empty text shows the "no connections" wording, so a
	// folded section is never blank.
	void setSummary(const QString& text);
	// The text a folded section shows, in full (the label elides it).
	QString summaryText() const;

	bool isFolded() const;
	// Sets the state without announcing it: the default, or a stored choice.
	void setFolded(bool folded);

	// The fold button, for the gates that drive it.
	QAbstractButton* foldButton() const;

signals:
	// The user folded or opened the section.
	void foldChosen(bool folded);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
	void toggleClicked();

private:
	void apply();

	QHBoxLayout* row = nullptr;
	QToolButton* toggle = nullptr;
	QLabel* captionLabel = nullptr;
	ElidedLabel* summary = nullptr;
	QList<QPointer<QWidget>> openControls;
	QPointer<QWidget> body;
	bool folded = false;
};
