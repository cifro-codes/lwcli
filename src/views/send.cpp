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

#include <array>
#include <ftxui/component/animation.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/table.hpp>
#include <future>
#include <lws_frontend.h>

#include "components/table.h"
#include "decorate/overlay.h"
#include "events.h"
#include "lwcli_config.h"
#include "translate.h"
#include "util.h"
#include "views/book.h"

namespace lwcli { namespace view
{
  namespace
  {
    using dest_pair = std::pair<std::string, std::string>;
    using dest_group = std::pair<std::vector<std::string>, std::vector<std::uint64_t>>;

    constexpr const std::array<char, 4> spinner{{'|', '/', '-', '\\'}};

    ftxui::Component last_input(std::string* str)
    {
      auto opt = ftxui::InputOption::Default();
      opt.cursor_position = str->size();
      opt.multiline = false;
      return ftxui::Input(str, std::move(opt));
    }

    ftxui::ButtonOption ascii() { return ftxui::ButtonOption::Ascii(); }

    class confirm_ final : public ftxui::ComponentBase
    {
      bool* confirmed_;
      const std::shared_ptr<Monero::PendingTransaction> tx_;
      const ftxui::Element title_;
      ftxui::Element info_;
      ftxui::Element error_;
      ftxui::Component buttons_;
      std::future<bool> sending_;
      unsigned animation_;
      bool sent_;
      bool closing_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return buttons_; }

    public:
      explicit confirm_(std::shared_ptr<Monero::PendingTransaction>&& tx, bool* confirmed)
        : ftxui::ComponentBase(),
          confirmed_(confirmed),
          tx_(std::move(tx)),
          title_(ftxui::text(_("Sending Tx(es)"))),
          info_(),
          error_(),
          buttons_(),
          sending_(),
          animation_(0),
          sent_(false),
          closing_(false)
      {}

      static void set_ui(std::shared_ptr<confirm_> self, dest_group&& dests)
      {
        if (!self)
          return;

        {
          std::vector<std::vector<ftxui::Element>> grid;
          grid.reserve(dests.first.size() + 3);

          grid.push_back({ftxui::text(_("Sending: ")), ftxui::text(lwsf::displayAmount(self->tx_->amount()) + " XMR")});
          grid.push_back({ftxui::text(_("Fee: ")), ftxui::text(lwsf::displayAmount(self->tx_->fee()) + " XMR")});
          {
            std::vector<std::string> ids = self->tx_->txid();

            ftxui::Elements rows;
            rows.reserve(ids.size());

            for (std::string& id : ids)
              rows.push_back(ftxui::text(std::move(id)));

            grid.push_back({ftxui::text(_("TX IDs:")), ftxui::vbox(std::move(rows))});
          }

          for (std::size_t i = 0; i < dests.first.size(); ++i)
            grid.push_back({ftxui::text(lwsf::displayAmount(dests.second.at(i)) + " XMR to "), ftxui::text(std::move(dests.first.at(i)))});
          
          self->info_ = ftxui::gridbox(std::move(grid));
        }

        const std::weak_ptr<confirm_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Send/Confirm"), [weak] () { send_tx(weak.lock()); }, ascii())
        });
  
        self->Add(self->buttons_);
      }

      static void send_tx(std::shared_ptr<confirm_> self)
      {
        const auto tx_commit = [] (std::shared_ptr<Monero::PendingTransaction> tx)
        {
          return tx->commit();
        };

        if (self)
        {
          self->closing_ = true;
          self->sending_ = std::async(std::launch::async, tx_commit, self->tx_);
        }
      }
 
      bool OnEvent(ftxui::Event evt) override final
      {
        const bool send_async = (evt == event::send_async);
        if (!evt.is_mouse() && !send_async)
          error_.reset();

        if (evt == event::close())
        {
          closing_ = true;
          if (!sending_.valid())
            return false;
        }
        else if (send_async && closing_)
        {
          if (confirmed_)
            *confirmed_ = sent_;
          if (!sending_.valid())
            event::send(event::close());
        }
        else if (!closing_)
          buttons_->OnEvent(std::move(evt));

        return true;
      }

      ftxui::Element OnRender() override final
      {
        ftxui::Elements rows;
        rows.reserve(4);

        bool animate = false;
        if (sending_.valid())
        {
          if (sending_.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
          {
            sent_ = sending_.get();
            if (!sent_)
              error_ = ftxui::text(tx_->errorString());
            else
              error_.reset();

            event::send(event::send_async);
          }
          else
          {
            animate = true; 
            animation_ = (animation_ + 1) % spinner.size();
            error_ = ftxui::text(std::string{spinner[animation_]} + _(" Sending ") + spinner[animation_]);
            ftxui::animation::RequestAnimationFrame();
          }
        }

        if (!closing_ && !animate) 
          rows.push_back(buttons_->Render() | ftxui::hcenter);

        if (error_)
          rows.push_back(decorate::banner(error_) | ftxui::inverted); 
        else
          rows.push_back(ftxui::separator());

        if (closing_)
          rows.push_back(ftxui::text(_("...Waiting for Tx Send...")));
        rows.push_back(info_);

        return ftxui::window(title_, ftxui::vbox(std::move(rows)));
      }
    };

    ftxui::Component confirm(std::shared_ptr<Monero::PendingTransaction> tx, dest_group dests, bool* confirm)
    {
      auto self = std::make_shared<confirm_>(std::move(tx), confirm);
      confirm_::set_ui(self, std::move(dests));
      return self;
    }

    ftxui::Component book(std::shared_ptr<Monero::Wallet> wal, std::shared_ptr<dest_pair> dest)
    {
      return nullptr; //return std::make_shared<book_>(std::move(wm), std::move(wal), std::move(dest));
    }

    class send_ final : public ftxui::ComponentBase
    {
      using buttons_tuple =
        std::tuple<ftxui::Component, ftxui::Component, ftxui::Component, ftxui::Component>; 

      const std::shared_ptr<Monero::WalletManager> wm_;
      const std::shared_ptr<Monero::Wallet> wal_;
      const ftxui::Element title_;
      const std::vector<std::string> priority_names_;
      std::vector<std::shared_ptr<dest_pair>> dests_;
      std::vector<buttons_tuple> dests_ui_;
      unsigned animation_;
      int priority_;
      const ftxui::Decorator min_amount_size;
      ftxui::Component overlay_;
      ftxui::Component buttons_;
      const ftxui::Component priority_menu_;
      ftxui::Element error_;
      ftxui::Component ui_;
      ftxui::Element cached_;
      std::future<std::tuple<std::string, std::shared_ptr<dest_pair>, bool>> oa_;
      std::future<std::tuple<std::shared_ptr<Monero::PendingTransaction>, dest_group, std::string>> tx_;
      const std::uint32_t account_;
      bool closing_;
      bool confirmed_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (overlay_)
          return overlay_;
        return ui_; 
      }

      static ftxui::MenuOption toggle(const int focused)
      {
        auto options = ftxui::MenuOption::Toggle();
        options.focused_entry = focused;
        return options;
      }

    public:
      explicit send_(std::shared_ptr<Monero::WalletManager>&& wm, std::shared_ptr<Monero::Wallet>&& wal, const std::uint32_t account)
        : ftxui::ComponentBase(),
          wm_(std::move(wm)),
          wal_(std::move(wal)),
          title_(ftxui::text(_("Send from account #") + std::to_string(account) + " (" + lwsf::displayAmount(wal_->unlockedBalance(account)) + " XMR available)")),
          priority_names_({_("Auto"), _("Unimportant"), _("Normal"), _("Elevated"), _("Priority")}),
          dests_(),
          dests_ui_(),
          animation_(0),
          priority_(2),
          min_amount_size(ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 5)),
          overlay_(),
          buttons_(),
          priority_menu_(ftxui::Menu(&priority_names_, &priority_, toggle(priority_))),
          error_(),
          ui_(),
          cached_(),
          oa_(),
          tx_(),
          account_(account),
          closing_(false),
          confirmed_(false)
      {}

      static void set_ui(std::shared_ptr<send_> self)
      {
        if (!self)
          return;

        const std::weak_ptr<send_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Add Dest"), [weak] () { add_dest(weak.lock()); }, ascii()),
          ftxui::Button(_("Construct Tx"), [weak] () { try_construct(weak.lock()); }, ascii())
        });

        add_dest(self);
      }

      static void add_dest(std::shared_ptr<send_> self)
      {
        if (!self)
          return;

        self->dests_.push_back(std::make_shared<std::pair<std::string, std::string>>());

        const std::weak_ptr<send_> weak{self};
        const std::shared_ptr<dest_pair> dest = self->dests_.back();
        const std::size_t elem = self->dests_ui_.size();
        self->dests_ui_.emplace_back(
          last_input(&self->dests_.back()->first),
          last_input(&self->dests_.back()->second),
          ftxui::Button(_("Book"), [weak, dest] () {
            if (auto self = weak.lock(); self)
              self->overlay_ = book(self->wal_, dest);
          }, ascii()),
          ftxui::Button(_("Remove"), [weak, elem] () { remove_dest(weak.lock(), elem); }, ascii())
        );

        self->update_ui();
      }

      static void remove_dest(std::shared_ptr<send_> self, const std::size_t elem)
      {
        if (!self)
          return;

        self->dests_ui_.erase(self->dests_ui_.begin() + elem);
        self->dests_.erase(self->dests_.begin() + elem);

        const std::weak_ptr<send_> weak{self};
        for (std::size_t i = 0; i < self->dests_ui_.size(); ++i)
          std::get<3>(self->dests_ui_.at(i)) = ftxui::Button(_("Remove"), [weak, i] () { remove_dest(weak.lock(), i); }, ascii());

        self->update_ui();
      }

      void update_ui()
      {
        ftxui::Components ui;
        ui.reserve(dests_ui_.size() + 2);
        ui.push_back(buttons_);
        ui.push_back(priority_menu_);
        for (const auto& e : dests_ui_)
          ui.push_back(ftxui::Container::Horizontal({std::get<0>(e), std::get<1>(e), std::get<2>(e), std::get<3>(e)}));

        if (ui_)
          ui_->Detach();
        ui_ = ftxui::Container::Vertical(std::move(ui));
        Add(ui_);
      }

      static void try_construct(std::shared_ptr<send_> self)
      {
        if (self)
          return self->try_construct();
      }

      void try_construct()
      {
        if (oa_.valid() || tx_.valid())
          return;

        if (dests_.empty())
        {
          error_ = ftxui::text("Must have one destination");
          return;
        }

        dest_group dests;

        dests.first.reserve(dests_.size());
        dests.second.reserve(dests_.size());

        for (const auto& dest : dests_)
        {
          const std::optional<std::uint64_t> amount = lwsf::amountFromString(dest->first);
          if (!amount || (*amount == 0 && dests_.size() != 1))
          {
            error_ = ftxui::text("Invalid amount");
            return;
          }
          dests.second.push_back(*amount);

          if (!lwsf::addressValid(dest->second, wal_->nettype()))
          {
            if (dest->second.find_first_of(u8'.') == std::string::npos)
            {
              error_ = ftxui::text(_("Invalid Address/OpenAlias"));
              return;
            }
            const auto oa_lookup = [dest] (std::shared_ptr<Monero::WalletManager> wm, const std::string& uri)
              -> std::tuple<std::string, std::shared_ptr<dest_pair>, bool>
            {
              bool dnssec = false;
              std::string result = wm->resolveOpenAlias(uri, dnssec);
              return std::make_tuple(std::move(result), dest, dnssec);
            };

            if (!wm_)
              throw std::runtime_error{"WalletManager is nullptr"};
            oa_ = std::async(std::launch::async, oa_lookup, wm_, dest->second);
            return;
          }
          dests.first.push_back(dest->second);
        }

        const auto tx_construct = [] (std::shared_ptr<Monero::Wallet> wal, dest_group dests, const std::uint32_t account, const int priority)
          -> std::tuple<std::shared_ptr<Monero::PendingTransaction>, dest_group, std::string>
        {
          const auto dispose = [wal] (Monero::PendingTransaction* ptr)
          {
            if (ptr)
              wal->disposeTransaction(ptr);
          };

          Monero::optional<std::vector<std::uint64_t>> amounts;
          if (1 <= dests.second.size() && dests.second.back() != 0)
            amounts = dests.second;

          std::shared_ptr<Monero::PendingTransaction> tx{
            wal->createTransactionMultDest(dests.first, {}, std::move(amounts), 0 /*mixin_count*/, Monero::PendingTransaction::Priority(priority), account),
            dispose
          };
          if (!tx)
            return {nullptr, {}, "Unexpected nullptr tx"};
          if (tx->status() == Monero::PendingTransaction::Status_Ok)
            return {std::move(tx), std::move(dests), {}};
          return {nullptr, {}, tx->errorString()};
        };

        tx_ = std::async(std::launch::async, tx_construct, wal_, std::move(dests), account_, priority_);
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        const bool is_waiting = oa_.valid() || tx_.valid();

        if (!evt.is_mouse() && evt != event::send_async)
          error_.reset();

        if (overlay_)
        {
          if (!overlay_->OnEvent(evt) && evt == event::close())
          {
            overlay_->Detach();
            overlay_.reset();
            if (confirmed_)
            {
              closing_ = true;
              return false;
            }
          }
        }
        else if (evt == event::close())
        {
          closing_ = true;
          if (!is_waiting)
            return false;
        }
        else if (closing_ && evt == event::send_async)
          return event::send(event::close());
        else if (!is_waiting)
          ui_->OnEvent(std::move(evt));

        return true;
      }

      ftxui::Element OnRender() override final
      {
        bool animate = false;
        if (oa_.valid() || tx_.valid())
        {
          animate = true;
          if (oa_.valid() && oa_.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
          {
            auto oa = oa_.get();
            error_.reset();

            if (std::get<0>(oa).empty())
              error_ = ftxui::text(_("No XMR OpenAlias found for ") + std::get<1>(oa)->second);
            else if (!lwsf::addressValid(std::get<0>(oa), wal_->nettype()))
              error_ = ftxui::text(_("OpenAlias record is invalid for ") + std::get<1>(oa)->second);
            else if (!closing_)
            {
              std::get<1>(oa)->second = std::move(std::get<0>(oa));
              if (!std::get<2>(oa))
                error_ = ftxui::text(_("dnssec verification failure for ") + std::get<1>(oa)->second);
              else
                try_construct();
            }

            animate = oa_.valid();
          }
          else if (tx_.valid() && tx_.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
          {
            animate = false;
            auto tx = tx_.get();
            if (std::get<0>(tx))
              overlay_ = confirm(std::move(std::get<0>(tx)), std::move(std::get<1>(tx)), &confirmed_);
            else
              error_ = ftxui::text(std::move(std::get<2>(tx)));
          }

          if (animate)
          {
            char const* const label = oa_.valid() ?
              _(" OpenAlias Lookup ") : _(" Constructing Transaction ");
            animation_ = (animation_ + 1) % spinner.size();
            error_ = ftxui::text(std::string{spinner[animation_]} + label + spinner[animation_]);
            ftxui::animation::RequestAnimationFrame();
          }
          else
            event::send(event::send_async);
        }

        if (!overlay_)
        {
          ftxui::Elements rows;
          rows.reserve(5);

          if (!animate)
            rows.push_back(buttons_->Render() | ftxui::hcenter);

          if (error_)
            rows.push_back(decorate::banner(error_) | ftxui::inverted);
          else
            rows.push_back(ftxui::separator());

          if (!animate)
          {
            rows.push_back(priority_menu_->Render() | ftxui::hcenter);
            //rows.push_back(ftxui::hbox({ftxui::filler(), priority_menu_->Render(), ftxui::filler()}));
            rows.push_back(ftxui::separator());
          }

          if (!closing_)
          {
            std::vector<std::vector<ftxui::Element>> grid;
            grid.reserve(dests_ui_.size());
            for (const auto& e : dests_ui_)
            {
              ftxui::Elements row;
              row.reserve(6);
              row.push_back(std::get<0>(e)->Render() | min_amount_size);
              row.push_back(ftxui::text(" XMR to "));
              row.push_back(std::get<1>(e)->Render());
              if (!animate)
              {
                row.push_back(ftxui::separator());
                row.push_back(std::get<2>(e)->Render());
                row.push_back(std::get<3>(e)->Render());
              }
              grid.push_back(std::move(row));
            }
            rows.push_back(ftxui::gridbox(std::move(grid)));
          }
          else
            rows.push_back(ftxui::text(_("...Cleaning Up...")) | ftxui::hcenter);

          cached_ = ftxui::window(title_, ftxui::vbox(std::move(rows)));
          return cached_;
        }
        return ftxui::dbox(cached_, decorate::overlay(overlay_->Render()));
      }
    };
  }

  ftxui::Component send(std::shared_ptr<Monero::WalletManager> wm, std::shared_ptr<Monero::Wallet> wal, const std::uint32_t account)
  {
    if (!wal)
      throw std::invalid_argument{"views::send cannot be given nullptr"};
    auto self = std::make_shared<send_>(std::move(wm), std::move(wal), account);
    send_::set_ui(self);
    return self;
  }
}} // lwcli // view
