/* LigaseUI.cpp — the plugin GUI: the SVG control surface in a DPF web view.
 *
 * The page (resources/index.html, built by ui/build_page.py) is the SAME surface as the
 * browser prototype: silkscreen SVG + emit_web overlay + the panel brain, talking to this
 * plugin through plugin/ui/bridge.js. DPF forwards parameterChanged/stateChanged into the page
 * and delivers the page's setParameterValue/setState/sendNote calls to the DSP side. The extra
 * live channels (outlet-9 replies, console, transport status, VU, scope XY) are pulled from the
 * plugin instance here on the UI thread (DPF direct access) and pushed as base64 "state"
 * updates the page decodes — no DPF modification needed.
 */
#include "DistrhoUI.hpp"
#include "LigasePlugin.hpp"

#include <cstdio>
#include <cstring>
#include <string>

START_NAMESPACE_DISTRHO

class LigaseUI : public UI
{
public:
    LigaseUI()
        : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT),
          fLastStatusSeq(0), fIdleCount(0), fSentHello(false)
    {
        setGeometryConstraints(614, 280, true, false);
    }

protected:
    void parameterChanged(uint32_t index, float value) override { UI::parameterChanged(index, value); }
    void stateChanged(const char* key, const char* value) override { UI::stateChanged(key, value); }
    void sampleRateChanged(double sr) override { UI::sampleRateChanged(sr); }

    void uiIdle() override
    {
        LigasePlugin* const plugin = static_cast<LigasePlugin*>(getPluginInstancePointer());
        if (plugin == nullptr) return;
        ++fIdleCount;

        if (!fSentHello) {
            fSentHello = true;
            push("hello", plugin->vocabulary());
            const std::string panel = plugin->panelState();
            if (!panel.empty()) push("panel", panel);
        }

        /* outlet-9 replies + console lines: bundle whatever is queued into one update each */
        char line[ligase::kOutBytes];
        std::string out9;
        for (int n = 0; n < 64 && plugin->outlet9.pop(line, sizeof line); n++) { out9 += line; out9 += '\n'; }
        if (!out9.empty()) push("out9", out9);
        std::string log;
        for (int n = 0; n < 64 && plugin->console.pop(line, sizeof line); n++) { log += line; log += '\n'; }
        if (!log.empty()) push("log", log);

        /* transport / reel / morph status ~10 Hz (uiIdle runs ~30-60 Hz) */
        const uint32_t seq = plugin->statusSeq.load(std::memory_order_acquire);
        if (seq != fLastStatusSeq && (fIdleCount % 3) == 0) {
            fLastStatusSeq = seq;
            const ligase_status_t& st = plugin->statusSnap;
            char buf[1024];
            std::snprintf(buf, sizeof buf,
                "{\"splice\":%d,\"splices\":%d,\"spliceStart\":%d,\"spliceEnd\":%d,\"playing\":%d,\"recording\":%d,"
                "\"recMode\":%d,\"bpm\":%.3f,\"reelLen\":%d,\"reelSr\":%d,\"voices\":%d,\"activeGrains\":%d,"
                "\"snapbufHas\":%d,\"vu\":[%.4f,%.4f],\"morph\":{\"x\":%.4f,\"y\":%.4f,\"points\":%d,\"route\":%d,\"running\":%d},"
                "\"snapshotMaskLo\":%u,\"snapshotMaskHi\":%u}",
                st.splice_current, st.splice_count, st.splice_start, st.splice_end, st.playing, st.recording,
                st.record_mode, (double)st.bpm, st.reel_length, st.sample_rate, st.voice_count, st.active_grains,
                st.snapbuf_has, (double)plugin->peakL.load(), (double)plugin->peakR.load(),
                (double)st.morph_cursor_x, (double)st.morph_cursor_y, st.morph_points, st.morph_route_len, st.morph_running,
                (unsigned)(st.snapshot_mask & 0xFFFFFFFFu), (unsigned)(st.snapshot_mask >> 32));
            push("status", buf);
        }

        /* scope XY: the latest 256 samples as a compact CSV ~20 Hz */
        if ((fIdleCount % 2) == 0) {
            const uint32_t w = plugin->scopeWrite.load(std::memory_order_acquire);
            std::string csv; csv.reserve(256 * 14);
            char num[32];
            for (int i = 0; i < 256; i++) {
                const uint32_t idx = (w + ligase::kScopeLen - 256 + (uint32_t)i) % ligase::kScopeLen;
                std::snprintf(num, sizeof num, "%.3f,%.3f;", (double)plugin->scopeX[idx], (double)plugin->scopeY[idx]);
                csv += num;
            }
            push("scope", csv);
        }
    }

private:
    /* base64 keeps the payload free of quotes/newlines: UI::stateChanged evaluates
     * stateChanged('key','value') in the page verbatim. */
    void push(const char* key, const std::string& value)
    {
        const std::string b64 = ligase::base64Encode((const uint8_t*)value.data(), value.size());
        UI::stateChanged(key, b64.c_str());
    }

    uint32_t fLastStatusSeq;
    uint32_t fIdleCount;
    bool fSentHello;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LigaseUI)
};

UI* createUI() { return new LigaseUI(); }

END_NAMESPACE_DISTRHO
