// Synthetic stress graphs; no claim that these are observed causal networks.
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
using U = uint64_t;
U mix(U x) {
  x += 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
U number(const char* text) {
  std::string s(text);size_t end=0;
  if(s.empty() || s.find_first_not_of("0123456789")!=std::string::npos)
    throw std::runtime_error("expected unsigned integer");
  U x=std::stoull(s,&end);if(end!=s.size())throw std::runtime_error("invalid integer");return x;
}
int main(int argc, char** argv) {
  try {
    if (argc != 8)
      throw std::runtime_error("usage: generate_large_dag uniform|hub N M LAYERS SEED OUTPUT_DIR BUFFER_MB");
    std::string family(argv[1]);
    U n=number(argv[2]),m=number(argv[3]),layers=number(argv[4]),seed=number(argv[5]);
    U buffer_mb=number(argv[7]);
    if((family!="uniform" && family!="hub") || layers<2 || n==0 || n%layers || buffer_mb<1 || buffer_mb>256)
      throw std::runtime_error("invalid family, layer layout, or buffer size");
    if(m>UINT64_MAX/16)throw std::runtime_error("edge file size exceeds uint64 range");
    U width=n/layers, sources=n-width;
    U d=m/sources, extra=m%sources;
    if(d>width || (d==width && extra))throw std::runtime_error("too many edges for adjacent-layer DAG");
    // Keep the per-source sorting workspace bounded and avoid accidental dense requests.
    if(d+(extra!=0)>1000000)throw std::runtime_error("fanout exceeds supported bounded workspace");
    std::filesystem::path dest(argv[6]),tmp(dest.string()+".partial");
    if(std::filesystem::exists(dest)||std::filesystem::exists(tmp))throw std::runtime_error("output exists");
    std::filesystem::create_directories(tmp);
    std::vector<char> buffer(buffer_mb*1024*1024);
    std::ofstream out(tmp/"edges.u64le",std::ios::binary);
    out.exceptions(std::ios::failbit|std::ios::badbit);
    size_t used=0;
    auto put=[&](U x) {
      if(used+8>buffer.size()){out.write(buffer.data(),used);used=0;}
      for(int i=0;i<8;++i)buffer[used++]=char(x>>(8*i));
    };
    std::vector<U> targets;
    for(U u=0;u<sources;++u) {
      U count=d+(u<extra), next=(u/width+1)*width;
      U domain=width;
      if(family=="hub" && mix(u^seed)%5!=0)
        domain=std::min(width,std::max(count,std::max(U(1),width/1000)));
      U offset=mix(u^seed^0x243f6a8885a308d3ULL)%domain;
      U step=mix(u^seed^0x13198a2e03707344ULL)%domain;
      while(std::gcd(step,domain)!=1)step=(step+1)%domain;
      targets.clear();
      for(U j=0;j<count;++j) {
        targets.push_back(next+offset);
        // Modular addition without integer overflow.
        offset = offset >= domain-step ? offset-(domain-step) : offset+step;
      }
      std::sort(targets.begin(),targets.end());
      for(U v:targets){put(u);put(v);}
      if(u && u%10000000==0)std::cerr<<"processed_sources="<<u<<"/"<<sources<<'\n';
    }
    out.write(buffer.data(),used);out.close();
    if(m>UINT64_MAX/16 || std::filesystem::file_size(tmp/"edges.u64le")!=16*m)
      throw std::runtime_error("edge byte count mismatch");
    std::ofstream meta(tmp/"stats.json");meta.exceptions(std::ios::failbit|std::ios::badbit);
    meta<<"{\n  \"format\": \"synthetic-directed-u64le-pairs-v1\",\n  \"node_labels\": \"implicit integer IDs 0..n-1, including isolates\",\n  \"synthetic\": true,\n  \"family\": \"layered-"<<family<<"\",\n  \"nodes\": "<<n<<",\n  \"edges\": "<<m<<",\n  \"layers\": "<<layers<<",\n  \"seed\": "<<seed<<",\n  \"is_dag\": true,\n  \"acyclicity_certificate\": \"every edge increases layer by exactly one; ID order is topological\",\n  \"duplicate_free_certificate\": \"each source uses distinct positions of a coprime modular cycle\",\n  \"complete\": true\n}\n";
    meta.close();std::filesystem::rename(tmp,dest);
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
