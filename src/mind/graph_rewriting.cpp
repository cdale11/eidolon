#include "mind/graph_rewriting.hpp"
#include <algorithm>
#include <queue>
#include <unordered_set>
#include <sstream>

namespace eidolon {

void GraphNode::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.u8(static_cast<uint8_t>(type));
  w.str(label);
  w.u32(static_cast<uint32_t>(properties.size()));
  for (const auto& kv : properties) {
    w.str(kv.first);
    w.str(kv.second);
  }
  w.u64(created_at);
  w.u64(updated_at);
  w.f32(confidence);
}

bool GraphNode::deserialize(struct BinaryReader& r) {
  if (!r.u32(id)) return false;
  uint8_t t;
  if (!r.u8(t)) return false;
  type = static_cast<NodeType>(t);
  if (!r.str(label)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  for (uint32_t i = 0; i < n; ++i) {
    std::string k, v;
    if (!r.str(k) || !r.str(v)) return false;
    properties[k] = v;
  }
  if (!r.u64(created_at) || !r.u64(updated_at) || !r.f32(confidence))
    return false;
  return true;
}

void GraphEdge::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.u32(source);
  w.u32(target);
  w.u8(static_cast<uint8_t>(type));
  w.f32(weight);
  w.f32(confidence);
  w.u64(created_at);
  w.str(evidence);
}

bool GraphEdge::deserialize(struct BinaryReader& r) {
  if (!r.u32(id) || !r.u32(source) || !r.u32(target)) return false;
  uint8_t t;
  if (!r.u8(t)) return false;
  type = static_cast<EdgeType>(t);
  if (!r.f32(weight) || !r.f32(confidence) || !r.u64(created_at) || !r.str(evidence))
    return false;
  return true;
}

void RewriteRule::serialize(struct BinaryWriter& w) const {
  w.u32(id);
  w.str(name);
  w.str(description);
  w.u32(static_cast<uint32_t>(pattern.node_types.size()));
  for (uint32_t t : pattern.node_types) w.u32(t);
  w.u32(static_cast<uint32_t>(pattern.edges.size()));
  for (const auto& e : pattern.edges) {
    w.u32(e.first);
    w.u8(static_cast<uint8_t>(e.second));
  }
  w.u32(static_cast<uint32_t>(pattern.node_constraints.size()));
  for (const auto& kv : pattern.node_constraints) {
    w.u32(kv.first);
    w.u32(static_cast<uint32_t>(kv.second.size()));
    for (const auto& p : kv.second) {
      w.str(p.first);
      w.str(p.second);
    }
  }
  w.u32(static_cast<uint32_t>(replacement.nodes_to_add.size()));
  for (const auto& n : replacement.nodes_to_add) {
    w.u8(static_cast<uint8_t>(n.first));
    w.str(n.second);
  }
  w.u32(static_cast<uint32_t>(replacement.edges_to_add.size()));
  for (const auto& e : replacement.edges_to_add) {
    w.u32(std::get<0>(e));
    w.u32(std::get<1>(e));
    w.u8(static_cast<uint8_t>(std::get<2>(e)));
    w.f32(std::get<3>(e));
  }
  w.u32(static_cast<uint32_t>(replacement.nodes_to_remove.size()));
  for (uint32_t n : replacement.nodes_to_remove) w.u32(n);
  w.u32(static_cast<uint32_t>(replacement.edges_to_remove.size()));
  for (const auto& e : replacement.edges_to_remove) {
    w.u32(e.first);
    w.u32(e.second);
  }
  w.f32(priority);
  w.u32(min_confidence);
  w.u32(max_applications);
  w.u32(applications);
}

bool RewriteRule::deserialize(struct BinaryReader& r) {
  if (!r.u32(id) || !r.str(name) || !r.str(description)) return false;
  uint32_t n;
  if (!r.u32(n)) return false;
  pattern.node_types.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    if (!r.u32(pattern.node_types[i])) return false;
  }
  if (!r.u32(n)) return false;
  pattern.edges.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t idx;
    uint8_t et;
    if (!r.u32(idx) || !r.u8(et)) return false;
    pattern.edges[i].second = static_cast<EdgeType>(et);
  }
  if (!r.u32(n)) return false;
  pattern.node_constraints.clear();
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t idx;
    if (!r.u32(idx)) return false;
    uint32_t m;
    if (!r.u32(m)) return false;
    for (uint32_t j = 0; j < m; ++j) {
      std::string k, v;
      if (!r.str(k) || !r.str(v)) return false;
      pattern.node_constraints[idx][k] = v;
    }
  }
  if (!r.u32(n)) return false;
  replacement.nodes_to_add.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    uint8_t t;
    if (!r.u8(t) || !r.str(replacement.nodes_to_add[i].second)) return false;
    replacement.nodes_to_add[i].first = static_cast<NodeType>(t);
  }
  if (!r.u32(n)) return false;
  replacement.edges_to_add.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    uint32_t src, tgt;
    uint8_t et;
    float w;
    if (!r.u32(src) || !r.u32(tgt) || !r.u8(et) || !r.f32(w))
      return false;
    replacement.edges_to_add[i] = {src, tgt, static_cast<EdgeType>(et), w};
  }
  if (!r.u32(n)) return false;
  replacement.nodes_to_remove.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    if (!r.u32(replacement.nodes_to_remove[i])) return false;
  }
  if (!r.u32(n)) return false;
  replacement.edges_to_remove.resize(n);
  for (uint32_t i = 0; i < n; ++i) {
    if (!r.u32(std::get<0>(replacement.edges_to_remove[i])) ||
        !r.u32(std::get<1>(replacement.edges_to_remove[i])))
      return false;
  }
  if (!r.f32(priority) || !r.u32(min_confidence) ||
      !r.u32(max_applications) || !r.u32(applications))
    return false;
  return true;
}

GraphRewritingSystem::GraphRewritingSystem(uint64_t seed) : seed_(seed), rng_(seed) {}

uint32_t GraphRewritingSystem::add_node(NodeType type, const std::string& label,
                                        const std::unordered_map<std::string, std::string>& props) {
  uint32_t id = next_node_id_++;
  Node node;
  node.data.id = next_node_id_ - 1;
  node.data.type = type;
  node.data.label = label;
  node.data.properties = props;
  node.data.created_at = 0;
  node.data.updated_at = 0;
  node.data.confidence = 1.0f;
  nodes_[id] = node;
  return id;
}

void GraphRewritingSystem::remove_node(uint32_t node_id) {
  remove_node_internal(node_id);
}

void GraphRewritingSystem::remove_node_internal(uint32_t node_id) {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return;
  for (uint32_t edge_id : it->second.out_edges) {
    remove_edge_internal(edge_id);
  }
  for (uint32_t edge_id : it->second.in_edges) {
    remove_edge_internal(edge_id);
  }
  nodes_.erase(node_id);
}

void GraphRewritingSystem::add_edge(uint32_t source, uint32_t target, EdgeType type,
                                    float weight, float confidence, const std::string& evidence) {
  if (nodes_.find(source) == nodes_.end() || nodes_.find(target) == nodes_.end()) return;
  
  uint32_t id = next_edge_id_++;
  Edge edge;
  edge.data.id = next_edge_id_ - 1;
  edge.data.source = source;
  edge.data.target = target;
  edge.data.type = type;
  edge.data.weight = weight;
  edge.data.confidence = confidence;
  edge.data.created_at = 0;
  edge.data.evidence = evidence;
  edges_[id] = edge;
  
  nodes_[source].out_edges.push_back(id);
  nodes_[target].in_edges.push_back(id);
}

void GraphRewritingSystem::remove_edge(uint32_t edge_id) {
  remove_edge_internal(edge_id);
}

void GraphRewritingSystem::remove_edge_internal(uint32_t edge_id) {
  auto it = edges_.find(edge_id);
  if (it == edges_.end()) return;
  
  uint32_t src = it->second.data.source;
  uint32_t tgt = it->second.data.target;
  
  auto& src_edges = nodes_[src].out_edges;
  src_edges.erase(std::remove(src_edges.begin(), src_edges.end(), edge_id), src_edges.end());
  auto& tgt_edges = nodes_[tgt].in_edges;
  tgt_edges.erase(std::remove(tgt_edges.begin(), tgt_edges.end(), edge_id), tgt_edges.end());
  
  edges_.erase(edge_id);
}

void GraphRewritingSystem::add_rule(const RewriteRule& rule) {
  RewriteRule rule_copy = rule;
  if (rule_copy.id == 0) {
    rule_copy.id = next_rule_id_++;
  } else if (rule.id >= next_rule_id_) {
    next_rule_id_ = rule.id + 1;
  }
  rules_[rule_copy.id] = rule_copy;
}

void GraphRewritingSystem::remove_rule(uint32_t rule_id) {
  rules_.erase(rule_id);
}

std::vector<uint32_t> GraphRewritingSystem::get_neighbors(uint32_t node_id, EdgeType type) const {
  auto it = nodes_.find(node_id);
  if (it == nodes_.end()) return {};
  
  std::vector<uint32_t> result;
  for (uint32_t edge_id : it->second.out_edges) {
    const auto& edge = edges_.at(edge_id);
    if (type == EdgeType::RelatedTo || edge.data.type == type) {
      result.push_back(edge.data.target);
    }
  }
  return result;
}

std::vector<uint32_t> GraphRewritingSystem::get_nodes_by_type(NodeType type) const {
  std::vector<uint32_t> result;
  for (const auto& kv : nodes_) {
    if (kv.second.data.type == type) result.push_back(kv.first);
  }
  return result;
}

std::vector<uint32_t> GraphRewritingSystem::find_nodes_by_label(const std::string& label) const {
  std::vector<uint32_t> result;
  for (const auto& kv : nodes_) {
    if (kv.second.data.label == label) result.push_back(kv.first);
  }
  return result;
}

std::vector<uint32_t> GraphRewritingSystem::find_path(uint32_t source, uint32_t target, uint32_t max_depth) const {
  if (source == target) return {source};
  if (nodes_.find(source) == nodes_.end() || nodes_.find(target) == nodes_.end()) return {};
  
  std::queue<std::pair<uint32_t, std::vector<uint32_t>>> q;
  std::unordered_set<uint32_t> visited;
  q.push({source, {source}});
  visited.insert(source);
  
  while (!q.empty()) {
    auto [node, path] = q.front(); q.pop();
    if (path.size() > max_depth) continue;
    
    for (uint32_t edge_id : nodes_.at(node).out_edges) {
      const auto& edge = edges_.at(edge_id);
      if (visited.count(edge.data.target)) continue;
      if (edge.data.target == target) {
        std::vector<uint32_t> result = path;
        result.push_back(target);
        return result;
      }
      visited.insert(edge.data.target);
      std::vector<uint32_t> new_path = path;
      new_path.push_back(edge.data.target);
      q.push({edge.data.target, new_path});
    }
  }
  return {};
}

using Match = std::vector<uint32_t>;

std::vector<std::vector<uint32_t>> GraphRewritingSystem::match_pattern(const RewriteRule::Pattern& pattern) const {
  std::vector<std::vector<uint32_t>> matches;
  
  if (pattern.node_types.empty()) return {};
  
  std::vector<uint32_t> candidates = get_nodes_by_type(static_cast<NodeType>(pattern.node_types[0]));
  if (candidates.empty()) return {};
  
  if (!pattern.node_constraints.empty() && pattern.node_constraints.count(0)) {
    const auto& constraints = pattern.node_constraints.at(0);
    auto it = candidates.begin();
    while (it != candidates.end()) {
      const auto& node = nodes_.at(*it).data;
      bool ok = true;
      for (const auto& c : constraints) {
        auto it2 = node.properties.find(c.first);
        if (it2 == node.properties.end() || it2->second != c.second) {
          ok = false; break;
        }
      }
      if (!ok) it = candidates.erase(it);
      else ++it;
    }
  }
  
  for (uint32_t start : candidates) {
    std::vector<uint32_t> match(pattern.node_types.size(), 0);
    match[0] = start;
    if (extend_match(pattern, 1, {start}, match)) {
      matches.push_back(match);
    }
  }
  return matches;
}

bool GraphRewritingSystem::match_node(const Node& node, const RewriteRule::Pattern& pattern,
                                      size_t pattern_idx, const std::vector<uint32_t>& /*match*/) const {
  if (pattern_idx >= pattern.node_types.size()) return true;
  if (node.data.type != static_cast<NodeType>(pattern.node_types[pattern_idx])) return false;
  
  auto it = pattern.node_constraints.find(pattern_idx);
  if (it != pattern.node_constraints.end()) {
    for (const auto& c : it->second) {
      auto pit = node.data.properties.find(c.first);
      if (pit == node.data.properties.end() || pit->second != c.second) {
        return false;
      }
    }
  }
  return true;
}

bool GraphRewritingSystem::match_edges(const Node& /*node*/, const RewriteRule::Pattern& pattern,
                                       size_t /*pattern_idx*/, const std::vector<uint32_t>& match) const {
  for (const auto& [target_idx, edge_type] : pattern.edges) {
    if (target_idx >= match.size()) return false;
    uint32_t target_id = match[target_idx];
    for (uint32_t edge_id : nodes_.at(match.back()).out_edges) {
      const auto& edge = edges_.at(edge_id);
      if (edge.data.target == target_id && 
          (pattern.edges.empty() || edge.data.type == pattern.edges[0].second)) {
        return true;
      }
    }
    return false;
  }
  return true;
}

bool GraphRewritingSystem::extend_match(const RewriteRule::Pattern& pattern, size_t next_idx,
                                         const std::vector<uint32_t>& /*partial_match*/,
                                         std::vector<uint32_t>& match) const {
  if (next_idx >= pattern.node_types.size()) return true;
  
  NodeType required_type = static_cast<NodeType>(pattern.node_types[next_idx]);
  
  std::unordered_set<uint32_t> candidates;
  for (size_t i = 0; i < match.size(); ++i) {
    if (match[i] == 0) continue;
    for (uint32_t edge_id : nodes_.at(match[i]).out_edges) {
      candidates.insert(edges_.at(edge_id).data.target);
    }
    for (uint32_t edge_id : nodes_.at(match[i]).in_edges) {
      candidates.insert(edges_.at(edge_id).data.source);
    }
  }
  
  if (candidates.empty()) {
    for (uint32_t id : get_nodes_by_type(required_type)) {
      candidates.insert(id);
    }
  }
  
  for (uint32_t cand : candidates) {
    const auto& node = nodes_.at(cand);
    if (!match_node(node, pattern, next_idx, match)) continue;
    
    bool edges_ok = true;
    for (const auto& [target_idx, edge_type] : pattern.edges) {
      if (target_idx >= match.size()) continue;
      bool found = false;
      for (uint32_t edge_id : nodes_.at(cand).out_edges) {
        const auto& edge = edges_.at(edge_id);
        if (edge.data.target == match[target_idx] && edge.data.type == edge_type) {
          found = true; break;
        }
      }
      for (uint32_t edge_id : nodes_.at(cand).in_edges) {
        const auto& edge = edges_.at(edge_id);
        if (edge.data.source == match[target_idx] && edge.data.type == edge_type) {
          found = true; break;
        }
      }
      if (!found) { edges_ok = false; break; }
    }
    if (!edges_ok) continue;
    
    if (!match_node(nodes_.at(cand), pattern, next_idx, match)) continue;
    
    match.push_back(cand);
    if (next_idx + 1 < pattern.node_types.size()) {
      if (!extend_match(pattern, next_idx + 1, match, match)) {
        match.pop_back();
        continue;
      }
      return true;
    }
    return true;
  }
  return false;
}

size_t GraphRewritingSystem::apply_rules(size_t max_applications, class Rng& rng) {
  size_t total_applied = 0;
  
  for (auto& [rule_id, rule] : rules_) {
    if (rule.max_applications > 0 && rule.applications >= rule.max_applications) continue;
    
    auto matches = match_pattern(rule.pattern);
    for (const auto& match : matches) {
      if (apply_rule_at_match(rules_[rule_id], match, rng)) {
        total_applied++;
        if (total_applied >= max_applications) return total_applied;
      }
    }
  }
  return total_applied;
}

bool GraphRewritingSystem::apply_rule_at_match(const RewriteRule& rule, const std::vector<uint32_t>& match, class Rng& /*rng*/) {
  if (rule.max_applications > 0 && rule.applications >= rule.max_applications) return false;
  
  for (uint32_t idx : rule.replacement.nodes_to_remove) {
    if (idx < match.size()) {
      remove_node_internal(match[idx]);
    }
  }
  
  for (const auto& [src_idx, tgt_idx] : rule.replacement.edges_to_remove) {
    if (src_idx < match.size() && tgt_idx < match.size()) {
      uint32_t src = match[src_idx];
      uint32_t tgt = match[tgt_idx];
      (void)src; (void)tgt;
      for (uint32_t edge_id : nodes_[match[0]].out_edges) {
        const auto& edge = edges_.at(edge_id);
        if (edge.data.source == match[0] && edge.data.target == match[1]) {
          remove_edge_internal(edge_id);
          break;
        }
      }
    }
  }
  
  std::vector<uint32_t> new_node_ids;
  for (const auto& [type, label] : rule.replacement.nodes_to_add) {
    uint32_t id = add_node(type, label, {});
    new_node_ids.push_back(id);
  }
  
  for (const auto& [src_idx, tgt_idx, edge_type, weight] : rule.replacement.edges_to_add) {
    uint32_t src;
    if (src_idx < match.size()) src = match[src_idx];
    else if (src_idx < match.size() + new_node_ids.size()) src = new_node_ids[src_idx - match.size()];
    else continue;
    
    uint32_t tgt;
    if (tgt_idx < match.size()) tgt = match[tgt_idx];
    else if (tgt_idx < match.size() + new_node_ids.size()) tgt = new_node_ids[tgt_idx - match.size()];
    else continue;
    
    add_edge(src, tgt, edge_type, 1.0f, 1.0f, "");
  }
  
  return true;
}

void GraphRewritingSystem::sync_with_concepts(const ConceptFormation& concepts) {
  for (const auto& c : concepts.get_concepts()) {
    std::unordered_map<std::string, std::string> props;
    props["cohesion"] = std::to_string(c.cohesion);
    props["separation"] = std::to_string(c.separation);
    props["usage"] = std::to_string(c.usage_count);
    
    add_node(NodeType::Concept, c.name, props);
  }
}

GraphRewritingSystem::Subgraph GraphRewritingSystem::get_subgraph_for_concept(uint32_t /*concept_id*/) const {
  Subgraph sg;
  return sg;
}

void GraphRewritingSystem::serialize(struct BinaryWriter& w) const {
  w.u32(static_cast<uint32_t>(nodes_.size()));
  for (const auto& kv : nodes_) {
    kv.second.data.serialize(w);
  }
  w.u32(static_cast<uint32_t>(edges_.size()));
  for (const auto& kv : edges_) {
    kv.second.data.serialize(w);
  }
  w.u32(static_cast<uint32_t>(rules_.size()));
  for (const auto& kv : rules_) {
    kv.second.serialize(w);
  }
  w.u32(next_node_id_);
  w.u32(next_edge_id_);
  w.u32(next_rule_id_);
}

bool GraphRewritingSystem::deserialize(struct BinaryReader& r) {
  uint32_t n;
  if (!r.u32(n)) return false;
  for (uint32_t i = 0; i < n; ++i) {
    Node node;
    if (!node.data.deserialize(r)) return false;
    nodes_[node.data.id] = node;
  }
  uint32_t m;
  if (!r.u32(m)) return false;
  for (uint32_t i = 0; i < m; ++i) {
    Edge edge;
    if (!edge.data.deserialize(r)) return false;
    edges_[edge.data.id] = edge;
  }
  uint32_t rn;
  if (!r.u32(rn)) return false;
  for (uint32_t i = 0; i < rn; ++i) {
    RewriteRule rule;
    if (!rule.deserialize(r)) return false;
    rules_[rule.id] = rule;
  }
  if (!r.u32(next_node_id_) || !r.u32(next_edge_id_) || !r.u32(next_rule_id_))
    return false;
  return true;
}

} // namespace eidolon
