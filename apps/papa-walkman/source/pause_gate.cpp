#include "pause_gate.hpp"
namespace papa {
bool PauseGateSink::Reset(std::string&e){held_.clear();if(!sink_.Reset(e))return false;return sink_.Pause(desired_.load(),e);}
bool PauseGateSink::Flush(std::string&e){
 if(desired_.load())return sink_.Pause(true,e);
 if(!held_.empty()&&sink_.Room()){if(!sink_.Submit(held_.data(),held_.size()/2,e))return false;held_.clear();}
 return sink_.Pause(false,e);
}
bool PauseGateSink::Pause(bool pause,std::string&e){if(pause||desired_.load())return sink_.Pause(true,e);return Flush(e);}
bool PauseGateSink::Poll(uint64_t&n,std::string&e){if(!Flush(e))return false;return sink_.Poll(n,e);}
bool PauseGateSink::Submit(const int16_t*p,size_t n,std::string&e){
 if(!n||n>1024){e="音频块大小错误";return false;}
 if(desired_.load()){
  if(!held_.empty()){e="暂停保留缓冲已满";return false;}
  held_.assign(p,p+n*2);return sink_.Pause(true,e);
 }
 return sink_.Submit(p,n,e);
}
}
