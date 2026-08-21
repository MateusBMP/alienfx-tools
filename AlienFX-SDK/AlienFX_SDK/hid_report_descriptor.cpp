// HID 1.11 section 6.2.2 short/long item parser, scoped to exactly what
// AlienFXProbeDevice's detection switch needs: Output and Feature report byte
// lengths, in Windows' HidP_GetCaps convention. See hid_report_descriptor.h for the
// contract this implements and why.
//
// `data` is untrusted (kernel-supplied, but treated as untrusted regardless of
// source): every item is bounds-checked before its payload is read, Report Size x
// Report Count is bounded before use, and the final lengths are rejected -- not
// clamped -- if they would not fit AlienFX_SDK.h's MAX_BUFFERSIZE. Any structural
// problem (truncated item, unbalanced Collection/Push nesting, a report length that
// doesn't fit) is a hard `false`, never a partial or best-effort result: a caller
// silently treating a malformed descriptor as "no report" (length 0) would be
// indistinguishable from a genuinely absent report, which the V5 detection path
// depends on being able to trust.

#include "hid_report_descriptor.h"

#include <array>

namespace alienfx_hid {

namespace {

constexpr int kMaxNestingDepth = 32;
constexpr uint64_t kMaxTotalBits = 8ull * 4096; // sanity cap, well above any real
                                                 // report; guards Report Size x
                                                 // Report Count against overflow-by-
                                                 // absurdity rather than relying on
                                                 // wraparound not happening to occur.
constexpr int kMaxBufferSize = 193; // AlienFX_SDK.h's MAX_BUFFERSIZE -- duplicated as
                                     // a literal rather than included, to keep this
                                     // parser dependency-free (see the .h's file
                                     // comment); AlienFX_SDK.h itself is Windows-type-
                                     // laden and not something this TU should include.
constexpr unsigned kMaxReportIds = 32; // generous headroom over any real device's
                                        // distinct Report IDs.

// HID 1.11 6.2.2.2: the 2-bit bSize field is NOT the byte count directly -- 0b11
// means 4 bytes, not 3.
unsigned ShortItemDataBytes(uint8_t bSize) {
	static constexpr unsigned kSizes[4] = {0, 1, 2, 4};
	return kSizes[bSize & 0x3];
}

uint32_t ReadUnsigned(const uint8_t* data, unsigned byteCount) {
	uint32_t v = 0;
	for (unsigned i = 0; i < byteCount; ++i)
		v |= static_cast<uint32_t>(data[i]) << (8 * i);
	return v;
}

// Only the three global items that feed a report's bit length. Push/Pop (below) saves
// and restores exactly this -- a HID-spec-complete Push would also cover Usage Page,
// Logical/Physical Min/Max, Unit*, etc., but nothing else here is ever read, so
// tracking more would be dead state.
struct GlobalState {
	uint32_t reportSize = 0;
	uint32_t reportCount = 0;
	uint32_t reportId = 0; // 0 doubles as "no Report ID item seen yet" (an unnumbered
	                       // report) -- HID reserves ID 0, so this is not ambiguous
	                       // with a real device-declared ID.
};

struct ReportIdBucket {
	uint32_t reportId = 0;
	uint64_t outputBits = 0;
	uint64_t featureBits = 0;
};

} // namespace

bool ParseHidReportDescriptor(const uint8_t* data, size_t size, HidCaps* out) {
	// `data == nullptr` is only invalid when `size != 0`: an empty
	// std::vector<uint8_t>::data() is permitted to return nullptr (libstdc++ does),
	// and an empty descriptor is a valid, documented input (see the .h) -- not one
	// this function should reject as malformed.
	if (!out || (!data && size != 0))
		return false;
	*out = HidCaps{};

	GlobalState state;
	std::array<GlobalState, kMaxNestingDepth> pushStack{};
	int stackDepth = 0;
	int collectionDepth = 0;

	// Per-report-ID accumulators, aggregated across the WHOLE descriptor (every top-
	// level collection in it) -- not per top-level collection the way Windows'
	// HidP_GetCaps works. See hid_report_descriptor.h's file comment / doc 04's note
	// next to Finding 2 for why that's a deliberate, accepted gap here.
	std::array<ReportIdBucket, kMaxReportIds> buckets{};
	unsigned bucketCount = 0;

	auto bucketFor = [&](uint32_t reportId) -> ReportIdBucket* {
		for (unsigned i = 0; i < bucketCount; ++i)
			if (buckets[i].reportId == reportId)
				return &buckets[i];
		if (bucketCount >= buckets.size())
			return nullptr; // more distinct Report IDs than any real device declares
		buckets[bucketCount] = ReportIdBucket{reportId, 0, 0};
		return &buckets[bucketCount++];
	};

	size_t i = 0;
	while (i < size) {
		uint8_t prefix = data[i];

		if (prefix == 0xFE) { // long item, HID 1.11 6.2.2.3
			if (i + 3 > size)
				return false; // truncated: needs prefix + bDataSize + bLongItemTag
			uint8_t dataSize = data[i + 1];
			if (i + 3 + dataSize > size)
				return false; // declared length runs past the buffer
			i += 3 + dataSize;
			continue;
		}

		uint8_t bSize = prefix & 0x3;
		uint8_t bType = (prefix >> 2) & 0x3; // 0 Main, 1 Global, 2 Local, 3 Reserved
		uint8_t bTag = (prefix >> 4) & 0xF;
		unsigned dataBytes = ShortItemDataBytes(bSize);

		if (i + 1 + dataBytes > size)
			return false; // truncated short item

		const uint8_t* itemData = data + i + 1;

		if (bType == 1) { // Global
			switch (bTag) {
			case 0x7: state.reportSize = ReadUnsigned(itemData, dataBytes); break;
			case 0x8: state.reportId = ReadUnsigned(itemData, dataBytes); break;
			case 0x9: state.reportCount = ReadUnsigned(itemData, dataBytes); break;
			case 0xA: // Push
				if (stackDepth >= kMaxNestingDepth)
					return false;
				pushStack[stackDepth++] = state;
				break;
			case 0xB: // Pop
				if (stackDepth <= 0)
					return false; // Pop with no matching Push: malformed
				state = pushStack[--stackDepth];
				break;
			default:
				break; // Usage Page / Logical|Physical Min|Max / Unit* -- unneeded
			}
		} else if (bType == 0) { // Main
			switch (bTag) {
			case 0xA: // Collection
				if (++collectionDepth > kMaxNestingDepth)
					return false;
				break;
			case 0xC: // End Collection
				if (--collectionDepth < 0)
					return false; // End Collection with nothing open: malformed
				break;
			case 0x9:   // Output
			case 0xB: { // Feature
				uint64_t bits = static_cast<uint64_t>(state.reportSize) *
				                 static_cast<uint64_t>(state.reportCount);
				if (bits > kMaxTotalBits)
					return false; // Report Size x Report Count guard
				ReportIdBucket* b = bucketFor(state.reportId);
				if (!b)
					return false;
				uint64_t& total = (bTag == 0x9) ? b->outputBits : b->featureBits;
				total += bits;
				if (total > kMaxTotalBits)
					return false;
				break;
			}
			default:
				break; // Input and any other Main tag: doesn't feed Output/Feature caps
			}
		}
		// bType == 2 (Local) and 3 (Reserved): consumed, no effect on report length.

		i += 1 + dataBytes;
	}

	if (collectionDepth != 0 || stackDepth != 0)
		return false; // unbalanced Collection or Push/Pop nesting: malformed

	uint64_t maxOutputBits = 0, maxFeatureBits = 0;
	for (unsigned k = 0; k < bucketCount; ++k) {
		if (buckets[k].outputBits > maxOutputBits) maxOutputBits = buckets[k].outputBits;
		if (buckets[k].featureBits > maxFeatureBits) maxFeatureBits = buckets[k].featureBits;
	}

	// The +1 (leading report-ID byte) is conditional on the report existing at all --
	// Windows reports byte length 0, not 1, for a report kind the collection doesn't
	// declare. See hid_report_descriptor.h's HidCaps comment: getting this backwards
	// (unconditional +1) makes the API_V5 `!length` detection condition permanently
	// false, not just blocked on Finding 2.
	auto toLength = [](uint64_t bits) -> int {
		return bits ? static_cast<int>((bits + 7) / 8) + 1 : 0;
	};

	int outputLength = toLength(maxOutputBits);
	int featureLength = toLength(maxFeatureBits);

	if (outputLength > kMaxBufferSize || featureLength > kMaxBufferSize)
		return false; // reject, don't clamp -- see the file comment above

	out->outputReportByteLength = outputLength;
	out->featureReportByteLength = featureLength;
	return true;
}

} // namespace alienfx_hid
