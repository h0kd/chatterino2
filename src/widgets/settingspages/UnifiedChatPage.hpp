// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

// UNIFIED-CHAT: settings page for the Twitch + Kick + YouTube unified chat.
#pragma once

#include "widgets/settingspages/GeneralPageView.hpp"
#include "widgets/settingspages/SettingsPage.hpp"

namespace chatterino {

class UnifiedChatPage : public SettingsPage
{
public:
    UnifiedChatPage();

    bool filterElements(const QString &query) override;

private:
    void initLayout(GeneralPageView &layout);

    GeneralPageView *view{};
};

}  // namespace chatterino
