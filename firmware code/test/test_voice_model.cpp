#include "edge-impulse-sdk/classifier/ei_run_classifier.h"
#include "../include/voice_model_config.h"
#include <fstream>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
static int16_t *slice;
int main(int argc,char **argv) {
 int wake=-1;
 for(int i=0;i<EI_CLASSIFIER_LABEL_COUNT;++i) if(!strcmp(ei_classifier_inferencing_categories[i],"hey_stopmate")) wake=i;
 if(wake<0)return 2;
 for(int f=1;f<argc;++f){
  std::ifstream in(argv[f],std::ios::binary|std::ios::ate);
  if(!in)return 3;
  size_t count=in.tellg()/2;
  std::vector<int16_t> audio(16000+count+16000,0);
  in.seekg(0);in.read(reinterpret_cast<char*>(audio.data()+16000),count*2);
  run_classifier_init();int hits=0;bool detected=false;float peak=0;
  for(size_t p=0;p+EI_CLASSIFIER_SLICE_SIZE<=audio.size();p+=EI_CLASSIFIER_SLICE_SIZE){
   slice=audio.data()+p;signal_t signal;signal.total_length=EI_CLASSIFIER_SLICE_SIZE;
   signal.get_data=[](size_t o,size_t n,float *out){for(size_t i=0;i<n;++i)out[i]=slice[o+i];return 0;};
   ei_impulse_result_t result={};
   auto status=run_classifier_continuous(&signal,&result,false,false);
   if(status!=EI_IMPULSE_OK){printf("INFERENCE_ERROR %d\n",status);return 4;}
   if(p+EI_CLASSIFIER_SLICE_SIZE<16000)continue;
   float score=result.classification[wake].value;peak=std::max(peak,score);
   float other=0;for(int i=0;i<EI_CLASSIFIER_LABEL_COUNT;++i)if(i!=wake)other=std::max(other,result.classification[i].value);
   hits=(score>=VOICE_WAKE_THRESHOLD && score-other>=VOICE_WAKE_MARGIN)?hits+1:0;
   if(hits>=VOICE_REQUIRED_HITS)detected=true;
  }
  printf("RESULT %s %d %.3f\n",argv[f],detected,peak);fflush(stdout);
 }
}
