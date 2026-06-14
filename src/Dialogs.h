#ifndef DIALOGS_H
#define DIALOGS_H

#include "Playlist.h"

#include <wx/wx.h>
#include <wx/dialog.h>
#include <wx/filepicker.h>
#include <wx/textctrl.h>

// ============================================================================
// AddPlaylistFileDialog
// ============================================================================
class AddPlaylistFileDialog : public wxDialog
{
public:
    explicit AddPlaylistFileDialog(wxWindow* parent);
    wxString GetFilePath() const { return m_filePath; }
    wxString GetTitle()    const { return m_title; }

private:
    void OnOK    (wxCommandEvent&);
    void OnCancel(wxCommandEvent&);  // opens extra file‑open dialog

    wxFilePickerCtrl* m_filePicker;   // replaces the plain text control
    wxTextCtrl*       m_titleCtrl;
    wxString          m_filePath;     // filled after OK
    wxString          m_title;        // filled after OK
    
        enum {
        ID_BtnOk     = wxID_HIGHEST + 1,
        ID_BtnCancel
    };

    wxDECLARE_EVENT_TABLE();
};


// ============================================================================
// AddPlaylistUrlDialog
// ============================================================================
class AddPlaylistUrlDialog : public wxDialog {
public:
    explicit AddPlaylistUrlDialog(wxWindow* parent);

    wxString GetUrl() const { return m_url; }
    wxString GetTitle() const { return m_title; }
    wxString GetUserAgent() const { return m_userAgent; }

private:
    void initializeControls();
    void setupLayout();
    void bindEvents();
    wxSizer* createInputField(const wxString& label, wxTextCtrl* ctrl);

    void onOK(wxCommandEvent& event);

    wxTextCtrl* m_urlCtrl;
    wxTextCtrl* m_titleCtrl;
    wxTextCtrl* m_userAgentCtrl;

    wxString m_url;
    wxString m_title;
    wxString m_userAgent;
};

// ============================================================================
// EditPlaylistDialog
// ============================================================================
class EditPlaylistDialog : public wxDialog {
public:
    EditPlaylistDialog(wxWindow* parent, const wxString& title,
                      const wxString& source, const wxString& userAgent,
                      bool autoUpdate);

    wxString GetTitle() const { return m_title; }
    wxString GetSource() const { return m_source; }
    wxString GetUserAgent() const { return m_userAgent; }
    bool GetAutoUpdate() const { return m_autoUpdate; }

    void SetPlaylistIndex(size_t index) { m_playlistIndex = index; }
    size_t GetPlaylistIndex() const { return m_playlistIndex; }

private:
    void initializeControls();
    void setupLayout();
    void bindEvents();
    wxSizer* createInputField(const wxString& label, wxTextCtrl* ctrl);
    wxSizer* createButtonSection();

    void onOK(wxCommandEvent& event);
    void onExport(wxCommandEvent& event);

    wxTextCtrl* m_titleCtrl;
    wxTextCtrl* m_sourceCtrl;
    wxTextCtrl* m_userAgentCtrl;
    wxCheckBox* m_autoUpdateCheck;

    wxString m_title;
    wxString m_source;
    wxString m_userAgent;
    bool m_autoUpdate;
    size_t m_playlistIndex;
};

bool showRemovePlaylistDialog(wxWindow* parent, const Playlist* playlist, bool& removeSource);

#endif // DIALOGS_H
