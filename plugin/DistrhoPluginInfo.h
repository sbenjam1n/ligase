/* DistrhoPluginInfo.h — ligase~ as a DPF plugin (VST3 / VST2 / CLAP / LV2 / AU / JACK). */
#ifndef DISTRHO_PLUGIN_INFO_H_INCLUDED
#define DISTRHO_PLUGIN_INFO_H_INCLUDED

#define DISTRHO_PLUGIN_BRAND   "sbenjam1n"
#define DISTRHO_PLUGIN_NAME    "ligase"
#define DISTRHO_PLUGIN_URI     "https://github.com/sbenjam1n/ligase"
#define DISTRHO_PLUGIN_CLAP_ID "com.github.sbenjam1n.ligase"

#define DISTRHO_PLUGIN_BRAND_ID  Sbnj
#define DISTRHO_PLUGIN_UNIQUE_ID Lgas

#define DISTRHO_PLUGIN_HAS_UI            1
#define DISTRHO_PLUGIN_IS_RT_SAFE        1
#define DISTRHO_PLUGIN_IS_SYNTH          1   /* MIDI in + audio in: an instrument that also processes its input */
#define DISTRHO_PLUGIN_NUM_INPUTS        2
#define DISTRHO_PLUGIN_NUM_OUTPUTS       2
#define DISTRHO_PLUGIN_WANT_MIDI_INPUT   1
#define DISTRHO_PLUGIN_WANT_MIDI_OUTPUT  0
#define DISTRHO_PLUGIN_WANT_STATE        1
#define DISTRHO_PLUGIN_WANT_FULL_STATE   1
#define DISTRHO_PLUGIN_WANT_TIMEPOS      1
#define DISTRHO_PLUGIN_WANT_LATENCY      1
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS 1
#define DISTRHO_PLUGIN_WANT_PARAMETER_VALUE_CHANGE_REQUEST 1

#define DISTRHO_UI_USE_WEB_VIEW          1
#define DISTRHO_UI_USER_RESIZABLE        1
#define DISTRHO_UI_FILE_BROWSER          0
#define DISTRHO_UI_DEFAULT_WIDTH         1228
#define DISTRHO_UI_DEFAULT_HEIGHT        560

#define DISTRHO_PLUGIN_LV2_CATEGORY      "lv2:InstrumentPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES   "Instrument|Sampler|Fx"
#define DISTRHO_PLUGIN_CLAP_FEATURES     "instrument", "sampler", "granular", "audio-effect", "stereo"
#define DISTRHO_PLUGIN_AU_TYPE           aumu

#endif
