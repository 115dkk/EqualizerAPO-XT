#pragma once

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QStackedWidget>
#include <QToolButton>
#include <QWidget>

#include "Editor/FilterTable.h"
#include "Editor/widgets/FilterCardModel.h"

class FilterCardRow : public QWidget
{
	Q_OBJECT

public:
	FilterCardRow(FilterTable* table, int number, FilterTable::Item* item, IFilterGUI* gui, int depth, QWidget* parent = nullptr);

	QRect getHeaderRect() const;
	void editText();
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;

private slots:
	void updateModel();
	void addBefore();
	void removeThis();
	void editTextToggled(bool checked);
	void lineEditingFinished();
	void enabledToggled(bool checked);
	void expandedToggled(bool checked);

private:
	void rebuildSummary();
	void setEditing(bool editing);
	void buildChannelBadges(const QStringList& channels);
	QString uncommentedLine() const;

	FilterTable* table = nullptr;
	FilterTable::Item* item = nullptr;
	IFilterGUI* gui = nullptr;
	FilterCardDescriptor descriptor;

	QFrame* cardFrame = nullptr;
	QWidget* headerWidget = nullptr;
	QLabel* numberLabel = nullptr;
	QLabel* typeBadge = nullptr;
	QLabel* titleLabel = nullptr;
	QLabel* summaryLabel = nullptr;
	QWidget* channelBadgeContainer = nullptr;
	QHBoxLayout* channelBadgeLayout = nullptr;
	QCheckBox* enabledCheckBox = nullptr;
	QToolButton* expandButton = nullptr;
	QToolButton* addButton = nullptr;
	QToolButton* removeButton = nullptr;
	QToolButton* editButton = nullptr;
	QStackedWidget* bodyStack = nullptr;
	QLineEdit* lineEdit = nullptr;
	bool editingDone = false;
};
