#include "core/engine.h"
#include <iostream>
#include <stdexcept>
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
int main(){
 lf::Sample s;s.boot=1;s.seq=1;s.tick=100;s.base=100;s.mode=1;s.output=50;
 lf::Decoder d;int count=0;d.sample=[&](lf::Sample a){++count;CHECK(a.base==100);};auto wire=lf::encode(s);
 for(char c:wire)d.feed(std::string(1,c));CHECK(count==1);d.feed(wire+wire);CHECK(count==3);
 auto corrupt=wire;corrupt[5]='9';d.feed(corrupt);CHECK(count==3&&d.errors==1);
 d.feed(std::string(300,'x')+"\n"+wire);CHECK(count==4&&d.errors==2);
 lf::Timeline t;CHECK(t.accept(s)==1);CHECK(t.accept(s)==0);s.seq=3;s.tick=300;CHECK(t.accept(s)==1&&t.missing==1);s.boot=2;CHECK(t.accept(s)==2);
 lf::Window w;for(int i=0;i<=30;++i){s.t=i*100;s.output=i%2?3:-3;s.base=100;w.push(s);}auto f=w.features();CHECK(f.valid&&f.x[1]==0);
 s.t=4000;w.push(s);CHECK(!w.features().valid);
 lf::Policy p;p.observe(lf::State::Risk);p.observe(lf::State::Risk);p.observe(lf::State::Risk);
 CHECK(p.propose(100,lf::State::Risk)==90);CHECK(p.allowed(4000,100,90,true,1,false));CHECK(!p.allowed(4000,100,90,false,1,false));CHECK(!p.allowed(4000,100,90,true,2,false));p.sent(4000);CHECK(!p.allowed(4100,100,90,true,1,false));
 lf::Tree tree;tree.nodes={{-1,-1,-1,2,0}};std::string e;CHECK(tree.validate(e));CHECK(tree.predict(f)==lf::State::Risk);tree.nodes={{0,0,0,-1,0}};CHECK(!tree.validate(e));
 std::cout<<"core tests passed\n";
}
