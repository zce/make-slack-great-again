// The inline audio player in the message list (the audio file card, its
// play / pause and Slack transcript dialogs).
//
// An audio file (File::isAudio) shows a card with playback controls,
// a seek bar and optional Slack-provided transcript. Files download into
// the audio cache through the normal background file fetch.
#pragma once

#include "app/screens/messages/context_fwd.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace screens {

class MessageList;

inline constexpr float kAudioCardH       = 88;
inline constexpr float kAudioTranscriptH = 26; // the transcript line under the card
inline constexpr float kAudioCardMaxW    = 380;

// The card for `f` (its message `ts` in `list`; ts 0 or no list: a quoted
// file, no file bar).
ui::View *
addAudioCard(ui::View *parent, Context &ctx, MessageList *list, Ts ts, const model::File &f);

// Single-line layouts the audio cards have shaped so far (tests: playback
// ticks reshape nothing but a changed time label).
size_t audioCardLayoutBuilds();

// Play or pause `f`: downloads it first when needed.
void toggleAudio(Context &ctx, const model::File &f);
// "View transcript": Slack's WebVTT cues (or the preview text).
void openTranscript(Context &ctx, const model::File &f, const model::Message *m);

// One WebVTT cue: start time and its text (speaker dashes/tags stripped).
struct VttCue {
    int64_t     startMs = 0;
    std::string text;
    bool        operator==(const VttCue &o) const { return startMs == o.startMs && text == o.text; }
};
std::vector<VttCue> parseVtt(std::string_view vtt);
// "0:05", "12:34", "1:02:03". Position labels floor, duration labels round —
// a 4.98 s clip is "0:05" long but is at "0:04" while it plays.
std::string         formatDuration(int64_t ms, bool round = false);

} // namespace screens
