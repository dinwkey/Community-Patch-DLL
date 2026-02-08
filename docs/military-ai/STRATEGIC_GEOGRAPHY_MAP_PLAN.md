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

4. **What about naval strategic geography?** Sea lanes, straits (e.g., Gibraltar, Bosphorus), and naval chokepoints are important but significantly more complex. Recommendation: defer to a separate naval strategic geography pass after land is working.

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
