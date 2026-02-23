// Fog Effect Bypass/Passthrough Test
//
// Verifies that with mix=1.0 and both smear and specmagfilter DISABLED,
// the fog effect is a transparent FFT->IFFT roundtrip with unity gain
// and low THD. Catches normalization and OLA regressions.

#include "src/grain_fog.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define SAMPLE_RATE 48000
#define FFT_SIZE    1024
#define BLOCK_SIZE  64
// Run enough samples for OLA to reach steady state and measure cleanly
// Latency: ~1792 samples (initial fill + 4x overlap warmup)
// Use 2 seconds for plenty of steady-state data
#define TOTAL_SAMPLES (SAMPLE_RATE * 2)
// Skip first portion to avoid OLA startup transients
#define SKIP_SAMPLES  4096

static float in_left[TOTAL_SAMPLES];
static float in_right[TOTAL_SAMPLES];
static float out_left[TOTAL_SAMPLES];
static float out_right[TOTAL_SAMPLES];

// Find delay between two signals using cross-correlation
static int find_delay(float *reference, float *delayed, int len, int max_lag) {
    float best_corr = -1e30f;
    int best_lag = 0;

    for (int lag = 0; lag < max_lag; lag++) {
        float corr = 0.0f;
        for (int i = 0; i < len - max_lag; i++) {
            corr += reference[i] * delayed[i + lag];
        }
        if (corr > best_corr) {
            best_corr = corr;
            best_lag = lag;
        }
    }
    return best_lag;
}

// Process all samples through fog in BLOCK_SIZE chunks
static void run_fog(grain_fog_t *fog, int num_samples) {
    for (int i = 0; i < num_samples; i += BLOCK_SIZE) {
        int remaining = num_samples - i;
        int block = remaining < BLOCK_SIZE ? remaining : BLOCK_SIZE;
        grain_fog_process_block(fog, in_left + i, in_right + i,
                                out_left + i, out_right + i, block);
    }
}

// Test 1: Sine wave passthrough with smear and specmagfilter disabled
// Expected: unity gain, low THD (< -40 dB)
static int test_sine_passthrough(void) {
    printf("Test 1: Sine passthrough (smear=off, specmagfilter=off, mix=1.0)\n");

    grain_fog_t *fog = grain_fog_create(SAMPLE_RATE, FFT_SIZE);
    if (!fog) { printf("  FAIL: couldn't create fog\n"); return 1; }

    grain_fog_set_mix(fog, 1.0f);
    grain_fog_set_smear_enabled(fog, 0);
    grain_fog_set_specmagfilter_enabled(fog, 0);

    // Generate 440 Hz sine
    float freq = 440.0f;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        in_left[i] = 0.5f * sinf(2.0f * M_PI * freq * i / SAMPLE_RATE);
        in_right[i] = in_left[i];
    }

    run_fog(fog, TOTAL_SAMPLES);

    // Find delay using cross-correlation on steady-state region
    int delay = find_delay(in_left + SKIP_SAMPLES, out_left + SKIP_SAMPLES,
                           TOTAL_SAMPLES - SKIP_SAMPLES, FFT_SIZE * 2);
    printf("  Detected delay: %d samples\n", delay);

    // Measure gain and THD on aligned steady-state region
    int start = SKIP_SAMPLES + delay;
    int end = TOTAL_SAMPLES - FFT_SIZE;  // avoid tail edge effects
    int count = end - start;
    if (count < SAMPLE_RATE / 2) {
        printf("  FAIL: not enough steady-state samples (count=%d)\n", count);
        grain_fog_destroy(fog);
        return 1;
    }

    double rms_ref = 0.0, rms_out = 0.0, rms_err = 0.0;
    for (int i = 0; i < count; i++) {
        float ref = in_left[SKIP_SAMPLES + i];
        float out = out_left[start + i];
        float err = out - ref;
        rms_ref += (double)ref * ref;
        rms_out += (double)out * out;
        rms_err += (double)err * err;
    }
    rms_ref = sqrt(rms_ref / count);
    rms_out = sqrt(rms_out / count);
    rms_err = sqrt(rms_err / count);

    float gain_db = 20.0f * log10f(rms_out / rms_ref);
    float thd_db = 20.0f * log10f(rms_err / rms_out);

    printf("  Gain: %.4f (%.2f dB)\n", (float)(rms_out / rms_ref), gain_db);
    printf("  THD:  %.2f dB\n", thd_db);

    grain_fog_destroy(fog);

    int fail = 0;
    if (fabsf(gain_db) > 0.5f) {
        printf("  FAIL: gain %.2f dB exceeds +/-0.5 dB tolerance\n", gain_db);
        fail = 1;
    }
    if (thd_db > -40.0f) {
        printf("  FAIL: THD %.2f dB exceeds -40 dB threshold\n", thd_db);
        fail = 1;
    }
    if (!fail) printf("  PASS\n");
    return fail;
}

// Test 2: DC passthrough - stable output with low ripple
static int test_dc_passthrough(void) {
    printf("Test 2: DC passthrough (constant 0.5, smear=off, specmagfilter=off)\n");

    grain_fog_t *fog = grain_fog_create(SAMPLE_RATE, FFT_SIZE);
    if (!fog) { printf("  FAIL: couldn't create fog\n"); return 1; }

    grain_fog_set_mix(fog, 1.0f);
    grain_fog_set_smear_enabled(fog, 0);
    grain_fog_set_specmagfilter_enabled(fog, 0);

    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        in_left[i] = 0.5f;
        in_right[i] = 0.5f;
    }

    run_fog(fog, TOTAL_SAMPLES);

    // Measure mean and std dev of steady-state output
    int start = SKIP_SAMPLES;
    int end = TOTAL_SAMPLES - FFT_SIZE;
    int count = end - start;

    double sum = 0.0, sum2 = 0.0;
    for (int i = start; i < end; i++) {
        sum += out_left[i];
        sum2 += (double)out_left[i] * out_left[i];
    }
    double mean = sum / count;
    double variance = sum2 / count - mean * mean;
    double stddev = sqrt(fabs(variance));

    float dc_gain = (float)(mean / 0.5);
    float ripple_db = (stddev > 1e-10) ? 20.0f * log10f(stddev / fabs(mean)) : -120.0f;

    printf("  Mean output: %.6f (expected ~0.5)\n", mean);
    printf("  DC gain:     %.4f (%.2f dB)\n", dc_gain, 20.0f * log10f(fabsf(dc_gain)));
    printf("  Ripple:      %.2f dB\n", ripple_db);

    grain_fog_destroy(fog);

    int fail = 0;
    if (fabsf(20.0f * log10f(fabsf(dc_gain))) > 0.5f) {
        printf("  FAIL: DC gain outside +/-0.5 dB\n");
        fail = 1;
    }
    if (ripple_db > -40.0f) {
        printf("  FAIL: ripple %.2f dB exceeds -40 dB threshold\n", ripple_db);
        fail = 1;
    }
    if (!fail) printf("  PASS\n");
    return fail;
}

// Test 3: mix=0 should be bit-exact dry passthrough
static int test_silence_bypass(void) {
    printf("Test 3: Silence passthrough (mix=0.0 should be clean bypass)\n");

    grain_fog_t *fog = grain_fog_create(SAMPLE_RATE, FFT_SIZE);
    if (!fog) { printf("  FAIL: couldn't create fog\n"); return 1; }

    grain_fog_set_mix(fog, 0.0f);
    // Leave smear/specmagfilter at defaults - shouldn't matter at mix=0

    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        in_left[i] = 0.5f * sinf(2.0f * M_PI * 440.0f * i / SAMPLE_RATE);
        in_right[i] = in_left[i];
    }

    run_fog(fog, TOTAL_SAMPLES);

    // Should be bit-exact passthrough
    int errors = 0;
    for (int i = 0; i < TOTAL_SAMPLES; i++) {
        if (out_left[i] != in_left[i] || out_right[i] != in_right[i]) {
            errors++;
        }
    }

    grain_fog_destroy(fog);

    if (errors > 0) {
        printf("  FAIL: %d samples differ (expected bit-exact)\n", errors);
        return 1;
    }
    printf("  PASS (bit-exact)\n");
    return 0;
}

// Test 4: Verify gain consistency across frequencies
static int test_multi_frequency_gain(void) {
    printf("Test 4: Multi-frequency gain consistency (100, 440, 1000, 5000 Hz)\n");

    float freqs[] = {100.0f, 440.0f, 1000.0f, 5000.0f};
    int nfreqs = 4;
    float gains[4];

    for (int f = 0; f < nfreqs; f++) {
        grain_fog_t *fog = grain_fog_create(SAMPLE_RATE, FFT_SIZE);
        grain_fog_set_mix(fog, 1.0f);
        grain_fog_set_smear_enabled(fog, 0);
        grain_fog_set_specmagfilter_enabled(fog, 0);

        for (int i = 0; i < TOTAL_SAMPLES; i++) {
            in_left[i] = 0.5f * sinf(2.0f * M_PI * freqs[f] * i / SAMPLE_RATE);
            in_right[i] = in_left[i];
        }

        run_fog(fog, TOTAL_SAMPLES);

        // Measure RMS in steady-state region
        int start = SKIP_SAMPLES;
        int end = TOTAL_SAMPLES - FFT_SIZE;
        double rms_in = 0.0, rms_out = 0.0;
        for (int i = start; i < end; i++) {
            rms_in += (double)in_left[i] * in_left[i];
            rms_out += (double)out_left[i] * out_left[i];
        }
        gains[f] = (float)sqrt(rms_out / rms_in);

        printf("  %5.0f Hz: gain = %.4f (%.2f dB)\n",
               freqs[f], gains[f], 20.0f * log10f(gains[f]));
        grain_fog_destroy(fog);
    }

    // Check that all gains are within 1 dB of each other
    float min_gain = gains[0], max_gain = gains[0];
    for (int f = 1; f < nfreqs; f++) {
        if (gains[f] < min_gain) min_gain = gains[f];
        if (gains[f] > max_gain) max_gain = gains[f];
    }
    float spread_db = 20.0f * log10f(max_gain / min_gain);

    if (spread_db > 1.0f) {
        printf("  FAIL: gain spread %.2f dB exceeds 1 dB tolerance\n", spread_db);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

int main(void) {
    printf("=== Fog Effect Bypass/Passthrough Tests ===\n");

    int failures = 0;
    failures += test_sine_passthrough();
    failures += test_dc_passthrough();
    failures += test_silence_bypass();
    failures += test_multi_frequency_gain();

    if (failures == 0) {
        printf("=== All tests passed ===\n");
    } else {
        printf("=== %d test(s) failed ===\n", failures);
    }
    return failures;
}
