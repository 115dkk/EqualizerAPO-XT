/*
    This file is part of EqualizerAPO, a system-wide equalizer.
    Copyright (C) 2015  Jonas Thedering

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include <algorithm>

#include "FilterGUIFactoryRegistry.h"
#include "IFilterGUIFactory.h"

namespace
{
struct FilterGUIFactoryRegistration
{
	int order = 0;
	FilterGUIFactoryCreator creator = nullptr;
};

// Construct-on-first-use so the list exists no matter the static-init order of
// the factory translation units that fill it.
QList<FilterGUIFactoryRegistration>& registrations()
{
	static QList<FilterGUIFactoryRegistration> registeredFactories;
	return registeredFactories;
}
}

bool FilterGUIFactoryRegistry::registerFactory(int order, FilterGUIFactoryCreator creator)
{
	registrations().append({order, creator});
	return true;
}

QList<IFilterGUIFactory*> FilterGUIFactoryRegistry::createFactories()
{
	QList<FilterGUIFactoryRegistration> sortedRegistrations = registrations();
	std::stable_sort(sortedRegistrations.begin(), sortedRegistrations.end(), [](const FilterGUIFactoryRegistration& left, const FilterGUIFactoryRegistration& right) {
		return left.order < right.order;
	});

	QList<IFilterGUIFactory*> factories;
	factories.reserve(sortedRegistrations.size());
	for (const FilterGUIFactoryRegistration& registration : sortedRegistrations)
		factories.append(registration.creator());

	return factories;
}
