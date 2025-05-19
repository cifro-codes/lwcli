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

#include "decorate/overlay.h"
#include "table.h"

namespace lwcli { namespace component
{
  namespace
  { 
    class table_ final : public ftxui::ComponentBase
    {
      const table_generator generator_;
      const table_on_key key_;
      const std::vector<std::vector<std::string>> title_;
      std::vector<std::array<ftxui::Box, 2>> boxes_;
      ftxui::Box box_;
      std::ptrdiff_t rows_;
      std::ptrdiff_t selected_;
      std::ptrdiff_t highlighted_;
      const std::size_t columns_;

      static constexpr std::ptrdiff_t min_row() noexcept { return 0; };
      bool Focusable() const override final { return true; }

      static std::vector<std::vector<std::string>> get_title_bar(std::vector<std::string>&& title)
      {
        std::vector<std::vector<std::string>> out;
        out.reserve(2);

        const std::size_t count = title.size();
        out.push_back(std::move(title));
        out.push_back(std::vector<std::string>(count));

        return out;
      }

    public:
      explicit table_(std::vector<std::string>&& title, table_generator&& generator, table_on_key&& key)
        : ftxui::ComponentBase(),
          generator_(std::move(generator)),
          key_(std::move(key)),
          title_(get_title_bar(std::move(title))),
          boxes_(),
          box_(),
          rows_(0),
          selected_(-1),
          highlighted_(-1),
          columns_(title_.empty() ? 0 : title_.at(0).size())
      {
        set_size(generator_().size());
      }

      void set_size(const std::size_t rows)
      {
        if (std::numeric_limits<std::ptrdiff_t>::max() < rows)
          throw std::runtime_error{"lwcli::component::table exceeds max size"};
        rows_ = rows;
      }

      bool can_increment() const noexcept
      { return selected_ + min_row() < rows_; }

      bool can_decrement() const noexcept
      { return min_row() <= selected_; }

      bool OnEvent(ftxui::Event event) override final
      {
        const auto original = selected_;
        if (event.is_mouse() && box_.Contain(event.mouse().x, event.mouse().y))
        {
          if (event.mouse().button == ftxui::Mouse::WheelDown && can_increment())
          {
            ++selected_;
            if (!Focused())
              TakeFocus();
          }
          else if (event.mouse().button == ftxui::Mouse::WheelUp && can_decrement())
          {
            --selected_;// = std::max(min_row(), selected_ - 1);
            if (!Focused())
              TakeFocus();
          }
          else
          {
            highlighted_ = -1;
            const auto x = event.mouse().x;
            const auto y = event.mouse().y;

            const auto match = std::lower_bound(boxes_.begin(), boxes_.end(), y, [] (const auto& lhs, const auto rhs) {
              return std::get<0>(lhs).y_min < rhs;
            });

            if (match != boxes_.end() && ftxui::Box::Union(std::get<0>(*match), std::get<1>(*match)).Contain(x, y)) 
            {
              const std::size_t i = match - boxes_.begin();
              highlighted_ = i;
              if (key_(event, i))
              {
                selected_ = i;
                TakeFocus();
              }
              return true; 
            }
          }
        }
        else if (event == ftxui::Event::ArrowDown && can_increment())
          ++selected_;
        else if (event == ftxui::Event::ArrowUp && can_decrement())
          --selected_;
        else if (event == ftxui::Event::PageDown)
          selected_ = std::min(rows_ + min_row(), selected_ + 15);
        else if (event == ftxui::Event::PageUp)
          selected_ = std::max(min_row(), selected_ - 15);
        else if (min_row() <= selected_ && selected_ < rows_)
          return key_(std::move(event), std::size_t(selected_)); 

        if (original != selected_)
          highlighted_ = -1;
        return min_row() <= selected_ && selected_ - min_row() < rows_;
      }

      ftxui::Element OnRender() override final
      {
        auto rows = generator_();
        set_size(rows.size());

        rows.reserve(rows.size() + title_.size());
        rows.insert(rows.begin(), title_.begin(), title_.end());

        ftxui::Table table{std::move(rows)};

        {
          auto title = table.SelectRow(0);
          title.Decorate(ftxui::bold);
          title.SeparatorVertical(ftxui::LIGHT);
        }

        if (columns_)
        {
          const std::size_t offset = title_.size();
          boxes_.resize(rows_);
          for (std::size_t i = offset; i < rows_ + offset; ++i)
          {
            auto& boxes = boxes_.at(i - offset);

            auto cell1 = table.SelectCell(0, i);
            cell1.DecorateCells(ftxui::reflect(std::get<0>(boxes)));

            auto cell2 = table.SelectCell(columns_ - 1, i);
            cell2.DecorateCells(ftxui::reflect(std::get<1>(boxes)));
          }
        }
          
        if (Focused())
        {
          if (selected_ < min_row())
            selected_ = min_row();
          else if (rows_ <= selected_ - min_row())
            selected_ = rows_ - 1 + min_row();

          if (min_row() <= selected_ && selected_ < rows_)
          {
            auto row = table.SelectRow(std::size_t(selected_) + title_.size());
            row.Decorate(ftxui::inverted);
            row.Decorate(ftxui::focus);
          }
        }
        else
          selected_ = -1;

        if (selected_ != highlighted_ && min_row() <= highlighted_ && highlighted_ < rows_)
        {
          auto row = table.SelectRow(std::size_t(highlighted_) + title_.size());
          row.Decorate(ftxui::inverted);
        }

        return table.Render() | ftxui::reflect(box_);
      }
    };
  } // anonymous

  ftxui::Component table(std::vector<std::string> title, table_generator generator, table_on_key key)
  {
    if (!generator || !key)
      throw std::invalid_argument{"lwcli::components::table was given nullptr"};
    return std::make_shared<table_>(std::move(title), std::move(generator), std::move(key));
  }
}} // lwcli // view
