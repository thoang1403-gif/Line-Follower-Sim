#include "engine.h"
#include <algorithm>
#include <cmath>
#include <cctype>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace lf {
const char* stateKey(State s) {
    switch(s) { case State::Stable:return "stable"; case State::Oscillating:return "oscillating";
    case State::Risk:return "risk"; default:return "unknown"; }
}
uint16_t crc16(const std::string &s) {
    uint16_t crc=0xFFFF;
    for(unsigned char c:s) { crc^=uint16_t(c)<<8; for(int i=0;i<8;++i) crc=(crc&0x8000)?uint16_t((crc<<1)^0x1021):uint16_t(crc<<1); }
    return crc;
}
std::string frame(const std::string &body) {
    std::ostringstream s; s<<body<<'*'<<std::uppercase<<std::hex<<std::setw(4)<<std::setfill('0')<<crc16(body)<<'\n'; return s.str();
}
std::string encode(const Sample &s) {
    std::ostringstream o;
    o<<"T,1,"<<s.boot<<','<<s.seq<<','<<s.tick<<','<<s.left<<','<<s.right<<','<<std::lround(s.error*1000)
     <<','<<std::lround(s.output*1000)<<','<<s.pwmL<<','<<s.pwmR<<','<<s.base<<','<<s.mode
     <<','<<std::lround(s.kp*1000)<<','<<std::lround(s.ki*1000)<<','<<std::lround(s.kd*1000)
     <<','<<s.losses<<','<<s.lastLoss<<','<<(s.synthetic?0:1);
    return frame(o.str());
}
std::string command(uint32_t id,const std::string &op,int value) {
    return frame("C,1,"+std::to_string(id)+","+op+","+std::to_string(value));
}
static std::vector<std::string> split(const std::string &s) {
    std::vector<std::string> v; std::istringstream i(s); std::string t;
    while(std::getline(i,t,',')) v.push_back(t); return v;
}
static int64_t number(const std::string &s,int64_t lo,int64_t hi) {
    if(s.empty()) throw std::runtime_error("empty");
    size_t j=(s[0]=='-')?1:0;
    if(j==s.size()) throw std::runtime_error("sign");
    for(;j<s.size();++j) if(s[j]<'0'||s[j]>'9') throw std::runtime_error("integer");
    size_t used=0; auto n=std::stoll(s,&used);
    if(used!=s.size()||n<lo||n>hi) throw std::runtime_error("range"); return n;
}
void Decoder::reset(){buffer.clear();discard=false;errors=0;}
void Decoder::feed(const std::string &bytes) {
    for(char c:bytes) {
        if(c=='\n') { if(!discard&&!buffer.empty()) line(buffer);buffer.clear();discard=false; }
        else if(!discard) {
            if(buffer.size()>=255) {++errors;discard=true;buffer.clear();}
            else buffer+=c;
        }
    }
}
void Decoder::line(const std::string &raw) {
    try {
        std::string s=raw; if(!s.empty()&&s.back()=='\r') s.pop_back();
        auto star=s.rfind('*'); if(star==std::string::npos||s.size()-star!=5) throw std::runtime_error("crc");
        for(size_t i=star+1;i<s.size();++i) if(!std::isxdigit(static_cast<unsigned char>(s[i]))) throw std::runtime_error("hex");
        if(std::stoul(s.substr(star+1),nullptr,16)!=crc16(s.substr(0,star))) throw std::runtime_error("crc");
        auto v=split(s.substr(0,star));
        if(v.size()<2||v[1]!="1") throw std::runtime_error("version");
        if(v[0]=="T"&&v.size()==19) {
            Sample t; t.boot=uint32_t(number(v[2],1,UINT32_MAX)); t.seq=uint32_t(number(v[3],0,UINT32_MAX));
            t.tick=uint32_t(number(v[4],0,UINT32_MAX)); t.left=int(number(v[5],0,1)); t.right=int(number(v[6],0,1));
            t.error=number(v[7],-1000000,1000000)/1000.0; t.output=number(v[8],-255000,255000)/1000.0;
            t.pwmL=int(number(v[9],0,255));t.pwmR=int(number(v[10],0,255));t.base=int(number(v[11],0,255));t.mode=int(number(v[12],0,2));
            t.kp=number(v[13],0,1000000)/1000.0;t.ki=number(v[14],0,1000000)/1000.0;t.kd=number(v[15],0,1000000)/1000.0;
            t.losses=uint32_t(number(v[16],0,UINT32_MAX));t.lastLoss=uint32_t(number(v[17],0,UINT32_MAX));t.synthetic=number(v[18],0,1)==0;t.wire=s;
            if(sample) sample(t);
        } else if(v[0]=="A"&&v.size()==6) {
            Ack a; a.id=uint32_t(number(v[2],0,UINT32_MAX));a.code=int(number(v[3],0,4));a.base=int(number(v[4],0,255));
            if(v[5]!="PWM") throw std::runtime_error("ack op"); if(ack) ack(a);
        } else throw std::runtime_error("type");
    } catch(const std::exception&) { ++errors; }
}
void Timeline::reset(){initialized=false;missing=0;}
int Timeline::accept(Sample &s) {
    int result=1;
    if(!initialized||s.boot!=previous.boot){ result=initialized?2:1; s.t=s.tick; initialized=true; }
    else {
        uint32_t ds=s.seq-previous.seq, dt=s.tick-previous.tick;
        if(ds==0||ds>=0x80000000U||dt==0||dt>=0x80000000U) return 0;
        s.t=previous.t+dt; missing+=ds-1;
    }
    previous=s; return result;
}
void Window::push(const Sample &s) {
    if(!data.empty()&&(s.boot!=data.back().boot||s.t-data.back().t>cfg.maxGapMs||s.t<=data.back().t||
       s.kp!=data.back().kp||s.ki!=data.back().ki||s.kd!=data.back().kd||s.base!=data.back().base||
       (s.mode==0)!=(data.back().mode==0))) data.clear();
    data.push_back(s);
    while(!data.empty()&&(s.t-data.front().t>cfg.windowMs||data.size()>512)) data.pop_front();
}
Features extract(const std::deque<Sample> &w,const FeatureConfig &cfg) {
    Features f;
    if(w.size()<8){f.reason="Chưa đủ mẫu (tối thiểu 8)";return f;}
    f.start=w.front().t;f.end=w.back().t;
    double span=(f.end-f.start)/1000.0;
    if(span*1000<cfg.windowMs-250){f.reason="Đang thu đủ cửa sổ lịch sử";return f;}
    if(w.back().mode==0){f.reason="Xe đang dừng";return f;}
    double changes=0,rev=0,sum=0,sq=0,delta=0,diff=0,searchTime=0,entries=0,absu=0,base=0;
    int sign=0;
    for(size_t i=0;i<w.size();++i) {
        const auto &s=w[i]; double u=s.output;
        int current=u>cfg.deadband?1:u<-cfg.deadband?-1:0;
        if(current) {if(sign&&current!=sign) ++rev; sign=current;}
        sum+=u;sq+=u*u;diff+=std::abs(s.pwmL-s.pwmR);absu+=std::abs(u);base+=s.base;
        if(i) {
            const auto &p=w[i-1];auto dt=s.t-p.t;
            if(dt<=0||dt>cfg.maxGapMs){f.reason="Cửa sổ có khoảng trống dữ liệu";return f;}
            changes+=(s.left!=p.left)+(s.right!=p.right);delta+=std::abs(u-p.output);
            if(p.mode==2) searchTime+=dt/1000.0;
            auto dl=uint32_t(s.losses-p.losses);if(dl<0x80000000U) entries+=dl;
        }
    }
    double n=double(w.size()),mean=sum/n;
    f.x={changes/span,rev/span,std::sqrt(std::max(0.0,sq/n-mean*mean)),delta/(n-1),diff/n,searchTime/span,entries/span,absu/n,base/n};
    f.valid=true; return f;
}
Prediction rules(const Features &f) {
    if(!f.valid) return {State::Unknown,f.reason};
    if(f.x[5]>0.05||f.x[6]>0) return {State::Risk,"Đã có tìm lại line trong cửa sổ; đây không phải bằng chứng cảnh báo trước"};
    if(f.x[7]>100&&f.x[4]>100) return {State::Risk,"Hiệu chỉnh lớn và chênh PWM kéo dài; cần kiểm tra thực tế"};
    if(f.x[1]>1.5&&f.x[2]>30&&f.x[4]>35) return {State::Oscillating,"Hiệu chỉnh đổi chiều nhiều, biến thiên PID và chênh PWM cùng tăng; đường uốn cũng có thể gây dấu hiệu này"};
    return {State::Stable,"Chưa vượt ngưỡng bộ quy tắc; chưa xác nhận chất lượng bám line thực tế"};
}
bool Tree::validate(std::string &error) const {
    if(nodes.empty()||nodes.size()>255||config.windowMs<2000||config.windowMs>10000||config.deadband<0||config.deadband>100||config.maxGapMs!=600){error="Invalid model size/config";return false;}
    std::vector<int> seen(nodes.size());
    std::function<bool(int,int)> visit=[&](int i,int depth){
        if(i<0||i>=int(nodes.size())||depth>8||seen[i]) return false;
        seen[i]=1;const auto &n=nodes[i];
        if(n.feature==-1) return n.label>=0&&n.label<=2&&n.left==-1&&n.right==-1;
        return n.feature>=0&&n.feature<FeatureCount&&std::isfinite(n.threshold)&&visit(n.left,depth+1)&&visit(n.right,depth+1);
    };
    if(!visit(0,0)||std::find(seen.begin(),seen.end(),0)!=seen.end()){error="Invalid/cyclic/unreachable tree";return false;} return true;
}
State Tree::predict(const Features &f) const {
    if(!f.valid||nodes.empty()) return State::Unknown;
    int i=0;
    for(int step=0;step<9;++step) {
        if(i<0||i>=int(nodes.size())) return State::Unknown;
        const auto &n=nodes[i];if(n.feature==-1) return State(n.label);
        if(n.feature<0||n.feature>=FeatureCount||!std::isfinite(f.x[n.feature])) return State::Unknown;
        // sklearn's tree predictor converts X to float32 before comparing thresholds.
        i=float(f.x[n.feature])<=n.threshold?n.left:n.right;
    } return State::Unknown;
}
void Policy::reset(){last=State::Unknown;streak=0;lastSent=-100000;holdUntil=0;}
void Policy::observe(State s){if(s==State::Unknown){last=s;streak=0;}else if(s==last)streak=std::min(streak+1,100);else{last=s;streak=1;}}
void Policy::hold(int64_t now){holdUntil=now+cfg.holdMs;streak=0;}
int Policy::propose(int base,State state) const {
    if(base<cfg.minimum||base>cfg.maximum||state==State::Unknown||state==State::Stable) return base;
    return std::max(cfg.minimum,base-std::min(cfg.maxStep,state==State::Risk?10:5));
}
bool Policy::allowed(int64_t now,int base,int target,bool fresh,int mode,bool pending) const {
    return fresh&&mode==1&&!pending&&streak>=cfg.confirmations&&now>=holdUntil&&now-lastSent>=cfg.intervalMs&&
        target>=cfg.minimum&&target<=cfg.maximum&&target<base&&base<=cfg.maximum&&base>=cfg.minimum&&base-target<=cfg.maxStep;
}
} // namespace lf
