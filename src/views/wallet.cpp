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

namespace lwcli { namespace view
{
  namespace
  {
    std::optional<std::uint64_t> from_string(const std::string_view str) noexcept
    {
      std::uint64_t out = 0;
      const auto end = str.data() + str.size();
      auto [ptr, ec] = std::from_chars(str.data(), end, out);
      if (bool(ec) || ptr != end)
        return std::nullopt;
      return out;
    }

    bool set_proxy(Monero::Wallet& wal, const std::string& proxy)
    {
      wal.setProxy(proxy);
      return true;
    }

    bool set_url(Monero::Wallet& wal, const std::string& url)
    {
      const bool is_ssl = bool(from_string(wal.getCacheAttribute(std::string{config::server::ssl})));
      wal.init(url, 0, "", "", is_ssl, true, wal.getCacheAttribute(std::string{config::server::proxy}));
      return true;
    }

    bool set_refresh(Monero::Wallet& wal, const std::string& interval)
    {
      const auto number = from_string(interval);
      if (!number)
        return false;
      wal.setAutoRefreshInterval(std::chrono::milliseconds{std::chrono::seconds{*number}}.count());
      return true;
    }

    bool set_ssl(Monero::Wallet& wal, const std::string& ssl)
    {
      const auto is_ssl = from_string(ssl);
      if (!is_ssl)
        return false;
      
      // no way to forcibly select ssl without another init call. hopefully doesn't break things
      wal.init(wal.getCacheAttribute(std::string{config::server::url}), 0, "", "", bool(*is_ssl), true, wal.getCacheAttribute(std::string{config::server::proxy}));
      return true;
    }

    struct option
    {
      using updater = bool(Monero::Wallet&, const std::string&);
      const std::string_view path;
      const std::string_view description;
      updater* const update;
    };

    const std::array<option, 4> options{{
      {config::server::proxy,            _("LWS Proxy"),                      set_proxy},
      {config::server::refresh_interval, _("LWS Refresh Interval (seconds)"), set_refresh},
      {config::server::ssl,              _("LWS SSL Verify"),                 set_ssl},
      {config::server::url,              _("LWS URL"),                        set_url}
    }};

    ftxui::Component last_input(std::string* str)
    {
      auto opt = ftxui::InputOption::Default();
      opt.cursor_position = str->size();
      opt.multiline = false;
      return ftxui::Input(str, std::move(opt));
    }

    ftxui::ButtonOption ascii() { return ftxui::ButtonOption::Ascii(); }

    struct option_state
    {
      ftxui::Element description;
      std::string original;
      std::string value;
      ftxui::Component ui;
    };
    
    struct configuration
    {
      std::array<option_state, options.size()> states;

      configuration(const Monero::Wallet& wal)
        : states()
      {
        for (std::size_t i = 0; i < options.size(); ++i)
        {
          states[i].description = ftxui::text(std::string{options[i].description} + ": ");
          states[i].original = wal.getCacheAttribute(std::string{options[i].path});
          states[i].value = states[i].original;
          states[i].ui = last_input(&states[i].value);
        }
      }

      void store(Monero::Wallet& wal, std::string& error)
      {
        for (std::size_t i = 0; i < options.size(); ++i)
        {
          if (states[i].original != states[i].value)
          {
            if (!options[i].update || options[i].update(wal, states[i].value))
              wal.setCacheAttribute(std::string{options[i].path}, states[i].value);
            else
            {
              error = std::string{options[i].description} + " is invalid";
              break;
            }
          }
        }
      }
    };

    class settings_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::Wallet> wal_;
      const ftxui::Element title_;
      const std::unique_ptr<configuration> config_;
      std::string error_;
      ftxui::Component buttons_;
      ftxui::Component ui_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return ui_; }

    public:
      explicit settings_(std::shared_ptr<Monero::Wallet>&& wal)
        : ftxui::ComponentBase(),
          wal_(std::move(wal)),
          title_(ftxui::text(_("Settings"))),
          config_(std::make_unique<configuration>(*wal_)),
          error_(),
          buttons_(),
          ui_()
      {
        buttons_ = ftxui::Container::Horizontal({
          ftxui::Button("Cancel", [] () { throw event::close{}; }, ascii()),
          ftxui::Button("Save", [this] () {
            error_.clear();
            config_->store(*wal_, error_);
            if (error_.empty())
              throw event::close{};
          }, ascii())
        });

        ftxui::Components ui;
        ui.reserve(config_->states.size() + 1);
        ui.push_back(buttons_);
        for (const auto& opt : config_->states)
          ui.push_back(opt.ui);

        ui_ = ftxui::Container::Vertical(std::move(ui));
        Add(ui_);
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
        const auto min_size = ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 5);
        std::vector<ftxui::Elements> grid;
        grid.reserve(config_->states.size());
        for (const auto& opt : config_->states)
          grid.push_back({opt.description, min_size(opt.ui->Render())});

        ftxui::Element highlighted;
        if (error_.empty())
          highlighted = ftxui::separator();
        else
          highlighted = ftxui::inverted(ftxui::text(error_));

        return ftxui::window(title_, ftxui::vbox({
          ftxui::hcenter(buttons_->Render()),
          highlighted,
          ftxui::gridbox(std::move(grid))
        }));
      }
    }; 

    ftxui::Component settings(std::shared_ptr<Monero::Wallet> wal)
    {
      return std::make_shared<settings_>(std::move(wal));
    }

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
          else if (event == ftxui::Event::s || event == ftxui::Event::S)
            ; // state_.overlay = send(state_.wal);
          else if (event == ftxui::Event::b || event == ftxui::Event::B)
            ; // state_.overlay = book(state_.wal);
          else if (event == ftxui::Event::a || event == ftxui::Event::a)
            ; // overlay = accounts(state_.wal, &active_account_);
          else if (event == ftxui::Event::r || event == ftxui::Event::R)
            ; //state_.wal->refreshAsync();
          else if (event == ftxui::Event::e || event == ftxui::Event::E)
            state_.overlay = settings(state_.wal);
          else
            ui_->OnEvent(std::move(event));

        //  if (!has_overlay && state_.overlay)
        //    Add(state_.overlay);

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
