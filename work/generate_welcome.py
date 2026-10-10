import sys
sys.path.insert(0, 'work/tts-runtime')
import sherpa_onnx
import numpy as np
from pathlib import Path
import wave
folder='work/vits-piper-en_US-lessac-medium'
config=sherpa_onnx.OfflineTtsConfig(model=sherpa_onnx.OfflineTtsModelConfig(vits=sherpa_onnx.OfflineTtsVitsModelConfig(model=folder+'/en_US-lessac-medium.onnx',data_dir=folder+'/espeak-ng-data',tokens=folder+'/tokens.txt'),num_threads=2))
assert config.validate()
tts=sherpa_onnx.OfflineTts(config)
audio=tts.generate(text=Path('outputs/welcome-script.txt').read_text(),sid=0,speed=0.70)
samples=np.asarray(audio.samples)
assert len(samples)>audio.sample_rate*10 and np.max(np.abs(samples))>0.01
samples=samples*(18000/np.max(np.abs(samples)))
with wave.open('work/welcome-source.wav','wb') as w:
 w.setnchannels(1);w.setsampwidth(2);w.setframerate(audio.sample_rate);w.writeframes(samples.astype('<i2').tobytes())
print('Generated welcome speech:',len(samples)/audio.sample_rate,'seconds',flush=True)
