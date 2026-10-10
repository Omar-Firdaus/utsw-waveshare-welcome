# Project RECALL device firmware

Speech capture firmware for the RECALL protocol (Speech Capture Addendum to
STU-2021-0834 v0.3, section 5), built from the "Device Firmware Build Spec" of
Oct 9, 2026.

The device runs one session per participant: six tasks (0 to 5) in a fixed
order. Each task's instruction is shown on screen and stays visible while the
participant answers. Each answer is written to its own WAV file on the microSD
card, which is closed and checked before the next question appears. Nothing is
played aloud.

## Hardware

Waveshare **ESP32-S3-Touch-LCD-1.85C V2**:

| Part | Detail |
| --- | --- |
| Display / input | 360x360 round ST77916 LCD, CST816 touch |
| Microphone | ES7210 codec on I2S0 (MCLK 2, BCLK 48, WS 38, DIN 39), I2C 0x40 |
| Storage | microSD, SDMMC 1-bit (CLK 14, CMD 17, D0 16) |
| Clock | PCF85063 real-time clock, I2C 0x51 |

The speaker amplifier (GPIO 15) is held off. The ES8311 output codec is not used.

## Build and flash

Arduino IDE or `arduino-cli`, esp32 core **3.1.1**, Waveshare's bundled LVGL 8.3.10.

- Board: ESP32S3 Dev Module
- Flash size 16 MB, PSRAM OPI
- Partition: 16M Flash (3MB APP/9.9MB FATFS)
- USB CDC On Boot: Enabled

```sh
scripts/build-recall.sh            # compile
scripts/build-recall.sh --upload   # compile and flash /dev/cu.usbmodem*
```

## Serial commands (115200 baud)

| Command | Effect |
| --- | --- |
| `status` | Firmware, unit, next session ID, clock, SD space, mic level |
| `time YYYY-MM-DD HH:MM:SS` | Set the real-time clock (local time) |
| `unit N` | Set the unit number (RECALL-0N); only on the session start screen |

## Audio

44.1 kHz, 16-bit, mono, uncompressed PCM WAV. Fixed gain: 30 dB analog, 0 dB
digital, codec automatic level control off, no noise suppression. One codec
channel is recorded unchanged (no mixing). The channel is chosen at boot (left
unless it is much quieter than right) and logged. Gain, channel and a codec
register dump are written into every session log.

Capture runs on its own task. A second task streams an 8 s PSRAM ring buffer to
the card, so the screen and SD writes never block capture. Buffer overruns,
dropped samples and I2S DMA overflows are counted and logged.

All task timing uses the microphone's sample clock. Fixed-length recordings
end on an exact sample count: task 0 is 441,000 samples, and tasks 4 and 5 are
2,646,000. Session times in the log are milliseconds since Start of task 0
(ESP32 timer), plus wall-clock time from the real-time clock when it has been
set.

## Files

```
/RECALL/<session>/<session>_T0_roomtone.wav
                  <session>_T1_vowel_a1.wav, _a2, _a3
                  <session>_T2_sentence.wav
                  <session>_T3_picture.wav
                  <session>_T4_fluency.wav
                  <session>_T5_narrative.wav
                  <session>_log.json
```

The session ID is `RECALL-<unit>-<counter>`, for example `RECALL-01-0007`. The
counter is stored in flash before the folder is created, and existing folders
are skipped, so IDs never repeat. A restarted task gets `_r1`, `_r2`. Nothing
is overwritten or deleted.

Write safety: the WAV header is written when the file opens. Its size fields
are updated and the file is flushed every 2 s. On close the header is
finalised, and the file is reopened to check that its size matches the samples
captured. The log is rewritten after every task (and before each recording) via
a `.tmp` file. If power is lost during that swap, the `.tmp` file is the log.
Every file operation checks the card is still present. A failed write, a
missing card or a full card shows an error screen, and the session does not
advance until Retry succeeds.

## Controls

The examiner holds the device.

- **Start** (bottom) on instruction screens begins recording.
- **Done** (bottom) ends the answer early in tasks 1, 2, 3 and 5. It is shown
  disabled in tasks 0 and 4.
- **Menu** (top right) offers **Restart task** while recording, and **Stop**
  with a reason. Stop then gives **Skip this task** or **End session**.
  - Distressed and declined are offered everywhere.
  - "Cannot read the card" is offered in task 2 only.
  - "Short of time" is offered in tasks 5 and 2 only.

The session start screen shows the unit, the next session ID, SD free space,
the clock, the firmware version and the live mic level. Instruction screens
show the session elapsed time (m:ss) at the top left for the examiner.
Recording screens show REC there instead. There is no countdown.

## Decisions taken where the spec left a choice

Defaults from the spec's Open decisions section are implemented as written.

- **Examiner presses Start/Done.**
- **Task 1:** the examiner demonstrates and confirms Valid/Invalid. Attempts
  with under 3 s of voice are marked invalid automatically.
- **Cards and picture stay physical.**
- **Task 3** ends at whichever comes first: 75 s after first speech, or 20 s
  after the prompt.
- **Task 5** runs 60 s from Start.
- **15 s more silence after the prompt ends the task** (tasks 1, 2 and 5).
- **No encryption on the card;** a handling rule is needed.
- **Subject ID** is linked on the paper log form.

Choices made in this build that the spec does not cover:

- **Speech detection.** 20 ms frames. Speech starts after 200 ms above the task
  0 noise floor + 10 dB, with a 300 ms hangover. First speech is logged at the
  start of that 200 ms run. Detection drives timing only and never edits audio.
  If task 0 was skipped, a -60 dBFS default floor is used and logged.
- **Task 1 attempt end:** 1 s of silence after the voice, or Done, with a 60 s
  safety cap. A task 1 attempt that times out after the prompt with no voice at
  all ends the task as "no response".
- **Restart.** For tasks 0 and 2 to 5, a restart re-administers the task, so its
  one prompt becomes available again. In task 1, a restart repeats only the
  current attempt, and the once-per-task prompt is not reset.
- **Session cap.** If 5:00 has passed when task 5 is due, task 5 is skipped
  with reason `time`. At 6:00, a running task 2 or 5 is ended
  (`session_cap`). Tasks 3 and 4 always run.
- **End messages.** "Thank you, that's enough for that one." (task 3) and
  "Thank you, you can stop there." (task 4) show for 2.5 s after the file is
  saved.
- **Codec high-pass filter.** The ES7210 driver's default DC-blocking high-pass
  filter is left on. This is filtering, and the spec recommends none; confirm
  whether to switch it off. The register values are in the log.
- **Screen font** covers ASCII only, so apostrophes are straight quotes.
  The wording is otherwise unchanged.

## Session log

`<session>_log.json` (schema `recall-session-log/1`) contains:

- session ID, unit, firmware version and build time, and clock validity
- audio configuration, speech detection settings, noise floor
- buffer overrun, dropped-sample and DMA overflow counts
- per task: status (completed / omitted / in progress), omit reason, restarts,
  task 1 attempt and valid counts, prompts with timestamps, events, and every
  recording

Each recording entry has its file, start and end times, first speech time, how
it ended, stop reason, interrupted flag, verified flag, sample count, duration,
bytes, peak level and clipped samples. Task 1 entries also have voiced time and
validity.
