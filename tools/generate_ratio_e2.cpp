// Standalone experiment tool; reuse the independently implemented constructor.
// No minimum-cost solver or HPF call is made by this generator.
#include "../src/enumerate.cpp"
#include "query_common.hpp"
#include <random>
#include <sstream>

std::vector<int> ratio_levels(int n,int i){
 const int cap=std::min(256,(n-2)/i);std::vector<int> ks;
 for(int k:{16,32,64,128,256})if(k<=cap)ks.push_back(k);
 if(ks.empty()||ks.back()!=cap)ks.push_back(cap);
 return ks;
}

int main(int argc,char** argv){
 try{
  if(argc!=5)throw std::runtime_error("usage: fdratioe2 GRAPH ENDPOINTS OUTPUT SEED");
  const u64 seed=number(argv[4],"seed");const std::string output=argv[3],parts=output+".parts";
  if(std::filesystem::exists(output))throw std::runtime_error("output exists");
  std::filesystem::create_directories(parts);
  fd::Query q;
  fdio::read_graph_bin(argv[1],[&](u64 n,u64 m){if(n>INT32_MAX-2||m>INT32_MAX||n<18)throw std::runtime_error("unsupported graph size");q.n=n;q.u.reserve(m);q.v.reserve(m);},[&](u64 u,u64 v){q.u.push_back(u);q.v.push_back(v);});
  q.weights.assign(q.n,1);q.marks.assign(q.n,8);
  const auto endpoints=read_endpoints(argv[2],q.n);const auto schedule=e2_i_schedule(seed);
  std::array<bool,20> done{};std::set<std::pair<int,int>> used;int accepted=0;u64 ticket=0,checks=0;
  for(int s=0;s<20;++s){std::ifstream in(parts+"/"+std::to_string(s)+".jsonl");if(!in)continue;std::string line;int count=0;while(std::getline(in,line)){if(!count)used.insert({json_field(line,"x"),json_field(line,"y")});++count;}if(count!=int(ratio_levels(q.n,schedule[s]).size()))throw std::runtime_error("bad saved group");done[s]=true;++accepted;}
  {std::ifstream in(parts+"/ticket");if(in)in>>ticket;}
  auto started=std::chrono::steady_clock::now();
  for(;accepted<20;++ticket){
   if(ticket%20==0){std::ofstream save(parts+"/ticket.tmp");save<<ticket;save.close();if(!save)throw std::runtime_error("checkpoint write");std::filesystem::rename(parts+"/ticket.tmp",parts+"/ticket");std::cerr<<"ticket="<<ticket<<" accepted="<<accepted<<" constructor_calls="<<checks<<" elapsed_s="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-started).count()<<std::endl;}
   const int slot=ticket%20;if(done[slot])continue;
   std::mt19937_64 rng(mix(seed^ticket));const auto ep=endpoints[rng()%endpoints.size()];if(used.count({ep.x,ep.y}))continue;
   std::fill(q.marks.begin(),q.marks.end(),8);q.marks[ep.x]=1;q.marks[ep.y]=2;
   fd::Constructors ctor(q);fd::Bits required(q.n),allowed(q.n,1);allowed[ep.x]=allowed[ep.y]=0;
   std::vector<int> z;++checks;if(!ctor.wienoebst(required,allowed,z))throw std::runtime_error("endpoint infeasible");
   const int target=schedule[slot];const auto levels=ratio_levels(q.n,target);
   if(int(z.size())<target){std::cerr<<"reject ticket="<<ticket<<" witness_too_small="<<z.size()<<" target="<<target<<std::endl;continue;}
   std::shuffle(z.begin(),z.end(),rng);std::vector<int> iv(z.begin(),z.begin()+target);
   for(int v:iv){required[v]=1;q.marks[v]=12;}
   // Random block removal reduces a feasible witness, not its objective.
   // Accepted updates come only from the constrained FD constructor.
   std::vector<int> optional(z.begin()+target,z.end());std::fill(allowed.begin(),allowed.end(),0);for(int v:z)allowed[v]=1;
   std::vector<std::vector<int>> blocks{optional};int shrink_calls=0;
   while(z.size()>std::size_t(target*levels.front())&&!blocks.empty()&&shrink_calls<256){
    auto block=std::move(blocks.back());blocks.pop_back();if(block.empty())continue;
    for(int v:block)allowed[v]=0;
    std::vector<int> candidate;++checks;++shrink_calls;
    if(ctor.wienoebst(required,allowed,candidate)){z=std::move(candidate);}
    else{for(int v:block)allowed[v]=1;if(block.size()>1){auto mid=block.begin()+block.size()/2;blocks.emplace_back(block.begin(),mid);blocks.emplace_back(mid,block.end());}}
   }
   if(z.size()>std::size_t(target*levels.front())){std::cerr<<"reject ticket="<<ticket<<" witness_too_large="<<z.size()<<" target="<<target<<std::endl;continue;}
   auto verify=[&](){fd::Bits fixed(q.n);for(int v:z){if(!(q.marks[v]&8)||fixed[v])return false;fixed[v]=1;}for(int v:iv)if(!fixed[v])return false;std::vector<int> checked;++checks;if(!ctor.wienoebst(fixed,fixed,checked))return false;return q.n>1000||fd::valid_fd(q,z);};
   if(!verify())throw std::runtime_error("original DAG fixed-witness check failed");
   std::vector<int> r=z;std::set<int> seen(r.begin(),r.end());
   while(r.size()<std::size_t(target*levels.back())){int v=rng()%q.n;if(v!=ep.x&&v!=ep.y&&seen.insert(v).second)r.push_back(v);}
   std::ostringstream rows;int level=0;
   for(int k:levels){
    std::fill(q.marks.begin(),q.marks.end(),0);q.marks[ep.x]=1;q.marks[ep.y]=2;
    for(int j=0;j<target*k;++j)q.marks[r[j]]=8;
    for(int v:iv)q.marks[v]=12;
    if(!verify())throw std::runtime_error("final query witness invalid");
    rows<<"{\"sampling\":\"paired-wienoebst-witness-ratio-e2-v1\",\"k_definition\":\"r_size/i_size\",\"query_id\":"<<slot*5+level++<<",\"pair_id\":"<<slot<<",\"attempt\":"<<ticket<<",\"seed\":"<<seed<<",\"x\":"<<ep.x<<",\"y\":"<<ep.y<<",\"k\":"<<k<<",\"i_size\":"<<target<<",\"r_size\":"<<target*k<<",\"feasible\":true,\"nonempty\":true,\"witness_size\":"<<z.size()<<",\"i\":[";
    for(int j=0;j<target;++j){if(j)rows<<',';rows<<iv[j];}rows<<"],\"r\":[";
    for(int j=0;j<target*k;++j){if(j)rows<<',';rows<<r[j];}rows<<"]}\n";
   }
   const std::string path=parts+"/"+std::to_string(slot)+".jsonl";std::ofstream save(path+".tmp");save<<rows.str();save.close();if(!save)throw std::runtime_error("group write");std::filesystem::rename(path+".tmp",path);
   done[slot]=true;used.insert({ep.x,ep.y});++accepted;std::cerr<<"accepted="<<accepted<<" slot="<<slot<<" i_size="<<target<<" z_size="<<z.size()<<" ticket="<<ticket<<std::endl;
  }
  int total=0;std::ofstream out(output+".tmp");for(int s=0;s<20;++s){total+=ratio_levels(q.n,schedule[s]).size();std::ifstream in(parts+"/"+std::to_string(s)+".jsonl");if(!in)throw std::runtime_error("missing group");out<<in.rdbuf();}out.close();if(!out)throw std::runtime_error("merge failed");std::filesystem::rename(output+".tmp",output);std::cerr<<"complete rows="<<total<<" constructor_calls="<<checks<<std::endl;return 0;
 }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<std::endl;return 2;}
}
