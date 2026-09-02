#include "renderer.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>
int main() {
  try {
    const auto start=std::chrono::steady_clock::now();
    framebridge::render::Renderer renderer(false);
    renderer.Legacy(30,1280,720,"artifacts/native-cube-canonical.png");
    auto read=[] { std::ifstream f("artifacts/native-cube-canonical.png",std::ios::binary); return std::vector<char>(std::istreambuf_iterator<char>(f),{}); };
    const auto first=read();
    for(int i=0;i<100;++i) renderer.Legacy(static_cast<uint64_t>(i+1),i%2?1280:2560,i%2?720:1440);
    renderer.Legacy(30,1280,720,"artifacts/native-cube-canonical.png");
    if(first!=read()) throw std::runtime_error("legacy repeatability failed");
    renderer.Validate();
    std::cout<<renderer.Telemetry()<<"\nFRAMEBRIDGE_TCW003_PASS resize_cycles=100 elapsed_seconds="
      <<std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count()<<"\n";
    return 0;
  } catch(const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<"\n"; return 4; }
}
