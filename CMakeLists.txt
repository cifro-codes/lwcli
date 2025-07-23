# Copyright (c) 2025, Cifro Codes LLC
#
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification, are
# permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this list of
#    conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice, this list
#    of conditions and the following disclaimer in the documentation and/or other
#    materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors may be
#    used to endorse or promote products derived from this software without specific
#    prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
# THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
# PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
# STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
# THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cmake_minimum_required(VERSION 3.5.0)
project(lwcli)

enable_language(CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (NOT MONERO_SOURCE_DIR)
  message(FATAL_ERROR "The argument -DMONERO_SOURCE_DIR must specify a location of a monero source tree")
endif()

include(FetchContent)
FetchContent_Declare(lwsf SOURCE_DIR "${lwcli_SOURCE_DIR}/external/lwsf")

set(LWSF_MASTER_ENABLE "ON")
if (NOT lwsf_POPULATED)
  FetchContent_MakeAvailable(lwsf)
endif ()

FetchContent_Declare(monero SOURCE_DIR ${MONERO_SOURCE_DIR})
if (NOT monero_POPULATED)
  FetchContent_MakeAvailable(monero)
endif () 
find_package(monero::libraries)

FetchContent_Declare(ftxui SOURCE_DIR "${lwcli_SOURCE_DIR}/external/ftxui")
if (NOT ftxui_POPULATED)
  FetchContent_MakeAvailable(ftxui)
endif ()

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(src)

