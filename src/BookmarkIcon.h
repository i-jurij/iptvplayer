#pragma once

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/gdicmn.h>

// Залитая закладка (watchlist=on).
wxBitmap CreateFilledBookmarkIcon(
    const wxSize &size = wxSize(128, 128),
    const wxColour &colour = wxColour(0x1e, 0x88, 0xe5)); // accent blue

// Контурная закладка (watchlist=off).
// colour = wxNullColour → авто-выбор по теме:
//   Light — #5d5f60, Dark — #b0b0b0 (как StarIcon outline).
wxBitmap CreateOutlineBookmarkIcon(const wxSize &size = wxSize(128, 128),
                                   const wxColour &colour = wxNullColour,
                                   int penWidth = 2);
                                   