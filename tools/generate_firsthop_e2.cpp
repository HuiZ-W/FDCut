// Endpoint-guided first-hop candidate pool; full original DAG is never reduced.
#include "../src/enumerate.cpp"
#define main unused_ir_generator_main
#include "generate_ir_queries.cpp"
#undef main
#include <random>
#include <sstream>

int main(int argc,char**argv){try{
 if(argc!=6)throw std::runtime_error("usage: GRAPH OUTPUT SEED ATTEMPTS SECONDS");
 const std::string output=argv[2],parts=output+".parts";
 if(std::filesystem::exists(output)||std::filesystem::exists(parts))throw std::runtime_error("refusing existing output/checkpoints");
 const u64 seed=number(argv[3],"seed"),limit=number(argv[4],"attempts"),seconds=number(argv[5],"seconds");
 if(seed==0||seed>UINT32_MAX||limit>UINT32_MAX)throw std::runtime_error("seed must be nonzero uint32; attempts must fit uint32");
 auto start=std::chrono::steady_clock::now();auto elapsed=[&](){return std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();};
 fd::Query q;
 fdio::read_graph_bin(argv[1],[&](u64 n,u64 m){if(n<2050||n>INT32_MAX-2||m>INT32_MAX)throw std::runtime_error("unsupported graph");q.n=n;q.u.reserve(m);q.v.reserve(m);},[&](u64 u,u64 v){q.u.push_back(u);q.v.push_back(v);});
 q.weights.assign(q.n,1);q.marks.assign(q.n,0);
 std::vector<u64> off(q.n+1);for(int u:q.u)++off[u+1];std::partial_sum(off.begin(),off.end(),off.begin());
 std::vector<int> children(q.u.size());{auto pos=off;for(size_t e=0;e<q.u.size();++e)children[pos[q.u[e]]++]=q.v[e];}
 std::filesystem::create_directories(parts);auto schedule=e2_i_schedule(seed);std::array<bool,20> done{};
 std::set<std::pair<int,int>> used;u64 accepted=0,checks=0,degree_reject=0,endpoint_reject=0,infeasible=0,zsmall=0,ticket=0;
 std::cerr<<"ready read_index_s="<<elapsed()<<std::endl;
 for(;ticket<limit&&accepted<20&&elapsed()<seconds;++ticket){
  // Injective (worker seed, attempt) encoding; adjacent seeds cannot alias.
  int slot=ticket%20;if(done[slot])continue;int target=schedule[slot];std::mt19937_64 rng(mix((seed<<32)|ticket));
  int x=-1;std::vector<int> r;
  for(int draw=0;draw<256;++draw){int v=rng()%q.n;u64 degree=off[v+1]-off[v];if(degree<u64(target)||degree>u64(16*target))continue;
   x=v;r.assign(children.begin()+off[v],children.begin()+off[v+1]);std::sort(r.begin(),r.end());r.erase(std::unique(r.begin(),r.end()),r.end());break;}
  if(x<0){++degree_reject;continue;}
  int y=x,steps=2+rng()%3,taken=0;
  for(;taken<steps;++taken){u64 degree=off[y+1]-off[y];if(!degree)break;y=children[off[y]+rng()%degree];}
  if(taken<2||y==x||std::binary_search(r.begin(),r.end(),y)||used.count({x,y})){++endpoint_reject;continue;}
  std::fill(q.marks.begin(),q.marks.end(),0);q.marks[x]=1;q.marks[y]=2;for(int v:r)q.marks[v]=8;
  std::cerr<<"ticket="<<ticket<<" slot="<<slot<<" I="<<target<<" x="<<x<<" y="<<y<<" firsthop="<<r.size()<<" constructing elapsed="<<elapsed()<<std::endl;
  fd::Constructors ctor(q);fd::Bits required(q.n),allowed(q.n);for(int v:r)allowed[v]=1;
  std::vector<int> z;++checks;
  if(!ctor.wienoebst(required,allowed,z)){++infeasible;std::cerr<<"reject restricted_R_infeasible"<<std::endl;continue;}
  if(z.size()<size_t(target)){++zsmall;std::cerr<<"reject Z_too_small="<<z.size()<<std::endl;continue;}
  std::shuffle(z.begin(),z.end(),rng);std::vector<int> iv(z.begin(),z.begin()+target);fd::Bits fixed(q.n);for(int v:z)fixed[v]=1;
  std::vector<int> checked;++checks;
  if(!ctor.wienoebst(fixed,fixed,checked)||(q.n<10000&&!fd::valid_fd(q,z)))throw std::runtime_error("full-DAG fixed-witness validation failed");
  const auto firsthop=r.size();std::shuffle(r.begin(),r.end(),rng);std::set<int> seen(r.begin(),r.end());
  while(r.size()<size_t(256*target)){int v=rng()%q.n;if(v!=x&&v!=y&&seen.insert(v).second)r.push_back(v);}
  std::ostringstream rows;int level=0;
  for(int k:{16,32,64,128,256}){
   rows<<"{\"sampling\":\"paired-firsthop-witness-ratio-e2-v2\",\"k_definition\":\"r_size/i_size\",\"query_id\":"<<slot*5+level++<<",\"pair_id\":"<<slot<<",\"attempt\":"<<ticket<<",\"seed\":"<<seed<<",\"x\":"<<x<<",\"y\":"<<y<<",\"i_size\":"<<target<<",\"r_size\":"<<k*target<<",\"k\":"<<k<<",\"firsthop_size\":"<<firsthop<<",\"feasible\":true,\"nonempty\":true,\"witness_size\":"<<z.size()<<",\"i\":[";
   for(int j=0;j<target;++j){if(j)rows<<',';rows<<iv[j];}rows<<"],\"r\":[";
   for(int j=0;j<k*target;++j){if(j)rows<<',';rows<<r[j];}rows<<"],\"witness\":[";
   for(size_t j=0;j<z.size();++j){if(j)rows<<',';rows<<z[j];}rows<<"]}\n";
  }
  auto path=parts+"/"+std::to_string(slot)+".jsonl";std::ofstream f(path+".tmp");f<<rows.str();f.close();if(!f)throw std::runtime_error("checkpoint write");std::filesystem::rename(path+".tmp",path);
  done[slot]=true;used.insert({x,y});++accepted;std::cerr<<"accepted="<<accepted<<" slot="<<slot<<" witness="<<z.size()<<" elapsed="<<elapsed()<<std::endl;
 }
 std::cerr<<"SUMMARY attempts="<<ticket<<" accepted="<<accepted<<" checks="<<checks<<" degree_reject="<<degree_reject<<" endpoint_reject="<<endpoint_reject<<" infeasible="<<infeasible<<" z_small="<<zsmall<<" elapsed="<<elapsed()<<std::endl;
 if(accepted!=20)return 3;
 std::ofstream out(output+".tmp");for(int slot=0;slot<20;++slot){std::ifstream f(parts+"/"+std::to_string(slot)+".jsonl");out<<f.rdbuf();}out.close();if(!out)throw std::runtime_error("output write");std::filesystem::rename(output+".tmp",output);return 0;
 }catch(const std::exception&e){std::cerr<<e.what()<<std::endl;return 2;}}
