#include "transport_log.h"

#include <iomanip>
#include <sstream>
#include <utility>

namespace alienfx_test {

const char* ToToken(TransportKind kind) {
	switch (kind) {
	case TransportKind::Out:     return "out";
	case TransportKind::Feat:    return "feat";
	case TransportKind::Write:   return "write";
	case TransportKind::Read:    return "read";
	case TransportKind::GetFeat: return "getfeat";
	case TransportKind::GetIn:   return "getin";
	case TransportKind::Sleep:   return "sleep";
	}
	return "?";
}

bool FromToken(const std::string& token, TransportKind* outKind) {
	static const std::pair<const char*, TransportKind> kTokens[] = {
		{"out", TransportKind::Out},       {"feat", TransportKind::Feat},
		{"write", TransportKind::Write},   {"read", TransportKind::Read},
		{"getfeat", TransportKind::GetFeat}, {"getin", TransportKind::GetIn},
		{"sleep", TransportKind::Sleep},
	};
	for (const auto& entry : kTokens) {
		if (token == entry.first) {
			*outKind = entry.second;
			return true;
		}
	}
	return false;
}

void PrintTo(const TransportEvent& event, std::ostream* os) {
	*os << ToToken(event.kind);
	if (event.kind == TransportKind::Sleep) {
		*os << " " << event.sleepMs << "ms";
		return;
	}
	std::ostringstream hex;
	hex << std::hex << std::setfill('0');
	for (uint8_t b : event.bytes)
		hex << " " << std::setw(2) << static_cast<unsigned>(b);
	*os << hex.str();
}

} // namespace alienfx_test
