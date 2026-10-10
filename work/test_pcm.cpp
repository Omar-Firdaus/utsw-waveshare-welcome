#include "../outputs/UTSW_Splash/PcmUtils.h"
#include <assert.h>
int main() {
  assert(pcm_from_mic32(0)==0);
  assert(pcm_from_mic32(16384000)==1000);
  assert(pcm_from_mic32(-16384000)==-1000);
  assert(pcm_from_mic32(INT32_MAX)==32767);
  assert(pcm_from_mic32(INT32_MIN)==-32768);
  int16_t left[] = {-32768,0,12000,0};
  int16_t right[] = {0,-32768,0,12000};
  assert(pcm_active_lane(left,2)==0);
  assert(pcm_active_lane(right,2)==1);
  assert(pcm_playback_sample(-1000,40,1000)==-1500);
  assert(pcm_playback_sample(1000,40,1000)==1500);
  assert(pcm_playback_sample(-32768,100,1000)==-32767);
  assert(pcm_playback_sample(32767,100,1000)==32767);
  assert(pcm_playback_sample(1000,0,1000)==0);
  assert(pcm_playback_sample(-1000,999,1000)==0);
  assert(pcm_playback_sample(1000,0,0)==0);
  assert(pcm_playback_sample(1000,1000,1000)==0);
}
