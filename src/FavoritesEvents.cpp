#include "Channel.h"
#include "MainFrame.h"
#include "EventIDs.h"

#include <wx/msgdlg.h>

void MainFrame::onFavoriteSelected(const Channel& ch, size_t /*index*/, const wxRect& /*rect*/) {
  PlayChannel(ch);
}
