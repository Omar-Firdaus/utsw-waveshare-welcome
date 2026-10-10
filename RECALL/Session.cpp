#include "Session.h"
#include "Audio.h"
#include "Config.h"
#include "Rtc.h"
#include "Script.h"
#include "Storage.h"
#include "Ui.h"
#include <esp_timer.h>
#include <vector>

namespace {

enum class TaskStatus { Pending, InProgress, Completed, Omitted };
enum class StopReason { None, Distressed, Declined, CannotRead, ShortOfTime, Time, SessionEnded };
enum class Screen { Start, Opening, Instruction, Recording, Review, Message, Closing, Error };

struct TaskInfo {
  const char *name;
  const char *file;
  const char *text;
  bool done_enabled;
};

const TaskInfo TASKS[6] = {
    {"room_tone", "T0_roomtone", script::T0, false},
    {"sustained_vowel", "T1_vowel", script::T1, true},
    {"read_sentence", "T2_sentence", script::T2, true},
    {"picture_description", "T3_picture", script::T3, true},
    {"animal_naming", "T4_fluency", script::T4, false},
    {"personal_narrative", "T5_narrative", script::T5, true},
};

const char *const PROMPTS[6] = {nullptr, script::T1_PROMPT, script::T2_PROMPT,
                                script::T3_PROMPT, script::T4_PROMPT, script::T5_PROMPT};

struct RecordingLog {
  String file;
  uint8_t attempt = 0, restart = 0;
  uint32_t start_ms = 0, end_ms = 0;
  int32_t first_speech_ms = -1;
  bool in_progress = true, interrupted = false, verified = false;
  EndReason ended_by = EndReason::None;
  StopReason stop_reason = StopReason::None;
  String error;
  uint32_t samples = 0, bytes = 0, clipped = 0, dropped = 0;
  float peak_dbfs = -120;
  int32_t voiced_ms = -1;
  int8_t valid = -1;
  const char *validity = nullptr;
};

struct PromptLog {
  uint32_t t_ms, file_offset_ms;
  String file;
};

struct EventLog {
  int32_t t_ms;
  String type, detail;
};

struct TaskLog {
  TaskStatus status = TaskStatus::Pending;
  StopReason omit_reason = StopReason::None;
  uint8_t restarts = 0;
  bool prompt_used = false;
  uint8_t attempts = 0, valid_attempts = 0;
  std::vector<RecordingLog> recordings;
  std::vector<PromptLog> prompts;
  std::vector<EventLog> events;
};

struct Session {
  String id, unit;
  bool started = false;
  int64_t t0_us = 0;
  bool rtc_valid = false;
  DateTime created{}, t0_time{};
  float noise_rms = 0;
  const char *noise_source = "default";
  TaskLog tasks[6];
  int task = 0;
  uint8_t attempt = 1;
  uint8_t restart = 0;
  String status = "in_progress";
  StopReason end_reason = StopReason::None;
  int32_t ended_ms = -1;
};

Session s;
String hardware_error;
Screen screen = Screen::Start;
lv_obj_t *prompt_label = nullptr, *status_label = nullptr, *done_button = nullptr, *menu_button = nullptr,
         *elapsed_label = nullptr, *mic_label = nullptr;
bool prompt_shown = false, cap_stop_sent = false, stop_end_session = false;
int menu_task = -1;
Screen menu_screen = Screen::Start;
StopReason stop_reason = StopReason::None;
uint32_t message_until = 0, last_refresh = 0;
ui::Action after_message;

// ---------------------------------------------------------------- helpers

const char *status_name(TaskStatus st) {
  switch (st) {
    case TaskStatus::InProgress: return "in_progress";
    case TaskStatus::Completed: return "completed";
    case TaskStatus::Omitted: return "omitted";
    default: return "pending";
  }
}

const char *reason_name(StopReason r) {
  switch (r) {
    case StopReason::Distressed: return "participant_distressed";
    case StopReason::Declined: return "participant_declined";
    case StopReason::CannotRead: return "cannot_read_card";
    case StopReason::ShortOfTime: return "short_of_time";
    case StopReason::Time: return "time";
    case StopReason::SessionEnded: return "session_ended";
    default: return "none";
  }
}

const char *reason_label(StopReason r) {
  switch (r) {
    case StopReason::Distressed: return "Participant distressed";
    case StopReason::Declined: return "Participant declined";
    case StopReason::CannotRead: return "Cannot read the card";
    case StopReason::ShortOfTime: return "Short of time";
    default: return "";
  }
}

int32_t now_ms() { return s.started ? int32_t((esp_timer_get_time() - s.t0_us) / 1000) : -1; }
uint32_t samples_to_ms(uint32_t n) { return uint64_t(n) * 1000 / cfg::SAMPLE_RATE; }
String session_dir() { return "/RECALL/" + s.id; }
String log_path() { return session_dir() + "/" + s.id + "_log.json"; }
TaskLog &current() { return s.tasks[s.task]; }

float noise_floor_rms() {
  float floor = s.noise_rms > 0 ? s.noise_rms : dbfs_to_rms(cfg::DEFAULT_NOISE_FLOOR_DBFS);
  return max(floor, dbfs_to_rms(cfg::MIN_NOISE_FLOOR_DBFS));
}

float threshold_rms() { return noise_floor_rms() * powf(10, cfg::VAD_MARGIN_DB / 20); }

void event(TaskLog &t, const String &type, const String &detail = "") { t.events.push_back({now_ms(), type, detail}); }

// ---------------------------------------------------------------- session log

String q(const String &v) {
  String out = "\"";
  for (char c : v) {
    if (c == '"' || c == '\\') out += '\\';
    out += c;
  }
  return out + "\"";
}
String q(const char *v) { return v ? q(String(v)) : String("null"); }
String num(int32_t v) { return v < 0 ? String("null") : String(v); }
String flag(bool v) { return v ? "true" : "false"; }
String reason_or_null(StopReason r) { return r == StopReason::None ? String("null") : q(reason_name(r)); }
String wall_time(int32_t ms) { return s.rtc_valid && s.started && ms >= 0 ? q(rtc_iso(rtc_add_ms(s.t0_time, ms))) : String("null"); }

String recording_json(int task, const RecordingLog &r) {
  String j = "{\"file\":" + q(r.file);
  if (task == 1) j += ",\"attempt\":" + String(r.attempt);
  j += ",\"restart\":" + String(r.restart) + ",\"start_ms\":" + String(r.start_ms) +
       ",\"start_time\":" + wall_time(r.start_ms) + ",\"first_speech_ms\":" + num(r.first_speech_ms) +
       ",\"end_ms\":" + (r.in_progress ? String("null") : String(r.end_ms)) +
       ",\"ended_by\":" + (r.in_progress ? String("null") : q(end_reason_name(r.ended_by))) +
       ",\"stop_reason\":" + reason_or_null(r.stop_reason) + ",\"in_progress\":" + flag(r.in_progress) +
       ",\"interrupted\":" + flag(r.interrupted) + ",\"verified\":" + flag(r.verified) +
       ",\"error\":" + (r.error.length() ? q(r.error) : String("null")) + ",\"samples\":" + String(r.samples) +
       ",\"duration_ms\":" + String(samples_to_ms(r.samples)) + ",\"bytes\":" + String(r.bytes) +
       ",\"peak_dbfs\":" + String(r.peak_dbfs, 1) + ",\"clipped_samples\":" + String(r.clipped) +
       ",\"dropped_samples\":" + String(r.dropped);
  if (task == 1) {
    j += ",\"voiced_ms\":" + num(r.voiced_ms) + ",\"valid\":" + (r.valid < 0 ? String("null") : flag(r.valid)) +
         ",\"validity_source\":" + q(r.validity);
  }
  return j + "}";
}

String task_json(int i) {
  const TaskLog &t = s.tasks[i];
  String j = "{\"task\":" + String(i) + ",\"name\":" + q(TASKS[i].name) + ",\"status\":" + q(status_name(t.status)) +
             ",\"omit_reason\":" + reason_or_null(t.omit_reason) + ",\"restarts\":" + String(t.restarts);
  if (i == 1) j += ",\"attempts\":" + String(t.attempts) + ",\"valid_attempts\":" + String(t.valid_attempts);
  j += ",\n  \"prompts\":[";
  for (size_t k = 0; k < t.prompts.size(); ++k) {
    const PromptLog &p = t.prompts[k];
    j += String(k ? "," : "") + "{\"t_ms\":" + String(p.t_ms) + ",\"time\":" + wall_time(p.t_ms) +
         ",\"file\":" + q(p.file) + ",\"file_offset_ms\":" + String(p.file_offset_ms) + "}";
  }
  j += "],\n  \"events\":[";
  for (size_t k = 0; k < t.events.size(); ++k) {
    const EventLog &e = t.events[k];
    j += String(k ? "," : "") + "{\"t_ms\":" + num(e.t_ms) + ",\"type\":" + q(e.type) +
         ",\"detail\":" + (e.detail.length() ? q(e.detail) : String("null")) + "}";
  }
  j += "],\n  \"recordings\":[";
  for (size_t k = 0; k < t.recordings.size(); ++k) j += String(k ? ",\n   " : "\n   ") + recording_json(i, t.recordings[k]);
  return j + "]}";
}

String log_json() {
  String j;
  j.reserve(12000);
  j += "{\"schema\":\"recall-session-log/1\"";
  j += ",\n\"session_id\":" + q(s.id) + ",\n\"unit_id\":" + q(s.unit);
  j += ",\n\"firmware_version\":" + q(RECALL_FIRMWARE_VERSION) + ",\"firmware_build\":" + q(__DATE__ " " __TIME__) +
       ",\"bench_build\":" + flag(RECALL_BENCH) + ",\"storage\":" + q(storage_medium());
  j += ",\n\"status\":" + q(s.status) + ",\"end_reason\":" + reason_or_null(s.end_reason);
  j += ",\n\"clock\":{\"rtc_valid\":" + flag(s.rtc_valid) +
       ",\"session_created\":" + (s.rtc_valid ? q(rtc_iso(s.created)) : String("null")) +
       ",\"task0_start\":" + wall_time(0) + "}";
  j += ",\n\"times\":\"milliseconds since Start of task 0\"";
  j += ",\n\"elapsed_at_end_ms\":" + num(s.ended_ms) + ",\"session_cap_ms\":" + String(cfg::SESSION_CAP_MS) +
       ",\"cap_exceeded\":" + flag(s.ended_ms > int32_t(cfg::SESSION_CAP_MS));
  j += ",\n\"audio\":" + audio_config_json();
  j += ",\n\"speech_detection\":{\"frame_ms\":" + String(cfg::VAD_FRAME_MS) + ",\"margin_db\":" +
       String(cfg::VAD_MARGIN_DB, 1) + ",\"confirm_ms\":" + String(cfg::VAD_CONFIRM_MS) +
       ",\"hangover_ms\":" + String(cfg::VAD_HANGOVER_MS) + ",\"threshold_dbfs\":" + String(rms_to_dbfs(threshold_rms()), 1) + "}";
  j += ",\n\"noise_floor\":{\"source\":" + q(s.noise_source) + ",\"rms_dbfs\":" +
       (s.noise_rms > 0 ? String(rms_to_dbfs(s.noise_rms), 2) : String("null")) + "}";
  j += ",\n\"buffer_overruns\":" + String(audio_overrun_events()) + ",\"dropped_samples\":" +
       String(audio_dropped_samples()) + ",\"i2s_dma_overflows\":" + String(audio_dma_overflows()) +
       ",\"i2s_read_errors\":" + String(audio_read_errors());
  j += ",\n\"tasks\":[\n ";
  for (int i = 0; i < 6; ++i) j += String(i ? ",\n " : "") + task_json(i);
  return j + "\n]}\n";
}

// ---------------------------------------------------------------- screens

void show_start();
void show_opening();
void show_instruction(int task);
void advance();
void show_closing();
void open_menu(bool recording);

void show_error(const String &message, ui::Action retry) {
  screen = Screen::Error;
  Serial.println("Storage error: " + message);
  lv_obj_t *scr = ui::screen();
  ui::label(scr, "Storage error", &recall_font_28, ui::WARNING, 280, 0, -95);
  ui::label(scr, message.c_str(), &recall_font_20, ui::TEXT, 270, 0, -25);
  ui::label(scr, "Check the SD card, then tap Retry. The session will not move on until it is saved.",
            &lv_font_montserrat_16, ui::MUTED, 270, 0, 50);
  ui::button(scr, "Retry", 170, 50, 0, 128, ui::Kind::Primary, [retry] {
    String error;
    if (!storage_mount(error) || !storage_ready(error)) {
      show_error(error, retry);
      return;
    }
    retry();
  });
}

void save_log_then(ui::Action next) {
  String error;
  if (storage_write_text(log_path(), log_json(), error)) {
    next();
    return;
  }
  show_error(error, [next] { save_log_then(next); });
}

void top_row(lv_obj_t *scr, bool recording) {
  status_label = ui::label(scr, "", &lv_font_montserrat_16, recording ? ui::RECORDING : ui::MUTED, 90, -45, -142);
  if (recording) lv_label_set_text(status_label, "\xE2\x80\xA2 REC");
  elapsed_label = recording ? nullptr : status_label;
  menu_button = ui::button(scr, "Menu", 76, 32, 42, -142, ui::Kind::Secondary, [recording] { open_menu(recording); },
                           &lv_font_montserrat_16);
}

lv_obj_t *question(lv_obj_t *scr, const char *text) {
  lv_obj_t *col = ui::column(scr, 270, -6);
  ui::label(col, text, &recall_font_20, ui::TEXT, 270);
  return col;
}

void update_elapsed() {
  if (!elapsed_label || !s.started) return;
  int32_t ms = now_ms();
  char text[16];
  snprintf(text, sizeof(text), "%ld:%02ld", long(ms / 60000), long(ms / 1000 % 60));
  lv_label_set_text(elapsed_label, text);
}

void show_message(const char *text, ui::Action then) {
  screen = Screen::Message;
  lv_obj_t *scr = ui::screen();
  ui::label(scr, text, &recall_font_28, ui::TEXT, 280);
  message_until = millis() + cfg::END_MESSAGE_MS;
  after_message = then;
}

void omit_remaining(StopReason reason) {
  for (int i = s.task + 1; i < 6; ++i) {
    if (s.tasks[i].status != TaskStatus::Pending) continue;
    s.tasks[i].status = TaskStatus::Omitted;
    s.tasks[i].omit_reason = reason;
  }
}

void finish_stop(StopReason reason, bool end_session) {
  TaskLog &t = current();
  t.status = TaskStatus::Omitted;
  t.omit_reason = reason;
  event(t, end_session ? "session_ended" : "task_skipped", reason_name(reason));
  if (end_session) {
    omit_remaining(StopReason::SessionEnded);
    s.status = "ended_early";
    s.end_reason = reason;
    show_closing();
    return;
  }
  save_log_then(advance);
}

void apply_stop(StopReason reason, bool end_session) {
  if (s.task != menu_task || menu_screen != screen) return;  // the task ended while the menu was open
  if (screen == Screen::Recording) {
    stop_reason = reason;
    stop_end_session = end_session;
    audio_request_stop(EndReason::Stop);
    ui::set_enabled(done_button, false);
    ui::set_enabled(menu_button, false);
    lv_label_set_text(status_label, "Saving");
    ui::label(ui::overlay(), "Saving...", &recall_font_20, ui::TEXT, 260);
    return;
  }
  finish_stop(reason, end_session);
}

void choose_stop_action(StopReason reason, bool opening) {
  lv_obj_t *panel = ui::overlay();
  ui::label(panel, reason_label(reason), &recall_font_20, ui::TEXT, 260, 0, -105);
  if (!opening) {
    ui::button(panel, "Skip this task", 230, 46, 0, -40, ui::Kind::Primary, [reason] { apply_stop(reason, false); });
  }
  ui::button(panel, "End session", 230, 46, 0, opening ? -40 : 18, ui::Kind::Danger, [reason] { apply_stop(reason, true); });
  ui::button(panel, "Back", 140, 42, 0, 118, ui::Kind::Secondary, [panel] { lv_obj_del(panel); });
}

void choose_stop_reason(bool opening) {
  std::vector<StopReason> reasons = {StopReason::Distressed, StopReason::Declined};
  if (!opening && s.task == 2) reasons.push_back(StopReason::CannotRead);
  // Short of time drops task 5 first, then task 2; tasks 3 and 4 are never dropped for time.
  if (!opening && (s.task == 5 || s.task == 2)) reasons.push_back(StopReason::ShortOfTime);
  lv_obj_t *panel = ui::overlay();
  ui::label(panel, "Stop: choose a reason", &lv_font_montserrat_16, ui::MUTED, 240, 0, -128);
  int y = -82;
  for (StopReason r : reasons) {
    ui::button(panel, reason_label(r), 250, 42, 0, y, ui::Kind::Primary, [panel, r, opening] {
      lv_obj_del(panel);
      choose_stop_action(r, opening);
    }, &lv_font_montserrat_16);
    y += 50;
  }
  ui::button(panel, "Cancel", 140, 42, 0, 128, ui::Kind::Secondary, [panel] { lv_obj_del(panel); });
}

void confirm_restart() {
  lv_obj_t *panel = ui::overlay();
  ui::label(panel, "Restart this task?", &recall_font_20, ui::TEXT, 260, 0, -90);
  ui::label(panel, "The current recording is kept and marked interrupted.", &lv_font_montserrat_16, ui::MUTED, 250, 0, -42);
  ui::button(panel, "Restart", 200, 46, 0, 30, ui::Kind::Danger, [panel] {
    lv_obj_del(panel);
    if (screen != Screen::Recording) return;
    audio_request_stop(EndReason::Restart);
    ui::set_enabled(done_button, false);
    ui::set_enabled(menu_button, false);
    lv_label_set_text(status_label, "Saving");
  });
  ui::button(panel, "Cancel", 140, 42, 0, 110, ui::Kind::Secondary, [panel] { lv_obj_del(panel); });
}

void open_menu(bool recording) {
  menu_task = s.task;
  menu_screen = screen;
  bool opening = screen == Screen::Opening;
  if (!recording) {
    choose_stop_reason(opening);
    return;
  }
  lv_obj_t *panel = ui::overlay();
  ui::label(panel, "Examiner", &lv_font_montserrat_16, ui::MUTED, 200, 0, -110);
  ui::button(panel, "Restart task", 230, 46, 0, -55, ui::Kind::Primary, [panel] {
    lv_obj_del(panel);
    confirm_restart();
  });
  ui::button(panel, "Stop...", 230, 46, 0, 5, ui::Kind::Danger, [panel] {
    lv_obj_del(panel);
    choose_stop_reason(false);
  });
  ui::button(panel, "Cancel", 140, 42, 0, 100, ui::Kind::Secondary, [panel] { lv_obj_del(panel); });
}

RecordingRules rules_for(int task, const TaskLog &t) {
  RecordingRules r;
  uint32_t prompt = t.prompt_used ? 0 : cfg::ms(cfg::PROMPT_SILENCE_MS);
  switch (task) {
    case 0:
      r.limit = cfg::ms(cfg::T0_LENGTH_MS);
      break;
    case 1:
      r.limit = cfg::ms(cfg::T1_MAX_ATTEMPT_MS);
      r.limit_reason = EndReason::MaxLength;
      r.end_voice_stopped = cfg::ms(cfg::T1_VOICE_STOP_MS);
      r.prompt_silence = prompt;
      r.end_silence_after_prompt = cfg::ms(cfg::END_SILENCE_AFTER_PROMPT_MS);
      r.prompt_already_used = t.prompt_used;
      break;
    case 2:
      r.prompt_silence = prompt;
      r.end_silence_after_prompt = cfg::ms(cfg::END_SILENCE_AFTER_PROMPT_MS);
      r.prompt_already_used = t.prompt_used;
      break;
    case 3:
      r.limit_after_speech = cfg::ms(cfg::T3_AFTER_FIRST_SPEECH_MS);
      r.limit_after_prompt = cfg::ms(cfg::T3_AFTER_PROMPT_MS);
      r.prompt_silence = prompt;
      break;
    case 4:
      r.limit = cfg::ms(cfg::T4_LENGTH_MS);
      r.prompt_silence = prompt;
      break;
    case 5:
      r.limit = cfg::ms(cfg::T5_LENGTH_MS);
      r.prompt_silence = prompt;
      r.end_silence_after_prompt = cfg::ms(cfg::END_SILENCE_AFTER_PROMPT_MS);
      r.prompt_already_used = t.prompt_used;
      break;
  }
  return r;
}

const char *screen_text(int task) { return task == 1 && s.attempt > 1 ? script::T1_AGAIN : TASKS[task].text; }

void show_recording() {
  screen = Screen::Recording;
  prompt_shown = false;
  cap_stop_sent = false;
  lv_obj_t *scr = ui::screen();
  top_row(scr, true);
  lv_obj_t *col = question(scr, screen_text(s.task));
  if (s.task == 4) ui::label(col, script::T4_BEGIN, &recall_font_28, ui::TEXT, 270);
  prompt_label = ui::label(col, PROMPTS[s.task] ? PROMPTS[s.task] : "", &recall_font_20, ui::PROMPT, 270);
  lv_obj_add_flag(prompt_label, LV_OBJ_FLAG_HIDDEN);
  done_button = ui::button(scr, "Done", 150, 46, 0, 134, ui::Kind::Primary, [] {
    if (screen != Screen::Recording || !TASKS[s.task].done_enabled) return;
    audio_request_stop(EndReason::Done);
    ui::set_enabled(done_button, false);
    ui::set_enabled(menu_button, false);
    lv_label_set_text(status_label, "Saving");
  });
  ui::set_enabled(done_button, TASKS[s.task].done_enabled);
}

void begin_capture(const String &path) {
  String error;
  if (!audio_start(path.c_str(), rules_for(s.task, current()), threshold_rms(), error)) {
    RecordingLog &rec = current().recordings.back();
    rec.in_progress = false;
    rec.interrupted = true;
    rec.error = error;
    s.restart++;
    event(current(), "storage_error", error);
    show_error(error, [] { show_instruction(s.task); });
    return;
  }
  Serial.printf("Recording %s\n", path.c_str());
  show_recording();
}

void start_recording() {
  String error;
  if (!storage_ready(error)) {
    show_error(error, [] { show_instruction(s.task); });
    return;
  }
  if (!s.started) {
    s.started = true;
    s.t0_us = esp_timer_get_time();
    bool valid = false;
    if (rtc_read(s.t0_time, valid)) s.rtc_valid = valid;
  }
  TaskLog &t = current();
  String name = s.id + "_" + TASKS[s.task].file;
  if (s.task == 1) name += "_a" + String(s.attempt);
  if (s.restart) name += "_r" + String(s.restart);
  String file = name + ".wav";
  for (int n = 1; storage_exists(session_dir() + "/" + file); ++n) file = name + "_dup" + String(n) + ".wav";

  RecordingLog rec;
  rec.file = file;
  rec.attempt = s.task == 1 ? s.attempt : 0;
  rec.restart = s.restart;
  rec.start_ms = max<int32_t>(now_ms(), 0);
  t.recordings.push_back(rec);
  t.status = TaskStatus::InProgress;
  String path = session_dir() + "/" + file;
  save_log_then([path] { begin_capture(path); });
}

void show_instruction(int task) {
  screen = Screen::Instruction;
  s.task = task;
  lv_obj_t *scr = ui::screen();
  top_row(scr, false);
  update_elapsed();
  question(scr, screen_text(task));
  ui::button(scr, "Start", 150, 46, 0, 134, ui::Kind::Primary, start_recording);
}

void log_prompt(int32_t prompt_at) {
  TaskLog &t = current();
  if (t.recordings.empty()) return;
  t.prompt_used = true;
  uint32_t start = s.started ? uint32_t((audio_recording_start_us() - s.t0_us) / 1000) : 0;
  uint32_t offset = samples_to_ms(prompt_at);
  t.prompts.push_back({start + offset, offset, t.recordings.back().file});
}

void complete_task(const char *message = nullptr) {
  current().status = TaskStatus::Completed;
  save_log_then([message] {
    if (message) show_message(message, advance);
    else advance();
  });
}

void decide_attempt(bool valid, bool no_response) {
  TaskLog &t = current();
  RecordingLog &rec = t.recordings.back();
  if (rec.valid < 0) {
    rec.valid = valid;
    rec.validity = "examiner";
  }
  if (rec.valid == 1) t.valid_attempts++;
  if (no_response) event(t, "no_response");
  if (t.valid_attempts >= cfg::T1_VALID_NEEDED || t.attempts >= cfg::T1_MAX_ATTEMPTS || no_response) {
    complete_task();
    return;
  }
  s.attempt++;
  s.restart = 0;
  save_log_then([] { show_instruction(1); });
}

void show_review(const RecordingLog &rec, bool no_response) {
  screen = Screen::Review;
  lv_obj_t *scr = ui::screen();
  char title[40], detail[48];
  snprintf(title, sizeof(title), "Attempt %u of %u", rec.attempt, cfg::T1_MAX_ATTEMPTS);
  snprintf(detail, sizeof(detail), "Voice %.1f s", rec.voiced_ms / 1000.0f);
  ui::label(scr, "Examiner", &lv_font_montserrat_16, ui::MUTED, 200, 0, -135);
  ui::label(scr, title, &recall_font_28, ui::TEXT, 280, 0, -95);
  ui::label(scr, detail, &recall_font_20, ui::TEXT, 280, 0, -55);
  if (rec.valid == 0) {
    ui::label(scr, no_response ? "No voice detected: marked invalid" : "Under 3 s: marked invalid",
              &recall_font_20, ui::WARNING, 270, 0, -5);
    ui::button(scr, "Continue", 170, 50, 0, 110, ui::Kind::Primary, [no_response] { decide_attempt(false, no_response); });
    return;
  }
  ui::label(scr, "Steady, with no voice break or cough?", &lv_font_montserrat_16, ui::MUTED, 260, 0, 0);
  ui::button(scr, "Valid", 125, 50, -68, 70, ui::Kind::Good, [] { decide_attempt(true, false); });
  ui::button(scr, "Invalid", 125, 50, 68, 70, ui::Kind::Danger, [] { decide_attempt(false, false); });
}

void on_finished(const AudioResult &res) {
  TaskLog &t = current();
  RecordingLog &rec = t.recordings.back();
  rec.in_progress = false;
  if (s.started) rec.start_ms = uint32_t((audio_recording_start_us() - s.t0_us) / 1000);
  rec.samples = res.samples;
  rec.bytes = res.bytes;
  rec.verified = res.ok;
  rec.error = res.error;
  rec.ended_by = res.ended_by;
  rec.end_ms = rec.start_ms + samples_to_ms(res.samples);
  rec.first_speech_ms = res.first_speech >= 0 ? int32_t(rec.start_ms + samples_to_ms(res.first_speech)) : -1;
  rec.peak_dbfs = rms_to_dbfs(res.peak);
  rec.clipped = res.clipped;
  rec.dropped = res.dropped;
  if (res.prompt_at >= 0 && !prompt_shown) log_prompt(res.prompt_at);
  Serial.printf("Saved %s: %lu samples, %s, ended by %s\n", rec.file.c_str(), (unsigned long)res.samples,
                res.ok ? "verified" : res.error.c_str(), end_reason_name(res.ended_by));

  if (!res.ok) {
    rec.interrupted = true;
    s.restart++;
    event(t, "storage_error", res.error);
    show_error(res.error, [] { save_log_then([] { show_instruction(s.task); }); });
    return;
  }
  if (res.ended_by == EndReason::Restart) {
    rec.interrupted = true;
    t.restarts++;
    s.restart++;
    if (s.task != 1) t.prompt_used = false;  // a restarted task is administered afresh
    event(t, "restart", rec.file);
    save_log_then([] { show_instruction(s.task); });
    return;
  }
  if (res.ended_by == EndReason::Stop) {
    rec.stop_reason = stop_reason;
    finish_stop(stop_reason, stop_end_session);
    return;
  }

  switch (s.task) {
    case 0:
      s.noise_rms = max(res.mean_rms, dbfs_to_rms(cfg::MIN_NOISE_FLOOR_DBFS));
      s.noise_source = "task0";
      Serial.printf("Noise floor %.1f dBFS\n", rms_to_dbfs(s.noise_rms));
      complete_task();
      break;
    case 1: {
      t.attempts++;
      bool spoke = res.first_speech >= 0 && res.last_speech_end >= res.first_speech;
      rec.voiced_ms = spoke ? samples_to_ms(res.last_speech_end - res.first_speech) : 0;
      bool no_response = !spoke && res.ended_by == EndReason::SilenceAfterPrompt;
      if (rec.voiced_ms < int32_t(cfg::T1_MIN_VALID_MS)) {
        rec.valid = 0;
        rec.validity = no_response ? "auto_no_voice" : "auto_under_3s";
      }
      save_log_then([no_response] { show_review(current().recordings.back(), no_response); });
      break;
    }
    case 3: complete_task(script::T3_END); break;
    case 4: complete_task(script::T4_END); break;
    default: complete_task(); break;
  }
}

void advance() {
  int next = s.task + 1;
  s.attempt = 1;
  s.restart = 0;
  if (next == 5 && s.started && now_ms() > int32_t(cfg::T5_AUTO_SKIP_AFTER_MS)) {
    TaskLog &t5 = s.tasks[5];
    t5.status = TaskStatus::Omitted;
    t5.omit_reason = StopReason::Time;
    t5.events.push_back({now_ms(), "auto_skipped", "more than 5:00 elapsed"});
    next = 6;
  }
  if (next >= 6) {
    show_closing();
    return;
  }
  show_instruction(next);
}

void new_session() {
  s = Session{};
  s.unit = storage_unit_id();
  show_start();
}

void show_closing() {
  s.ended_ms = now_ms();
  if (s.status == "in_progress") s.status = "complete";
  save_log_then([] {
    screen = Screen::Closing;
    Serial.println("Session " + s.id + " finished: " + s.status);
    lv_obj_t *scr = ui::screen();
    ui::label(scr, script::CLOSING, &recall_font_28, ui::TEXT, 280, 0, -15);
    ui::button(scr, "New session", 150, 40, 0, 135, ui::Kind::Secondary, new_session, &lv_font_montserrat_16);
  });
}

void show_opening() {
  screen = Screen::Opening;
  s.task = 0;
  lv_obj_t *scr = ui::screen();
  top_row(scr, false);
  question(scr, script::OPENING);
  ui::button(scr, "Start", 150, 46, 0, 134, ui::Kind::Primary, [] { show_instruction(0); });
}

void begin_session() {
  String error;
  if (!storage_ready(error)) {
    show_error(error, show_start);
    return;
  }
  if (storage_free_bytes() < storage_min_free()) {
    show_start();
    return;
  }
  if (!storage_reserve_session(s.id, error)) {
    show_error(error, show_start);
    return;
  }
  rtc_read(s.created, s.rtc_valid);
  Serial.println("Session " + s.id + " started");
  save_log_then(show_opening);
}

void show_start() {
  screen = Screen::Start;
  s.unit = storage_unit_id();
  lv_obj_t *scr = ui::screen();
  if (RECALL_BENCH) ui::label(scr, "BENCH BUILD - NOT FOR STUDY USE", &lv_font_montserrat_14, ui::WARNING, 220, 0, -150);
  ui::label(scr, "RECALL", &recall_font_28, ui::TEXT, 280, 0, -122);

  String error, sd;
  bool ready = hardware_error.isEmpty();
  bool card = storage_ready(error);
  if (card) {
    uint64_t free = storage_free_bytes();
    const char *medium = RECALL_BENCH ? "Flash" : "SD card";
    char text[64];
    if (free >= 10ull * 1024 * 1024 * 1024) snprintf(text, sizeof(text), "%s: %.0f GB free", medium, free / 1073741824.0);
    else snprintf(text, sizeof(text), "%s: %.1f MB free", medium, free / 1048576.0);
    sd = text;
    if (free < storage_min_free()) {
      snprintf(text, sizeof(text), " (need %.0f MB)", storage_min_free() / 1048576.0);
      sd += text;
      ready = false;
    }
  } else {
    sd = "SD card: " + error;
    ready = false;
  }
  DateTime now{};
  bool valid = false;
  String clock = rtc_read(now, valid) && valid ? "Clock " + rtc_iso(now).substring(0, 16) : String("Clock not set");
  clock.replace("T", " ");
  String info = "Unit " + s.unit + "\nSession " + (card ? storage_next_session_id() : String("-")) +
                "\n" + sd + "\n" + clock + "\nFirmware " + RECALL_FIRMWARE_VERSION;
  ui::label(scr, info.c_str(), &lv_font_montserrat_16, ui::TEXT, 290, 0, -28);
  mic_label = ui::label(scr, "", &lv_font_montserrat_16, ui::MUTED, 260, 0, 62);
  if (!hardware_error.isEmpty()) {
    lv_obj_set_style_text_color(mic_label, lv_color_hex(ui::WARNING), 0);
    lv_label_set_text(mic_label, hardware_error.c_str());
    mic_label = nullptr;
  }
  if (ready) {
    ui::button(scr, "Start", 150, 46, 0, 128, ui::Kind::Primary, begin_session);
  } else {
    ui::button(scr, "Retry", 150, 46, 0, 128, ui::Kind::Secondary, [] {
      String e;
      storage_mount(e);
      show_start();
    });
  }
}

}  // namespace

void session_begin(const String &error) {
  hardware_error = error;
  new_session();
}

bool session_idle() { return screen == Screen::Start; }

void session_refresh() {
  if (screen == Screen::Start) show_start();
}

void session_loop() {
  ui::run_deferred();
  uint32_t now = millis();
  switch (screen) {
    case Screen::Start:
      if (mic_label && now - last_refresh > 250) {
        char text[32];
        snprintf(text, sizeof(text), "Mic level %.0f dBFS", audio_level_dbfs());
        lv_label_set_text(mic_label, text);
        last_refresh = now;
      }
      break;
    case Screen::Instruction:
    case Screen::Opening:
      if (now - last_refresh > 500) {
        update_elapsed();
        last_refresh = now;
      }
      break;
    case Screen::Recording: {
      AudioStatus st = audio_status();
      if (st.prompt_at >= 0 && !prompt_shown && PROMPTS[s.task]) {
        prompt_shown = true;
        lv_obj_clear_flag(prompt_label, LV_OBJ_FLAG_HIDDEN);
        log_prompt(st.prompt_at);
      }
      if (!cap_stop_sent && (s.task == 2 || s.task == 5) && now_ms() >= int32_t(cfg::SESSION_CAP_MS)) {
        cap_stop_sent = true;
        audio_request_stop(EndReason::SessionCap);
      }
      AudioResult res;
      if (audio_take_result(res)) on_finished(res);
      break;
    }
    case Screen::Message:
      if (int32_t(now - message_until) >= 0) {
        ui::Action next = after_message;
        after_message = nullptr;
        if (next) next();
      }
      break;
    default:
      break;
  }
}
