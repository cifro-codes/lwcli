// Copyright (c) 2025, Cifro Codes LLC
// 
// All rights reserved.
// 
// Redistribution and use in source and binary forms, with or without modification, are
// permitted provided that the following conditions are met:
// 
// 1. Redistributions of source code must retain the above copyright notice, this list of
//    conditions and the following disclaimer.
// 
// 2. Redistributions in binary form must reproduce the above copyright notice, this list
//    of conditions and the following disclaimer in the documentation and/or other
//    materials provided with the distribution.
// 
// 3. Neither the name of the copyright holder nor the names of its contributors may be
//    used to endorse or promote products derived from this software without specific
//    prior written permission.
// 
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
// THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
// STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
// THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#include "keys.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <lws_frontend.h>
#include <string_view>

#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "views/history.h"

namespace lwcli { namespace view
{
  namespace
  {
    class keys_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::Wallet> wal_;
      const ftxui::Element title_;
      const ftxui::Component ui_;
      ftxui::Element warning_;
      ftxui::Element grid_;
      const ftxui::Element seed_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return ui_; }

    public:
      explicit keys_(std::shared_ptr<Monero::Wallet>&& wal, bool show_warning)
        : ftxui::ComponentBase(),
          title_(ftxui::text(_("Wallet Info"))),
          wal_(std::move(wal)),
          ui_(ftxui::Button(_("OK"), [] () { throw event::close{}; }, ftxui::ButtonOption::Ascii())),
          warning_(),
          grid_(),
          seed_(ftxui::hflow(ftxui::paragraph(wal_->seed({}))))
      {
        if (show_warning)
          warning_ = ftxui::inverted(decorate::banner(ftxui::text("WRITE THE 25 \"SEED\" WORDS DOWN TO PREVENT LOSS OF FUNDS")));
        
        const auto size = ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, 5);
        grid_ = ftxui::gridbox({
          {ftxui::text(_("Address: ")), ftxui::text(wal_->mainAddress())},
          {ftxui::text(_("Spend Pub: ")), ftxui::text(wal_->publicSpendKey())},
          {ftxui::text(_("View Pub: ")), ftxui::text(wal_->publicViewKey())},
          {ftxui::text(_("Spend Key: ")), ftxui::text(wal_->secretSpendKey())},
          {ftxui::text(_("View Key: ")), ftxui::text(wal_->secretViewKey())}
        });
      }

      bool OnEvent(ftxui::Event event) override final
      {
        if (event == ftxui::Event::CtrlQ)
          throw event::close{};
        ui_->OnEvent(std::move(event));
        return true;
      }

      ftxui::Element OnRender() override final
      {
        ftxui::Elements vbox;
        vbox.reserve(6);
        if (warning_)
          vbox.push_back(warning_);

        vbox.push_back(seed_);
        vbox.push_back(ftxui::separator());
        vbox.push_back(grid_);
        vbox.push_back(ftxui::separator());
        vbox.push_back(decorate::banner(ui_->Render()));
        
        return ftxui::window(title_, ftxui::vbox(std::move(vbox)));
      }
    };
  }

  ftxui::Component keys(std::shared_ptr<Monero::Wallet> wal, bool show_warning)
  {
    if (!wal)
      throw std::runtime_error{"lwcli::view::keys given nullptr"};
    return std::make_shared<keys_>(std::move(wal), show_warning);
  } 
}} // lwcli // view
