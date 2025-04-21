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
      const table_enter enter_;
      const std::vector<std::vector<std::string>> title_;
      std::ptrdiff_t rows_;
      std::ptrdiff_t selected_;

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
      explicit table_(std::vector<std::string>&& title, table_generator&& generator, table_enter&& enter)
        : ftxui::ComponentBase(),
          generator_(std::move(generator)),
          enter_(std::move(enter)),
          title_(get_title_bar(std::move(title))),
          rows_(0),
          selected_(-1)
      {
        set_size(generator_().size());
      }

      void set_size(const std::size_t rows)
      {
        if (std::numeric_limits<std::ptrdiff_t>::max() < rows)
          throw std::runtime_error{"lwcli::component::table exceeds max size"};
        rows_ = rows;
      }

      bool OnEvent(ftxui::Event event) override final
      {
        if (event == ftxui::Event::ArrowDown && selected_ + min_row() < rows_)
          ++selected_;
        else if (event == ftxui::Event::ArrowUp && min_row() <= selected_)
          --selected_;
        else if (event == ftxui::Event::PageDown)
          selected_ = std::min(rows_ + min_row(), selected_ + 15);
        else if (event == ftxui::Event::PageUp)
          selected_ = std::max(min_row(), selected_ - 15);
        else if (event == ftxui::Event::Return && min_row() <= selected_ && selected_ < rows_)
          return enter_(std::size_t(selected_));

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
          //title.Border(ftxui::LIGHT);
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
        return table.Render();
      }
    };
  } // anonymous

  ftxui::Component table(std::vector<std::string> title, table_generator generator, table_enter enter)
  {
    if (!generator || !enter)
      throw std::invalid_argument{"lwcli::components::table was given nullptr"};
    return std::make_shared<table_>(std::move(title), std::move(generator), std::move(enter));
  }
}} // lwcli // view
