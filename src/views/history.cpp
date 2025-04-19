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

#include "decorate/overlay.h"
#include "events.h"
#include "history.h"
#include "translate.h"

namespace lwcli { namespace view
{
  namespace
  {
// Notice for next two functions

// Copyright (c) 2014-2024, The Monero Project
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
// 
// Parts of this file are originally copyright (c) 2012-2013 The Cryptonote developers

    constexpr const unsigned int default_decimal_point = 12;
    void insert_money_decimal_point(std::string &s, unsigned int decimal_point)
    {
      if (decimal_point == (unsigned int)-1)
        decimal_point = default_decimal_point;
      if(s.size() < decimal_point+1)
      {
        s.insert(0, decimal_point+1 - s.size(), '0');
      }
      if (decimal_point > 0)
        s.insert(s.size() - decimal_point, ".");
    }
    std::string print_money(const std::uint64_t amount)
    {
      std::string s = std::to_string(amount);
      insert_money_decimal_point(s, default_decimal_point);
      return s;
    }

    std::string print_amount(std::uint64_t amount, int direction)
    {
      const bool negate = direction == Monero::TransactionInfo::Direction_Out;
      return (negate ? "-" : "") + print_money(amount);
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
            ftxui::text(print_money(transfer.amount) + address)
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
        fee_ = {ftxui::text(_("Fee: ")), ftxui::text(print_money(info_->fee()))};
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
      ftxui::Component overlay_;
      ftxui::Element title_;
      ftxui::Element table_; //!< Does not redraw when child is enabled
      std::vector<std::vector<std::string>> base_rows;
      std::ptrdiff_t highlighted_;
      std::unordered_map<std::size_t, int> row_map_;
      const std::uint32_t account_;

      static constexpr std::size_t min_row() noexcept { return 2; };
      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return overlay_; }

    public:
      explicit history_(std::shared_ptr<Monero::Wallet>&& wallet, std::uint32_t account)
        : ftxui::ComponentBase(),
          wallet_(std::move(wallet)),
          overlay_(nullptr),
          title_(nullptr),
          table_(nullptr),
          base_rows(),
          highlighted_(min_row() - 1),
          row_map_(),
          account_(account)
      {
        Monero::Subaddress* subaddress = wallet_->subaddress();
        if (!subaddress)
          throw std::runtime_error{"unexpected subaddress nullptr"};
        subaddress->refresh(account_);

        std::string address = _("Account ") + std::to_string(account_);
        const auto subaddresses = subaddress->getAll();
        if (!subaddresses.empty() && *subaddresses.begin())
          address += ": " + (*subaddresses.begin())->getAddress();

        title_ = ftxui::text(std::move(address));

        // perform transalation lookup once
        base_rows = {
          {_("Date"), _("Amount"), _("Payment ID"), _("Desription"), _("Block"), _("Fee"), _("Hash")},
          {  "",        "",          "",              "",              "",         "",       ""            }
        };
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
          else if (event == ftxui::Event::ArrowDown && highlighted_ < row_map_.size() + min_row() - 1)
            ++highlighted_;
          else if (event == ftxui::Event::ArrowUp && min_row() <= highlighted_)
            --highlighted_;
          else if (event == ftxui::Event::PageDown)
            highlighted_ = std::min(std::ptrdiff_t(row_map_.size() + min_row()), highlighted_ + 10);
          else if (event == ftxui::Event::PageUp)
            highlighted_ = std::max(std::ptrdiff_t(min_row()), highlighted_ - 10);
          else if (event == ftxui::Event::Return)
          {
            overlay_ = ftxui::Make<tx_details>(wallet_->history(), row_map_.at(highlighted_ - min_row()));
            Add(overlay_);
          } 
        }
        catch (const event::close&)
        {
          if (!overlay_)
            throw;
          overlay_->Detach();
          overlay_.reset();
        }
        return min_row() <= highlighted_ && highlighted_ - min_row() < row_map_.size();
      }

      ftxui::Element OnRender() override final
      {
        if (Focused())
        {
          if (highlighted_ < min_row())
            highlighted_ = min_row();
          else if (row_map_.size() <= highlighted_ - min_row())
            highlighted_ = row_map_.size() - 1 + min_row();
        }

        /* Do not redraw table when showing tx. the 
        history()->refresh() call can invalidate pointers */
        if (!overlay_)
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

          std::uint64_t balance = 0;
          std::vector<std::vector<std::string>> rows;
          rows.reserve(ordered.size() + 2);
          rows = base_rows;

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
                balance -= amount;
                balance -= fee;
              }
              else
                balance += amount;
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
            rows.push_back({
              std::string{date},
              print_amount(amount, direction),
              payment_id.substr(0, 16) + extended_payment_id,
              tx->description(),
              std::to_string(tx->blockHeight()),
              print_money(tx->fee()),
              tx->hash().substr(0, 16) + "..."
            });

            ++i;
          }

          ftxui::Table table{std::move(rows)};
          table.SelectRow(0).Decorate(ftxui::bold);
          table.SelectRow(0).SeparatorVertical(ftxui::LIGHT);
          table.SelectRow(0).Border(ftxui::LIGHT);
          //table.SelectColumn(3).DecorateCells(ftxui::xflex_grow);
          if (min_row() <= highlighted_ && highlighted_ - min_row() < row_map_.size())
          {
            auto row = table.SelectRow(highlighted_);
            row.Decorate(ftxui::inverted);
            if (Focused())
              row.Decorate(ftxui::focus);
            else if (Active())
              row.Decorate(ftxui::select);
          }
     
          table_ = ftxui::vbox({
            title_,
            ftxui::text(_("Balance: ") + print_money(balance)),
            table.Render() | ftxui::vscroll_indicator | ftxui::yframe | ftxui::center | ftxui::flex
            //ftxui::flex(ftxui::yframe(ftxui::vscroll_indicator(table.Render())))
          }); 
          return table_;
        }
        return ftxui::dbox(table_, decorate::overlay(overlay_->Render()));
      }
    };
  } // anonymous

  ftxui::Component history(std::shared_ptr<Monero::Wallet> wallet, std::uint32_t account)
  {
    return std::make_shared<history_>(std::move(wallet), account);
  }
}} // lwcli // view
