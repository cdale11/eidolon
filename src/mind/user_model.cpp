#include "mind/user_model.hpp"
#include "core/detmath.hpp"
#include <algorithm>
#include <sstream>

namespace eidolon {

void UserModel::record_interaction(bool positive, uint64_t tick) {
  total_interactions++;
  if (positive) {
    positive_interactions++;
    trust = std::min(1.0f, trust + 0.02f);
    affection = std::min(1.0f, affection + 0.015f);
    reciprocity = std::min(1.0f, reciprocity + 0.01f);
    resentment = std::max(0.0f, resentment - 0.01f);
  } else {
    negative_interactions++;
    trust = std::max(0.0f, trust - 0.03f);
    affection = std::max(0.0f, affection - 0.02f);
    resentment = std::min(1.0f, resentment + 0.03f);
    reciprocity = std::max(0.0f, reciprocity - 0.02f);
  }
  familiarity = std::min(1.0f, familiarity + 0.005f);
  last_interaction_tick = tick;
  last_seen_tick = tick;
}

void UserModel::record_user_message(const std::string& /*message*/, bool verified, uint64_t tick) {
  // Store the message as a belief
  // For now, just track that a message was received
  // In a full implementation, this would parse the message and extract verifiable facts
  
  // Update trust based on verification
  if (verified) {
    trust = std::min(1.0f, trust + 0.02f);
  } else {
    trust = std::max(0.0f, trust - 0.01f);
  }
  last_interaction_tick = tick;
}

void UserModel::set_user_present(bool present, uint64_t tick) {
  if (user_present && !present) {
    // User just left
    last_separation_tick = tick;
    user_present = false;
    attachment_pressure = 0.0f;
  } else if (!user_present && present) {
    // User returned
    user_present = true;
    // Reunion effect - attachment pressure releases as positive interaction
    if (attachment_pressure > 0.5f) {
      affection = std::min(1.0f, affection + 0.1f);
      trust = std::min(1.0f, trust + 0.05f);
    }
    attachment_pressure = 0.0f;
    last_seen_tick = tick;
  }
  user_present = present;
}

void UserModel::update_attachment_pressure(uint64_t current_tick) {
  if (!user_present && last_separation_tick > 0) {
    uint64_t separation_duration = current_tick - last_separation_tick;
    // Attachment pressure builds up over time (logarithmic)
    attachment_pressure = std::min(1.0f, 
        0.1f * detmath::log1pf(static_cast<float>(separation_duration) / 3600.0f));
    
    // High attachment pressure affects behavior
    if (attachment_pressure > 0.7f) {
      fear = std::min(1.0f, fear + 0.01f);
      resentment = std::min(1.0f, resentment + 0.005f);
    }
  }
}

void UserModel::update_expectations() {
  if (total_interactions > 0) {
    float positive_rate = static_cast<float>(positive_interactions) / total_interactions;
    expectations = 0.5f * expectations + 0.5f * positive_rate;
  }
}

void UserModel::serialize(struct BinaryWriter& w) const {
  w.f32(familiarity);
  w.f32(trust);
  w.f32(affection);
  w.f32(fear);
  w.f32(respect);
  w.f32(resentment);
  w.f32(reciprocity);
  w.f32(expectations);
  w.u32(total_interactions);
  w.u32(positive_interactions);
  w.u32(negative_interactions);
  w.u64(last_interaction_tick);
  w.u64(last_seen_tick);
  w.f32(attachment_strength);
  w.f32(attachment_pressure);
  w.u64(last_separation_tick);
  w.u8(user_present ? 1 : 0);
  // Beliefs - simplified
  w.u32(static_cast<uint32_t>(beliefs.size()));
  for (const auto& b : beliefs) {
    w.str(b);
  }
}

bool UserModel::deserialize(struct BinaryReader& r) {
  if (!r.f32(familiarity) || !r.f32(trust) || !r.f32(affection) || !r.f32(fear) ||
      !r.f32(respect) || !r.f32(resentment) || !r.f32(reciprocity) || !r.f32(expectations))
    return false;
  if (!r.u32(total_interactions) || !r.u32(positive_interactions) ||
      !r.u32(negative_interactions) || !r.u64(last_interaction_tick) ||
      !r.u64(last_seen_tick))
    return false;
  if (!r.f32(attachment_strength) || !r.f32(attachment_pressure) ||
      !r.u64(last_separation_tick))
    return false;
  uint8_t up;
  if (!r.u8(up)) return false;
  user_present = up != 0;

  uint32_t n;
  if (!r.u32(n)) return false;
  beliefs.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.str(beliefs[i])) return false;
  }
  return true;
}

} // namespace eidolon