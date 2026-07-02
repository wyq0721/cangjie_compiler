// Copyright (c) Huawei Technologies Co., Ltd. 2025. All rights reserved.
// This source file is part of the Cangjie project, licensed under Apache-2.0
// with Runtime Library Exception.
//
// See https://cangjie-lang.cn/pages/LICENSE for license information.

/**
 * @file
 *
 * This file declares some utility functions.
 */

#ifndef CANGJIE_DRIVER_UTILS_H
#define CANGJIE_DRIVER_UTILS_H

#include <optional>
#include <string>
#include <vector>

namespace Cangjie {
/**
 * @brief Get the input string quoted with single quotes.
 * Note: Single quotes in the input are transform to '\'' instead of \'.
 *
 * @param str The input string.
 * @return std::string The single quoted string.
 */
std::string GetSingleQuoted(const std::string& str);

/**
 * @brief Get the input string quoted for passing as a command line argument.
 * - In the case of Linux, the argument is quoted with single quotes. Nested single quotes are
 *   transformed to '\''.
 * - In the case of Windows, the argument is quoted with double quotes. Nested double quotes and
 *   backslashes are escaped by \.
 *
 * @param arg The input string.
 * @return std::string The quoted argument.
 */
std::string GetCommandLineArgumentQuoted(const std::string& arg);

/**
 * @brief Prepend to paths.
 *
 * @param prefix The path prefix to be added.
 * @param paths The path vector.
 * @param quoted Determine whether the path string add single quotes.
 * @return std::vector<std::string> The vector of paths with a prefix added.
 */
std::vector<std::string> PrependToPaths(
    const std::string& prefix, const std::vector<std::string>& paths, bool quoted = false);

/**
 * @brief Get darwin SDK version.
 *
 * @param sdkPath The sdk path.
 * @return std::optional<std::string> The sdk version info.
 */
std::optional<std::string> GetDarwinSDKVersion(const std::string& sdkPath);

/**
 * @brief Build a diagnostic message for a missing OpenHarmony C runtime object.
 *
 * When cross-compiling for OpenHarmony, the C runtime start object `crti.o` is resolved from the
 * C runtime library search paths (populated from `--sysroot` / `-B` / `--toolchain`). When the
 * sysroot is missing or misconfigured, `crti.o` cannot be resolved and the link later fails with a
 * cryptic `ld.lld: error: cannot open crti.o`. This helper turns that unresolved state into an
 * actionable diagnostic. It is pure so it can be unit tested independently of the linker invocation.
 *
 * @param resolvedCrti The result of resolving `crti.o` in the C runtime library paths.
 * @param crtRuntimePaths The C runtime library search paths that were consulted (listed in the message).
 * @return std::optional<std::string> The warning message when `resolvedCrti` is empty, otherwise nullopt.
 */
std::optional<std::string> DiagnoseMissingCRuntime(
    const std::optional<std::string>& resolvedCrti, const std::vector<std::string>& crtRuntimePaths);
} // namespace Cangjie

#endif // CANGJIE_DRIVER_UTILS_H
