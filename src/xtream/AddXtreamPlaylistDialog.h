#pragma once

#include <wx/checkbox.h>
#include <wx/combobox.h>
#include <wx/dialog.h>
#include <wx/stattext.h>
#include <wx/string.h>
#include <wx/textctrl.h>

class AddXtreamPlaylistDialog : public wxDialog {
public:
  explicit AddXtreamPlaylistDialog(wxWindow *parent);

  wxString GetServerUrl() const; // нормализованный (http:// + trim)
  wxString GetUsername() const;
  wxString GetPassword() const;
  wxString GetTitle() const;  // host[:port] если пусто
  wxString GetUserAgent() const; // пустое поле → xtream::DefaultUserAgent()
  wxString GetEpgUrl() const; // авто-ген xmltv если пусто и creds есть
  bool GetAutoUpdate() const;
  wxString GetProbeServer() const { return m_probeServer; }
  wxString GetProbeUser() const { return m_probeUser; }
  wxString GetProbePass() const { return m_probePass; }

private:
  void OnOK(wxCommandEvent &);
  void OnServerUrlChanged(wxCommandEvent &);
  void ShowInlineError(const wxString &msg);
  void ClearInlineError();

  wxTextCtrl *m_serverCtrl = nullptr;
  wxTextCtrl *m_userCtrl = nullptr;
  wxTextCtrl *m_passCtrl = nullptr;
  wxTextCtrl *m_titleCtrl = nullptr;
  wxComboBox *m_uaCombo = nullptr;
  wxTextCtrl *m_epgUrlCtrl = nullptr;
  wxCheckBox *m_autoUpdateCheck = nullptr;
  wxStaticText *m_errorLabel = nullptr;
  // Нормализованные значения, вычисляются в OnOK. Используются MainFrame
  // после успешного ShowModal → onAddXtreamPlaylist → DialProbeAsync.
  wxString m_probeServer;
  wxString m_probeUser;
  wxString m_probePass;
};
