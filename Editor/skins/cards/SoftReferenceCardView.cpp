#include "SoftReferenceCardView.h"

SoftReferenceCardView::SoftReferenceCardView(const QString& kind, QWidget* parent)
	: DefaultReferenceCardView(parent)
{
	Q_UNUSED(kind);
}
