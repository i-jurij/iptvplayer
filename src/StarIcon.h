#pragma once

#include <wx/bitmap.h>
#include <wx/colour.h>
#include <wx/gdicmn.h>

// Залитая звезда (favorite=on).
wxBitmap CreateFilledStarIcon(const wxSize &size = wxSize(128, 128),
                              const wxColour &colour = wxColour(0x00, 0x8d,
                                                                0x58));

// Контурная звезда (favorite=off).
// colour = wxNullColour → авто-выбор по теме (ShouldUseDarkColors()):
//   Light theme — #5d5f60 (тёмно-серый, контраст на белом)
//   Dark theme  — #b0b0b0 (светло-серый, контраст на тёмном)
// Явный цвет — используется как есть.
wxBitmap CreateOutlineStarIcon(const wxSize &size = wxSize(128, 128),
                               const wxColour &colour = wxNullColour,
                               int penWidth = 2);