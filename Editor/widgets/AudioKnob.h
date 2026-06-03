#pragma once

#include <QDial>

class AudioKnob : public QDial
{
	Q_OBJECT

public:
	explicit AudioKnob(QWidget* parent = nullptr);

	void setValueText(const QString& text);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent*) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseMoveEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	QPointF pointOnArc(const QRectF& rect, double degrees) const;
	void setValueFromAngle(const QPointF& widgetPos);

	QString text;
};
