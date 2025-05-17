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

#include <ctime>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/table.hpp>
#include <lws_frontend.h>
#include <map>
#include <unordered_map>

#include "components/table.h"
#include "decorate/overlay.h"
#include "events.h"
#include "history.h"
#include "translate.h"

namespace lwcli { namespace view
{
  namespace
  {
    std::string print_amount(std::uint64_t amount, int direction)
    {
      const bool negate = direction == Monero::TransactionInfo::Direction_Out;
      return (negate ? "-" : "") + lwsf::displayAmount(amount);
    }

    std::string print_amount(const Monero::TransactionInfo& tx)
    {
      return print_amount(tx.amount(), tx.direction());
    }

    class tx_details final : public ftxui::ComponentBase
    {
      Monero::TransactionHistory* const history_;
      Monero::TransactionInfo const* info_;
      std::string note_;
      ftxui::Component note_input_;
      const ftxui::Component cancel_;
      ftxui::Component ok_;
      ftxui::Component container_;
      ftxui::Element title_;
      ftxui::Elements timestamp_;
      ftxui::Elements payment_id_;
      ftxui::Elements amount_;
      ftxui::Elements fee_;
      ftxui::Elements minors_;
      ftxui::Elements coinbase_;

      bool OnEvent(ftxui::Event event) override final
      {
        if (event == ftxui::Event::CtrlQ)
          throw event::close{};
        return container_->OnEvent(std::move(event));
      }

      virtual ftxui::Element OnRender() override final
      {
        std::vector<ftxui::Elements> grid{
          {ftxui::text(_("Description: ")), note_input_->Render()},
          timestamp_,
          payment_id_,
          {ftxui::text(_("Confirmations: ")), ftxui::text(std::to_string(info_->confirmations()))},
          amount_,
          fee_,
          {ftxui::text(_("Block Height: ")), ftxui::text(std::to_string(info_->blockHeight()))},
          minors_,
          coinbase_
        };

        const std::size_t base_size = grid.size();
        for (const auto& transfer : info_->transfers())
        {
          const std::string address = transfer.address.empty() ? 
            std::string{_(" to Unknown Address")} : _(" to ")  + transfer.address;
          std::string text;
          if (base_size == grid.size())
            text = _("Transfers: ");
          grid.push_back({
            ftxui::text(std::move(text)),
            ftxui::text(lwsf::displayAmount(transfer.amount) + address)
          });
        }

        ftxui::Elements vertical;
        vertical.reserve(4);

        vertical.push_back(ftxui::hcenter(ftxui::hbox(cancel_->Render(), ok_->Render())));
        if (info_->isFailed())
          vertical.push_back(ftxui::inverted(ftxui::hcenter(ftxui::text(_("FAILED")))));
        else if (info_->isPending())
          vertical.push_back(ftxui::inverted(ftxui::hcenter(ftxui::text(_("PENDING")))));
        else
          vertical.push_back(ftxui::separator());

        vertical.push_back(ftxui::gridbox(std::move(grid)));

        return ftxui::window(title_, ftxui::vbox(std::move(vertical)));
      }

    public:
      explicit tx_details(Monero::TransactionHistory* history, std::size_t index)
        : history_(history),
          info_(nullptr),
          note_(),
          note_input_(nullptr),
          cancel_(ftxui::Button(_("Cancel"), [] () { throw event::close{}; }, ftxui::ButtonOption::Ascii())),
          ok_(nullptr),
          container_(),
          title_(nullptr),
          timestamp_(),
          payment_id_(),
          amount_(),
          fee_(),
          minors_(),
          coinbase_()
      {
        if (!history_)
          throw std::runtime_error{"unexpected nullptr"};

        info_ = history->transaction(index);
        if (!info_)
          throw std::runtime_error{"unexpected nullptr"};
 
        title_ = ftxui::text(_("Tx ") + info_->hash());
        {
          std::tm expanded{};
          const std::time_t timestamp = info_->timestamp();
          if (!gmtime_r(std::addressof(timestamp), std::addressof(expanded)))
            throw std::runtime_error{"gmtime failure"};

          char buf[100] = {0};
          if (sizeof(buf) <= std::strftime(buf, sizeof(buf), "%B %m %Y %I:%M:%S", std::addressof(expanded)))
            throw std::runtime_error{"strftime failed"};
          timestamp_ = {ftxui::text(_("Timestamp: ")), ftxui::text(buf)};
        }
        payment_id_ = {ftxui::text(_("Payment ID: ")), ftxui::text(info_->paymentId())};
        amount_ = {ftxui::text(_("Amount: ")), ftxui::text(print_amount(*info_))};
        fee_ = {ftxui::text(_("Fee: ")), ftxui::text(lwsf::displayAmount(info_->fee()))};
        {
          std::string minors;
          for (const std::uint32_t minor : info_->subaddrIndex())
          {
            if (!minors.empty())
              minors.append(", ");
            minors.append(std::to_string(minor));
          }
          minors_ = {ftxui::text(_("Subaddress Minor: ")), ftxui::text(std::move(minors))};
        }
        coinbase_ = {ftxui::text(_("Coinbase: ")), ftxui::text(info_->isCoinbase() ? _("Yes"): _("No"))};

        note_ = info_->description();
        auto options = ftxui::InputOption::Default();
        options.cursor_position = note_.size();
        note_input_ = ftxui::Input(&note_, std::move(options));

        ok_ = ftxui::Button(_("OK"), [this] () {
          history_->setTxNote(info_->hash(), note_);
          throw event::close{};
        }, ftxui::ButtonOption::Ascii());

        container_ = ftxui::Container::Vertical({
          ftxui::Container::Horizontal({cancel_, ok_}),
          note_input_
        });
      }
    };

    class history_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::Wallet> wallet_;
      ftxui::Component table_;
      ftxui::Component overlay_;
      ftxui::Element title_;
      ftxui::Element table_cached_; //!< Does not redraw when child is enabled
      std::unordered_map<std::size_t, std::size_t> row_map_;
      std::uint64_t balance_;
      const std::uint32_t account_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (overlay_)
          return overlay_;
        return table_;
      }

    public:
      explicit history_(std::shared_ptr<Monero::Wallet>&& wallet, std::uint32_t account)
        : ftxui::ComponentBase(),
          wallet_(std::move(wallet)),
          table_(),
          overlay_(nullptr),
          title_(nullptr),
          table_cached_(nullptr),
          row_map_(),
          account_(account)
      {
        if (!wallet_)
          throw std::invalid_argument{"lwcli::view::history given nullptr"};

        {
          std::string address = _("Account #") + std::to_string(account_);
          address.append(" / ").append(wallet_->getSubaddressLabel(account_, 0)).append(" / ");
          address.append(wallet_->address(account_, 0).substr(0, 20)).append("...");
          title_ = ftxui::text(std::move(address));
        }

        // perform transalation lookup once
        table_ = component::table(
          {_("Date"), _("Amount"), _("Payment ID"), _("Label"), _("Desription"), _("Block"), _("Fee"), _("Hash")},
          [this] () { return transaction_list(); },
          [this] (ftxui::Event e, std::size_t i) { return add_overlay(e, i); }
        );

        Add(table_);
      }

      bool add_overlay(const ftxui::Event& e, const std::size_t i)
      {
        if (e == ftxui::Event::Return)
        {
          if (overlay_)
            overlay_->Detach();
          overlay_ = std::make_shared<tx_details>(wallet_->history(), row_map_.at(i));
          Add(overlay_);
        }
        return true;
      }

      bool OnEvent(ftxui::Event event) override final
      {
        try
        {
          if (overlay_)
          {
            overlay_->OnEvent(std::move(event));
            return true;
          }
          else if (event == ftxui::Event::CtrlQ)
            throw event::close{};
          else if (event.is_character())
            return false;
          else
            return table_->OnEvent(std::move(event));
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

      std::vector<std::vector<std::string>> transaction_list()
      {
        Monero::TransactionHistory* tx_history = wallet_->history();
        if (!tx_history)
          throw std::runtime_error{"unexpected history nullptr"};

        std::map<std::pair<std::uint64_t, std::string>, std::pair<const Monero::TransactionInfo*, std::size_t>, std::greater<>> ordered;
        tx_history->refresh();
        const auto history = tx_history->getAll();
        for (std::size_t i = 0; i < history.size(); ++i)
        {
          const Monero::TransactionInfo* tx = history[i];
          if (!tx)
            throw std::runtime_error{"unexpected tx_info nullptr"};

          if (tx->subaddrAccount() == account_)
            ordered.try_emplace({tx->blockHeight(), tx->hash()}, tx, i);
        }

        balance_ = 0;
        std::vector<std::vector<std::string>> rows;
        rows.reserve(ordered.size() + 2);

        std::size_t i = 0;
        row_map_.clear();
        for (const auto& entry : ordered)
        {
          const Monero::TransactionInfo* tx = entry.second.first;
          row_map_.emplace(i, entry.second.second);
         
          const std::uint64_t amount = tx->amount();
          const int direction = tx->direction();
          const std::uint64_t fee = tx->fee();

        
          if (!tx->isFailed())
          {
            if (direction == Monero::TransactionInfo::Direction_Out)
            {
              balance_ -= amount;
              balance_ -= fee;
            }
            else
              balance_ += amount;
          }

          std::tm expanded{};
          const std::time_t timestamp = tx->timestamp();
          if (!gmtime_r(std::addressof(timestamp), std::addressof(expanded)))
            throw std::runtime_error{"gmtime failure"};

          char date[12] = {0};
          if (sizeof(date) - 1 != std::strftime(date, sizeof(date), "%Y/%m/%d ", std::addressof(expanded)))
            throw std::runtime_error{"strftime failed"};

          std::string payment_id = tx->paymentId();
          if (payment_id.size() == 16 && payment_id.find_first_not_of('0') == std::string::npos)
            payment_id.clear();

          char const* const extended_payment_id = 16 < payment_id.size() ?
            "..." : "";

          std::string label;
          const std::set<std::uint32_t> subaddrs = tx->subaddrIndex();
          if (!subaddrs.empty())
          {
            const std::uint32_t index = *subaddrs.begin();
            if (index)
              label = wallet_->getSubaddressLabel(account_, index);
          }

          rows.push_back({
            std::string{date},
            print_amount(amount, direction),
            payment_id.substr(0, 16) + extended_payment_id,
            std::move(label),
            tx->description(),
            std::to_string(tx->blockHeight()),
            lwsf::displayAmount(tx->fee()),
            tx->hash().substr(0, 16) + "..."
          });

          ++i;
        }

        return rows;
      }

      ftxui::Element OnRender() override final
      {
        /* Do not redraw table when showing tx. the 
        history()->refresh() call can invalidate pointers */
        if (!overlay_)
        {
          auto table = table_->Render(); // compute balance first in callback 
          table_cached_ = ftxui::vbox({
            title_,
            ftxui::text(_("Balance: ") + lwsf::displayAmount(balance_)),
            std::move(table) | ftxui::vscroll_indicator | ftxui::yframe | ftxui::center | ftxui::flex
          }); 
          return table_cached_;
        }
        return ftxui::dbox(table_cached_, decorate::overlay(overlay_->Render()));
      }
    };
  } // anonymous

  ftxui::Component history(std::shared_ptr<Monero::Wallet> wallet, std::uint32_t account)
  {
    return std::make_shared<history_>(std::move(wallet), account);
  }
}} // lwcli // view
