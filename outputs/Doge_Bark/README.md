# Doge Bark — Waveshare ESP32-S3-Touch-LCD-1.85C V2

Open `Doge_Bark.ino` in Arduino IDE. This is a separate sketch alongside
`UTSW_Splash`; the welcome program files are unchanged.

The photo fills the entire round display and spins clockwise once every six seconds. Tap anywhere on it to hear one short real bark. The picture gives a small pop and
shows “Woof!” as feedback. Rapid taps queue up to eight barks; holding does
not repeatedly trigger the sound. Audio and artwork are embedded in flash.
No SD card, Wi-Fi, microphone capture, or cloud service is needed.

Use the existing installed LVGL 8.3.10 and ESP32 core 3.1.1:
- Board: ESP32S3 Dev Module
- Flash: 16MB; PSRAM: OPI
- Partition: 16M Flash (3MB APP/9.9MB FATFS)
- USB CDC On Boot: Enabled
- USB Mode: Hardware CDC and JTAG
- Select the board's USB port and click Upload.

Uploading switches the device to this program. Upload UTSW_Splash again
whenever you want to return to the welcome program.

V2 audio uses the same tested ES8311/ES7210 setup as the welcome sketch.
GPIO15 enables the speaker amplifier. Playback runs on a separate task;
only the Arduino loop updates LVGL. Microphone RX remains disabled.

## Assets and credits

`assets/doge.png` is the restored original Doge photograph supplied by the user, encoded
at 360×360 RGB565 in DogeImage.cpp to fill the display. The round screen naturally masks the photo corners.
The older image-prompt.txt describes the previously used illustration only.

`assets/bark.wav` and BarkClip.cpp contain a 0.38-second mono bark, resampled
to 16kHz from https://github.com/CureJe/sedentary-reminder/blob/main/assets/audio/corgi.wav
Original: “Sound-of-dog.ogg” by Kriplozoik at English Wikipedia:
https://commons.wikimedia.org/wiki/File:Sound-of-dog.ogg
License: CC BY-SA 3.0, https://creativecommons.org/licenses/by-sa/3.0/
The bark asset and its PCM representation are distributed under that license.
Upstream trimmed, normalized and faded the recording. This program resamples
it to 16kHz and encodes it as a C++ array; the breed in the recording is unknown.

Hardware drivers: https://github.com/waveshareteam/ESP32-S3-Touch-LCD-1.85C
See LICENSE-Waveshare.txt and individual source headers.
