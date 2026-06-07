# Terrain & Tile Effects Review

A comprehensive analysis of Civilization V's terrain and tile-based mechanics: movement costs, defensive modifiers, and resource handling. Covers C++ implementation, gameplay balance, and known issues.

---

## Table of Contents

1. [System Overview](#1-system-overview)
2. [Movement Costs](#2-movement-costs)
3. [Defensive Modifiers](#3-defensive-modifiers)
4. [Resources on Tiles](#4-resources-on-tiles)
5. [Integration Points](#5-integration-points)
6. [Known Issues & Improvements](#6-known-issues--improvements)
7. [Testing Checklist](#7-testing-checklist)

---

## 1. System Overview

### Purpose
Terrain and tile effects define the cost and strategic value of moving units through the map and defending positions. They consist of three independent but interrelated subsystems:

| Subsystem | Purpose | Key Files |
|-----------|---------|-----------|
| **Movement Costs** | Determines MP (movement points) required to enter a tile | `CvUnitMovement.cpp`, `CvUnitMovement.h`, `CvAStar.cpp` |
| **Defensive Modifiers** | Provides combat bonuses when defending on specific terrain/features | `CvUnit.cpp::terrainDefenseModifier()`, `CvUnit.cpp::featureDefenseModifier()` |
| **Resource Effects** | Yields, visibility, and special interactions tied to resources on tiles | `CvPlot.h`, `CvPlot.cpp`, `CvCity.h` |

### Data Flow

```
Game Start / Load
    ↓
Terrain/Feature/Resource Info loaded from SQL/XML
    ↓
CvPlot initialized with terrain type, feature, resource, improvements
    ↓
Unit pathfinding: GetCostsForMove() → movement cost calculation
    ↓
Combat: GetMaxDefenseStrength() → terrain/feature defense modifiers applied
    ↓
Yield calculation: calculateNatureYield() → resource yields processed
```

---

## 2. Movement Costs

### 2.1 Cost Hierarchy

Movement costs are calculated by `CvUnitMovement::GetCostsForMove()` and follow a **priority hierarchy**:

1. **Routes (Highest Priority)**
   - Roads, railroads, and feature-based "fake routes" (rivers with trait, forests with trait)
   - Cost: typically 1 MP per tile or variable (depends on route type)
   - Bypasses most terrain costs if both source and destination are on valid routes

2. **Embarkation/Disembarkation (Second Priority)**
   - Changing domain (land ↔ water)
   - Cost structure (tiered):
     - **Full cost**: unit ends turn (INT_MAX or Denominator)
     - **Cheap cost**: flat cost trait (`IsEmbarkedToLandFlatCost`) or city bonus
     - **Free cost**: some traits or tech-driven (`isCityNoEmbarkCost`)
   - Reference: [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:90-160)

3. **River Crossing**
   - Penalty when crossing rivers (unless unit ignores terrain costs or has amphibious promotion)
   - Typically adds 1 MP to base cost
   - Reference: [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:160-190)

4. **Terrain/Feature Base Cost (Fallback)**
   - Default movement cost for the destination terrain or feature
   - Applied if no route, embarkation, or river penalties apply
   - Example costs:
     - Grass: 1 MP
     - Forest/Jungle (if no feature cost): 1 MP
     - Hill: 1 MP
     - Mountain: Impassable (INT_MAX) unless special promotion
   - Reference: [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:200-330)

5. **Promotion Modifiers**
   - Multipliers and adders from unit promotions
   - Two types:
     - **Additive** (MOD_BALANCE_SANE_UNIT_MOVEMENT_COST):  `cost + adder` (transparent, stackable)
     - **Multiplicative** (vanilla): `cost * multiplier` (unintuitive, non-additive stacking)
   - Reference: [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:250-330)

### 2.2 Key Functions

#### `GetCostsForMove()`
**File:** [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:10)

```cpp
static int GetCostsForMove(
    const CvUnit* pUnit,
    const CvPlot* pFromPlot,
    const CvPlot* pToPlot,
    int iTerrainFeatureCostMultiplierFromPromotions = INT_MAX,
    int iTerrainFeatureCostAdderFromPromotions = INT_MAX
);
```

**Purpose:** Compute the movement cost for a unit to move from `pFromPlot` to `pToPlot`.

**Logic:**
1. Check if unit is embarking/disembarking, crossing rivers, or on routes
2. Apply embark cost hierarchy (full → cheap → free)
3. Apply route bonuses if both plots are on valid routes
4. Apply terrain/feature base costs if no route
5. Apply promotion modifiers (multiplicative or additive)

**Return:** Movement cost in MP (denominator-based: cost = returned value / MOVE_DENOMINATOR)

#### `MovementCost()` & Variants
**File:** [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:354)

```cpp
static int MovementCost(const CvUnit* pUnit, const CvPlot* pFromPlot, const CvPlot* pToPlot, 
                        int iMovesRemaining, int iMaxMoves, ...);
static int MovementCostSelectiveZOC(...);  // With ZOC exemption list
static int MovementCostNoZOC(...);         // No zone of control checks
```

**Purpose:** Wrapper around `GetCostsForMove()` that also checks zones of control (ZOC) and movement remaining.

### 2.3 Cost Examples

| Unit | Terrain | Feature | Route | Promo | Result | Notes |
|------|---------|---------|-------|-------|--------|-------|
| Scout | Grass | None | Road | None | 0.67 MP | Normal movement on road |
| Warrior | Plains | Forest | None | None | 1.0 MP | Forest replaces plains cost |
| Knight | Mountain | None | None | None | INT_MAX | Impassable unless promoted |
| Quadrireme | Ocean | None | None | None | 1.0 MP | Water units pay flat cost |
| Caravan | Plains | None | Road | Fleet Logistics | ~0.5 MP | Promotion applies discount |

### 2.4 Known Issues

**Issue MTC-001: Feature Cost Override Logic Unclear**
- When a feature is present, it typically replaces terrain cost (except hills/mountains which may stack).
- Code at [CvUnitMovement.cpp:276-290](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:276-290) checks:
  - "if feature, use feature cost; else terrain cost; if hill, add hill cost"
- **Problem:** This logic is not fully documented in comments, and edge cases (feature + hill + river) are complex.
- **Recommendation:** Add inline comments clarifying the cost stacking order.

**Issue MTC-002: Promotion Cost Modifier Interaction**
- Multiplicative promotions with base costs >1 can create unintuitive costs.
- Example: "50% cost" on a 2-MP mountain = 1 MP (clear), but "50% cost" on mixed terrain varies.
- **Status:** `MOD_BALANCE_SANE_UNIT_MOVEMENT_COST` addresses this by using additive costs.
- **Recommendation:** Ensure VP/Community Patch always uses additive costs; consider deprecating multiplicative path.

**Issue MTC-003: Embark Cost Hierarchy Not Fully Tested**
- Three-tier embark cost system (full/cheap/free) is scattered across code.
- City bonuses (`isCityLessEmbarkCost`, `isCityNoEmbarkCost`) are checked late in the flow.
- **Recommendation:** Unit test all combinations; add explicit test for "embark in city with flat-cost trait".

---

## 3. Defensive Modifiers

### 3.1 Defense Composition

When a unit defends on a tile, the final defense strength is:

```
Base Defense Strength (from unit type)
  + Flanking bonus (adjacent friendlies minus adjacent enemies)
  + Feature Defense Modifier
    OR
  + Terrain Defense Modifier (if no feature)
    + Hill Defense Modifier (if on hills)
  + Rough/Open Ground Modifiers
  + VP Terrain Modifier (additional bonus per promotion)
  + Building Defense (city walls, etc.)
  + River Crossing Penalty (if attacker crosses)
  + City/Garrison Bonuses
```

### 3.2 Key Functions

#### `featureDefenseModifier()`
**File:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:19574)

```cpp
int CvUnit::featureDefenseModifier(FeatureTypes eFeature) const
{
    return getExtraFeatureDefensePercent(eFeature);
}
```

**Purpose:** Return percentage defense bonus when defending on a tile with the given feature.

**Example Values:**
- Forest: +25%
- Jungle: +25%
- Hill: +15%
- Mountain: Impassable (usually)

#### `terrainDefenseModifier()`
**File:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:19555)

```cpp
int CvUnit::terrainDefenseModifier(TerrainTypes eTerrain) const
{
    return getExtraTerrainDefensePercent(eTerrain);
}
```

**Purpose:** Return percentage defense bonus when defending on a tile with the given base terrain (only used if no feature present).

**Example Values:**
- Grass: 0%
- Plains: 0%
- Desert: 0%
- Tundra: 0%
- Snow: 0%
- Hills (as terrain): +15%

#### `GetTerrainModifierDefense()` (VP Addition)
**File:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp) (promotion-based)

```cpp
int CvUnit::GetTerrainModifierDefense(TerrainTypes eTerrain) const;
```

**Purpose:** Additional defense bonus from unit promotions that grant terrain-specific bonuses (e.g., "Pathfinder: +15% vs. Plains").

**Note:** This is a **promotion-level** modifier, separate from base terrain defense.

### 3.3 Defense Calculation in Combat

**File:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:16580-16610)

Combat resolution calls `GetMaxDefenseStrength()`, which applies:

1. **Feature Defense** (if feature present)
   ```cpp
   if (pInPlot->getFeatureType() != NO_FEATURE)
       iModifier += featureDefenseModifier(pInPlot->getFeatureType());
   ```

2. **Terrain Defense** (if no feature)
   ```cpp
   else {
       iModifier += terrainDefenseModifier(pInPlot->getTerrainType());
       if (pInPlot->isHills())
           iModifier += terrainDefenseModifier(TERRAIN_HILL);
   }
   ```

3. **VP Terrain Modifier** (from promotions)
   ```cpp
   iModifier += GetTerrainModifierDefense(pInPlot->getTerrainType());
   if (pInPlot->isHills())
       iModifier += GetTerrainModifierDefense(TERRAIN_HILL);
   ```

4. **Rough/Open Ground**
   ```cpp
   if (pInPlot->isRoughGround())
       iModifier += roughDefenseModifier();
   else
       iModifier += openDefenseModifier();
   ```

### 3.4 Known Issues

**Issue TDF-001: Feature Defense Stacking with Hills**
- Current logic: If feature present, use feature defense; **ignore** terrain and hill defense.
- **Problem:** A forest on a hill gives only +25% (forest), not +25% + 15% (forest + hill).
- **Impact:** Hills under forests are less valuable defensively than expected.
- **Recommendation:**
  - Option A: Change to `iModifier += featureDefenseModifier() + (isHills() ? hillModifier() : 0)`
  - Option B: Add explicit feature+hill combo rules in XML (e.g., "Hills_Forest" grants +40%)
  - Option C: Document current behavior and accept as intentional

**Issue TDF-002: Promotion Terrain Modifiers May Double-Count**
- `GetTerrainModifierDefense()` is called **after** base `terrainDefenseModifier()`.
- If both apply, a unit could get e.g., +15% terrain + 15% promotion = +30%.
- **Question:** Is this intended stacking or a bug?
- **Recommendation:** Review code path [CvUnit.cpp:16588-16603](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:16588-16603) and document the interaction.

**Issue TDF-003: Open Ground vs. Rough Ground Modifiers**
- Units get a modifier if on open **or** rough ground, but these are not tied to specific terrain.
- `openDefenseModifier()` and `roughDefenseModifier()` appear to be global, unit-trait bonuses.
- **Problem:** If a unit trait grants "open ground defense", it overrides all terrain-specific modifiers.
- **Recommendation:** Clarify in comments whether these are additive or replace feature/terrain modifiers.

---

## 4. Resources on Tiles

### 4.1 Resource Mechanics

Resources are placed on tiles and provide:
1. **Yields** — production, gold, food, science, culture, faith, tourism
2. **Strategic Value** — required for unit production (iron for swordsmen, etc.)
3. **Trade Routes** — can be traded between civilizations for gold/strategic resources
4. **Visibility** — some resources require tech to be revealed
5. **Morale/Happiness** — some provide amenities or happiness modifiers

### 4.2 Resource Types

| Type | Purpose | Example | Revealed By |
|------|---------|---------|-------------|
| **Bonus** | Yield boost to tile | Wheat, Deer | Scouting / Start |
| **Strategic** | Required for units | Iron, Horse, Coal | Specific techs |
| **Luxury** | Happiness & trade | Silk, Spices, Gold | Specific techs |

### 4.3 Resource Data Structures

#### CvPlot Resource Methods
**File:** [CvPlot.h](../../CvGameCoreDLL_Expansion2/CvPlot.h:680-710)

```cpp
// Getters
ResourceTypes getResourceType(TeamTypes eTeam = NO_TEAM, bool bIgnoreTechPrereq = false) const;
ResourceTypes getNonObsoleteResourceType(TeamTypes eTeam = NO_TEAM) const;
int getNumResource() const;
int getNumResourceForPlayer(PlayerTypes ePlayer, bool bExtraResources, bool bIgnoreTechPrereq = false) const;

// Setters
void setResourceType(ResourceTypes eNewValue, int iResourceNum, bool bForMinorCivPlot = false);
void setNumResource(int iNum);
void changeNumResource(int iChange);
```

**Key Parameters:**
- `eTeam`: Used to check if team has tech to reveal resource
- `bIgnoreTechPrereq`: Force return resource even if tech not yet researched
- `iResourceNum`: Quantity of resource on tile (1, 2, 3, etc.)

#### Resource Yield Calculation
**File:** [CvPlot.cpp](../../CvGameCoreDLL_Expansion2/CvPlot.cpp) (various yield functions)

```cpp
int CvPlot::calculateNatureYield(YieldTypes eYield, PlayerTypes ePlayer, 
                                 FeatureTypes eFeature, ResourceTypes eResource, 
                                 ImprovementTypes eImprovement, const CvCity* pOwningCity, 
                                 bool bDisplay = false) const;
```

**Logic:**
1. Start with base terrain yield
2. Apply feature yield modifiers (e.g., forest gives less production before improvement)
3. **Apply resource yield bonus** (e.g., wheat on grassland grants +1 food)
4. Apply improvement bonus (e.g., farm improves wheat further)
5. Apply player/civ modifiers (traits, policies, beliefs)

**Example:**
```
Grassland: 2 food
+ Wheat resource: +1 food (if improved) or +0.5 food (if unimproved, varies by mod)
+ Farm improvement: +1 food
+ Granary building: +0 (yields already counted)
= 3.5 - 4 food per turn (depending on improvements and modifiers)
```

### 4.4 Resource Visibility & Tech Requirements

**File:** [CvPlot.cpp](../../CvGameCoreDLL_Expansion2/CvPlot.cpp) (getResourceType logic)

Resource visibility is determined by **tech prerequisites**:
- **Bonus resources** (wheat, deer, fish): Visible at game start or after scouting
- **Strategic resources** (iron, coal, uranium): Visible only after researching a prerequisite tech
  - Iron: revealed by Bronze Working
  - Coal: revealed by Industrialization
  - Uranium: revealed by Atomic Theory
- **Luxury resources** (silk, spices): Similar to strategic resources

**Code Path:**
```cpp
ResourceTypes CvPlot::getResourceType(TeamTypes eTeam, bool bIgnoreTechPrereq) const
{
    if (bIgnoreTechPrereq)
        return m_eResourceType;  // Return unconditionally
    
    // Check if team has tech to see this resource
    if (!GET_TEAM(eTeam).hasFoundResource(m_eResourceType))
        return NO_RESOURCE;  // Hidden until tech researched
    
    return m_eResourceType;
}
```

### 4.5 Resource and Tile Interaction Example

**Scenario:** Wheat on grassland with farm improvement, in Egypt (Nile floodplain civilization)

```
Base terrain (grassland):        Food: 2
+ Wheat resource bonus:           Food: +1
+ Farm improvement:               Food: +1
+ Nile trait (riverland bonus):   Food: +0.5
+ Granary (city building):        (affects city-wide food)
+ Bread-making belief:            Food: +0.5 (if applicable)
─────────────────────────────────────────
Effective yield:                  Food: ~5
```

### 4.6 Known Issues

**Issue RTL-001: Resource Obsolescence Logic**
- `getNonObsoleteResourceType()` returns a resource only if it's still valuable (not rendered obsolete by newer techs).
- **Problem:** Logic for determining "obsolete" is scattered; no central function.
- **Example:** Coal becomes obsolete when Oil is researched (in some mods).
- **Recommendation:** Add comprehensive obsolescence matrix in XML; centralize in a function.

**Issue RTL-002: Resource Amount Granularity**
- Resource quantity is an integer (1, 2, 3...), but yields may be fractional.
- **Problem:** A +0.5 bonus per resource unit doesn't scale clearly if quantity changes.
- **Recommendation:** Document yield-per-resource formula in XML; consider using fractional resource amounts.

**Issue RTL-003: Missing Resource Modifier Sources**
- Yields from resources are modified by buildings, beliefs, policies, and traits.
- **Problem:** There's no single place to see all modifiers; they're scattered across:
  - [CvCity.h](../../CvGameCoreDLL_Expansion2/CvCity.h:203-206) (event-driven modifiers)
  - [CvBeliefClasses.h](../../CvGameCoreDLL_Expansion2/CvBeliefClasses.h:473-480) (religion modifiers)
  - [CvPlot.cpp](../../CvGameCoreDLL_Expansion2/CvPlot.cpp) (yields)
- **Recommendation:** Create a resource modifier aggregation function that lists all applicable modifiers.

**Issue RTL-004: Hidden Resources in War**
- When at war with the resource owner, the resource tile may be blocked or inaccessible.
- **Problem:** No clear logic for "contested" resources; ownership and visibility are separate.
- **Recommendation:** Clarify whether at-war status prevents resource use/visibility in code and docs.

---

## 5. Integration Points

### 5.1 Movement & Pathfinding

**Connection:** `CvUnitMovement::GetCostsForMove()` is called by the A* pathfinder to determine tile costs.

**File:** [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp:1002-1010)

```cpp
// In CvAStar pathfinder setup:
int iMultiplier = CvUnitMovement::GetMovementCostMultiplierFromPromotions(pUnit, pToPlot);
int iAdder = CvUnitMovement::GetMovementCostAdderFromPromotions(pUnit, pToPlot);
int iCost = CvUnitMovement::GetCostsForMove(pUnit, pFromPlot, pToPlot, iMultiplier, iAdder);
// Cost is stored in A* node and used for heuristic/path selection
```

**Implication:** Changes to terrain costs directly affect which paths units take. Balance one carefully.

### 5.2 Combat & Defense

**Connection:** `CvUnit::GetMaxDefenseStrength()` uses terrain/feature modifiers.

**File:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:16300-16610)

Combat odds depend on:
1. Attacker's attack strength (unit type + promotions + bonuses)
2. **Defender's defense strength (unit type + terrain defense + feature defense + other modifiers)**
3. Damage calculation based on odds

Terrain defense affects battle outcome directly:
- Defender with +25% forest defense loses ~1-2 less HP per round than defender in open field.

**Implication:** Terrain defense is a key strategic tool for defending borders; balance carefully to avoid making certain positions impassable.

### 5.3 Yield & Economics

**Connection:** Tile yields (from resources, terrain, improvements) feed into city production and trade.

**File:** [CvCity.cpp](../../CvGameCoreDLL_Expansion2/CvCity.cpp) (various yield/production calculations)

**Example Path:**
```
Unit idle on a resource tile
  → CvPlot::calculateNatureYield() → resource bonus applied
  → CvCity::GetBaseYieldRateTimes100() → city production/food updated
  → UI displays updated yields
```

**Implication:** Resource placement affects city development. A resource with +2 food is much more valuable than +0.5.

---

## 6. Known Issues & Improvements

### Issue Priority Framework

- **HIGH:** Gameplay breaking, unfair, or causes crashes
- **MEDIUM:** Unclear behavior, balance concern, or minor bug
- **LOW:** Documentation, code clarity, or minor edge case

---

### TTE-001: Feature Cost Override Inconsistency
**Status:** ⚠️ PARTIAL  
**Priority:** MEDIUM  
**Category:** Movement Cost

**Problem:**
When a feature is on a tile, the feature's movement cost overrides the base terrain cost (except for hills/mountains, which may add). This creates unintuitive cases:
- Forest on grassland: 1 MP (forest cost)
- Jungle on grassland: 2 MP (jungle cost, usually higher)
- Forest on hill: Forest cost + potential hill cost (unclear if stacking)

**Current Code:**
[CvUnitMovement.cpp:276-290](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:276-290)
```cpp
if (eToFeature != NO_FEATURE) {
    iRegularCost = pToFeatureInfo ? pToFeatureInfo->getMovementCost() : 0;
} else {
    iRegularCost = pToTerrainInfo ? pToTerrainInfo->getMovementCost() : 0;
    if (pToPlot->isHills()) {
        // add hill cost...
    }
}
```

**Recommendation:**
- [ ] Document exact stacking rules (feature overrides terrain, but hills may add)
- [ ] Add code comments clarifying the priority order
- [ ] Consider making feature cost rules configurable via XML (e.g., "allow_hill_stacking" or cost_override_rule")
- [ ] Test edge case: feature on hill + river crossing

---

### TTE-002: Promotion-Based Terrain Modifiers May Double-Stack in Defense
**Status:** 📋 OPEN  
**Priority:** MEDIUM  
**Category:** Defensive Modifiers

**Problem:**
In `GetMaxDefenseStrength()`, both base `terrainDefenseModifier()` and VP's `GetTerrainModifierDefense()` are applied:
```cpp
iModifier += terrainDefenseModifier(eInTerrain);        // Base terrain defense
iModifier += GetTerrainModifierDefense(eInTerrain);     // Promotion-based defense
```

Is this intentional stacking (unit can get +15% from terrain + 15% from promotion = +30%) or a bug?

**Current Code:**
[CvUnit.cpp:16588-16603](../../CvGameCoreDLL_Expansion2/CvUnit.cpp:16588-16603)

**Recommendation:**
- [ ] Verify intent: is double-stacking desired?
  - If YES: add comment explaining why both apply
  - If NO: remove one and consolidate
- [ ] Test case: unit with promotion "Terrain Defense" on rough terrain
- [ ] Update EnemyUnitPanel.lua to display both modifiers clearly if both apply

---

### TTE-003: Feature + Hill Defense Interaction Unclear
**Status:** ⚠️ PARTIAL  
**Priority:** MEDIUM  
**Category:** Defensive Modifiers

**Problem:**
When a unit is on a tile with both a feature and hills:
- Current code uses **feature defense only**, ignoring hills.
- Example: Forest on hill = +25% (forest), not +25% + 15% = +40%

**Impact:**
- Forests on hills are defensively identical to forests on flat land.
- Hills lose strategic value if heavily forested.

**Recommendation:**
- [ ] Option A: Allow hill + feature stacking:
  ```cpp
  if (eFeature != NO_FEATURE) {
      iModifier += featureDefenseModifier(eFeature);
      if (isHills) iModifier += terrainDefenseModifier(TERRAIN_HILL);
  }
  ```
- [ ] Option B: Create explicit feature+terrain combos in XML (e.g., "Hills_Forest" entry)
- [ ] Option C: Document current behavior as intentional and update UI tooltips

---

### TTE-004: Resource Visibility Tech Logic Not Centralized
**Status:** 📋 OPEN  
**Priority:** LOW  
**Category:** Resources on Tiles

**Problem:**
Resource visibility is determined by multiple functions scattered across code:
- `getResourceType()` checks team tech
- `getNonObsoleteResourceType()` checks obsolescence
- Various belief/building functions check resource availability

There's no single "canonical" resource visibility check. This makes it hard to:
- Add new visibility rules (e.g., espionage reveals resources)
- Ensure consistency across all code paths
- Balance resource discovery

**Recommendation:**
- [ ] Create a centralized function `IsResourceVisibleToPlayer()` that checks:
  - Team has prerequisite tech
  - Resource is not obsolete
  - Player is not at war with current owner (if applicable)
  - Espionage visibility state
- [ ] Update all code paths to use this function
- [ ] Add XML-based visibility rules for mods

---

### TTE-005: Resource Yield Aggregation Lacks Documentation
**Status:** 📋 OPEN  
**Priority:** MEDIUM  
**Category:** Resources on Tiles

**Problem:**
Resource yields are modified by:
1. Base resource yield (defined in XML)
2. Improvement bonus (farm for wheat, etc.)
3. Building modifier (e.g., Granary +15% food)
4. Belief modifier (e.g., "Orchards +1 production")
5. Policy modifier (e.g., +10% production on bonus resources)
6. Trait modifier (e.g., Egypt river bonus)
7. Cooperative bonus (if near different resource type)

There's no single place to see all applicable modifiers for a given resource. Code is scattered across:
- [CvPlot.cpp](../../CvGameCoreDLL_Expansion2/CvPlot.cpp) (base yields, improvements)
- [CvCity.cpp](../../CvGameCoreDLL_Expansion2/CvCity.cpp) (buildings, events)
- [CvBeliefClasses.cpp](../../CvGameCoreDLL_Expansion2/CvBeliefClasses.cpp) (religion)
- [CvTraits.cpp](../../CvGameCoreDLL_Expansion2/CvTraits.cpp) (civ traits)

**Recommendation:**
- [ ] Create a `ResourceYieldModifier` struct/function that aggregates all modifiers
  ```cpp
  struct ResourceYieldModifier {
      int baseYield;
      int improvementBonus;
      int buildingBonus;
      int beliefBonus;
      int policyBonus;
      int traitBonus;
      int otherBonus;
      int total() const { return baseYield + ... + otherBonus; }
  };
  ResourceYieldModifier GetResourceYieldModifiers(ResourceTypes eResource, 
                                                   const CvPlot* pPlot,
                                                   const CvCity* pCity);
  ```
- [ ] Use this function consistently in UI (resource tooltips, city yields) and AI (city site evaluation)
- [ ] Document in XML what each modifier does

---

### TTE-006: Embark Cost Hierarchy Lacks Coverage Testing
**Status:** ⚠️ PARTIAL  
**Priority:** MEDIUM  
**Category:** Movement Costs

**Problem:**
The three-tier embark cost system (full/cheap/free) is complex and scattered:
1. Full cost: `INT_MAX` (ends turn)
2. Cheap cost: city bonus + flat-cost trait
3. Free cost: special tech + trait

Multiple conditions are checked in different places:
- [CvUnitMovement.cpp:90-160](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp:90-160)

No comprehensive unit test covering all combinations of:
- Trait states (flat cost, reduced cost, no cost)
- City bonuses (less embark, no embark)
- Tech states (early game vs. ocean-capable tech)
- Domain changes (land→water, water→land)

**Recommendation:**
- [ ] Add unit tests for all embark cost combinations:
  ```cpp
  TEST(Embark, FlatCostTrait_LandToWater) { ... }  // Should cost ~1 MP
  TEST(Embark, CityBonus_LandToWater) { ... }      // Should be cheap
  TEST(Embark, NoModifier_LandToWater) { ... }     // Should be full cost
  ```
- [ ] Document embark cost hierarchy in code comments
- [ ] Add embark cost to unit preview tooltip

---

### TTE-007: Rough Ground vs. Open Ground Modifier Unclear
**Status:** 📋 OPEN  
**Priority:** LOW  
**Category:** Defensive Modifiers

**Problem:**
Units have global rough and open ground defense modifiers:
```cpp
if (pInPlot->isRoughGround())
    iModifier += roughDefenseModifier();
else
    iModifier += openDefenseModifier();
```

These are **separate from** terrain and feature modifiers, but:
1. It's unclear how they interact with terrain defense
2. They appear to be unit-trait-based (not terrain-based)
3. No clear documentation of which terrains count as "rough" vs. "open"

**Recommendation:**
- [ ] Document which terrains are "rough" (hills, mountains?) vs. "open" (grassland, plains?)
- [ ] Add XML entry for "rough ground terrains" so mods can configure
- [ ] Clarify in code whether rough/open modifiers are **additive** or **replace** terrain modifiers
- [ ] Add UI tooltip showing whether tile is considered rough or open

---

### TTE-008: Tile Damage (Terrain Damage, Extra Feature Damage)
**Status:** ⚠️ PARTIAL  
**Priority:** MEDIUM  
**Category:** Terrain & Tile Effects (Not Fully Covered)

**Problem:**
Some terrains and features deal damage to units at end of turn:
- Volcano tile: 50 damage
- Fallout feature: 25 damage
- Marshland: 15 damage (in some mods)

Damage is calculated by `CvPlot::getTurnDamage()`, but:
1. Logic is complex and involves multiple flags (ignore terrain damage, extra feature damage)
2. No clear way to preview damage before moving to a tile
3. UI doesn't always show tile damage in tooltips

**Recommendation:**
- [ ] Add `GetTileHealthDamagePerTurn()` function with clear parameters
- [ ] Add tile damage to move preview ("This tile deals 50 damage per turn")
- [ ] Document in XML which terrains/features deal damage and how much
- [ ] Consider balance: is volcanic tile damage too punishing?

---

## 7. Testing Checklist

### Movement Costs

- [ ] **Basic Terrain Navigation**
  - [ ] Grassland costs 1 MP to enter
  - [ ] Forest costs 1 MP (or feature-specific cost) to enter
  - [ ] Hill costs 1 MP to enter
  - [ ] Mountain is impassable without promotion

- [ ] **Routes**
  - [ ] Road reduces cost to ~0.5 MP (from 1 MP)
  - [ ] Railroad reduces cost further (~0.33 MP)
  - [ ] River with river-movement trait acts as fake road

- [ ] **Embarkation**
  - [ ] Entering water from land with no embark trait ends turn
  - [ ] Flat-cost embark trait allows 1 MP embark
  - [ ] City bonus allows cheap embark (~0.5 MP)
  - [ ] Tech/trait combo allows free embark (~0.1 MP)

- [ ] **Promotion Modifiers**
  - [ ] "Flat cost" promotion provides consistent MP reduction across all terrains
  - [ ] Stacking two "flat cost" promotions shows additive reduction (with MOD_BALANCE_SANE_UNIT_MOVEMENT_COST)
  - [ ] Desert movement bonus (e.g., camel trait) reduces desert cost

- [ ] **Pathfinding**
  - [ ] Unit takes shortest path based on movement costs
  - [ ] Pathfinder prefers roads over rough terrain
  - [ ] Unit avoids high-cost routes unless necessary

### Defensive Modifiers

- [ ] **Base Terrain Defense**
  - [ ] Unit defending on plains gets 0% modifier
  - [ ] Unit defending on rough terrain gets modifier (e.g., +15%)
  - [ ] Unit defending on open ground gets different modifier

- [ ] **Feature Defense**
  - [ ] Unit defending in forest gets feature defense (e.g., +25%)
  - [ ] Unit defending in jungle gets feature defense
  - [ ] Feature defense overrides terrain defense (no stacking currently)

- [ ] **Hill Defense**
  - [ ] If feature present, hill modifier NOT applied (known issue TTE-003)
  - [ ] If no feature, hill modifier applied (e.g., +15%)

- [ ] **Promotion Terrain Defense**
  - [ ] Promotion "Terrain Defense" grants bonus vs. specific terrain
  - [ ] Stacking with base terrain defense (verify double-count behavior)

- [ ] **Combat Odds**
  - [ ] Defender in forest has ~5-10% higher survival chance vs. same defender in open field
  - [ ] Defender on rough terrain with hills has accumulated modifiers applied

### Resources on Tiles

- [ ] **Resource Visibility**
  - [ ] Bonus resource visible at game start
  - [ ] Strategic resource hidden until tech researched
  - [ ] Tech prerequisite correctly gates visibility

- [ ] **Resource Yields**
  - [ ] Unimproved wheat on grassland grants +1 food
  - [ ] Farm improvement on wheat grants additional +1 food
  - [ ] City building (e.g., granary) modifies resource yields correctly

- [ ] **Resource Discovery & Tech**
  - [ ] Researching Bronze Working reveals iron
  - [ ] Coal hidden until Industrialization
  - [ ] Oil hidden until Plastics tech

- [ ] **Obsolescence**
  - [ ] Coal becomes less valuable after Oil discovered
  - [ ] Old strategic resources still accessible (no hard obsolescence)

- [ ] **Resource Quantities**
  - [ ] Tile can have 1-3 resources of same type
  - [ ] Yield scales with quantity (correctly or fractionally)

### Edge Cases

- [ ] **Feature on Hill Movement**
  - [ ] Forest on hill costs same as forest on flat (feature overrides hill cost)
  - [ ] Hill defense NOT applied if feature present (known issue)

- [ ] **River + Road Interaction**
  - [ ] Unit on road crossing river pays road cost + river penalty (or none if amphibious)

- [ ] **Embarking in City with Flat-Cost Trait**
  - [ ] Cost should be ~1 MP (flat cost), not full or cheap tier

- [ ] **Resource in Multiple Domains**
  - [ ] Strategic resource on hill (land) still works for unit production
  - [ ] Strategic resource on island does not help production without iron working tech

---

## References

### Code Files

- **Movement:** [CvUnitMovement.h](../../CvGameCoreDLL_Expansion2/CvUnitMovement.h), [CvUnitMovement.cpp](../../CvGameCoreDLL_Expansion2/CvUnitMovement.cpp)
- **Combat & Defense:** [CvUnit.cpp](../../CvGameCoreDLL_Expansion2/CvUnit.cpp#L16300-L16610)
- **Tiles & Resources:** [CvPlot.h](../../CvGameCoreDLL_Expansion2/CvPlot.h), [CvPlot.cpp](../../CvGameCoreDLL_Expansion2/CvPlot.cpp)
- **Pathfinding:** [CvAStar.cpp](../../CvGameCoreDLL_Expansion2/CvAStar.cpp)
- **Yields & Economics:** [CvCity.cpp](../../CvGameCoreDLL_Expansion2/CvCity.cpp)

### Related Reviews

- [Unit Combat Review](unit-combat-review.md)
- [Promotions & Experience Review](promotions-experience-review.md)
- [Unit Movement & Pathfinding](../unit-movement/UNIT_MOVEMENT_PATHFINDING.md)

### Game Data

- **Terrain Info:** `GameInfo.Terrains` (XML/SQL definition of terrain properties)
- **Feature Info:** `GameInfo.Features` (feature movement costs, defensive bonuses)
- **Resource Info:** `GameInfo.Resources` (resource yields, visibility, strategic value)

---

## Summary

Terrain and tile effects are a core strategic layer in Civilization V:
1. **Movement Costs** control unit positioning and tempo
2. **Defensive Modifiers** make certain positions valuable and defensible
3. **Resources** drive economic and military strategy

The current implementation is generally sound, but several areas lack clarity (feature + hill interaction, promotion stacking, resource visibility centralization) and testing coverage. Recommended next steps:
1. Centralize resource visibility logic
2. Document terrain cost stacking rules
3. Add comprehensive test suite for embark costs and terrain combinations
4. Consider balance pass on feature defense interactions

