#include "packet_matrix.h"

#include <cstdint>
#include <initializer_list>

namespace alienfx_test {
namespace {

using AlienFX_SDK::Afx_action;
using AlienFX_SDK::Afx_colorcode;
using AlienFX_SDK::Afx_light;
using AlienFX_SDK::Afx_lightblock;
using AlienFX_SDK::Functions;

struct VersionInfo {
	int version;
	int length;
	const char* suffix;
};

// Lengths match AlienFX_SDK.h's Afx_Version enum comments (04-alienfx-sdk-hid.md's
// "API version enum and detection" table restates them).
constexpr VersionInfo kAllVersions[] = {
	{AlienFX_SDK::API_V2, 9,  "v2"},
	{AlienFX_SDK::API_V3, 12, "v3"},
	{AlienFX_SDK::API_V4, 34, "v4"},
	{AlienFX_SDK::API_V5, 64, "v5"},
	{AlienFX_SDK::API_V6, 65, "v6"},
	{AlienFX_SDK::API_V7, 65, "v7"},
	{AlienFX_SDK::API_V8, 65, "v8"},
};

// WaitForBusy's API_V4 branch checks `pid == 0x551` ("patch for newer v4") and
// returns immediately without polling -- reusing that existing escape hatch is
// simpler than teaching the fake transport to distinguish a WaitForBusy poll from
// a WaitForReady poll, which the real transport has no way to do either. See
// fake_hid.h's default-response comment for the WaitForReady side of this.
unsigned short PidFor(int version) {
	return version == AlienFX_SDK::API_V4 ? 0x551 : 0;
}

void AddCommonCases(std::vector<PacketCase>* out) {
	for (const auto& vi : kAllVersions) {
		const std::string prefix = vi.suffix;
		const int version = vi.version;
		const int length = vi.length;
		const unsigned short pid = PidFor(version);

		out->push_back({prefix + "_reset", version, length, 0, pid,
			[](Functions& f) { f.Reset(); }});

		out->push_back({prefix + "_reset_update", version, length, 0, pid,
			[](Functions& f) {
				f.Reset();
				f.UpdateColors();
			}});

		out->push_back({prefix + "_setcolor", version, length, 0, pid,
			[](Functions& f) {
				f.SetColor(0, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xff, 0, 0});
			}});

		out->push_back({prefix + "_setaction_pulse", version, length, 0, pid,
			[](Functions& f) {
				Afx_lightblock blk{0, {Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Pulse, 0, 10, 0, 0xff, 0}}};
				f.SetAction(&blk);
			}});

		out->push_back({prefix + "_setaction_morph", version, length, 0, pid,
			[](Functions& f) {
				Afx_lightblock blk{0, {
					Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Morph, 5, 10, 0xff, 0, 0},
					Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Morph, 5, 10, 0, 0, 0xff},
				}};
				f.SetAction(&blk);
			}});

		out->push_back({prefix + "_setmulticolor_2lights", version, length, 0, pid,
			[](Functions& f) {
				std::vector<uint8_t> lights{0, 1};
				f.SetMultiColor(&lights, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0, 0, 0xff});
			}});

		out->push_back({prefix + "_setbrightness", version, length, 0, pid,
			[](Functions& f) {
				std::vector<Afx_light> mappings(2);
				mappings[0].lightid = 0; mappings[0].flags = 0;
				mappings[1].lightid = 1; mappings[1].flags = 0;
				f.SetBrightness(200, 255, &mappings, true);
			}});
	}
}

void AddV4ChunkingCase(std::vector<PacketCase>* out) {
	// SetMultiColor's V4 arm caps at 26 light IDs per packet and auto-chunks with
	// an Update+Reset between batches -- Doc/linux_roadmap/04-alienfx-sdk-hid.md,
	// "Bulk same-color". 30 lights forces exactly one chunk boundary.
	out->push_back({"v4_setmulticolor_chunk30", AlienFX_SDK::API_V4, 34, 0, PidFor(AlienFX_SDK::API_V4),
		[](Functions& f) {
			std::vector<uint8_t> lights;
			for (int i = 0; i < 30; i++) lights.push_back(static_cast<uint8_t>(i));
			f.SetMultiColor(&lights, Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xff, 0xff, 0});
		}});
}

void AddGlobalEffectsCases(std::vector<PacketCase>* out) {
	// V5/V8 only -- Doc 04's "Global effects (V5/V8 only)"; IsHaveGlobal() returns
	// false for every other version and SetGlobalEffects is a same no-op for them.
	out->push_back({"v5_setglobaleffects", AlienFX_SDK::API_V5, 64, 0, PidFor(AlienFX_SDK::API_V5),
		[](Functions& f) {
			f.SetGlobalEffects(1, 0, 2, 5, Afx_colorcode(0, 0, 0xff), Afx_colorcode(0, 0xff, 0));
		}});
	out->push_back({"v8_setglobaleffects", AlienFX_SDK::API_V8, 65, 0, PidFor(AlienFX_SDK::API_V8),
		[](Functions& f) {
			f.SetGlobalEffects(1, 0, 2, 5, Afx_colorcode(0, 0, 0xff), Afx_colorcode(0, 0xff, 0));
		}});
}

// V2/V3's SetPowerAction and SaveLightsState (for any non-empty light list) both
// route through SavePowerBlock (AlienFX_SDK.cpp:161-188), which has a genuine
// pre-existing defect independent of this port: `group` is a single-element
// vector consumed and *cleared* by the very first `PrepareAndSend(COMMV1_saveGroup,
// &group)` call (:163; PrepareAndSend always clears `*mods` after use), then
// reused -- still empty -- by the unconditional call at :179. PrepareAndSend's V8
// heuristic (:118, `needV8Feature = mods->front().vval.size() == 1`) calls
// `.front()` on `*mods` whenever mods is non-null, with no emptiness check, for
// EVERY version, not just V8 -- so this is UB on an empty vector on every call to
// SavePowerBlock, on any version, regardless of the needSecondary/needInverse
// flags. It doesn't crash on MSVC release builds (silently reads garbage), but
// aborts under this toolchain's hardened libstdc++ (`vector::front(): assertion
// '!this->empty()' failed`) -- which is how M2a's characterization testing found
// it. Fixing it is a deliberate, reviewed change to shared Windows-compiled code,
// out of scope for M2a's byte-for-byte characterization; V2/V3 SetPowerAction and
// SaveLightsState are excluded from this matrix until it's fixed (tracked as a
// defect, not re-derived here).
void AddPowerActionCases(std::vector<PacketCase>* out) {
	// V4 only -- see the SavePowerBlock defect note above for why V2/V3 are
	// excluded. Every version other than V2/V3/V4 has no matching case in
	// SetPowerAction's switch and returns immediately (AlienFX_SDK.cpp:738-739).
	out->push_back({"v4_setpoweraction", AlienFX_SDK::API_V4, 34, 0, PidFor(AlienFX_SDK::API_V4),
		[](Functions& f) {
			Afx_lightblock blk{0, {
				Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xff, 0, 0},
				Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0, 0xff, 0},
			}};
			f.SetPowerAction(&blk);
		}});
}

void AddSaveLightsStateCases(std::vector<PacketCase>* out) {
	// V4 only -- see the SavePowerBlock defect note above for why V2/V3 are
	// excluded (SaveLightsState's V2/V3 branch, AlienFX_SDK.cpp:636-654, calls
	// SavePowerBlock for every non-Power block in the list). V4's branch
	// (:627-635) calls SetV4Action directly and never touches SavePowerBlock.
	out->push_back({"v4_savelightsstate", AlienFX_SDK::API_V4, 34, 0, PidFor(AlienFX_SDK::API_V4),
		[](Functions& f) {
			std::vector<Afx_lightblock> blocks{
				{0, {Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Color, 0, 0, 0xff, 0, 0}}},
				{1, {Afx_action{(BYTE)AlienFX_SDK::AlienFX_A_Power, 0, 0, 0, 0xff, 0}}},
			};
			f.SaveLightsState(&blocks);
		}});
}

} // namespace

const std::vector<PacketCase>& AllPacketCases() {
	static const std::vector<PacketCase> cases = [] {
		std::vector<PacketCase> v;
		AddCommonCases(&v);
		AddV4ChunkingCase(&v);
		AddGlobalEffectsCases(&v);
		AddPowerActionCases(&v);
		AddSaveLightsStateCases(&v);
		return v;
	}();
	return cases;
}

} // namespace alienfx_test
