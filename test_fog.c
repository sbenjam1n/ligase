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

// OLA timing constants derived from FFT_SIZE and 4x overlap:
//
// Signal delay: the steady-state latency from input to output is FFT_SIZE
// samples. Analytically: the first FFT fires after FFT_SIZE input samples;
// position 0 of the output buffer at that point corresponds to input sample
// FFT_SIZE - FFT_SIZE = 0, so the output at time T reads input from T - FFT_SIZE.
//
// Startup latency: frames_processed < 4 uses dry passthrough. The 4th frame
// fires after FFT_SIZE + 3*(FFT_SIZE/4) samples. Skip well past this before
// measuring to avoid startup transients.
#define OLA_HOP_SIZE        (FFT_SIZE / 4)                       // 256
#define OLA_SIGNAL_DELAY    FFT_SIZE                             // 1024 samples
#define OLA_STARTUP_SAMPLES (FFT_SIZE + 3 * OLA_HOP_SIZE)       // 1792 samples

#define BLOCK_SIZE  64
// Run 2 seconds total; skip 3x the startup window before measuring
#define TOTAL_SAMPLES       (SAMPLE_RATE * 2)
#define SKIP_SAMPLES        (OLA_STARTUP_SAMPLES * 3)           // 5376 samples

static float in_left[TOTAL_SAMPLES];
static float in_right[TOTAL_SAMPLES];
static float out_left[TOTAL_SAMPLES];
static float out_right[TOTAL_SAMPLES];

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
//
// The OLA introduces a fixed signal delay of OLA_SIGNAL_DELAY samples.
// THD is measured by aligning the output to the input using this known delay
// rather than cross-correlation, which produces ambiguous peaks for periodic
// signals (the sine autocorrelation has equal-height peaks every ~period
// samples, so cross-correlation returns delay % period rather than true delay).
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

    // Align output to input using the known analytical OLA signal delay.
    // out_left[SKIP_SAMPLES + OLA_SIGNAL_DELAY + i] corresponds to
    // in_left[SKIP_SAMPLES + i] in steady state.
    int out_start = SKIP_SAMPLES + OLA_SIGNAL_DELAY;
    int in_start  = SKIP_SAMPLES;
    int end       = TOTAL_SAMPLES - FFT_SIZE;  // avoid tail edge effects
    int count     = end - out_start;

    if (count < SAMPLE_RATE / 2) {
        printf("  FAIL: not enough steady-state samples (count=%d)\n", count);
        grain_fog_destroy(fog);
        return 1;
    }

    double rms_ref = 0.0, rms_out = 0.0, rms_err = 0.0;
    for (int i = 0; i < count; i++) {
        float ref = in_left[in_start + i];
        float out = out_left[out_start + i];
        float err = out - ref;
        rms_ref += (double)ref * ref;
        rms_out += (double)out * out;
        rms_err += (double)err * err;
    }
    rms_ref = sqrt(rms_ref / count);
    rms_out = sqrt(rms_out / count);
    rms_err = sqrt(rms_err / count);

    float gain_db = 20.0f * log10f(rms_out / rms_ref);
    float thd_db  = (rms_err > 1e-15) ? 20.0f * log10f(rms_err / rms_out) : -180.0f;

    printf("  OLA signal delay: %d samples (analytical)\n", OLA_SIGNAL_DELAY);
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

// Test 2: DC passthrough — stable output with low ripple
//
// DC has no period, so alignment is not needed. However, SKIP_SAMPLES must
// exceed OLA_STARTUP_SAMPLES (1792) to avoid measuring the startup transient
// where frames_processed < 4 forces dry passthrough instead of OLA output.
// At SKIP_SAMPLES = 3 * OLA_STARTUP_SAMPLES the OLA is fully settled.
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

    // Measure mean and std dev of steady-state output (past startup + signal delay)
    int start = SKIP_SAMPLES + OLA_SIGNAL_DELAY;
    int end   = TOTAL_SAMPLES - FFT_SIZE;
    int count = end - start;

    double sum = 0.0, sum2 = 0.0;
    for (int i = start; i < end; i++) {
        sum  += out_left[i];
        sum2 += (double)out_left[i] * out_left[i];
    }
    double mean     = sum / count;
    double variance = sum2 / count - mean * mean;
    double stddev   = sqrt(fabs(variance));

    float dc_gain   = (float)(mean / 0.5);
    float dc_gain_db = 20.0f * log10f(fabsf(dc_gain));
    float ripple_db = (stddev > 1e-10) ? 20.0f * log10f(stddev / fabs(mean)) : -120.0f;

    printf("  Mean output: %.6f (expected ~0.5)\n", mean);
    printf("  DC gain:     %.4f (%.2f dB)\n", dc_gain, dc_gain_db);
    printf("  Ripple:      %.2f dB\n", ripple_db);

    grain_fog_destroy(fog);

    int fail = 0;
    if (fabsf(dc_gain_db) > 0.5f) {
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
// Uses RMS comparison of aligned (delay-corrected) output vs input.
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

        // Compare aligned RMS: out[SKIP + DELAY + i] vs in[SKIP + i]
        int out_start = SKIP_SAMPLES + OLA_SIGNAL_DELAY;
        int end       = TOTAL_SAMPLES - FFT_SIZE;
        int count     = end - out_start;
        double rms_in = 0.0, rms_out = 0.0;
        for (int i = 0; i < count; i++) {
            rms_in  += (double)in_left[SKIP_SAMPLES + i] * in_left[SKIP_SAMPLES + i];
            rms_out += (double)out_left[out_start + i]   * out_left[out_start + i];
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
