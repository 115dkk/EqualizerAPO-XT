#pragma once

#include <QLabel>
#include <QLineEdit>
#include <QStackedLayout>
#include <QWidget>

class EditableValue : public QWidget
{
	Q_OBJECT

public:
	explicit EditableValue(QWidget* parent = nullptr);

	double value() const;
	void setValue(double value);
	const QString& unit() const;
	void setUnit(const QString& unit);

signals:
	void valueChanged(double value);

protected:
	void mouseDoubleClickEvent(QMouseEvent* event) override;

private slots:
	void commitEdit();

private:
	void refreshText();

	QStackedLayout* stack = nullptr;
	QLabel* displayLabel = nullptr;
	QLineEdit* editField = nullptr;
	double currentValue = 0.0;
	QString currentUnit;
};
