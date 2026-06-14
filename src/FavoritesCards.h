#pragma once

#include "CardsBase.h"

class FavoritesCards : public CardsBase
{
public:
    FavoritesCards(wxWindow* parent);

    void SetChannels(const std::vector<Channel>& channels);
    void SyncFavorites(const std::vector<std::pair<std::string, std::string>> &favKeys);

private : wxBitmap m_favFilled;

    wxBitmap GetStarBitmap(const Channel& ch) const override;
    void OnCardClick(size_t index, bool fav, const wxRect& rect) override;
};
