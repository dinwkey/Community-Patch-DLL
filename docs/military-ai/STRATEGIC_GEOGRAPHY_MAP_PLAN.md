# Strategic Geography Map — Implementation Plan

> **Status:** Planning  
> **Author:** AI-assisted analysis session, Feb 2026  
> **Scope:** Major AI improvement — persistent strategic terrain awareness for Vox Populi  
> **Prerequisite commit:** `6d5f9df81` (multi-front war handling improvements)

---

## 1. Problem Statement

### The Greece Scenario (motivating example)

On a TSL Giant Earth map, AI Greece gets coop-war'd by four civilizations simultaneously:
- **Venice** (west) — naval + land threat to western coast
- **Poland** (north) — land threat from the Balkans
- **Byzantium** (east) — land threat from Anatolia
- **Egypt** (south) — naval threat across the Mediterranean

**Observed behavior:** Greece concentrates defense on a peripheral northeastern city while Athens (the capital) falls essentially undefended. A human player would immediately recognize that Athens is the strategic center-of-gravity, that the northeastern city is an exposed salient, and that the mountain passes north of Athens form natural chokepoints worth defending.

### Root Cause

The AI has no persistent **strategic geography layer**. It evaluates cities independently each turn through dominance zones and threat scores but lacks understanding of:
- How cities relate to each other spatially (dependency chains)
- Where natural defensive lines exist (mountains, rivers, narrow land bridges)
- Which cities are exposed salients vs. defensible core territory
- How terrain corridors constrain enemy approach vectors
- Which routes are strategically critical for interior lines

---

## 2. What Exists Today

### 2.1 CvTacticalAnalysisMap (tactical layer)

| Feature | What it does | Limitation |
|---------|-------------|------------|
| **Dominance zones** | Divides map into city-centric sectors, tracks friendly/enemy strength | Per-zone, no inter-zone relationships |
| **Posture selection** | Assigns WITHDRAW/HEDGEHOG/STEAMROLL etc. per zone | Single-zone analysis; multi-zone pass is shallow |
| **Zone priority** | Scores zones by economic value, damage, dominance, focus area | No defensive depth or strategic terrain weighting |
| **Border score** | Per-zone metric of border exposure | Binary (exposed or not), no corridor analysis |

**Recent enhancements (commit `6d5f9df81`):**
- Capital zone gets 2x base value priority boost
- Core cities within 6 tiles of capital get 1.5x boost
- Focus-area cities get 3x boost

### 2.2 CvMilitaryAI (operational layer)

| Feature | What it does | Limitation |
|---------|-------------|------------|
| **m_exposedCities** | List of cities vulnerable to enemy attack | Flat list, no front/theater grouping |
| **Defense operations** | Assigns RAPID_RESPONSE ops to threatened cities | Fixed slots (2-3 cities), not terrain-aware |
| **Memory threat weight** | Aggregates threat from vanished units + coalition signals | No spatial component |
| **Attack targets** | Evaluates enemy cities for offensive operations | Path-based, no strategic corridor awareness |

**Recent enhancements (commit `6d5f9df81`):**
- Per-enemy front defense assignments (`perEnemyLandCity`/`perEnemyCoastalCity` maps)
- 3rd defense slot when fighting ≥3 enemies
- `EvaluateTacticalRetreat()` implemented (was a stub) — capital rapid response + offense consolidation
- Coalition multiplier in memory threat weight

### 2.3 CvPlayer — City Threat Criteria

| Feature | What it does | Limitation |
|---------|-------------|------------|
| **UpdateCityThreatCriteria()** | Scores each city's threat level for defense prioritization | Independent per-city, no inter-city dependency |
| **City triage** | Deprioritizes isolated cities in multi-front wars | Simple heuristic (nearby enemy/friendly count), not terrain-aware |

**Recent enhancements (commit `6d5f9df81`):**
- Capital +150 priority, original major capitals +75
- Core cities within 6/10 tiles get +40/+15
- Population-based bonuses (+5/+15/+30)
- Multi-front triage for expendable isolated cities (−50/−80)

### 2.4 Existing Strategic Concepts in Code

| Concept | Where | Status |
|---------|-------|--------|
| `CvPlot::IsChokePoint()` | CvPlot.h | Exists — used for encampment placement, improvement scoring |
| `GetStrategicValue()` | CvPlot | Exists — threshold triggers strategic road building |
| `CHOKEPOINT_STRATEGIC_VALUE` | CvGlobals (default 10) | Exists — global weight for chokepoint importance |
| `ConnectPointsForStrategy()` | CvBuilderTaskingAI | Exists — builds roads to high-strategic-value plots within 6 tiles |
| Defensive depth analysis | — | **Missing** |
| Terrain corridor mapping | — | **Missing** |
| City dependency graph | — | **Missing** |
| Salient/bulge detection | — | **Missing** |
| Per-front defensive line | — | **Missing** |
| Strategic road priority by threat | — | **Missing** |

---

## 3. Proposed System: CvStrategicGeographyMap

### 3.1 Design Philosophy

Create a **persistent strategic layer** that sits between the per-turn tactical analysis and the high-level military strategy. It answers the question: *"Given my territory's shape and terrain, how should I allocate defense?"*

**Key principle:** Compute expensive geography analysis infrequently (every 5-10 turns, or on city gain/loss), then consume the cached results every turn in existing decision points.

### 3.2 Core Data Structure

```cpp
// New file: CvStrategicGeographyMap.h

struct StrategicCityAnalysis
{
    // Identity
    int iCityID;
    PlayerTypes eOwner;
    
    // Strategic classification
    bool bIsCapital;
    bool bIsFrontLine;       // Adjacent to border with hostile/neutral territory
    bool bIsSecondLine;      // One city back from front line
    bool bIsCore;            // Interior city, not near any border
    bool bIsSalient;         // Front-line city that protrudes into enemy territory
    bool bIsDefensibleSalient; // Salient but surrounded by forest/jungle — hedgehog viable pre-Indirect Fire
    bool bIsChokepoint;      // Controls a narrow passage (mountains/water on flanks)
    bool bIsFloodgate;       // If lost, exposes 2+ other cities to direct attack
    bool bIsCoastalExposed;  // Coastal city vulnerable to amphibious assault
    
    // Defensive metrics
    int iDefensiveDepth;     // Min distance to nearest hostile border
    int iApproachCorridors;  // Number of distinct land approach vectors (1=chokepoint, 4+=exposed)
    int iTerrainDefenseScore; // Aggregate terrain defense bonus of surrounding tiles
    int iRiverBarriers;      // Number of river crossings enemies must make to approach
    int iMountainShielding;  // Mountain/impassable tiles blocking approach vectors
    
    // Dependency graph
    int iSupportsNumCities;  // How many other cities depend on this one for connectivity
    std::vector<int> vDependentCities;  // City IDs that lose connectivity if this falls
    std::vector<int> vDefensiveLineCities; // Other cities on the same defensive line
    
    // Infrastructure
    int iRoadPriority;       // Derived priority for road/rail building (higher = build first)
    bool bNeedsStrategicRoad; // Should builders prioritize connecting this city
    
    // Per-enemy analysis (populated during wartime)
    struct EnemyApproach
    {
        PlayerTypes eEnemy;
        int iDistanceFromEnemy;       // Tiles from nearest enemy city
        int iApproachDifficulty;      // Terrain cost of best path from nearest enemy city
        int iLikelyApproachDirection; // DirectionTypes of most probable attack vector
    };
    std::vector<EnemyApproach> vEnemyApproaches;
};

class CvStrategicGeographyMap
{
public:
    void Init(PlayerTypes ePlayer);
    void Update();  // Full recalculation (expensive, every 5-10 turns)
    void UpdateIncremental();  // Light update (after city gain/loss/border change)
    
    // Queries consumed by other systems
    const StrategicCityAnalysis* GetCityAnalysis(int iCityID) const;
    bool IsCitySalient(int iCityID) const;
    bool IsDefensibleSalient(int iCityID) const;  // Salient but hedgehog-viable (era-dependent)
    bool IsCityChokepoint(int iCityID) const;
    bool IsCityFloodgate(int iCityID) const;
    int GetDefensiveDepth(int iCityID) const;
    int GetApproachCorridors(int iCityID) const;
    int GetRoadPriority(int iCityID) const;
    
    // Defensive line queries
    std::vector<int> GetDefensiveLine(PlayerTypes eEnemy) const;
    std::vector<int> GetCriticalCities() const;  // Cities where loss cascades
    std::vector<int> GetExpendableCities() const; // Cities that can be sacrificed
    
    // Chokepoint queries
    std::vector<CvPlot*> GetChokepoints(PlayerTypes eEnemy) const;
    CvPlot* GetBestDefensivePosition(CvCity* pCity, PlayerTypes eEnemy) const;
    
private:
    PlayerTypes m_ePlayer;
    int m_iLastFullUpdate;
    std::map<int, StrategicCityAnalysis> m_cityAnalysis;
    
    // Internal computation
    void ClassifyAllCities();
    void ComputeDefensiveDepth();
    void DetectSalients();
    void DetectChokepoints();
    void BuildDependencyGraph();
    void ComputeApproachCorridors();
    void DeriveRoadPriorities();
};
```

### 3.3 Algorithm Sketches

#### A. Defensive Layer Classification

Classify each city into a layer based on distance to hostile/neutral borders:

```
For each owned city C:
  1. Compute min_border_dist = minimum distance to nearest tile owned by hostile/neutral player
  2. If min_border_dist <= 4:  → FRONT_LINE
  3. If min_border_dist <= 8:  → SECOND_LINE
  4. If min_border_dist <= 12: → REAR_AREA
  5. Else:                     → CORE
  
  Capital always gets CORE treatment for defense priority regardless of actual distance.
```

This directly feeds into `UpdateCityThreatCriteria()` to replace the current flat ±40/±15 bonuses with graduated, geography-aware values.

#### B. Salient Detection

A city is a salient if it protrudes into hostile territory relative to neighbors:

```
For each FRONT_LINE city C:
  1. Find the 2-3 nearest friendly cities
  2. Compute the "front line" as the line connecting those neighbors
  3. If C is significantly forward of that line (> 4 tiles ahead):
     → C is a SALIENT
  
  Simpler heuristic (Phase 1):
  - Count hostile-owned tiles within RING3 (37 plots)
  - Count friendly-owned tiles within RING3
  - If hostile/friendly ratio > 2.0 AND city is FRONT_LINE:
    → C is a SALIENT
```

Salients should get reduced defense priority (expendable) and increased evacuation urgency unless they are also chokepoints **or defensible salients** (see below).

#### B2. Defensible Salient Exception (era-aware)

Not all salients are expendable. A salient city on terrain that forces melee-range engagements is actually a strong "hedgehog" position:

```
A salient city is DEFENSIBLE if:
  1. ≥4 of 6 adjacent tiles have forest, jungle, or hills+forest/jungle
  2. This forces siege/ranged units to move adjacent before attacking
  3. A garrisoned melee unit can then strike adjacent siege units without
     leaving the city (garrison attack), killing the most dangerous threats

Era check — Indirect Fire degrades this advantage:
  - If ANY enemy at war has access to the Indirect Fire promotion
    (tech-dependent, typically Industrial era+), the defensible classification
    is DOWNGRADED because ranged/siege units can now fire over forest/jungle
    without moving adjacent
  - Pre-Indirect Fire: defensible salient gets +60 priority (treat like
    second-line city, worth holding)
  - Post-Indirect Fire: defensible bonus removed, reverts to normal salient
    (expendable unless also a chokepoint/floodgate)
```

**Settler AI implication:** When the AI must settle in a salient position (e.g., only available high-yield location), it should prefer plots with maximum adjacent forest/jungle coverage to create a natural hedgehog. This makes early/mid-game salient cities much more viable.

**Garrison doctrine for defensible salients:**
- Station a strong melee unit (ideally with Cover/Drill promotions) in the city
- The melee garrison attacks adjacent enemy siege units each turn
- Siege units adjacent to the city are vulnerable because they had to spend movement entering rough terrain
- This creates a "siege trap" where enemy siege units die before they can bombard effectively
- Works best with walls/castle adding city ranged attack on top of the melee garrison strike

#### C. Chokepoint Detection

A city controls a chokepoint if the terrain narrows around it:

```
For each city C at position (x, y):
  1. Scan a rectangular area ±6 tiles around the city
  2. For each cardinal direction (N, S, E, W):
     - Cast rays from the city center outward
     - Count passable land tiles in a 3-wide corridor
     - If a corridor has ≤ 2 passable tiles wide → that's a choke
  3. If ANY approach direction has a chokepoint:
     → C.bIsChokepoint = true
     → C.iApproachCorridors -= 1 for each blocked direction
  
  Enhanced: use CvPlot::IsChokePoint() on surrounding tiles and aggregate.
  A city with ≥3 adjacent IsChokePoint() tiles is itself a chokepoint city.
```

Chokepoint cities should get **massively** increased defense priority — losing a chokepoint city is catastrophic because it opens a wide front.

#### D. Floodgate / Dependency Detection

A city is a "floodgate" if losing it exposes multiple other cities:

```
For each city C:
  1. Temporarily "remove" C from the connectivity graph
  2. For each other city D that was connected through C's territory:
     - Does D now border hostile territory directly?
     - Is D's shortest path to the capital significantly longer?
  3. If removing C exposes 2+ cities:
     → C.bIsFloodgate = true
     → C.iSupportsNumCities = count of newly exposed cities
     → C.vDependentCities = list of those cities
```

Floodgate cities get the highest defense priority after the capital itself.

#### E. Approach Corridor Analysis

For each city and each enemy, analyze realistic attack vectors:

```
For each city C, for each enemy E at war or hostile:
  1. Find nearest enemy city EC
  2. Compute A* path from EC to C using terrain movement costs
  3. Along the path, identify terrain features:
     - River crossings (each +25% defense for defender)
     - Mountains flanking the path (natural walls)
     - Hills (defender advantage)
     - Forest/jungle (movement cost)
  4. Aggregate into iApproachDifficulty score
  5. Count distinct approach vectors (if 2+ paths of similar length exist)
  6. Record primary approach direction
```

This feeds into unit positioning — garrison units should face the primary approach direction.

#### F. Road Priority Derivation

```
For each city C:
  1. Start with base road priority = C.iSupportsNumCities * 10
  2. If C.bIsFloodgate: priority += 50
  3. If C.bIsChokepoint: priority += 40
  4. If C.bIsFrontLine: priority += 30
  5. If C.bIsSalient: priority -= 20 (don't invest in expendable cities)
  6. If no road/rail to capital: priority += 100
  7. Feed into CvBuilderTaskingAI::ConnectPointsForStrategy()
```

---

## 4. Integration Points

### 4.1 Where the Strategic Map Gets Consumed

| Consumer | Current Code | How Strategic Map Improves It |
|----------|-------------|-------------------------------|
| **UpdateCityThreatCriteria()** | `CvPlayer.cpp ~L4952` | Replace flat capital/core bonuses with `iDefensiveDepth`, `bIsFloodgate`, `bIsChokepoint` scores |
| **UpdateOperations()** | `CvMilitaryAI.cpp ~L3205` | Use `GetDefensiveLine(eEnemy)` to assign defense ops to actual front-line cities per enemy |
| **EvaluateTacticalRetreat()** | `CvMilitaryAI.cpp ~L5305` | Retreat toward chokepoints, not just capital. Abandon salients first |
| **PrioritizeZones()** | `CvTacticalAnalysisMap.cpp ~L965` | Weight zones by strategic classification (chokepoint zone 3x, defensible salient 1.5x, expendable salient 0.5x) |
| **DoUpdatePeaceTreatyWillingness()** | `CvDiplomacyAI.cpp ~L27828` | Consider which fronts threaten chokepoints/floodgates (more urgent) vs. salients (less) |
| **ConnectPointsForStrategy()** | `CvBuilderTaskingAI` | Use `GetRoadPriority()` instead of raw `GetStrategicValue()` threshold |
| **City production AI** | `CvCityStrategyAI` | Front-line/chokepoint cities should prioritize walls, garrison units |
| **Settler AI** | `CvEconomicAI` | Avoid settling in salient positions; prefer sites that create chokepoints |
| **FindSafestPlotInReach()** | `TacticalAIHelpers` | Prefer plots behind chokepoints, toward second-line cities |

### 4.2 Update Triggers

| Trigger | Action |
|---------|--------|
| First city founded (new game) | Initial computation — no cities exist at game start (only settlers), so skip until first city is planted |
| Save game loaded | Full computation if player has ≥1 city (rehydrate cache from scratch since we don't serialize the map) |
| Every 5 turns (peacetime) | Full recomputation |
| Every 3 turns (wartime) | Full recomputation |
| City founded/captured/razed | Incremental update (reclassify affected cities + neighbors) |
| War declared / peace signed | Full recomputation |
| Border expansion (culture) | Light incremental (defensive depth only) |

### 4.3 Ownership in Code

```
CvPlayer
  └── CvMilitaryAI
       └── CvStrategicGeographyMap*  m_pStrategyMap  (new member)
            ├── Update() called from CvMilitaryAI::DoTurn()
            └── Queries consumed by CvMilitaryAI, CvTacticalAI, CvDiplomacyAI, CvBuilderTaskingAI
```

Serialization: add to `CvMilitaryAI::Read()` / `Write()` since the data is cache-like and can be recomputed. Save only `m_iLastFullUpdate` to trigger recomputation on load.

---

## 5. Implementation Phases

### Phase 1: Defensive Layer Classification + Capital Protection (Easiest, Highest Impact)

**Goal:** Every city knows if it's front-line, second-line, rear, or core.

**Files to modify:**
- New: `CvStrategicGeographyMap.h`, `CvStrategicGeographyMap.cpp`
- Modified: `CvMilitaryAI.h/cpp` (ownership, init, update call)
- Modified: `CvPlayer.cpp` → `UpdateCityThreatCriteria()` (consume layer classification)
- Modified: `CvTacticalAnalysisMap.cpp` → `PrioritizeZones()` (consume layer info)

**Estimated scope:** ~300 lines new code, ~50 lines modified

**Acceptance criteria:**
- Cities correctly classified as FRONT_LINE / SECOND_LINE / REAR / CORE
- Capital zone and chokepoint zones get measurably higher defense priority than peripheral zones
- In the Greece scenario, Athens should always outprioritize the northeastern city

---

### Phase 2: Salient Detection + Defensible Salient Exception (Medium difficulty)

**Goal:** Identify cities that protrude into enemy territory, distinguish expendable salients from defensible hedgehog positions.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` (add `DetectSalients()` + defensible-salient terrain check + era-aware Indirect Fire degradation)
- Modified: `CvPlayer.cpp` → `UpdateCityThreatCriteria()` (salient penalty; defensible salient bonus pre-Indirect Fire)
- Modified: `CvMilitaryAI.cpp` → `EvaluateTacticalRetreat()` (abandon expendable salients first; hold defensible salients longer)
- Modified: `CvDiplomacyAI.cpp` → peace willingness (losing expendable salient = less alarming; losing defensible salient = moderate concern)
- Modified: `CvEconomicAI` or settler evaluation → prefer forest/jungle-surrounded plots when forced to settle in salient positions

**Estimated scope:** ~250 lines new, ~100 lines modified

**Acceptance criteria:**
- In the Greece scenario, northeastern exposed cities are flagged as salients
- A salient city surrounded by 4+ forest/jungle tiles is classified as DEFENSIBLE
- Defensible salient gets +60 priority boost pre-Indirect Fire era, reverts to expendable post-Indirect Fire
- Defense operations are not wasted on expendable salient cities when core territory is threatened
- AI is willing to trade expendable salient cities in peace deals but resists trading defensible ones
- Settler AI prefers forest/jungle plots for salient-position settlements

---

### Phase 3: Chokepoint City Detection (Medium difficulty, high strategic value)

**Goal:** Identify cities that control terrain chokes and make them high-priority defense targets.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` (add `DetectChokepoints()`)
- Modified: `CvTacticalAnalysisMap.cpp` → zones containing chokepoint cities get massive priority
- Modified: `CvMilitaryAI.cpp` → defense operations preferentially assigned to chokepoint cities
- Modified: `TacticalAIHelpers` → `FindSafestPlotInReach()` prefers plots behind chokepoints

**Estimated scope:** ~250 lines new, ~60 lines modified

**Acceptance criteria:**
- Mountain pass cities (e.g., Thermopylae-like positions) are correctly identified
- Narrow land bridge cities (e.g., Isthmus of Corinth) are correctly identified
- Chokepoint cities get 3x defense priority multiplier

---

### Phase 4: Floodgate/Dependency Detection (Higher difficulty, highest strategic value)

**Goal:** Know which cities, if lost, cascade into losing other cities.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` (add `BuildDependencyGraph()`)
- Modified: `CvPlayer.cpp` → threat criteria incorporate dependency count
- Modified: `CvDiplomacyAI.cpp` → peace urgency increases when floodgate cities are threatened

**Estimated scope:** ~350 lines new, ~60 lines modified

**Acceptance criteria:**
- Cities whose loss exposes 2+ other cities are flagged as floodgates
- Floodgate cities receive defense priority second only to the capital
- AI makes peace more readily when floodgate cities are about to fall

---

### Phase 5: Approach Corridor Analysis + Road Priority (Advanced)

**Goal:** Understand *how* enemies can attack each city, optimize road networks.

**Files to modify:**
- Modified: `CvStrategicGeographyMap.cpp` (add `ComputeApproachCorridors()`, `DeriveRoadPriorities()`)
- Modified: `CvBuilderTaskingAI.cpp` → consume road priority for strategic road building
- Modified: `CvMilitaryAI.cpp` → position garrison units facing primary approach direction

**Estimated scope:** ~400 lines new, ~100 lines modified

**Acceptance criteria:**
- Each enemy-city pair has a computed approach difficulty score
- Strategic roads are built prioritizing floodgate and chokepoint connections
- Road to capital is absolutely prioritized over roads to peripheral cities

---

### Phase 6: Full Integration + Tuning (Polish)

**Goal:** Wire everything together, tune weights, handle edge cases.

**Tasks:**
- Settler AI avoids founding salient cities
- City production AI builds walls in front-line/chokepoint cities earlier
- Diplomatic AI considers strategic geography in war evaluation
- Performance profiling — ensure Update() on large maps (180×94) runs < 50ms
- Serialization for smooth save/load
- Logging system (`StrategicGeographyLog.csv`) for debugging

---

## 6. Performance Considerations

| Operation | Complexity | Frequency | Budget |
|-----------|-----------|-----------|--------|
| Layer classification | O(cities × border_tiles) | Every 3-5 turns | < 5ms |
| Salient detection | O(cities × RING3) | Every 3-5 turns | < 10ms |
| Chokepoint detection | O(cities × scan_area) | Every 5-10 turns | < 15ms |
| Dependency graph | O(cities²) | Every 5-10 turns | < 10ms |
| Approach corridors | O(cities × enemies × pathfinding) | Wartime only | < 20ms |
| **Total per full update** | | | **< 60ms** |

For reference, Giant Earth is 180×94 = 16,920 tiles. A typical empire has 5-20 cities. The analysis is city-centric, not tile-centric, so it scales with empire size rather than map size.

**Optimization strategies:**
- Cache aggressively — geography changes slowly
- Interrupt-and-resume for pathfinding (spread across frames if needed)
- Skip computation for cities deep in core territory (defensive depth > 15 = skip)
- Use the existing `CvPlot::IsChokePoint()` bitflag instead of recomputing

---

## 7. Testing Strategy

Since there is no internal test harness, testing requires Civ5:

### Logging-Based Validation
Add `StrategicGeographyLog.csv` output per turn showing:
```
Turn, CityName, Layer, IsSalient, IsChokepoint, IsFloodgate, DefDepth, ApproachCorridors, RoadPriority
```

### Scenario Tests

| Test | Map | Setup | Expected Result |
|------|-----|-------|-----------------|
| **Greece 4-front war** | Giant Earth TSL | Greece vs Venice/Poland/Byzantium/Egypt | Athens = CORE/Floodgate, NE city = SALIENT, Thermopylae-area = CHOKEPOINT |
| **Isthmus defense** | Giant Earth TSL | Any civ with narrow land connection | Isthmus city = CHOKEPOINT with 1-2 approach corridors |
| **Peninsula civ** | Giant Earth TSL | Korea, Italy, India | Peninsula neck = CHOKEPOINT, tip cities = FRONT_LINE or SALIENT |
| **Island civ** | Giant Earth TSL | Japan, Britain | All cities = COASTAL_EXPOSED, no land chokepoints |
| **Deep interior** | Giant Earth TSL | Russia, China (large empire) | Capital = CORE, border cities = FRONT_LINE, interior = REAR |

### Behavioral Validation
- AI should no longer abandon capitals to defend peripheral cities
- AI should concentrate units at chokepoints rather than spreading thin
- AI should build strategic roads to floodgate cities first
- AI should be more willing to cede salient cities in peace deals

---

## 8. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| Performance on large maps | High | Cache + incremental updates + skip deep-core cities |
| Incorrect salient classification | Medium | Conservative threshold (ratio > 2.5 not 2.0), exclude capitals, era-aware defensible check |
| Over-prioritizing chokepoints | Medium | Cap at 3x multiplier, ensure capital always ranks highest |
| Dependency graph cycles | Low | DAG construction with cycle-breaking heuristic |
| Serialization compatibility | Medium | Version in save format, recompute on load if version mismatch |
| C++03/VC9 compatibility | High | Use only STL containers available in VC9 (vector, map, set, pair) |

---

## 9. Relationship to Previous Multi-Front War Fixes

The commit `6d5f9df81` established the **foundation** this system builds on:

| Previous Fix | What Strategic Geography Replaces/Enhances |
|-------------|---------------------------------------------|
| Capital +150 threat priority | → Replaced by `iDefensiveDepth` + `bIsFloodgate` scoring (more nuanced) |
| Core city ±40/±15 bonuses | → Replaced by layer classification (FRONT_LINE/SECOND_LINE/CORE with graduated values) |
| City triage (nearby enemy count) | → Replaced by proper salient detection + dependency analysis |
| Per-enemy front assignments | → Enhanced with `GetDefensiveLine(eEnemy)` providing actual front-line cities |
| EvaluateTacticalRetreat to capital | → Enhanced to retreat to nearest chokepoint, then capital |
| Zone priority capital 2x boost | → Replaced by strategic classification (chokepoint 3x, floodgate 2.5x, defensible salient 1.5x, expendable salient 0.5x) |

The strategic geography map does **not** replace the previous fixes — it provides a richer data source that the same decision points can consume. Phase 1 can coexist with the current heuristics; later phases gradually replace the simple bonuses with geography-derived values.

---

## 10. Open Questions

1. **Should the strategic map be per-player or global?** Per-player is more accurate (each civ has different enemies/borders) but more expensive. Recommendation: per-player, computed only for AI players.

2. **How to handle fog-of-war?** The geography analysis should use *revealed* terrain, not current vision, since terrain doesn't change. But enemy city locations should use last-known positions.

3. **Should this affect human players?** The map could be exposed to human players via an overlay in the advisor UI, but that's a separate feature. For now, AI-only.

4. **What about naval strategic geography?** Sea lanes, straits (e.g., Gibraltar, Bosphorus), and naval chokepoints are important but significantly more complex. Recommendation: defer to a separate naval strategic geography pass after land is working. **See Appendix C for detailed research and feasibility analysis.**

5. **Turn-budget when many AI players compute simultaneously?** On Giant Earth with 20+ AI civs, each computing strategic geography, total budget could reach 1-2 seconds per turn. May need to stagger (not all civs update the same turn).

---

## Appendix A: Key Source Files Reference

| File | Role | Lines (approx) |
|------|------|----------------|
| `CvTacticalAnalysisMap.h/cpp` | Zone dominance, posture, priority | 340 / 1,394 |
| `CvMilitaryAI.h/cpp` | Operations, defense state, attack targets | 450 / 5,534 |
| `CvPlayer.cpp` | City threat criteria, war damage | 49,533 |
| `CvDiplomacyAI.h/cpp` | Peace willingness, coalition detection | 2,469 / 59,835 |
| `CvTacticalAI.h/cpp` | Zone processing, unit assignment | ~800 / 19,760 |
| `CvBuilderTaskingAI.h/cpp` | Road building, strategic routing | ~200 / large |
| `CvPlot.h/cpp` | `IsChokePoint()`, `GetStrategicValue()` | large |
| `CvCityStrategyAI.h/cpp` | City production priorities | large |

## Appendix B: Existing IsChokePoint() Usage

`CvPlot::IsChokePoint()` is already computed and stored as a plot flag. Current consumers:
- **CvBuilderTaskingAI**: Encampment placement heavily weights chokepoints (`iChokepointsCovered * iCombatBonusValue * 3`)
- **CvCity**: Uses chokepoint info for improvement scoring
- **CvAStar**: Has `//todo: extra cost if this is a chokepoint` — chokepoint-aware pathfinding is planned but unimplemented

The strategic geography map should **aggregate** per-plot `IsChokePoint()` flags into city-level chokepoint classification rather than recomputing from scratch.

---

## Appendix C: Naval Strategic Geography — Research & Feasibility Analysis

> **Status:** Research complete, implementation deferred  
> **Date:** Feb 2026  
> **Prerequisite:** Land strategic geography (Phases 1-6) — COMPLETED  
> **Complexity estimate:** HIGH — significantly harder than land geography  

### C.1 Problem Statement

The land strategic geography system (Phases 1-6) handles terrain chokepoints, defensive layers, floodgate cities, and approach corridors for land warfare. But coastal civs face a fundamentally different threat model:

- **Amphibious assault**: Enemy fleets can bypass land chokepoints entirely by landing troops on an undefended coast
- **Naval blockade**: Cutting sea trade routes cripples cities that depend on coastal trade
- **Strait control**: Narrow water passages (Gibraltar, Bosphorus, Malacca) are the naval equivalents of mountain passes
- **Multi-ocean reachability**: Islands and peninsulas may face threats from multiple ocean basins independently
- **Coastal exposure asymmetry**: A city with 2 coastal tiles is less exposed than one with 5

**Motivating example:** Japan on Giant Earth TSL has all cities coastal. Land chokepoints are irrelevant. The AI needs to understand that control of the Sea of Japan and the Pacific approaches determines Japan's survival — not mountain passes.

### C.2 What Exists Today (Codebase Audit)

| Concept | Status | Location | Notes |
|---------|--------|----------|-------|
| **Water body identification** | Partial | `CvArea` (flood-fill), `CvLandmass` (groups areas) | Contiguous water of same passability = one Area. No "Atlantic vs Pacific" naming. |
| **Lake vs ocean** | Yes | `CvLandmass::isLake()` | `numTiles < MIN_WATER_SIZE_FOR_OCEAN (default 10)` |
| **Land bridge detection** | Yes | `CvPlot::IsWaterAreaSeparator()` | Detects land tiles adjacent to 2+ different water Areas. Used for canal site / settle scoring. |
| **Coastal city detection** | Yes | `CvCity::isCoastal(int minSize)` | Uses `CvPlot::isCoastalLand()` checking adjacent water Landmass size. |
| **Naval pathfinding** | Yes | `CvAStar` with `NavalUnitSimpleValid` | Domain checks, deep water gating, ice. Coastal cities + passable forts act as "canals" for naval transit. |
| **Naval trade paths** | Yes | `CvGameTrade` with `PT_TRADE_WATER` | Separate water path cache (`m_aPotentialTradePathsWater`). Trade paths implicitly define navigable sea lanes. |
| **Canal detection (prospective)** | Yes | `CvBuilderTaskingAI::WantCanalAtPlot()` | Uses `MOVEFLAG_PRETEND_CANALS` to find potential canal locations via trade pathfinding. |
| **Tactical water zones** | Partial | `CvTacticalAnalysisMap` | Water zones have negative IDs (= `-cityID`), separate strength counting (naval ranged/melee), posture selection. Water zones get 1/3 base priority of land zones. |
| **Naval defense state** | Yes | `CvMilitaryAI::UpdateDefenseState()` | `m_eNavalDefenseState` computed from naval unit count vs recommended, coastal siege status, enemy naval movement. |
| **Naval chokepoints** | **NO** | — | No concept of narrow water passages, sea lane control points, or naval strategic positions. |
| **Water area connectivity graph** | **NO** | — | Areas don't track which other water areas they connect to, or what land separates them. |
| **Amphibious landing zones** | **NO** | — | Per-unit embark/disembark only. No strategic-level "vulnerable coastline" concept. |
| **Sea lane identification** | **NO** | — | Trade paths are cached but not abstracted into "sea lanes" with strategic value. |
| **Coastal exposure scoring** | **NO** | — | No per-city count of how much coastline is exposed to open ocean vs sheltered water. |

### C.3 Proposed Naval Strategic Geography Concepts

#### C.3.1 Naval Chokepoint Detection (Narrow Water Passages)

The naval analog of a mountain pass. A water tile (or small set of tiles) where the navigable water narrows to ≤ 2-3 tiles wide, forcing all naval traffic through a bottleneck.

**Algorithm sketch:**
```
For each water tile W:
  1. Count adjacent water tiles (not lake, not ice) in a cross-section
     perpendicular to the "flow" direction
  2. If the narrowest cross-section ≤ 3 tiles AND water exists on both sides:
     → W is a NAVAL_CHOKEPOINT
  
  More robust approach:
  - For each pair of large water Areas separated by land:
    - Find the shortest water path connecting them (may go through coastal cities/canals)
    - Measure the minimum "width" along that path
    - If minimum width ≤ 3: the narrowest point is a NAVAL_CHOKEPOINT
```

**Difficulty:** MEDIUM-HIGH. The main challenge is defining "perpendicular to flow" on a hex grid. The cross-section measurement requires knowing the local direction of the waterway, which means computing it from the path between two water bodies.

**Simpler alternative:** For each water plot, scan all 6 hex directions. In each direction, count consecutive water tiles. If the plot has water extending far (>5 tiles) in exactly 2 opposite-ish directions but only 0-1 tiles in the perpendicular directions, it's a strait tile. This is the "narrow corridor" approach used in land chokepoint detection (Phase 3), adapted for water.

**Real-world examples on Giant Earth:**
- **Gibraltar**: 1-2 tile gap between Iberian Peninsula and North Africa
- **Bosphorus**: 1 tile between Europe and Anatolia (often a city site)
- **Strait of Malacca**: Narrow water between Malay Peninsula and Sumatra
- **English Channel**: 2-3 tiles wide at narrowest
- **Danish Straits**: Narrow passages between Denmark and Scandinavia
- **Suez (canal)**: Man-made connection, handled by `IsWaterAreaSeparator()` + canal building

#### C.3.2 Coastal Exposure Classification

Per-city assessment of how vulnerable each coastal city is to naval attack.

```
For each coastal city C:
  1. Count water tiles in RING1 (0-6) and RING2 (0-12)
  2. Classify:
     - 1-2 water tiles in RING1: SHELTERED_COAST (harbor access but limited exposure)
     - 3-4 water tiles in RING1: MODERATE_COAST (standard coastal city)
     - 5-6 water tiles in RING1: EXPOSED_COAST (peninsula tip, island)
  3. Check if those water tiles connect to open ocean or only to a lake/small sea
  4. Compute "landing zone" count: how many coastline tiles within RING2 are
     flat land adjacent to deep-enough water for troop landings
```

**Difficulty:** LOW. This is straightforward tile counting, similar to Phase 2 salient detection. The main complexity is distinguishing "connected to ocean" from "connected to lake."

#### C.3.3 Sea Lane Identification

Abstract paths that connect important coastal cities or ocean basins.

```
For each pair of coastal cities (own or allied):
  1. Compute water path between them (reuse trade path cache)
  2. If path exists and distance < threshold:
     → This is a SEA_LANE
  3. Identify tiles along the sea lane that are near naval chokepoints
  4. Assign strategic value to the sea lane based on:
     - Economic value (trade routes using this path)
     - Military value (reinforcement route between theaters)
     - Vulnerability (passes through enemy-controlled water)
```

**Difficulty:** MEDIUM. The trade path cache already computes these paths (`m_aPotentialTradePathsWater`). The challenge is abstracting individual paths into strategic sea lanes and measuring their vulnerability. Could reuse `CvGameTrade::UpdateTradePathCache()` output.

#### C.3.4 Water Area Connectivity Graph

A graph showing how water bodies connect to each other through straits, canals, and coastal city passages.

```
Nodes: each CvArea where isWater() == true
Edges: connections through:
  - Direct adjacency (tiles of two different water areas touch)
  - Coastal city passages (naval units can transit through coastal cities)
  - Canal improvements (passable forts connecting water areas)
  - Potential canal sites (for forward planning)
  
Edge weight: width of the connection (1 tile = chokepoint, 5+ tiles = open water)
```

**Difficulty:** MEDIUM-HIGH. Requires iterating all water area boundaries. The `IsWaterAreaSeparator()` function already identifies land tiles between different water areas — this could be the foundation. But building a full graph with weighted edges and transit nodes (cities/canals) is non-trivial.

#### C.3.5 Amphibious Threat Assessment

Per-city evaluation of vulnerability to amphibious landing.

```
For each coastal city C:
  1. Find all coast tiles within RING2 that are flat land adjacent to water
     (potential landing zones)
  2. For each enemy with naval capability:
     - Can their fleet reach our coastal waters? (water area connectivity)
     - Do they have embarked units or transports?
     - How many landing zones can they hit simultaneously?
  3. Compute amphibious_threat_score per-enemy per-city
  4. Cities with high scores need garrison units + coastal defenses
```

**Difficulty:** HIGH. Requires cross-referencing water area connectivity, enemy naval strength, embarkation capability, and landing zone geometry. Most complex individual feature.

### C.4 Integration Points

| Consumer | How Naval Geography Improves It |
|----------|-------------------------------|
| **`UpdateDefenseState()`** (`CvMilitaryAI`) | Factor in coastal exposure and naval chokepoint control when computing `m_eNavalDefenseState` |
| **`PrioritizeZones()`** (`CvTacticalAnalysisMap`) | Water zones near naval chokepoints get higher priority (currently water zones are 1/3 of land) |
| **`CheckBuildingBuildSanity()`** (`CvBuildingProductionAI`) | Exposed coastal cities prioritize harbor defenses, walls |
| **`PlotFoundValue()`** (`CvSiteEvaluationClasses`) | Penalize settlements on fully exposed coastline; bonus for sheltered harbors |
| **`ConnectPointsForStrategy()`** (`CvBuilderTaskingAI`) | Prioritize canal construction at plots that connect two large water areas |
| **`DoUpdateWarTargets()`** (`CvDiplomacyAI`) | Consider naval geography when evaluating island / peninsular targets |
| **Naval superiority operations** (`CvMilitaryAI`) | Deploy fleets to naval chokepoints, not just nearest threatened city |
| **Trade route security** (`CvEconomicAI`) | Factor in sea lane vulnerability when choosing trade routes |
| **City ranged strike priority** | Coastal cities near chokepoints should have ranged strike capability (walls, castle) |

### C.5 Proposed Implementation Phases

#### Naval Phase 1: Coastal Exposure Classification (LOW complexity, ~150 lines)

**Goal:** Every coastal city knows how exposed it is to naval attack.

- Count water tiles in RING1/RING2 for each city
- Classify as SHELTERED / MODERATE / EXPOSED
- Check if adjacent water is ocean vs lake
- Count potential landing zones (flat coastal tiles in RING2)
- Feed into city defense priority and building production AI

**Estimated effort:** 1-2 sessions. Reuses existing `isCoastalLand()`, `isWater()`, `isLake()` APIs.

#### Naval Phase 2: Naval Chokepoint Detection (MEDIUM complexity, ~300 lines)

**Goal:** Identify narrow water passages (straits) on the map.

- Adapt land chokepoint corridor-width scanning for water domains
- For each water tile, measure navigable width perpendicular to the waterway direction
- Tag narrow passages (width ≤ 3) as NAVAL_CHOKEPOINT
- Aggregate into per-city "controls naval chokepoint" flag for adjacent cities
- Boost tactical zone priority for water zones containing chokepoints

**Estimated effort:** 2-3 sessions. The corridor-width scan from Phase 3 land chokepoints is a template, but hex-grid perpendicular direction computation adds complexity.

**Key challenge:** Determining the local "flow direction" of a waterway. Options:
1. **Brute force:** For each water tile, test all 3 hex axis pairs. The axis where water extends farthest in both directions is the "flow" axis; the perpendicular is the "width."
2. **Path-based:** Find shortest water path between two large bodies; width is measured perpendicular to each path segment.
3. **Gradient-based:** Compute distance-to-nearest-land for each water tile; gradient direction is perpendicular to coastline; width is measured along gradient.

Recommend option 1 (brute force) for simplicity. It's O(water_tiles × 6_directions × scan_depth) — feasible.

#### Naval Phase 3: Water Area Connectivity Graph (MEDIUM-HIGH complexity, ~400 lines)

**Goal:** Know how ocean basins connect through straits and canals.

- Build adjacency graph of water Areas
- Edge weight = narrowest connection width
- Track transit points (coastal cities, canals, potential canal sites)
- Identify when an enemy controls a transit point (city on a strait)
- Enables "can our fleet reach their coast?" queries

**Estimated effort:** 3-4 sessions. Iterating area boundaries, building graph, handling canal transit. Requires careful handling of the Area/Landmass distinction.

#### Naval Phase 4: Sea Lane & Amphibious Threat (HIGH complexity, ~500 lines)

**Goal:** Abstract sea lanes between important cities; assess amphibious vulnerability.

- Extract strategic sea lanes from trade path cache
- Identify vulnerable segments (near enemy territory, through chokepoints)
- Per-city amphibious threat scoring
- Cross-reference with enemy fleet composition and embarkation tech

**Estimated effort:** 4-5 sessions. Heavy cross-system integration. Depends on Phases 1-3.

### C.6 Complexity Assessment Summary

| Naval Phase | Complexity | Lines (est.) | Sessions | Dependencies |
|-------------|-----------|-------------|----------|-------------|
| N1: Coastal Exposure | LOW | ~150 | 1-2 | None (standalone) |
| N2: Naval Chokepoints | MEDIUM | ~300 | 2-3 | N1 preferred but not required |
| N3: Water Connectivity | MEDIUM-HIGH | ~400 | 3-4 | N2 (chokepoint data feeds edges) |
| N4: Sea Lanes + Amphibious | HIGH | ~500 | 4-5 | N1 + N2 + N3 |
| **Total** | | **~1,350** | **~10-14** | |

**Comparison with land system:** Land Phases 1-6 totaled ~1,964 insertions across ~27 files. Naval would be ~1,350 lines but is **architecturally harder** because:

1. **No existing naval chokepoint concept** — land has `IsChokePoint()` to aggregate; water has nothing
2. **Water area topology is complex** — multiple disjoint areas per Landmass, canal transit, deep/shallow split
3. **Cross-domain interactions** — amphibious threats bridge land and sea; land chokepoints don't
4. **Performance concerns** — scanning all water tiles for chokepoint detection is O(water_tiles) not O(cities), and Giant Earth has ~8,000+ water tiles
5. **Harder to validate** — naval AI behavior is less visible than land (fleets hidden at sea, hard to observe chokepoint control)

### C.7 Key API Building Blocks

| API | File | What It Provides |
|-----|------|-----------------|
| `CvPlot::isWater()` | CvPlot.h | Basic water check |
| `CvPlot::isDeepWater()` / `isShallowWater()` | CvPlot.h | Ocean vs coast depth |
| `CvPlot::isCoastalLand(int)` | CvPlot.cpp | Is land adjacent to water of given min size |
| `CvPlot::IsWaterAreaSeparator()` | CvPlot.cpp | Land tile between two different water Areas — **key for strait adjacent land** |
| `CvPlot::isCoastalCityOrPassableImprovement()` | CvPlot.cpp | Can naval units transit through this land tile |
| `CvPlot::getArea()` | CvPlot.h | Water area ID for this tile |
| `CvPlot::getLandmass()` | CvPlot.h | Landmass ID (groups areas) |
| `CvArea::isWater()` / `getNumTiles()` | CvArea.h | Water body identification and size |
| `CvLandmass::isLake()` | CvMap.h | `isWater && numTiles < MIN_WATER_SIZE_FOR_OCEAN` |
| `CvMap::getAreaById(int)` | CvMap.h | Look up specific area |
| `CvCity::isCoastal(int)` | CvCity.h | City coastal check |
| `CvGameTrade::m_aPotentialTradePathsWater` | CvTradeClasses.h | Cached naval trade paths — implicit sea lanes |
| `TradePathWaterValid()` / `TradePathWaterCost()` | CvAStar.cpp | Naval trade pathfinding validity/cost |
| `NavalUnitSimpleValid()` | CvAStar.cpp | Naval unit movement validity |
| `CvUnit::needsEmbarkation()` / `CanEverEmbark()` | CvUnit.h | Embarkation checks |
| `iterateRingPlots()` / `RING1_PLOTS..RING3_PLOTS` | CvGameCoreUtils.h | Ring scanning (reuse for coastal exposure) |

### C.8 Recommendations

1. **Start with Naval Phase 1 (Coastal Exposure)** — it's standalone, low-risk, immediately useful. Even without full naval geography, classifying coastal cities by exposure level feeds into existing defense/production systems.

2. **Naval Phase 2 (Chokepoints) is the highest-value target** — straits and narrow passages are where naval strategic control is won or lost. But it requires solving the "perpendicular to waterway" problem on a hex grid.

3. **Naval Phase 3 (Connectivity) may not be needed initially** — the game already handles naval pathfinding correctly; the value is in *strategic awareness* (knowing the enemy controls a chokepoint), which Phase 2 provides.

4. **Naval Phase 4 should wait for testing** — sea lane vulnerability and amphibious threat are the most complex and least likely to produce visible AI improvement without extensive tuning.

5. **Performance mitigation:** Compute naval geography on map initialization and re-scan only when cities are founded/captured near water. Water terrain doesn't change — only control of transit points (cities/canals on straits) changes.

6. **Consider adding `IsNavalChokePoint()` to CvPlot** as a cached bitflag (like existing `IsChokePoint()`) computed once during `calculateAreas()`. This amortizes the cost and gives all systems free access.

### C.9 Open Questions (Naval-Specific) — Refined

1. **How to handle deep water gating?**
   - Before Astronomy, civs can only use coastal water, so ocean basins seem disconnected — but multiple exceptions exist: Polynesia UA ignores this entirely, explorers and caravels cross ocean before embarked units can, and tech timing varies per player.
   - **Resolution: Don't era-gate.** Compute naval geography for all non-lake water bodies unconditionally. The existing naval threat assessment already handles reachability on a per-enemy basis (it knows who has Astronomy). Adding era-awareness to the geography layer would duplicate that logic and add complexity for marginal gain.

2. **Ice dynamics?**
   - In Civ 5, ice is permanent — it cannot be cleared by any ability (unlike Civ 6). Only submarines can traverse ice tiles. Ice appears only in polar regions, far from any strategic city locations.
   - **Resolution: Safely ignore.** No implementation needed. If a future mod adds ice-clearing, revisit then.

3. **City canal transit — controllable chokepoints.**
   - **City canals** (coastal cities connecting two water areas) are strictly **owner-only** for military naval units. Allied naval units CANNOT transit through a friendly city canal — only trade units (cargo ships) can. This makes city canals the most restrictive form of naval transit.
   - **Passable forts/citadels** (`isCoastalCityOrPassableImprovement()`) on strait-adjacent plots allow **friend-accessible** transit (units with open borders can pass). This is actually *more permissive* than a city canal for allied movement.
   - **Loss asymmetry:** If a canal city falls, the conqueror gains naval transit (floodgate opens for them) while the original owner loses it. However, the owner may be able to **restore their own connectivity** by building a fort on a nearby plot between the same two water bodies (or using a Great General citadel / buying a plot for fort placement). This restores the owner's passage but does NOT deny the enemy their new transit through the captured city.
   - **Strategic calculus depends on context:**
     - If the canal city is the **only** transit point and no fort-able alternative exists → defend at all costs (irreplaceable floodgate).
     - If an alternative fort plot exists nearby → city loss is partially recoverable for own access, but enemy still gains a floodgate.
     - If the canal city is **deep in territory** → lower risk of loss, lower priority.
     - The real threat is often not losing own access (fort fallback) but **the enemy gaining transit** — this is what makes canal cities naval floodgates.
   - **Resolution: Tag as `CONTROLLABLE_NAVAL_CHOKEPOINT`** with higher strategic value than natural straits. Implementation should:
     - **Defense AI:** Prioritize garrison and defensive buildings for canal cities (they're naval floodgates). Check if alternative fort plots exist nearby — if none, escalate priority further.
     - **War targeting:** Capturing an enemy's canal city is disproportionately valuable — it breaks their naval network AND opens a new transit route for the attacker. Conversely, attacking into a well-defended canal city should be penalized (hard chokepoint with naval + land defenders).
     - **Fallback awareness:** If a canal city is lost, the AI should consider building a fort on a nearby strait-adjacent plot to restore own connectivity (separate from the priority of recapturing the city to deny enemy transit).
     - **Note:** Allied canal defense incentives are limited by the owner-only transit rule — allies can't use the canal anyway. The incentive is indirect: if the enemy gains naval access through the canal, it may threaten the ally's coastline too.

4. **Submarine/stealth considerations.**
   - Submarines bypass surface zone-of-control but cannot hold territory, capture cities, or operate deep in enemy waters without fleet support. A submarine slipping past a chokepoint is tactically useful but strategically insignificant — it can't project sustained power or deny the chokepoint to the enemy.
   - **Resolution: Don't degrade chokepoint value for submarines.** This is a niche tactical concern, not a strategic geography issue. The tactical AI already handles submarine pathfinding separately.

5. **Island civ special handling.**
   - Island civs (Japan, Britain, Polynesia, Indonesia, etc.) have fundamentally different strategic needs: ALL threats arrive by sea, coastal defense is paramount, embarked units are maximum-vulnerability targets, naval superiority is the primary strategic objective rather than a supporting arm, ranged naval units (frigates, battleships) become the core military rather than land siege, and coastal food/production tiles matter more than inland expansion.
   - **Resolution: Warrants a separate research document.** The strategic model for island civs is too divergent from the continental model to handle as a sub-feature. A dedicated doc should cover: coastal defense classification (exposed vs sheltered harbors), naval control zones (how many sea tiles a navy can deny), amphibious threat assessment, island economy priorities, and inter-island logistics. Appendix D or a standalone `ISLAND_CIV_STRATEGY.md`.
