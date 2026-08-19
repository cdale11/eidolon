#ifndef EIDOLON_ATTACHMENT_HPP
#define EIDOLON_ATTACHMENT_HPP

#include <cstdint>

#include "core/serialize.hpp"
#include "body/physiology.hpp"

namespace eidolon {

// Attachment system: models the organism's attachment to the user
// Based on attachment theory - secure/insecure attachment styles affect behavior

enum class AttachmentStyle : uint8_t {
  Secure = 0,        // comfortable with separation and reunion
  Anxious = 1,       // high anxiety during separation, clingy on reunion
  Avoidant = 2,      // dismissive of attachment, avoids reunion
  Disorganized = 3   // inconsistent/confused attachment behaviors
};

struct AttachmentSystem {
  // Core attachment parameters
  AttachmentStyle style = AttachmentStyle::Secure;
  float attachment_strength = 0.5f;    // overall attachment strength (0..1)
  float separation_anxiety = 0.3f;     // anxiety during separation (0..1)
  float reunion_response = 0.5f;       // reunion behavior intensity (0..1)
  
  // Separation tracking
  uint64_t last_separation_tick = 0;
  uint64_t last_reunion_tick = 0;
  uint64_t total_separation_time = 0;
  uint32_t separation_count = 0;
  uint32_t reunion_count = 0;
  
  // Current state
  bool user_present = true;
  float current_separation_distress = 0.0f;
  float reunion_excitement = 0.0f;
  
  // Attachment behaviors
  bool seeks_proximity_on_reunion = true;
  bool shows_distress_on_separation = true;
  bool explores_when_user_present = true;
  bool explores_when_user_absent = false;
  
  // Initialize with a seed for reproducibility
  void initialize(uint64_t seed);
  
  // Separation event
  void on_user_leaves(uint64_t tick);
  
  // Reunion event
  void on_user_returns(uint64_t tick);
  
  // Update internal state each tick
  void update(uint64_t current_tick);
  
  // Get current attachment behavior modifiers
  struct BehaviorModifiers {
    float exploration_modifier = 1.0f;    // multiplies exploration tendency
    float proximity_seeking = 0.0f;       // additional proximity seeking (0..1)
    float distress_level = 0.0f;          // current distress (0..1)
    float reunion_affection = 0.0f;       // affection boost on reunion
  };
  BehaviorModifiers get_behavior_modifiers() const;
  
  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

} // namespace eidolon

#endif // EIDOLON_ATTACHMENT_HPP