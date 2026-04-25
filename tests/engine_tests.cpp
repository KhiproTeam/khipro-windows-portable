// SPDX-License-Identifier: MPL-2.0
// Copyright (c) 2026 qomarhsn

#include <fstream>
#include <iostream>
#include <string>

#include "../src/mim_engine.h"

namespace {

std::string Trim(std::string s) {
  size_t start = 0;
  while (start < s.size() && (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n')) {
    ++start;
  }
  size_t end = s.size();
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' || s[end - 1] == '\r' || s[end - 1] == '\n')) {
    --end;
  }
  return s.substr(start, end - start);
}

}  // namespace

int main() {
  std::ifstream in("data/bn-khipro.mim", std::ios::binary);
  if (!in) {
    std::cerr << "missing bn-khipro.mim\n";
    return 1;
  }
  std::string spec((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

  khipro::KhiproEngine engine(spec);

  bool ok = true;
  size_t passed = 0;
  size_t failed = 0;

  std::ifstream csv("tests/khipro-testcases.csv");
  if (!csv) {
    std::cerr << "missing tests/khipro-testcases.csv\n";
    return 1;
  }

  std::string line;
  bool header_skipped = false;
  while (std::getline(csv, line)) {
    if (!header_skipped) {
      header_skipped = true;
      continue;
    }

    line = Trim(line);
    if (line.empty()) {
      continue;
    }

    const size_t comma = line.find(',');
    if (comma == std::string::npos) {
      continue;
    }

    const std::string input = Trim(line.substr(0, comma));
    const std::string expected = Trim(line.substr(comma + 1));
    if (input.empty()) {
      continue;
    }

    const std::string got = engine.Convert(input);
    if (got != expected) {
      ok = false;
      ++failed;
      std::cerr << "CSV FAIL in='" << input << "' expected='" << expected << "' got='" << got << "'\n";
    } else {
      ++passed;
    }
  }

  std::cout << "tests passed=" << passed << " failed=" << failed << "\n";
  if (!ok) {
    return 1;
  }

  std::cout << "engine tests passed\n";
  return 0;
}
