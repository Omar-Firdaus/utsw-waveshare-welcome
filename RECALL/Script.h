#pragma once

// Addendum to STU-2021-0834 v0.3, section 5. Word for word; do not reword.
// The display font covers ASCII only, so apostrophes are straight quotes.

namespace script {

constexpr const char *OPENING =
    "I'm going to ask you to do a few short speaking tasks. There are no right or wrong "
    "answers. The whole thing takes about five minutes. You can stop at any time. Do you "
    "have any questions before we start?";

constexpr const char *CLOSING = "That's everything. Thank you.";

constexpr const char *T0 =
    "First I need about ten seconds of quiet to check the microphone. Please just sit "
    "comfortably without speaking.";

constexpr const char *T1 =
    "Take a deep breath, and say 'ahh' for as long as you comfortably can, in a steady "
    "voice. Ready? Go ahead.";
constexpr const char *T1_AGAIN = "Let's try that once more.";
constexpr const char *T1_PROMPT = "Whenever you're ready, just say 'ahh' and hold it.";

constexpr const char *T2 = "Please read this sentence out loud, at your normal speaking pace.";
constexpr const char *T2_PROMPT = "Please continue from where you left off.";

constexpr const char *T3 =
    "Tell me everything you see going on in this picture. Take as much time as you need.";
constexpr const char *T3_PROMPT = "Is there anything else you see?";
constexpr const char *T3_END = "Thank you, that's enough for that one.";

constexpr const char *T4 =
    "Now I'd like you to name as many animals as you can think of. Any animals at all. "
    "You'll have sixty seconds. Ready?";
constexpr const char *T4_BEGIN = "Begin";
constexpr const char *T4_PROMPT = "Keep going, any animals you can think of.";
constexpr const char *T4_END = "Thank you, you can stop there.";

constexpr const char *T5 =
    "Last one. Tell me about something you enjoyed recently. It could be anything at all, "
    "a meal, a visit, something you watched. Just describe it in your own words.";
constexpr const char *T5_PROMPT = "What else do you remember about it?";

}  // namespace script
