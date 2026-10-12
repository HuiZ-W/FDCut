#include "frontdoor.hpp"
#include "method_names.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <random>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <cstdint>
#include <cstdlib>

extern "C" {
void* fd_screen_prepare(int,int,const int*,const int*);
int fd_screen_pair_type(void*,int,int);
int fd_screen_check(void*,const int*);
void fd_screen_free(void*);
}

int main(int argc,char** argv) {
  try {
    const char* skip_env=std::getenv("FD_SKIP_POSTVALIDATION");
    const bool skip_postvalidation=skip_env && std::string(skip_env)=="1";
    std::string command=argc>1?argv[1]:"help";
    if(command=="capacity"&&argc==4){
      auto count=[](const char* text){
        std::string s=text;
        if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
          throw std::runtime_error("invalid nonnegative graph count");
        size_t used=0;auto value=std::stoull(s,&used);
        if(used!=s.size())throw std::runtime_error("invalid nonnegative graph count");
        return static_cast<std::uint64_t>(value);
      };
      auto nodes=count(argv[2]),edges=count(argv[3]);
      auto reason=fd::input_capacity_error(nodes,edges);
      std::cout<<"{\"nodes\":"<<nodes<<",\"edges\":"<<edges
        <<",\"supported\":"<<(reason.empty()?"true":"false")
        <<",\"reason\":\""<<reason<<"\"}\n";
      return 0;
    }
    if(command=="solve"&&(argc==4||argc==6)){
      auto begin=std::chrono::steady_clock::now();std::ifstream input(argv[2]);
      if(!input)throw std::runtime_error("cannot open input");
      auto q=fd::read(input);double read_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
      const auto selected_method=fd::parse_cli_method(argv[3]);
      const auto& method=selected_method.internal;const auto& method_label=selected_method.label;
      bool enumeration=method=="enum"||method=="enum-prune"||method=="jenum"||method=="wenum"||method=="wbb";
      if(argc==6&&!enumeration)throw std::runtime_error("limits apply only to enumeration");
      auto limit=[](const char* text){std::string s=text; if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid nonnegative limit");return std::stoull(s);};
      auto a=enumeration?fd::enumerate(q,method,argc==6?limit(argv[4]):0,argc==6?limit(argv[5]):0):fd::solve(q,method);if(!skip_postvalidation&&a.feasible&&!fd::valid_fd(q,a.selected))throw std::runtime_error("original FD witness check failed");
      std::cout<<"{\"read_s\":"<<read_s<<",\"post_validation\":\""<<(skip_postvalidation?"skipped":"enabled")<<"\",\"query\":";fd::print(std::cout,a,method_label);std::cout<<"}\n";return 0;
    }
    if(command=="solve-bin-full"&&(argc==7||argc==9)){
      auto vertex=[](const char* text){
        std::string s=text;
        if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
          throw std::runtime_error("invalid nonnegative vertex ID");
        auto value=std::stoull(s);
        if(value>static_cast<unsigned long long>(std::numeric_limits<int>::max()))
          throw std::runtime_error("vertex ID exceeds signed 32-bit limit");
        return static_cast<int>(value);
      };
      auto cost=[](const char* text){
        std::string s=text;
        if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
          throw std::runtime_error("invalid nonnegative cost");
        auto value=std::stoull(s);
        if(value>=1ULL<<30)throw std::runtime_error("cost exceeds fast path");
        return static_cast<fd::i64>(value);
      };
      auto limit=[](const char* text){std::string s=text; if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)throw std::runtime_error("invalid nonnegative limit");return std::stoull(s);};
      auto begin=std::chrono::steady_clock::now();
      auto q=fd::read_binary_full_domain(argv[2],vertex(argv[3]),vertex(argv[4]),cost(argv[5]));
      double read_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
      const auto selected_method=fd::parse_cli_method(argv[6]);
      const auto& method=selected_method.internal;const auto& method_label=selected_method.label;
      bool enumeration=method=="enum"||method=="enum-prune"||method=="jenum"||method=="wenum"||method=="wbb";
      if(argc==9&&!enumeration)throw std::runtime_error("limits apply only to enumeration");
      auto a=enumeration?fd::enumerate(q,method,argc==9?limit(argv[7]):0,argc==9?limit(argv[8]):0):fd::solve(q,method);
      if(!skip_postvalidation&&a.feasible&&!fd::valid_fd(q,a.selected))throw std::runtime_error("original FD witness check failed");
      std::cout<<"{\"read_s\":"<<read_s<<",\"post_validation\":\""<<(skip_postvalidation?"skipped":"enabled")<<"\",\"query\":";fd::print(std::cout,a,method_label);std::cout<<"}\n";return 0;
    }
    if(command=="solve-bin-batch"&&argc==6){
      struct BatchQuery { std::uint64_t id; int x,y; fd::i64 cost; };
      auto field=[](const std::string& line,const std::string& name){
        auto key='"'+name+'"';auto pos=line.find(key);
        if(pos==std::string::npos)throw std::runtime_error("query JSONL missing "+name);
        pos=line.find(':',pos+key.size());
        if(pos==std::string::npos)throw std::runtime_error("invalid query JSONL field "+name);
        ++pos;while(pos<line.size()&&(line[pos]==' '||line[pos]=='\t'))++pos;
        auto begin=pos;while(pos<line.size()&&line[pos]>='0'&&line[pos]<='9')++pos;
        if(begin==pos)throw std::runtime_error("invalid nonnegative query JSONL field "+name);
        return std::stoull(line.substr(begin,pos-begin));
      };
      std::ifstream queries_input(argv[3]);
      if(!queries_input)throw std::runtime_error("cannot open query JSONL");
      std::vector<BatchQuery> queries;std::string line;
      while(std::getline(queries_input,line))if(line.find_first_not_of(" \t\r")!=std::string::npos){
        auto id=field(line,"query_id"),x=field(line,"x"),y=field(line,"y"),cost=field(line,"cost");
        if(x>static_cast<unsigned long long>(std::numeric_limits<int>::max())||
           y>static_cast<unsigned long long>(std::numeric_limits<int>::max())||x==y)
          throw std::runtime_error("invalid query JSONL X/Y vertices");
        if(cost>=1ULL<<30)throw std::runtime_error("query JSONL cost exceeds fast path");
        queries.push_back({id,static_cast<int>(x),static_cast<int>(y),static_cast<fd::i64>(cost)});
      }
      if(queries.empty())throw std::runtime_error("empty query JSONL");
      const auto selected_method=fd::parse_cli_method(argv[5]);
      const auto& method=selected_method.internal;const auto& method_label=selected_method.label;
      if(method!="base"&&method!="sweep"&&method!="component")
        throw std::runtime_error("batch solving requires FDCut (advanced: base or sweep)");
      if(std::filesystem::exists(argv[4]))throw std::runtime_error("refusing existing batch output");
      auto begin=std::chrono::steady_clock::now();
      auto q=fd::read_binary_full_domain(argv[2],queries[0].x,queries[0].y,queries[0].cost);
      double read_s=std::chrono::duration<double>(std::chrono::steady_clock::now()-begin).count();
      std::ofstream output(argv[4]);if(!output)throw std::runtime_error("cannot open batch output");
      int previous_x=queries[0].x,previous_y=queries[0].y;fd::i64 previous_cost=queries[0].cost;
      double solver_s=0;
      for(std::size_t index=0;index<queries.size();++index){
        const auto& item=queries[index];
        if(item.x>=q.n||item.y>=q.n)throw std::runtime_error("query X/Y vertex outside binary graph");
        q.marks[previous_x]=q.marks[previous_y]=8;
        if(item.cost!=previous_cost)std::fill(q.weights.begin(),q.weights.end(),item.cost);
        else q.weights[previous_x]=q.weights[previous_y]=item.cost;
        q.marks[item.x]=1;q.marks[item.y]=2;q.weights[item.x]=q.weights[item.y]=0;
        previous_x=item.x;previous_y=item.y;previous_cost=item.cost;
        fd::Answer answer;
        try { answer=fd::solve(q,method); }
        catch(const std::exception& error){
          throw std::runtime_error("query_id="+std::to_string(item.id)+": "+error.what());
        }
        solver_s+=answer.total_s;
        output<<"{\"query_id\":"<<item.id<<",\"x\":"<<item.x<<",\"y\":"<<item.y
          <<",\"cost\":"<<item.cost<<",\"graph_read_s\":"<<read_s<<",\"query\":";
        fd::print(output,answer,method_label,false);output<<"}\n"<<std::flush;
        std::cerr<<"completed "<<index+1<<'/'<<queries.size()<<" query_id="<<item.id
          <<" feasible="<<(answer.feasible?"true":"false")<<" total_s="<<answer.total_s<<'\n';
      }
      std::cout<<"{\"status\":\"PASS\",\"queries\":"<<queries.size()
        <<",\"method\":\""<<method_label<<"\",\"graph_read_s\":"<<read_s
        <<",\"solver_s\":"<<solver_s<<"}\n";
      return 0;
    }
    if(command=="sample-bin-full"&&(argc==9||argc==10||argc==11)){
      auto number=[](const char* text,const char* message){
        std::string s=text;
        if(s.empty()||s.find_first_not_of("0123456789")!=std::string::npos)
          throw std::runtime_error(message);
        return std::stoull(s);
      };
      auto count=number(argv[4],"invalid positive query count");
      auto seed=number(argv[5],"invalid random seed");
      auto max_attempts=number(argv[6],"invalid maximum attempt count");
      auto cost=number(argv[7],"invalid nonnegative cost");
      if(!count||count>10000||max_attempts<count)
        throw std::runtime_error("invalid query count or maximum attempt count");
      if(cost>=(1ULL<<30))throw std::runtime_error("cost exceeds fast path");
      const auto selected_method=fd::parse_cli_method(argv[8]);
      const auto& method=selected_method.internal;const auto& method_label=selected_method.label;
      if(method!="base"&&method!="sweep"&&method!="component")
        throw std::runtime_error("sampling requires FDCut (advanced: base or sweep)");
      std::string sampling=argc>=10?argv[9]:"any-x";
      if(sampling!="any-x"&&sampling!="root-x"&&sampling!="random-pair")
        throw std::runtime_error("sampling mode must be any-x, root-x, or random-pair");
      auto threads=argc==11?number(argv[10],"invalid thread count"):1;
      if(!threads||threads>64)throw std::runtime_error("thread count must be between 1 and 64");
      if(threads>1&&sampling!="random-pair")
        throw std::runtime_error("parallel sampling currently requires random-pair mode");
      if(std::filesystem::exists(argv[3]))throw std::runtime_error("refusing existing sample output");
      auto q=fd::read_binary_full_domain(argv[2],0,1,static_cast<fd::i64>(cost));
      q.marks[0]=q.marks[1]=8;q.weights[0]=q.weights[1]=static_cast<fd::i64>(cost);
      if(q.u.empty()||!std::is_sorted(q.u.begin(),q.u.end()))
        throw std::runtime_error("sampling requires edges sorted by source");
      std::unique_ptr<void,decltype(&fd_screen_free)> screen(
        fd_screen_prepare(q.n,static_cast<int>(q.u.size()),q.u.data(),q.v.data()),fd_screen_free);
      if(!screen)throw std::runtime_error("cannot prepare feasibility screen");
      std::ofstream output(argv[3]);if(!output)throw std::runtime_error("cannot open sample output");
      std::mt19937_64 rng(seed);std::set<std::pair<int,int>> seen;
      std::uniform_int_distribution<std::uint64_t> first_edge(0,q.u.size()-1);
      std::uniform_int_distribution<int> vertex(0,q.n-1);
      std::vector<std::pair<std::size_t,std::size_t>> root_ranges;
      std::vector<std::uint64_t> root_cumulative;
      if(sampling=="root-x"){
        std::vector<unsigned char> has_parent(q.n);
        for(int v:q.v)has_parent[v]=1;
        std::uint64_t total=0;
        for(std::size_t begin=0;begin<q.u.size();){
          std::size_t end=begin+1;
          while(end<q.u.size()&&q.u[end]==q.u[begin])++end;
          if(!has_parent[q.u[begin]]){
            root_ranges.push_back({begin,end});total+=end-begin;root_cumulative.push_back(total);
          }
          begin=end;
        }
        if(root_ranges.empty())throw std::runtime_error("root-x sampling found no root with outgoing edges");
      }
      if(sampling=="random-pair"&&threads>1){
        std::atomic<std::uint64_t> next{0},processed{0},accepted{0},no_path{0},direct{0},
          fd_infeasible{0},duplicate{0};
        std::mutex result_mutex,error_mutex;
        std::exception_ptr worker_error;
        auto mix=[](std::uint64_t value){
          value+=0x9e3779b97f4a7c15ULL;
          value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
          value=(value^(value>>27))*0x94d049bb133111ebULL;
          return value^(value>>31);
        };
        auto worker=[&](){
          try {
            std::vector<int> marks(q.n,8);
            while(accepted.load(std::memory_order_relaxed)<count){
              auto ticket=next.fetch_add(1,std::memory_order_relaxed);
              if(ticket>=max_attempts)break;
              auto state=mix(seed^(ticket*0x9e3779b97f4a7c15ULL));
              int x=static_cast<int>(state%static_cast<std::uint64_t>(q.n));
              state=mix(state+1);
              int y=static_cast<int>(state%static_cast<std::uint64_t>(q.n));
              if(y==x)y=(y+1)%q.n;
              int pair_type=fd_screen_pair_type(screen.get(),x,y);
              if(pair_type<0)throw std::runtime_error("directed-path precheck failed");
              if(pair_type==0)no_path.fetch_add(1,std::memory_order_relaxed);
              else if(pair_type==2)direct.fetch_add(1,std::memory_order_relaxed);
              else {
                marks[x]=1;marks[y]=2;
                int status=fd_screen_check(screen.get(),marks.data());
                marks[x]=marks[y]=8;
                if(status==1)fd_infeasible.fetch_add(1,std::memory_order_relaxed);
                else if(status!=0)throw std::runtime_error("feasibility screen failed");
                else {
                  std::lock_guard<std::mutex> lock(result_mutex);
                  if(accepted.load(std::memory_order_relaxed)<count){
                    if(!seen.insert({x,y}).second)duplicate.fetch_add(1,std::memory_order_relaxed);
                    else {
                      auto query_id=accepted.fetch_add(1,std::memory_order_relaxed);
                      output<<"{\"query_id\":"<<query_id<<",\"attempt\":"<<ticket+1
                        <<",\"x\":"<<x<<",\"y\":"<<y<<",\"cost\":"<<cost
                        <<",\"method\":\""<<method_label<<"\",\"sampling\":\"random-pair\""
                        <<",\"threads\":"<<threads<<",\"nonempty\":true"
                        <<",\"screen\":\"fd-compiler-hard-constraints\",\"feasible\":true}\n"<<std::flush;
                      std::cerr<<"accepted "<<query_id+1<<'/'<<count<<" at ticket "<<ticket+1<<'\n';
                    }
                  }
                }
              }
              auto done=processed.fetch_add(1,std::memory_order_relaxed)+1;
              if(!(done%100000))std::cerr<<"progress: processed="<<done
                <<" accepted="<<accepted.load()<<" no_path="<<no_path.load()
                <<" direct="<<direct.load()<<" fd_infeasible="<<fd_infeasible.load()
                <<" duplicate="<<duplicate.load()<<'\n';
            }
          }catch(...){
            std::lock_guard<std::mutex> lock(error_mutex);
            if(!worker_error)worker_error=std::current_exception();
            accepted.store(count,std::memory_order_relaxed);
          }
        };
        std::vector<std::thread> workers;
        for(std::uint64_t worker_id=0;worker_id<threads;++worker_id)
          workers.emplace_back(worker);
        for(auto& thread:workers)thread.join();
        if(worker_error)std::rethrow_exception(worker_error);
        auto accepted_count=accepted.load();
        std::cerr<<"summary: accepted="<<accepted_count<<" processed="<<processed.load()
          <<" issued="<<std::min<std::uint64_t>(next.load(),static_cast<std::uint64_t>(max_attempts))
          <<" no_path="<<no_path.load()
          <<" direct="<<direct.load()<<" fd_infeasible="<<fd_infeasible.load()
          <<" duplicate="<<duplicate.load()<<" threads="<<threads<<'\n';
        if(accepted_count!=count)
          throw std::runtime_error("maximum attempts reached before enough feasible queries");
        return 0;
      }
      std::uint64_t accepted=0,attempt=0,no_path=0,direct=0,fd_infeasible=0,duplicate=0;
      while(accepted<count&&attempt<max_attempts){
        ++attempt;
        if(!(attempt%100000))std::cerr<<"progress: attempts="<<attempt<<" accepted="<<accepted
          <<" no_path="<<no_path<<" direct="<<direct<<" fd_infeasible="<<fd_infeasible
          <<" duplicate="<<duplicate<<'\n';
        std::size_t e=0;int x,mediator=-1,y;
        if(sampling=="random-pair"){
          x=vertex(rng);do y=vertex(rng);while(y==x);
          int pair_type=fd_screen_pair_type(screen.get(),x,y);
          if(pair_type<0)throw std::runtime_error("directed-path precheck failed");
          if(pair_type==0){++no_path;continue;}
          if(pair_type==2){++direct;continue;}
        }else if(sampling=="root-x"){
          std::uniform_int_distribution<std::uint64_t> root_edge(0,root_cumulative.back()-1);
          auto ticket=root_edge(rng);
          auto range=static_cast<std::size_t>(std::upper_bound(
            root_cumulative.begin(),root_cumulative.end(),ticket)-root_cumulative.begin());
          auto before=range?root_cumulative[range-1]:0;
          e=root_ranges[range].first+static_cast<std::size_t>(ticket-before);
        }else e=static_cast<std::size_t>(first_edge(rng));
        if(sampling!="random-pair"){
          x=q.u[e];mediator=q.v[e];
          auto begin=std::lower_bound(q.u.begin(),q.u.end(),mediator);
          auto end=std::upper_bound(begin,q.u.end(),mediator);
          if(begin==end)continue;
          std::uniform_int_distribution<std::uint64_t> second_edge(
            static_cast<std::uint64_t>(begin-q.u.begin()),
            static_cast<std::uint64_t>(end-q.u.begin()-1));
          y=q.v[static_cast<std::size_t>(second_edge(rng))];
          if(x==mediator||mediator==y||x==y)continue;
        }
        if(seen.count({x,y})){++duplicate;continue;}
        q.marks[x]=1;q.marks[y]=2;q.weights[x]=q.weights[y]=0;
        int status=fd_screen_check(screen.get(),q.marks.data());
        q.marks[x]=q.marks[y]=8;
        q.weights[x]=q.weights[y]=static_cast<fd::i64>(cost);
        if(status==1){++fd_infeasible;continue;}
        if(status!=0)throw std::runtime_error("feasibility screen failed");
        seen.insert({x,y});
        output<<"{\"query_id\":"<<accepted<<",\"attempt\":"<<attempt
          <<",\"x\":"<<x;
        if(mediator>=0)output<<",\"mediator\":"<<mediator;
        output<<",\"y\":"<<y<<",\"cost\":"<<cost<<",\"method\":\""<<method_label
          <<"\",\"sampling\":\""<<sampling
          <<"\",\"nonempty\":"<<(sampling=="random-pair"?"true":"false")
          <<",\"screen\":\"fd-compiler-hard-constraints\",\"feasible\":true}\n"<<std::flush;
        ++accepted;
        std::cerr<<"accepted "<<accepted<<'/'<<count<<" after "<<attempt<<" attempts\n";
      }
      std::cerr<<"summary: accepted="<<accepted<<" attempts="<<attempt
        <<" no_path="<<no_path<<" direct="<<direct<<" fd_infeasible="<<fd_infeasible
        <<" duplicate="<<duplicate<<'\n';
      if(accepted!=count)throw std::runtime_error("maximum attempts reached before enough feasible queries");
      return 0;
    }
    if(command=="generate"&&argc==7){
      auto q=fd::generate(std::stoi(argv[3]),std::stoi(argv[4]),std::stoul(argv[5]),argv[6]);
      std::ofstream out(argv[2]);if(!out)throw std::runtime_error("cannot open output");fd::write(out,q);return 0;
    }
    if(command=="benchmark"&&argc==7){
      int variables=std::stoi(argv[2]),width=std::stoi(argv[3]),repeats=std::stoi(argv[5]);
      unsigned seed=std::stoul(argv[4]);std::string family=argv[6];
      if(repeats<1||repeats>10000)throw std::runtime_error("invalid repeat count");
      std::vector<std::string> methods={"base","sweep","component"};
      auto warm=fd::generate(32,4,1,"middle");for(auto& m:methods)fd::solve(warm,m);
      auto q=fd::generate(variables,width,seed,family);std::mt19937 rng(seed);
      bool first=true;fd::i64 optimum=0;
      for(int r=0;r<repeats;++r){std::shuffle(methods.begin(),methods.end(),rng);
        for(auto& method:methods){
          auto a=fd::solve(q,method);
          if(!a.feasible||!fd::valid_fd(q,a.selected))throw std::runtime_error("benchmark original FD oracle failed");
          if(first){optimum=a.value;first=false;}else if(optimum!=a.value)throw std::runtime_error("methods disagree");
          std::cout<<"{\"family\":\""<<family<<"\",\"variables\":"<<variables<<",\"width\":"<<width
            <<",\"seed\":"<<seed<<",\"repeat\":"<<r<<",\"input_nodes\":"<<q.n<<",\"input_edges\":"<<q.u.size()<<",\"query\":";
          fd::print(std::cout,a,method,false);std::cout<<"}\n"<<std::flush;
        }
      }return 0;
    }
    if(command=="selftest"){
      int count=0;
      for(auto f:{"random","middle","corridor","joined"})for(unsigned seed:{1,7,23}){
        auto q=fd::generate(64,8,seed,f);fd::i64 expected=-1;
        std::unique_ptr<void,decltype(&fd_screen_free)> screen(
          fd_screen_prepare(q.n,static_cast<int>(q.u.size()),q.u.data(),q.v.data()),fd_screen_free);
        if(!screen||fd_screen_check(screen.get(),q.marks.data())!=0)
          throw std::runtime_error("selftest feasibility screen rejected a feasible query");
        for(auto m:{"base","sweep","component"}){
          auto a=fd::solve(q,m);if(!a.feasible||!fd::valid_fd(q,a.selected))throw std::runtime_error("selftest FD failure");
          if(expected<0)expected=a.value;else if(expected!=a.value)throw std::runtime_error("selftest costs differ");++count;
        }
        auto screened=q;screened.marks.assign(q.n,8);screened.weights.assign(q.n,1);
        std::unique_ptr<void,decltype(&fd_screen_free)> batch_screen(
          fd_screen_prepare(q.n,static_cast<int>(q.u.size()),q.u.data(),q.v.data()),fd_screen_free);
        if(!batch_screen)throw std::runtime_error("selftest feasibility screen preparation failed");
        for(int sample=0;sample<128;++sample){
          int x=(sample*17+static_cast<int>(seed))%q.n;
          int y=(sample*29+7+static_cast<int>(seed))%q.n;if(y==x)y=(y+1)%q.n;
          screened.marks[x]=1;screened.marks[y]=2;screened.weights[x]=screened.weights[y]=0;
          int status=fd_screen_check(batch_screen.get(),screened.marks.data());
          auto exact=fd::solve(screened,"component");
          if(status>1||(status==0)!=exact.feasible)
            throw std::runtime_error("selftest feasibility screen disagrees with exact solver");
          screened.marks[x]=screened.marks[y]=8;screened.weights[x]=screened.weights[y]=1;
        }
      }
      auto impossible=fd::generate(64,8,1,"middle");impossible.u.push_back(0);impossible.v.push_back(1);
      std::unique_ptr<void,decltype(&fd_screen_free)> impossible_screen(
        fd_screen_prepare(impossible.n,static_cast<int>(impossible.u.size()),impossible.u.data(),impossible.v.data()),fd_screen_free);
      if(!impossible_screen||fd_screen_check(impossible_screen.get(),impossible.marks.data())!=1||
         fd::solve(impossible,"component").feasible)
        throw std::runtime_error("selftest feasibility screen accepted an impossible query");
      std::cout<<"{\"status\":\"PASS\",\"native_configs\":"<<count<<"}\n";return 0;
    }
    std::cout<<"Exact and Scalable Minimum-Cost Front-Door on Causal Graphs\n"
      <<"fdtool solve INPUT.fdg FDCut|Brute|Prune|ListJ|ListW|PruneW [MAX_STATES TIMEOUT_MS]\n"
      <<"fdtool solve-bin-full GRAPH.bin X Y COST FDCut|Brute|Prune|ListJ|ListW|PruneW [MAX_STATES TIMEOUT_MS]\n"
      <<"fdtool solve-bin-batch GRAPH.bin QUERIES.jsonl OUTPUT.jsonl FDCut\n"
      <<"fdtool sample-bin-full GRAPH.bin OUTPUT.jsonl COUNT SEED MAX_ATTEMPTS COST FDCut [any-x|root-x|random-pair] [THREADS]\n"
      <<"fdtool generate OUTPUT.fdg VARIABLES WIDTH SEED random|middle|corridor|joined\n"
      <<"fdtool benchmark VARIABLES WIDTH SEED REPEATS FAMILY\n"
      <<"fdtool capacity NODES EDGES\n"
      <<"fdtool selftest\n"
      <<"Paper method names are case-insensitive; legacy aliases remain supported.\n"
      <<"Advanced FDCut configurations: base, sweep.\n";
    return command=="help"||command=="--help"?0:2;
  }catch(const std::exception& e){std::cerr<<"error: "<<e.what()<<'\n';return 2;}
}
