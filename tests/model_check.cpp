#include "app/model_io.h"
#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <iostream>
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=3)return 2;lf::Tree tree;QString e;if(!loadTree(argv[1],tree,e)){std::cerr<<e.toStdString();return 3;}QFile f(argv[2]);if(!f.open(QIODevice::ReadOnly))return 4;QTextStream in(&f);in.readLine();int n=0;while(!in.atEnd()){auto v=in.readLine().split(',');if(v.size()!=10)return 5;lf::Features x;x.valid=true;for(int i=0;i<9;++i)x.x[i]=v[i].toDouble();if(int(tree.predict(x))!=v[9].toInt())return 6;++n;}std::cout<<"parity rows: "<<n<<'\n';return n?0:7;}
