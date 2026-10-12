#include "../tools/query_common.hpp"

namespace {
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
int main() { selftest(); }
