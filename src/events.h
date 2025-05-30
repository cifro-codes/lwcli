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

#pragma once

#include <ftxui/component/event.hpp>
#include <stdexcept>

namespace lwcli { namespace event
{
  //! Thrown when a window should be closed
  struct close final : public std::exception
  {
    close () noexcept
      : std::exception()
    {}

    virtual ~close() noexcept override = default;
    virtual const char* what() const noexcept override final { return "close window"; }
  };

  extern const ftxui::Event lock_wallet;
  extern const ftxui::Event refresh_wallet;
  extern const ftxui::Event send_async;

  inline bool is_left_click(ftxui::Event& e) noexcept
  { return e.is_mouse() && e.mouse().button == ftxui::Mouse::Left && e.mouse().motion == ftxui::Mouse::Pressed; }

  inline bool is_right_click(ftxui::Event& e) noexcept
  { return e.is_mouse() && e.mouse().button == ftxui::Mouse::Right && e.mouse().motion == ftxui::Mouse::Pressed; }

}} // lwcli // event
