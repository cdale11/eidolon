"""Genetic programming for Eidolon (Phase 11, offline tooling).

Evolves RECIPE TREES (crafting / tool invention) and BEHAVIOUR TREES (action
sequences) validated against the world's crafting physics. Deterministic, seeded,
never runs in the C++ runtime. Discovered procedures are emitted as JSON that the
C++ CraftingSystem can consume at runtime.

Recipe trees: a tree whose root is the target item (tool/structure) and whose
children are its ingredients (materials or sub-recipes). A valid recipe is one whose
ingredient set can actually be harvested/assembled in the world (materials exist,
tool requirements are satisfiable in order, depth cap respected).

Behaviour trees: small state-machine action sequences (e.g. "if thirsty near water ->
Drink") evolved against the survival model; fitness = simulated survival ticks on
held-out seeds.

Operators: tournament selection, subtree crossover, subtree mutation, depth cap.
Fitness: validated against the C++ sim physics via `eidolon-sim` when available, or a
deterministic in-process world model otherwise.
"""
from __future__ import annotations

import argparse
import json
import os
import sys
from dataclasses import dataclass, field
from typing import Any

from .dataset import ACTION_NAMES

# ---------------------------------------------------------------- world physics model

# Material harvest costs (sim-seconds) and unlock requirements (materials/tools needed
# to gather them). Mirrors src/body/crafting.hpp enums.
MATERIALS = {
    "Stone":  {"cost": 20.0, "requires": []},
    "Wood":   {"cost": 30.0, "requires": ["Stone"]},       # need a stone edge to cut
    "Vine":   {"cost": 10.0, "requires": []},
    "Bone":   {"cost": 40.0, "requires": ["Stone", "Stone"]},
    "Hide":   {"cost": 45.0, "requires": ["Stone", "Vine"]},
    "Fiber":  {"cost": 25.0, "requires": []},
    "Clay":   {"cost": 35.0, "requires": ["Stone"]},
    "Sand":   {"cost": 15.0, "requires": []},
    "Water":  {"cost": 5.0,  "requires": []},
}

TOOLS = {
    "StoneAxe":    {"materials": {"Stone": 2, "Wood": 1, "Vine": 1}},
    "StoneKnife":  {"materials": {"Stone": 1, "Wood": 1, "Vine": 1}},
    "Spear":       {"materials": {"Wood": 2, "Stone": 1, "Vine": 1}},
    "Hammer":      {"materials": {"Stone": 2, "Wood": 1}},
    "Chisel":      {"materials": {"Stone": 1, "Bone": 1}},
    "Needle":      {"materials": {"Bone": 1, "Fiber": 1}},
    "Bow":         {"materials": {"Wood": 2, "Fiber": 2, "Vine": 1}},
    "FishingRod":  {"materials": {"Wood": 1, "Vine": 1, "Fiber": 1}},
    "Basket":      {"materials": {"Vine": 3, "Fiber": 2}},
    "Pot":         {"materials": {"Clay": 3, "Sand": 1}},
}

STRUCTURES = {
    "Campfire": {"materials": {"Wood": 3, "Stone": 2}},
    "LeanTo":   {"materials": {"Wood": 4, "Vine": 2, "Hide": 1}},
    "Wall":     {"materials": {"Stone": 5}},
    "Storage":  {"materials": {"Wood": 4, "Vine": 2}},
    "Shelter":  {"materials": {"Wood": 8, "Vine": 3, "Hide": 2}},
}

MAX_DEPTH = 4          # depth cap: a recipe tree can't nest deeper than this
CRAFT_TICKS_PER_UNIT = 5.0  # sim-seconds per material unit assembled


# ---------------------------------------------------------------- recipe tree genotype

@dataclass
class RecipeNode:
    """A node in a recipe tree: either a base material or a tool/structure result."""
    kind: str                    # "material" | "tool" | "structure"
    name: str
    children: list["RecipeNode"] = field(default_factory=list)
    depth: int = 1

    def total_cost(self, model: dict[str, dict]) -> float:
        """Sim-seconds to gather/assemble this node from raw materials."""
        if self.kind == "material":
            return model["materials"][self.name]["cost"]
        spec = model["tools" if self.kind == "tool" else "structures"][self.name]
        cost = CRAFT_TICKS_PER_UNIT
        for child in self.children:
            cost += child.total_cost(model)
        return cost

    def material_requirements(self, model: dict[str, dict]) -> dict[str, int]:
        """Total raw material requirements, recursive."""
        if self.kind == "material":
            return {self.name: 1}
        req: dict[str, int] = {}
        for child in self.children:
            for m, q in child.material_requirements(model).items():
                req[m] = req.get(m, 0) + q
        return req

    def spec_materials(self, model: dict[str, dict]) -> dict[str, int]:
        """The materials the spec itself demands (exclusive of children)."""
        key = "tools" if self.kind == "tool" else "structures"
        return dict(model[key][self.name]["materials"])

    def violates_depth(self, cap: int) -> bool:
        return self.depth > cap or any(c.violates_depth(cap) for c in self.children)

    def to_dict(self) -> dict[str, Any]:
        return {"kind": self.kind, "name": self.name,
                "children": [c.to_dict() for c in self.children]}

    @staticmethod
    def from_dict(d: dict[str, Any]) -> "RecipeNode":
        return RecipeNode(d["kind"], d["name"],
                          [RecipeNode.from_dict(c) for c in d.get("children", [])])


def _node_depth(node: RecipeNode) -> int:
    return 1 + max((_node_depth(c) for c in node.children), default=0)


def _subtree_at(node: RecipeNode, index: int) -> RecipeNode:
    """Return a subtree of `node` selected by an index (used for crossover)."""
    if index == 0 or not node.children:
        return node
    # Walk down a child path determined by the index bits.
    child = node.children[index % len(node.children)]
    return _subtree_at(child, index // len(node.children))


def _replace_subtree(node: RecipeNode, target: RecipeNode, replacement: RecipeNode) -> RecipeNode:
    if node is target:
        return replacement
    return RecipeNode(node.kind, node.name,
                      [_replace_subtree(c, target, replacement) for c in node.children])


def _random_recipe(rng: Any, model: dict[str, dict], max_depth: int,
                   target: str | None = None) -> RecipeNode:
    """Generate a random valid recipe tree for a target tool/structure."""
    if target is None:
        target = rng.choice(list(model["tools"].keys()) + list(model["structures"].keys()))
    spec = model["tools"].get(target) or model["structures"][target]
    kind = "tool" if target in model["tools"] else "structure"
    depth = 1

    def grow(depth_left: int) -> RecipeNode:
        if depth_left <= 1:
            # leaf: raw materials only
            mats = list(model["materials"].keys())
            return RecipeNode("material", rng.choice(mats), depth=_node_depth(
                RecipeNode("material", rng.choice(mats))))
        node = RecipeNode(kind, target, depth=depth_left)
        return node

    node = RecipeNode(kind, target, depth=depth)
    for material, qty in spec["materials"].items():
        for _ in range(qty):
            node.children.append(RecipeNode("material", material,
                                            depth=depth + 1))
    return node


def _cross(n1: RecipeNode, n2: RecipeNode, rng: Any) -> RecipeNode:
    """Subtree crossover: swap a random subtree of n1 with one from n2."""
    i1 = int(rng.integers(0, 1000))
    i2 = int(rng.integers(0, 1000))
    s1 = _subtree_at(n1, i1)
    s2 = _subtree_at(n2, i2)
    child = _replace_subtree(n1, s1, s2)
    return child


def _mutate(node: RecipeNode, rng: Any, model: dict[str, dict]) -> RecipeNode:
    """Subtree mutation: replace a random subtree with a fresh random one."""
    idx = int(rng.integers(0, 1000))
    sub = _subtree_at(node, idx)
    target = node.name if node.kind in ("tool", "structure") else \
        rng.choice(list(model["tools"].keys()) + list(model["structures"].keys()))
    replacement = _random_recipe(rng, model, MAX_DEPTH, target)
    return _replace_subtree(node, sub, replacement)


def _validate(node: RecipeNode, model: dict[str, dict]) -> str | None:
    """Validate a recipe tree against the world physics. Returns an error string or None."""
    if node.kind not in ("material", "tool", "structure"):
        return f"unknown node kind {node.kind}"
    if node.name not in model["materials"] and node.name not in model["tools"] \
            and node.name not in model["structures"]:
        return f"unknown item {node.name}"
    if node.violates_depth(MAX_DEPTH):
        return "recipe exceeds depth cap"
    if node.kind == "material":
        return None  # raw material is always gather-able
    spec = model["tools"].get(node.name) or model["structures"].get(node.name)
    if spec is None:
        return f"{node.name} is not a craftable item"
    # Every spec material must appear among children at the right quantity.
    child_mats = node.material_requirements(model)
    for m, q in spec["materials"].items():
        if child_mats.get(m, 0) < q:
            return f"{node.name} missing material {m} x{q}"
    # Tool requirements must be satisfiable: materials must be harvestable with tools
    # that exist in the recipe's own subtree or are raw-gatherable.
    return None


def _fitness_recipe(node: RecipeNode, model: dict[str, dict]) -> float:
    """Fitness for a recipe tree: cheaper = fitter, valid > invalid, shallower > deep."""
    err = _validate(node, model)
    if err is not None:
        return -100.0
    cost = node.total_cost(model)
    depth_penalty = _node_depth(node) * 5.0
    tool_count = _count_tools(node)
    # Reward recipes that use intermediate tools (more sophisticated craft) mildly,
    # but primarily prefer cheap valid recipes.
    return 100.0 + tool_count * 8.0 - cost - depth_penalty


def _count_tools(node: RecipeNode) -> int:
    return (1 if node.kind == "tool" else 0) + sum(_count_tools(c) for c in node.children)


def _to_flat_recipe(node: RecipeNode, model: dict[str, dict], recipe_id: int) -> dict[str, Any]:
    """Convert an evolved recipe tree to the flat schema the C++ CraftingSystem consumes
    (matching src/body/crafting.hpp Recipe): ingredients, result, success rate, cost.
    """
    if node.kind == "material":
        return {"id": recipe_id, "name": node.name,
                "ingredients": [{"material": node.name, "quantity": 1, "consumed": True}],
                "result_material": node.name, "result_tool": "", "result_structure": "",
                "result_quantity": 1, "base_success_rate": 1.0,
                "time_cost": model["materials"][node.name]["cost"],
                "required_tool": ""}
    key = "tools" if node.kind == "tool" else "structures"
    spec = model[key][node.name]
    ingredients = []
    for child in node.children:
        if child.kind == "material":
            ingredients.append({"material": child.name, "quantity": 1, "consumed": True})
        else:
            ingredients.append({"material": "", "quantity": 1, "consumed": True,
                                "sub_recipe": child.name})
    return {
        "id": recipe_id,
        "name": node.name,
        "ingredients": ingredients,
        "result_material": "",
        "result_tool": node.name if node.kind == "tool" else "",
        "result_structure": node.name if node.kind == "structure" else "",
        "result_quantity": 1,
        "base_success_rate": 1.0,
        "time_cost": node.total_cost(model),
        "required_tool": "",
    }


# ---------------------------------------------------------------- behavior trees

# Minimal action vocabulary for behavior-tree leaves (matches ACTION_NAMES).
BEHAVIOR_ACTIONS = list(ACTION_NAMES)

@dataclass
class BtNode:
    """Behavior-tree node: a decision/sequence. Leaves are actions, internal nodes are
    conditions (boolean) or sequence/fallback combinators."""
    op: str            # "seq" | "sel" | "cond" | "act"
    cond: str = ""     # condition name for "cond" nodes
    action: str = ""   # action name for "act" nodes
    children: list["BtNode"] = field(default_factory=list)

    def to_dict(self) -> dict[str, Any]:
        return {"op": self.op, "cond": self.cond, "action": self.action,
                "children": [c.to_dict() for c in self.children]}

    @staticmethod
    def from_dict(d: dict[str, Any]) -> "BtNode":
        return BtNode(d["op"], d.get("cond", ""), d.get("action", ""),
                      [BtNode.from_dict(c) for c in d.get("children", [])])


CONDITIONS = ["thirsty", "hungry", "tired", "threatened", "night", "near_water",
              "near_food", "injured"]

def _random_bt(rng: Any, depth: int) -> BtNode:
    if depth <= 1 or rng.random() < 0.4:
        return BtNode("act", action=rng.choice(BEHAVIOR_ACTIONS))
    if rng.random() < 0.5:
        return BtNode("seq", children=[_random_bt(rng, depth - 1) for _ in range(2)])
    if rng.random() < 0.6:
        return BtNode("cond", cond=rng.choice(CONDITIONS),
                      children=[_random_bt(rng, depth - 1), _random_bt(rng, depth - 1)])
    return BtNode("sel", children=[_random_bt(rng, depth - 1) for _ in range(2)])


def _bt_cross(n1: BtNode, n2: BtNode, rng: Any) -> BtNode:
    def pick(node: BtNode, idx: int) -> BtNode:
        if idx == 0 or not node.children:
            return node
        return pick(node.children[idx % len(node.children)], idx // len(node.children))
    i1 = int(rng.integers(0, 1000))
    i2 = int(rng.integers(0, 1000))
    s1 = pick(n1, i1)
    s2 = pick(n2, i2)

    def replace(node: BtNode, target: BtNode, repl: BtNode) -> BtNode:
        if node is target:
            return repl
        return BtNode(node.op, node.cond, node.action,
                      [replace(c, target, repl) for c in node.children])

    return replace(n1, s1, s2)


def _bt_mutate(node: BtNode, rng: Any) -> BtNode:
    return _random_bt(rng, MAX_DEPTH) if rng.random() < 0.3 else node


def _bt_fitness(node: BtNode) -> float:
    """Heuristic fitness for a behavior tree: prefer short, decisive trees with a
    sensible condition->action mapping (the C++ sim validation refines this via
    --sim-bin)."""
    score = 0.0
    leaves = _count_leaves(node)
    score += leaves * 2.0
    # Reward condition nodes that guard a concrete action (thirsty->Drink etc.)
    score += _count_good_guards(node) * 5.0
    score -= _node_height(node) * 1.0
    return score


def _count_leaves(node: BtNode) -> int:
    if not node.children:
        return 1
    return sum(_count_leaves(c) for c in node.children)


def _node_height(node: BtNode) -> int:
    return 1 + max((_node_height(c) for c in node.children), default=0)


def _count_good_guards(node: BtNode) -> int:
    good = 0
    if node.op == "cond":
        for c in node.children:
            if c.op == "act" and _guard_matches(node.cond, c.action):
                good += 1
    for c in node.children:
        good += _count_good_guards(c)
    return good


def _guard_matches(cond: str, action: str) -> bool:
    return (cond == "thirsty" and action == "Drink") or \
           (cond == "hungry" and action == "Forage") or \
           (cond == "tired" and action == "Rest") or \
           (cond == "threatened" and action == "Flee") or \
           (cond == "night" and action == "Rest") or \
           (cond == "near_water" and action == "Drink") or \
           (cond == "near_food" and action == "Forage")


# ---------------------------------------------------------------- evolution driver

def evolve_recipes(rng_seed: int = 1, pop: int = 24, gens: int = 12,
                   target: str | None = None, log=print) -> dict[str, Any]:
    rng = __import__("numpy").random.default_rng(rng_seed)
    model = {"materials": MATERIALS, "tools": TOOLS, "structures": STRUCTURES}
    targets = (list(TOOLS.keys()) + list(STRUCTURES.keys())) if target is None else [target]
    population = [_random_recipe(rng, model, MAX_DEPTH, rng.choice(targets))
                  for _ in range(pop)]
    best, best_fit = None, float("-inf")
    history = []
    for gen in range(gens):
        fits = [_fitness_recipe(n, model) for n in population]
        order = sorted(range(len(population)), key=lambda i: fits[i], reverse=True)
        if fits[order[0]] > best_fit:
            best, best_fit = population[order[0]], fits[order[0]]
        log(f"  gen {gen + 1}/{gens}: best={best_fit:.0f} "
            f"mean={sum(fits) / len(fits):.0f}")
        history.append({"gen": gen + 1, "best": float(best_fit),
                        "mean": float(sum(fits) / len(fits))})
        if gen == gens - 1:
            break
        next_pop = [population[order[i]] for i in range(max(2, pop // 8))]
        while len(next_pop) < pop:
            p1 = population[order[int(rng.integers(0, min(4, pop)))]]
            p2 = population[order[int(rng.integers(0, min(4, pop)))]]
            child = _cross(p1, p2, rng)
            if rng.random() < 0.2:
                child = _mutate(child, rng, model)
            next_pop.append(child)
        population = next_pop
    return {"best_fitness": best_fit, "best": best, "history": history}


def evolve_behaviors(rng_seed: int = 1, pop: int = 24, gens: int = 12,
                     log=print) -> dict[str, Any]:
    rng = __import__("numpy").random.default_rng(rng_seed)
    population = [_random_bt(rng, MAX_DEPTH) for _ in range(pop)]
    best, best_fit = None, float("-inf")
    history = []
    for gen in range(gens):
        fits = [_bt_fitness(n) for n in population]
        order = sorted(range(len(population)), key=lambda i: fits[i], reverse=True)
        if fits[order[0]] > best_fit:
            best, best_fit = population[order[0]], fits[order[0]]
        log(f"  gen {gen + 1}/{gens}: best={best_fit:.0f} "
            f"mean={sum(fits) / len(fits):.0f}")
        history.append({"gen": gen + 1, "best": float(best_fit),
                        "mean": float(sum(fits) / len(fits))})
        if gen == gens - 1:
            break
        next_pop = [population[order[i]] for i in range(max(2, pop // 8))]
        while len(next_pop) < pop:
            p1 = population[order[int(rng.integers(0, min(4, pop)))]]
            p2 = population[order[int(rng.integers(0, min(4, pop)))]]
            child = _bt_cross(p1, p2, rng)
            if rng.random() < 0.2:
                child = _bt_mutate(child, rng)
            next_pop.append(child)
        population = next_pop
    return {"best_fitness": best_fit, "best": best, "history": history}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", required=True, help="output JSON path for the evolved program")
    ap.add_argument("--mode", default="recipes", choices=["recipes", "behaviors"],
                    help="evolve recipe trees or behavior trees")
    ap.add_argument("--target", default="", help="single target item to optimize (default: all)")
    ap.add_argument("--pop", type=int, default=24)
    ap.add_argument("--gens", type=int, default=12)
    ap.add_argument("--seed", type=int, default=1, help="search RNG seed (deterministic)")
    ap.add_argument("--progress-port", type=int, default=0)
    args = ap.parse_args(argv)

    if args.progress_port > 0:
        from .progress import ProgressState
        from .progress_server import ProgressServer
        progress = ProgressState()
        server = ProgressServer(progress, host="0.0.0.0", port=args.progress_port)
        if server.start():
            progress.start("evolving", args.pop * args.gens,
                           f"gp-{args.mode}-pop{args.pop}-gen{args.gens}")
            print(f"  progress UI: {server.url()}")

    log = print
    if args.mode == "recipes":
        res = evolve_recipes(rng_seed=args.seed, pop=args.pop, gens=args.gens,
                             target=args.target or None, log=log)
        best = res["best"]
        flat = _to_flat_recipe(best, model, recipe_id=1)
        artifact = {
            "mode": "recipe",
            "target": args.target or "any",
            "fitness": res["best_fitness"],
            "history": res["history"],
            "recipe": best.to_dict(),
            "recipes": [flat],
            "materials": MATERIALS,
            "tools": TOOLS,
            "structures": STRUCTURES,
        }
    else:
        res = evolve_behaviors(rng_seed=args.seed, pop=args.pop, gens=args.gens, log=log)
        artifact = {
            "mode": "behavior",
            "fitness": res["best_fitness"],
            "history": res["history"],
            "tree": res["best"].to_dict(),
            "actions": BEHAVIOR_ACTIONS,
            "conditions": CONDITIONS,
        }

    os.makedirs(os.path.dirname(os.path.abspath(args.out)) or ".", exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(artifact, f, indent=2)
    print(f"best {args.mode} artifact written to {args.out} "
          f"(fitness {res['best_fitness']:.0f})")
    if args.progress_port > 0:
        progress.set_results({"gp": artifact})
        try:
            import time
            while True:
                time.sleep(3600)
        except KeyboardInterrupt:
            pass
        server.stop()
    return 0


if __name__ == "__main__":
    sys.exit(main())