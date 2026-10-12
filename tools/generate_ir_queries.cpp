#include "graph_io.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <numeric>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

extern "C" {
void* fd_screen_prepare(int, int, const int*, const int*);
int fd_screen_pair_type(void*, int, int);
int fd_screen_check(void*, const int*);
void fd_screen_free(void*);
}

namespace {

using u64 = std::uint64_t;

struct Endpoint { int x, y; };
struct Accepted {
  u64 attempt, permutation_seed;
  int pair_id, i_size, x, y;
  std::vector<u64> i_positions;
};
bool grow_i=false;

u64 number(const char* text, const char* message) {
  const std::string value=text;
  if(value.empty()||value.find_first_not_of("0123456789")!=std::string::npos)
    throw std::runtime_error(message);
  return std::stoull(value);
}

u64 json_field(const std::string& line,const std::string& name) {
  const auto key='"'+name+'"';auto pos=line.find(key);
  if(pos==std::string::npos)throw std::runtime_error("endpoint JSONL missing "+name);
  pos=line.find(':',pos+key.size());
  if(pos==std::string::npos)throw std::runtime_error("invalid endpoint JSONL field "+name);
  ++pos;while(pos<line.size()&&(line[pos]==' '||line[pos]=='\t'))++pos;
  const auto begin=pos;while(pos<line.size()&&line[pos]>='0'&&line[pos]<='9')++pos;
  if(begin==pos)throw std::runtime_error("invalid endpoint JSONL field "+name);
  return std::stoull(line.substr(begin,pos-begin));
}

u64 mix(u64 x) {
  x+=0x9e3779b97f4a7c15ULL;
  x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
  x=(x^(x>>27))*0x94d049bb133111ebULL;
  return x^(x>>31);
}

// Eight balanced Feistel rounds form a keyed permutation of the smallest
// even-bit power-of-two domain containing [0,n). Cycle walking restricts that
// permutation exactly to [0,n), without storing an O(n) shuffle table.
u64 permuted_rank(u64 position,u64 n,u64 seed) {
  if(position>=n||!n)throw std::runtime_error("invalid compact permutation input");
  if(n==1)return 0;
  unsigned bits=0;for(u64 top=n-1;top;top>>=1)++bits;
  if(bits&1U)++bits;
  const unsigned half=bits/2;
  const u64 mask=(u64(1)<<half)-1;
  u64 value=position;
  do {
    u64 left=value>>half,right=value&mask;
    for(unsigned round=0;round<8;++round){
      const u64 f=mix(right^mix(seed+round*0x9e3779b97f4a7c15ULL))&mask;
      const u64 next=left^f;left=right;right=next;
    }
    value=(left<<half)|right;
  } while(value>=n);
  return value;
}

int vertex_from_rank(u64 rank,int x,int y) {
  int value=static_cast<int>(rank);
  const int first=std::min(x,y),second=std::max(x,y);
  if(value>=first)++value;
  if(value>=second)++value;
  return value;
}

void set_marks(std::vector<int>& marks,int x,int y,int i_size,u64 r_size,u64 seed) {
  std::fill(marks.begin(),marks.end(),0);
  const u64 universe=marks.size()-2;
  if(i_size<0||static_cast<u64>(i_size)>r_size||r_size>universe)
    throw std::runtime_error("invalid I/R cardinalities");
  for(u64 position=0;position<r_size;++position){
    const int vertex=vertex_from_rank(permuted_rank(position,universe,seed),x,y);
    marks[vertex]=position<static_cast<u64>(i_size)?12:8;
  }
  marks[x]=1;marks[y]=2;
}

std::vector<int> i_vertices(int n,int x,int y,int i_size,u64 seed) {
  std::vector<int> result;result.reserve(i_size);
  for(int position=0;position<i_size;++position)
    result.push_back(vertex_from_rank(permuted_rank(position,n-2,seed),x,y));
  return result;
}

std::vector<Endpoint> read_endpoints(const std::string& path,int n) {
  std::ifstream input(path);if(!input)throw std::runtime_error("cannot open endpoint JSONL");
  std::vector<Endpoint> result;std::string line;std::set<std::pair<int,int>> seen;
  while(std::getline(input,line))if(line.find_first_not_of(" \t\r")!=std::string::npos){
    const auto raw_x=json_field(line,"x"),raw_y=json_field(line,"y");
    if(raw_x>=static_cast<u64>(n)||raw_y>=static_cast<u64>(n)||raw_x==raw_y)
      throw std::runtime_error("endpoint JSONL vertex outside graph");
    Endpoint item{static_cast<int>(raw_x),static_cast<int>(raw_y)};
    if(seen.insert({item.x,item.y}).second)result.push_back(item);
  }
  if(result.size()<20)throw std::runtime_error("need at least 20 unique endpoint pairs");
  return result;
}

std::vector<int> e2_levels(u64 universe) {
  if(universe<9)throw std::runtime_error("E2 requires at least nine non-endpoint variables");
  const u64 guaranteed_max=universe-8;
  std::vector<int> levels;
  for(int k:{16,32,64,128,256})if(static_cast<u64>(k)<=guaranteed_max)levels.push_back(k);
  if(guaranteed_max<256&&(!levels.empty())&&static_cast<u64>(levels.back())!=guaranteed_max)
    levels.push_back(static_cast<int>(guaranteed_max));
  if(levels.empty())levels.push_back(static_cast<int>(guaranteed_max));
  return levels;
}

std::array<int,20> e2_i_schedule(u64 seed) {
  std::array<int,20> values{{1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,1,3,5,7}};
  for(std::size_t i=values.size();i>1;--i){
    const std::size_t j=static_cast<std::size_t>(mix(seed+i)%i);
    std::swap(values[i-1],values[j]);
  }
  return values;
}

void write_row(std::ostream& output,u64 query_id,const Accepted& item,const char* workload,
               u64 universe,u64 r_size,int level_index,int level_value) {
  auto required=i_vertices(static_cast<int>(universe+2),item.x,item.y,item.i_size,
                           item.permutation_seed);
  if(grow_i){
    required.clear();
    for(u64 position:item.i_positions)
      required.push_back(vertex_from_rank(permuted_rank(position,universe,item.permutation_seed),item.x,item.y));
  }
  output<<"{\"query_id\":"<<query_id<<",\"pair_id\":"<<item.pair_id
    <<",\"attempt\":"<<item.attempt<<",\"workload\":\""<<workload
    <<"\",\"level_index\":"<<level_index<<",\"level_value\":"<<level_value
    <<",\"x\":"<<item.x<<",\"y\":"<<item.y<<",\"cost\":1"
    <<",\"i_size\":"<<item.i_size<<",\"r_size\":"<<r_size
    <<",\"k\":"<<(r_size-item.i_size)<<",\"universe_size\":"<<universe
    <<",\"i\":[";
  for(std::size_t index=0;index<required.size();++index){
    if(index)output<<',';
    output<<required[index];
  }
  output<<"],\"i_positions\":[";
  for(std::size_t j=0;j<item.i_positions.size();++j){
    if(j)output<<',';
    output<<item.i_positions[j];
  }
  output<<"],\"r_representation\":{\"scheme\":\"feistel-cyclewalk-v1\""
    <<",\"seed\":"<<item.permutation_seed
    <<",\"domain\":\"natural-vertex-ranks-excluding-x-y\",\"prefix_size\":"<<r_size
    <<"},\"sampling\":\""<<(grow_i?"paired-random-R-feasible-growth-I-v1":"paired-random-ir-conditioned-feasible")<<"\""
    <<",\"screen\":\"fd-compiler-hard-constraints\""
    <<",\"feasibility_certificate\":\"exact-smallest-R-plus-monotonicity\""
    <<",\"feasible\":true,\"nonempty\":true}\n";
}

template<class SmallestR>
std::vector<Accepted> sample_pairs(void* screen,int n,const std::vector<Endpoint>& endpoints,
                                   const std::array<int,20>& i_sizes,u64 seed,u64 max_attempts,
                                   SmallestR smallest_r,const char* workload,const std::string& checkpoint) {
  std::vector<Accepted> accepted(20);std::array<unsigned char,20> complete{};
  std::set<std::pair<int,int>> used;std::vector<int> marks(n);u64 count=0;
  u64 next_ticket=0,checked=0,r_rejected=0,growth_rejected=0;
  if(grow_i&&std::filesystem::exists(checkpoint)){
    std::ifstream saved(checkpoint);u64 saved_seed;int saved_n;std::string saved_mode;
    if(!(saved>>saved_seed>>saved_n>>saved_mode>>next_ticket>>checked>>r_rejected>>growth_rejected)||
       saved_seed!=seed||saved_n!=n||saved_mode!=workload)
      throw std::runtime_error("checkpoint configuration mismatch");
    Accepted a{};
    while(saved>>a.attempt>>a.permutation_seed>>a.pair_id>>a.i_size>>a.x>>a.y){
      if(a.pair_id<0||a.pair_id>=20||complete[a.pair_id]||a.i_size!=i_sizes[a.pair_id])
        throw std::runtime_error("invalid checkpoint pair");
      a.i_positions.resize(a.i_size);
      for(auto& position:a.i_positions)if(!(saved>>position))throw std::runtime_error("truncated checkpoint");
      accepted[a.pair_id]=a;complete[a.pair_id]=1;used.insert({a.x,a.y});++count;
    }
    std::cerr<<workload<<": resumed accepted="<<count<<" next_ticket="<<next_ticket<<'\n';
  }
  auto save=[&](u64 next){
    if(!grow_i)return;
    const auto temporary=checkpoint+".tmp";
    {std::ofstream out(temporary);
     out<<seed<<' '<<n<<' '<<workload<<' '<<next<<' '<<checked<<' '<<r_rejected<<' '<<growth_rejected<<'\n';
     for(int j=0;j<20;++j)if(complete[j]){
       const auto& a=accepted[j];
       out<<a.attempt<<' '<<a.permutation_seed<<' '<<a.pair_id<<' '<<a.i_size<<' '<<a.x<<' '<<a.y;
       for(auto position:a.i_positions)out<<' '<<position;
       out<<'\n';
     }
     out.flush();if(!out)throw std::runtime_error("cannot write checkpoint");}
    std::filesystem::rename(temporary,checkpoint);
  };
  for(u64 ticket=next_ticket;ticket<max_attempts&&count<20;++ticket){
    if(ticket&&ticket%1000==0)
      {save(ticket);std::cerr<<workload<<": progress attempts="<<ticket<<" accepted="<<count
        <<"/20 checked="<<checked<<" R_infeasible="<<r_rejected<<" growth_rejected="<<growth_rejected<<"\n";}
    const int slot=static_cast<int>(ticket%20);if(complete[slot])continue;
    const u64 state=mix(seed^(ticket*0x9e3779b97f4a7c15ULL));
    const auto& endpoint=endpoints[static_cast<std::size_t>(state%endpoints.size())];
    if(used.count({endpoint.x,endpoint.y}))continue;
    const int pair_type=fd_screen_pair_type(screen,endpoint.x,endpoint.y);
    if(pair_type<0)throw std::runtime_error("directed-path precheck failed");
    if(pair_type!=1)continue;
    const u64 permutation_seed=mix(state+0xd1b54a32d192ed03ULL);
    const u64 r_size=smallest_r(i_sizes[slot]);
    set_marks(marks,endpoint.x,endpoint.y,grow_i?0:i_sizes[slot],r_size,permutation_seed);
    ++checked;
    const int status=fd_screen_check(screen,marks.data());
    if(status==2)throw std::runtime_error("feasibility screen rejected generated marks");
    if(status==1){++r_rejected;continue;}
    std::vector<u64> positions;
    if(grow_i){
      // R's random order also supplies the random candidate order for growing I.
      // A rejected inclusion cannot become feasible after more requirements are added.
      for(u64 position=0;position<r_size&&positions.size()<static_cast<std::size_t>(i_sizes[slot]);++position){
        const int v=vertex_from_rank(permuted_rank(position,n-2,permutation_seed),endpoint.x,endpoint.y);
        marks[v]=12;
        const int test=fd_screen_check(screen,marks.data());
        if(test==2)throw std::runtime_error("growth feasibility check failed");
        if(test==0)positions.push_back(position);else marks[v]=8;
      }
      if(positions.size()!=static_cast<std::size_t>(i_sizes[slot])){++growth_rejected;continue;}
      if(fd_screen_check(screen,marks.data())!=0)throw std::runtime_error("final I/R feasibility check failed");
    }
    accepted[slot]={ticket+1,permutation_seed,slot,i_sizes[slot],endpoint.x,endpoint.y,positions};
    complete[slot]=1;used.insert({endpoint.x,endpoint.y});++count;
    save(ticket+1);
    std::cerr<<workload<<": accepted pair "<<count<<"/20 slot="<<slot
      <<" attempt="<<ticket+1<<" I="<<i_sizes[slot]<<"\n";
  }
  if(count!=20)throw std::runtime_error(std::string(workload)+
    ": maximum attempts reached before 20 paired feasible queries");
  return accepted;
}

void publish_e2(const std::string& output_path,void* screen,int n,
                const std::vector<Endpoint>& endpoints,u64 seed,u64 max_attempts) {
  if(std::filesystem::exists(output_path))throw std::runtime_error("refusing existing E2 output");
  const u64 universe=static_cast<u64>(n)-2;const auto levels=e2_levels(universe);
  const auto schedule=e2_i_schedule(seed);
  const auto accepted=sample_pairs(screen,n,endpoints,schedule,seed,max_attempts,
    [&](int i_size){return static_cast<u64>(i_size+levels.front());},"E2",output_path+".checkpoint");
  const std::string temporary=output_path+".partial";
  {std::ofstream output(temporary,std::ios::trunc);if(!output)throw std::runtime_error("cannot open E2 output");
   u64 query_id=0;
   for(std::size_t level=0;level<levels.size();++level)for(const auto& item:accepted)
     write_row(output,query_id++,item,"E2",universe,item.i_size+levels[level],
               static_cast<int>(level),levels[level]);
   if(!output)throw std::runtime_error("cannot write E2 output");}
  std::filesystem::rename(temporary,output_path);
  std::cerr<<"E2: complete rows="<<20*levels.size()<<" levels=";
  for(int value:levels)std::cerr<<value<<',';
  std::cerr<<"\n";
}

void publish_e3(const std::string& output_path,void* screen,int n,
                const std::vector<Endpoint>& endpoints,u64 seed,u64 max_attempts) {
  if(std::filesystem::exists(output_path))throw std::runtime_error("refusing existing E3 output");
  const u64 universe=static_cast<u64>(n)-2;
  const std::array<int,5> percentages{{10,20,30,40,50}};
  std::array<u64,5> sizes{};
  for(std::size_t index=0;index<sizes.size();++index)
    sizes[index]=std::max<u64>(8,universe*percentages[index]/100);
  std::array<int,20> schedule{};schedule.fill(8);
  const auto accepted=sample_pairs(screen,n,endpoints,schedule,seed,max_attempts,
    [&](int){return sizes.front();},"E3",output_path+".checkpoint");
  const std::string temporary=output_path+".partial";
  {std::ofstream output(temporary,std::ios::trunc);if(!output)throw std::runtime_error("cannot open E3 output");
   u64 query_id=0;
   for(std::size_t level=0;level<percentages.size();++level)for(const auto& item:accepted)
     write_row(output,query_id++,item,"E3",universe,sizes[level],
               static_cast<int>(level),percentages[level]);
   if(!output)throw std::runtime_error("cannot write E3 output");}
  std::filesystem::rename(temporary,output_path);
  std::cerr<<"E3: complete rows=100 percentages=10,20,30,40,50\n";
}

void selftest() {
  for(u64 n=1;n<=500;++n)for(u64 seed:{0ULL,1ULL,7ULL,20260923ULL}){
    std::vector<unsigned char> seen(n);
    for(u64 position=0;position<n;++position){
      const auto value=permuted_rank(position,n,seed);
      if(value>=n||seen[value])throw std::runtime_error("compact permutation is not bijective");
      seen[value]=1;
    }
  }
  for(int n:{10,71,96,673})for(int x=0;x<n;++x)for(int y=0;y<n;++y)if(x!=y){
    std::vector<unsigned char> seen(n);
    for(u64 rank=0;rank<static_cast<u64>(n-2);++rank){
      const int value=vertex_from_rank(rank,x,y);
      if(value<0||value>=n||value==x||value==y||seen[value])
        throw std::runtime_error("endpoint-excluding rank map is not bijective");
      seen[value]=1;
    }
  }
  std::cout<<"{\"status\":\"PASS\",\"permutation\":\"feistel-cyclewalk-v1\"}\n";
}

} // namespace

int main(int argc,char** argv) {
  try {
    if(argc==2&&std::string(argv[1])=="selftest"){selftest();return 0;}
    if(argc!=7&&argc!=8){
      std::cerr<<"usage: fdquerygen GRAPH.bin ENDPOINTS.jsonl E2.jsonl E3.jsonl SEED MAX_ATTEMPTS\n"
               <<"       fdquerygen selftest\n";return 2;
    }
    const u64 seed=number(argv[5],"invalid seed");
    if(argc==8){
      if(std::string(argv[7])!="grow-i")throw std::runtime_error("unknown sampling mode");
      grow_i=true;
    }
    const u64 max_attempts=number(argv[6],"invalid maximum attempts");
    if(max_attempts<20)throw std::runtime_error("maximum attempts must be at least 20");
    std::vector<int> tails,heads;int nodes=0;u64 edge_count=0;
    fdio::read_graph_bin(argv[1],[&](u64 n,u64 m){
      if(n>static_cast<u64>(std::numeric_limits<int>::max()-2)||
         m>static_cast<u64>(std::numeric_limits<int>::max()))
        throw std::runtime_error("graph exceeds signed 32-bit feasibility-screen limit");
      nodes=static_cast<int>(n);edge_count=m;tails.reserve(static_cast<std::size_t>(m));
      heads.reserve(static_cast<std::size_t>(m));
    },[&](u64 u,u64 v){tails.push_back(static_cast<int>(u));heads.push_back(static_cast<int>(v));});
    if(nodes<11)throw std::runtime_error("graph is too small for requested I/R workloads");
    auto endpoints=read_endpoints(argv[2],nodes);
    std::unique_ptr<void,decltype(&fd_screen_free)> screen(
      fd_screen_prepare(nodes,static_cast<int>(edge_count),tails.data(),heads.data()),fd_screen_free);
    if(!screen)throw std::runtime_error("cannot prepare DAG feasibility screen");
    if(!std::filesystem::exists(argv[3]))publish_e2(argv[3],screen.get(),nodes,endpoints,mix(seed^0xe2),max_attempts);
    if(!std::filesystem::exists(argv[4]))publish_e3(argv[4],screen.get(),nodes,endpoints,mix(seed^0xe3),max_attempts);
    std::cout<<"{\"status\":\"PASS\",\"nodes\":"<<nodes<<",\"edges\":"<<edge_count
      <<",\"e2\":\""<<argv[3]<<"\",\"e3\":\""<<argv[4]<<"\"}\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"error: "<<error.what()<<'\n';return 2;
  }
}
