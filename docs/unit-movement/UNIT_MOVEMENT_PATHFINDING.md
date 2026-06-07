# Unit Movement & Pathfinding System

## Overview

The Community Patch DLL implements a comprehensive unit movement and pathfinding system for Civilization V. This document reviews the key subsystems: movement points, zones of control (ZOC), embarkation/disembarkation, the A* pathfinder, and movement flags.

---

## 1. Movement Points System

### Core Concept
Movement points (MP) in Civ5 are tracked in units of 1/60th of a tile, allowing fine-grained movement costs. The denominator is configurable via `MOVE_DENOMINATOR` global data (default: 60).

**Key Files:**
- [CvUnitMovement.h](../../CvGameCoreDLL_Expansion2/CvUnitMovement.h) — movement cost calculations
- [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp) — implementation

### Movement Cost Calculation

#### `GetCostsForMove()`
Computes the cost to move from one plot to another, considering:

1. **Route Bonuses** — roads/railroads provide reduced costs
   - River movement bonuses (trait-based)
   - Woodland movement bonuses (forest/jungle traversal)
   - Fake routes created by traits

2. **Embarkation States**
   - Transitioning land→water or water→land may incur full-turn cost
   - City-based bonuses can reduce embark/disembark costs
   - Traits and promotions can enable flat-cost embarkation

3. **Terrain & Feature Costs**
   - Base terrain cost (e.g., hills +1 MP)
   - Feature cost overrides terrain cost (except hills/mountains)
   - Promotions can ignore terrain/feature costs
   - River crossing penalties (unless amphibious/bridging)

4. **Promotion Modifiers**
   - Cost multipliers (VP terrain specialist units)
   - Cost adders (reduction in cost)
   - Promotion-based cost changes (additive modifiers)

**Flow:**
```
GetCostsForMove(pUnit, pFromPlot, pToPlot)
  → Check routes (road/railroad bonuses)
  → Check embarkation transitions
  → Check terrain/feature costs
  → Apply promotion modifiers
  → Return movement cost (in units of 1/60 tile)
```

### Movement Variants

**`MovementCost()`** — Standard movement accounting for ZOC
**`MovementCostSelectiveZOC()`** — Movement with selective ZOC application (ignore some enemy units)
**`MovementCostNoZOC()`** — Ignores ZOC entirely (e.g., for reachable-plots calculations)

### Issues & Improvements

#### Sane Unit Movement Cost (MOD_BALANCE_SANE_UNIT_MOVEMENT_COST)
- **Issue:** Vanilla system uses multiplicative terrain cost modifiers that can result in unintuitive costs.
- **Improvement:** When enabled, terrain cost changes are applied as additive offsets instead, making promotions more predictable.
- **Status:** Conditional compilation flag (checked in `CvUnitMovement.cpp:59`, `CvAStar.cpp:1002`)

#### Promotion Cost Modifiers
- **Function:** `GetMovementCostMultiplierFromPromotions()`, `GetMovementCostAdderFromPromotions()`
- **Note:** Distinction between additive vs. multiplicative approaches affects unit balance and micro-management burden.

#### Route-Based Movement
- Routes are often the largest movement cost reduction. The system must carefully handle:
  - Hybrid routes (part road, part unroaded)
  - Trait-based "fake routes" (rivers, forests)
  - Bridge building for river crossing
  - Scaling with team bonuses

---

## 2. Zones of Control (ZOC)

### Core Concept
A ZOC is created by enemy combat units, slowing or blocking unit movement through adjacent tiles.

**Key Files:**
- [CvUnitMovement.h](../../CvGameCoreDLL_Expansion2/CvUnitMovement.h) — ZOC interface
- [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp) — `IsSlowedByZOC()` implementation
- [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp) — pathfinder integration

### IsSlowedByZOC()

**Signature:**
```cpp
static bool IsSlowedByZOC(const CvUnit* pUnit, const CvPlot* pFromPlot, const CvPlot* pToPlot);
static bool IsSlowedByZOC(const CvUnit* pUnit, const CvPlot* pFromPlot, const CvPlot* pToPlot, const PlotIndexContainer& plotsToIgnore);
```

**Behavior:**
- Checks if moving from `pFromPlot` to `pToPlot` crosses an enemy ZOC
- If yes, the move costs more (ends the unit's turn)
- Overloaded version accepts a set of plots to ignore (e.g., for escort units or allies)

**Mechanics:**
- Only enemy combat units create a ZOC
- Cities do NOT create ZOC in Civ5
- Units are slowed when entering an enemy ZOC, not when exiting one

### Pathfinder Integration

**Flags:**
- `MOVEFLAG_IGNORE_ZOC` — disable ZOC penalties entirely
- `MOVEFLAG_SELECTIVE_ZOC` — ignore ZOC from specific enemy units (stored in `plotsToIgnoreForZOC`)

**Cost Functions:**
In `CvAStar.cpp`, pathfinding applies ZOC via:
```cpp
iMovementCost = CvUnitMovement::MovementCostSelectiveZOC(..., data.plotsToIgnoreForZOC);
```
or
```cpp
iMovementCost = CvUnitMovement::MovementCost(...);  // includes ZOC
```

### Issues & Improvements

#### Selective ZOC Implementation
- **Purpose:** Allow AI to compute danger avoidance without hard-coding which enemy units to avoid.
- **Status:** Functional but requires pre-population of `plotsToIgnoreForZOC` by the caller.
- **Review Point:** Verify that all call sites correctly pass the ignore list, especially for multi-unit groups.

#### ZOC and Pathfinding Performance
- **Concern:** Computing ZOC for every tile in a path can be expensive.
- **Optimization:** Cache enemy unit positions and ZOC coverage at the start of each pathfind operation.
- **Status:** Partially mitigated by node cache data (see `CvPathNodeCacheData`).

#### No City ZOC
- **Rationale:** In Civ5, cities don't block movement directly; enemies inside do.
- **Note:** This differs from some mods that add city ZOC. Current design is vanilla-compatible.

---

## 3. Embarkation & Disembarkation

### Core Concept
Embarked units move on water; disembarked units move on land. The transition between states is restricted and may consume movement points.

**Key Files:**
- [CvUnit.h](../../CvGameCoreDLL_Expansion2/CvUnit.h) — embark state flags and methods
- [CvDllUnit.cpp](../../CvGameCoreDLL_Expansion2/CvDllUnit.cpp) — `CanEmbarkOnto()`, `CanDisembarkOnto()`
- [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp) — embark cost logic
- [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp) — embark/disembark validation in pathfinding

### Embark/Disembark Costs

**Three Cost Tiers:**

1. **Full Cost (Turn-Ending)** — embark/disembark without special promotions or traits
   - Cost: `INT_MAX` (ends the turn)
   
2. **Cheap Cost (1 MP)** — flat-cost embarkation via:
   - Trait: `IsEmbarkedToLandFlatCost()`
   - Promotion: `isEmbarkFlatCost()`, `isDisembarkFlatCost()`
   - City bonus: `isCityLessEmbarkCost()` (50% cost reduction)
   
3. **Free Cost (Cover Charge)** — no movement cost but with a nominal cover charge
   - City bonus: `isCityNoEmbarkCost()`
   - Cost: `MOVE_DENOMINATOR / 10` (1/10 of a normal move)

**Code Flow (CvUnitMovement.cpp:100-140):**
```cpp
if ((bToIsWater != bFromIsWater) && pUnit->CanEverEmbark())
{
    // Determine transition type (land→water or water→land)
    bool bFromEmbark = pFromPlot->needsEmbarkation(pUnit);
    bool bToEmbark = pToPlot->needsEmbarkation(pUnit);

    // Check for free/cheap transitions (traits, promotions, cities)
    if (pTraits->IsEmbarkedToLandFlatCost() || pUnit->isDisembarkFlatCost())
        bCheapEmbarkStateChange = true;
    
    // Apply city bonuses
    if (pToPlot->isCoastalCityOrPassableImprovement(...))
    {
        if (kUnitTeam->isCityNoEmbarkCost())
            bFreeEmbarkStateChange = true;
        else if (kUnitTeam->isCityLessEmbarkCost())
            bCheapEmbarkStateChange = true;
    }

    // Return cost based on transition type
    if (bFullCostEmbarkStateChange)
        return INT_MAX;
    else if (bFreeEmbarkStateChange)
        return iMoveDenominator / 10;
    else if (bCheapEmbarkStateChange)
        return iMoveDenominator;  // 1 MP
}
```

### Embark/Disembark Checks in Pathfinding

**Functions:**
- `canEmbarkOnto(pFromPlot, pToPlot, bOverride, iFlags)` — can the unit embark at this location?
- `canDisembarkOnto(pFromPlot, pToPlot, bOverride, iFlags)` — can the unit disembark at this location?

**Pathfinder Integration (CvAStar.cpp:1544-1555):**
```cpp
if (!pUnit->canEmbarkOnto(*pFromPlot, *pToPlot, true, kToNodeCacheData.iMoveFlags))
    return NS_FORBIDDEN;  // Cannot embark here; don't explore this path

if (!pUnit->canDisembarkOnto(*pFromPlot, *pToPlot, true, kToNodeCacheData.iMoveFlags))
    return NS_FORBIDDEN;  // Cannot disembark here; don't explore this path
```

### Deep Water Embarkation (MOD_PROMOTIONS_DEEP_WATER_EMBARKATION)

**Feature:** Some units can embark/disembark in deep ocean (not just coastal tiles).

**Implementation:**
- Conditional check: `if (MOD_PROMOTIONS_DEEP_WATER_EMBARKATION && ...)`
- Promotion: `PROMOTION_DEEPWATER_EMBARKATION`
- Enables pathfinding through non-coastal ocean for eligible units

### Issues & Improvements

#### Cargo Ships (MOD_CARGO_SHIPS)
- **Issue:** Land units embarked on cargo ships need special handling.
- **Status:** Reduced movement cost for land units moving in shallow water via cargo ships.
- **Code Location:** [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:210)

#### Embark/Disembark Cost Clarity
- **Issue:** Three-tier cost system can be confusing for modders.
- **Improvement Suggestion:** Consolidate cost logic into a single function with clear documentation.
- **Review Point:** Verify that all three tiers (free, cheap, full) are consistently applied across pathfinding and unit movement.

#### City-Based Embark Bonuses
- **Issue:** City bonuses are checked multiple times (land→water and water→land separately).
- **Optimization:** Could cache whether a city allows free/cheap embarkation.

#### Missing Documentation
- **Issue:** No single place documents the full embark/disembark cost hierarchy.
- **Status:** Partially addressed in inline comments; could be expanded.

---

## 4. A* Pathfinder

### Core Architecture

**Key Files:**
- [CvAStar.h](../../CvGameCoreDLL_Expansion2/CvAStar.h) — pathfinder interface
- [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp) — A* algorithm implementation
- [CvAStarNode.h](../../CvGameCoreDLL_Expansion2/CvAStarNode.h) — node structure and path types
- [CvPathFinder.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp) — unit-specific path functions (embedded)

### A* Algorithm Overview

The pathfinder uses the **A* search algorithm** with a priority queue:

1. **Initialization:** Add start node to open list.
2. **Main Loop:**
   - Pop the best node (lowest f-cost = g-cost + h-cost) from open list.
   - Check if destination is reached; if so, success.
   - Expand neighbors and link children based on cost functions.
3. **Termination:** Either destination found or open list exhausted.

**Cost Components:**
- **g-cost** (known cost): Actual movement cost from start to current node.
- **h-cost** (heuristic): Estimated cost from current node to destination (must be admissible, i.e., never overestimate).
- **f-cost** (total): g-cost + h-cost.

### Path Types (enum PathType)

The pathfinder supports multiple path types, each with different cost functions:

| PathType | Purpose | Cost Function |
|----------|---------|---------------|
| `PT_UNIT_MOVEMENT` | Unit movement with ZOC, stacking, danger | `PathCost()` |
| `PT_GENERIC_SAME_AREA` | Land-only or water-only paths | `StepPathCost()` |
| `PT_ARMY_LAND` | Army movement on land | `StepPathCost()` |
| `PT_ARMY_WATER` | Army movement on water | `StepPathCost()` |
| `PT_TRADE_LAND` / `PT_TRADE_WATER` | Trade route connectivity | `TradePathCost()` |
| `PT_BUILD_ROUTE` / `PT_BUILD_ROUTE_MIXED` | Route building (workers) | `RoutePathCost()` |
| `PT_CITY_INFLUENCE` | City border expansion | `CityInfluenceCost()` |
| `PT_AIR_REBASE` | Aircraft rebasing (carriers/cities) | `AirRebaseCost()` |
| `PT_LAND_UNIT_SIMPLE` | Simple distance estimate (land) | `SimplePathCost()` |

### Unit Movement Path (PT_UNIT_MOVEMENT)

The most complex path type, handling individual unit movement with all features.

**Cost Function: `PathCost(parent, node, data, finder)`**

Located in [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp:1370+), this function:

1. **Retrieves cached node data** (terrain, features, enemies, stacking limits)
2. **Computes movement cost** using `CvUnitMovement::MovementCost()` variants
3. **Applies end-turn costs** via `PathEndTurnCost()`:
   - Defense modifier penalty
   - Foreign territory penalty
   - Water embarkation penalty
   - Danger/threat assessment
4. **Returns final cost** or `-1` if move is forbidden

**Key Cache Data (CvPathNodeCacheData):**
- `bIsRevealedToTeam`, `bPlotVisibleToTeam` — fog of war
- `bCanEnterTerrainIntermediate`, `bCanEnterTerrainPermanent` — terrain passability
- `bCanEnterTerritoryIntermediate`, `bCanEnterTerritoryPermanent` — territory access
- `bIsNonNativeDomain` — water for land units or vice versa
- `bIsVisibleEnemyUnit`, `bIsVisibleEnemyCombatUnit` — combat unit presence
- `plotMovementCostMultiplier`, `plotMovementCostAdder` — cached promotion modifiers

**Heuristic Function: `PathHeuristic(iCurrentX, iCurrentY, iNextX, iNextY, iDestX, iDestY)`**

Computes an admissible heuristic:
```cpp
return plotDistance(iNextX, iNextY, iDestX, iDestY) * PATH_BASE_COST * 4;
```

- Assumes unit can move ~4 tiles per turn (conservative estimate for fast units on roads)
- Ensures heuristic never overestimates actual cost

### Destination Validation

**Function: `PathDestValid(iToX, iToY, data, finder)`**

Validates the destination before pathfinding begins:
- Unit must be able to reach and stop at the destination
- Checks visibility, terrain, territory, and embarkation requirements
- Handles approximate destination modes (ring 1/2 for siege units)

### Approximate Destination Modes

For siege units that cannot reach exact destination, use approximation:

- `MOVEFLAG_APPROX_TARGET_RING1` — stop 1 tile away from target
- `MOVEFLAG_APPROX_TARGET_RING2` — stop 1-2 tiles away
- `MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN` — don't embark on approximate target tile

**Implementation:** [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp:1702+) `DestinationReached()`

### Performance Optimizations

#### Node Caching
- **Cache Data:** `CvPathNodeCacheData` stores computed values (terrain cost, enemy presence, etc.)
- **Generation ID:** Caches are versioned per pathfind operation to avoid stale data
- **Benefit:** Millions of cache lookups per AI turn are ~100x faster than recomputation

#### Extra Children (Wormholes)
- Harbors and trade posts can act as wormholes, creating long-range connections
- **Function:** `udGetExtraChildrenFunc` callback
- **Benefit:** Reduces path length for units using trade routes or harbors

#### Stop Nodes
- Pathfinder can voluntarily stop on a plot (e.g., when danger appears)
- **Function:** `AddStopNodeIfRequired()`
- **Flag:** `MOVEFLAG_NO_STOPNODES` disables stop node insertion

#### Turn-Slice Limits
- Pathfinding is time-sliced: each operation has a budget of ~50ms
- **Mechanism:** Checked via `GC.getGame().getTurnSlice()`
- **Fallback:** If time limit exceeded, use best path found so far

### Issues & Improvements

#### Heuristic Tightness
- **Issue:** Conservative heuristic (assumes 4 tiles/turn) may underestimate for slow units, leading to wider search.
- **Improvement Suggestion:** Compute per-unit heuristic based on `baseMoves()`.
- **Trade-off:** More computation per node vs. tighter search space.

#### Danger Calculation Recursion
- **Issue:** Pathfinder calls `GetDanger()`, which may use pathfinding internally (circular dependency).
- **Status:** Mitigated by caching danger values per plot; pathfinder must be careful with recursion depth.
- **Location:** [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp:1811+) `PathEndTurnCost()`

#### Approximate Destination Correctness
- **Issue:** Ring2 approximation requires common neighbor check (`CommonNeighborIsPassable()`).
- **Status:** Implemented but not thoroughly tested for edge cases (islands, narrow passages).
- **Review Point:** Verify ring2 approximation works correctly for siege units near enemy cities.

#### Extra Children Overhead
- **Issue:** Computing extra children (harbors, trade routes) for every node can be expensive.
- **Optimization:** Only compute for specific node types or when extra connections are likely.

#### Infinite Turn Pathfinding
- **Issue:** No hard limit on path length; only soft limit via `iMaxTurns`.
- **Risk:** Pathfinding could theoretically run forever on impossible destinations.
- **Mitigation:** `iMaxTurns` defaults to a large value; caller must set appropriately.

---

## 5. Movement Flags

### Flag System

Movement flags control pathfinding behavior via a bitmask. Defined in [CvUnit.h](../../CvGameCoreDLL_Expansion2/CvUnit.h:190-216).

### Complete Flag Reference

| Flag | Value | Purpose |
|------|-------|---------|
| `MOVEFLAG_ATTACK` | 0x1 | Allow melee combat or civilian capture |
| `MOVEFLAG_DESTINATION` | 0x4 | Unit will stop at this plot |
| `MOVEFLAG_IGNORE_STACKING_SELF` | 0x10 | Ignore stacking limits with owned units |
| `MOVEFLAG_SAFE_EMBARK_ONLY` | 0x80 | Only embark if danger = 0 on turn-end plots |
| `MOVEFLAG_IGNORE_DANGER` | 0x100 | Ignore danger penalties in pathfinding |
| `MOVEFLAG_NO_EMBARK` | 0x200 | Don't embark (but can move if already embarked) |
| `MOVEFLAG_NO_ENEMY_TERRITORY` | 0x400 | Don't enter enemy territory (but can pass through ZOC) |
| `MOVEFLAG_MAXIMIZE_EXPLORE` | 0x800 | Prioritize revealing new tiles |
| `MOVEFLAG_NO_DEFENSIVE_SUPPORT` | 0x1000 | Melee attacker won't receive ranged support |
| `MOVEFLAG_NO_OCEAN` | 0x2000 | Don't use deep water (coastal only) |
| `MOVEFLAG_DONT_STACK_WITH_NEUTRAL` | 0x4000 | Don't stack with neutral combat units (escorted civilians) |
| `MOVEFLAG_APPROX_TARGET_RING1` | 0x8000 | Stop 1 tile from target (good enough) |
| `MOVEFLAG_APPROX_TARGET_RING2` | 0x10000 | Stop 1-2 tiles from target |
| `MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN` | 0x20000 | No embarkation on ring target |
| `MOVEFLAG_IGNORE_ZOC` | 0x40000 | Ignore zones of control |
| `MOVEFLAG_IGNORE_RIGHT_OF_PASSAGE` | 0x80000 | Enter any territory (ignore open borders) |
| `MOVEFLAG_SELECTIVE_ZOC` | 0x100000 | Ignore ZOC from specific units (in `plotsToIgnoreForZOC`) |
| `MOVEFLAG_PRETEND_ALL_REVEALED` | 0x200000 | Assume all terrain is known (leaks info; AI only) |
| `MOVEFLAG_AI_ABORT_IN_DANGER` | 0x400000 | Abort if moving to dangerous plot (for AI) |
| `MOVEFLAG_NO_STOPNODES` | 0x800000 | Don't create voluntary stop nodes |
| `MOVEFLAG_ABORT_IF_NEW_ENEMY_REVEALED` | 0x1000000 | Abort if new enemies appear |

### Flag Usage Patterns

**Human Unit Movement:**
```cpp
int iFlags = MOVEFLAG_ATTACK | MOVEFLAG_DESTINATION;
// Human player moves: can attack, will stop at destination
```

**AI Movement (Exploration):**
```cpp
int iFlags = MOVEFLAG_MAXIMIZE_EXPLORE | MOVEFLAG_IGNORE_DANGER;
// Explorer: reveal new tiles, ignore danger
```

**Civilian with Escort:**
```cpp
int iFlags = MOVEFLAG_DONT_STACK_WITH_NEUTRAL;
// Civilian won't path through plots with neutral units
```

**Siege Unit (Ranged):**
```cpp
int iFlags = MOVEFLAG_APPROX_TARGET_RING1 | MOVEFLAG_NO_DEFENSIVE_SUPPORT;
// Ranged unit: stop 1 tile away from target, no support
```

### Custom Mods (MOD_* Conditionals)

Key mod flags affecting movement:

| Mod Flag | Feature |
|----------|---------|
| `MOD_BALANCE_SANE_UNIT_MOVEMENT_COST` | Additive terrain cost modifiers |
| `MOD_BALANCE_VP` or `MOD_BALANCE_CORE` | Vanilla+ balance changes |
| `MOD_PROMOTIONS_DEEP_WATER_EMBARKATION` | Deep-water embarking |
| `MOD_LINKED_MOVEMENT` | Linked units move together |
| `MOD_TRAITS_YIELD_FROM_ROUTE_MOVEMENT_IN_FOREIGN_TERRITORY` | Route yield bonuses |
| `MOD_UI_DISPLAY_PRECISE_MOVEMENT_POINTS` | UI shows exact MP (e.g., 3.5/6) |

---

## 6. Known Issues & Improvement Opportunities

### High Priority

#### Issue 1: Pathfinder Performance on Large Maps
**Description:** Pathfinding can become a bottleneck on huge Earth maps (200+ cities).
**Cause:** Each unit pathfind touches thousands of nodes; multiple pathfinds per turn.
**Status:** Partially mitigated by node caching and turn-slice limits.
**Recommendation:** 
- Profile `PathCost()` to identify hotspots.
- Consider hierarchical pathfinding (macro paths between cities, micro paths around obstacles).
- Limit pathfinding frequency for non-critical movement (trade routes, civilian explore).

#### Issue 2: ZOC and Selective ZOC Correctness
**Description:** Selective ZOC (`MOVEFLAG_SELECTIVE_ZOC`) requires pre-population of ignore list.
**Cause:** No automatic detection of which units to ignore; caller must handle manually.
**Status:** Functional but error-prone.
**Recommendation:**
- Add validation that ignore list is consistent across all calls.
- Consider caching "safe" units per faction pair to reduce computation.
- Add debug logging to catch misuse.

#### Issue 3: Embark/Disembark Cost Hierarchy Inconsistency
**Description:** Three-tier cost system (full, cheap, free) is scattered across code.
**Cause:** Costs are computed in multiple places (`GetCostsForMove()`, `PathCost()`, embark check functions).
**Status:** Works but hard to maintain.
**Recommendation:**
- Create a single function: `ComputeEmbarkDisbarkCost(pUnit, pFromPlot, pToPlot)` that returns cost and state.
- Document the three tiers with examples.
- Add unit tests for each tier.

#### Issue 4: Danger Calculation Recursion
**Description:** Danger calculation inside pathfinder can trigger additional pathfinds.
**Cause:** `GetDanger()` uses A* to compute unit reachability.
**Status:** Mitigated by caching but still a risk.
**Recommendation:**
- Add recursion guard: fail fast if pathfinder is called recursively.
- Log warnings when recursion depth exceeds 1.
- Consider separate danger calculator that doesn't use pathfinding.

### Medium Priority

#### Issue 5: Heuristic Tightness for Slow Units
**Description:** Conservative heuristic (4 tiles/turn) underestimates slow units, widening search.
**Cause:** Heuristic is not unit-specific.
**Impact:** Slower pathfinding for slowUnits (embarked units, workers).
**Recommendation:**
- Compute unit-specific heuristic using `baseMoves()`.
- Ensure admissibility is maintained (never overestimate).

#### Issue 6: Approximate Destination Edge Cases
**Description:** Ring2 approximation may fail on islands or narrow passages.
**Cause:** `CommonNeighborIsPassable()` assumes open terrain.
**Status:** Not thoroughly tested.
**Recommendation:**
- Add test cases for siege units near enemy cities on islands.
- Document assumptions in `DestinationReached()`.

#### Issue 7: Extra Children Performance
**Description:** Computing harbors/trade posts as extra children for every node is expensive.
**Cause:** No optimization; all extra children computed unconditionally.
**Recommendation:**
- Cache extra children per area (update only when new harbors built).
- Only compute for plot types that can have extra children (harbors, trade posts).

#### Issue 8: Movement Cost Promotion Modifiers
**Description:** Distinction between additive and multiplicative modifiers is unclear.
**Cause:** Two separate code paths exist (legacy and `MOD_BALANCE_SANE_UNIT_MOVEMENT_COST`).
**Recommendation:**
- Deprecate multiplicative path if additive is superior.
- Document when to use each approach.
- Add balance testing for unit identity (e.g., should a pathfinder trait really halve all movement?).

### Low Priority

#### Issue 9: Documentation
**Description:** No single reference document for movement/pathfinding system.
**Status:** Partially addressed by this document.
**Recommendation:** Add inline code comments pointing to this document.

#### Issue 10: Edge Case: Embarked Units in Deep Water
**Description:** Behavior of embarked land units in deep ocean vs. shallow water is not clearly specified.
**Cause:** Deep-water embarkation logic is conditional (`MOD_PROMOTIONS_DEEP_WATER_EMBARKATION`).
**Recommendation:** Document interaction with cargo ships and deep-water embarkation.

---

## 7. Testing & Validation

### Test Checklist

- [ ] **Basic Movement:** Unit moves 1 tile on road (cost = 1/6 MP), open terrain (cost = 1 MP)
- [ ] **ZOC Blocking:** Unit cannot cross enemy ZOC without ending turn
- [ ] **Selective ZOC:** Unit can cross ZOC from units in ignore list
- [ ] **Embark Free:** Trait-enabled embarkation costs 1 MP
- [ ] **Embark Full:** Normal embarkation ends turn
- [ ] **Disembark Free:** City-enabled disembarkation costs ~1/6 MP
- [ ] **Pathfinding:** Simple path (no obstacles) is found correctly
- [ ] **Pathfinding with Danger:** Unit avoids dangerous plots
- [ ] **Approximate Target Ring1:** Siege unit stops 1 tile from enemy city
- [ ] **Deep Water Embarkation:** Unit with promotion can embark in ocean

### Performance Baselines

- Single pathfind (100 tile distance): <10ms
- Full AI turn (all units): <1000ms
- Pathfinder accuracy: >99% (paths should match expected routes)

---

## 8. References & Related Code

### Core Files
- [CvUnit.h](../../CvGameCoreDLL_Expansion2/CvUnit.h) — movement flags, unit methods
- [CvUnitMovement.h/cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.h) — movement cost system
- [CvAStar.h/cpp](../../CvGameCoreDLL_Expansion2/CvAStar.h) — A* pathfinder
- [CvAStarNode.h](../../CvGameCoreDLL_Expansion2/CvAStarNode.h) — path node structure

### Helper Functions
- `CvUnit::baseMoves(bool bEmbarked)` — base movement per turn
- `CvPlot::needsEmbarkation(CvUnit*)` — check if embarkation required
- `CvPlot::isValidRoute(CvUnit*)` — check if route applies to unit
- `CvUnit::isIgnoreTerrainCostIn(TerrainTypes)` — promotion-based cost ignores
- `CvUnit::GetDanger(CvPlot*)` — compute plot danger (expensive!)

### Game Data
- `MOVE_DENOMINATOR` — movement unit scale (default: 60)
- `HILLS_EXTRA_MOVEMENT` — hills cost adder (default: 1)
- `PATH_BASE_COST` — pathfinder base cost unit (default: 1)
- Promotion XML: terrain cost modifiers, embark/disembark flat cost

---

## 9. Changelog & Recent Improvements

### VP Improvements
- Added `MOD_BALANCE_SANE_UNIT_MOVEMENT_COST` for additive terrain modifiers
- Implemented selective ZOC for multi-unit groups
- Enhanced danger calculation with threat assessment by unit type
- Added approximate target rings for siege units

### Community Patch Improvements
- Fixed `canLoad()` embarkation check (see [NAVAL_AIR_MECHANICS_FIXES.md](../naval-air/NAVAL_AIR_MECHANICS_FIXES.md))
- Added linked movement (`MOD_LINKED_MOVEMENT`) for grouped units
- Improved deep-water embarkation support
- Enhanced node cache data structure for performance

---

**Document Version:** 1.0  
**Last Updated:** January 2025  
**Maintenance:** Community Patch DLL developers
