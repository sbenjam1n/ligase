/* LigasePlugin.cpp — the DSP side of ligase~ as a DPF plugin. See LigasePlugin.hpp. */
#include "LigasePlugin.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <algorithm>
#ifdef _WIN32
# include <windows.h>
# include <process.h>
#else
# include <unistd.h>
#endif

START_NAMESPACE_DISTRHO

namespace ligase {

/* ---- base64 ------------------------------------------------------------------------------ */
static const char* kB64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const uint8_t* data, size_t len) {
    std::string out; out.reserve((len + 2) / 3 * 4);
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16;
        if (i + 1 < len) v |= (uint32_t)data[i + 1] << 8;
        if (i + 2 < len) v |= (uint32_t)data[i + 2];
        out += kB64[(v >> 18) & 63]; out += kB64[(v >> 12) & 63];
        out += (i + 1 < len) ? kB64[(v >> 6) & 63] : '=';
        out += (i + 2 < len) ? kB64[v & 63] : '=';
    }
    return out;
}

std::vector<uint8_t> base64Decode(const std::string& text) {
    std::vector<uint8_t> out; out.reserve(text.size() * 3 / 4);
    uint32_t acc = 0; int bits = 0;
    for (char c : text) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+') v = 62; else if (c == '/') v = 63;
        else continue;   /* '=' padding, whitespace */
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; out.push_back((uint8_t)((acc >> bits) & 0xFF)); }
    }
    return out;
}

/* ---- journal ------------------------------------------------------------------------------ */
static std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> t; std::istringstream ss(text); std::string w;
    while (ss >> w) t.push_back(w);
    return t;
}
static bool isNumber(const std::string& s) {
    char* end = nullptr; std::strtod(s.c_str(), &end); return end && *end == 0 && !s.empty();
}

bool Journal::isDenied(const std::string& sel) {
    static const char* const deny[] = {
        "play", "record", "recinput", "recsplice", "stut", "trigger", "bang", "clockstop", "load", "save",
        "get_inlets", "query", "get_params", "get_ranges", "get_generators", "get_state",
        "snapshot", "snapshot_recall", "snapshot_clear", "morph_point", "morph_place", "morph_unplace", "morph",
        "morph_x", "morph_y", "morph_run", "morph_stop", "morph_pause", "morph_route", "morph_route_clear",
        "morph_save", "morph_load", "morph_state", "morph_export", "morph_import", "morph_cursor",
        "splice", "shift", "organize", "clear_splices", "clear_splices_except_current", "splice_join_right",
        "splice_join_all", "clear_current_splice", "chord", "midi", "sphere_kick", "sphere_kick_rand",
        "perlin_reset", "lorenz_reset", "nbody_reset", "sphere_reset", "gdelay_clear", "bencina_clear",
        "matrix_dump", "pattern_debug", "smear_pitch_debug", "dsp", "float", "headless", "snapbuf_dump",
        "snapbuf_compare", "snapbuf_audition", "snapbuf_apply", "snapbuf_store", "snapbuf_load",
        "snapbuf_from_live", "snapbuf_clear", "snapbuf_set", "snapbuf_get", NULL };
    for (int i = 0; deny[i]; i++) if (sel == deny[i]) return true;
    return false;
}

std::string Journal::keyFor(const std::string& sel, const std::vector<std::string>& args) {
    static const char* const instanceSels[] = {
        "waveform_phase", "square_pw", "saw_skew", "nbody_epsilon", "nbody_damping", "nbody_pump", "nbody_G",
        "nbody_mode", "lorenz_sigma", "lorenz_rho", "lorenz_beta", "sphere_damping", "sphere_elasticity",
        "sphere_mode", "sphere_spin", "pitch_scale_to", "smear_pitch_scale_to", "splice_msg", "clear_splice_msg", NULL };
    for (int i = 0; instanceSels[i]; i++)
        if (sel == instanceSels[i]) return args.empty() ? sel : sel + " " + args[0];
    if (sel == "rand_type") {                         /* rand_type <gen> <param> : one source per param */
        for (auto it = args.rbegin(); it != args.rend(); ++it) if (!isNumber(*it)) return sel + " " + *it;
        return sel;
    }
    std::string key = sel;
    for (const std::string& a : args) if (!isNumber(a)) key += " " + a;   /* matrix_connect src dst, param_range name, pattern target ... */
    return key;
}

void Journal::record(const std::string& text) {
    std::vector<std::string> t = tokenize(text);
    if (t.empty()) return;
    const std::string sel = t[0];
    if (isDenied(sel)) return;
    std::vector<std::string> args(t.begin() + 1, t.end());
    std::lock_guard<std::mutex> lk(fMutex);
    if (sel == "matrix_clear") {
        for (auto it = fEntries.begin(); it != fEntries.end();)
            if (it->first.rfind("matrix_connect", 0) == 0) it = fEntries.erase(it); else ++it;
        return;
    }
    if (sel == "matrix_disconnect") {
        fEntries.erase(keyFor("matrix_connect", args));
        return;
    }
    if (sel == "pattern_clear") {
        fEntries.erase(keyFor("pattern", args));
        return;
    }
    if (sel == "pattern" && !args.empty() && args[0] == "event") {   /* pattern event <action> <tokens>: key by action */
        std::string key = "pattern event"; if (args.size() > 1) key += " " + args[1];
        fEntries[key] = text; return;
    }
    fEntries[keyFor(sel, args)] = text;
}

std::string Journal::serialize() const {
    std::lock_guard<std::mutex> lk(fMutex);
    std::string out;
    for (const auto& kv : fEntries) { out += kv.second; out += '\n'; }
    return out;
}
void Journal::clear() { std::lock_guard<std::mutex> lk(fMutex); fEntries.clear(); }

} // namespace ligase

using namespace ligase;

/* ============================================================================================ */

LigasePlugin::LigasePlugin()
    : Plugin(LIGASE_PARAM_COUNT, 0, 7),
      fEngine(nullptr), fRecMode(2), fRecording(false), fVelGain(1.0f), fMidiChGrain(1), fMidiChSmear(2),
      fWasPlaying(false), fLastBeat(-1.0), fInFill(0), fOutHead(0), fOutCount(0), fPrimed(false), fLatency(0),
      fStatusCounter(0)
{
    std::memset(scopeX, 0, sizeof scopeX); std::memset(scopeY, 0, sizeof scopeY);
    std::memset(&statusSnap, 0, sizeof statusSnap);
    std::memset(fInAcc, 0, sizeof fInAcc); std::memset(fOutQ, 0, sizeof fOutQ);

    /* groups: first-appearance order of the table's group strings */
    for (uint32_t i = 0; i < LIGASE_PARAM_COUNT; i++) {
        const std::string g = LIGASE_PARAMS[i].group;
        size_t k = 0; for (; k < fGroups.size(); k++) if (fGroups[k] == g) break;
        if (k == fGroups.size()) fGroups.push_back(g);
        fGroupOf[i] = (int)k;
        fParamValue[i].store(LIGASE_PARAMS[i].def);
        fParamDirty[i].store(false);
        fOutValue[i].store(0.0f);
    }
    /* default CC map */
    struct { int cc; const char* id; } defaults[] = {
        { 1, "dly_mix" }, { 7, "level" }, { 10, "pan" }, { 11, "sos" }, { 12, "grainsize" }, { 13, "density" },
        { 14, "speed" }, { 15, "start" }, { 16, "joy_x" }, { 17, "joy_y" }, { 71, "resonance" }, { 74, "cutoff" },
        { 91, "dly_feed" }, { 93, "smr_mix" }, { 94, "flt_mix" }, { 64, "play" } };
    for (auto& d : defaults)
        for (uint32_t i = 0; i < LIGASE_PARAM_COUNT; i++) if (!std::strcmp(LIGASE_PARAMS[i].id, d.id)) fCCMap[d.cc] = (int)i;

    fEngine = ligase_engine_new((int)getSampleRate(), kBlock, nullptr);
    DISTRHO_SAFE_ASSERT_RETURN(fEngine != nullptr,);
    ligase_engine_set_callbacks(fEngine, onPrint, onOutlet, this);
    applyDefaults();
    configureLatency(getBufferSize());
}

LigasePlugin::~LigasePlugin() {
    std::lock_guard<std::mutex> lk(fEngineMutex);
    if (fEngine) { ligase_engine_free(fEngine); fEngine = nullptr; }
}

/* ---- engine hooks ------------------------------------------------------------------------- */
void LigasePlugin::onPrint(void* user, int level, const char* text) {
    LigasePlugin* self = static_cast<LigasePlugin*>(user);
    char line[kOutBytes];
    std::snprintf(line, sizeof line, "%s%s", level ? "!" : "", text);
    self->console.push(line);
    if (level && std::getenv("LIGASE_PLUGIN_STDERR")) std::fprintf(stderr, "%s\n", text);
}

void LigasePlugin::onOutlet(void* user, int outlet, const char* sel, int argc, const ligase_atom_t* argv) {
    LigasePlugin* self = static_cast<LigasePlugin*>(user);
    if (outlet >= 4 && outlet <= 7) {                       /* modout1-4 -> output parameters */
        if (argc >= 1 && argv[0].type == LIGASE_ATOM_FLOAT) self->fOutValue[LP_O_MODOUT1 + (outlet - 4)].store(argv[0].f);
        return;
    }
    if (outlet != 8) return;                                 /* bangs on 2/3 are not surfaced (yet) */
    char line[kOutBytes]; size_t n = 0;
    n += (size_t)std::snprintf(line, sizeof line, "%s", sel);
    for (int i = 0; i < argc && n < sizeof line - 32; i++) {
        if (argv[i].type == LIGASE_ATOM_FLOAT) n += (size_t)std::snprintf(line + n, sizeof line - n, " %.9g", (double)argv[i].f);
        else n += (size_t)std::snprintf(line + n, sizeof line - n, " %s", argv[i].s ? argv[i].s : "?");
    }
    self->outlet9.push(line);
}

/* ---- defaults: the panel's loadbang contract --------------------------------------------- */
void LigasePlugin::applyDefaults() {
    /* emit_pd.py load contract: the surface IS the hardware (every inlet driven), the scope shows
     * the Lorenz butterfly, the morph cursor is the CV pair (inlets 23/24 = joy_x/joy_y) */
    ligase_engine_send_text(fEngine, "headless 0; scope_tap lorenz 1; morph_cursor 1");
    for (uint32_t i = 0; i < LIGASE_PARAM_COUNT; i++) {
        const lp_param_t& p = LIGASE_PARAMS[i];
        if (p.kind == LP_OUTPUT || p.kind == LP_TRIGGER) continue;
        if (p.kind == LP_INLET) { ligase_engine_set_inlet(fEngine, p.inlet - 1, p.def, 0.0f); continue; }
        if (!p.init_send) continue;
        applyParameter(i, p.def);
    }
}

/* ---- ports / parameters / groups / states ------------------------------------------------- */
void LigasePlugin::initAudioPort(bool input, uint32_t index, AudioPort& port) {
    port.groupId = kPortGroupStereo;
    port.name   = input ? (index == 0 ? "Input Left" : "Input Right") : (index == 0 ? "Output Left" : "Output Right");
    port.symbol = input ? (index == 0 ? "in_l" : "in_r") : (index == 0 ? "out_l" : "out_r");
}

void LigasePlugin::initParameter(uint32_t index, Parameter& parameter) {
    DISTRHO_SAFE_ASSERT_RETURN(index < LIGASE_PARAM_COUNT,);
    const lp_param_t& p = LIGASE_PARAMS[index];
    uint32_t hints = 0;
    if (p.kind == LP_OUTPUT) hints |= kParameterIsOutput; else hints |= kParameterIsAutomatable;
    if (p.is_int) hints |= kParameterIsInteger;
    if (p.is_bool) hints |= kParameterIsBoolean;
    if (p.is_log) hints |= kParameterIsLogarithmic;
    if (p.kind == LP_TRIGGER) hints |= kParameterIsTrigger;
    parameter.hints = hints;
    parameter.name = p.name;
    parameter.symbol = p.id;
    parameter.unit = p.unit;
    parameter.ranges.def = p.def; parameter.ranges.min = p.lo; parameter.ranges.max = p.hi;
    parameter.groupId = (uint32_t)fGroupOf[index];
    if (p.labels != nullptr && p.nmap > 0) {
        const int n = p.nmap;
        ParameterEnumerationValue* ev = new ParameterEnumerationValue[n];
        for (int k = 0; k < n; k++) { ev[k].value = p.lo + (float)k; ev[k].label = p.labels[k]; }
        parameter.enumValues.count = (uint8_t)n; parameter.enumValues.values = ev; parameter.enumValues.restrictedMode = true;
    } else if (p.labels != nullptr) {
        const int n = (int)(p.hi - p.lo) + 1;   /* labelled switch without a message map (recmode, clock_src) */
        ParameterEnumerationValue* ev = new ParameterEnumerationValue[n];
        for (int k = 0; k < n; k++) { ev[k].value = p.lo + (float)k; ev[k].label = p.labels[k]; }
        parameter.enumValues.count = (uint8_t)n; parameter.enumValues.values = ev; parameter.enumValues.restrictedMode = true;
    }
}

void LigasePlugin::initPortGroup(uint32_t groupId, PortGroup& portGroup) {
    if (groupId >= fGroups.size()) return;
    portGroup.name = fGroups[groupId].c_str();
    std::string sym;
    for (char c : fGroups[groupId]) sym += (std::isalnum((unsigned char)c) ? (char)std::tolower((unsigned char)c) : '_');
    if (sym.empty() || !(std::isalpha((unsigned char)sym[0]) || sym[0] == '_')) sym = "g_" + sym;
    portGroup.symbol = sym.c_str();
}

void LigasePlugin::initState(uint32_t index, State& state) {
    switch (index) {
    case 0: state.key = "voice";     state.label = "Voice (snapshots + morph surface, text schema)"; state.hints = kStateIsHostReadable; break;
    case 1: state.key = "journal";   state.label = "Message journal (matrix, patterns, grids, shapes)"; state.hints = kStateIsHostReadable; break;
    case 2: state.key = "reel_path"; state.label = "Reel file path (last load/save)"; state.hints = kStateIsHostReadable; break;
    case 3: state.key = "reel";      state.label = "Reel audio + splices (embedded WAV, reels <= 60 s)"; state.hints = kStateIsBase64Blob; break;
    case 4: state.key = "panel";     state.label = "Panel-side surface state"; state.hints = kStateIsHostReadable; break;
    case 5: state.key = "midimap";   state.label = "MIDI CC map (cc=parameter per line)"; state.hints = kStateIsHostWritable; state.defaultValue = ccMapText().c_str(); break;
    case 6: state.key = "cmd";       state.label = "Engine message (transient)"; state.hints = kStateIsOnlyForDSP; break;
    }
}

/* ---- parameters ----------------------------------------------------------------------------- */
float LigasePlugin::getParameterValue(uint32_t index) const {
    if (index >= LIGASE_PARAM_COUNT) return 0.0f;
    if (LIGASE_PARAMS[index].kind == LP_OUTPUT) return fOutValue[index].load();
    return fParamValue[index].load();
}

void LigasePlugin::setParameterValue(uint32_t index, float value) {
    if (index >= LIGASE_PARAM_COUNT) return;
    const lp_param_t& p = LIGASE_PARAMS[index];
    if (p.kind == LP_OUTPUT) return;
    if (p.kind == LP_TRIGGER) { if (value > 0.5f) { fParamValue[index].store(1.0f); fParamDirty[index].store(true); } return; }
    fParamValue[index].store(value);
    fParamDirty[index].store(true, std::memory_order_release);
}

/* audio thread (or constructor): one parameter reaches the engine */
void LigasePlugin::applyParameter(uint32_t index, float value) {
    const lp_param_t& p = LIGASE_PARAMS[index];
    char buf[256];
    switch (p.kind) {
    case LP_INLET: {
        float v = value;
        if (index == LP_LEVEL) v *= fVelGain;
        ligase_engine_set_inlet(fEngine, p.inlet - 1, v, kGlideMs);
        break;
    }
    case LP_MSG:
        std::snprintf(buf, sizeof buf, "%s %.9g", p.sel, (double)value);
        ligase_engine_send_text(fEngine, buf);
        break;
    case LP_MSG2:   /* midi_channel <grain> <smear> */
        if (index == LP_MIDI_CH_GRAIN) fMidiChGrain = (int)std::lround(value); else fMidiChSmear = (int)std::lround(value);
        std::snprintf(buf, sizeof buf, "midi_channel %d %d", fMidiChGrain, fMidiChSmear);
        ligase_engine_send_text(fEngine, buf);
        break;
    case LP_MSGMAP: {
        int k = (int)std::lround(value - p.lo);
        if (k < 0) k = 0;
        if (k >= p.nmap) k = p.nmap - 1;
        if (p.map[k]) ligase_engine_send_text(fEngine, p.map[k]);
        break;
    }
    case LP_TOGGLE:
        std::snprintf(buf, sizeof buf, "%s %d", p.sel, value > 0.5f ? 1 : 0);
        ligase_engine_send_text(fEngine, buf);
        break;
    case LP_TRIGGER:
        ligase_engine_send_text(fEngine, p.sel);
        fParamValue[index].store(0.0f);
        break;
    case LP_SPECIAL:
        if (!std::strcmp(p.sel, "recmode")) { fRecMode = (int)std::lround(value); }
        else if (!std::strcmp(p.sel, "record")) {
            const bool on = value > 0.5f;
            if (on && !fRecording) {
                ligase_engine_send_text(fEngine, fRecMode == 0 ? "recinput" : fRecMode == 1 ? "recsplice" : "record 1");
                fRecording = true;
            } else if (!on && fRecording) { ligase_engine_send_text(fEngine, "record 0"); fRecording = false; }
        }
        else if (!std::strcmp(p.sel, "dist_preset")) {
            int k = (int)std::lround(value);
            if (k < 1) k = 1;
            if (k > 8) k = 8;
            ligase_engine_send_text(fEngine, LIGASE_DIST_PRESETS[k - 1]);
        }
        else if (!std::strcmp(p.sel, "master")) fMaster.store(value);
        else if (!std::strcmp(p.sel, "clock_src")) { fClockHost.store(value > 0.5f ? 1 : 0); if (value <= 0.5f) fLastBeat = -1.0; }
        else if (!std::strcmp(p.sel, "midi_vel_amp")) fVelAmp.store(value);
        else if (!std::strcmp(p.sel, "midi_bend_cents")) fBendCents.store((int)std::lround(value));
        break;
    case LP_OUTPUT: break;
    }
}

void LigasePlugin::drainParametersAndCommands() {
    for (uint32_t i = 0; i < LIGASE_PARAM_COUNT; i++) {
        if (fParamDirty[i].load(std::memory_order_acquire)) {
            fParamDirty[i].store(false, std::memory_order_relaxed);
            applyParameter(i, fParamValue[i].load());
        }
    }
    char cmd[kCmdBytes];
    int budget = 64;   /* bound the work per block */
    while (budget-- > 0 && fCmd.pop(cmd, sizeof cmd)) ligase_engine_send_text(fEngine, cmd);
}

/* ---- commands ------------------------------------------------------------------------------- */
bool LigasePlugin::isHeavy(const char* text) {
    static const char* const heavy[] = { "load", "save", "morph_load", "morph_save", "morph_import", "morph_export", NULL };
    const char* p = text; while (*p == ' ' || *p == '\t') p++;
    for (int i = 0; heavy[i]; i++) {
        const size_t n = std::strlen(heavy[i]);
        if (!std::strncmp(p, heavy[i], n) && (p[n] == ' ' || p[n] == 0 || p[n] == ';')) return true;
    }
    return std::strchr(p, ';') != nullptr && (std::strstr(p, "load ") || std::strstr(p, "save ") || std::strstr(p, "morph_import ") || std::strstr(p, "morph_export "));
}

void LigasePlugin::sendCommand(const char* text, bool journal) {
    if (!text || !*text) return;
    if (journal) {
        /* journal each ';'-separated message */
        std::string s(text); size_t pos = 0;
        while (pos <= s.size()) {
            size_t e = s.find_first_of(";\n", pos); if (e == std::string::npos) e = s.size();
            std::string one = s.substr(pos, e - pos);
            size_t a = one.find_first_not_of(" \t"); if (a != std::string::npos) fJournal.record(one.substr(a));
            pos = e + 1;
        }
    }
    if (isHeavy(text)) {
        std::lock_guard<std::mutex> lk(fEngineMutex);
        /* remember the reel path for state (load/save <path>) */
        const char* p = text; while (*p == ' ') p++;
        if (!std::strncmp(p, "load ", 5) || !std::strncmp(p, "save ", 5)) {
            std::lock_guard<std::mutex> sl(fStateMutex);
            fReelPath = std::string(p + 5); size_t e = fReelPath.find_first_of(";\n"); if (e != std::string::npos) fReelPath.erase(e);
        }
        ligase_engine_send_text(fEngine, text);
        return;
    }
    std::lock_guard<std::mutex> lk(fCmdProducerMutex);
    if (!fCmd.push(text)) fCmdDrops.fetch_add(1);
}

/* ---- state ---------------------------------------------------------------------------------- */
std::string LigasePlugin::tempPath(const char* suffix) const {
    char buf[512];
#ifdef _WIN32
    char tmp[MAX_PATH]; DWORD n = GetTempPathA(MAX_PATH, tmp); if (n == 0) std::strcpy(tmp, ".");
    std::snprintf(buf, sizeof buf, "%s\\ligase_%lu_%p%s", tmp, (unsigned long)_getpid(), (const void*)this, suffix);
#else
    const char* tmp = std::getenv("TMPDIR"); if (!tmp || !*tmp) tmp = "/tmp";
    std::snprintf(buf, sizeof buf, "%s/ligase_%ld_%p%s", tmp, (long)getpid(), (const void*)this, suffix);
#endif
    return buf;
}

static std::string readFile(const std::string& path, bool binary) {
    std::string out; FILE* f = std::fopen(path.c_str(), binary ? "rb" : "r"); if (!f) return out;
    char buf[65536]; size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
    std::fclose(f); return out;
}
static bool writeFile(const std::string& path, const void* data, size_t len) {
    FILE* f = std::fopen(path.c_str(), "wb"); if (!f) return false;
    const bool ok = std::fwrite(data, 1, len, f) == len; std::fclose(f); return ok;
}

String LigasePlugin::getState(const char* key) const {
    LigasePlugin* self = const_cast<LigasePlugin*>(this);
    if (!std::strcmp(key, "voice")) {
        std::lock_guard<std::mutex> lk(fEngineMutex);
        const std::string tmp = tempPath(".txt");
        char cmd[600];
        std::snprintf(cmd, sizeof cmd, "snapshot %d", kLiveSnapshot);
        ligase_engine_send_text(self->fEngine, cmd);
        std::snprintf(cmd, sizeof cmd, "morph_export %s", tmp.c_str());
        ligase_engine_send_text(self->fEngine, cmd);
        std::string text = readFile(tmp, false); std::remove(tmp.c_str());
        return String(text.c_str());
    }
    if (!std::strcmp(key, "journal")) return String(fJournal.serialize().c_str());
    if (!std::strcmp(key, "reel_path")) { std::lock_guard<std::mutex> sl(fStateMutex); return String(fReelPath.c_str()); }
    if (!std::strcmp(key, "reel")) {
        std::lock_guard<std::mutex> lk(fEngineMutex);
        ligase_status_t st; ligase_engine_status(self->fEngine, &st);
        if (st.reel_length <= 0 || st.sample_rate <= 0) return String("");
        if ((double)st.reel_length / (double)st.sample_rate > kEmbedMaxSec) return String("");
        const std::string tmp = tempPath(".wav");
        char cmd[600]; std::snprintf(cmd, sizeof cmd, "save %s", tmp.c_str());
        ligase_engine_send_text(self->fEngine, cmd);
        std::string bytes = readFile(tmp, true); std::remove(tmp.c_str());
        if (bytes.empty()) return String("");
        return String(base64Encode((const uint8_t*)bytes.data(), bytes.size()).c_str());
    }
    if (!std::strcmp(key, "panel")) { std::lock_guard<std::mutex> sl(fStateMutex); return String(fPanelState.c_str()); }
    if (!std::strcmp(key, "midimap")) return String(ccMapText().c_str());
    return String("");
}

void LigasePlugin::setState(const char* key, const char* value) {
    if (!key) return;
    const std::string v = value ? value : "";
    if (!std::strcmp(key, "cmd")) {
        if (!v.empty()) sendCommand(v.c_str(), true);
        return;
    }
    if (!std::strcmp(key, "voice")) {
        if (v.empty()) return;
        std::lock_guard<std::mutex> lk(fEngineMutex);
        const std::string tmp = tempPath(".txt");
        if (!writeFile(tmp, v.data(), v.size())) return;
        char cmd[600];
        std::snprintf(cmd, sizeof cmd, "morph_import %s", tmp.c_str());
        ligase_engine_send_text(fEngine, cmd);
        std::remove(tmp.c_str());
        std::snprintf(cmd, sizeof cmd, "snapshot_recall %d", kLiveSnapshot);
        ligase_engine_send_text(fEngine, cmd);
        return;
    }
    if (!std::strcmp(key, "journal")) {
        fJournal.clear();
        std::lock_guard<std::mutex> lk(fEngineMutex);
        size_t pos = 0;
        while (pos < v.size()) {
            size_t e = v.find('\n', pos); if (e == std::string::npos) e = v.size();
            const std::string line = v.substr(pos, e - pos);
            if (!line.empty()) { fJournal.record(line); ligase_engine_send_text(fEngine, line.c_str()); }
            pos = e + 1;
        }
        return;
    }
    if (!std::strcmp(key, "reel_path")) {
        { std::lock_guard<std::mutex> sl(fStateMutex); fReelPath = v; }
        if (v.empty()) return;
        std::lock_guard<std::mutex> lk(fEngineMutex);
        ligase_status_t st; ligase_engine_status(fEngine, &st);
        if (st.reel_length == 0) {                 /* nothing embedded (yet): fall back to the file */
            FILE* f = std::fopen(v.c_str(), "rb");
            if (f) { std::fclose(f); std::string cmd = "load " + v; ligase_engine_send_text(fEngine, cmd.c_str()); }
        }
        return;
    }
    if (!std::strcmp(key, "reel")) {
        if (v.empty()) return;
        std::vector<uint8_t> bytes = base64Decode(v);
        if (bytes.size() < 44) return;
        std::lock_guard<std::mutex> lk(fEngineMutex);
        const std::string tmp = tempPath(".wav");
        if (!writeFile(tmp, bytes.data(), bytes.size())) return;
        std::string cmd = "load " + tmp; ligase_engine_send_text(fEngine, cmd.c_str());
        std::remove(tmp.c_str());
        return;
    }
    if (!std::strcmp(key, "panel")) { std::lock_guard<std::mutex> sl(fStateMutex); fPanelState = v; return; }
    if (!std::strcmp(key, "midimap")) { setCCMap(v); return; }
}

std::string LigasePlugin::panelState() { std::lock_guard<std::mutex> sl(fStateMutex); return fPanelState; }

std::string LigasePlugin::vocabulary() {
    std::string out;
    const int n = ligase_engine_selector_count();
    for (int i = 0; i < n; i++) { out += ligase_engine_selector_name(i); out += '/'; out += ligase_engine_selector_signature(i); out += '\n'; }
    return out;
}

void LigasePlugin::setCCMap(const std::string& text) {
    std::lock_guard<std::mutex> lk(fCCMutex);
    fCCMap.clear();
    std::istringstream ss(text); std::string line;
    while (std::getline(ss, line)) {
        const size_t eq = line.find('='); if (eq == std::string::npos) continue;
        const int cc = std::atoi(line.substr(0, eq).c_str()); const std::string id = line.substr(eq + 1);
        for (uint32_t i = 0; i < LIGASE_PARAM_COUNT; i++) if (id == LIGASE_PARAMS[i].id && LIGASE_PARAMS[i].kind != LP_OUTPUT) { fCCMap[cc] = (int)i; break; }
    }
}
std::string LigasePlugin::ccMapText() const {
    std::lock_guard<std::mutex> lk(fCCMutex);
    std::string out;
    for (const auto& kv : fCCMap) { out += std::to_string(kv.first); out += '='; out += LIGASE_PARAMS[kv.second].id; out += '\n'; }
    return out;
}

/* ---- processing ----------------------------------------------------------------------------- */
void LigasePlugin::configureLatency(uint32_t bufferSize) {
    const bool aligned = bufferSize > 0 && (bufferSize % kBlock) == 0;
    fInFill = 0; fOutHead = 0; fOutCount = 0;
    fPrimed = !aligned;
    if (fPrimed) { fOutCount = kBlock; std::memset(fOutQ, 0, sizeof fOutQ); }
    fLatency = fPrimed ? (uint32_t)kBlock : 0u;
    setLatency(fLatency);
}

void LigasePlugin::activate() { configureLatency(getBufferSize()); fLastBeat = -1.0; }
void LigasePlugin::deactivate() {}
void LigasePlugin::bufferSizeChanged(uint32_t newBufferSize) { configureLatency(newBufferSize); }
void LigasePlugin::sampleRateChanged(double newSampleRate) {
    std::lock_guard<std::mutex> lk(fEngineMutex);
    ligase_engine_set_sample_rate(fEngine, (int)newSampleRate);
}

void LigasePlugin::handleMidi(const MidiEvent* events, uint32_t count) {
    char buf[128];
    for (uint32_t i = 0; i < count; i++) {
        const MidiEvent& ev = events[i];
        if (ev.size < 2) continue;
        const uint8_t status = ev.data[0] & 0xF0, ch = (ev.data[0] & 0x0F) + 1;
        if (status == 0x90 || status == 0x80) {
            const int note = ev.data[1], vel = (status == 0x80) ? 0 : ev.data[2];
            if (note < 1 || note > 127) continue;
            if (vel > 0) {
                const float amp = fVelAmp.load();
                fVelGain = 1.0f - amp * (1.0f - (float)vel / 127.0f);
                applyParameter(LP_LEVEL, fParamValue[LP_LEVEL].load());
            }
            std::snprintf(buf, sizeof buf, "midi %d %d %d", note, vel, ch);
            ligase_engine_send_text(fEngine, buf);
        } else if (status == 0xB0) {
            const int cc = ev.data[1], val = ev.data[2];
            if (cc == 120 || cc == 123) { ligase_engine_send_text(fEngine, "chord"); continue; }
            int pidx = -1;
            { std::lock_guard<std::mutex> lk(fCCMutex); auto it = fCCMap.find(cc); if (it != fCCMap.end()) pidx = it->second; }
            if (pidx < 0) continue;
            const lp_param_t& p = LIGASE_PARAMS[pidx];
            float v;
            if (p.is_log && p.lo > 0.0f) v = p.lo * std::pow(p.hi / p.lo, (float)val / 127.0f);
            else v = p.lo + (p.hi - p.lo) * ((float)val / 127.0f);
            if (p.is_bool) v = val >= 64 ? 1.0f : 0.0f;
            if (p.is_int) v = std::round(v);
            /* apply now (the engine must follow the CC even if the host ignores the request) and ask
             * the host to adopt the value so automation lanes / the UI stay in sync */
            fParamValue[pidx].store(v); applyParameter((uint32_t)pidx, v);
            requestParameterValueChange((uint32_t)pidx, v);
        } else if (status == 0xE0 && ev.size >= 3) {
            const int bend = ((ev.data[2] << 7) | ev.data[1]) - 8192;
            const float cents = (float)bend / 8192.0f * (float)fBendCents.load();
            std::snprintf(buf, sizeof buf, "pitch_fine %.4f", (double)cents);
            ligase_engine_send_text(fEngine, buf);
        } else if (status == 0xC0) {
            std::snprintf(buf, sizeof buf, "snapshot_recall %d", ev.data[1] & 63);
            ligase_engine_send_text(fEngine, buf);
        }
    }
}

void LigasePlugin::handleTransport(uint32_t frames) {
    if (!fClockHost.load()) return;
    const TimePosition& tp = getTimePosition();
    if (!tp.playing || !tp.bbt.valid || tp.bbt.beatsPerMinute <= 0.0 || tp.bbt.beatsPerBar <= 0.0f) {
        if (fWasPlaying) { ligase_engine_send_text(fEngine, "clockstop"); fWasPlaying = false; fLastBeat = -1.0; }
        return;
    }
    fWasPlaying = true;
    const double sr = getSampleRate();
    const double beat0 = (double)(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + (double)(tp.bbt.beat - 1)
                       + (tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.tick / tp.bbt.ticksPerBeat : 0.0);
    const double blockBeats = (double)frames / sr * tp.bbt.beatsPerMinute / 60.0;
    const double beat1 = beat0 + blockBeats;
    const double t0ms = ligase_engine_logical_ms(fEngine);
    const double blockMs = (double)frames / sr * 1000.0;
    double k = std::ceil(beat0 - 1e-9);
    if (fLastBeat >= 0.0 && k <= fLastBeat) k = fLastBeat + 1.0;   /* never re-fire a beat already banged */
    for (; k < beat1; k += 1.0) {
        const double frac = blockBeats > 0.0 ? (k - beat0) / blockBeats : 0.0;
        ligase_engine_bang(fEngine, t0ms + frac * blockMs);
        fLastBeat = k;
    }
}

void LigasePlugin::processInnerBlock(const float* inL, const float* inR, float* outL, float* outR) {
    ligase_engine_process(fEngine, inL, inR, outL, outR, fBlockSX, fBlockSY);
    const float g = fMaster.load();
    float pl = 0.f, pr = 0.f;
    for (int i = 0; i < kBlock; i++) {
        outL[i] *= g; outR[i] *= g;
        const float al = std::fabs(outL[i]), ar = std::fabs(outR[i]);
        if (al > pl) pl = al;
        if (ar > pr) pr = ar;
    }
    if (pl > peakL.load()) peakL.store(pl);
    if (pr > peakR.load()) peakR.store(pr);
    uint32_t w = scopeWrite.load(std::memory_order_relaxed);
    for (int i = 0; i < kBlock; i++) { scopeX[(w + (uint32_t)i) % kScopeLen] = fBlockSX[i]; scopeY[(w + (uint32_t)i) % kScopeLen] = fBlockSY[i]; }
    scopeWrite.store((w + kBlock) % kScopeLen, std::memory_order_release);
}

void LigasePlugin::updateStatusAndMeters() {
    if (++fStatusCounter < 16) return;   /* ~ every 1024 frames */
    fStatusCounter = 0;
    ligase_engine_status(fEngine, &statusSnap);
    statusSeq.fetch_add(1, std::memory_order_release);
    fOutValue[LP_VU_L].store(peakL.load()); fOutValue[LP_VU_R].store(peakR.load());
    peakL.store(peakL.load() * 0.5f); peakR.store(peakR.load() * 0.5f);
    fOutValue[LP_O_SPLICE].store((float)statusSnap.splice_current);
    fOutValue[LP_O_SPLICES].store((float)statusSnap.splice_count);
    fOutValue[LP_O_PLAYING].store(statusSnap.playing ? 1.f : 0.f);
    fOutValue[LP_O_RECORDING].store(statusSnap.recording ? 1.f : 0.f);
    fOutValue[LP_O_BPM].store(statusSnap.bpm);
    fOutValue[LP_O_REEL_SEC].store(statusSnap.sample_rate > 0 ? (float)statusSnap.reel_length / (float)statusSnap.sample_rate : 0.f);
    fOutValue[LP_O_GRAINS].store((float)statusSnap.active_grains);
    fOutValue[LP_O_VOICES].store((float)statusSnap.voice_count);
}

void LigasePlugin::run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiEvents, uint32_t midiEventCount) {
    const float* inL = inputs ? inputs[0] : nullptr;
    const float* inR = inputs ? inputs[1] : nullptr;
    float* outL = outputs[0]; float* outR = outputs[1];

    std::unique_lock<std::mutex> lk(fEngineMutex, std::try_to_lock);
    if (!lk.owns_lock() || fEngine == nullptr) {       /* a heavy op (reel load) owns the engine: silence this block */
        std::memset(outL, 0, sizeof(float) * frames); std::memset(outR, 0, sizeof(float) * frames);
        return;
    }

    drainParametersAndCommands();
    handleMidi(midiEvents, midiEventCount);
    handleTransport(frames);

    /* A host block that is not a multiple of the inner block needs one block of latency: prime the
     * output queue with 64 zeros once (the queue then always holds >= the frames owed). */
    if (!fPrimed && (frames % kBlock) != 0) {
        fOutHead = (fOutHead + 4 * kBlock - kBlock) % (4 * kBlock);
        for (int k = 0; k < kBlock; k++) { fOutQ[0][(fOutHead + k) % (4 * kBlock)] = 0.f; fOutQ[1][(fOutHead + k) % (4 * kBlock)] = 0.f; }
        fOutCount += kBlock; fPrimed = true; fLatency = kBlock; setLatency(fLatency);
    }
    /* push input into the inner-block FIFO; each completed block is processed into the output
     * queue, which is drained into the host buffer as it fills */
    uint32_t wi = 0;
    for (uint32_t i = 0; i < frames; i++) {
        fInAcc[0][fInFill] = inL ? inL[i] : 0.f;
        fInAcc[1][fInFill] = inR ? inR[i] : 0.f;
        if (++fInFill == kBlock) {
            fInFill = 0;
            processInnerBlock(fInAcc[0], fInAcc[1], fBlockOutL, fBlockOutR);
            for (int k = 0; k < kBlock; k++) {
                const int idx = (fOutHead + fOutCount + k) % (4 * kBlock);
                fOutQ[0][idx] = fBlockOutL[k]; fOutQ[1][idx] = fBlockOutR[k];
            }
            fOutCount += kBlock;
            updateStatusAndMeters();
        }
        while (wi < frames && fOutCount > 0) {
            outL[wi] = fOutQ[0][fOutHead]; outR[wi] = fOutQ[1][fOutHead];
            fOutHead = (fOutHead + 1) % (4 * kBlock); fOutCount--; wi++;
        }
    }
    for (; wi < frames; wi++) { outL[wi] = 0.f; outR[wi] = 0.f; }   /* unreachable in steady state */
}

Plugin* createPlugin() { return new LigasePlugin(); }

END_NAMESPACE_DISTRHO
