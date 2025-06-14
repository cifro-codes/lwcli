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
#include <lws_frontend.h>

#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "util.h"
#include "views/history.h"


namespace lwcli { namespace view
{
  namespace
  {
    bool set_proxy(Monero::Wallet& wal, const std::string& proxy)
    {
      return wal.setProxy(proxy);
    }

    bool set_url(Monero::Wallet& wal, const std::string& url)
    {
      const bool is_ssl = bool(from_string(wal.getCacheAttribute(std::string{config::server::ssl})).value_or(0));
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

      wal.init(wal.getCacheAttribute(std::string{config::server::url}), 0, "", "", bool(*is_ssl), true, wal.getCacheAttribute(std::string{config::server::proxy}));
      return true;
    }

    bool set_major_lookahead(Monero::Wallet& wal, const std::string& major)
    {
      const auto major_integer = from_string(major);
      if (!major_integer)
        return false;

      auto minor = from_string(wal.getCacheAttribute(std::string{config::minor_lookahead}));
      if (!minor)
        minor = config::default_minor_lookahead;

      wal.setSubaddressLookahead(*major_integer, *minor);
      return true;
    }

    bool set_minor_lookahead(Monero::Wallet& wal, const std::string& minor)
    {
      const auto minor_integer = from_string(minor);
      if (!minor_integer)
        return false;

      auto major = from_string(wal.getCacheAttribute(std::string{config::major_lookahead}));
      if (!major)
        major = config::default_major_lookahead;

      wal.setSubaddressLookahead(*major, *minor_integer);
      return true;
    }

    struct option
    {
      using updater = bool(Monero::Wallet&, const std::string&);
      const std::string_view path;
      const std::string_view description;
      updater* const update;
    };

    const std::array<option, 6> options{{
      {config::server::url,              _("API Server"),                 set_url},
      {config::server::refresh_interval, _("Refresh Interval (seconds)"), set_refresh},
      {config::server::ssl,              _("TLS/SSL Cert Check"),         set_ssl},
      {config::server::proxy,            _("Proxy"),                      set_proxy},
      {config::major_lookahead,          _("Subaddress Major Lookahead"), set_major_lookahead},
      {config::minor_lookahead,          _("Subaddress Minor Lookahead"), set_minor_lookahead}
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

    class show_keys_ final : public ftxui::ComponentBase
    {
      const ftxui::Element title_;
      const ftxui::Component buttons_;
      const ftxui::Element display_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return buttons_; }

      static ftxui::Element get_keys(const std::shared_ptr<Monero::Wallet>& wal)
      {
        return ftxui::vbox({
          ftxui::paragraph(wal->seed()),
          ftxui::separator(),
          ftxui::gridbox({
            {ftxui::text("View Public: "), ftxui::text(wal->publicViewKey())},
            {ftxui::text("Spend Public: "), ftxui::text(wal->publicSpendKey())},
            {ftxui::text("View Key: "), ftxui::text(wal->secretViewKey())},
            {ftxui::text("Spend Key: "), ftxui::text(wal->secretSpendKey())}
          })
        });
      }

    public:
      explicit show_keys_(const std::shared_ptr<Monero::Wallet>& wal)
        : ftxui::ComponentBase(),
          title_(ftxui::text(_("Wallet (Secret) Keys"))),
          buttons_(ftxui::Button(_("Close"), [] () { throw event::close{}; }, ascii())),
          display_(get_keys(wal))
      {}

      bool OnEvent(ftxui::Event event) override final
      {
        buttons_->OnEvent(std::move(event));
        return true;
      }

      ftxui::Element OnRender() override final
      {
        return ftxui::window(title_, ftxui::vbox({
          ftxui::hcenter(buttons_->Render()),
          ftxui::separator(),
          display_
        }));
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
      ftxui::Component overlay_;
      ftxui::Element cached_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (overlay_)
          return overlay_;
        return ui_;
      }

    public:
      explicit settings_(std::shared_ptr<Monero::Wallet>&& wal)
        : ftxui::ComponentBase(),
          wal_(std::move(wal)),
          title_(ftxui::text(_("Settings"))),
          config_(std::make_unique<configuration>(*wal_)),
          error_(),
          buttons_(),
          ui_(),
          overlay_(),
          cached_()
      {
        buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { throw event::close{}; }, ascii()),
          ftxui::Button(_("Save"), [this] () {
            error_.clear();
            config_->store(*wal_, error_);
            if (error_.empty())
              throw event::close{};
          }, ascii()),
          ftxui::Button(_("Secret Keys"), [this] () { show_keys(); }, ascii())
        });

        ftxui::Components ui;
        ui.reserve(config_->states.size() + 1);
        ui.push_back(buttons_);
        for (const auto& opt : config_->states)
          ui.push_back(opt.ui);

        ui_ = ftxui::Container::Vertical(std::move(ui));
        Add(ui_);
      }

      void show_keys()
      {
        overlay_ = std::make_shared<show_keys_>(wal_);
        Add(overlay_);
      }

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          if (overlay_)
            return overlay_->OnEvent(std::move(event));
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          ui_->OnEvent(std::move(event));
        }
        catch (const event::close&)
        {
          if (!overlay_)
            throw;
          overlay_->Detach();
          overlay_.reset();
        }
        return true;
      }

      ftxui::Element OnRender() override final
      {
        if (overlay_)
        {
          return ftxui::dbox({cached_, decorate::overlay(overlay_->Render())});
        }

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

        cached_ = ftxui::window(title_, ftxui::vbox({
          ftxui::hcenter(buttons_->Render()),
          highlighted,
          ftxui::gridbox(std::move(grid))
        }));
        return cached_;
      }
    };
  }

  ftxui::Component settings(std::shared_ptr<Monero::Wallet> wal)
  {
    if (!wal)
      throw std::invalid_argument{"view::settings cannot be given nullptr"};
    return std::make_shared<settings_>(std::move(wal));
  }

}} // lwcli // 
