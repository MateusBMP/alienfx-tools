// Sanity tests for the vendored kiss_fft build (see
// alienfx-gui/kiss_fft/CMakeLists.txt). No hardware, no golden files -- every
// assertion is checked against an analytically-known result, verified against this
// exact build with a throwaway probe program before being written down here.
//
// kiss_fftr's inverse is unnormalized: a forward+inverse round trip reproduces the
// input scaled by N, not the input itself -- divide by N before comparing.

#include "kiss_fft.h"
#include "tools/kiss_fftr.h"

#include <gtest/gtest.h>

#include <cmath>
#include <memory>
#include <vector>

namespace {

constexpr int kN = 64;
constexpr float kTolerance = 1e-4f;

// RAII wrapper: kiss_fftr_alloc's buffer is freed with kiss_fft_free (== free()).
struct KissFftrCfg {
    void* cfg;
    explicit KissFftrCfg(bool inverse)
        : cfg(kiss_fftr_alloc(kN, inverse ? 1 : 0, nullptr, nullptr)) {}
    ~KissFftrCfg() { kiss_fft_free(cfg); }
};

std::vector<kiss_fft_cpx> Forward(const std::vector<float>& in) {
    EXPECT_EQ(in.size(), static_cast<size_t>(kN));
    KissFftrCfg cfg(/*inverse=*/false);
    std::vector<kiss_fft_cpx> out(kN / 2 + 1);
    kiss_fftr(cfg.cfg, in.data(), out.data());
    return out;
}

float Magnitude(const kiss_fft_cpx& c) {
    return std::sqrt(c.r * c.r + c.i * c.i);
}

}  // namespace

TEST(KissFftr, UnitImpulseProducesFlatSpectrum) {
    std::vector<float> in(kN, 0.0f);
    in[0] = 1.0f;

    const auto out = Forward(in);
    for (const auto& bin : out) {
        EXPECT_NEAR(bin.r, 1.0f, kTolerance);
        EXPECT_NEAR(bin.i, 0.0f, kTolerance);
    }
}

TEST(KissFftr, DcInputProducesEnergyOnlyInBinZero) {
    std::vector<float> in(kN, 1.0f);

    const auto out = Forward(in);
    EXPECT_NEAR(out[0].r, static_cast<float>(kN), kTolerance);
    EXPECT_NEAR(out[0].i, 0.0f, kTolerance);
    for (size_t k = 1; k < out.size(); ++k) {
        EXPECT_NEAR(Magnitude(out[k]), 0.0f, kTolerance);
    }
}

TEST(KissFftr, SineWavePeaksAtItsOwnBin) {
    constexpr int kBin = 5;
    std::vector<float> in(kN);
    for (int n = 0; n < kN; ++n) {
        in[n] = std::sin(2.0f * static_cast<float>(M_PI) * kBin * n / kN);
    }

    const auto out = Forward(in);
    // A real sine of unit amplitude at bin k puts magnitude N/2 at bin k and
    // ~0 everywhere else (verified against this build: 32 == 64/2).
    EXPECT_NEAR(Magnitude(out[kBin]), kN / 2.0f, kTolerance);
    for (size_t k = 0; k < out.size(); ++k) {
        if (static_cast<int>(k) == kBin) continue;
        EXPECT_LT(Magnitude(out[k]), 1e-3f) << "unexpected energy at bin " << k;
    }
}

TEST(KissFftr, ForwardThenInverseRecoversInputAfterScaling) {
    std::vector<float> in(kN);
    for (int n = 0; n < kN; ++n) {
        in[n] = std::sin(2.0f * static_cast<float>(M_PI) * 3.0f * n / kN) * 0.5f + 0.1f;
    }

    const auto freq = Forward(in);

    KissFftrCfg inverse_cfg(/*inverse=*/true);
    std::vector<float> back(kN);
    kiss_fftri(inverse_cfg.cfg, freq.data(), back.data());

    for (int n = 0; n < kN; ++n) {
        EXPECT_NEAR(back[n] / kN, in[n], kTolerance) << "mismatch at n=" << n;
    }
}
