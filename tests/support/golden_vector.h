#pragma once
// Reader/writer for the golden-vector format described in tests/README.md
// (the authoritative copy) and Doc/linux_roadmap/16-testing-and-validation.md.

#include <string>
#include <vector>

#include "fake_hid.h"

namespace alienfx_test {

// Reads tests/golden/<target>/<case>.txt. Comment (`#`) and blank lines are
// skipped. A line starting with `>` is `> <token> <hex bytes>` or
// `> sleep <ms>`; any other non-comment line is bare hex, implying `out` (the
// M0-era format, still accepted). Throws std::runtime_error on a malformed line
// or a missing file -- a golden file is a committed fixture, not user input, so a
// hard failure beats silently treating a typo as "zero expected packets".
std::vector<TransportEvent> ReadGoldenFile(const std::string& path);

// Writes a golden-vector file: `headerComments` become `#`-prefixed lines (the
// first must carry `origin=`, see tests/README.md), followed by one `> <token>
// <hex bytes>` / `> sleep <ms>` line per event. Always emits the tagged form,
// even for `out` events, so a diff always shows exactly what transport call
// produced each line.
void WriteGoldenFile(const std::string& path,
                      const std::vector<std::string>& headerComments,
                      const std::vector<TransportEvent>& events);

} // namespace alienfx_test
