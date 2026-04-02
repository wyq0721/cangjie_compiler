# Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
# This source file is part of the Cangjie project, licensed under Apache-2.0
# with Runtime Library Exception.
#
# See https://cangjie-lang.cn/pages/LICENSE for license information.

add_executable(ManglerContextTest ManglerContextTest.cpp)
target_link_libraries(
    ManglerContextTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME ManglerContextTest COMMAND ManglerContextTest)

add_executable(FilteredPrefixTest FilteredPrefixTest.cpp)
target_link_libraries(
    FilteredPrefixTest
    cangjie-lsp
    ${CMAKE_DL_LIBS}
    GTest::gtest
    GTest::gtest_main)
add_test(NAME FilteredPrefixTest COMMAND FilteredPrefixTest)
