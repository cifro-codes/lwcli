// Copyright (c) 2026, Cifro Codes LLC
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

#include "book.h"

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
    constexpr const std::array<char, 4> spinner{{'|', '/', '-', '\\'}};

    ftxui::Component last_input(std::string* str)
    {
      auto opt = ftxui::InputOption::Default();
      opt.cursor_position = str->size();
      opt.multiline = false;
      return ftxui::Input(str, std::move(opt));
    }

    ftxui::ButtonOption ascii() { return ftxui::ButtonOption::Ascii(); }

    class new_entry_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::WalletManager> wm_;
      const std::shared_ptr<Monero::Wallet> wal_;
      const ftxui::Element title_;
      dest_pair entry_;
      const ftxui::Element address_;
      const ftxui::Component address_ui_;
      const ftxui::Element description_;
      const ftxui::Component description_ui_;
      ftxui::Element error_;
      ftxui::Component buttons_;
      ftxui::Component ui_;
      std::future<std::tuple<std::string, bool>> oa_;
      unsigned animation_;
      bool closing_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return ui_; }

      static void save(std::shared_ptr<new_entry_> self)
      {
        if (!self)
          return;

        if (!lwsf::addressValid(self->entry_.first, self->wal_->nettype()))
        {
          self->error_ = ftxui::text(_("Invalid address"));
          return;
        }

        Monero::AddressBook* book = self->wal_->addressBook();
        if (!book)
          throw std::runtime_error{"Unexpected address book nullptr"};

        book->addRow(self->entry_.first, "", self->entry_.second);
        event::send(event::close());
      }

      static void lookup(std::shared_ptr<new_entry_> self)
      {
        if (!self)
          return;

        if (self->entry_.first.find_first_of(u8'.') == std::string::npos)
        {
          self->error_ = ftxui::text(_("Invalid OpenAlias"));
          return;
        }

        const auto oa_lookup = [self] (const std::string uri) -> std::tuple<std::string, bool>
        {
          bool dnssec = false;
          std::string result = self->wm_->resolveOpenAlias(uri, dnssec);
          return std::make_tuple(std::move(result), dnssec);
        };

        self->oa_ = std::async(std::launch::async, oa_lookup, self->entry_.first);
      }

    public:
      explicit new_entry_(std::shared_ptr<Monero::WalletManager>&& wm, std::shared_ptr<Monero::Wallet>&& wal)
        : ftxui::ComponentBase(),
          wm_(std::move(wm)),
          wal_(std::move(wal)),
          title_(ftxui::text(_("New Book Entry"))),
          entry_(),
          address_(ftxui::text(_("Address: "))),
          address_ui_(last_input(&entry_.first)),
          description_(ftxui::text(_("Description: "))),
          description_ui_(last_input(&entry_.second)),
          error_(),
          buttons_(),
          ui_(),
          oa_(),
          animation_(0),
          closing_(false)
      {}

      static void set_ui(std::shared_ptr<new_entry_> self)
      {
        if (!self)
          return;

        const std::weak_ptr<new_entry_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Save"), [weak] () { save(weak.lock()); }, ascii()),
          ftxui::Button(_("OpenAlias Lookup"), [weak] () { lookup(weak.lock()); }, ascii()),
        });

        self->ui_ = ftxui::Container::Vertical({self->buttons_, self->address_ui_, self->description_ui_});
        self->Add(self->ui_);
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        const bool lookup_async = (evt == event::send_async);
        if (!evt.is_mouse() && !lookup_async)
          error_.reset();

        if (evt == event::close())
        {
          closing_ = true;
          if (!oa_.valid())
            return false;
        }
        else if (lookup_async && closing_)
        {
          if (!oa_.valid())
            event::send(event::close());
        }
        else if (!closing_)
          ui_->OnEvent(std::move(evt));

        return true;
      }

      ftxui::Element OnRender() override final
      { 
        bool animate = false;
        if (oa_.valid())
        {
          if (oa_.wait_for(std::chrono::seconds{0}) == std::future_status::ready)
          {
            auto oa = oa_.get();
            if (!std::get<1>(oa))
              error_ = ftxui::text(_("OpenAlias DNSSEC issue"));
            else
              error_.reset();

            entry_.first = std::move(std::get<0>(oa));
            event::send(event::send_async);
          }
          else
          {
            animate = true; 
            animation_ = (animation_ + 1) % spinner.size();
            error_ = ftxui::text(std::string{spinner[animation_]} + _(" fetching ") + spinner[animation_]);
            ftxui::animation::RequestAnimationFrame();
          }
        }

        ftxui::Elements rows;
        rows.reserve(4);
        if (!closing_ && !animate) 
          rows.push_back(buttons_->Render() | ftxui::hcenter);

        if (error_)
          rows.push_back(decorate::banner(error_) | ftxui::inverted); 
        else
          rows.push_back(ftxui::separator());

        if (closing_)
          rows.push_back(ftxui::text(_("...Waiting for DNS...")));

        rows.push_back(ftxui::gridbox({{address_, address_ui_->Render()}, {description_, description_ui_->Render()}}));

        return ftxui::window(title_, ftxui::vbox(std::move(rows)));
      }
    };

    ftxui::Component new_entry(std::shared_ptr<Monero::WalletManager> wm, std::shared_ptr<Monero::Wallet> wal)
    {
      auto self = std::make_shared<new_entry_>(std::move(wm), std::move(wal));
      new_entry_::set_ui(self);
      return self;
    }

    class edit_entry_ final : public ftxui::ComponentBase
    {
      const std::shared_ptr<Monero::WalletManager> wm_;
      const std::shared_ptr<Monero::Wallet> wal_;
      const ftxui::Element title_;
      std::string entry_;
      const ftxui::Element address_;
      const ftxui::Element address_ui_;
      const ftxui::Element description_;
      const ftxui::Component description_ui_;
      ftxui::Component buttons_;
      ftxui::Component ui_;
      const std::size_t i_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final { return ui_; }

      static void save(std::shared_ptr<edit_entry_> self)
      {
        if (!self)
          return;

        Monero::AddressBook* book = self->wal_->addressBook();
        if (!book)
          throw std::runtime_error{"Unexpected address book nullptr"};

        book->setDescription(self->i_, self->entry_);
        event::send(event::close());
      }

    public:
      explicit edit_entry_(std::shared_ptr<Monero::WalletManager>&& wm, std::shared_ptr<Monero::Wallet>&& wal, std::string&& address, std::string&& entry, const std::size_t i)
        : ftxui::ComponentBase(),
          wm_(std::move(wm)),
          wal_(std::move(wal)),
          title_(ftxui::text(_("Edit Book Entry"))),
          entry_(std::move(entry)),
          address_(ftxui::text(_("Address: "))),
          address_ui_(ftxui::text(std::move(address))),
          description_(ftxui::text(_("Description: "))),
          description_ui_(last_input(&entry_)),
          buttons_(),
          ui_(),
          i_(i)
      {}

      static void set_ui(std::shared_ptr<edit_entry_> self)
      {
        if (!self)
          return;

        const std::weak_ptr<edit_entry_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Cancel"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Save"), [weak] () { save(weak.lock()); }, ascii()),
        });

        self->ui_ = ftxui::Container::Vertical({self->buttons_, self->description_ui_});
        self->Add(self->ui_);
      }

      bool OnEvent(ftxui::Event evt) override final
      {
        if (evt == event::close())
          return false;
        ui_->OnEvent(std::move(evt));
        return true;
      }

      ftxui::Element OnRender() override final
      { 
        ftxui::Elements rows;
        rows.reserve(3);
        rows.push_back(buttons_->Render() | ftxui::hcenter);
        rows.push_back(ftxui::separator());
        rows.push_back(ftxui::gridbox({{address_, address_ui_}, {description_, description_ui_->Render()}}));

        return ftxui::window(title_, ftxui::vbox(std::move(rows)));
      }
    };

    ftxui::Component make_edit_entry(std::shared_ptr<Monero::WalletManager> wm, std::shared_ptr<Monero::Wallet> wal, const std::size_t i)
    {
      Monero::AddressBook const* const book = wal->addressBook();
      if (!book)
        throw std::runtime_error{"Unexpected nullptr address book"};

      const auto rows = book->getAll();
      auto row = rows.begin();
      for ( ; row != rows.end(); ++row)
        if (*row && (*row)->getRowId() == i)
          break;
      if (row == rows.end())
        throw std::runtime_error{"Unexpected row not found"};

      auto self = std::make_shared<edit_entry_>(std::move(wm), std::move(wal), (*row)->getAddress(), (*row)->getDescription(), i);
      edit_entry_::set_ui(self);
      return self;
    }

    class book_ final : public std::enable_shared_from_this<book_>, public ftxui::ComponentBase
    {
      using buttons_tuple =
        std::tuple<ftxui::Element, ftxui::Component, ftxui::Component, ftxui::Component>;

      const std::shared_ptr<Monero::WalletManager> wm_;
      const std::shared_ptr<Monero::Wallet> wal_;
      const std::shared_ptr<dest_pair> out_;
      const ftxui::Element title_;
      ftxui::Component buttons_;
      std::string filter_;
      ftxui::Component filter_ui_;
      ftxui::Component ui_;
      ftxui::Component overlay_;
      std::vector<buttons_tuple> entries_;
      ftxui::Element cached_;

      bool Focusable() const override final { return true; }
      ftxui::Component ActiveChild() override final
      {
        if (overlay_)
          return overlay_;
        return ui_; 
      }

      static void add_entry(std::shared_ptr<book_> self)
      {
        if (!self)
          return;
        self->overlay_ = new_entry(self->wm_, self->wal_);
        self->Add(self->overlay_);
      }

      static void edit_entry(std::shared_ptr<book_> self, const std::size_t i)
      {
        if (!self)
          return;

        self->overlay_ = make_edit_entry(self->wm_, self->wal_, i);
        self->Add(self->overlay_);
      }

      static void use_entry(std::shared_ptr<book_> self, const std::size_t i)
      {
        if (!self)
          return;

        Monero::AddressBook const* const book = self->wal_->addressBook();
        if (!book)
          throw std::runtime_error{"Unexpected AddressBook nullptr"};

        const auto rows = book->getAll();
        for (Monero::AddressBookRow const* const row : rows)
        {
          if (row && row->getRowId() == i)
            std::get<1>(*self->out_) = row->getAddress();
        }

        event::send(event::close());
      }

      static void remove_entry(std::shared_ptr<book_> self, const std::size_t i)
      {
        if (!self)
          return;

        auto book = self->wal_->addressBook();
        if (!book || !book->deleteRow(i))
          throw std::runtime_error{"Unable to delete AddressBookRow"};

        self->update_ui(self, *book);
      }

      void update_ui(const std::weak_ptr<book_> weak, Monero::AddressBook& book)
      {
        ftxui::Components ui;
        ui.push_back(buttons_);
        ui.push_back(filter_ui_);

        book.refresh();
        entries_.clear();

        const auto get_entry = [&weak] (Monero::AddressBookRow const* const row) -> buttons_tuple
        {
          if (!row)
            throw std::logic_error{"unexpected AddressBookRow nullptr"};
          const std::size_t i = row->getRowId();
          return {
            ftxui::text(row->getDescription()),
            ftxui::Button(_("Select"), [weak, i] () { use_entry(weak.lock(), i); }, ascii()),
            ftxui::Button(_("Edit"), [weak, i] () { edit_entry(weak.lock(), i); }, ascii()),
            ftxui::Button(_("Delete"), [weak, i] () { remove_entry(weak.lock(), i); }, ascii())
          };
        };

        auto rows = book.getAll();
        for (auto row = rows.begin(); row != rows.end(); )
        {
          if (*row && (*row)->getDescription().find(filter_) == std::string::npos)
            row = rows.erase(row);
          else
            ++row;
        }

        std::sort(rows.begin(), rows.end(), [] (auto l, auto r)  { return l && r && l->getDescription() < r->getDescription(); });

        for (Monero::AddressBookRow* row : rows)
        {
          const auto& e = entries_.emplace_back(get_entry(row));
          ui.push_back(ftxui::Container::Horizontal({std::get<1>(e), std::get<2>(e), std::get<3>(e)}));
        }

        if (ui_)
          ui_->Detach();
        ui_ = ftxui::Container::Vertical(std::move(ui));
        Add(ui_);
      } 

      void update_ui(const std::weak_ptr<book_> weak)
      {
        Monero::AddressBook* book = wal_->addressBook();
        if (book)
          update_ui(weak, *book);
      }

    public:
      explicit book_(std::shared_ptr<Monero::WalletManager>&& wm, std::shared_ptr<Monero::Wallet>&& wal, std::shared_ptr<dest_pair>&& out)
        : std::enable_shared_from_this<book_>(),
          ftxui::ComponentBase(),
          wm_(std::move(wm)),
          wal_(std::move(wal)),
          out_(std::move(out)),
          title_(ftxui::text(_("Address Book"))),
          buttons_(),
          filter_(),
          filter_ui_(),
          ui_(),
          overlay_(),
          entries_(),
          cached_()
      {}

      static void set_ui(std::shared_ptr<book_> self)
      {
        if (!self)
          return;

        const std::weak_ptr<book_> weak{self};
        self->buttons_ = ftxui::Container::Horizontal({
          ftxui::Button(_("Close"), [] () { event::send(event::close()); }, ascii()),
          ftxui::Button(_("Add Entry"), [weak] () { add_entry(weak.lock()); }, ascii())
        });

        {
          auto opt = ftxui::InputOption::Default();
          auto on_change = opt.on_change;
          opt.cursor_position = self->filter_.size();
          opt.multiline = false;
          opt.on_change = [weak, on_change = std::move(on_change)] ()
          {
            auto self = weak.lock();
            if (self)
              self->update_ui(weak);
            on_change();
            self->filter_ui_->TakeFocus();
          };
          self->filter_ui_ = ftxui::Input(&self->filter_, std::move(opt));
        }

        self->update_ui(self);
      }

      
      bool OnEvent(ftxui::Event evt) override final
      {
        if (overlay_)
        {
          if (!overlay_->OnEvent(evt) && evt == event::close())
          {
            overlay_->Detach();
            overlay_.reset();
            update_ui(weak_from_this());
          }
        }
        else if (evt == event::close())
          return false;
        else
          ui_->OnEvent(std::move(evt));
        return true;
      }

      ftxui::Element OnRender() override final
      {
        if (!overlay_)
        {
          ftxui::Elements rows;
          rows.reserve(5);

          rows.push_back(buttons_->Render() | ftxui::hcenter);
          rows.push_back(ftxui::separator());
          rows.push_back(ftxui::hbox({ftxui::text(_("Filter: ")), filter_ui_->Render()}));
          rows.push_back(ftxui::separator());

          std::vector<std::vector<ftxui::Element>> grid;
          grid.reserve(entries_.size());
          for (const auto& e : entries_)
            grid.push_back({std::get<1>(e)->Render(), ftxui::separator(), std::get<0>(e), ftxui::separator(), std::get<2>(e)->Render(), std::get<3>(e)->Render()});
          rows.push_back(ftxui::gridbox(std::move(grid)));

          cached_ = ftxui::window(title_, ftxui::vbox(std::move(rows)));
          return cached_;
        }
        return ftxui::dbox(cached_, decorate::overlay(overlay_->Render()));
      }
    };
  }

  ftxui::Component book(std::shared_ptr<Monero::WalletManager> wm, std::shared_ptr<Monero::Wallet> wal, std::shared_ptr<dest_pair> out)
  {
    if (!wm || !wal || !out)
      throw std::invalid_argument{"views::book cannot be given nullptr"};
    auto self = std::make_shared<book_>(std::move(wm), std::move(wal), std::move(out));
    book_::set_ui(self);
    return self;
  }
}} // lwcli // view
