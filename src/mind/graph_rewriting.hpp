#ifndef EIDOLON_GRAPH_REWRITING_HPP
#define EIDOLON_GRAPH_REWRITING_HPP

#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstdint>
#include <functional>
#include <sstream>

#include "core/serialize.hpp"
#include "mind/concept_formation.hpp"

namespace eidolon {

enum class NodeType : uint8_t {
  Concept = 0,
  Relation = 1,
  Property = 2,
  Event = 3,
  Category = 4,
};

enum class EdgeType : uint8_t {
  IsA = 0,
  HasProperty = 1,
  Causes = 2,
  PartOf = 3,
  RelatedTo = 4,
  Opposes = 5,
  Enables = 6,
};

struct GraphNode {
  uint32_t id = 0;
  NodeType type = NodeType::Concept;
  std::string label;
  std::unordered_map<std::string, std::string> properties;
  uint64_t created_at = 0;
  uint64_t updated_at = 0;
  float confidence = 1.0f;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct GraphEdge {
  uint32_t id = 0;
  uint32_t source = 0;
  uint32_t target = 0;
  EdgeType type = EdgeType::RelatedTo;
  float weight = 1.0f;
  float confidence = 1.0f;
  uint64_t created_at = 0;
  std::string evidence;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

struct RewriteRule {
  uint32_t id = 0;
  std::string name;
  std::string description;
  
  struct Pattern {
    std::vector<uint32_t> node_types;
    std::vector<std::pair<uint32_t, EdgeType>> edges;
    std::unordered_map<uint32_t, std::unordered_map<std::string, std::string>> node_constraints;
  } pattern;
  
  struct Replacement {
    std::vector<std::pair<NodeType, std::string>> nodes_to_add;
    std::vector<std::tuple<uint32_t, uint32_t, EdgeType, float>> edges_to_add;
    std::vector<uint32_t> nodes_to_remove;
    std::vector<std::pair<uint32_t, uint32_t>> edges_to_remove;
  } replacement;
  
  float priority = 1.0f;
  uint32_t min_confidence = 50;
  uint32_t max_applications = 0;
  uint32_t applications = 0;
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
};

class GraphRewritingSystem {
public:
  GraphRewritingSystem() = default;
  explicit GraphRewritingSystem(uint64_t seed);
  
  uint32_t add_node(NodeType type, const std::string& label, const std::unordered_map<std::string, std::string>& props = {});
  void remove_node(uint32_t node_id);
  void add_edge(uint32_t source, uint32_t target, EdgeType type, float weight = 1.0f, float confidence = 1.0f, const std::string& evidence = "");
  void remove_edge(uint32_t edge_id);
  
  std::vector<uint32_t> get_neighbors(uint32_t node_id, EdgeType type = EdgeType::RelatedTo) const;
  std::vector<uint32_t> get_nodes_by_type(NodeType type) const;
  std::vector<uint32_t> find_nodes_by_label(const std::string& label) const;
  std::vector<uint32_t> find_path(uint32_t source, uint32_t target, uint32_t max_depth = 5) const;
  
  void add_rule(const RewriteRule& rule);
  void remove_rule(uint32_t rule_id);
  
  size_t apply_rules(size_t max_applications, class Rng& rng);
  
  using Match = std::vector<uint32_t>;
  std::vector<std::vector<uint32_t>> match_pattern(const RewriteRule::Pattern& pattern) const;
  
  bool apply_rule_at_match(const RewriteRule& rule, const std::vector<uint32_t>& match, class Rng& /*rng*/);
  
  void sync_with_concepts(const ConceptFormation& concepts);
  
  struct Subgraph {
    std::vector<uint32_t> nodes;
    std::vector<uint32_t> edges;
  };
  Subgraph get_subgraph_for_concept(uint32_t /*concept_id*/) const;
  
  size_t node_count() const { return nodes_.size(); }
  size_t edge_count() const { return edges_.size(); }
  size_t rule_count() const { return rules_.size(); }
  
  void serialize(struct BinaryWriter& w) const;
  bool deserialize(struct BinaryReader& r);
  
private:
  uint64_t seed_;
  class Rng rng_;
  
  struct Node {
    struct GraphNode data;
    std::vector<uint32_t> out_edges;
    std::vector<uint32_t> in_edges;
  };
  struct Edge {
    struct GraphEdge data;
  };
  
  std::unordered_map<uint32_t, struct Node> nodes_;
  std::unordered_map<uint32_t, struct Edge> edges_;
  std::unordered_map<uint32_t, RewriteRule> rules_;
  uint32_t next_node_id_ = 1;
  uint32_t next_edge_id_ = 1;
  uint32_t next_rule_id_ = 1;
  
  bool match_node(const struct Node& node, const RewriteRule::Pattern& pattern,
                  size_t pattern_idx, const std::vector<uint32_t>& match) const;
  bool match_edges(const struct Node& /*node*/, const RewriteRule::Pattern& pattern,
                   size_t /*pattern_idx*/, const std::vector<uint32_t>& match) const;
  bool extend_match(const RewriteRule::Pattern& pattern, size_t next_idx,
                    const std::vector<uint32_t>& /*partial_match*/,
                    std::vector<uint32_t>& match) const;
  void apply_replacement(const RewriteRule& rule, const std::vector<uint32_t>& match,
                         class Rng& /*rng*/);
  void remove_node_internal(uint32_t node_id);
  void remove_edge_internal(uint32_t edge_id);
};

} // namespace eidolon

#endif // EIDOLON_GRAPH_REWRITING_HPP
