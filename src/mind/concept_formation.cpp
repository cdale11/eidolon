#include "mind/concept_formation.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <sstream>

namespace eidolon {

void Concept::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.str(name);
  w.u32(static_cast<uint32_t>(centroid.size()));
  for (float v : centroid) w.f32(v);
  w.u32(static_cast<uint32_t>(member_experiences.size()));
  for (uint32_t e : member_experiences) w.u32(e);
  w.f32(cohesion);
  w.f32(separation);
  w.u32(usage_count);
  w.u64(created_at);
  w.u64(last_used);
  w.u8(named_by_llm ? 1 : 0);
}

bool Concept::deserialize(struct BinaryReader& r) {
  if (!r.u32(id)) return false;
  if (!r.str(name)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  centroid.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.f32(centroid[i])) return false;
  }
  uint32_t m;
  if (!r.u32(m)) return false;
  member_experiences.resize(static_cast<size_t>(m));
  for (size_t i = 0; i < static_cast<size_t>(m); ++i) {
    if (!r.u32(member_experiences[i])) return false;
  }
  if (!r.f32(cohesion) || !r.f32(separation) ||
      !r.u32(usage_count) || !r.u64(created_at) ||
      !r.u64(last_used))
    return false;
  uint8_t nbl;
  if (!r.u8(nbl)) return false;
  named_by_llm = nbl != 0;
  return true;
}

void ExperienceVector::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(features.size()));
  for (float f : features) w.f32(f);
  w.u64(tick);
  w.str(context);
  w.u32(concept_id);
}

bool ExperienceVector::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  features.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!r.f32(features[i])) return false;
  }
  if (!r.u64(tick) || !r.str(context) || !r.u32(concept_id)) return false;
  return true;
}

ConceptFormation::ConceptFormation(uint64_t seed) : seed_(seed), rng_(seed) {}

uint32_t ConceptFormation::process_experience(const std::vector<float>& features,
                                              const std::string& context,
                                              uint64_t tick,
                                              class Rng& /*rng*/) {
  // Store experience
  ExperienceVector exp;
  exp.features = features;
  exp.tick = tick;
  exp.context = context;
  exp.concept_id = 0;
  experience_buffer_.push_back(exp);
  
  // Find best matching concept
  uint32_t best_id = find_best_concept(features);
  
  if (best_id != 0) {
    // Assign to existing concept
    auto it = std::find_if(concepts_.begin(), concepts_.end(),
                          [best_id](const Concept& c) { return c.id == best_id; });
    if (it != concepts_.end()) {
      it->member_experiences.push_back(experience_buffer_.size() - 1);
      it->usage_count++;
      it->last_used = tick;
      update_concept_centroid(it->id);
      return it->id;
    }
  }
  
  // Create new concept
  maybe_create_new_concept(features, "", tick, const_cast<Rng&>(rng_));
  return concepts_.back().id;
}

const Concept* ConceptFormation::get_concept(uint32_t id) const {
  auto it = std::find_if(concepts_.begin(), concepts_.end(),
                        [id](const Concept& c) { return c.id == id; });
  return it != concepts_.end() ? &*it : nullptr;
}

void ConceptFormation::merge_concepts(float cohesion_threshold, float separation_threshold) {
  for (size_t i = 0; i < concepts_.size(); ++i) {
    for (size_t j = i + 1; j < concepts_.size(); ++j) {
      const Concept& a = concepts_[i];
      const Concept& b = concepts_[j];
      
      float sim = cosine_similarity(a.centroid, b.centroid);
      if (sim > cohesion_threshold && (1.0f - sim) < separation_threshold) {
        // Merge b into a
        concepts_[i].member_experiences.insert(
            concepts_[i].member_experiences.end(),
            concepts_[j].member_experiences.begin(),
            concepts_[j].member_experiences.end());
        concepts_[i].usage_count += concepts_[j].usage_count;
        concepts_[i].last_used = std::max(concepts_[i].last_used, concepts_[j].last_used);
        update_concept_centroid(concepts_[i].id);
        
        // Remove b
        concepts_.erase(concepts_.begin() + j);
        // Need to restart outer loop since indices changed
        goto recheck;
      }
    }
  recheck:
    ;
  }
}

std::string ConceptFormation::request_llm_name(const Concept& c,
                                               const std::function<std::string(const std::string&)>& llm_callback) {
  // Build prompt describing the concept
  std::ostringstream oss;
  oss << "Name this concept based on its properties:\n";
  oss << "Cohesion: " << c.cohesion << "\n";
  oss << "Separation: " << c.separation << "\n";
  oss << "Usage count: " << c.usage_count << "\n";
  oss << "Member experiences: " << c.member_experiences.size() << "\n";
  oss << "Centroid size: " << c.centroid.size() << "\n";
  oss << "Provide a short, descriptive name (1-3 words):\n";
  
  std::string name = llm_callback(oss.str());
  return name;
}

std::vector<std::pair<uint32_t, float>> ConceptFormation::get_active_concepts(
    const std::vector<float>& current_features,
    float threshold) const {
  std::vector<std::pair<uint32_t, float>> active;
  for (const auto& c : concepts_) {
    float sim = cosine_similarity(current_features, c.centroid);
    if (sim >= threshold) {
      active.emplace_back(c.id, sim);
    }
  }
  std::sort(active.begin(), active.end(),
            [](const auto& a, const auto& b) { return a.second > b.second; });
  return active;
}

void ConceptFormation::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(concepts_.size()));
  for (const auto& c : concepts_) c.serialize(w);
  w.u32(static_cast<uint32_t>(experience_buffer_.size()));
  for (const auto& e : experience_buffer_) e.serialize(w);
  w.u32(next_concept_id_);
  w.u64(seed_);
}

bool ConceptFormation::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  concepts_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!concepts_[i].deserialize(r)) return false;
  }
  if (!r.u32(n)) return false;
  experience_buffer_.resize(static_cast<size_t>(n));
  for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
    if (!experience_buffer_[i].deserialize(r)) return false;
  }
  if (!r.u32(next_concept_id_) || !r.u64(seed_)) return false;
  return true;
}

uint32_t ConceptFormation::find_best_concept(const std::vector<float>& features) const {
  if (concepts_.empty()) return 0;
  
  float best_sim = -1.0f;
  uint32_t best_id = 0;
  
  for (const auto& c : concepts_) {
    float sim = cosine_similarity(features, c.centroid);
    if (sim > 0.7f && sim > best_sim) { // minimum similarity threshold
      best_sim = sim;
      best_id = c.id;
    }
  }
  return best_id;
}

float ConceptFormation::cosine_similarity(const std::vector<float>& a, const std::vector<float>& b) const {
  if (a.size() != b.size()) return 0.0f;
  
  float dot = 0.0f, norm_a = 0.0f, norm_b = 0.0f;
  for (size_t i = 0; i < a.size(); ++i) {
    dot += a[i] * b[i];
    norm_a += a[i] * a[i];
    norm_b += b[i] * b[i];
  }
  if (norm_a == 0.0f || norm_b == 0.0f) return 0.0f;
  return dot / (std::sqrt(norm_a) * std::sqrt(norm_b));
}

void ConceptFormation::update_concept_centroid(uint32_t concept_id) {
  auto it = std::find_if(concepts_.begin(), concepts_.end(),
                        [concept_id](const Concept& c) { return c.id == concept_id; });
  if (it == concepts_.end()) return;
  
  Concept& c = const_cast<Concept&>(*it);
  if (c.member_experiences.empty()) return;
  
  size_t dim = experience_buffer_[c.member_experiences[0]].features.size();
  std::vector<float> new_centroid(dim, 0.0f);
  
  for (uint32_t exp_idx : c.member_experiences) {
    if (exp_idx < experience_buffer_.size()) {
      const auto& exp = experience_buffer_[exp_idx];
      for (size_t i = 0; i < dim && i < exp.features.size(); ++i) {
        new_centroid[i] += exp.features[i];
      }
    }
  }
  
  float count = static_cast<float>(c.member_experiences.size());
  for (size_t i = 0; i < dim; ++i) {
    new_centroid[i] /= count;
  }
  c.centroid = new_centroid;
}

float ConceptFormation::compute_concept_cohesion(const Concept& c) const {
  if (c.member_experiences.size() < 2) return 1.0f;
  
  float total_sim = 0.0f;
  int count = 0;
  for (size_t i = 0; i < c.member_experiences.size(); ++i) {
    for (size_t j = i + 1; j < c.member_experiences.size(); ++j) {
      uint32_t ei = c.member_experiences[i];
      uint32_t ej = c.member_experiences[j];
      if (ei < experience_buffer_.size() && ej < experience_buffer_.size()) {
        float sim = cosine_similarity(experience_buffer_[ei].features, experience_buffer_[ej].features);
        total_sim += sim;
        count++;
      }
    }
  }
  return count > 0 ? total_sim / count : 1.0f;
}

float ConceptFormation::compute_concept_separation(const Concept& c) const {
  if (concepts_.size() <= 1) return 1.0f;
  
  float max_sim = 0.0f;
  for (const auto& other : concepts_) {
    if (other.id == c.id) continue;
    float sim = cosine_similarity(c.centroid, other.centroid);
    max_sim = std::max(max_sim, sim);
  }
  return 1.0f - max_sim; // higher = more separate
}

void ConceptFormation::maybe_create_new_concept(const std::vector<float>& features,
                                                const std::string& /*context*/,
                                                uint64_t /*tick*/,
                                                class Rng& /*rng*/) {
  Concept c;
  c.id = next_concept_id_++;
  c.name = "Concept_" + std::to_string(c.id);
  c.centroid = features;
  c.member_experiences.push_back(experience_buffer_.size() - 1);
  c.cohesion = 1.0f;
  c.separation = 1.0f;
  c.usage_count = 1;
  c.created_at = 0; // would be set by caller
  c.last_used = 0;
  c.named_by_llm = false;
  concepts_.push_back(c);
}

} // namespace eidolon