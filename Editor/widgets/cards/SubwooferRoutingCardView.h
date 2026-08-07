/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	SubwooferRoutingCardView is the skin seam for the compact summary of a
	SubwooferRouting command. The editor owns parsing, validation and actions;
	the view owns only structure and presentation.
*/

#pragma once

#include <QString>
#include <QWidget>

class QAbstractButton;
class QGridLayout;
class QHBoxLayout;
class QLabel;
class QKeyEvent;
class QMouseEvent;

// The card speaks the user's language: layout, crossover corner, source-LFE
// handling, headroom and profile. Internal graph statistics (group, path and
// matrix-edge counts) belong to the full editor, not the summary card.
//
// Status contract (review round 2): errorText is the single error sentence
// and warningText the single advisory sentence. The editor guarantees that
// valid == false implies a non-empty errorText, and that a missing linked
// profile is already described by errorText. Views must render each text at
// most once and must not synthesize additional error lines from `valid` or
// `profileMissing` - those flags only drive badge tone and short badges.
struct SubwooferRoutingCardState
{
	bool enabled = true;
	bool valid = false;
	QString layoutLabel;
	bool sourceLfePreserved = false;
	double sourceLfeGainDb = 0.0;
	// Representative crossover corners; 0.0 when the state has no section of
	// that type (a full-range layout). The slope labels carry the recognized
	// alignment ("BW2", "LR4"); empty for custom chains.
	double highPassHz = 0.0;
	double lowPassHz = 0.0;
	QString highPassSlope;
	QString lowPassSlope;
	bool headroomAuto = true;
	double headroomTrimDb = 0.0;
	QString profileName;
	bool linkedProfile = false;
	bool profileMissing = false;
	QString warningText;
	QString errorText;
};

class SubwooferRoutingCardView : public QWidget
{
	Q_OBJECT

public:
	explicit SubwooferRoutingCardView(QWidget* parent = nullptr);

	void setState(const SubwooferRoutingCardState& state);
	const SubwooferRoutingCardState& state() const;

	virtual void addActionButton(QAbstractButton* button) = 0;

	// Shared formatting so the five skins agree on the numbers even though
	// each dresses them in its own grammar.
	static QString formatHz(double hz);
	static QString crossoverSummary(const SubwooferRoutingCardState& state);

signals:
	void openEditorRequested();

protected:
	virtual void applyState(const SubwooferRoutingCardState& state) = 0;

	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	SubwooferRoutingCardState currentState;
};

class DefaultSubwooferRoutingCardView : public SubwooferRoutingCardView
{
	Q_OBJECT

public:
	explicit DefaultSubwooferRoutingCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const SubwooferRoutingCardState& state) override;

private:
	void addReadoutRow(int row, const QString& caption, QLabel*& valueLabel,
		const QString& accessibleName, const QString& toolTip);

	QGridLayout* grid = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* layoutValue = nullptr;
	QLabel* crossoverValue = nullptr;
	QLabel* sourceLfeValue = nullptr;
	QLabel* headroomValue = nullptr;
	QLabel* profileValue = nullptr;
	QLabel* statusLabel = nullptr;
};
