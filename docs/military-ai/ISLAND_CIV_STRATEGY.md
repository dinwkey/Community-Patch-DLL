# Island Civilization Strategy — Research & Implementation Plan

> **Status:** Research complete, implementation not started
> **Author:** AI-assisted analysis session, Feb 2026
> **Scope:** Major AI improvement — specialized strategic model for island and archipelago civilizations
> **Prerequisite:** Strategic Geography Map (land phases 1-6 + naval phases 1-4) — see `STRATEGIC_GEOGRAPHY_MAP_PLAN.md`
> **Related:** Appendix C of `STRATEGIC_GEOGRAPHY_MAP_PLAN.md` (Naval Strategic Geography)

---

## 1. Motivation

### Why Island Civs Need a Separate Strategic Model

The continental strategic model (defensive layers, chokepoints, floodgates, approach corridors) assumes a fundamentally land-based threat model: enemies approach over terrain, chokepoints are mountain passes, defensive depth is measured in land tiles, and the army is the primary fighting force. This model breaks down completely for island civilizations.

**Island civs (Japan, Britain, Polynesia, Indonesia, etc.) face a radically different strategic reality:**

| Continental Civ | Island Civ |
|----------------|-----------|
| Primary threat axis: land borders | Primary threat axis: coastline (360°) |
| Chokepoints: mountain passes, river crossings | Chokepoints: narrow straits, harbor approaches |
| Defensive depth in land tiles | Defensive depth in sea tiles (naval patrol range) |
| Army is the core military arm | Navy is the core military arm |
| Embarked units are a niche concern | Embarked transit is a constant vulnerability |
| Territory is contiguous | Territory may be fragmented across islands |
| Interior lines allow rapid redeployment | Redeployment requires ocean transit (slow, dangerous) |
| Trade routes mainly overland | Trade routes mainly over sea (longer range, higher vulnerability) |
| Workers improve land tiles locally | Workers embark only to reach other islands for land improvements; sea resources are improved by work boats |
| Settlers move to adjacent plots safely | Settlers must embark across open water (extreme vulnerability) |

**The current AI treats island and continental civs identically at the strategic level.** There is no `IsIslandCiv()` function. The only island-awareness is:
- Per-city detection: `iLandApproaches == 0` → city is on an island (tactical, not strategic)
- `ECONOMICAISTRATEGY_ISLAND_START`: temporary early-game flag (first 25 turns only, pre-embarkation)
- Tiny island garrison cap: landmass ≤ 3 tiles → don't build more land units
- `IsTestStrategy_MostlyOnTheCoast()`: coastal population ≥ inland population (economic, not military)

None of these create a persistent, strategic-level awareness that "I am an island civilization and my entire strategic posture should reflect that."

### Motivating Scenarios

**Scenario 1: AI Japan on Giant Earth TSL**
- All cities are coastal on relatively small islands
- Every threat arrives by sea (China, Korea, Polynesia, or cross-Pacific enemies)
- The AI should prioritize navy above all else, treat the Sea of Japan as a defensive moat, and position fleets to intercept amphibious invasions
- **Current behavior:** The AI builds a balanced land/sea force, garrisons land units in coastal cities, and doesn't recognize that its survival depends entirely on naval superiority

**Scenario 2: AI Britain on Giant Earth TSL**
- Core cities on the British Isles (small landmass)
- English Channel provides a natural defensive barrier (narrow strait = naval chokepoint)
- Should station fleet in the Channel, treat any cross-Channel embarkation as maximum threat
- **Current behavior:** AI treats London like any other coastal city, doesn't understand the Channel as a strategic barrier

**Scenario 3: AI Indonesia on Giant Earth TSL**
- Cities scattered across multiple islands (archipelago)
- No single contiguous territory — each island is effectively independent
- Inter-island logistics (moving units, settlers, trade) requires naval escort
- Strait of Malacca is a critical naval chokepoint
- **Current behavior:** Each city is defended independently, no convoy escort for inter-island movement, no recognition of Malacca's strategic value

**Scenario 4: AI Polynesia on any map**
- UA allows ocean embarkation + combat bonus while embarked
- Should be the most aggressive naval/expansion civ
- Unique tile improvement (Moai) requires coast
- **Current behavior:** Gets `ISLAND_START` for 25 turns, then reverts to standard continental model

---

## 2. What Exists Today (Codebase Audit)

### 2.1 Island Detection — Fragmented and Incomplete

| Feature | Location | What It Does | Limitation |
|---------|----------|-------------|------------|
| `bIslandCity` | `CvTacticalAI.cpp L1260` | `iLandApproaches == 0` per city | Per-city only; no player-level flag |
| `ISLAND_START` | `CvEconomicAI.cpp L4340` | First 25 turns if can't embark + small landmass | Expires before it matters; only affects tech priority |
| Tiny island garrison cap | `CvUnitProductionAI.cpp L351` | Don't build land units if landmass ≤ 3 tiles | Too narrow — a 20-tile island still gets land unit spam |
| `MostlyOnTheCoast` | `CvEconomicAI.cpp L4470` | Coastal pop ≥ inland pop | Economic only; doesn't influence military posture |
| `isCoastalCiv()` | `CvInfos.cpp L2278` | DB flag from `Civilization_Start_Along_Ocean` | Start bias only; no gameplay AI effect beyond city placement bonus |
| `HasSharedAreaWith()` | `CvCity.cpp L16474` | Do two cities share a land area? | Binary land/naval decision; no "mostly water" nuance |
| Landmass size thresholds | Various | 3, 23, 30-tile island thresholds | Ad-hoc, not unified into "island civ" concept |

**Key gap:** No function answers "Is this player an island civ?" persistently throughout the game.

### 2.2 Naval Defense — Reactive, Not Proactive

| Feature | Location | What It Does | Limitation |
|---------|----------|-------------|------------|
| `m_eNavalDefenseState` | `CvMilitaryAI.cpp L2670` | ENOUGH/NEUTRAL/NEEDED/CRITICAL based on ship count vs recommended | Only CRITICAL on siege or <50% ships; no "approaching fleet" escalation |
| Naval superiority ops | `CvMilitaryAI.cpp L3148` | Launches fleet defense at threatened coastal city | 1 operation at a time; doesn't position fleets preventively |
| Coastal threat detection | `CvTacticalAI.cpp L2436` | Enemy naval in RING2-RING5 triggers proactive garrison | Good per-city; no theater-level naval posture |
| Blockade response | `CvCityStrategyAI.cpp L2855` | `UNDER_BLOCKADE` strategy triggers defense builds | City-level only; no operational blockade-breaking |
| Emergency naval purchase | `CvTacticalAI.cpp L3046` | Buy naval unit when <2 friendly naval in zone | Reactive (needs crisis), not preventive |

**Key gap:** The AI never pre-positions fleets at defensive stations. It waits for the enemy to arrive.

### 2.3 Embarked Unit Vulnerability — Extreme

| Mechanic | Detail | Impact |
|----------|--------|--------|
| **Combat strength** | Base strength only — no promotions, terrain, fortify | A veteran infantry embarked = raw recruit |
| **Retaliation** | Zero self-damage to attacker (except air) | Enemy melee units attack with impunity |
| **Best defender** | Embarked never selected if non-embarked available | Can't protect stack by having strong unit embark |
| **Capture** | Embarked civilians killed, not captured | Settlers lost permanently on interception |
| **Escort** | Individual pairing only | No convoy for groups; high-value targets (settlers) get priority 5 |

**Key gap:** The AI doesn't understand that embarked transit is its greatest vulnerability as an island civ. It doesn't plan convoys or avoid exposing high-value units.

### 2.4 Naval Economy — Partially Handled

| Feature | Location | What It Does | Works for Islands? |
|---------|----------|-------------|-------------------|
| `NEED_NAVAL_GROWTH` | `CvCityStrategyAI.cpp L2502` | Triggers at ≥35% ocean tiles, boosts harbor/lighthouse | Yes, well-suited |
| `NEED_NAVAL_TILE_IMPROVEMENT` | `CvCityStrategyAI.cpp L2550` | Prioritizes workboat for unimproved sea resources | Yes |
| Sea trade range 2× land | `CvTradeClasses.cpp L5363` | Base 20 vs land base 10 | Benefit for island civs |
| `Policy_CoastalCityYieldChanges` | `CvPolicyClasses.cpp L801` | Policy/belief coastal city yields | AI values more when all-coastal |
| Sea food assumption | `CvSiteEvaluationClasses.cpp L974` | +1 food for shallow water in site eval (lighthouse) | Helps island city founding |
| Blockade gold penalty | `CvCity.cpp L22750` | -25% gold when sea-blockaded | Devastating for island civs |

**Key gap:** No understanding that sea trade is existential for island civs (not supplementary). Blockade should trigger maximum naval response for island civs.

### 2.5 Strategic Geography Map — Naval Phases Exist But Aren't Fully Wired

The `CvStrategicGeographyMap` already computes:
- **Coastal Exposure** (NONE/SHELTERED/MODERATE/EXPOSED) per city
- **Naval Chokepoints** (NEAR_STRAIT/CANAL_CITY) with passage width
- **Water Connectivity Graph** (which water bodies connect, via canal cities/forts)
- **Amphibious Threat Score** (0-100 composite per city)

These feed into `GetDefensePriorityModifier()` and `CvBuildingProductionAI`, but are NOT consumed by:
- `SetRecommendedArmyNavySize()` — doesn't consider coastal exposure
- `UpdateDefenseState()` — doesn't factor in amphibious threat score
- `UpdateWarType()` — doesn't know about water connectivity
- Settler AI — doesn't avoid exposed coastal positions
- Naval operation planning — doesn't target naval chokepoints

---

## 3. Proposed System: Island Civ Strategic Model

### 3.1 Design Principles

1. **Persistent identification:** Classify each player as CONTINENTAL, COASTAL, PENINSULAR, or ISLAND at game start (and reclassify on city gain/loss). This classification persists and pervasively influences all AI subsystems.

2. **Navy-first posture:** Island civs should allocate 60-80% of military production to naval units (vs. current formula which is flavor-driven and typically produces 30-40%).

3. **Defensive perimeter replaces defensive depth:** Instead of layered land defenses, island civs think in terms of a naval patrol zone — the perimeter of water tiles surrounding their islands that must be kept clear of enemy fleets.

4. **Risk-assessed escort doctrine:** Inter-island movement of high-value units (settlers, workers, civilian units) should be escorted when at war with a nearby civ or when barbarian naval threats exist. During peacetime with no nearby hostile naval presence, escort is unnecessary — but the AI should evaluate risk accordingly (check danger plots, nearby hostile units, barbarian camps near water). Early-game embarked movement is slow, making transit windows longer and riskier; later embarkation techs that grant faster water movement reduce this window.

5. **Strait control:** Naval chokepoints (already detected by the Strategic Geography Map) should be the primary defensive stations for island civ fleets — the naval equivalent of garrisoning mountain passes.

6. **Economic resilience:** Island civs should prioritize lighthouse/harbor infrastructure, maintain naval superiority to protect sea trade, and treat blockade as a crisis event.

### 3.2 Player Classification: `eGeographicPosture`

```cpp
// New enum in CvStrategicGeographyMap.h or CvMilitaryAI.h
enum GeographicPosture
{
    GEO_POSTURE_CONTINENTAL,  // All/most cities on large contiguous landmass (>100 tiles)
    GEO_POSTURE_COASTAL,      // Core on large landmass but significant coastal exposure
    GEO_POSTURE_PENINSULAR,   // Core on a peninsula with narrow land connection
    GEO_POSTURE_ISLAND,       // All/most cities on small landmass(es) (<50 tiles)
    GEO_POSTURE_ARCHIPELAGO,  // Cities dispersed across multiple disjoint small landmasses
};
```

#### Classification Algorithm

```
ComputeGeographicPosture():
  1. For each owned city, record its landmass ID and size
  2. Group cities by landmass
  3. Compute:
     - largestLandmass = max landmass size among all cities
     - numLandmasses = count of distinct landmasses with ≥1 city
     - citiesOnLargest = number of cities on the largest landmass
     - totalCities = numCities
     - coastalCityRatio = numCoastalCities / totalCities

  4. Classify:
     If largestLandmass > 100 AND citiesOnLargest / totalCities >= 0.8:
       If coastalCityRatio > 0.6 AND all border cities are coastal:
         → GEO_POSTURE_COASTAL
       Else:
         → GEO_POSTURE_CONTINENTAL

     If largestLandmass > 50 AND landConnectionWidth <= 3:
       → GEO_POSTURE_PENINSULAR    (detected via chokepoint on land)

     If numLandmasses >= 3 AND largestLandmass < 50:
       → GEO_POSTURE_ARCHIPELAGO

     If largestLandmass < 50:
       → GEO_POSTURE_ISLAND

     Default: GEO_POSTURE_CONTINENTAL
```

**Reclassification triggers:** City founded, city captured, city razed, significant border expansion. Cached with `m_iLastPostureUpdate` turn counter.

**Peninsula detection:** Requires the land chokepoint system from Phase 3 of the Strategic Geography Map. A peninsula is a landmass where a narrow chokepoint (width ≤ 3 passable land tiles) separates the player's core cities from the rest of the continent. Example: Korea, Italy, Iberian Peninsula on Giant Earth.

### 3.3 Core Data Structures

```cpp
// Extension to CvStrategicGeographyMap or new sub-object

struct IslandCivData
{
    // Classification
    GeographicPosture ePosture;

    // Island metrics
    int iNumLandmasses;                // Distinct landmasses with cities
    int iLargestLandmassSize;          // Tiles of the biggest island
    float fCoastalCityRatio;           // Fraction of cities that are coastal
    float fNavalForceRatio;            // Target naval / total military ratio

    // Defensive perimeter
    int iDefensivePerimeterRadius;     // How far out to patrol (tiles from coast)
    std::vector<CvPlot*> vPatrolStations; // Key water tiles for fleet positioning
    std::vector<CvPlot*> vStraitDefensePositions; // Naval chokepoint guard positions

    // Inter-island logistics
    struct IslandGroup
    {
        int iLandmassID;
        int iNumCities;
        int iPopulation;
        int iMilitaryStrength;         // Land units on this island
        bool bHasCapital;
        bool bConnectedByCanal;        // Connected to another island via canal city
        int iNearestOtherIslandDist;   // Water tiles to nearest friendly island
    };
    std::vector<IslandGroup> vIslandGroups;

    // Convoy tracking (transient, per-turn)
    struct PendingTransit
    {
        int iUnitID;
        int iSourceIslandLandmass;
        int iDestIslandLandmass;
        int iEstimatedTransitTurns;
        bool bNeedsEscort;            // true for settlers, workers, GPs
        int iAssignedEscortID;        // -1 if no escort yet
    };
    std::vector<PendingTransit> vPendingTransits;

    // Economy
    int iSeaTradeRouteCount;
    int iSeaTradeRouteValue;          // Total gold from sea trade
    float fTradeVulnerability;        // Fraction of trade routes through hostile water
    bool bBlockadeCrisis;             // Any city blockaded AND island civ → crisis
};
```

---

## 4. Subsystem Specifications

### 4.1 Coastal Defense Classification (Exposed vs. Sheltered Harbors)

**Goal:** Each coastal city should know whether its harbor faces open ocean or is sheltered within a bay/strait, and how this affects its defensive posture.

#### What already exists:

`AnalyzeCoastalExposure()` in `CvStrategicGeographyMap.cpp` already classifies cities:
- **EXPOSED** (5-6 water in RING1): peninsula tip, island city — vulnerable from all directions
- **MODERATE** (3-4 water): standard coastal — some flanking coverage from land
- **SHELTERED** (1-2 water): harbor tucked behind land — limited naval approach vectors

Plus additional data: deep water count in RING2, landing zone count (flat land adjacent to coast), ocean connectivity.

#### What needs enhancement for island civs:

1. **Harbor approach vector analysis.** For EXPOSED and MODERATE cities, scan outward along each water-adjacent direction (up to 6 tiles) to determine how many distinct approach vectors an enemy fleet could use. A city with water on 3 sides but mountains behind it has 3 approach vectors. A city between two landmasses in a strait has only 1-2. Fewer vectors = more defensible.

2. **Sheltered harbor scoring for island settlements.** When the settler AI evaluates coastal plots, island civ settlers should **strongly prefer** SHELTERED sites (1-2 water tiles adjacent, land on 4-5 sides) over EXPOSED sites (5-6 water). This is the naval equivalent of settling behind a mountain range.

   ```
   Island civ settler coastal score adjustment:
     SHELTERED: +30% site value  (defensible harbor)
     MODERATE:  +0%              (neutral)
     EXPOSED:   -20% site value  (hard to defend)

   Exception: if the EXPOSED site controls a naval chokepoint, +15% instead
   ```

3. **Harbor defense building priority.** Island civ coastal cities should prioritize defensive buildings earlier than continental civs:
   - FRONT_LINE + EXPOSED: walls at pop 3 (immediate)
   - FRONT_LINE + MODERATE: walls at pop 4
   - FRONT_LINE + SHELTERED: walls at pop 5 (standard timing)
   - All island cities: +50 `FLAVOR_CITY_DEFENSE` when island posture

4. **Ranged coverage from harbor.** Cities with high coastal exposure benefit more from ranged garrison units and walls (city ranged strike covers water approaches). For EXPOSED cities, the effective "zone of denial" from city ranged attack covers 2-tile radius into water — this is a significant defensive asset. The AI should recognize that an EXPOSED city with walls + ranged garrison is more defensible than an EXPOSED city without.

#### Integration points:

| Consumer | Current | Proposed |
|----------|---------|----------|
| `PlotFoundValue()` | +40% coastal bonus, +80% for naval civs | Add island posture modifier: SHELTERED +30%, EXPOSED -20% |
| `CheckBuildingBuildSanity()` | Generic defensive building priority | Earlier walls for EXPOSED island cities |
| `CvStrategicGeographyMap::GetDefensePriorityModifier()` | EXPOSED +30, MODERATE +15, SHELTERED +5 | Island posture: multiply by 1.5× for all coastal modifiers |
| `UpdateCityThreatCriteria()` | No coastal exposure factor | Add +20 for EXPOSED, +10 for MODERATE when island posture |

### 4.2 Naval Control Zones (Defensive Perimeter)

**Goal:** Island civs should maintain a "naval patrol zone" — a ring of water tiles around their islands that must be kept clear of enemy fleets. This replaces the "defensive depth in land tiles" concept used by continental civs.

#### Concept: Defensive Perimeter Radius

```
For an island civ:
  - DefensivePerimeterRadius = max(3, min(NavalUnitRange, 5))

  Where NavalUnitRange = movement / GD_INT_GET(MOVE_DENOMINATOR) + rangedRange

  This is the distance from the coastline that friendly naval units can
  effectively patrol and intercept threats within one turn.

  Peacetime: patrol at 2-3 tile radius (detection + interception)
  Wartime:   extend to 4-5 tile radius (early warning + engagement)
```

#### Patrol Station Placement

```
ComputePatrolStations():
  For each coastal city C:
    1. Find the direction of greatest water exposure (scan RING1-2,
       find the direction with deepest continuous water)
    2. Place a patrol station 2-3 tiles out from the city along that axis
    3. If city is adjacent to a naval chokepoint, place station ON the
       chokepoint tile instead (strait defense > perimeter patrol)
    4. Merge nearby patrol stations (within 2 tiles) to avoid redundancy

  Result: 1-2 patrol stations per FRONT_LINE coastal city, positioned
  to cover the primary threat axis with overlapping fields of fire
```

**Patrol station use:** The tactical AI should prefer moving idle naval units to patrol stations rather than clustering in harbors. This provides:
- **Early warning:** Enemy fleet detected 2-3 turns before reaching the city
- **Interception zone:** Ranged naval units at patrol stations can fire on approaching enemies
- **Deterrence:** Visible naval strength discourages enemy approach

#### Integration:

| Consumer | How |
|----------|-----|
| `PlotNavalEscortMoves()` | Idle naval units not escorting → move to nearest patrol station |
| `CvHomelandAI::PlotNavalSentryMoves()` | Use patrol stations as sentry destinations instead of random water tiles |
| `CvTacticalAI::PlotCoastalDefenseMoves()` | Naval units defending coast → position at patrol station, not ad hoc |
| `CvDangerPlots` | Patrol station provides "soft boundary" — enemy units crossing it increase alert level |

### 4.3 Amphibious Threat Assessment

**Goal:** Each island city should have a per-enemy amphibious threat score that drives defensive allocation. This extends the existing `AssessAmphibiousThreats()` in `CvStrategicGeographyMap.cpp`.

#### What already exists:

The Strategic Geography Map computes per-city amphibious threat (0-100) using:
- Coastal exposure base (EXPOSED=30, MODERATE=15, SHELTERED=5)
- Landing zone density (flat land adjacent to coast in RING2, ×3, capped at 15)
- Ocean connectivity (+5 if ocean-connected, +5 for deep water)
- Per-enemy naval power (base +10 per reachable enemy fleet, +5 for ranged naval, scaled by fleet size, capped at 45)
- Threshold: `bVulnerableToAmphibious = (score >= 40)`

#### What needs enhancement for island civs:

1. **Island posture amplifier.** For `GEO_POSTURE_ISLAND` and `GEO_POSTURE_ARCHIPELAGO`, multiply the amphibious threat score by 1.5×. An island city facing an amphibious threat has no land fallback — loss is total.

2. **Landing zone defense scoring.** Not just counting landing zones, but evaluating which ones are defensible:
   ```
   For each landing zone tile in RING2:
     - Hills landing: -3 threat (attacker needs to assault uphill)
     - Flat open landing: +0 (standard)
     - Behind river: -2 (river crossing penalty for attacker)
     - Adjacent to city: +5 (direct assault target)
     - Adjacent to fort/citadel: -5 (covered by improvement)
   ```

3. **Convoy detection.** When an enemy has multiple embarked units traveling together (within 2 tiles of each other, moving toward us), that's an invasion force, not individual crossings:
   ```
   If ≥3 enemy embarked units within 3 tiles of each other
   AND they're within 8 tiles of our coastline:
     → AMPHIBIOUS_INVASION_IMMINENT
     → Force naval defense state to CRITICAL
     → Launch NAVAL_SUPERIORITY operation targeting the convoy
   ```

4. **Post-landing threat.** Once enemy units have landed, the threat model shifts:
   ```
   For each friendly island:
     Count enemy units on same landmass
     If enemyOnIsland > 0:
       → ISLAND_INVADED state
       → Priority: destroy beachhead before reinforcements arrive
       → Stop evacuating — fight on the beaches
   ```

#### Integration:

| Consumer | How |
|----------|-----|
| `UpdateDefenseState()` | `AMPHIBIOUS_INVASION_IMMINENT` → force `DEFENSE_STATE_CRITICAL` for naval |
| `UpdateOperations()` | Detected invasion convoy → launch `NAVAL_SUPERIORITY` targeting convoy |
| `PrioritizeZones()` | Zone with landing zone adjacent to enemy embarked → 3× priority |
| `EvaluateTacticalRetreat()` | Embarked units retreat toward nearest friendly coastal city (where they can disembark under city/naval cover); see §4.3.1 |

#### 4.3.1 Retreat Behavior on Islands (Clarification)

Retreat logic depends on the unit's current state and the island's size:

- **Embarked units under attack:** Should retreat toward the nearest friendly coastal city where they can disembark and receive city ranged-strike cover and/or naval support. Moving further out to open sea is almost always worse — the unit is more vulnerable there.
- **Land units on an island:** Retreat follows normal land logic — move away from the enemy toward safer interior plots. If the island is large enough to have interior tiles, retreat inland. If the island is very small (essentially just the city and 1-2 rings of land), there IS no meaningful inland retreat; the unit should fall back into the city itself for garrison bonus and city defense stacking.
- **Land units near a coastal city on a large island:** Standard retreat rules apply — retreat inland toward reinforcements, not toward the coast where they might be forced to embark.
- **Key distinction:** "Retreat toward harbor" applies specifically to embarked units that need to get back to safety on land, NOT to land units that are already on solid ground.

### 4.4 Island Economy Priorities

**Goal:** Island civs should understand that their economy depends on the sea — sea trade, fishing, and harbor infrastructure are not optional but existential.

#### 4.4.1 Infrastructure Priority

| Building | Standard Priority | Island Civ Priority | Rationale |
|----------|-------------------|---------------------|-----------|
| Lighthouse | Standard (NAVAL_GROWTH flavor) | +150 weight | Every food/production tile from shallow water matters |
| Harbor | Standard | +200 weight | Trade range, production, naval unit healing |
| Seaport | Standard | +250 weight | Late-game naval production hub |
| Walls/Castle | FRONT_LINE gets boost | ALL island cities get +100 | Every city is vulnerable to amphibious assault |
| Market/Bank | Standard | +50 if sea trade active | Sea trade gold is critical revenue |

#### 4.4.2 Sea Trade Route Protection

Current state: `ApplyTradeRouteDefenseFlavors()` boosts FLAVOR_NAVAL for sea routes, but only during CRITICAL/NEEDED defense states. For island civs, this should be **permanent**:

```
For GEO_POSTURE_ISLAND / ARCHIPELAGO:
  - Minimum FLAVOR_NAVAL boost = +5 per active sea trade route
  - If any sea trade route passes through hostile-controlled water area:
    fTradeVulnerability += 0.2 per route
  - If fTradeVulnerability > 0.5:
    → Reroute vulnerable trade routes if alternatives exist
    → Prioritize naval production to secure trade lanes
    → Consider peace if losing sea control
```

#### 4.4.3 Blockade as Crisis Event

For continental civs, a blockaded city is painful but survivable (inland trade, land resource access). For island civs, blockade can be catastrophic:

```
Blockade crisis severity depends on island size relative to city workable tiles:

For each blockaded coastal city:
  iLandTilesWorkable = count of non-water tiles in city's workable range
  iTotalTilesWorkable = total tiles in city's workable range
  fLandRatio = iLandTilesWorkable / iTotalTilesWorkable

  If fLandRatio < 0.3: → BLOCKADE_CATASTROPHIC
    (tiny island city depends almost entirely on sea tiles for food/production)
  If fLandRatio < 0.5: → BLOCKADE_SEVERE
    (significant sea dependency)
  If fLandRatio < 0.7: → BLOCKADE_PAINFUL
    (moderate sea dependency)
  Else: → BLOCKADE_ANNOYING
    (large island, city mostly works land tiles)

If GEO_POSTURE_ISLAND/ARCHIPELAGO AND any city blockaded:
  bBlockadeCrisis = (worst blockade severity >= BLOCKADE_SEVERE)

  BLOCKADE_CATASTROPHIC effects:
    - Force DEFENSE_STATE_CRITICAL for naval
    - Emergency naval purchase enabled regardless of gold reserves
    - All available naval units redirected to break blockade
    - If blockade persists ≥3 turns: increase peace willingness by 40
    - Diplomatic: request ally naval assistance (if ally has fleet)

  BLOCKADE_SEVERE effects:
    - Force DEFENSE_STATE_CRITICAL for naval
    - Emergency naval purchase if affordable
    - If blockade persists ≥5 turns: increase peace willingness by 25

  BLOCKADE_PAINFUL effects:
    - Escalate naval defense to NEEDED if not already
    - If blockade persists ≥8 turns: increase peace willingness by 15

  BLOCKADE_ANNOYING effects (continental behavior):
    - Standard blockade response (existing code)
```

#### 4.4.4 Worker/Builder Priorities

Island civ workers should prioritize:
1. Roads connecting cities on same island (short, cheap)
2. Land tile improvements on current island (farms, mines, etc.)
3. Forts on strait-adjacent plots (create naval transit points — see Naval Geography Appendix C)

**Note:** Sea resources (fish, whales, pearls, etc.) are improved by **work boats**, not workers. Workers only need to embark when moving between islands to improve land tiles on a different island. Lighthouse/harbor are city buildings, not worker tasks.

Worker inter-island travel risk assessment:
- **Peacetime, no nearby hostile naval/barbarians:** Worker may embark freely — escort unnecessary
- **At war with a civ that has naval units within ~8 tiles of the transit route:** Worker should wait for escort or delay until route is clear
- **Barbarian naval units or camps near water on the transit route:** Worker should wait for escort
- **Late game (faster embarkation movement):** Transit window is shorter, risk is lower — escort less critical
- If escort is needed but none available within 3 tiles: queue the task and build on current island first

### 4.5 Inter-Island Logistics

**Goal:** The AI should move units between its islands efficiently and safely, using convoy escort and naval cover.

#### 4.5.1 The Convoy Problem

Currently, when an island civ needs to send units to another island, each unit embarks independently and the tactical AI tries to pair each with an escort (if available). This leads to:
- Units embarking at unpredictable times, scattered across the coast
- Escorts chasing individual embarked units instead of screening a group
- Settlers embarking with no escort because the nearest naval unit is 4 tiles away
- Units arriving at the destination staggered, unable to mass for defense

**Proposed: Convoy Staging System**

```
Convoy escort is situational, not mandatory:

RISK ASSESSMENT first:
  - Are we at war with any civ whose naval units can reach the transit route?
  - Are there barbarian naval units/camps near the transit route?
  - How long is the transit (tiles of open water)?
  - How fast is embarked movement at current tech?

If LOW RISK (peacetime, no hostiles, short transit):
  → Units may embark freely without escort
  → Single settlers/workers can cross independently

If MEDIUM RISK (hostile civ exists but no visible naval near route):
  → Group ≥2 units traveling same direction
  → Assign 1 escort if available; proceed without if not

If HIGH RISK (at war, enemy naval within ~10 tiles of route):
  Full convoy staging:
  1. STAGING: Designate a coastal city as the embarkation point
     (nearest city to destination, on the origin island)
  2. ASSEMBLY: Move all units to the embarkation city; move escorts
     to adjacent water tiles
  3. TRANSIT: Units embark simultaneously, escorts move alongside
     - Convoy speed = slowest embarked unit's water movement
     - Escorts maintain formation (RING1 of embarked group)
     - If any enemy unit detected within 5 tiles: escorts engage,
       embarked units continue on safest path
  4. LANDING: Disembark at destination, escorts patrol destination coast

  Priority assignment (HIGH RISK only):
    Settlers/GPs: MUST have escort (queue if none available, max 3-turn wait)
    Combat units (≥3):  Should have escort (proceed without if urgent)
    Single combat unit: May proceed unescorted (expendable)
```

#### 4.5.2 Island Group Awareness

The AI should track which islands it controls and their capabilities:

```
For each friendly landmass with ≥1 city:
  Record:
    - iLandmassID, iNumCities, iTotalPopulation
    - iLandMilitaryStrength (sum of garrison strength)
    - bHasCapital
    - bHasNavalProductionCity (city with harbor + naval unit production)
    - iDistanceToCapitalIsland (water tiles)
    - bConnectedByCanal (canal city linking this island to another)

  Priority for defense:
    Capital island > largest population island > naval production island > other

  Reinforcement logic:
    If an island has < 1 garrison per city AND enemies nearby:
      Request reinforcement from nearest island with surplus
      Use convoy staging system for transport
```

#### 4.5.3 Island-to-Island Attack Operations

When an island civ attacks an enemy city on another island:
- Pure naval feasibility first: can ranged ships reduce the city from water?
- Combined attack: embark land units + naval escort + ranged naval bombardment
- **Landing doctrine is situational, not fixed distance:**
  - The AI should evaluate the enemy's garrison strength and defensive capabilities before choosing landing positions.
  - **Surrounding the city** (landing adjacent to it) is often correct because it enables simultaneous land + water blockade and concentrated bombardment from both domains. City defense targeting priority spreads damage across multiple units, so landing a large group adjacent simultaneously is often survivable.
  - **Landing 1-2 tiles away** is better when the city has strong garrison and the AI has limited forces — avoids losing units to city bombardment during the landing turn.
  - **Key factors:** number of landing units (more = safer to land close), enemy garrison strength, whether friendly naval ranged can provide covering fire, whether landing as a group vs trickling in.
  - **Group landings preferred:** Landing multiple units in the same turn creates immediate tactical mass. A single unit landing adjacent to a defended city is likely to die; 4+ units landing simultaneously can absorb the city's attacks while establishing the siege.
- Pre-soften with naval bombardment for 1-3 turns when possible, but don't delay if the window is closing (enemy reinforcements approaching)
- Island civ should strongly prefer `AI_OPERATION_CITY_ATTACK_NAVAL` or `AI_OPERATION_CITY_ATTACK_COMBINED` over `AI_OPERATION_CITY_ATTACK_LAND` even when `HasSharedAreaWith` returns true (because the "shared area" may be a tiny island where maneuvering is impossible)

#### 4.5.4 Large Amphibious Landings on Continents

When an island civ needs to capture cities on a large continent (not just another small island), the force composition must shift significantly:

- **Exception to naval force ratio:** A continental invasion requires a substantial land force, including melee frontliners. The 60-70% naval floor should be temporarily relaxed when planning a major amphibious operation against a continental target.
- **Melee units are essential:** Melee land units are needed to capture the city, hold the beachhead, and form a frontline while ranged units (both land and naval) provide fire support. An all-ranged landing force cannot hold ground.
- **Force composition for continental invasion:**
  ```
  Recommended landing force:
    - 2-3 melee land units (frontline, city capture)
    - 2-3 ranged land units (fire support from beachhead)
    - 1 siege unit if available (city attack bonus)
    - 3-4 naval ranged (offshore bombardment)
    - 1-2 naval melee (blockade, escort, city capture from sea)
  ```
- **Post-landing logistics:** Once a beachhead is established on a continent, the island civ should consider building or capturing a port city to serve as a forward staging base. This avoids the need to escort every reinforcement from the home island.
- **Production shift:** When an amphibious continental invasion is planned, temporarily increase land unit production even beyond the normal garrison cap. The operational need overrides the posture-based force ratio.

#### 4.5.5 Late-Game Inter-Island Airlift

In the late game, cities with airports can airlift units between them. Airlift is NOT limited to one unit per city per turn — units can airlift from any plot adjacent to the city center as well as the city tile itself, meaning up to ~7 units can airlift out of (or into) a single city per turn. This provides a massive logistics improvement for island/archipelago civs:

- **Airlift eliminates embarkation risk** for unit transfers between established cities. No escort needed, no transit vulnerability.
- **High throughput:** With 6 adjacent plots + 1 city center, a single airport city can send or receive up to ~7 units per turn. Two airport cities can shuttle an entire army in 1-2 turns. This completely changes the logistics calculus for archipelago defense.
- **Airport priority for island civs:** Island/archipelago posture civs should prioritize building airports in all cities, not just front-line ones. The logistics value is much higher than for continental civs.
- **Airlift doesn't replace naval transit for:** settlers (no airport at destination yet), workers going to unsettled islands, initial colonization. But for reinforcing existing island cities, airlift is strictly superior.
- **Production AI impact:** Once airports are available, the land garrison cap per island becomes almost irrelevant — an entire garrison force can be airlifted from any island in a single turn, effectively creating a shared garrison pool across all airport-connected cities.
- **Strategic implication:** Late-game archipelago defense shifts from per-island garrison to centralized production + airlift. Build land units wherever production is cheapest, airlift to wherever they're needed. The AI should recognize that with airports, holding one high-production island and airlifting defenders out is superior to distributing production across all islands.

### 4.6 Navy-First Military Posture

**Goal:** Island civs should allocate the majority of their military production to naval units.

#### 4.6.1 Force Ratio Targets

```
Naval Force Ratio by Posture (base floors — trait/neighbor adjusted):
  CONTINENTAL:  iNavalPercent from formula (typically 25-40%)
  COASTAL:      max(formula, 35-45%)  ← depends on nearby civ threats (see §4.6.5)
  PENINSULAR:   max(formula, 30-45%)  ← depends on peninsula width + nearby naval civs
  ISLAND:       max(formula, 55-65%)  ← reduced from 60 when planning continental invasion
  ARCHIPELAGO:  max(formula, 65-75%)  ← reduced from 70 when planning continental invasion

These are starting floors. Actual ratio adjusted by:
  - Trait bonuses (see Appendix B: +5% for naval maintenance reduction, etc.)
  - Nearby threat composition (land-heavy neighbor → shift toward land)
  - Active continental invasion plan → temporarily reduce naval floor by 10-15%
  - Era (early = more naval for survival; late = airlift reduces naval dependency)
```

Currently, `SetRecommendedArmyNavySize()` computes `iNavalPercent` from `(coastalCities × FLAVOR_NAVAL × 7) / totalCities`. For an island civ with all coastal cities and FLAVOR_NAVAL=5 (average), this gives `(N × 5 × 7) / N = 35%`. This is too low for an island civ that depends entirely on naval superiority.

**Fix:** In `SetRecommendedArmyNavySize()`, after computing `iNavalPercent`, apply posture floor:
```cpp
// Floor for island/archipelago posture — base values, then adjust
GeographicPosture ePosture = GetStrategyMap()->GetGeographicPosture();
int iFloor = (ePosture == GEO_POSTURE_ARCHIPELAGO) ? 65
           : (ePosture == GEO_POSTURE_ISLAND) ? 55
           : (ePosture == GEO_POSTURE_PENINSULAR) ? 30
           : (ePosture == GEO_POSTURE_COASTAL) ? 35
           : 0;

// Trait adjustments
if (m_pPlayer->GetPlayerTraits()->GetNavalUnitMaintenanceModifier() < 0)
    iFloor += 5;  // Can sustain bigger fleet (England, Ottoman)
if (m_pPlayer->GetPlayerTraits()->GetExtraEmbarkMoves() > 0)
    iFloor -= 5;  // Embarked units less vulnerable (Polynesia)

// Neighbor threat adjustment for peninsula/coastal civs
// Peninsula/coastal civs near enemies across narrow water need more navy
// but those on long continental coastlines face primarily land threats
int iNearbyNavalThreat = EvaluateNearbyNavalThreat();  // see §4.6.5
iFloor += iNearbyNavalThreat;  // can be negative (shift toward land)

// Active continental invasion reduces floor temporarily
if (HasActiveAmphibiousInvasionPlan())
    iFloor -= 10;

iNavalPercent = max(iNavalPercent, iFloor);
```

#### 4.6.2 Land Unit Production on Islands

Island civs should still build SOME land units (garrison, city defense), but should stop earlier:
```
For each island:
  Desired land garrison = numCities on island × 1 (ranged garrison per city)
                        + 1 reserve unit per 3 cities

  If current land units on island >= desiredGarrison:
    Stop land unit production on this island (shift to naval)

  Exception: if ISLAND_INVADED, build land combat units urgently
```

This extends the existing tiny-island garrison cap (`landmass ≤ 3 → single garrison`) to all island sizes with a per-island target.

#### 4.6.3 Naval Composition for Island Civs

| Era | Preferred Composition |
|-----|----------------------|
| Ancient/Classical | 60% melee (galleys), 40% ranged (triremes equivalent) |
| Medieval | 50% ranged (galleasses), 30% melee (caravels), 20% transport for invasion |
| Renaissance | 50% ranged (frigates), 30% melee (corvettes), 20% subs/transport |
| Industrial+ | 40% ranged (battleships), 25% melee (destroyers), 20% subs, 15% carriers |

Standard flavor-driven composition works but should be overridden for island civs:
- Ranged naval units are more valuable (island defense = ranged denial)
- Submarines are valuable for intercepting enemy convoys (anti-shipping)
- Carriers become critical in late game for air power projection from sea

#### 4.6.4 Great Admiral Priority

For island civs, the Great Admiral should be generated more aggressively:
- In `WARTYPE_SEA`, the AI already requests Great Admiral over Great General (`CvPlayerAI.cpp L509`)
- For island posture, request Great Admiral even in `WARTYPE_LAND` (the island's "land war" is fought on the beaches)
- Great Admiral positioning: near the main fleet, ideally at a patrol station
- Great Admiral combat bonus applies to all naval units in range — critical for strait defense

#### 4.6.5 Neighbor-Aware Naval Investment (Peninsula & Coastal Civs)

The naval force ratio floor for PENINSULAR and COASTAL civs should NOT be static — it must depend on the composition and proximity of nearby threats:

```
EvaluateNearbyNavalThreat():
  iAdjustment = 0

  For each known civ within trade route range:
    iWaterDistance = shortest water path to their nearest city
    iLandDistance = shortest land path to their nearest city

    // Civs across narrow water (Korea↔Japan, Britain↔France)
    If iWaterDistance < 10 AND iWaterDistance < iLandDistance:
      iAdjustment += 10  (high naval priority — enemy is closer by sea)

    // Civs on same continent with long coastline (America, Brazil, Inca)
    If iLandDistance < iWaterDistance AND we share a large landmass:
      iAdjustment -= 5   (primary threat is by land)

    // Enemy force composition matters:
    If enemy has strong navy (>1.5× our naval strength):
      iAdjustment += 5
    If enemy has strong army but weak navy:
      iAdjustment -= 3   (they threaten by land, not sea)

  // Era adjustment: late-game naval units are more powerful and mobile
  If currentEra >= ERA_INDUSTRIAL:
    iAdjustment += 3     (naval threats become more dangerous)

  // Clamp to reasonable range
  return clamp(iAdjustment, -15, +15)
```

**Examples:**
- Korea (PENINSULAR) near Japan across narrow water: +10 adjustment → floor rises from 30 to 40
- America (COASTAL) with long coastline but main threat from Shoshone/Inca by land: -5 → floor drops from 35 to 30
- Brazil (COASTAL) with Inca nearby by land: -5 → reduce naval, focus on land defense
- Britain (ISLAND) near France across Channel: +10 → floor rises to 65

### 4.7 Strait Defense Doctrine

**Goal:** Naval chokepoints (straits) are the island civ's mountain passes. Controlling them is the primary defensive strategy.

#### What already exists:

`DetectNavalChokepoints()` identifies:
- `NAVAL_CHOKE_NEAR_STRAIT`: city within 3 tiles of a narrow water passage (width ≤ 3)
- `NAVAL_CHOKE_CANAL_CITY`: city connecting two water areas (highest strategic value)
- Passage width and strait tile positions are recorded

#### Strait defense behavior:

```
For each identified naval chokepoint:
  1. Classify importance:
     - Width 1 (single tile): EXTREME (one ship blocks all traffic)
     - Width 2: HIGH (two ships block)
     - Width 3: MODERATE (defensible with fleet)
     - Width 4+: LOW (bypassable, patrol rather than block)

  2. Assign fleet:
     - Width 1: Station 1 ranged ship ON the tile + 1 melee adjacent
     - Width 2: Station 2 ranged + 1 melee
     - Width 3: Station 2 ranged + 2 melee (overlapping fire)
     - Rotate ships when damaged (maintain presence)

  3. Alert logic:
     - Enemy ship enters within 3 tiles of strait → ALERT
     - ≥3 enemy ships within 5 tiles → MOBILIZE (call reinforcements)
     - Enemy breaks through strait → ESCALATE (emergency naval ops)

  4. Combined defense:
     - If city adjacent to strait, use city ranged attack + fleet fire
     - This creates overlapping kill zone: city + 2 ranged ships = 3 attacks
       on any unit entering the strait
```

#### Strait defense priority:

For island civs, strait defense gets the HIGHEST military priority (above even capital defense in some cases):
- A capital on a large island can tolerate some coastal raiding
- A broken strait allows enemy access to ALL water behind it
- → Strait = naval floodgate (even more critical than land floodgate)

### 4.8 Diplomacy Adjustments for Island Civs

**Goal:** Island civ diplomatic behavior should reflect their geographic reality.

#### 4.8.1 Threat Assessment

```
For an island civ evaluating threat from enemy E:
  If E has strong navy AND can reach our waters:
    Threat multiplier = 1.5× (they can actually hurt us)
  If E has strong army but weak navy AND no shared land area:
    Threat multiplier = 0.5× (they can't reach us yet)
  If E has embarkation tech AND army but no navy:
    Threat multiplier = 0.7× (amphibious invasion possible but risky for them)
  If E has submarines:
    Threat multiplier += 0.2 (trade route interdiction threat)
```

Currently, threat assessment uses raw military strength without considering geographic accessibility. An island civ shouldn't be terrified of a land superpower on another continent that has no navy.

#### 4.8.2 Alliance Value

```
For an island civ:
  Ally with strong navy: value += 50 (naval allies provide deterrence)
  Ally on same water body: value += 30 (can actually assist)
  Ally with army only: value += 10 (limited usefulness unless adjacent)
  Ally controlling strait we need: value += 80 (critical for connectivity)
```

#### 4.8.3 Peace Willingness

```
Island civ peace modifiers:
  If we control all relevant straits: -10 peace willingness (strong position)
  If enemy controls a strait we need: +20 peace willingness (cut off)
  If blockaded: +15 peace willingness per blockaded city
  If lost naval superiority (enemy fleet > 2× ours): +25 peace willingness
  If enemy has landed on our island: +30 peace willingness (existential)
```

#### 4.8.4 War Declaration

```
Island civ war evaluation:
  Against enemy on same water body:
    If our navy > 1.5× their navy: willing (can project power)
    If navies roughly equal: cautious (war of attrition at sea is costly)
    If their navy > our navy: unwilling (we'd lose control of our waters)

  Against enemy on different water body:
    If water connectivity exists (through straits/canals): evaluate normally
    If no water path: cannot attack → do not declare war

  Against enemy sharing our island:
    Standard land war evaluation applies
    But: other island civ enemies may exploit our distraction → consider
    two-front naval risk
```

---

## 5. Implementation Phases

### Phase I-1: Geographic Posture Classification (Lowest complexity, foundation for all else)

**Goal:** Compute `GeographicPosture` per player, expose it to all AI subsystems.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.h/cpp` — add `GeographicPosture` enum and `ComputeGeographicPosture()` function
- Modified: `CvMilitaryAI.h/cpp` — expose `GetGeographicPosture()` for other systems

**Algorithm:**
1. Group cities by landmass ID
2. Find largest landmass, count landmasses, compute coastal ratio
3. Classify: CONTINENTAL / COASTAL / PENINSULAR / ISLAND / ARCHIPELAGO

**Estimated scope:** ~120 lines new code, ~20 lines modified

**Acceptance criteria:**
- Japan on Giant Earth → ISLAND
- Britain on Giant Earth → ISLAND (small island) or PENINSULAR (if large map variant)
- Indonesia on Giant Earth → ARCHIPELAGO
- Russia on Giant Earth → CONTINENTAL
- Italy on Giant Earth → PENINSULAR
- Portugal on Giant Earth → COASTAL
- Polynesia → ISLAND or ARCHIPELAGO depending on expansion

---

### Phase I-2: Navy-First Force Allocation (Medium complexity, high immediate impact)

**Goal:** Island/archipelago posture civs allocate 60-70% of military production to naval units.

**Files to modify:**
- Modified: `CvMilitaryAI.cpp` → `SetRecommendedArmyNavySize()` — apply posture-based naval floor
- Modified: `CvUnitProductionAI.cpp` → extend garrison cap to per-island basis
- Modified: `CvMilitaryAI.cpp` → Great Admiral request for island posture regardless of war type

**Estimated scope:** ~80 lines modified

**Acceptance criteria:**
- Island civ with FLAVOR_NAVAL=5 produces ≥60% naval units (vs current ~35%)
- Island with 3 cities stops producing land units after 4 garrison + 1 reserve
- Island civ requests Great Admiral even during land skirmishes

---

### Phase I-3: Blockade Crisis Response (Low-medium complexity, high impact for island civs)

**Goal:** Blockade severity scales with island size — small-island cities are catastrophically affected, large-island cities less so.

**Files to modify:**
- Modified: `CvMilitaryAI.cpp` → `UpdateDefenseState()` — blockade severity scaling by land/water tile ratio
- Modified: `CvMilitaryAI.cpp` → `CheckSeaDefenses()` — auto-launch NAVAL_SUPERIORITY on SEVERE+ blockade
- Modified: `CvDiplomacyAI.cpp` → peace willingness modifier scaled by blockade severity

**Estimated scope:** ~100 lines modified

**Acceptance criteria:**
- Tiny island city (>70% water tiles) under blockade → BLOCKADE_CATASTROPHIC → CRITICAL defense state
- Large island city (<30% water tiles) under blockade → BLOCKADE_ANNOYING → standard response
- NAVAL_SUPERIORITY operation launched for SEVERE+ blockade
- Peace willingness scales: +40 for CATASTROPHIC after 3 turns, +15 for PAINFUL after 8 turns

---

### Phase I-4: Patrol Station Placement (Medium complexity, defensive posture enabler)

**Goal:** Island civ naval units have persistent patrol stations rather than clustering in harbors.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` — add `ComputePatrolStations()` to the update pipeline
- Modified: `CvHomelandAI.cpp` → `PlotNavalSentryMoves()` — use patrol stations as sentry destinations
- Modified: `CvTacticalAI.cpp` → idle naval units → move to nearest unoccupied patrol station

**Estimated scope:** ~200 lines new, ~50 lines modified

**Acceptance criteria:**
- Each FRONT_LINE coastal city has 1-2 patrol stations in nearby water
- Idle naval units drift toward patrol stations instead of clustering in harbor
- Patrol station positioned to cover primary threat approach vector

---

### Phase I-5: Risk-Assessed Convoy System (High complexity, logistics improvement)

**Goal:** Group embarked units for inter-island transit; assign convoy escort based on threat level, not unconditionally.

**Files to modify:**
- New function: `PlanConvoyTransit()` in `CvTacticalAI.cpp` or `CvMilitaryAI.cpp`
- Modified: `CvTacticalAI.cpp` → `PlotNavalEscortMoves()` — convoy escort mode
- Modified: `CvHomelandAI.cpp` → settler/worker movement — risk assessment before embarking
- Modified: `CvTacticalAI.cpp` → `FindSafestPlotInReach()` — embarked units prefer convoy path

**Estimated scope:** ~350 lines new, ~100 lines modified

**Acceptance criteria:**
- Peacetime with no hostile naval: units embark freely without waiting for escort
- At war with nearby naval civ: settlers wait at embarkation city until escort (max 3-turn window)
- ≥2 embarked units in HIGH RISK traveling same direction grouped into convoy with escort
- Convoys take safest water route, not shortest
- Late-game cities with airports use airlift instead of embarkation when possible

---

### Phase I-6: Amphibious Threat Escalation (Medium complexity, early warning)

**Goal:** Detect approaching enemy invasion convoys and respond before they land.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` → `AssessAmphibiousThreats()` — add convoy detection
- Modified: `CvMilitaryAI.cpp` → `UpdateDefenseState()` — convoy proximity escalation
- Modified: `CvTacticalAI.cpp` → new `PlotAntiInvasionMoves()` targeting detected convoys

**Estimated scope:** ~200 lines new, ~60 lines modified

**Acceptance criteria:**
- ≥3 enemy embarked units within 3 tiles of each other + within 8 tiles of coast → INVASION_IMMINENT
- Naval defense state forced to CRITICAL
- Available naval units intercept convoy (target embarked units, prioritize settlers)

---

### Phase I-7: Strait Defense Doctrine (Medium-high complexity, strategic defense)

**Goal:** Fleets stationed at identified naval chokepoints as primary defensive positions.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` → add `GetStraitDefensePositions()` query
- Modified: `CvTacticalAI.cpp` → `PlotCoastalDefenseMoves()` — assign ships to strait positions
- Modified: `CvMilitaryAI.cpp` → defense operation targeting strait when threatened
- Modified: `CvDiplomacyAI.cpp` → strait control affects threat/peace calculations

**Estimated scope:** ~250 lines new, ~100 lines modified

**Acceptance criteria:**
- Width-1 strait has 1 ranged + 1 melee ship stationed permanently
- Enemy entering within 3 tiles of strait triggers ALERT
- Strait breach (enemy passes through) triggers emergency mobilization
- Strait control reduces perceived threat from far-side enemies

---

### Phase I-8: Island Economy & Diplomacy (Medium complexity, polish)

**Goal:** Full integration of island posture into economic and diplomatic AI.

**Files to modify:**
- Modified: `CvBuildingProductionAI.cpp` → harbor/lighthouse/seaport priority boost for island posture
- Modified: `CvSiteEvaluationClasses.cpp` → settler prefers SHELTERED harbors for island civs
- Modified: `CvDiplomacyAI.cpp` → threat assessment filtered by naval accessibility
- Modified: `CvDiplomacyAI.cpp` → alliance value weighted by partner's naval strength
- Modified: `CvDiplomacyAI.cpp` → war willingness gated by relative naval power for island civs
- Modified: `CvBuilderTaskingAI.cpp` → worker risk-assesses embarkation (escort only when hostiles nearby)
- Modified: `CvBuilderTaskingAI.cpp` → fort construction priority on `IsWaterAreaSeparator()` plots (canal > city for allied naval access)

**Estimated scope:** ~150 lines new, ~200 lines modified

**Acceptance criteria:**
- Island civ builds lighthouse/harbor 20% earlier than continental civ
- Island civ settler prefers SHELTERED coastal sites (+30% value)
- Island civ doesn't declare war on naval superior enemy across water
- Island civ values naval alliance partner 50% more than land-army partner
- Workers risk-assess embarkation (escort when at war or barbarian threat; free movement in safe peacetime)
- Builder AI prioritizes forts on strait-adjacent plots over cities (for allied naval access)

---

## 6. Performance Considerations

The island civ system adds very little computational overhead because it's primarily a **classification** and **policy** layer, not a new scanning system:

| Operation | Complexity | Frequency | Budget |
|-----------|-----------|-----------|--------|
| Geographic posture classification | O(cities) | On city events | < 1ms |
| Patrol station computation | O(coastal_cities × RING3) | Every 5 turns | < 5ms |
| Convoy staging/tracking | O(embarked_units × naval_units) | Every turn | < 2ms |
| Convoy detection (enemy) | O(enemy_embarked × scan_radius) | Every turn (wartime) | < 3ms |
| Strait defense assignment | O(straits × navy_size) | Every 3 turns | < 2ms |
| Force ratio enforcement | O(1) | Every turn | < 0ms (constant time) |
| **Total per full update** | | | **< 13ms** |

Most heavy lifting (coastal exposure, naval chokepoints, water connectivity, amphibious threats) is already computed by the Strategic Geography Map's naval phases. The island civ system primarily *consumes* that data rather than computing new geographic analysis.

---

## 7. Testing Strategy

### 7.1 Logging

Add `IslandCivLog.csv` per turn:
```
Turn, Player, Posture, NumIslands, LargestIsland, NavalForceRatio, PatrolStations,
ActiveConvoys, StraitControlled, BlockadeCrisis, AmphibiousImminentFrom
```

### 7.2 Scenario Tests

| Test | Map | Setup | Expected Result |
|------|-----|-------|-----------------|
| **Japan defense** | Giant Earth TSL | Japan vs China (war) | ISLAND posture, 60% naval, patrol stations in Sea of Japan, strait defense |
| **Britain Channel** | Giant Earth TSL | Britain vs France (war) | ISLAND posture, fleet concentrated in English Channel strait, SHELTERED harbor preferred for city |
| **Indonesia archipelago** | Giant Earth TSL | Indonesia vs India (war) | ARCHIPELAGO posture, 70% naval, convoy escort for inter-island settlers, Malacca strait defense |
| **Polynesia expansion** | Giant Earth TSL | Polynesia peacetime | ARCHIPELAGO posture after 3+ cities, convoy escort for settlers, naval patrol perimeter |
| **Continental comparison** | Giant Earth TSL | Russia peacetime | CONTINENTAL posture, standard land force allocation, no patrol stations |
| **Peninsula detection** | Giant Earth TSL | Korea vs China | PENINSULAR posture, chokepoint defense at peninsula neck, 35% naval floor |
| **Blockade crisis** | Any map | Blockade an island AI | Immediate CRITICAL response, NAVAL_SUPERIORITY launched, peace willingness increases |
| **Invasion detection** | Any map | Send 4 embarked units toward island AI | INVASION_IMMINENT detected at 8 tiles, naval interception, target embarked units |

### 7.3 Behavioral Validation

- [ ] Island AI maintains naval patrol perimeter during peacetime
- [ ] Island AI does NOT build excessive land units (stops at garrison cap)
- [ ] Island AI protects settlers with naval escort during inter-island transit
- [ ] Island AI prioritizes lighthouse/harbor/walls in coastal cities
- [ ] Island AI treats blockade as a crisis (not a nuisance)
- [ ] Island AI stations fleet at straits, not randomly in harbors
- [ ] Island AI doesn't fear land superpowers with no navy
- [ ] Island AI makes peace when losing naval superiority
- [ ] Island AI targets enemy embarked units with extreme prejudice
- [ ] Island AI prefers SHELTERED harbor sites for new cities

---

## 8. Risk Assessment

| Risk | Impact | Likelihood | Mitigation |
|------|--------|-----------|------------|
| **Island over-investment in navy** → no land defense | HIGH | MEDIUM | Maintain per-island garrison minimum (1 ranged per city + 1 reserve per 3 cities) |
| **Convoy staging delays** → settlers sit for too long | MEDIUM | MEDIUM | 3-turn max wait for escort; after that, proceed unescorted. Don't block the entire operation. |
| **Misclassification** (e.g., COASTAL → ISLAND) | MEDIUM | LOW | Conservative thresholds (landmass < 50 for ISLAND); reclassify on city events |
| **Patrol stations on wrong tiles** | LOW | MEDIUM | Patrol positions are suggestions, not mandates; tactical AI can override if combat requires |
| **Performance on archipelago maps** | LOW | LOW | System is O(cities), not O(tiles); even 20+ islands are trivial |
| **Strait defense too rigid** | MEDIUM | MEDIUM | Cap strait fleet at 30% of total navy; allow tactical override when main fleet needed elsewhere |
| **C++03/VC9 compatibility** | HIGH | LOW | Use only STL containers (vector, map, set); avoid C++11 features (enum class, auto, range-for) |

---

## 9. Relationship to Strategic Geography Map

The island civ system is a **consumer** of the Strategic Geography Map, not a replacement:

| Strategic Geography Data | Island Civ System Uses It For |
|-------------------------|------------------------------|
| Coastal exposure (SHELTERED/MODERATE/EXPOSED) | Settler site evaluation, harbor defense classification |
| Naval chokepoints (straits) | Strait defense doctrine, patrol station placement |
| Water connectivity graph | Convoy route planning, threat accessibility filtering |
| Amphibious threat score | Invasion detection, defense state escalation |
| Defensive layer classification | Island posture adds to this — all island cities are effectively FRONT_LINE |
| City dependency graph (floodgates) | Strait cities may be naval floodgates — highest priority |

**New data the island system adds:**
- `GeographicPosture` per player (consumed by all subsystems)
- `IslandGroup` inventory (per-island city/military tracking)
- `PatrolStations` (consumed by tactical/homeland AI for naval positioning)
- `PendingTransits` (convoy tracking for escort coordination)
- Force ratio floors (consumed by military production AI)

The island system sits conceptually **above** the Strategic Geography Map:

```
CvPlayer
  └── CvMilitaryAI
       └── CvStrategicGeographyMap (terrain analysis — already exists)
            ├── Land phases 1-6 (defensive layers, salients, chokepoints, etc.)
            ├── Naval phases 1-4 (coastal exposure, straits, connectivity, amphibious)
            └── IslandCivData (NEW — strategic posture, patrol, convoy, economy)
                 ├── Consumed by: CvMilitaryAI (force sizing, defense state)
                 ├── Consumed by: CvTacticalAI (patrol, convoy escort, interception)
                 ├── Consumed by: CvDiplomacyAI (threat filter, alliance value, peace)
                 ├── Consumed by: CvBuildingProductionAI (infrastructure priority)
                 ├── Consumed by: CvSiteEvaluationClasses (settler harbor preference)
                 └── Consumed by: CvHomelandAI (sentry positioning, worker embark safety)
```

---

## 10. Resolved Questions & Remaining Open Questions

### Resolved

1. **Peninsula classification and naval investment** — RESOLVED: Naval investment for peninsular/coastal civs should depend on nearby civ proximity and force composition, not a static floor. Korea near Japan across narrow water needs higher naval investment than Italy with land threats from the north. See §4.6.5 `EvaluateNearbyNavalThreat()` for the dynamic adjustment system. Similarly, continental civs with long coastlines (America, Shoshone, Inca, Brazil) face primarily land threats and should not over-invest in navy just because they have coastal cities — the system evaluates land vs. water distance to nearby threats and adjusts accordingly.

2. **Trait system feeding into geographic posture** — RESOLVED: Yes, traits should adjust the naval force ratio floor. Naval maintenance reduction (England/Ottoman) allows sustaining a larger fleet and adds +5% to the floor. Embarked combat bonuses (Polynesia) reduce convoy vulnerability and subtract -5% from the floor. Trade route bonuses add +3% per active sea trade route. See §4.6.1 and Appendix B for the full interaction model.

3. **Canal cities vs. fort canals** — RESOLVED: For creating naval transit points on strait-adjacent plots, **building a fort is generally preferable to founding a city.** Reason: a canal city's transit is owner-only for military naval units, while a passable fort allows allied navy to pass through (with open borders). For an island/archipelago civ that needs allied naval support, fort-canals provide more strategic value. The settler AI should NOT prioritize settling on `IsWaterAreaSeparator()` plots unless the site is also a good city location for other reasons. Instead, the builder AI (`CvBuilderTaskingAI`) should prioritize fort construction on strait-adjacent plots.

4. **Multi-island defense allocation** — RESOLVED: Fleet concentration vs. distribution depends on early warning and enemy force size:
   - **Rely on early water patrol** (patrol stations from §4.2) to detect enemy fleets early. This gives time to concentrate forces.
   - **Dispersed 1-ship-per-city** has value as a last resort: a single ship can retreat into the city during attack and wait for reinforcements from other cities. It also provides detection/vision.
   - **Concentrated fleet is usually better** because a scattered force cannot stand against an enemy naval army. Exception: if the enemy is also scattered.
   - **Blockade complication:** If a city is blockaded, naval units inside may be prevented from leaving (or leave at significant risk). Often it's better for the relief fleet to converge at a back-water plot (behind the city, away from the enemy) rather than trying to exit through the blockade.
   - **Heavily situational:** The AI must evaluate enemy unit count and positions, use city/coastal ranged units to assist, and avoid committing the fleet until it can achieve local superiority.

5. **Era-gating** — RESOLVED: The existing `ECONOMICAISTRATEGY_ISLAND_START` already handles the pre-embarkation case: it triggers in the first 25 turns if the civ can't embark and is on a small landmass, boosting embarkation tech research priority by +10. This is correct behavior. The full island posture system should activate once the first city is founded (for classification) but the escort/convoy/logistics subsystems only become relevant once the civ has embarkation tech. Pre-embarkation island civs should focus on naval tech rush (which `ISLAND_START` already does) and early naval unit production for coastal defense.

6. **War type system and geographic posture** — RESOLVED: Yes, the `WARTYPE_SEA` / `WARTYPE_LAND` classification should respect geographic posture. For island/archipelago civs, default to `WARTYPE_SEA` for all wars where the enemy is across water, regardless of enemy force composition. Rationale: even if the enemy has no navy, the island civ must project power across water to attack them, so naval production is needed. The existing per-enemy computation based on unit counts can remain as a modifier, but the posture-based default provides the correct baseline.

7. **Venice merchant** — RESOLVED: Venice's Merchant of Venice is a restricted type of settler that can found or buy city-states. It is NOT restricted to coastal tiles — it can found cities anywhere and purchase any city-state. Venice's entry in Appendix B should not emphasize "coastal expansion" as a defining trait. The Great Galleass UU is Venice's actual naval strength, but its timing matters: Venice gets the Fusta (unique galleass replacement) first, then must wait for the Great Galleass tech — which comes LATER than the standard Galleon tech. This creates a vulnerable gap period where Venice has only the Liburnian (unique trireme) while other civs already have Galleons. Venice should build Fustas to bridge this gap.

8. **One-city challenge (OCC) island** — RESOLVED: OCC island civ is a "fortress harbor" strategy. 100% garrison priority for the single city (always keep a strong garrison unit inside), all remaining production goes to naval units. No inter-island logistics apply since there is only one city. Naval patrol perimeter around the city is the only defensive ring. Naval force should be maximized since there is no land expansion to protect.

9. **Peacetime patrol stations** — RESOLVED: In true peacetime with no hostile neighbors, patrols can be temporarily suspended or reduced to allow naval units to heal/upgrade in city. However, the AI must maintain minimum detection capability to guard against surprise/sneak attacks:
   - **Full peacetime (no hostile neighbors within ~15 tiles):** Reduce patrol to 1 unit on a 3-tile rotation (enough for vision coverage), recall the rest to city for healing/upgrading.
   - **Tense peace (hostile neighbor exists but no active war):** Maintain full patrol. Sneak attacks are a real risk — the patrol is the early warning system.
   - **Just-ended war (within 10 turns of peace treaty):** The peace deal is enforced for 10 turns — the former enemy *cannot* attack during this window. Use this safe period to recall most naval units for healing/upgrading in city. However, maintain minimum patrol (1 unit rotation) to watch for *other* civs who might sneak-attack while the fleet is recovering. If no other hostile-capable civ exists nearby, even the minimum patrol can be suspended during the treaty window.
   - **Key rule:** Never leave ALL water approaches completely unmonitored when any potential threat exists. At minimum, keep 1 naval unit on patrol even in deep peacetime — it detects incoming enemy fleets 2-3 turns before they reach the city. The only exception is when all nearby civs are in enforced peace treaties or confirmed allies.

10. **Mid-game posture transitions** — RESOLVED: Reclassification speed depends on the safety of existing cities:
    - **Default: immediate reclassification.** When an island civ conquers a continental beachhead, immediately reclassify from ISLAND → COASTAL (or PENINSULAR) so the AI adjusts force ratios to account for the new land border.
    - **Exception: island homeland also under attack.** If the original island cities are simultaneously threatened (enemy navy near homeland), force a gradual transition instead — the island cities still need their naval defense allocation and the AI cannot instantly shift to a land-heavy posture while also defending the sea.
    - **Beachhead risk assessment:** The newly captured continental city usually carries higher risk initially (enemy counterattacks, no established garrison, unfamiliar terrain). The AI should temporarily allocate extra resources to the beachhead city's defense while ramping up land unit production.
    - **Transition logic:**
      ```
      If posture changes from ISLAND → COASTAL/PENINSULAR:
        If any island city has danger > MEDIUM (homeland under threat):
          → Gradual transition: blend old/new force ratios over 10 turns
          → Maintain naval patrol on island while building land forces for continent
        Else:
          → Immediate reclassification
          → Shift production: start building land units for continental defense
          → Beachhead city gets +50% garrison priority for first 10 turns
      ```

### Remaining Open Questions

(None at this time — all identified questions have been resolved through design discussion.)

---

## Appendix A: Key Source Files Reference

| File | Role | Relevant Functions | Lines (approx) |
|------|------|-------------------|----------------|
| `CvStrategicGeographyMap.h/cpp` | Strategic terrain analysis | `AnalyzeCoastalExposure`, `DetectNavalChokepoints`, `BuildWaterConnectivityGraph`, `AssessAmphibiousThreats` | 318 / 2,200+ |
| `CvMilitaryAI.h/cpp` | Military operations & force sizing | `SetRecommendedArmyNavySize`, `UpdateDefenseState`, `CheckSeaDefenses`, `GetWarType` | 450 / 5,534 |
| `CvTacticalAI.h/cpp` | Tactical unit assignment | `PlotCoastalDefenseMoves`, `PlotNavalEscortMoves`, `ExecuteEscortEmbarkedMoves`, `BuyEmergencyUnit` | ~800 / 19,760 |
| `CvHomelandAI.cpp` | Peacetime unit management | `PlotNavalSentryMoves`, naval exploration, Great Admiral | large |
| `CvUnitProductionAI.cpp` | Unit building decisions | Garrison cap, naval strategy bonuses, war type production boost | large |
| `CvDiplomacyAI.h/cpp` | Diplomatic behavior | Peace willingness, threat assessment, war evaluation | 2,469 / 59,835 |
| `CvSiteEvaluationClasses.cpp` | City founding site eval | `PlotFoundValue`, coastal bonuses, `ComputeFoodValue` | large |
| `CvBuildingProductionAI.cpp` | Building priority | Defense mod from strategic geography, naval building weights | large |
| `CvBuilderTaskingAI.cpp` | Worker/builder task selection | Fishing boat priority, fort/canal building, worker embark | large |
| `CvUnit.cpp` | Unit mechanics | `isEmbarked`, `GetEmbarkedUnitDefense`, `CanEverEmbark` | large |
| `CvAIOperation.h/cpp` | Military operations | `CvAIOperationNavalSuperiority`, `CvAIOperationCityAttackNaval` | 440 / large |
| `CvTradeClasses.cpp` | Trade route system | `GetTradeRouteRange`, sea trade, trade path cache | large |
| `CvPlayer.cpp` | Player-level queries | `GetNumEffectiveCoastalCities`, `UpdateCityThreatCriteria` | 49,533 |
| `CvEconomicAI.cpp` | Economic strategy | `ISLAND_START`, `MostlyOnTheCoast`, `ExpandToOtherContinents` | large |
| `CvCityStrategyAI.cpp` | City production strategy | `NEED_NAVAL_GROWTH`, `NEED_NAVAL_TILE_IMPROVEMENT`, `UNDER_BLOCKADE` | large |

## Appendix B: Naval Civ Traits in Vox Populi

These traits create natural synergies with the island civ strategy. The trait system should feed into posture-specific adjustments:

| Civ | Key Naval Trait | Island Strategy Interaction |
|-----|----------------|---------------------------|
| **England** | -25% naval maintenance, free promotions | Can sustain larger fleet on island budget; patrol stations more viable |
| **Polynesia** | Ocean embarkation, embarked combat, +2 prod fishing boats | Reduced convoy vulnerability; more aggressive exploration; embark penalty reduced |
| **Carthage** | Trade diversity bonus, +5 XP purchased units | Rapid fleet building; economic focus on sea trade routes |
| **Denmark** | Viking promotion (melee land), Longboat (melee naval) | Amphibious assault capability; melee-heavy naval composition |
| **Indonesia** | Unique luxuries, Djong UU (galleass) | Archipelago economic model; early ranged naval advantage |
| **Ottoman** | Zero naval maintenance, trade route yields | Maximum fleet size for budget; sea trade priority |
| **Netherlands** | Import/export bonuses, Sea Beggar UU (corvette) | Trade-focused naval; mid-game melee naval dominance |
| **Portugal** | +gold/science per trade route movement, Nau UU (caravel) | Exploration + trade synergy; early naval reach |
| **Venice** | Merchant of Venice (settler variant, NOT coast-restricted), Great Galleass UU | Fusta bridges gap until Great Galleass; vulnerable period between Fusta and Great Galleass when others have Galleon; Merchant can found/buy cities anywhere |

**Trait interaction with geographic posture (RESOLVED — implemented in §4.6.1):**

Traits directly adjust the naval force ratio floor:
- Naval maintenance reduction (England/Ottoman): +5% floor (can sustain bigger fleet on same budget)
- Ocean embarkation / embarked combat bonuses (Polynesia): -5% floor (embarked units less vulnerable, convoy escort less critical)
- Trade route range/yield bonuses (Portugal, Ottoman, Netherlands): +3% floor per active sea trade route (more economic value to protect at sea)
- Flat-cost disembarkation (Denmark's Viking promotion): +3% floor (amphibious attacks are stronger, favoring naval approach)
- Faith-purchased naval units: +2% floor (alternative production path for navy means less opportunity cost)

These adjustments are applied in `SetRecommendedArmyNavySize()` — see §4.6.1 code snippet.

## Appendix C: Embarked Unit Vulnerability — Detailed Analysis

Understanding exactly how vulnerable embarked units are is critical for convoy design:

### Defense Calculation Chain

```
Normal land unit defense:
  GetBaseCombatStrength()                    // e.g., 30
  × (100 + terrain defense mod) / 100        // e.g., +25% hills = 37.5
  × (100 + fortify mod) / 100                // e.g., +20% = 45
  × (100 + promotion mods) / 100             // e.g., +15% Drill = 51.75
  × (100 + flanking/support) / 100           // etc.
  = Final defense ~50+

Embarked unit defense:
  GetBaseCombatStrength()                    // e.g., 30
  × (100 + EmbarkDefensiveModifier) / 100    // typically +0
  = Final defense 30 (or 3000 in 100× scale)
```

**An embarked unit has ~40-60% less defense than the same unit on land with typical bonuses.** Against naval ranged units that do full damage at range, an embarked unit is effectively a one-shot kill.

### Attacker Benefits Against Embarked

```
Melee attack vs embarked:
  - Zero retaliation damage (attacker takes no self-damage)
  - Full attack strength (no penalty for attacking embarked)
  - Embarked unit uses base strength only (massive gap)

Ranged attack vs embarked:
  - Standard ranged damage calculation
  - But defense is so low that most ranged units one-shot or two-shot

Air attack vs embarked:
  - Normal air combat, but no interceptor penalty
  - Embarked unit CAN retaliate against air (exception to no-retaliation rule)
```

### Practical Kill Thresholds by Era

| Era | Typical Embarked Defense | Naval Ranged Damage | Shots to Kill |
|-----|-------------------------|--------------------:|:-------------:|
| Classical | Spearman (16) | Trireme ranged (~20) | 1-2 |
| Medieval | Longswordsman (25) | Galleass (~28) | 1-2 |
| Renaissance | Musketman (34) | Frigate (~40) | 1-2 |
| Industrial | Infantry (50) | Battleship (~65) | 1 |

**Conclusion:** Embarked units are extremely fragile across all eras. Convoy escort is not optional for high-value units — it's essential. The AI must treat embarked transit as a high-risk activity, not routine movement.

### Implications for Convoy Design

1. **Escort firepower must deter, not just respond.** A single escort ship may not prevent attacks — the enemy can sacrifice one unit to sink an embarked settler. Need 2+ escorts to create a deterrence zone where attacking the convoy costs more than it gains.

2. **Route matters more than escort count.** A convoy through contested waters with 3 escorts is less safe than a convoy through friendly waters with 1 escort. Convoy route planning should prioritize water tiles with low danger scores.

3. **Speed saves lives.** An embarked unit spending 3 turns crossing open water is 3× as vulnerable as one spending 1 turn. Movement bonuses (Great Admiral, promotions, trait bonuses) dramatically improve survival. The AI should avoid slow routes.

4. **Night crossing concept.** If the convoy can time its transit when enemy ships are furthest away (end of their turn movement), it reduces exposure. Practically, this means embarking and moving toward the destination in a single turn when possible, rather than embarking on one turn and sitting in water until the next.

---

## Appendix D: Comparison with Other Strategy Games

For context, here's how other strategy games handle island civs:

| Game | Approach | What VP Could Learn |
|------|---------|---------------------|
| **Civ 6** | `STRATEGY_NAVAL` trait flag; island start triggers permanent naval emphasis | Binary flag is too simple; graduated posture (our approach) is better |
| **Civ 4: BtS** | Naval AI module with "island empire" logic; dedicated amphibious operation planning | Convoy staging concept; fleet-in-being doctrine |
| **EU4** | Naval force limit separate from land; island nations get army tradition penalty but huge naval tradition | Dual force limits interesting but complex for Civ5 |
| **HoI4** | Naval invasion planning with convoy escorts; naval supremacy requirements for landings | Invasion planning UI; requires establishing naval supremacy before landing |
| **Total War** | Agent-based naval AI; fleets patrol sea lanes; amphibious invasions require fleet escort | Patrol station concept matches well |

**Key takeaway:** Most games give island civilizations a fundamentally different military doctrine. Civ 5/VP currently does not. The graduated posture system (CONTINENTAL → COASTAL → PENINSULAR → ISLAND → ARCHIPELAGO) is more nuanced than any single competitor's approach.
