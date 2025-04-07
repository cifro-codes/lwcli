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
      {config::server::proxy,            _("Proxy"),                      set_proxy},
      {config::server::refresh_interval, _("Refresh Interval (seconds)"), set_refresh},
      {config::server::ssl,              _("TLS/SSL Cert Check"),         set_ssl},
      {config::server::url,              _("API Server"),                 set_url}
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
          ftxui::Button(_("Cancel"), [] () { throw event::close{}; }, ascii()),
          ftxui::Button(_("Save"), [this] () {
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
  }

  ftxui::Component settings(std::shared_ptr<Monero::Wallet> wal)
  {
    return std::make_shared<settings_>(std::move(wal));
  }

}} // lwcli // 
