#pragma once
#include "LogControl.h"
#include <wx/filename.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/wx.h>

// ============================================================================
// Execute command and capture output (cross‑platform)
// ============================================================================
inline wxString ExecAndGetOutput(const wxString &cmd) {
  wxArrayString output;
  long code = wxExecute(cmd, output, wxEXEC_SYNC);

  if (code == -1)
    return "";

  wxString result;
  for (auto &line : output)
    result += line + "\n";

  return result;
}

// ============================================================================
// Invert bitmap colors (for dark theme)
// ============================================================================
inline wxBitmap InvertBitmap(const wxBitmap &src) {
  wxImage img = src.ConvertToImage();
  unsigned char *data = img.GetData();

  int total = img.GetWidth() * img.GetHeight() * 3;
  for (int i = 0; i < total; i += 3) {
    data[i] = 255 - data[i];
    data[i + 1] = 255 - data[i + 1];
    data[i + 2] = 255 - data[i + 2];
  }

  return wxBitmap(img);
}

// ============================================================================
// Load SVG icon from ./icons/ and recolor if needed
// ============================================================================
inline wxBitmapBundle LoadSvgIcon(const wxString &name, wxWindow *win) {
  int size = win ? win->FromDIP(24) : 24;
  wxSize iconSize(size, size);

  wxString exeDir =
      wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath();
  wxString path = exeDir + "/icons/" + name + ".svg";

  //LOG_DEBUG("SvgIcon: loading %s", path);

  if (!wxFileExists(path)) {
    LOG_WARN("SvgIcon: NOT FOUND %s", path);
    return wxBitmapBundle();
  }

  wxBitmapBundle bundle = wxBitmapBundle::FromSVGFile(path, iconSize);
  if (!bundle.IsOk()) {
    LOG_WARN("SvgIcon: FAILED to load %s", path);
    return wxBitmapBundle();
  }

  // Dark theme → invert rendered bitmap
  if (wxSystemSettings::GetAppearance().IsDark()) {
    wxBitmap bmp = bundle.GetBitmap(iconSize);
    wxBitmap inverted = InvertBitmap(bmp);
    return wxBitmapBundle::FromBitmaps({inverted});
  }

  return bundle;
}
