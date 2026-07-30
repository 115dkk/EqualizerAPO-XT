/*
	This file is part of EqualizerAPO-XT, a system-wide equalizer.

	BassManagementCardView is the skin seam for the compact summary of a
	BassManagement command. The editor owns parsing, validation and actions;
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

struct BassManagementCardState
{
	bool enabled = true;
	bool valid = false;
	QString layoutLabel;
	bool sourceLfePreserved = false;
	double sourceLfeGainDb = 0.0;
	int speakerGroupCount = 0;
	int bassPathCount = 0;
	QString representativeHighPass;
	QString representativeLowPass;
	int activeMatrixEdges = 0;
	bool headroomAuto = true;
	double headroomTrimDb = 0.0;
	QString profileName;
	bool linkedProfile = false;
	bool profileMissing = false;
	QString warningText;
	QString errorText;
};

class BassManagementCardView : public QWidget
{
	Q_OBJECT

public:
	explicit BassManagementCardView(QWidget* parent = nullptr);

	void setState(const BassManagementCardState& state);
	const BassManagementCardState& state() const;

	virtual void addActionButton(QAbstractButton* button) = 0;

signals:
	void openEditorRequested();

protected:
	virtual void applyState(const BassManagementCardState& state) = 0;

	void mouseDoubleClickEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;

private:
	BassManagementCardState currentState;
};

class DefaultBassManagementCardView : public BassManagementCardView
{
	Q_OBJECT

public:
	explicit DefaultBassManagementCardView(QWidget* parent = nullptr);

	void addActionButton(QAbstractButton* button) override;

protected:
	void applyState(const BassManagementCardState& state) override;

private:
	void addReadoutRow(int row, const QString& caption, QLabel*& valueLabel,
		const QString& accessibleName, const QString& toolTip);

	QGridLayout* grid = nullptr;
	QHBoxLayout* actionLayout = nullptr;
	QLabel* layoutValue = nullptr;
	QLabel* pathsValue = nullptr;
	QLabel* crossoverValue = nullptr;
	QLabel* sourceLfeValue = nullptr;
	QLabel* headroomValue = nullptr;
	QLabel* profileValue = nullptr;
	QLabel* statusLabel = nullptr;
};
