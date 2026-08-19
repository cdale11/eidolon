#include <iostream>
#include <cstdint>
#include "sim/engine.hpp"

int main() {
  std::cout << "Eidolon WASM ReplicaCore test\n";
  
  // Create a minimal engine with a fixed seed
  eidolon::Engine engine;
  engine.init(42, true, 128, 128);
  
  // Run a few ticks
  for (int i = 0; i < 10; ++i) {
    auto action = engine.tick();
    const auto& body = engine.body();
    std::cout << "Tick " << i << ": action=" << static_cast<int>(action)
              << " energy=" << body.energy() 
              << " hunger=" << body.hunger() << " thirst=" << body.thirst() << "\n";
  }
  
  std::cout << "WASM ReplicaCore test passed!\n";
  return 0;
}