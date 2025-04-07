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

#include "wallet.h"

#include <charconv>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/table.hpp>
#include <lws_frontend.h>

#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "views/history.h"
#include "views/settings.h"

namespace lwcli { namespace view
{
  namespace
  {
    ftxui::ButtonOption ascii() { return ftxui::ButtonOption::Ascii(); }

    struct wallet_state
    {
      const std::shared_ptr<Monero::Wallet> wal;
      ftxui::Component overlay;
    };

    ftxui::Component menu_bar(wallet_state* state)
    {
      const std::shared_ptr<Monero::Wallet> wal = state->wal;
      return ftxui::Container::Horizontal({
        ftxui::Button("[s]end", [] () {}, ascii()),
        ftxui::Button("address [b]ook", [] () {}, ascii()),
        ftxui::Button("[a]ccounts", [] () {}, ascii()),
        ftxui::Button("[r]efresh", [wal] () { wal->refreshAsync(); }, ascii()),
        ftxui::Button("s[e]ttings", [state] () { state->overlay = settings(state->wal); }, ascii())
      });
    }

    class wallet_ final : public ftxui::ComponentBase
    {
      wallet_state state_;
      ftxui::Element title_;
      std::uint32_t active_account_;
      std::uint32_t selected_account_;
      ftxui::Component bar_;
      ftxui::Component ui_;
      ftxui::Component history_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (state_.overlay)
          return state_.overlay; 
        return ui_;
      }

    public:
      explicit wallet_(std::shared_ptr<Monero::Wallet>&& data)
        : ftxui::ComponentBase(),
          state_{std::move(data)},
          title_(nullptr),
          active_account_(-1),
          selected_account_(0),
          bar_(),
          ui_(),
          history_(nullptr)
      {
        if (!state_.wal)
          throw std::runtime_error{"Unexpected nullptr Monero wallet"};
        bar_ = menu_bar(&state_);
        title_ = ftxui::text(_("lwcli Wallet (Primary ") + state_.wal->mainAddress() + ")");
        update_account();
      }

      void update_account()
      {
        if (active_account_ != selected_account_)
        {
          if (ui_)
            ui_->Detach();
          history_ = view::history(state_.wal, selected_account_);
          ui_ = ftxui::Container::Vertical({bar_, history_});
          Add(ui_);
        }
        active_account_ = selected_account_;
      }

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          const bool has_overlay = bool(state_.overlay);

          if (state_.overlay)
            state_.overlay->OnEvent(std::move(event));
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          else if (!ui_->OnEvent(event))
          {
            if (event == ftxui::Event::s || event == ftxui::Event::S)
              ; // state_.overlay = send(state_.wal);
            else if (event == ftxui::Event::b || event == ftxui::Event::B)
              ; // state_.overlay = book(state_.wal);
            else if (event == ftxui::Event::a || event == ftxui::Event::a)
              ; // overlay = accounts(state_.wal, &active_account_);
            else if (event == ftxui::Event::r || event == ftxui::Event::R)
              state_.wal->refreshAsync();
            else if (event == ftxui::Event::e || event == ftxui::Event::E)
              state_.overlay = settings(state_.wal);
          }

          if (!has_overlay && state_.overlay)
            Add(state_.overlay);

          update_account();
        }
        catch (const event::close&)
        {
          if (!state_.overlay)
            throw;
          state_.overlay->Detach();
          state_.overlay.reset();
        }

        return true;
      }

      ftxui::Element OnRender() override final
      {
        const bool connected =
          state_.wal->connected() == Monero::Wallet::ConnectionStatus_Connected;

        std::string status;
        if (!connected)
        {
          status = "Disconnected";
          const std::string& error = state_.wal->errorString();
          if (!error.empty())
            status += ": " + error;
        }
        else
          status = "Connected";

        auto screen = ftxui::vbox({
          title_,
          decorate::banner(bar_->Render()),
          ftxui::separator(),
          history_->Render() | ftxui::yflex_shrink,
          ftxui::filler(), 
          ftxui::inverted(decorate::banner(ftxui::text(std::move(status))))
        });
 
        if (state_.overlay)
          return ftxui::dbox({std::move(screen), decorate::overlay(state_.overlay->Render())});
        return screen;
      }
    };
  } // anonymous

  ftxui::Component wallet(std::shared_ptr<Monero::Wallet> data)
  {
    return std::make_shared<wallet_>(std::move(data));
  } 
}} // lwcli // view
