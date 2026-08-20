// Instruction trust modulation and repetition learning implementation
#include "llm/instruction_learning.hpp"

#include "core/detmath.hpp"
#include "core/serialize.hpp"
#include <algorithm>
#include <sstream>

namespace eidolon {

void InstructionMemory::record_instruction(const ParsedInstruction& instr, uint64_t tick, 
                                           float confidence, bool positive_outcome,
                                           UserModel& user_model) {
  std::string key = make_key(instr.intent, instr.target);
  
  auto it = records_.find(key);
  if (it == records_.end()) {
    // First time seeing this instruction
    InstructionRecord record;
    record.intent = instr.intent;
    record.target = instr.target;
    record.first_tick = tick;
    record.last_tick = tick;
    record.count = 1;
    record.avg_confidence = confidence;
    record.trust_at_first = user_model.trust;
    record.trust_current = user_model.trust;
    record.last_outcome_positive = positive_outcome;
    record.successful_executions = positive_outcome ? 1 : 0;
    record.failed_executions = positive_outcome ? 0 : 1;
    record.record_execution(tick, confidence, positive_outcome);
    records_[key] = std::move(record);
  } else {
    // Update existing record
    auto& record = it->second;
    record.record_execution(tick, confidence, positive_outcome);
    record.trust_current = user_model.trust;
    record.last_outcome_positive = positive_outcome;
    if (positive_outcome) {
      record.successful_executions++;
    } else {
      record.failed_executions++;
    }
  }
  
  // Apply trust modulation based on outcome
  if (positive_outcome) {
    user_model.trust = std::min(1.0f, user_model.trust + InstructionTrustModulator::TRUST_GAIN_SUCCESS);
    user_model.positive_interactions++;
  } else {
    user_model.trust = std::max(0.0f, user_model.trust - InstructionTrustModulator::TRUST_LOSS_FAILURE);
    user_model.negative_interactions++;
  }
  user_model.total_interactions++;
  user_model.last_interaction_tick = 0; // caller should set actual tick
}

float InstructionMemory::get_habit_strength(UserIntentType intent, const std::string& target) const {
  std::string key = make_key(intent, target);
  auto it = records_.find(key);
  if (it != records_.end()) {
    return it->second.habit_strength;
  }
  return 0.0f;
}

uint32_t InstructionMemory::get_instruction_count(UserIntentType intent, const std::string& target) const {
  std::string key = make_key(intent, target);
  auto it = records_.find(key);
  if (it != records_.end()) {
    return it->second.count;
  }
  return 0;
}

void InstructionMemory::serialize(BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(records_.size()));
  for (const auto& [key, record] : records_) {
    w.str(key);
    w.u8(static_cast<uint8_t>(record.intent));
    w.str(record.target);
    w.u64(record.first_tick);
    w.u64(record.last_tick);
    w.u32(record.count);
    w.f32(record.avg_confidence);
    w.f32(record.trust_at_first);
    w.f32(record.trust_current);
    w.u8(record.last_outcome_positive ? 1 : 0);
    w.u32(record.successful_executions);
    w.u32(record.failed_executions);
    w.f32(record.habit_strength);
  }
}

bool InstructionMemory::deserialize(BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  records_.clear();
  records_.reserve(n);
  for (uint32_t i = 0; i < n; ++i) {
    std::string key;
    if (!r.str(key)) return false;
    InstructionRecord record;
    uint8_t intent_val;
    if (!r.u8(intent_val)) return false;
    record.intent = static_cast<UserIntentType>(intent_val);
    if (!r.str(record.target)) return false;
    if (!r.u64(record.first_tick)) return false;
    if (!r.u64(record.last_tick)) return false;
    if (!r.u32(record.count)) return false;
    if (!r.f32(record.avg_confidence)) return false;
    if (!r.f32(record.trust_at_first)) return false;
    if (!r.f32(record.trust_current)) return false;
    uint8_t last_pos;
    if (!r.u8(last_pos)) return false;
    record.last_outcome_positive = (last_pos != 0);
    if (!r.u32(record.successful_executions)) return false;
    if (!r.u32(record.failed_executions)) return false;
    if (!r.f32(record.habit_strength)) return false;
    records_[record.target] = std::move(record); // key reconstruction would need intent+target
  }
  return true;
}

bool InstructionLearningSystem::process_instruction(const std::string& text, const ValidationContext& ctx,
                                                    [[maybe_unused]] uint64_t tick, UserModel& user_model,
                                                    ParsedInstruction& out_instr) {
  // Parse and validate
  out_instr = parser_.parseAndValidate(text, ctx);
  
  if (!out_instr.valid) {
    // Invalid instruction - still record interaction as negative if it was an attempt
    if (out_instr.intent != UserIntentType::None && out_instr.confidence > 0.3f) {
      user_model.record_interaction(false, 0);
    }
    return false;
  }
  
  // Valid instruction - record as positive interaction (user communicated clearly)
  user_model.record_interaction(true, 0);
  
  return true;
}

void InstructionLearningSystem::record_execution(const ParsedInstruction& instr, uint64_t tick,
                                                 bool positive_outcome, UserModel& user_model) {
  // Record in memory with trust modulation
  memory_.record_instruction(instr, tick, instr.confidence, positive_outcome, user_model);
}

void InstructionLearningSystem::record_ignored([[maybe_unused]] const ParsedInstruction& instr, UserModel& user_model) {
  // Valid instruction was ignored - small trust loss
  user_model.trust = std::max(0.0f, user_model.trust - InstructionTrustModulator::TRUST_LOSS_IGNORED);
  user_model.record_interaction(false, 0);
}

void InstructionLearningSystem::record_harmful([[maybe_unused]] const ParsedInstruction& instr, UserModel& user_model) {
  // Following instruction led to harm - larger trust loss
  user_model.trust = std::max(0.0f, user_model.trust - InstructionTrustModulator::TRUST_LOSS_HARMFUL);
  user_model.record_interaction(false, 0);
}

float InstructionLearningSystem::get_habit_influence(UserIntentType intent, const std::string& target) const {
  return memory_.get_habit_strength(intent, target);
}

void InstructionLearningSystem::serialize(BinaryWriter& w) const {
  // parser_ has no state to serialize currently
  memory_.serialize(w);
}

bool InstructionLearningSystem::deserialize(BinaryReader& r) {
  return memory_.deserialize(r);
}

} // namespace eidolon