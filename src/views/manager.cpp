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
#include <filesystem>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <lws_frontend.h>
#include <string_view>

#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "util.h"
#include "views/history.h"
#include "views/keys.h"

namespace lwcli { namespace view
{
  namespace
  {
    struct close_wallet
    {
      std::shared_ptr<Monero::WalletManager> wm;

      void operator()(Monero::Wallet* ptr) const
      {
        if (ptr)
          wm->closeWallet(ptr, true /* store */);
      }
    };

    std::string get_home()
    {
      const char* home = std::getenv("HOME");
      if (home)
        return home;
      return {};
    }

    ftxui::Component password(std::string* pass, std::function<void()> on_enter = nullptr)
    {
      auto opt = ftxui::InputOption::Default();
      opt.password = true;
      opt.multiline = false;
      if (on_enter)
        opt.on_enter = std::move(on_enter);
      return ftxui::Input(pass, std::move(opt));
    }

    ftxui::Component last_input(std::string* str)
    {
      auto opt = ftxui::InputOption::Default();
      opt.cursor_position = str->size();
      opt.multiline = false;
      return ftxui::Input(str, std::move(opt));
    }

    std::shared_ptr<Monero::Wallet> prep_wallet(std::shared_ptr<Monero::WalletManager> wm, Monero::Wallet* ptr, std::string* error)
    {
      std::unique_ptr<Monero::Wallet> data{ptr};
      if (!data)
        throw std::runtime_error{"unexpected wallet nullptr"};
 
      int status = 0;
      error->clear();
      data->statusWithErrorString(status, *error);
      if (status != Monero::Wallet::Status_Ok)
        return nullptr;

      return {data.release(), close_wallet{std::move(wm)}};
    }

    struct start_state;
    struct wallet_base
    {
      const std::weak_ptr<start_state> state;
      std::string file;
      std::string password;

      wallet_base(std::weak_ptr<start_state> state, std::string default_file)
        : state(std::move(state)), file(std::move(default_file)), password()
      {}
    };

    struct new_wallet : wallet_base
    {
      std::string confirm;
      std::string language;
      std::string server;
      std::string proxy;
      bool ssl;
      bool subaddresses;
      bool fido2;

      new_wallet(std::shared_ptr<start_state> state, std::string default_file)
        : wallet_base(std::move(state), std::move(default_file)),
          confirm(),
          language(config::default_language),
          server(config::server::default_url),
          proxy(),
          ssl(false),
          subaddresses(true),
          fido2(false)
      {}

      std::string get_api_password()
      {
        if (!fido2)
          return password;
        return "";
      }

      void setup(Monero::Wallet& wal)
      {
        wal.setCacheAttribute(std::string{config::server::refresh_interval}, std::to_string(config::server::default_refresh_interval.count()));
        wal.setCacheAttribute(std::string{config::server::url}, server);
        wal.setCacheAttribute(std::string{config::server::proxy}, proxy);
        wal.setCacheAttribute(std::string{config::server::ssl}, std::to_string(int(ssl)));

        const std::uint32_t major_lookahead = subaddresses ?
          config::default_major_lookahead : 0;
        const std::uint32_t minor_lookahead = subaddresses ?
          config::default_minor_lookahead : 0;

        wal.setCacheAttribute(std::string{config::major_lookahead}, std::to_string(major_lookahead));
        wal.setCacheAttribute(std::string{config::minor_lookahead}, std::to_string(minor_lookahead));
        wal.setSubaddressLookahead(major_lookahead, minor_lookahead);
      }
    };

    struct manager_state
    {
      const std::shared_ptr<Monero::WalletManager> wm;
      ftxui::Component wallet;

      explicit manager_state(std::shared_ptr<Monero::WalletManager> src) noexcept
        : wm(std::move(src)), wallet(nullptr)
      {}
    };

    struct start_state
    {
      const std::weak_ptr<manager_state> out;
      std::shared_ptr<Monero::Wallet> temp;
      ftxui::Component overlay;
      std::string error;

      explicit start_state(std::weak_ptr<manager_state> out)
        : out(std::move(out)), temp(nullptr), overlay(nullptr), error()
      {}

      void show_wallet(std::shared_ptr<Monero::Wallet> wal)
      {
        // Delay showing wallet if options required overlay
        if (auto update = out.lock(); update && wal && !overlay)
          update->wallet = view::wallet(update->wm, std::move(wal));
        else
          temp = std::move(wal);
      }
    };

    bool init_wallet(Monero::Wallet& wal, std::string* error)
    {
      auto refresh = from_string(wal.getCacheAttribute(std::string{config::server::refresh_interval}));
      if (!refresh)
        refresh = config::server::default_refresh_interval.count();
      wal.setAutoRefreshInterval(std::chrono::milliseconds{std::chrono::seconds{*refresh}}.count());

      auto ssl = from_string(wal.getCacheAttribute(std::string{config::server::ssl}));
      if (!ssl)
        ssl = 0;

      if (!wal.init(wal.getCacheAttribute(std::string{config::server::url}), 0, "", "", *ssl, true, wal.getCacheAttribute(std::string{config::server::proxy})))
      {
        *error = "Failure to initialize" + wal.errorString();
        return false;
      }
      return true;
    }

    using option_set = std::pair<std::vector<std::pair<ftxui::Element, ftxui::Component>>, ftxui::Component>;

    option_set get_load_options(std::string default_file, std::shared_ptr<start_state> state)
    {
      struct options
      {
        wallet_base config;

        options(std::string default_file, std::shared_ptr<start_state> state)
          : config(std::move(state), std::move(default_file))
        {}
      };

      auto enclosed = std::make_shared<options>(std::move(default_file), std::move(state));
      const auto load_action = [enclosed] () {
        const auto state = enclosed->config.state.lock();
        if (!state)
          return;

        const auto out = state->out.lock();
        if (!out || !out->wm)
          return;

        auto prepped = prep_wallet(
          out->wm,
          out->wm->openWallet(enclosed->config.file, enclosed->config.password, config::network),
          &state->error
        );
        if (prepped)
        {
          if (init_wallet(*prepped, &state->error))
          {
            enclosed->config.password.clear();
            prepped->startRefresh();
            state->show_wallet(std::move(prepped));
          }
        }
      };

      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password, load_action)}
        },
        ftxui::Button(_("Load"), load_action, ftxui::ButtonOption::Ascii())
      };
    }
    option_set get_create_options(std::string default_file, std::shared_ptr<start_state> state)
    {
      struct options
      {
        new_wallet config;

        options(std::string default_file, std::shared_ptr<start_state> state)
          : config(std::move(state), std::move(default_file))
        {}
      };
      auto enclosed = std::make_shared<options>(std::move(default_file), std::move(state));
      auto create = ftxui::Button("Create", [enclosed] () {
        const auto state = enclosed->config.state.lock();
        if (!state)
          return;

        const auto out = state->out.lock();
        if (!out || !out->wm)
          return;

        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            state->error = _("Passwords do not match");
            return;
          }
          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              out->wm,
              out->wm->createWallet(enclosed->config.file, enclosed->config.password, enclosed->config.language, config::network),
              &state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.setup(*prepped);
                if (init_wallet(*prepped, &state->error))
                {
                  enclosed->config.password.clear();
                  enclosed->config.confirm.clear();
                  state->overlay = view::keys(prepped, true /* show warning */);
                  prepped->startRefresh();
                  state->show_wallet(std::move(prepped));
                }
              }
              else
                state->error = _("Unable to create file: ") + prepped->errorString();
            }
          }
          else
            state->error = _("File already exists");
        }
        else
          state->error = _("Invalid Filename");
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.confirm)},
          {ftxui::text(_("Language: ")), last_input(&enclosed->config.language)},
          {ftxui::text(_("API Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Options: ")), ftxui::Container::Horizontal({
              ftxui::Checkbox(_("TLS/SSL Cert Check "), &enclosed->config.ssl),
              ftxui::Checkbox(_("Subaddresses "), &enclosed->config.subaddresses),
              ftxui::Checkbox(_("FIDO2"), &enclosed->config.fido2)
          })}
        },
        create
      };
    }
    option_set get_seed_options(std::string default_file, std::shared_ptr<start_state> state)
    {
      struct options
      {
        std::string mnemonic;
        std::string height;
        new_wallet config;

        options(std::string default_file, std::shared_ptr<start_state> state)
          : mnemonic(), height("0"), config(std::move(state), std::move(default_file))
        {}
      };
      auto enclosed = std::make_shared<options>(std::move(default_file), std::move(state));
      auto recover = ftxui::Button(_("Recover"), [enclosed] () {
        const auto state = enclosed->config.state.lock();
        if (!state)
          return;

        const auto out = state->out.lock();
        if (!out || !out->wm)
          return;

        const auto height = from_string(enclosed->height);
        if (!height)
        {
          state->error = _("Invalid Height");
          return;
        }
        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            state->error = _("Passwords do not match");
            return;
          }

          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              out->wm,
              out->wm->recoveryWallet(enclosed->config.file, enclosed->config.password, enclosed->mnemonic, config::network, *height),
              &state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.setup(*prepped);
                if (init_wallet(*prepped, &state->error))
                {
                  enclosed->mnemonic.clear();
                  enclosed->config.password.clear();
                  enclosed->config.confirm.clear();
                  prepped->rescanBlockchainAsync();
                  state->show_wallet(std::move(prepped));
                }
              }
              else
                state->error = _("Unable to create file: ") + prepped->errorString();
            }
          }
          else
            state->error = _("File already exists");
        }
        else
          state->error = _("Invalid Filename");
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.confirm)},
          {ftxui::text(_("Mnemonic: ")), last_input(&enclosed->mnemonic)},
          {ftxui::text(_("Height: ")), last_input(&enclosed->height)},
          {ftxui::text(_("API Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Options: ")), ftxui::Container::Horizontal({
              ftxui::Checkbox(_("TLS/SSL Cert Check "), &enclosed->config.ssl),
              ftxui::Checkbox(_("Subaddresses "), &enclosed->config.subaddresses),
              ftxui::Checkbox(_("FIDO2"), &enclosed->config.fido2)
          })}
        },
        recover
      };
    }

    option_set get_key_options(std::string default_file, std::shared_ptr<start_state> state)
    {
      struct options
      {
        std::string spend_key;
        std::string view_key;
        std::string address;
        std::string height;
        new_wallet config;

        options(std::string default_file, std::shared_ptr<start_state> state)
          : spend_key(), view_key(), address(), height("0"), config(std::move(state), std::move(default_file))
        {}
      };
      auto enclosed = std::make_shared<options>(std::move(default_file), std::move(state));
      auto recover = ftxui::Button(_("Recover"), [enclosed] () {
        const auto state = enclosed->config.state.lock();
        if (!state)
          return;

        const auto out = state->out.lock();
        if (!out || !out->wm)
          return;

        const auto height = from_string(enclosed->height);
        if (!height)
        {
          state->error = _("Invalid Height");
          return;
        }
        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            state->error = _("Passwords do not match");
            return;
          }

          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              out->wm,
              out->wm->createWalletFromKeys(
                enclosed->config.file,
                enclosed->config.password,
                enclosed->config.language,
                config::network,
                *height,
                enclosed->address,
                enclosed->view_key,
                enclosed->spend_key
              ),
              &state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.setup(*prepped);
                if (init_wallet(*prepped, &state->error))
                {
                  enclosed->spend_key.clear();
                  enclosed->config.password.clear();
                  enclosed->config.confirm.clear();
                  prepped->rescanBlockchainAsync();
                  state->show_wallet(std::move(prepped));
                }
              }
              else
                state->error = _("Unable to create file: ") + prepped->errorString();
            }
          }
          else
            state->error = _("File already exists");
        }
        else
          state->error = _("Invalid Filename");
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.confirm)},
          {ftxui::text(_("Spend key: ")), last_input(&enclosed->spend_key)},
          {ftxui::text(_("View key: ")), last_input(&enclosed->view_key)},
          {ftxui::text(_("Address: ")), last_input(&enclosed->address)},
          {ftxui::text(_("Height: ")), last_input(&enclosed->height)},
          {ftxui::text(_("Language: ")), last_input(&enclosed->config.language)},
          {ftxui::text(_("API Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Options: ")), ftxui::Container::Horizontal({
              ftxui::Checkbox(_("TLS/SSL Cert Check "), &enclosed->config.ssl),
              ftxui::Checkbox(_("Subaddresses "), &enclosed->config.subaddresses),
              ftxui::Checkbox(_("FIDO2"), &enclosed->config.fido2)
          })}
        },
        recover
      };
    }
    
    class start final : public ftxui::ComponentBase
    {
      const ftxui::Element title_;
      const ftxui::Element help_;
      const ftxui::Element disclaimer_;
      const std::shared_ptr<start_state> state_;
      const std::string default_file_;
      const std::vector<std::string> options_;
      int active_;
      int selected_;
      const ftxui::Component mode_;
      ftxui::Component completion_;
      std::vector<std::pair<ftxui::Element, ftxui::Component>> stack_;
      ftxui::Component ui_;
      
      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (state_->overlay)
          return state_->overlay;
        return ui_; 
      }

      static int get_selected(const std::string& file)
      {
        // default to load file if exists, and create if does not exist
        std::error_code ec{};
        return file.empty() || std::filesystem::exists(file, ec) ? 0 : 1;
      }

    public:
      explicit start(std::string&& file, std::shared_ptr<manager_state> out)
        : ftxui::ComponentBase(),
          title_(ftxui::text("lwcli")),
          help_(decorate::banner(ftxui::text(_("Ctrl-Q to close active window, Ctrl-C close app immediately")))),
          disclaimer_(decorate::banner(ftxui::text(_("Beware of mouse events in Tmux/Screen")))),
          state_(std::make_shared<start_state>(std::move(out))),
          default_file_(std::move(file)),
          options_({_("Load Wallet"), _("Create Wallet"), _("Recover from Seed"), _("Recover from Keys")}),
          active_(-1),
          selected_(get_selected(default_file_)),
          mode_(ftxui::Dropdown(&options_, &selected_)),
          completion_(),
          stack_(),
          ui_()
      {
        update_ui();
      }

      bool update_ui()
      {
        if (active_ != selected_)
        {
          const bool first_load = active_ == -1;
          active_ = selected_;
          switch(active_)
          {
            case 0:
            default:
              std::tie(stack_, completion_) = get_load_options((default_file_.empty() ? get_home() : default_file_), state_);
              break;
            case 1:
              std::tie(stack_, completion_) = get_create_options((default_file_.empty() ? get_home() : default_file_), state_);
              break;
            case 2:
              std::tie(stack_, completion_) = get_seed_options((default_file_.empty() ? get_home() : default_file_), state_);
              break;
            case 3:
              std::tie(stack_, completion_) = get_key_options((default_file_.empty() ? get_home() : default_file_), state_);
              break;
          }

          ftxui::Components ui;
          ui.reserve(stack_.size() + 2);
          ui.push_back(mode_);
          for (const auto& elem : stack_)
            ui.push_back(elem.second);
          ui.push_back(completion_);

          ui_ = ftxui::Container::Vertical(std::move(ui));

          // skip directly to password on init if default file given
          if (first_load)
          {
            if (!default_file_.empty())
              stack_.at(1).second->TakeFocus(); // jump to password field (load or create wallet)
            else
              stack_.at(0).second->TakeFocus(); // jump tp file (load wallet)
          }
        }

        return true;
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        if (!evt.is_mouse())
          state_->error.clear();

        if (evt == event::lock_wallet)
          state_->error = _("Wallet locked due to inactivity");
        else if (state_->overlay)
        {
          if (state_->overlay->OnEvent(evt))
            return true;
          else if (evt == event::close())
          {
            std::shared_ptr<Monero::Wallet> wal;
            state_->temp.swap(wal);

            state_->overlay->Detach();
            state_->overlay.reset();

            if (wal)
              state_->show_wallet(std::move(wal));
            return true;
          }
        }
        else if (evt == event::close())
          return false;
        else if (ui_->OnEvent(std::move(evt)))
          return update_ui();

        return false;
      }

      ftxui::Element OnRender() override final
      {
        std::vector<ftxui::Elements> elements;
        elements.reserve(stack_.size());
        for (const auto& elem : stack_)
          elements.push_back({elem.first, ftxui::xflex_grow(elem.second->Render())});

        ftxui::Elements out{
          ftxui::text("  ○━━━━━━━━━━━━━━━━━┓") | ftxui::hcenter,
          ftxui::text("┃ lwcli.cifro.codes ┃") | ftxui::hcenter,
          ftxui::text("┗━━━━━━━━━━━━━━━━━▶  ") | ftxui::hcenter,
          ftxui::separator(),
          help_,
          disclaimer_,
          decorate::banner(mode_->Render()),
          ftxui::separator(),
          ftxui::gridbox(std::move(elements)),
          ftxui::separator(),
          decorate::banner(completion_->Render())
        };
        if (!state_->error.empty())
          out[7] = ftxui::inverted(decorate::banner(ftxui::text(state_->error)));

        const auto base = ftxui::hcenter(ftxui::xflex_grow(ftxui::vbox(std::move(out)))); 
        if (state_->overlay)
          return ftxui::dbox(base, decorate::overlay(state_->overlay->Render()));
        return base;
      }
    };

    class manager_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<manager_state> state_;
      const ftxui::Component start_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (state_->wallet)
          return state_->wallet;
        return start_;
      }

    public:
      explicit manager_(std::shared_ptr<Monero::WalletManager>&& wm, std::string&& file)
        : ftxui::ComponentBase(),
          state_(std::make_shared<manager_state>(std::move(wm))),
          start_(std::make_shared<start>(std::move(file), state_))
      {}

      bool OnEvent(ftxui::Event evt) override final
      {
        if (state_->wallet)
        {
          bool detach = false;
          if (evt == event::lock_wallet)
          {
            start_->OnEvent(std::move(evt));
            detach = true;
          }
          else if (!state_->wallet->OnEvent(evt) && evt == event::close())
            detach = true;

          if (detach)
          {
            state_->wallet->Detach();
            state_->wallet.reset();
          }
          return true;
        }
        else if (!start_->OnEvent(evt) && evt == event::close())
          return false;

        return true;
      }

      ftxui::Element OnRender() override final
      {
        if (state_->wallet)
          return state_->wallet->Render();
        return start_->Render();
      }
    };
  }

  ftxui::Component manager(std::shared_ptr<Monero::WalletManager> wm, std::string&& file)
  {
    if (!wm)
      throw std::runtime_error{"lwcli::view::manaager given nullptr"};
    return std::make_shared<manager_>(std::move(wm), std::move(file));
  } 
}} // lwcli // view
