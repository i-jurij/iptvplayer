#ifndef ADD_EPG_SOURCE_DIALOG_H
#define ADD_EPG_SOURCE_DIALOG_H

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/textctrl.h>

class AddEpgSourceDialog : public wxDialog {
public:
  AddEpgSourceDialog(wxWindow *parent);

  wxString GetUrlOrPath() const;
  wxString GetName() const;
  bool IsFileSource() const;

private:
  wxTextCtrl *m_urlCtrl;
  wxTextCtrl *m_nameCtrl;
  wxButton *m_browseBtn;
  wxButton *m_infoBtn;

  void OnBrowse(wxCommandEvent &event);
  void OnInfo(wxCommandEvent &event);
  void OnOk(wxCommandEvent &event);
  void AutoFillName(wxCommandEvent &event);
  void ShowInfoDialog();
};

#endif