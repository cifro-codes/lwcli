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

#include <chrono>
#include <string_view>

#include "lws_frontend.h"

namespace lwcli { namespace config
{
  inline Monero::NetworkType network = Monero::MAINNET;

  //! Max length for small-string optimization expectations
  constexpr const unsigned sso_max = 15;

  //! \return True if `str` passes sso requirements.
  constexpr bool verify_sso(const std::string_view str) noexcept
  { return str.size() <= sso_max; }

  //! \return True if all `strs` pass sso requirements.
  template<typename... T>
  constexpr bool verify_sso(const T... strs) noexcept
  { return ( ... && verify_sso(strs)); }

  constexpr const std::string_view default_language{"English"};
  constexpr const std::string_view default_account_name{"Untitled Account"};

  //! Timeout interval for inactivity with open wallet
  constexpr const std::chrono::minutes wallet_timeout{2};
  namespace server
  { 
    constexpr const std::string_view default_url{"http://127.0.0.1:8080"};
    constexpr const std::chrono::seconds default_refresh_interval{30};

    constexpr const std::string_view proxy{"lwcli.ser.proxy"};
    constexpr const std::string_view refresh_interval{"lwcli.ser.refr"};
    constexpr const std::string_view ssl{"lwcli.ser.ssl"};
    constexpr const std::string_view url{"lwcli.ser.url"};

    static_assert(verify_sso(proxy, refresh_interval, ssl, url));
  }

  constexpr const std::uint32_t default_major_lookahead = 50;
  constexpr const std::uint32_t default_minor_lookahead = 200;

  constexpr const std::string_view major_lookahead{"lwcli.wal.maj_l"};
  constexpr const std::string_view minor_lookahead{"lwcli.wal.min_l"};

  static_assert(verify_sso(major_lookahead, minor_lookahead));

}} // lwcli // config
