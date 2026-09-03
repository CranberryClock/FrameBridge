#include "temporal_frame.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
using framebridge::render::SceneState;
using namespace framebridge::temporal;
namespace {
void Check(bool v,const char* m){if(!v)throw std::runtime_error(m);}
SceneState State(std::uint64_t frame,float x,float y,float z=3) { SceneState s; s.frame=frame;s.rotationX=x;s.rotationY=y;s.cameraZ=z;s.width=640;s.height=360;s.resizeGeneration=1;s.simulationTime=frame/60.0;return s; }
float Length(MotionVector v){return std::sqrt(v.x*v.x+v.y*v.y);}
}
int main(int argc,char** argv) {
 try {
  (void)argv;
  const bool fail=argc>1;
  TemporalInput half{{640,360},{320,180},.5f,1,ResetReason::Initialization,false};
  TemporalInput full{{640,360},{640,360},1.0f,2,ResetReason::None,false};
  TemporalInput wide{{800,450},{400,225},.5f,3,ResetReason::None,false};
  Check(ValidateInput(half)&&ValidateInput(full)&&ValidateInput(wide),"valid extents rejected");
  Check(half.input==Extent{320,180},"half extent");
  Check(!ValidateInput(TemporalInput{{640,360},{1,1},.25f,1,ResetReason::None,false}),"invalid scale accepted");
  auto first=BuildFrame(State(60,.1f,.2f),half,std::nullopt); Check(first.reset&&first.previousLogicalFrame==0,"initial reset");
  Check(first.motionScale==std::array<float,2>{1.0f/320.0f,1.0f/180.0f},"320x180 motion scale");
  auto wideFrame=BuildFrame(State(60,.1f,.2f),wide,first); Check(wideFrame.motionScale==std::array<float,2>{1.0f/400.0f,1.0f/225.0f},"400x225 motion scale");
  auto fullFrame=BuildFrame(State(60,.1f,.2f),full,wideFrame); Check(fullFrame.motionScale==std::array<float,2>{1.0f/640.0f,1.0f/360.0f},"scale 1 motion scale");
  auto stationary=BuildFrame(State(61,.1f,.2f),TemporalInput{{640,360},{320,180},.5f,2,ResetReason::None,false},first); Check(!stationary.reset&&Length(stationary.cornerMotion[0])<1e-6f,"stationary motion");
  auto x=BuildFrame(State(62,.2f,.2f),TemporalInput{{640,360},{320,180},.5f,3,ResetReason::None,false},stationary);
  auto y=BuildFrame(State(63,.2f,.3f),TemporalInput{{640,360},{320,180},.5f,4,ResetReason::None,false},x);
  auto xy=BuildFrame(State(64,.3f,.4f),TemporalInput{{640,360},{320,180},.5f,5,ResetReason::None,false},y);
  Check(Length(x.cornerMotion[6])>0&&Length(y.cornerMotion[6])>0&&Length(xy.cornerMotion[6])>0,"rotation motion");
  auto camera=BuildFrame(State(65,.3f,.4f,4),TemporalInput{{640,360},{320,180},.5f,6,ResetReason::None,false},xy); Check(Length(camera.cornerMotion[6])>0,"camera motion");
  auto skipped=BuildFrame(State(70,.35f,.45f,4),TemporalInput{{640,360},{320,180},.5f,7,ResetReason::None,false},camera); Check(skipped.previousLogicalFrame==65,"skipped previous submitted state");
  auto resize=BuildFrame(State(71,.35f,.45f,4),TemporalInput{{800,450},{400,225},.5f,8,ResetReason::Dimensions|ResetReason::ResizeGeneration,false},skipped); Check(resize.reset&&resize.inputExtent==Extent{400,225},"resize reset");
  auto scale=BuildFrame(State(72,.35f,.45f,4),TemporalInput{{800,450},{800,450},1.0f,9,ResetReason::RenderScale,false},resize); Check(scale.reset,"scale reset");
  auto jitter=BuildFrame(State(60,.1f,.2f),TemporalInput{{640,360},{320,180},.5f,10,ResetReason::Initialization,true},std::nullopt);
  auto jitterAgain=BuildFrame(State(999,.1f,.2f),TemporalInput{{640,360},{320,180},.5f,10,ResetReason::Initialization,true},std::nullopt);
  Check(jitter.jitterOffsetPixels==jitterAgain.jitterOffsetPixels&&jitter.currentUnjittered.projection==jitterAgain.currentUnjittered.projection&&jitter.jitteredProjection!=jitter.currentUnjittered.projection,"deterministic jitter/unjittered matrix");
  Check(jitter.motionConvention.find("previous-to-current")!=std::string::npos&&!jitter.motionIncludesJitter,"motion convention");
  ReferenceUpscaler up; auto invalid=up.Evaluate(jitter,nullptr); Check(invalid.status==UpscaleStatus::InvalidFrame,"upscaler resource guard");
  if(fail) { Check(Length(x.cornerMotion[6])<0,"controlled motion negative control"); }
  std::cout<<"PASS temporal_contract tests=18 scales=1.0,0.5 jitter=deterministic motion=previous-to-current-render-pixels resets=PASS upscaler=PASS\n"; return 0;
 } catch(const std::exception& e){std::cerr<<"FAIL temporal_contract: "<<e.what()<<"\n";return 1;}
}
