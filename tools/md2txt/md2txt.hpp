// Markdown (the subset README.md uses) -> plain text with CRLF line ends.
#pragma once

#include <string>

namespace md2txt {

constexpr size_t k_width = 100; // the last table column wraps so rows stay within this width

std::string convert(const std::string &md);

} // namespace md2txt
