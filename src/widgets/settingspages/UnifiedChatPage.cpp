// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/settingspages/UnifiedChatPage.hpp"

#include "singletons/Settings.hpp"
#include "widgets/settingspages/SettingWidget.hpp"

#include <QFormLayout>
#include <QGroupBox>
#include <QVBoxLayout>

namespace chatterino {

UnifiedChatPage::UnifiedChatPage()
    : view(GeneralPageView::withoutNavigation(this))
{
    auto *y = new QVBoxLayout;
    y->setContentsMargins(0, 0, 0, 0);
    y->addWidget(this->view);
    this->setLayout(y);

    this->initLayout(*this->view);
}

bool UnifiedChatPage::filterElements(const QString &query)
{
    return this->view->filterElements(query) || query.isEmpty();
}

void UnifiedChatPage::initLayout(GeneralPageView &layout)
{
    auto &s = *getSettings();

    layout.addTitle("Chat unificado");
    layout.addDescription(
        "Muestra los mensajes de Kick y de YouTube dentro del split de tu "
        "canal de Twitch, con un icono de la plataforma de origen delante del "
        "nombre. Solo lectura: no se envian mensajes a Kick ni a YouTube. "
        "Con esto apagado, Chatterino se comporta exactamente igual que el "
        "original.");

    SettingWidget::checkbox("Activar chat unificado", s.unifiedChatEnabled)
        ->setDescription("Los cambios se aplican al instante, sin reiniciar.")
        ->addTo(layout);

    {
        auto *form = new QFormLayout;
        layout.addLayout(form);

        SettingWidget::lineEdit("Canal de Twitch objetivo",
                                s.unifiedChatTargetTwitchChannel,
                                "(vacio = tu propio canal)")
            ->setDescription("Canal de Twitch que recibe los mensajes de las "
                             "otras plataformas.")
            ->conditionallyEnabledBy(s.unifiedChatEnabled)
            ->addTo(layout, form);

        SettingWidget::checkbox("Mostrar icono de plataforma",
                                s.unifiedChatShowPlatformBadges)
            ->setDescription(
                "Icono de Twitch / Kick / YouTube delante del nombre de "
                "usuario. Solo se muestra cuando al menos una de las otras "
                "plataformas esta activa.")
            ->conditionallyEnabledBy(s.unifiedChatEnabled)
            ->addTo(layout);
    }

    // Kick
    {
        auto *box = new QGroupBox("Kick");
        auto *boxLayout = new QVBoxLayout(box);
        auto *form = new QFormLayout;

        SettingWidget::checkbox("Activar Kick", s.unifiedChatKickEnabled)
            ->addToLayout(boxLayout);
        boxLayout->addLayout(form);
        SettingWidget::lineEdit("Canal de Kick (slug)",
                                s.unifiedChatKickChannelSlug, "ej. xqc")
            ->setDescription("El nombre que aparece en la URL kick.com/<slug>.")
            ->conditionallyEnabledBy(s.unifiedChatKickEnabled)
            ->addToLayout(form);
        SettingWidget::lineEdit("Chatroom id (opcional)",
                                s.unifiedChatKickChatroomId,
                                "solo si Kick bloquea la consulta automatica")
            ->conditionallyEnabledBy(s.unifiedChatKickEnabled)
            ->addToLayout(form);

        s.unifiedChatEnabled.connect(
            [box](const bool &value, const auto &) {
                box->setEnabled(value);
            },
            this->managedConnections_);
        layout.addWidget(box, {"kick", "chatroom", "slug"});
    }

    // YouTube
    {
        auto *box = new QGroupBox("YouTube");
        auto *boxLayout = new QVBoxLayout(box);
        auto *form = new QFormLayout;

        SettingWidget::checkbox("Activar YouTube", s.unifiedChatYouTubeEnabled)
            ->addToLayout(boxLayout);
        boxLayout->addLayout(form);
        SettingWidget::lineEdit("Canal o video de YouTube",
                                s.unifiedChatYouTubeSource,
                                "@handle, URL del canal/video o video ID")
            ->setDescription(
                "Con un handle o URL de canal se busca la transmision en "
                "vivo automaticamente.")
            ->conditionallyEnabledBy(s.unifiedChatYouTubeEnabled)
            ->addToLayout(form);
        SettingWidget::lineEdit("API key de YouTube Data (opcional)",
                                s.unifiedChatYouTubeApiKey,
                                "respaldo; consume cuota diaria")
            ->conditionallyEnabledBy(s.unifiedChatYouTubeEnabled)
            ->addToLayout(form);

        s.unifiedChatEnabled.connect(
            [box](const bool &value, const auto &) {
                box->setEnabled(value);
            },
            this->managedConnections_);
        layout.addWidget(box, {"youtube", "live", "api key"});
    }

    layout.addStretch();
}

}  // namespace chatterino
