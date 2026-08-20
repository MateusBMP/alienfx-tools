#include "golden_vector.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace alienfx_test {
namespace {

std::vector<uint8_t> ParseHexBytes(std::istringstream* line) {
	std::vector<uint8_t> bytes;
	std::string tok;
	while (*line >> tok)
		bytes.push_back(static_cast<uint8_t>(std::stoul(tok, nullptr, 16)));
	return bytes;
}

} // namespace

std::vector<TransportEvent> ReadGoldenFile(const std::string& path) {
	std::ifstream in(path);
	if (!in)
		throw std::runtime_error("golden vector file not found: " + path);

	std::vector<TransportEvent> events;
	std::string line;
	while (std::getline(in, line)) {
		if (line.empty() || line[0] == '#')
			continue;

		if (line[0] == '>') {
			std::istringstream iss(line.substr(1));
			std::string token;
			iss >> token;
			if (token.empty())
				throw std::runtime_error("golden vector line missing token: " + path + ": " + line);

			TransportKind kind;
			if (!FromToken(token, &kind))
				throw std::runtime_error("golden vector line has unknown token '" + token + "': " + path);

			if (kind == TransportKind::Sleep) {
				unsigned ms = 0;
				iss >> ms;
				events.push_back({kind, {}, ms});
			} else {
				events.push_back({kind, ParseHexBytes(&iss), 0});
			}
			continue;
		}

		// Bare hex line: the M0-era format, implicitly a HidD_SetOutputReport.
		std::istringstream iss(line);
		events.push_back({TransportKind::Out, ParseHexBytes(&iss), 0});
	}
	return events;
}

void WriteGoldenFile(const std::string& path,
                      const std::vector<std::string>& headerComments,
                      const std::vector<TransportEvent>& events) {
	std::ofstream out(path, std::ios::trunc);
	if (!out)
		throw std::runtime_error("cannot write golden vector file: " + path);

	for (const auto& comment : headerComments)
		out << "# " << comment << "\n";

	for (const auto& event : events) {
		out << "> " << ToToken(event.kind);
		if (event.kind == TransportKind::Sleep) {
			out << " " << event.sleepMs << "\n";
			continue;
		}
		for (uint8_t b : event.bytes) {
			char buf[4];
			std::snprintf(buf, sizeof(buf), " %02x", b);
			out << buf;
		}
		out << "\n";
	}
}

} // namespace alienfx_test
