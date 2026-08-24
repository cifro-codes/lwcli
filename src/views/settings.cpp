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
          buttons_(ftxui::Button(_("Close"), [] () { event::send(event::close()); }, ascii())),
          display_(get_keys(wal))
      {}

      bool OnEvent(ftxui::Event evt) override final
      {
        if (evt == event::close())
          return false;
        buttons_->OnEvent(std::move(evt));
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

    struct password_state
    {
      const std::shared_ptr<Monero::Wallet> wallet;
      std::weak_ptr<ftxui::ComponentBase> overlay;
      std::string password;
      std::string error;

      explicit password_state(std::shared_ptr<Monero::Wallet> in)
        : wallet(std::move(in)),
          overlay(),
          password(),
          error()
      {
        if (!wallet)
          throw std::logic_error{"unexpected nullptr wallet"};
      }
    };

    class password_prompt_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<password_state> state_;
      const ftxui::Element title_;
      const ftxui::Component buttons_;
      const ftxui::Component prompt_;
      const ftxui::Component ui_;
      const ftxui::Element display_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return ui_; }

      static ftxui::Component password(const std::shared_ptr<password_state>& state, std::function<void()> on_enter)
      {
        if (!state)
          throw std::logic_error{"unexpected nullptr state"};

        auto opt = ftxui::InputOption::Default();
        opt.password = true;
        opt.multiline = false;
        opt.on_enter = std::move(on_enter);
        return ftxui::Input(std::addressof(state->password), std::move(opt));
      }

      static void check(const std::shared_ptr<password_state> state)
      {
        if (!state)
          return;

        ftxui::Component overlay{nullptr};
        if (state->wallet->getPassword() == state->password && (overlay = state->overlay.lock()))
        {
          ftxui::ComponentBase* const parent = overlay->Parent();
          overlay->Detach();
          overlay = std::make_shared<show_keys_>(state->wallet);
          state->overlay = overlay;
          if (parent)
            parent->Add(std::move(overlay));
        }
        else
          state->error = _("Invalid Password");
      }

    public:
      explicit password_prompt_(std::weak_ptr<password_state> state)
        : ftxui::ComponentBase(),
          state_(state.lock()),
          title_(ftxui::text(_("Wallet (Secret) Keys - Password Required"))),
          buttons_(
            ftxui::Container::Horizontal({
              ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
              ftxui::Button(_("Show"), [state] () { check(state.lock()); }, ascii())
            })
          ),
          prompt_(password(state_, [state] { check(state.lock()); })),
          ui_(ftxui::Container::Vertical({buttons_, prompt_})),
          display_(ftxui::text(_("Password: ")))
      {
        Add(ui_);
        prompt_->TakeFocus();
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        if (!evt.is_mouse())
          state_->error.clear();
        if (evt == event::close())
          return false;
        ui_->OnEvent(std::move(evt));
        return true;
      }

      ftxui::Element OnRender() override final
      {
        ftxui::Element separator;
        if (state_->error.empty())
          separator = ftxui::separator();
        else
          separator = ftxui::text(state_->error) | ftxui::inverted;

        return ftxui::window(title_, ftxui::vbox({
          ftxui::hcenter(buttons_->Render()),
          separator,
          ftxui::hbox({display_, prompt_->Render()})
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
      std::shared_ptr<password_state> state_;
      ftxui::Element cached_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (state_)
          return state_->overlay.lock();
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
          state_(),
          cached_()
      {}

      static void set_ui(const std::shared_ptr<settings_> self)
      {
        if (!self)
          throw std::logic_error{"unexpected settings_ nullptr"};

        const std::weak_ptr<settings_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Save"), [weak] () {
            if (auto self = weak.lock(); self)
            {
              self->error_.clear();
              self->config_->store(*self->wal_, self->error_);
              if (self->error_.empty())
                event::send(event::close());
            }
          }, ascii()),
          ftxui::Button(_("Secret Keys"), [weak] () { password_prompt(weak.lock()); }, ascii())
        });

        ftxui::Components ui;
        ui.reserve(self->config_->states.size() + 1);
        ui.push_back(self->buttons_);
        for (const auto& opt : self->config_->states)
          ui.push_back(opt.ui);

        self->ui_ = ftxui::Container::Vertical(std::move(ui));
        self->Add(self->ui_);
      }

      static void password_prompt(const std::shared_ptr<settings_> self)
      {
        if (self)
        {
          auto state = std::make_shared<password_state>(self->wal_);
          auto overlay = std::make_shared<password_prompt_>(state);
          state->overlay = overlay;
          self->Add(std::move(overlay));
          self->state_ = std::move(state);
        }
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        if (state_)
        {
          const auto overlay = state_->overlay.lock();
          if (overlay && !overlay->OnEvent(evt) && evt == event::close())
          {
            overlay->Detach();
            state_->overlay.reset();
            state_.reset();
          }
          else if (evt == event::close())
            state_.reset();
        }
        else if (evt == event::close())
          return false;
        ui_->OnEvent(std::move(evt));
        return true;
      }

      ftxui::Element OnRender() override final
      {
        ftxui::Component overlay;
        if (state_ && (overlay = state_->overlay.lock()))
          return ftxui::dbox({cached_, decorate::overlay(overlay->Render())});

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
    auto self = std::make_shared<settings_>(std::move(wal));
    settings_::set_ui(self);
    return self;
  }

}} // lwcli // 
