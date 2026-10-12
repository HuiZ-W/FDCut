#include "../src/enumerate.cpp"
#include <fstream>
#include <iostream>
#include <cstdlib>
#include "query_common.hpp"
int main(int argc,char** argv){try{
 if(argc!=4&&argc!=5)return 2;
 std::ifstream in(argv[2]);int x,y,ni,nr;if(!(in>>x>>y>>ni>>nr)||ni<0||nr<ni)throw std::runtime_error("bad spec");
 auto start=std::chrono::steady_clock::now();auto q=fd::read_binary_full_domain(argv[1],x,y,1);
 std::fill(q.marks.begin(),q.marks.end(),0);q.marks[x]=1;q.marks[y]=2;std::vector<int> required;
 for(int j=0,v;j<ni;++j){if(!(in>>v)||v<0||v>=q.n||v==x||v==y)throw std::runtime_error("bad I");required.push_back(v);}
 if(argc==5){
  if(std::string(argv[4])!="compact")throw std::runtime_error("bad encoding");
  u64 seed;int anchors;if(!(in>>seed>>anchors)||anchors<0||anchors>nr)throw std::runtime_error("bad compact spec");
  for(int j=0,v;j<anchors;++j){if(!(in>>v)||v<0||v>=q.n||q.marks[v])throw std::runtime_error("bad anchor");q.marks[v]=8;}
  int count=anchors;
  for(u64 pos=0;count<nr&&pos<u64(q.n-2);++pos){int v=vertex_from_rank(permuted_rank(pos,q.n-2,seed),x,y);if(!q.marks[v]){q.marks[v]=8;++count;}}
  if(count!=nr)throw std::runtime_error("compact prefix exhausted");
 }else for(int j=0,v;j<nr;++j){if(!(in>>v)||v<0||v>=q.n||v==x||v==y||q.marks[v])throw std::runtime_error("bad R");q.marks[v]=8;}
 for(int v:required){if(q.marks[v]!=8)throw std::runtime_error("I not subset R or duplicate");q.marks[v]=12;}
 double read_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();std::string method=argv[3];
 const bool timing_markers=std::getenv("FD_TIMING_MARKERS")!=nullptr;
 if(timing_markers)std::cerr<<"FD_ALGO_START_NS "<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()<<std::endl;
 auto a=method=="component"?fd::solve(q,method):fd::enumerate(q,method,0,604800000);
 if(timing_markers)std::cerr<<"FD_ALGO_END_NS "<<std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()<<std::endl;
 auto checkstart=std::chrono::steady_clock::now();
 const char* skip_env=std::getenv("FD_SKIP_POSTVALIDATION");bool skip=skip_env&&std::string(skip_env)=="1";
 if(!skip&&a.feasible){fd::Bits fixed(q.n);for(int v:a.selected){if(v<0||v>=q.n||!(q.marks[v]&8)||fixed[v])throw std::runtime_error("invalid solution");fixed[v]=1;}for(int v:required)if(!fixed[v])throw std::runtime_error("missing I");fd::Constructors checker(q);std::vector<int> z;if(!checker.wienoebst(fixed,fixed,z))throw std::runtime_error("fixed solution FD check failed");if(q.n<10000&&!fd::valid_fd(q,a.selected))throw std::runtime_error("independent oracle failed");}
 double validation_s=skip?0:std::chrono::duration<double>(std::chrono::steady_clock::now()-checkstart).count();
 std::cout<<"{\"read_s\":"<<read_s<<",\"validation_s\":"<<validation_s<<",\"post_validation\":\""<<(skip?"skipped":"enabled")<<"\",\"validation\":\""<<(skip?"skipped-by-request":"original-DAG-fixed-set-Wienoebst")<<"\",\"query\":";fd::print(std::cout,a,method);std::cout<<"}\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 2;}}
