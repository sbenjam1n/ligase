/* LigasePlugin.hpp — ligase~ hosted as a DPF plugin. The DSP class + the small lock-free
 * "shared bus" the web-view UI reads through DPF direct access (same process).
 *
 * Threading (mirrors Pd's single-threaded scheduler, see ligase_engine.h):
 *   - audio thread: run() drains parameter changes + the command ring, handles MIDI and host
 *     transport, then processes 64-frame inner blocks (Pd's block) through the engine.
 *   - host/UI threads: parameters are written lock-free (dirty flags); text commands go into
 *     an SPSC ring; HEAVY operations (reel load/save, morph files, state restore) run on the
 *     caller thread under fEngineMutex while the audio thread outputs silence (try_lock).
 */
#ifndef LIGASE_PLUGIN_HPP
#define LIGASE_PLUGIN_HPP

#include "DistrhoPlugin.hpp"
#include "ligase_engine.h"
#include "ligase_params.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <string>
#include <vector>

START_NAMESPACE_DISTRHO

namespace ligase {

constexpr int kBlock          = 64;      /* inner DSP block (Pd's) */
constexpr int kOutSlots       = 256;     /* outlet-9 / console ring depth */
constexpr int kOutBytes       = 512;
constexpr int kCmdSlots       = 512;     /* UI->DSP command ring depth */
constexpr int kCmdBytes       = 1024;
constexpr int kScopeLen       = 1024;    /* scope XY history (samples) */
constexpr int kLiveSnapshot   = 63;      /* engine snapshot slot reserved for the live-voice capture */
constexpr float kGlideMs      = 20.0f;   /* the panel's [line~] CV glide */
constexpr double kEmbedMaxSec = 60.0;    /* reels up to this length are embedded in the DAW project */

/* Single-producer / single-consumer ring of fixed-size C strings. */
template <int SLOTS, int BYTES>
struct TextRing {
    char slots[SLOTS][BYTES];
    std::atomic<uint32_t> head{0}, tail{0};
    bool push(const char* s) noexcept {
        const uint32_t h = head.load(std::memory_order_relaxed), t = tail.load(std::memory_order_acquire);
        if (h - t >= (uint32_t)SLOTS) return false;
        size_t n = std::strlen(s); if (n > (size_t)BYTES - 1) n = BYTES - 1;
        std::memcpy(slots[h % SLOTS], s, n); slots[h % SLOTS][n] = 0;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(char* out, size_t n) noexcept {
        const uint32_t t = tail.load(std::memory_order_relaxed), h = head.load(std::memory_order_acquire);
        if (t == h) return false;
        size_t len = std::strlen(slots[t % SLOTS]); if (len > n - 1) len = n - 1;
        std::memcpy(out, slots[t % SLOTS], len); out[len] = 0;
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
    bool empty() const noexcept { return tail.load(std::memory_order_acquire) == head.load(std::memory_order_acquire); }
};

std::string base64Encode(const uint8_t* data, size_t len);
std::vector<uint8_t> base64Decode(const std::string& text);

/* The message JOURNAL: the last value of every *settable* engine message that reached the
 * engine from the UI, keyed so a replay reconstructs the non-snapshot state (matrix
 * routings, patterns, quant grids, generator shapes, ...). Transport/actions/file ops are
 * never journaled; snapshot bodies + the morph surface travel in the "voice" state instead. */
class Journal {
public:
    void record(const std::string& text);            /* one message, text form */
    std::string serialize() const;                   /* newline-joined messages */
    void clear();
private:
    static bool isDenied(const std::string& sel);
    static std::string keyFor(const std::string& sel, const std::vector<std::string>& args);
    mutable std::mutex fMutex;
    std::map<std::string, std::string> fEntries;
};

} // namespace ligase

class LigasePlugin : public Plugin
{
public:
    LigasePlugin();
    ~LigasePlugin() override;

    /* ---- what the UI reads through direct access (all lock-free / atomic) ---- */
    ligase::TextRing<ligase::kOutSlots, ligase::kOutBytes> outlet9;   /* outlet-9 messages, text form */
    ligase::TextRing<ligase::kOutSlots, ligase::kOutBytes> console;   /* post()/pd_error() lines ("!" prefix = error) */
    float scopeX[ligase::kScopeLen];
    float scopeY[ligase::kScopeLen];
    std::atomic<uint32_t> scopeWrite{0};
    std::atomic<float> peakL{0.f}, peakR{0.f};
    std::atomic<uint32_t> statusSeq{0};
    ligase_status_t statusSnap;                                       /* refreshed ~30 Hz by run() */
    std::string panelState();                                         /* the UI's persisted panel-side blob */
    std::string vocabulary();                                         /* "sel/sig" lines: the engine's message table */

protected:
    /* Information */
    const char* getLabel() const override { return "ligase"; }
    const char* getDescription() const override {
        return "ligase~ granular tape synthesizer / sampler / looper / delay: real-time recording into a 10-minute "
               "reel with splices, asynchronous granular playback, chordal MIDI, delay/smear/distortion/ladder "
               "filter, a modulation matrix and a snapshot metasurface. Audio in records; MIDI in plays.";
    }
    const char* getMaker() const override { return "Steven Benjamin"; }
    const char* getHomePage() const override { return "https://github.com/sbenjam1n/ligase"; }
    const char* getLicense() const override { return "GPL-2.0-only"; }
    uint32_t getVersion() const override { return d_version(0, 1, 0); }
    int64_t getUniqueId() const override { return d_cconst('L', 'g', 'a', 's'); }

    /* Init */
    void initAudioPort(bool input, uint32_t index, AudioPort& port) override;
    void initParameter(uint32_t index, Parameter& parameter) override;
    void initPortGroup(uint32_t groupId, PortGroup& portGroup) override;
    void initState(uint32_t index, State& state) override;

    /* Internal data */
    float getParameterValue(uint32_t index) const override;
    void setParameterValue(uint32_t index, float value) override;
    String getState(const char* key) const override;
    void setState(const char* key, const char* value) override;

    /* Processing */
    void activate() override;
    void deactivate() override;
    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) override;
    void bufferSizeChanged(uint32_t newBufferSize) override;
    void sampleRateChanged(double newSampleRate) override;

private:
    /* engine hooks */
    static void onPrint(void* user, int level, const char* text);
    static void onOutlet(void* user, int outlet, const char* sel, int argc, const ligase_atom_t* argv);

    void applyDefaults();                       /* the panel's loadbang contract + every init_send default */
    void applyParameter(uint32_t index, float value);   /* audio thread: param -> engine */
    void drainParametersAndCommands();          /* audio thread */
    void handleMidi(const MidiEvent* events, uint32_t count);
    void handleTransport(uint32_t frames);
    void processInnerBlock(const float* inL, const float* inR, float* outL, float* outR);
    void sendCommand(const char* text, bool journal);   /* any thread: heavy -> now under lock, light -> ring */
    static bool isHeavy(const char* text);
    std::string tempPath(const char* suffix) const;
    void updateStatusAndMeters();
    void configureLatency(uint32_t bufferSize);
    void setCCMap(const std::string& text);
    std::string ccMapText() const;

    ligase_engine_t* fEngine;
    mutable std::mutex fEngineMutex;

    /* parameters */
    std::atomic<float> fParamValue[LIGASE_PARAM_COUNT];
    std::atomic<bool>  fParamDirty[LIGASE_PARAM_COUNT];
    std::atomic<float> fOutValue[LIGASE_PARAM_COUNT];
    std::vector<std::string> fGroups;
    int fGroupOf[LIGASE_PARAM_COUNT];

    /* plugin-side specials (audio-thread owned unless atomic) */
    int   fRecMode;            /* 0 INPUT 1 SPLICE 2 OVRDUB (panel order) */
    bool  fRecording;
    std::atomic<float> fMaster{1.0f};
    std::atomic<int>   fClockHost{1};
    std::atomic<float> fVelAmp{0.0f};
    std::atomic<int>   fBendCents{50};
    float fVelGain;            /* last note velocity -> level multiplier */
    /* message-delivered knobs (LP_INLET with a selector): a 20 ms per-block ramp, each step sent
     * as `<sel> <value>` with the engine's console quiet (the panel's [line~] glide, as messages) */
    struct Ramp { float cur = 0.f, to = 0.f; int left = 0; bool active = false; };
    Ramp fRamp[LIGASE_PARAM_COUNT] = {};
    void stepRamps();
    void sendKnobMessage(uint32_t index, float v);
    void sendMorphCursor();
    int   fMidiChGrain, fMidiChSmear;
    std::map<int, int> fCCMap;  /* cc number -> parameter index */
    mutable std::mutex fCCMutex;

    /* command ring (UI -> audio) */
    ligase::TextRing<ligase::kCmdSlots, ligase::kCmdBytes> fCmd;
    std::mutex fCmdProducerMutex;
    ligase::Journal fJournal;

    /* state */
    std::string fReelPath;
    std::string fPanelState;
    mutable std::mutex fStateMutex;

    /* transport */
    bool   fWasPlaying;
    double fLastBeat;          /* absolute beat position at the end of the previous block, -1 = none */

    /* FIFO for host blocks that are not multiples of the inner block */
    float fInAcc[2][ligase::kBlock];
    int   fInFill;
    float fOutQ[2][4 * ligase::kBlock];
    int   fOutHead, fOutCount;
    bool  fPrimed;
    uint32_t fLatency;
    float fBlockOutL[ligase::kBlock], fBlockOutR[ligase::kBlock], fBlockSX[ligase::kBlock], fBlockSY[ligase::kBlock];
    uint32_t fStatusCounter;
    uint32_t fRunCount;        /* run() calls so far */
    std::atomic<bool> fActive{false};
    std::atomic<uint32_t> fCmdDrops{0};

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LigasePlugin)
};

END_NAMESPACE_DISTRHO
#endif
