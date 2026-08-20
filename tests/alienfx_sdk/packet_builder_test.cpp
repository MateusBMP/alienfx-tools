// Replays tests/support/packet_matrix.h's shared version x operation matrix and
// asserts the fake transport's recorded call log matches the committed golden
// vector for each case -- the `source-derived` tier of
// Doc/linux_roadmap/16-testing-and-validation.md. This is a regression check ("the
// port didn't change the bytes"), not a correctness proof -- see
// tests/alienfx_sdk/protocol_invariants_test.cpp for the `hand-derived` tier that
// covers the four spots this doc calls fragile independently of the builder's
// current output.

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "AlienFX_SDK.h"
#include "fake_hid.h"
#include "golden_vector.h"
#include "packet_matrix.h"

#ifndef ALIENFX_GOLDEN_DIR
#error "ALIENFX_GOLDEN_DIR must be defined by tests/CMakeLists.txt"
#endif

using ::testing::ElementsAreArray;

namespace {

class PacketBuilderTest : public ::testing::TestWithParam<alienfx_test::PacketCase> {};

TEST_P(PacketBuilderTest, MatchesGoldenVector) {
	const auto& c = GetParam();

	AlienFX_SDK::Functions f;
	f.version = c.apiVersion;
#ifdef ALIENFX_TESTING
	f.TestSetDeviceState(reinterpret_cast<HANDLE>(0x1), c.hidReportLength, c.vid, c.pid);
#endif
	alienfx_test::GetFakeTransport().Reset();
	c.run(f);

	const std::string path = std::string(ALIENFX_GOLDEN_DIR) + "/" + c.name + ".txt";
	const std::vector<alienfx_test::TransportEvent> expected = alienfx_test::ReadGoldenFile(path);
	EXPECT_THAT(alienfx_test::GetFakeTransport().Log(), ElementsAreArray(expected));
}

std::string NameFromParam(const ::testing::TestParamInfo<alienfx_test::PacketCase>& info) {
	return info.param.name;
}

INSTANTIATE_TEST_SUITE_P(AllCases, PacketBuilderTest,
                          ::testing::ValuesIn(alienfx_test::AllPacketCases()),
                          NameFromParam);

} // namespace
