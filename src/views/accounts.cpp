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

#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/table.hpp>
#include <lws_frontend.h>

#include "components/table.h"
#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "util.h"

namespace lwcli { namespace view
{
  namespace
  {
    ftxui::Component last_input(std::string* str)
    {
      auto opt = ftxui::InputOption::Default();
      opt.cursor_position = str->size();
      opt.multiline = false;
      return ftxui::Input(str, std::move(opt));
    }

    ftxui::ButtonOption ascii() { return ftxui::ButtonOption::Ascii(); }

    class account_detail final : public ftxui::ComponentBase
    {
      std::string account_name_;
      const std::shared_ptr<Monero::Wallet> wal_;
      Monero::Subaddress* const acct_;
      const ftxui::Element title_;
      const ftxui::Elements address_;
      const ftxui::Element desc_;
      ftxui::Component details_;
      ftxui::Component table_;
      ftxui::Component buttons_;
      const ftxui::Component name_;
      ftxui::Component ui_;
      std::unordered_map<std::size_t, std::size_t> row_map_;
      const std::uint32_t id_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (details_)
          return details_;
        return ui_; 
      }

    public:
      explicit account_detail(std::shared_ptr<Monero::Wallet> wal, Monero::Subaddress* acct, std::size_t id)
        : ftxui::ComponentBase(),
          account_name_(wal->getSubaddressLabel(id, 0)),
          wal_(std::move(wal)),
          acct_(acct),
          title_(ftxui::text(_("Account #") + std::to_string(id))),
          address_({ftxui::text("Address: "), ftxui::text(wal_->address(id, 0))}),
          desc_(ftxui::text(_("Name: "))),
          details_(),
          table_(),
          buttons_(),
          name_(last_input(&account_name_)),
          ui_(),
          id_(id)
      {
        if (!acct_)
          throw std::runtime_error{"lwcli::account_detail given nullptr"};
        if (std::numeric_limits<std::uint32_t>::max() < id_)
          throw std::runtime_error{"lwcli::account_detail given invalid id"};

        buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { throw event::close{}; }, ascii()),
          ftxui::Button(_("Save"), [this] () {
            wal_->setSubaddressLabel(id_, 0, account_name_);
            throw event::close{};
          }, ascii()),
          ftxui::Button(_("Add Subaddress"), [this] () {
            acct_->addRow(id_, std::string{config::default_subaddress_name});
          }, ascii())
        });
 
        table_ = component::table(
          {_("#"), _("Label"), _("Address")},
          [this] () { return subaddress_list(); },
          [this] (std::size_t i) { return display_details(i); }
        );

        ui_ = ftxui::Container::Vertical({buttons_, name_, table_});
        Add(ui_);
      }

      bool display_details(const std::size_t index)
      {
        if (row_map_.find(index) == row_map_.end())
          throw std::logic_error{"DEBUG"};
        return true;
      }

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          if (details_)
            return details_->OnEvent(std::move(event));
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          ui_->OnEvent(std::move(event));
        }
        catch (const event::close&)
        {
          if (!details_)
            throw;
          details_->Detach();
          details_.reset();
        }
        return true;
      }

      std::vector<std::vector<std::string>> subaddress_list()
      {
        std::vector<std::vector<std::string>> rows{};

        acct_->refresh(id_);
        auto all = acct_->getAll();

        // reverse order sort
        std::sort(all.begin(), all.end(), [] (const auto x, const auto y) {
          return y->getRowId() < x->getRowId();
        });

        std::size_t i = 0;
        rows.reserve(all.size());
        for (Monero::SubaddressRow const* const detail : all)
        {
          const std::size_t id = detail->getRowId();
          row_map_.try_emplace(i++).first->second = id;
          rows.push_back({
            std::to_string(id),
            detail->getLabel(),
            detail->getAddress().substr(0, 20) + "..."
          });
        }

        return rows;
      }

      ftxui::Element OnRender() override final
      {
        return ftxui::window(title_, ftxui::vbox({
          buttons_->Render() | ftxui::hcenter,
          ftxui::separator(),
          ftxui::gridbox({address_, {desc_, name_->Render()}}), 
          ftxui::separator(),
          table_->Render() | ftxui::vscroll_indicator | ftxui::yframe | ftxui::center
        }));
      }
    };


    class accounts_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::Wallet> wal_;
      Monero::SubaddressAccount* const wal_accounts_;
      std::uint32_t* const account_;
      const ftxui::Element title_;
      ftxui::Component details_;
      ftxui::Component table_;
      ftxui::Element table_cached_;
      ftxui::Component buttons_;
      ftxui::Component ui_;
      std::unordered_map<std::size_t, std::size_t> row_map_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (details_)
          return details_;
        return ui_; 
      }

    public:
      explicit accounts_(std::shared_ptr<Monero::Wallet>&& wal, std::uint32_t* account)
        : ftxui::ComponentBase(),
          wal_(std::move(wal)),
          wal_accounts_(wal_->subaddressAccount()),
          account_(account),
          title_(ftxui::text(_("Accounts"))),
          details_(),
          table_(),
          table_cached_(),
          buttons_(),
          ui_(),
          row_map_()
      {
        buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Close"), [] () { throw event::close{}; }, ascii()),
          ftxui::Button(_("Add Account"), [this] () {
            wal_->addSubaddressAccount(std::string{config::default_account_name});
          }, ascii()),
        });
 
        table_ = component::table(
          {_("#"), _("Balance"), _("Label"), _("Address")},
          [this] () { return accounts_list(); },
          [this] (std::size_t i) { return display_details(i); }
        );

        ui_ = ftxui::Container::Vertical({buttons_, table_});
        Add(ui_);
      }

      bool display_details(const std::size_t index)
      {
        if (details_)
          details_->Detach();
        details_ = std::make_shared<account_detail>(wal_, wal_->subaddress(), row_map_.at(index));
        return true;
      } 

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          if (details_)
            return details_->OnEvent(std::move(event));
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          ui_->OnEvent(std::move(event));
        }
        catch (const event::close&)
        {
          if (!details_)
            throw;
          details_->Detach();
          details_.reset();
        }
        return true;
      }

      std::vector<std::vector<std::string>> accounts_list()
      {
        std::vector<std::vector<std::string>> rows{};

        wal_accounts_->refresh();
        auto all = wal_accounts_->getAll();

        // reverse order sort
        std::sort(all.begin(), all.end(), [] (const auto x, const auto y) {
          return y->getRowId() < x->getRowId();
        });

        std::size_t i = 0;
        rows.reserve(all.size());
        for (Monero::SubaddressAccountRow const* const detail : all)
        {
          const std::size_t id = detail->getRowId();
          char const* const active = id == *account_ ? "*" : "";
          row_map_.try_emplace(i++).first->second = id;
          rows.push_back({
            active + std::to_string(id),
            detail->getBalance() + " XMR",
            detail->getLabel(),
            detail->getAddress().substr(0, 12) + "..."
          });
        }

        return rows;
      }

      ftxui::Element OnRender() override final
      {
        if (!details_)
        {
          table_cached_ = ftxui::window(title_, ftxui::vbox({
            buttons_->Render() | ftxui::hcenter,
            ftxui::separator(),
            table_->Render() //| ftxui::vscroll_indicator | ftxui::yframe | ftxui::center
          }));
          return table_cached_;
        }
        return ftxui::dbox(table_cached_, decorate::overlay(details_->Render()));
      }
    };
  }

  ftxui::Component accounts(std::shared_ptr<Monero::Wallet> wal, std::uint32_t* account)
  {
    if (!wal || !account)
      throw std::invalid_argument{"views::accounts cannot be given nullptr"};
    return std::make_shared<accounts_>(std::move(wal), account);
  }

}} // lwcli // 
