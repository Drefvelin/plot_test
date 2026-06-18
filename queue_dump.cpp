#include <algorithm>
#include <iostream>
#include <random>
#include <string>
#include <vector>
int main() {
  std::vector<std::string> q;
  auto add=[&](const char* t,int n){ for(int i=0;i<n;++i) q.push_back(t); };
  add("house",85); add("church",2); add("workshop",60); add("lumber_camp",8);
  add("mine",7); add("fisher_hut",4); add("watermill",3); add("farm",25);
  std::mt19937 rng(12345u);
  std::shuffle(q.begin(), q.end(), rng);
  for (int i=0;i<10;++i) std::cout<<i<<' '<<q[i]<<'\n';
}
