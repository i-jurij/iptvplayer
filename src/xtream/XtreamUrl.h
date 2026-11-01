#pragma once

#include <functional>

#include <wx/string.h>

namespace xtream {

enum class Format {
  M3uPlusTs,
  M3uPlusHls,
};

wxString BuildM3u(const wxString &base, const wxString &user,
                  const wxString &pass, Format format);

wxString BuildXmltv(const wxString &base, const wxString &user,
                    const wxString &pass);

using ProbeCallback = std::function<void(bool ok, const wxString &finalUrl,
                                         const wxString &error)>;

void ProbeAsync(const wxString &baseUrl, const wxString &user,
                const wxString &pass, ProbeCallback callback);

} // namespace xtream
