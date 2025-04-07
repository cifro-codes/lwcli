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

#include <cstdio>
#include <cstring>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <lws_frontend.h>
#include <memory>
#include <stdexcept>
#include <string>

#include "events.h"
#include "lwcli_config.h"
#include "views/manager.h"

namespace
{
  enum class rpc { lws = 0, monerod };
  struct program
  {
    std::string file;
    rpc backend = rpc::lws;
    // network is in static memory
    bool failed;
  };

  typedef const char**(*argument_handler)(program&, const char*[]);
  
  struct argument
  {
    const argument_handler handler;
    char const* const full;
    char const * const description;
    const char truncated;
  };

  const char** basic_handler(program& prog, std::string& out, const char* name, const char* argv[])
  {
    if (!argv || !argv[0])
    {
      fprintf(stderr, "Missing argument for --%s\n", name);
      return nullptr;
    }
    if (!out.empty())
    {
      prog.failed = true;
      fprintf(stderr, "Argument --%s listed twice\n", name);
      return nullptr;
    }
    out = argv[0];
    return ++argv;
  }

  const char** handle_backend(program& prog, const char* argv[])
  {
    if (!argv || !argv[0])
    {
      fprintf(stderr, "Missing argument for --backend\n");
      return nullptr;
    }

    if (std::strcmp("lws", argv[0]) == 0)
      prog.backend = rpc::lws;
    else if (std::strcmp("monerod", argv[0]) == 0)
      prog.backend = rpc::monerod;
    else
    {
      prog.failed = true;
      fprintf(stderr, "--backend value is not valid\n");
      return nullptr;
    }

    return ++argv;
  }
  const char** handle_file(program& prog, const char* argv[])
  {
    return basic_handler(prog, prog.file, "file", argv);
  }
  const char** handle_network(program& prog, const char* argv[])
  {
    if (!argv || !argv[0])
    {
      fprintf(stderr, "Missing argument for --network\n");
      return nullptr;
    }

    if (std::strcmp("main", argv[0]) == 0)
      lwcli::config::network = Monero::MAINNET;
    else if (std::strcmp("stage", argv[0]) == 0)
      lwcli::config::network = Monero::STAGENET;
    else if (std::strcmp("test", argv[0]) == 0)
      lwcli::config::network = Monero::TESTNET;
    else
    {
      prog.failed = true;
      fprintf(stderr, "--network value is not valid\n");
      return nullptr;
    }

    return ++argv;
  }

  constexpr const argument process_args[] =
  {
    {nullptr, "help", "\t\t\tList help", 'h'},
    {handle_backend, "backend", "\tlws | monerod\t\tlws = default , selects rpc backend", 'b'},
    {handle_file, "file", "\t[file path]\t\tDefaults to home directory. Auto-fills TUI value on launch", 'f'},
    {handle_network, "network", "\tmain | stage | test\tSelects wallet network type. main is default.", 'n'},
  };

  template<typename F>
  const argument* find_argument(F f)
  {
    return std::find_if(std::begin(process_args), std::end(process_args), f);
  }

  const char** process_argument(program& prog, const char* argv[])
  {
    if (!argv || !argv[0])
      return nullptr;

    const argument* current = std::end(process_args);
    if (std::strncmp("--", argv[0], 2) == 0)
      current = find_argument([argv] (const argument& arg) { return std::strcmp(arg.full, argv[0] + 2) == 0; });
    else if (std::strncmp("-", argv[0], 1) == 0)
      current = find_argument([argv] (const argument& arg) { return std::strlen(argv[0]) == 2 && argv[0][1] == arg.truncated; });

    if (current == std::end(process_args) || !current->handler)
    {
      prog.failed = true;
      if (current == std::end(process_args))
        fprintf(stderr, "No such argument %s\n", argv[0]);
      for (const argument& arg : process_args)
        fprintf(stderr, "\t--%s, -%c\t%s\n", arg.full, arg.truncated, arg.description);
      return nullptr;
    }

    ++argv;
    return current->handler(prog, argv);
  }
}

int main(int, const char* argv[])
{
  auto screen = ftxui::ScreenInteractive::Fullscreen();
  try
  {
    if (!argv)
    {
      fprintf(stderr, "No process name\n");
      return -1;
    }

    ++argv;
    program prog{};
    while (argv = process_argument(prog, argv));
    if (prog.failed)
      return -1;

    std::shared_ptr<Monero::WalletManager> wm;
    switch (prog.backend)
    {
      default:
      case rpc::lws:
        wm.reset(lwsf::WalletManagerFactory::getWalletManager());
        break;
      case rpc::monerod:
        wm.reset(Monero::WalletManagerFactory::getWalletManager());
        break;
    }

    auto window = ftxui::CatchEvent(lwcli::view::manager(std::move(wm), std::move(prog.file)), [&] (ftxui::Event event)
    {
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
    fprintf(stderr, "Fatal Error: %s\n", e.what());
    return EXIT_FAILURE;
  }
 
  screen.Clear();
  return EXIT_SUCCESS;  
}
