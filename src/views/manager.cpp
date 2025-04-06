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

    ftxui::Component password(std::string* pass)
    {
      auto opt = ftxui::InputOption::Default();
      opt.password = true;
      opt.multiline = false;
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

    struct wallet_base
    {
      std::string file;
      std::string password;

      wallet_base()
        : file(), password()
      {
        const char* home = std::getenv("HOME");
        if (home)
          file = home;
      }
    };

    struct new_wallet : wallet_base
    {
      std::string confirm;
      std::string language;
      std::string server;
      std::string proxy;
      bool ssl;

      new_wallet()
        : wallet_base(), confirm(), language(config::default_language), server(config::server::default_url), proxy(), ssl(false)
      {}

      void setup(Monero::Wallet& wal)
      {
        wal.setCacheAttribute(std::string{config::server::refresh_interval}, std::to_string(config::server::default_refresh_interval.count()));
        wal.setCacheAttribute(std::string{config::server::url}, server);
        wal.setCacheAttribute(std::string{config::server::proxy}, proxy);
        wal.setCacheAttribute(std::string{config::server::ssl}, std::to_string(int(ssl)));
      }
    };

    struct start_state
    {
      const std::shared_ptr<Monero::WalletManager> wm;
      std::shared_ptr<Monero::Wallet> wal;
      ftxui::Component overlay;
      std::string error;

      start_state(std::shared_ptr<Monero::WalletManager>&& wm)
        : wm(std::move(wm)), wal(nullptr), overlay(nullptr), error()
      {}
    };

    std::optional<std::uint64_t> from_string(const std::string& str) noexcept
    {
      std::uint64_t height = 0;
      const auto end = str.data() + str.size();
      auto [ptr, ec] = std::from_chars(str.data(), end, height);
      if (bool(ec) || ptr != end)
        return std::nullopt;
      return height;
    }

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
        *error = wal.errorString();
        return false;
      }
      return true;
    }

    std::pair<std::vector<std::pair<ftxui::Element, ftxui::Component>>, ftxui::Component> get_load_options(start_state* state)
    {
      struct options
      {
        start_state* state;
        wallet_base config;

        options(start_state* state)
          : state(state), config()
        {}
      };
      auto enclosed = std::make_shared<options>(state);
      auto load = ftxui::Button("Load", [enclosed] () {
        auto prepped = prep_wallet(
          enclosed->state->wm,
          enclosed->state->wm->openWallet(enclosed->config.file, enclosed->config.password, Monero::TESTNET),
          &enclosed->state->error
        );
        if (prepped)
        {
          enclosed->config.password.clear();
          init_wallet(*prepped, &enclosed->state->error);
          enclosed->state->wal = prepped;
        }
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)}
        },
        load
      };
    }
    std::pair<std::vector<std::pair<ftxui::Element, ftxui::Component>>, ftxui::Component> get_create_options(start_state* state)
    {
      struct options
      {
        start_state* state;
        new_wallet config;

        options(start_state* state)
          : state(state), config()
        {}
      };
      auto enclosed = std::make_shared<options>(state);
      auto create = ftxui::Button("Create", [enclosed] () {
        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            enclosed->state->error = "Passwords do not match";
            return;
          }
          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              enclosed->state->wm,
              enclosed->state->wm->createWallet(enclosed->config.file, enclosed->config.password, enclosed->config.language, Monero::TESTNET),
              &enclosed->state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.password.clear();
                enclosed->config.confirm.clear();
                enclosed->config.setup(*prepped);
                init_wallet(*prepped, &enclosed->state->error);
                enclosed->state->overlay = view::keys(prepped, true /* show warning */);
                enclosed->state->wal = prepped;
              }
              else
                enclosed->state->error = "Unable to create file: " + prepped->errorString();
            }
          }
          else
            enclosed->state->error = "File already exists";
        }
        else
          enclosed->state->error = "Invalid Filename";
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.confirm)},
          {ftxui::text(_("Language: ")), last_input(&enclosed->config.language)},
          {ftxui::text(_("LWS Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Server Options: ")), ftxui::Checkbox(_("SSL"), &enclosed->config.ssl)}
        },
        create
      };
    }
    std::pair<std::vector<std::pair<ftxui::Element, ftxui::Component>>, ftxui::Component> get_seed_options(start_state* state)
    {
      struct options
      {
        start_state* state;
        std::string mnemonic;
        std::string height;
        new_wallet config;

        options(start_state* state)
          : state(state), mnemonic(), height("0"), config()
        {}
      };
      auto enclosed = std::make_shared<options>(state);
      auto recover = ftxui::Button("Recover", [enclosed] () {
        const auto height = from_string(enclosed->height);
        if (!height)
        {
          enclosed->state->error = "Invalid Height";
          return;
        }
        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            enclosed->state->error = "Passwords do not match";
            return;
          }

          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              enclosed->state->wm,
              enclosed->state->wm->recoveryWallet(enclosed->config.file, enclosed->config.password, enclosed->mnemonic, Monero::TESTNET, *height),
              &enclosed->state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.password.clear();
                enclosed->config.confirm.clear();
                enclosed->config.setup(*prepped);
                init_wallet(*prepped, &enclosed->state->error);
                enclosed->state->wal = prepped;
              }
              else
                enclosed->state->error = "Unable to create file: " + prepped->errorString();
            }
          }
          else
            enclosed->state->error = "File already exists";
        }
        else
          enclosed->state->error = "Invalid Filename";
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.confirm)},
          {ftxui::text(_("Mnemonic: ")), last_input(&enclosed->mnemonic)},
          {ftxui::text(_("Height: ")), last_input(&enclosed->height)},
          {ftxui::text(_("LWS Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Server Options: ")), ftxui::Checkbox(_("SSL"), &enclosed->config.ssl)}
        },
        recover
      };
    }

    std::pair<std::vector<std::pair<ftxui::Element, ftxui::Component>>, ftxui::Component> get_key_options(start_state* state)
    {
      struct options
      {
        start_state* state;
        std::string spend_key;
        std::string height;
        new_wallet config;

        options(start_state* state)
          : state(state), spend_key(), height("0"), config()
        {}
      };
      auto enclosed = std::make_shared<options>(state);
      auto recover = ftxui::Button("Recover (Broken)", [enclosed] () {
        const auto height = from_string(enclosed->height);
        if (!height)
        {
          enclosed->state->error = "Invalid Height";
          return;
        }
        if (!enclosed->config.file.empty())
        {
          if (enclosed->config.password != enclosed->config.confirm)
          {
            enclosed->state->error = "Passwords do not match";
            return;
          }

          std::error_code ec{};
          if (!std::filesystem::exists(enclosed->config.file, ec))
          {
            auto prepped = prep_wallet(
              enclosed->state->wm,
              enclosed->state->wm->createWalletFromKeys(
                enclosed->config.file,
                enclosed->config.password,
                enclosed->config.language,
                Monero::TESTNET,
                *height,
                "address",
                "view_key",
                enclosed->spend_key
              ),
              &enclosed->state->error
            );
            if (prepped)
            {
              if (prepped->store({}))
              {
                enclosed->config.password.clear();
                enclosed->config.confirm.clear();
                enclosed->config.setup(*prepped);
                init_wallet(*prepped, &enclosed->state->error);
                enclosed->state->wal = prepped;
              }
              else
                enclosed->state->error = "Unable to create file: " + prepped->errorString();
            }
          }
          else
            enclosed->state->error = "File already exists";
        }
        else
          enclosed->state->error = "Invalid Filename";
      }, ftxui::ButtonOption::Ascii());
      return {
        {
          {ftxui::text(_("Filename: ")), last_input(&enclosed->config.file)},
          {ftxui::text(_("Password: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Confirm: ")), password(&enclosed->config.password)},
          {ftxui::text(_("Spend key: ")), last_input(&enclosed->spend_key)},
          {ftxui::text(_("Height: ")), last_input(&enclosed->height)},
          {ftxui::text(_("Language: ")), last_input(&enclosed->config.language)},
          {ftxui::text(_("LWS Server: ")), last_input(&enclosed->config.server)},
          {ftxui::text(_("Proxy: ")), last_input(&enclosed->config.proxy)},
          {ftxui::text(_("Connection Options: ")), ftxui::Checkbox(_("SSL"), &enclosed->config.ssl)}
        },
        recover
      };
    }
    
    class start final : public ftxui::ComponentBase
    {
      std::shared_ptr<Monero::Wallet>* out_;
      const ftxui::Element title_;
      const ftxui::Element help_;
      const ftxui::Element disclaimer_;
      start_state state_;
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
        if (state_.overlay)
          return state_.overlay; 
        return ui_; 
      }

    public:
      explicit start(std::shared_ptr<Monero::WalletManager>&& wm, std::shared_ptr<Monero::Wallet>* out)
        : out_(out),
          title_(ftxui::text("wmcli")),
          help_(decorate::banner(ftxui::text(_("Ctrl-Q to close active window, Ctrl-C close app immediately")))),
          disclaimer_(decorate::banner(ftxui::text(_("Beware of mouse events in Tmux/Screen")))),
          state_(std::move(wm)),
          options_({_("Load Wallet"), _("Create Wallet"), _("Recover from Seed"), _("Recover from Keys")}),
          active_(-1),
          selected_(0),
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
          active_ = selected_;
          switch(active_)
          {
            case 0:
            default:
              std::tie(stack_, completion_) = get_load_options(&state_);
            break;
            case 1:
              std::tie(stack_, completion_) = get_create_options(&state_);
              break;
            case 2:
              std::tie(stack_, completion_) = get_seed_options(&state_);
              break;
            case 3:
              std::tie(stack_, completion_) = get_key_options(&state_);
              break;
          }

          ftxui::Components ui;
          ui.reserve(stack_.size() + 2);
          ui.push_back(mode_);
          for (const auto& elem : stack_)
            ui.push_back(elem.second);
          ui.push_back(completion_);

          ui_ = ftxui::Container::Vertical(std::move(ui));
        }

        // Delay showing wallet if options required overlay
        if (state_.wal && !state_.overlay)
        {
          *out_ = std::move(state_.wal);
          state_.wal.reset();
        }

        return true;
      }

      bool OnEvent(ftxui::Event event) override final
      {
        if (!event.is_mouse())
          state_.error.clear();

        try
        {
          if (state_.overlay)
            return state_.overlay->OnEvent(std::move(event));
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          else if (ui_->OnEvent(std::move(event)))
            return update_ui();
        }
        catch (const event::close&)
        {
          if (state_.overlay)
          {
            state_.overlay.reset();
            *out_ = std::move(state_.wal);
            state_.wal.reset();
            return true;
          }
          throw;
        }
        return false;
      }

      ftxui::Element OnRender() override final
      {
        std::vector<ftxui::Elements> elements;
        elements.reserve(stack_.size());
        for (const auto& elem : stack_)
          elements.push_back({elem.first, ftxui::xflex_grow(elem.second->Render())});

        ftxui::Elements out{
          help_,
          disclaimer_,
          decorate::banner(mode_->Render()),
          ftxui::separator(),
          ftxui::gridbox(std::move(elements)),
          ftxui::separator(),
          decorate::banner(completion_->Render())
        };
        if (!state_.error.empty())
          out[3] = ftxui::inverted(decorate::banner(ftxui::text(state_.error)));

        const auto base = ftxui::hcenter(ftxui::xflex_grow(ftxui::vbox(std::move(out)))); 
        if (state_.overlay)
          return ftxui::window(title_, ftxui::dbox(base, decorate::overlay(state_.overlay->Render())));
        return ftxui::window(title_, base);
      }
    };

    class manager_ final : public ftxui::ComponentBase
    {
      std::shared_ptr<Monero::Wallet> data_;
      const ftxui::Component start_;
      ftxui::Component wallet_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (wallet_)
          return wallet_;
        return start_;
      }

    public:
      explicit manager_(std::shared_ptr<Monero::WalletManager>&& wm)
        : ftxui::ComponentBase(),
          data_(nullptr),
          start_(std::make_shared<start>(std::move(wm), &data_)),
          wallet_(nullptr)
      {}

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          if (wallet_)
            return wallet_->OnEvent(std::move(event));
          else if (start_->OnEvent(std::move(event)) && data_)
            wallet_ = view::wallet(std::move(data_));
          data_.reset();
        }
        catch (const event::close&)
        {
          if (!wallet_)
            throw;
          data_.reset();
          wallet_.reset();
        }
        return true;
      }

      ftxui::Element OnRender() override final
      {
        if (wallet_)
          return wallet_->Render();
        return start_->Render();
      }
    };
  }

  ftxui::Component manager(std::shared_ptr<Monero::WalletManager> wm)
  {
    if (!wm)
      throw std::runtime_error{"lwcli::view::manaager given nullptr"};
    return std::make_shared<manager_>(std::move(wm));
  } 
}} // lwcli // view
