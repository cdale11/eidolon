#ifndef EIDOLON_USER_MODEL_HPP
#define EIDOLON_USER_MODEL_HPP

#include <array>
#include <string>
#include <vector>
#include <cstdint>

#include "core/serialize.hpp"

namespace eidolon {

// User model: tracks the organism's beliefs and feelings about the user
// All values are 0..1 unless noted

struct UserModel {
  // Core affective dimensions
  float familiarity = 0.0f;     // how well the organism knows the user (0=never met, 1=very well)
  float trust = 0.5f;           // trust in user's words/actions (0=distrust, 1=complete trust)
  float affection = 0.0f;       // positive emotional attachment
  float fear = 0.0f;            // fear of the user
  float respect = 0.5f;         // respect for user's authority/competence
  float resentment = 0.0f;      // negative feelings from perceived betrayals
  float reciprocity = 0.5f;     // perceived give-and-take balance
  float expectations = 0.5f;    // expectations of user's helpfulness/harm

  // Interaction history
  uint32_t total_interactions = 0;
  uint32_t positive_interactions = 0;
  uint32_t negative_interactions = 0;
  uint64_t last_interaction_tick = 0;
  uint64_t last_seen_tick = 0;

  // Beliefs about user (verifiable facts learned from user)
  struct UserBelief {
    std::string fact;           // e.g., "water is at (10,10)", "berries are at (5,5)"
    float confidence = 0.0f;    // 0..1 confidence in this belief
    uint64_t learned_at = 0;
    uint32_t verifications = 0; // times this belief was confirmed
    uint32_t falsifications = 0; // times this belief was contradicted
    bool verified = false;      // whether externally verified
  };
  std::vector<std::string> beliefs; // simplified: just the fact strings

  // Attachment system
  float attachment_strength = 0.0f;    // 0..1
  float attachment_pressure = 0.0f;    // builds up during absence
  uint64_t last_separation_tick = 0;   // when user last went away
  bool user_present = false;

  // Update functions
  void record_interaction(bool positive, uint64_t tick);
  void record_user_message(const std::string& message, bool verified, uint64_t tick);
  void set_user_present(bool present, uint64_t tick);
  void update_attachment_pressure(uint64_t current_tick);
  void update_expectations();

  // Serialization
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

} // namespace eidolon

#endif // EIDOLON_USER_MODEL_HPP