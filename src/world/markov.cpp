#include "world/markov.hpp"
#include <algorithm>

#include "core/serialize.hpp"

namespace eidolon {

MarkovChain<static_cast<size_t>(WeatherState::Count)> makeWeatherMarkov() {
  // States: Clear, Rain, Storm, Snow
  // Transition probabilities (rows = from, cols = to)
  std::array<std::array<float, 4>, 4> t = {{
    // Clear    Rain    Storm   Snow
    { 0.85f,   0.10f,  0.03f,  0.02f }, // Clear
    { 0.20f,   0.60f,  0.15f,  0.05f }, // Rain
    { 0.10f,   0.30f,  0.50f,  0.10f }, // Storm
    { 0.15f,   0.05f,  0.05f,  0.75f }, // Snow
  }};
  return MarkovChain<4>(t);
}

MarkovChain<static_cast<size_t>(WildlifeBehavior::Count)> makeWildlifeBehaviorMarkov() {
  // States: Forage, Flee, Rest, Hunt, Wander
  std::array<std::array<float, 5>, 5> t = {{
    // Forage  Flee   Rest   Hunt  Wander
    { 0.60f,  0.15f, 0.10f, 0.05f, 0.10f }, // Forage
    { 0.05f,  0.70f, 0.05f, 0.10f, 0.10f }, // Flee
    { 0.20f,  0.05f, 0.65f, 0.05f, 0.05f }, // Rest
    { 0.10f,  0.20f, 0.10f, 0.50f, 0.10f }, // Hunt
    { 0.25f,  0.10f, 0.10f, 0.05f, 0.50f }, // Wander
  }};
  return MarkovChain<5>(t);
}

MarkovChain<static_cast<size_t>(SleepState::Count)> makeSleepMarkov() {
  // States: Awake, Drowsy, Sleep, Wake
  std::array<std::array<float, 4>, 4> t = {{
    // Awake  Drowsy  Sleep  Wake
    { 0.80f,  0.15f,  0.04f, 0.01f }, // Awake
    { 0.10f,  0.50f,  0.35f, 0.00f }, // Drowsy
    { 0.00f,  0.00f,  0.95f, 0.05f }, // Sleep
    { 1.00f,  0.00f,  0.00f, 0.00f }, // Wake
  }};
  return MarkovChain<4>(t);
}

MarkovChain<static_cast<size_t>(SkillStage::Count)> makeSkillProgressionMarkov() {
  // States: Novice, Apprentice, Journeyman, Expert, Master
  // Higher stages are absorbing-ish (slow progression)
  std::array<std::array<float, 5>, 5> t = {{
    // Novice  App.   Jour.   Expert  Master
    { 0.70f,  0.25f,  0.04f,  0.01f,  0.00f }, // Novice
    { 0.05f,  0.65f,  0.25f,  0.04f,  0.01f }, // Apprentice
    { 0.01f,  0.05f,  0.70f,  0.20f,  0.04f }, // Journeyman
    { 0.00f,  0.01f,  0.05f,  0.80f,  0.14f }, // Expert
    { 0.00f,  0.00f,  0.01f,  0.05f,  0.94f }, // Master
  }};
  return MarkovChain<5>(t);
}

} // namespace eidolon