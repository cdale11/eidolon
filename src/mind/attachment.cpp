#include "mind/attachment.hpp"
#include <algorithm>
#include <cmath>
#include <random>

namespace eidolon {

void AttachmentSystem::initialize(uint64_t seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> dist(0.0f, 1.0f);
  
  // Randomly assign attachment style based on probabilities
  float r = static_cast<float>(dist(rng));
  if (r < 0.55f) style = AttachmentStyle::Secure;
  else if (r < 0.75f) style = AttachmentStyle::Anxious;
  else if (r < 0.90f) style = AttachmentStyle::Avoidant;
  else style = AttachmentStyle::Disorganized;
  
  // Set initial parameters based on style
  switch (style) {
    case AttachmentStyle::Secure:
      attachment_strength = 0.6f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      separation_anxiety = 0.2f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      reunion_response = 0.6f + 0.3f * static_cast<float>(rand()) / RAND_MAX;
      seeks_proximity_on_reunion = true;
      shows_distress_on_separation = true;
      explores_when_user_present = true;
      explores_when_user_absent = true;
      break;
    case AttachmentStyle::Anxious:
      attachment_strength = 0.7f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      separation_anxiety = 0.6f + 0.3f * static_cast<float>(rand()) / RAND_MAX;
      reunion_response = 0.8f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      seeks_proximity_on_reunion = true;
      shows_distress_on_separation = true;
      explores_when_user_present = true;
      explores_when_user_absent = false;
      break;
    case AttachmentStyle::Avoidant:
      attachment_strength = 0.3f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      separation_anxiety = 0.1f + 0.1f * static_cast<float>(rand()) / RAND_MAX;
      reunion_response = 0.2f + 0.2f * static_cast<float>(rand()) / RAND_MAX;
      seeks_proximity_on_reunion = false;
      shows_distress_on_separation = false;
      explores_when_user_present = true;
      explores_when_user_absent = true;
      break;
    case AttachmentStyle::Disorganized:
      attachment_strength = 0.4f + 0.3f * static_cast<float>(rand()) / RAND_MAX;
      separation_anxiety = 0.4f + 0.4f * static_cast<float>(rand()) / RAND_MAX;
      reunion_response = 0.3f + 0.4f * static_cast<float>(rand()) / RAND_MAX;
      seeks_proximity_on_reunion = static_cast<float>(rand()) / RAND_MAX > 0.5f;
      shows_distress_on_separation = true;
      explores_when_user_present = static_cast<float>(rand()) / RAND_MAX > 0.5f;
      explores_when_user_absent = static_cast<float>(rand()) / RAND_MAX > 0.5f;
      break;
  }
}

void AttachmentSystem::on_user_leaves(uint64_t /*tick*/) {
  if (user_present) {
    user_present = false;
    last_separation_tick = 0; // will be set in update
    separation_count++;
    total_separation_time = 0;
    
    // Separation distress depends on style
    switch (style) {
      case AttachmentStyle::Secure:
        current_separation_distress = 0.3f * separation_anxiety;
        break;
      case AttachmentStyle::Anxious:
        current_separation_distress = 0.8f * separation_anxiety;
        break;
      case AttachmentStyle::Avoidant:
        current_separation_distress = 0.1f * separation_anxiety;
        break;
      case AttachmentStyle::Disorganized:
        current_separation_distress = 0.5f * separation_anxiety;
        break;
    }
  }
}

void AttachmentSystem::on_user_returns(uint64_t tick) {
  if (!user_present) {
    user_present = true;
    last_reunion_tick = tick;
    reunion_count++;
    
    // Reunion response depends on style
    switch (style) {
      case AttachmentStyle::Secure:
        reunion_excitement = 0.7f * reunion_response;
        break;
      case AttachmentStyle::Anxious:
        reunion_excitement = 1.0f * reunion_response;
        break;
      case AttachmentStyle::Avoidant:
        reunion_excitement = 0.2f * reunion_response;
        break;
      case AttachmentStyle::Disorganized:
        reunion_excitement = 0.4f * reunion_response;
        break;
    }
    
    reunion_response = std::min(1.0f, reunion_response + 0.05f); // improve over time
  }
}

void AttachmentSystem::update(uint64_t current_tick) {
  if (!user_present) {
    // Accumulate separation time and distress
    if (last_separation_tick == 0) {
      last_separation_tick = current_tick;
    }
    uint64_t separation_duration = current_tick - last_separation_tick;
    total_separation_time += separation_duration;
    
    // Separation distress builds up over time
    float time_factor = std::min(1.0f, static_cast<float>(current_tick - last_separation_tick) / 86400.0f); // 1 day
    current_separation_distress = std::min(1.0f, 
        separation_anxiety * (0.3f + 0.7f * time_factor));
    
    // Exploration decreases during separation for anxious/disorganized
    // (handled by behavior modifiers)
  } else {
    // User is present - separation distress decays
    if (current_separation_distress > 0.0f) {
      current_separation_distress = std::max(0.0f, current_separation_distress - 0.01f);
    }
    if (reunion_excitement > 0.0f) {
      reunion_excitement = std::max(0.0f, reunion_excitement - 0.02f);
    }
  }
}

AttachmentSystem::BehaviorModifiers AttachmentSystem::get_behavior_modifiers() const {
  BehaviorModifiers mods;
  
  mods.distress_level = current_separation_distress;
  mods.reunion_affection = reunion_excitement;
  
  // Proximity seeking based on style and current state
  if (!user_present) {
    // Separation proximity seeking
    if (style == AttachmentStyle::Anxious) {
      mods.proximity_seeking = 0.8f * separation_anxiety;
    } else if (style == AttachmentStyle::Secure) {
      mods.proximity_seeking = 0.3f * separation_anxiety;
    } else if (style == AttachmentStyle::Disorganized) {
      mods.proximity_seeking = 0.5f * separation_anxiety;
    } else { // Avoidant
      mods.proximity_seeking = 0.0f;
    }
  } else {
    // Reunion proximity seeking
    if (seeks_proximity_on_reunion) {
      mods.proximity_seeking = 0.7f * reunion_response;
    }
  }
  
  // Exploration modifier
  if (!user_present) {
    if (explores_when_user_absent) {
      mods.exploration_modifier = 1.0f;
    } else {
      mods.exploration_modifier = 0.3f; // reduced exploration when attached figure absent
    }
  } else {
    if (explores_when_user_present) {
      mods.exploration_modifier = 1.0f;
    } else {
      mods.exploration_modifier = 0.5f;
    }
  }
  
  mods.distress_level = current_separation_distress;
  mods.reunion_affection = reunion_excitement;
  mods.proximity_seeking = std::min(1.0f, mods.proximity_seeking);
  mods.exploration_modifier = std::clamp(mods.exploration_modifier, 0.1f, 1.0f);
  
  return mods;
}

void AttachmentSystem::serialize(struct BinaryWriter& w) const {
  w.u8(static_cast<uint8_t>(style));
  w.f32(attachment_strength);
  w.f32(separation_anxiety);
  w.f32(reunion_response);
  w.u64(last_separation_tick);
  w.u64(last_reunion_tick);
  w.u64(total_separation_time);
  w.u32(separation_count);
  w.u32(reunion_count);
  w.u8(user_present ? 1 : 0);
  w.f32(current_separation_distress);
  w.f32(reunion_excitement);
  w.u8(seeks_proximity_on_reunion ? 1 : 0);
  w.u8(shows_distress_on_separation ? 1 : 0);
  w.u8(explores_when_user_present ? 1 : 0);
  w.u8(explores_when_user_absent ? 1 : 0);
}

bool AttachmentSystem::deserialize(struct BinaryReader& r) {
  uint8_t s;
  if (!r.u8(s) || !r.f32(attachment_strength) || !r.f32(separation_anxiety) ||
      !r.f32(reunion_response) || !r.u64(last_separation_tick) ||
      !r.u64(last_reunion_tick) || !r.u64(total_separation_time) ||
      !r.u32(separation_count) || !r.u32(reunion_count))
    return false;
  style = static_cast<AttachmentStyle>(s);
  uint8_t up;
  if (!r.u8(up)) return false;
  user_present = up != 0;
  if (!r.f32(current_separation_distress) || !r.f32(reunion_excitement)) return false;
  uint8_t sp, sd, ewp, ewa;
  if (!r.u8(sp) || !r.u8(sd) || !r.u8(ewp) || !r.u8(ewa)) return false;
  seeks_proximity_on_reunion = sp != 0;
  shows_distress_on_separation = sd != 0;
  explores_when_user_present = ewp != 0;
  explores_when_user_absent = ewa != 0;
  return true;
}

} // namespace eidolon