// E3: fixed |I|=8, witness-anchored nested R at 10..50% of nonendpoints.
// R is Z followed by a reproducible permutation of nonendpoints, skipping Z.
#include "../src/enumerate.cpp"
#define main old_query_generator_main
#include "generate_ir_queries.cpp"
#undef main
#include <random>
#include <sstream>

int main(int argc,char** argv){
 try {
  if(argc!=5)throw std::runtime_error("usage: e3gen GRAPH ENDPOINTS OUTPUT SEED");
  const u64 seed=number(argv[4],"seed");std::string out=argv[3],parts=out+".parts";
  if(std::filesystem::exists(out))throw std::runtime_error("output exists");
  std::filesystem::create_directories(parts);
  fd::Query q;
  fdio::read_graph_bin(argv[1],[&](u64 n,u64 m){if(n>INT32_MAX-2||m>INT32_MAX||n<82)throw std::runtime_error("unsupported graph");q.n=n;q.u.reserve(m);q.v.reserve(m);},[&](u64 u,u64 v){q.u.push_back(u);q.v.push_back(v);});
  q.weights.assign(q.n,1);q.marks.assign(q.n,8);
  const auto eps=read_endpoints(argv[2],q.n);const u64 smallest=(q.n-2)/10;
  std::set<std::pair<int,int>> used;int accepted=0;u64 checks=0;
  for(int s=0;s<20;++s){std::ifstream f(parts+"/"+std::to_string(s)+".jsonl");if(!f)break;std::string line;int count=0;while(std::getline(f,line)){if(!count&&!used.insert({json_field(line,"x"),json_field(line,"y")}).second)throw std::runtime_error("duplicate checkpoint pair");++count;}if(count!=5)throw std::runtime_error("bad checkpoint");++accepted;}
  // Each endpoint gets one deterministic attempt. A rejected witness is not a
  // claim of infeasibility; exhaustion is reported rather than changing sampling.
  for(u64 ticket=0;ticket<eps.size()&&accepted<20;++ticket){
   auto ep=eps[ticket];if(used.count({ep.x,ep.y}))continue;
   std::mt19937_64 rng(mix(seed^ticket));
   std::fill(q.marks.begin(),q.marks.end(),8);q.marks[ep.x]=1;q.marks[ep.y]=2;
   fd::Constructors ctor(q);fd::Bits required(q.n),allowed(q.n,1);allowed[ep.x]=allowed[ep.y]=0;
   std::vector<int> z;std::cerr<<"attempt="<<ticket<<" accepted="<<accepted<<" constructing"<<std::endl;
   ++checks;if(!ctor.wienoebst(required,allowed,z))throw std::runtime_error("infeasible endpoint");
   if(z.size()<8){std::cerr<<"reject witness_too_small="<<z.size()<<std::endl;continue;}
   std::shuffle(z.begin(),z.end(),rng);std::vector<int> iv(z.begin(),z.begin()+8);
   for(int v:iv)required[v]=1;
   std::fill(allowed.begin(),allowed.end(),0);for(int v:z)allowed[v]=1;
   std::vector<std::vector<int>> blocks{std::vector<int>(z.begin()+8,z.end())};int calls=0;
   while(z.size()>smallest&&!blocks.empty()&&calls<256){
    auto block=std::move(blocks.back());blocks.pop_back();if(block.empty())continue;
    for(int v:block)allowed[v]=0;std::vector<int> candidate;++checks;++calls;
    if(ctor.wienoebst(required,allowed,candidate))z=std::move(candidate);
    else {for(int v:block)allowed[v]=1;if(block.size()>1){auto mid=block.begin()+block.size()/2;blocks.emplace_back(block.begin(),mid);blocks.emplace_back(mid,block.end());}}
   }
   if(z.size()>smallest){std::cerr<<"reject witness_too_large="<<z.size()<<std::endl;continue;}
   fd::Bits fixed(q.n);for(int v:z){if(v==ep.x||v==ep.y||v<0||v>=q.n||fixed[v])throw std::runtime_error("invalid witness");fixed[v]=1;}
   for(int v:iv)if(!fixed[v])throw std::runtime_error("lost I");
   std::vector<int> checked;++checks;if(!ctor.wienoebst(fixed,fixed,checked)||(q.n<10000&&!fd::valid_fd(q,z)))throw std::runtime_error("fixed witness check failed");
   const u64 permutation_seed=mix(seed^ticket^0xe3);
   std::ostringstream rows;
   for(int level=1;level<=5;++level){
    const u64 size=u64(q.n-2)*level/10;
    rows<<"{\"sampling\":\"paired-wienoebst-witness-e3-v1\",\"workload\":\"E3\",\"query_id\":"<<accepted*5+level-1<<",\"pair_id\":"<<accepted<<",\"attempt\":"<<ticket<<",\"seed\":"<<seed<<",\"x\":"<<ep.x<<",\"y\":"<<ep.y<<",\"i_size\":8,\"r_size\":"<<size<<",\"k\":"<<size-8<<",\"percent\":"<<level*10<<",\"universe_size\":"<<q.n-2<<",\"feasible\":true,\"nonempty\":true,\"i\":[";
    for(int j=0;j<8;++j){if(j)rows<<',';rows<<iv[j];}
    rows<<"],\"r_representation\":{\"type\":\"witness-prefix-feistel-skip-v1\",\"seed\":"<<permutation_seed<<",\"prefix_size\":"<<size<<",\"anchor\":[";
    for(std::size_t j=0;j<z.size();++j){if(j)rows<<',';rows<<z[j];}rows<<"]}}\n";
   }
   const auto path=parts+"/"+std::to_string(accepted)+".jsonl";std::ofstream f(path+".tmp");f<<rows.str();f.close();if(!f)throw std::runtime_error("write failed");std::filesystem::rename(path+".tmp",path);
   used.insert({ep.x,ep.y});++accepted;std::cerr<<"accepted="<<accepted<<" attempt="<<ticket<<" witness_size="<<z.size()<<" constructor_calls="<<checks<<std::endl;
  }
  if(accepted!=20)throw std::runtime_error("endpoint pool exhausted; partial groups preserved");
  std::ofstream f(out+".tmp");for(int s=0;s<20;++s){std::ifstream in(parts+"/"+std::to_string(s)+".jsonl");f<<in.rdbuf();}f.close();if(!f)throw std::runtime_error("merge failed");std::filesystem::rename(out+".tmp",out);
  std::cerr<<"complete rows=100 constructor_calls="<<checks<<std::endl;
 }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<std::endl;return 2;}
}
