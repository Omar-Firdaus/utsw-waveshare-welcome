# UT Southwestern spoken welcome — Waveshare 1.85C V2

Open UTSW_Splash.ino with every file in this folder in Arduino IDE.

## Flow
1. Blue screen, stationary official white logo, wide Start button.
2. Tap Start: a circular voice icon with seven bars appears and plays the
   welcome message once. Bar heights respond to the actual outgoing speech
   amplitude. The microphone is not enabled and nothing is recorded here.
3. When the audio has completely drained to the speaker, a second Start button
   appears with “When you're ready.” It is hidden throughout speech playback.
4. Tap the second Start: blue screensaver, stationary logo, three softly pulsing
   dots. No experiment, data capture, or network activity begins.

## Audio
WelcomeAudio.cpp embeds the 16kHz signed 16-bit mono speech in flash.
The source script and listenable audio are ../welcome-script.txt and
../welcome.wav. Speech was synthesized locally with Piper's en_US-lessac-medium
voice through sherpa-onnx; the board only plays the embedded audio.
No cloud TTS, speech recognition, API key, Wi-Fi, or SD card is used.
Voice-generator source: https://k2-fsa.github.io/sherpa/onnx/tts/all/English/vits-piper-en_US-lessac-medium.html
The spoken welcome uses the user's supplied wording. No participant speech or
other participant data is captured by this welcome/screensaver flow.

## Build settings
- ESP32S3 Dev Module; esp32 by Espressif Systems 3.1.1
- Flash Size: 16MB (128Mb)
- PSRAM: OPI PSRAM
- Partition: 16M Flash (3MB APP/9.9MB FATFS), not ESP SR
- USB CDC On Boot: Enabled
- USB Mode: Hardware CDC and JTAG
- Waveshare bundled LVGL 8.3.10, already installed on this Mac

## V2 hardware
Audio drivers are ES8311 and ES7210 from the official Waveshare V2 code.
Shared I2S0: MCLK 2, BCLK 48, WS 38, speaker DOUT 47, microphone DIN 39.
GPIO15 enables the speaker amplifier. I2C devices are 0x18 and 0x40.
The previously verified microphone/recorder implementation is retained but has
no active UI controls in this flow. No microphone capture is requested.
ES7210 digital gain is 0 dB (the vendor example's 40 is outside -95..32 dB).
Codec initialization happens before touch polling; audio runs on a separate
task and the Arduino loop alone updates LVGL.

## Sources
V2 drivers: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C
Source copyright notices and Apache license are included.
Logo: https://brand.utswmed.org/documents/1378/master-logo-reversed-white.png
Background #004C97; original white logo proportions preserved.
