#pragma once

#include <QWidget>

class EqGraphView : public QWidget
{
	Q_OBJECT

public:
	explicit EqGraphView(QWidget* parent = nullptr);

	void setChannel(const QString& channel);
	QString channel() const;
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;

private:
	QString currentChannel = QStringLiteral("All");
};
