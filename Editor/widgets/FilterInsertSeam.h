#pragma once

#include <QWidget>

// The hover-only insertion seam above the first card of the modern filter
// list. With the header "+" inserting BELOW its card (shared insertion
// contract, docs/skins/README.md), the very front of the list needs one
// direct entry point; this widget is it. It floats over the top margin of
// the first row, paints nothing at rest, and reveals a skin-drawn seam
// (ISkin::paintInsertSeam) while the cursor is inside. A click opens the
// filter picker and inserts the chosen line at index 0. Never constructed in
// LegacyRows mode.
class FilterInsertSeam : public QWidget
{
	Q_OBJECT

public:
	explicit FilterInsertSeam(QWidget* parent = nullptr);

signals:
	// Emitted on left click. The receiver (FilterTable) anchors the filter
	// picker at this seam and inserts the chosen line at the front.
	void activated();

protected:
	void paintEvent(QPaintEvent* event) override;
	void enterEvent(QEnterEvent* event) override;
	void leaveEvent(QEvent* event) override;
	void mousePressEvent(QMouseEvent* event) override;
	void mouseReleaseEvent(QMouseEvent* event) override;

private:
	bool hovered = false;
	bool pressed = false;
};
