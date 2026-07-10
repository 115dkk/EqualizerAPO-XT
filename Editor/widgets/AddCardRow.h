#pragma once

#include <QWidget>

// The persistent "add card" row at the end of the modern filter list. It
// replaces the legacy green-icon QToolBar in the card path. The widget owns
// all input handling - click,
// Space/Return, focus - and delegates every pixel to the active skin through
// ISkin::paintAddRow, so each skin answers the affordance in its own grammar
// (docs/skins/README.md, shared insertion contract). LegacyRows keeps the
// frozen toolbar and never constructs this widget.
class AddCardRow : public QWidget
{
	Q_OBJECT

public:
	explicit AddCardRow(QWidget* parent = nullptr);

	QSize sizeHint() const override;
	QSize minimumSizeHint() const override;

signals:
	// Emitted on click or keyboard activation. The receiver (FilterTable)
	// anchors the filter picker below this row and appends the chosen line.
	void activated();

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;
	void keyPressEvent(QKeyEvent* event) override;
	void focusInEvent(QFocusEvent* event) override;
	void focusOutEvent(QFocusEvent* event) override;

private:
	bool hovered = false;
	bool pressed = false;
};
