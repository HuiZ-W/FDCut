#pragma once
// Shared parsing, permutation and stratification only; no workload generator.
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

namespace {
using u64 = std::uint64_t;
struct Endpoint { int x, y; };

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

std::array<int,20> e2_i_schedule(u64 seed) {
  std::array<int,20> values{{1,2,3,4,5,6,7,8,1,2,3,4,5,6,7,8,1,3,5,7}};
  for(std::size_t i=values.size();i>1;--i){
    const std::size_t j=static_cast<std::size_t>(mix(seed+i)%i);
    std::swap(values[i-1],values[j]);
  }
  return values;
}

} // namespace
