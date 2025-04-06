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

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <lws_frontend.h>
#include <memory>
#include <iostream>
#include <stdexcept>

#include "events.h"
#include "views/manager.h"

int main()
{
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  try
  {
    std::shared_ptr<Monero::WalletManager> wm{
      lwsf::WalletManagerFactory::getWalletManager()
    };

    auto window = ftxui::CatchEvent(lwcli::view::manager(std::move(wm)), [&] (ftxui::Event event) {
      if (event == ftxui::Event::CtrlC)
      {
        screen.ExitLoopClosure()();
        return true;
      }
      return false;
    });
    screen.Loop(window);
  }
  catch (const lwcli::event::close&)
  {}
  catch (const std::exception& e)
  {
    screen.Clear();
    std::cerr << "Fatal Error: " << e.what() << std::endl;
    return EXIT_FAILURE;
  }
 
  screen.Clear();
  return EXIT_SUCCESS;  
}
