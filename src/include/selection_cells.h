//       _________ __                 __
//      /   _____//  |_____________ _/  |______     ____  __ __  ______
//      \_____  \   __\_  __ \__  \   __\__  \   / ___\|  |  \/  ___/
//      /        \|  |  |  | \// __ \|  |  / __ \_/ /_/  >  |  /\___ |
//     /_______  /|__|  |__|  (____  /__| (____  /\___  /|____//____  >
//             \/                  \/          \//_____/            \/
//  ______________________                           ______________________
//                        T H E   W A R   B E G I N S
//         Stratagus - A free fantasy real time strategy game engine
//
/**@name selection_cells.h - The kept cells of the group panel. */
//
//      This program is free software; you can redistribute it and/or modify
//      it under the terms of the GNU General Public License as published by
//      the Free Software Foundation; only version 2 of the License.
//
//      This program is distributed in the hope that it will be useful,
//      but WITHOUT ANY WARRANTY; without even the implied warranty of
//      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//      GNU General Public License for more details.
//


#ifndef SELECTION_CELLS_H
#define SELECTION_CELLS_H

#include <algorithm>
#include <vector>

/// The cells of the group panel are kept places, never packed again while the selection goes on
/// (measured in the original's Remastered view: run 33 item 6, eight steps; run 30 for deaths):
///   - a unit that is no longer selected (it died, or the new selection leaves it out) leaves
///     its cell empty, the others stay where they are;
///   - a unit of the new selection that had a cell keeps it; the units new to the selection take
///     the lowest free cells in the selection's order, behind the last cell when none is free;
///   - only a selection that shares no unit with the cells before starts again from the first cell.
/// `previous`: the cells so far (`empty` = no unit); `selected`: the selection now, in its order.
/// The number of cells never shrinks while a unit keeps its cell, so the grid keeps its columns.
/// Presentation only: the selection itself is not touched.
template <typename T>
std::vector<T> KeepSelectionCells(const std::vector<T> &previous, const std::vector<T> &selected, T empty = T())
{
	std::vector<T> cells(previous);
	bool shared = false;
	for (T &cell : cells) {
		if (cell == empty) {
			continue;
		}
		if (std::find(selected.begin(), selected.end(), cell) == selected.end()) {
			cell = empty;
		} else {
			shared = true;
		}
	}
	if (!shared) {
		return selected;
	}
	for (const T &unit : selected) {
		if (std::find(cells.begin(), cells.end(), unit) != cells.end()) {
			continue;
		}
		const auto free = std::find(cells.begin(), cells.end(), empty);
		if (free != cells.end()) {
			*free = unit;
		} else {
			cells.push_back(unit);
		}
	}
	return cells;
}

/// The selection changed: bring the panel's cells up to it (also done whenever the cells are read).
void SelectionCellsUpdate();

#endif // SELECTION_CELLS_H
