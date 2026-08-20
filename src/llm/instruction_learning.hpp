// Instruction trust modulation and repetition learning
// Integrates user instructions with UserModel trust and LearnSystem habit formation
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "core/detmath.hpp"
#include "llm/intent_parser.hpp"
#include "mind/user_model.hpp"

namespace eidolon {

// Track instruction history for repetition learning
struct InstructionRecord {
  UserIntentType intent;
  std::string target;
  uint64_t first_tick;
  uint64_t last_tick;
  uint32_t count;
  float avg_confidence;
  float trust_at_first;
  float trust_current;
  bool last_outcome_positive; // whether last execution was successful
  uint32_t successful_executions;
  uint32_t failed_executions;
  
  // Habit strength (0..1) - increases with repetition
  float habit_strength = 0.0f;
  
  // Update with new execution
  void record_execution(uint64_t tick, float confidence, bool positive_outcome) {
    last_tick = tick;
    count++;
    avg_confidence = (avg_confidence * (count - 1) + confidence) / count;
    trust_current = trust_current; // will be updated externally
    
    if (positive_outcome) {
      successful_executions++;
    } else {
      failed_executions++;
    }
    
    // Update habit strength (logarithmic growth, capped at 1.0)
    habit_strength = std::min(1.0f, 
        0.15f * detmath::log1pf(static_cast<float>(count) / 2.0f));
  }
};

// Trust modulation for user instructions
struct InstructionTrustModulator {
  // Trust weights for different outcomes
  static constexpr float TRUST_GAIN_SUCCESS = 0.015f;
  static constexpr float TRUST_GAIN_FOLLOWED = 0.005f;  // user instruction was followed
  static constexpr float TRUST_LOSS_FAILURE = 0.02f;
  static constexpr float TRUST_LOSS_IGNORED = 0.01f;    // user instruction was valid but ignored
  static constexpr float TRUST_LOSS_HARMFUL = 0.04f;    // instruction led to harm
  
  // Apply trust change based on instruction outcome
  static void apply_outcome(UserModel& user_model, bool positive, const char* reason = nullptr) {
    user_model.record_interaction(positive, 0); // tick will be filled by caller
    // Additional logging could go here
  }
  
  // Record that a valid user instruction was followed
  static void record_followed(UserModel& user_model) {
    user_model.record_interaction(true, 0);
  }
  
  // Record that a valid user instruction was ignored
  static void record_ignored(UserModel& user_model) {
    user_model.record_interaction(false, 0);
  }
  
  // Record that following instruction led to harm
  static void record_harmful(UserModel& user_model) {
    user_model.record_interaction(false, 0);
    // Additional penalty already in record_interaction
  }
};

// Repetition learning - tracks instruction history and builds habit strength
class InstructionMemory {
public:
  InstructionMemory() = default;
  
  // Record a user instruction execution
  void record_instruction(const ParsedInstruction& instr, uint64_t tick, 
                          float confidence, bool positive_outcome,
                          UserModel& user_model);
  
  // Get habit strength for an intent+target combination
  float get_habit_strength(UserIntentType intent, const std::string& target) const;
  
  // Get instruction count
  uint32_t get_instruction_count(UserIntentType intent, const std::string& target) const;
  
  // Get all records for serialization
  const std::unordered_map<std::string, InstructionRecord>& get_records() const { return records_; }
  
  // Serialization
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  // Key: "intent_type:target" (e.g., "1:the river")
  std::unordered_map<std::string, InstructionRecord> records_;
  
  std::string make_key(UserIntentType intent, const std::string& target) const {
    return std::to_string(static_cast<uint8_t>(intent)) + ":" + target;
  }
};

// Combined instruction learning system
class InstructionLearningSystem {
public:
  InstructionLearningSystem() = default;
  
  // Process a user instruction through the full learning pipeline
  // Returns whether the instruction was valid and should be executed
  bool process_instruction(const std::string& text, const ValidationContext& ctx,
                           uint64_t tick, UserModel& user_model,
                           ParsedInstruction& out_instr);
  
  // Call after instruction execution to update trust/habits
  void record_execution(const ParsedInstruction& instr, uint64_t tick,
                        bool positive_outcome, UserModel& user_model);
  
  // Record that a valid instruction was ignored
  void record_ignored(const ParsedInstruction& instr, UserModel& user_model);
  
  // Record that following instruction caused harm
  void record_harmful(const ParsedInstruction& instr, UserModel& user_model);
  
  // Get habit strength for policy influence
  float get_habit_influence(UserIntentType intent, const std::string& target) const;
  
  // Serialization
  void serialize(BinaryWriter& w) const;
  bool deserialize(BinaryReader& r);

private:
  IntentParser parser_;
  InstructionMemory memory_;
};

} // namespace eidolon