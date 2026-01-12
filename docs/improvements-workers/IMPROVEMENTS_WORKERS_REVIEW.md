# Improvements & Workers Review

## Overview

This document reviews the tile improvement system, builder/worker units, build action execution, build times, and builder AI in Community Patch and Vox Populi for Civilization V. The system encompasses:

- **Tile Improvements:** Resource improvements (farms, mines, etc.), roads, infrastructure
- **Build Actions:** Worker/builder tasks and the decision system that prioritizes them
- **Build Times:** Time cost to complete improvements, adjusted by technology and game speed
- **Builder AI:** Economic AI that decides how many workers to maintain and what improvements to prioritize

---

## 1. Architecture & Core Systems

### 1.1 Builder AI (CvBuilderTaskingAI)

**File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.{h,cpp}` (~4,900 lines)

**Purpose:** Assigns improvement/route building tasks to worker units and scores candidate plots.

**Key Components:**

#### **BuilderDirective System**
Enumerates valid build actions:
- `BUILD_IMPROVEMENT_ON_RESOURCE` — unlock a resource tile
- `BUILD_IMPROVEMENT` — improve a tile (farm, mine, quarry, etc.)
- `BUILD_ROUTE` — build a road/railroad for connectivity
- `REPAIR_IMPROVEMENT` — repair a pillaged improvement
- `REPAIR_ROUTE` — repair a pillaged road
- `REMOVE_FEATURE` — chop forest/jungle for production
- `REMOVE_ROAD` — delete a road from a plot
- `KEEP_IMPROVEMENT` — planning marker (not executed)

Each directive tracks:
- Build type (`BuildTypes eBuild`)
- Target plot (X, Y coordinates)
- Score and penalty score (for ranking)
- Whether it's a Great Person improvement

#### **Main Methods**

| Method | Purpose |
|--------|---------|
| `Update()` | Called each turn; recomputes improvement and route directives |
| `UpdateImprovementPlots()` | Scores all eligible plots for tile improvements |
| `UpdateRoutePlots()` | Scores routes between cities and strategic locations |
| `GetDirectives()` | Returns prioritized list of build tasks |
| `GetTurnsToBuild()` | Calculates turns needed for a unit to complete a build on a plot |
| `ScorePlotBuild()` | Primary scoring function for improvement value-per-turn |
| `CanUnitPerformDirective()` | Checks tech/promotion requirements |

#### **Route Planning**
Routes are prioritized for:
1. **Main routes:** Capital to all other cities (trade/movement/city connections)
2. **Shortcuts:** Between frequently-needed city pairs
3. **Strategic routes:** To key resources or conflict zones
4. **Scenario routes:** Custom paths from scenario data

**File:** Lines 200-350 contain connection logic (`ConnectCitiesToCapital`, `ConnectCitiesForShortcuts`, etc.)

---

### 1.2 Economic AI (Worker Production)

**File:** `CvGameCoreDLL_Expansion2/CvEconomicAI.{h,cpp}` (lines ~1200-3050)

**Purpose:** Decide how many worker units an empire should maintain.

**Key Metrics:**

- `GetNumWorkers()` — current count of workers with AI_UNITAI_WORKER flag
- `GetImprovedToImprovablePlotsRatio()` — ratio of improved tiles to improvable tiles
- `GetWorkersToCitiesRatio()` — worker count divided by number of cities

**Ratios & Thresholds:**

| Ratio | Interpretation |
|-------|-----------------|
| 1.0+ improved-to-improvable | City's tiles well-improved |
| 0.7+ workers-to-cities | Healthy builder capacity (typical: 0.5-0.8) |
| <0.3 workers-to-cities | Severe deficit; trigger NEED_TILE_IMPROVERS |

**Worker Production Gating:** Controlled by three city strategies:

1. **AICITYSTRATEGY_NEED_TILE_IMPROVERS** (CvCityStrategyAI line ~2358)
   - Trigger: empire has too few workers or none at all
   - Effect: `+Weight` in city production decision, pushes worker to front of queue
   - Recovery: only after worker is produced or NO_WORKER_AFTER_DISBAND_DURATION expires

2. **AICITYSTRATEGY_WANT_TILE_IMPROVERS** (CvCityStrategyAI line ~2425)
   - Trigger: worker-to-city ratio is low but not desperate
   - Effect: moderate production bonus to workers
   - Used alongside other city focus

3. **AICITYSTRATEGY_ENOUGH_TILE_IMPROVERS** (CvCityStrategyAI line ~2464)
   - Trigger: workers-to-cities ≥ threshold (e.g., 1.5 per city)
   - Effect: blocks worker production entirely (returns `true`)
   - Exception: minor civs always need 1 worker per city

**Special Rules:**

- **Worker Disbanding:** If a worker is disbanded, `NO_WORKER_AFTER_DISBAND_DURATION` (default: 10 turns) prevents new worker production to avoid thrashing
- **War Penalty:** If the player is losing a war, worker production is de-prioritized
- **Minor Civs:** Different ratios (1:1 workers-to-cities); always maintain at least one worker

---

### 1.3 City Strategy Triggers

**File:** `CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp` (lines ~2355-2510)

#### **IsTestCityStrategy_NeedTileImprovers** (line ~2358)

```cpp
bool CityStrategyAIHelpers::IsTestCityStrategy_NeedTileImprovers(AICityStrategyTypes eStrategy, CvCity* pCity)
{
    int iCurrentNumCities = kPlayer.getCitiesNeedingTerrainImprovements();
    int iNumWorkers = kPlayer.GetNumUnitsWithUnitAI(UNITAI_WORKER, true);
    
    int iModdedNumWorkers = iNumWorkers * pCityStrategy->GetWeightThreshold() / 100;
    
    // Trigger if fewer workers than cities or zero workers
    if(iModdedNumWorkers <= iCurrentNumCities || iModdedNumWorkers == 0)
    {
        int iDesperateTurn = GD_INT_GET(AI_CITYSTRATEGY_NEED_TILE_IMPROVERS_DESPERATE_TURN); // 30
        if(GC.getGame().getGameTurn() >= iDesperateTurn)
            return true;
    }
    
    return false;
}
```

**Triggering Logic:**
- Count cities needing improvements (tiles with low improvement ratio)
- Compare to current worker count
- If `iNumWorkers * (threshold %) < iCurrentNumCities`, trigger NEED
- Minor civs: trigger if `iNumWorkers == 0` and turn > 50 (game speed scaled)

#### **IsTestCityStrategy_WantTileImprovers** (line ~2435)

```cpp
bool CityStrategyAIHelpers::IsTestCityStrategy_WantTileImprovers(AICityStrategyTypes eStrategy, CvCity* pCity)
{
    int iNumBuilders = kPlayer.GetNumUnitsWithUnitAI(UNITAI_WORKER, true);
    
    if(iNumBuilders <= 0)
        return true; // immediately want if zero
    
    if (pCity->getPopulation() >= GD_INT_GET(AI_CITYSTRATEGY_WANT_TILE_IMPROVERS_MINIMUM_SIZE)) // 4
    {
        int iCurrentNumCities = kPlayer.getCitiesNeedingTerrainImprovements();
        if (iNumBuilders < iCurrentNumCities * pCityStrategy->GetWeightThreshold()) // limit per city
            return true;
    }
    
    return false;
}
```

**Triggering Logic:**
- Immediately true if empire has zero workers
- For large cities (population ≥ 4): check if workers-to-cities ratio below threshold
- Prevents unnecessary worker production in tiny settlements

#### **IsTestCityStrategy_EnoughTileImprovers** (line ~2464)

```cpp
bool CityStrategyAIHelpers::IsTestCityStrategy_EnoughTileImprovers(AICityStrategyTypes eStrategy, CvCity* pCity)
{
    int iNumBuilders = kPlayer.GetNumUnitsWithUnitAI(UNITAI_WORKER, true);
    if (iNumBuilders <= 0)
        return false;
    
    int iNumCities = kPlayer.getCitiesNeedingTerrainImprovements();
    
    // (workers * 100) >= (threshold * cities)?
    return (iNumBuilders * 100) >= iPerCityThreshold * iNumCities;
}
```

**Triggering Logic:**
- Returns true if workers are ≥ threshold per city (typically 100-150% of cities)
- Worker production blocked entirely when true
- Does not prevent tech advancement or policy adoption

---

### 1.4 Build Time Calculation

**File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp` (line ~1277)

```cpp
int CvBuilderTaskingAI::GetTurnsToBuild(const CvUnit* pUnit, BuildTypes eBuild, const CvPlot* pPlot) const
{
    int iBuildRate = pUnit ? pUnit->workRate(false) : 1;
    
    // Get base build time from DB
    int iBuiltQuantity = GC.getBuildInfo(eBuild)->getTime();
    
    // Adjust by feature (removing forest/jungle takes longer)
    FeatureTypes eFeature = pPlot->getFeatureType();
    if(eFeature != NO_FEATURE)
    {
        iBuiltQuantity += GC.getFeatureInfo(eFeature)->getBuildTime();
    }
    
    // Adjust by terrain
    TerrainTypes eTerrain = pPlot->getTerrainType();
    if(eTerrain != NO_TERRAIN)
    {
        iBuiltQuantity += GC.getTerrainInfo(eTerrain)->getBuildTime();
    }
    
    // Apply tech modifier from Build_TechTimeChanges
    int iTerrainTime = iBuiltQuantity;
    int iModifier = getTechModifier(...) + getHandicapModifier(...);
    iBuiltQuantity = (iBuiltQuantity * (100 + iModifier)) / 100;
    
    // Calculate turns
    int iTurns = (iBuiltQuantity + iBuildRate - 1) / iBuildRate; // ceil division
    return std::max(1, iTurns);
}
```

**Components:**

| Factor | Source | Effect |
|--------|--------|--------|
| Base time | BuildInfo table | 1-20 turns for most improvements |
| Feature penalty | FeatureInfo (forest, jungle) | +400-500 turns for chopping |
| Terrain penalty | TerrainInfo (hills, snow) | +100-200 turns on rough terrain |
| Tech modifier | Build_TechTimeChanges | -20% to +50% based on tech era |
| Difficulty | Handicap modifiers | +0-50% on higher difficulties |
| Unit work rate | Unit workRate() | Divisor (1 = normal, 2 = double speed) |

**Formula:**
```
adjusted_time = base_time * (100 + tech_modifier + handicap_modifier) / 100
turns = ceil(adjusted_time / unit_work_rate)
```

---

### 1.5 Improvement Scoring (ScorePlotBuild)

**File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp` (line ~3900 onwards, ~400 lines)

**Purpose:** Assign numerical score to an improvement on a candidate plot.

**Scoring Factors:**

1. **Yield Improvement**
   - Current yields of plot → proposed yields with improvement
   - Food, production, gold, science, culture, faith
   - `GetPlotYieldValueSimplified()` estimates marginal benefit

2. **Strategic Value**
   - Resource unlock bonus (iron, oil, uranium = high priority)
   - Food for growing city (highest priority early game)
   - Production for military/buildings (high priority mid-game)
   - Gold for maintenance/trade (mid priority)

3. **Build Time Penalty**
   - Longer builds are scored lower (ROI factor)
   - `score / (turns + 1)` to incentivize quick builds

4. **City Specialization**
   - Science cities value science tiles more
   - Military cities value production tiles more
   - Religious cities value faith tiles more

5. **Worker Efficiency**
   - Prioritize tiles near existing workers (less travel time)
   - Deprioritize isolated plots (pathfinding cost)

6. **Feature Removal Gains**
   - Chopping a forest for production rush is scored high if city building something
   - Otherwise deferred until strategic timing

**Scoring Range:** -1000 to +10000 (higher = better)

**Observation:** Exact formula is complex and embedded; lacks documentation.

---

### 1.6 Pathfinding for Workers

**File:** `CvGameCoreDLL_Expansion2/CvHomelandAI.cpp` (pathfinding logic)

**Special Handling for Slow Units:**

- Workers are marked as "slow units" (base 2 movement per turn)
- Pathfinding optimized for slow units: ~10-20% faster
- Workers avoid dangerous terrain if possible (enemy units, barbarians)

**Known Limitation:** Pathfinding heuristics may underestimate cost for slow units, causing wider search radius. See [docs/unit-movement/IMPLEMENTATION_SUMMARY.md](../unit-movement/IMPLEMENTATION_SUMMARY.md) for details.

---

## 2. Database Configuration

### 2.1 Build Time Tuning

**Files:**

- `(1) Community Patch/Database Changes/WorldMap/Improvements/CoreBuildSweeps.sql`
- `(2) Vox Populi/Database Changes/WorldMap/Improvements/BuildSweeps.sql`

**Key Table: Builds**

| Column | Example | Notes |
|--------|---------|-------|
| Type | IMPROVEMENT_FARM, IMPROVEMENT_MINE | Unique build identifier |
| Time | 10, 15, 20 | Base turns (adjusted by era tech) |
| ImprovementType | IMPROVEMENT_FARM | Result of build action |

**Example Data (VP):**
```sql
INSERT INTO Builds (Type, Time, ImprovementType, Description) VALUES
    ('BUILD_FARM', 10, 'IMPROVEMENT_FARM', 'Farm'),
    ('BUILD_MINE', 15, 'IMPROVEMENT_MINE', 'Mine'),
    ('BUILD_PLANTATION', 12, 'IMPROVEMENT_PLANTATION', 'Plantation'),
    ('BUILD_LUMBERMILL', 15, 'IMPROVEMENT_LUMBERMILL', 'Lumber Mill'),
    ...
    ('REMOVE_FOREST', 500, 'IMPROVEMENT_NONE', 'Remove Forest'),
    ('REMOVE_JUNGLE', 500, 'IMPROVEMENT_NONE', 'Remove Jungle'),
    ('REMOVE_MARSH', 500, 'IMPROVEMENT_NONE', 'Remove Marsh');
```

**Feature Build Times (VP):**

| Feature | Build Type | Base Time |
|---------|------------|-----------|
| Forest | REMOVE_FOREST | 400 turns |
| Jungle | REMOVE_JUNGLE | 500 turns |
| Marsh | REMOVE_MARSH | 500 turns |

### 2.2 Build Time Modifiers by Technology

**Table: Build_TechTimeChanges**

Adjusts build times based on technology era.

**Example (VP):**
```sql
INSERT INTO Build_TechTimeChanges (BuildType, TechType, TimeChange) VALUES
    ('BUILD_FARM', 'TECH_AGRICULTURE', -20),  -- 20% faster with Agriculture
    ('BUILD_MINE', 'TECH_MINING', -25),        -- 25% faster with Mining
    ('BUILD_ROAD', 'TECH_WRITING', -10),
    ('BUILD_RAILROAD', 'TECH_RAILROAD', -50),  -- 50% faster with Railroads
    ...;
```

**Application:** Base time is scaled: `adjusted_time = base_time * (100 + sum_of_tech_changes) / 100`

### 2.3 Civilization-Specific Modifiers

**File:** `(2) Vox Populi/Database Changes/Civilizations/Huns.sql` (line ~81)

Example:
```sql
-- Huns get 25% faster cavalry training but 50% slower improvements
INSERT INTO Build_TechTimeChanges (BuildType, TechType, TimeChange) VALUES
    ('BUILD_FARM', 'TECH_AGRICULTURE', 50),   -- 50% slower (penalty)
    ...;
```

**Other Civs with Modifiers:**
- Traits defining `GetCityAutomatonWorkersChange()` provide free workers
- Traits with `GetWorkerSpeedModifier()` boost builder work rate (e.g., Sparta)

---

## 3. Known Issues & Limitations

### 3.1 Open TODOs in CvBuilderTaskingAI.cpp

#### **Issue 1: Net Gold Not Factored** (Line ~2068)

```cpp
// TODO include net gold in all gold yield/maintenance computations
```

**Problem:** Builder scoring uses gross gold yield, ignoring maintenance costs.

**Impact:** 
- Builder may prioritize a tile that yields +3 GPT but costs +4 GPT maintenance (net -1)
- Gold-focused improvements may appear attractive but drain treasury over time

**Recommendation:** Modify `ScorePlotBuild()` to subtract maintenance costs before scoring.

---

#### **Issue 2: Tech Distance Heuristic** (Line ~2167)

```cpp
// A bit hacky, use tech position in tech tree to decide how far away it is
```

**Problem:** Tech tree distance is used to predict when tech unlock occurs, but position in tree is not a reliable predictor of actual research timeline.

**Impact:** 
- Build times for late-game improvements (railroads, airports) may be estimated incorrectly
- Route building priority may change unexpectedly when techs are researched early/late

**Recommendation:** Use actual expected research turn from EconomicAI, not tree position.

---

#### **Issue 3: Adjacency & Feature Interactions** (Line ~3468)

```cpp
// TODO how to handle yield per X worked plots with feature Y...
```

**Problem:** Scoring doesn't account for feature removal affecting adjacent tile adjacency bonuses.

**Impact:**
- Example: Chopping a forest between two ≤2 tiles may break an adjacency bonus
- Builder may not recognize this and remove the forest prematurely

**Recommendation:** Pre-compute adjacency impact of feature removal before scoring.

---

### 3.2 Route Planning Heuristics

**Issue:** Route planning logic (lines ~200-350) uses hard-coded heuristics without clear documentation.

**Examples:**
- When is a route considered "main" vs. "shortcut"? (Lines ~180-210)
- How are shortcut city pairs selected? (Lines ~250-280)
- What distance threshold triggers strategic route planning? (Line ~310)

**Impact:** Routes may be over/under-built depending on map topology and city density.

**Recommendation:** Document heuristics and consider data-driven approach (distance thresholds in DB).

---

### 3.3 Yield Per X Worked Plots

**Issue:** Adjacency modifiers (e.g., "yield +1 for every X adjacent tiles") are not factored into improvement scoring.

**Impact:**
- Agricultural improvements adjacent to farms don't get +1 food per farm scoring
- This undervalues clustered improvements vs. scattered ones

**Recommendation:** Pre-compute cluster bonuses before scoring.

---

### 3.4 Worker Production Gating

**Issue:** The three city strategies (NEED, WANT, ENOUGH) are not clearly explained.

**Gaps:**
- What is the `GetWeightThreshold()` value for each strategy?
- How are city strategies weighted against each other?
- Does ENOUGH_TILE_IMPROVERS fully block production or just reduce weight?

**Observation:** Code shows ENOUGH returns `true`, which blocks production entirely. This is strong signal but may cause thrashing if worker dies.

---

### 3.5 Great Person Builder Improvements

**Issue:** BuilderDirective has `m_bIsGreatPerson` flag, but scoring differs for great person improvements (e.g., unique ones like Nazca Lines).

**Observation:** Flag exists in code but special scoring logic is not visible in docstring or obvious in `ScorePlotBuild()`.

**Recommendation:** Clarify how unique/great person improvements are weighted vs. regular workers.

---

## 4. Builder AI Decision Flow

### 4.1 Turn-by-Turn Execution

```
Turn N:
  1. CvPlayer::doTurn() → CvBuilderTaskingAI::Update()
       a. UpdateRoutePlots() — score all city connection routes
       b. UpdateImprovementPlots() — score all unimproved tiles
       c. Combine scores, sort by priority
       
  2. Worker units call GetAssignedDirective()
       a. Match unit to highest-priority directive in queue
       b. If unit already has directive, keep it (don't thrash)
       c. Return new directive if old one complete
       
  3. ExecuteWorkerMove()
       a. Move unit toward target plot
       b. If reached, execute build action (GC.getBuildInfo(eBuild)->getTime())
       c. Apply work rate: progress += unit->workRate()
```

### 4.2 Scoring Priority (Simplified)

```
Priority 1: Resource unlocks (farm on grassland with wheat nearby)
Priority 2: Growth improvements (farms, especially for food-weak cities)
Priority 3: Production improvements (mines, quarries, lumbermills)
Priority 4: Infrastructure (roads to capital, trade routes)
Priority 5: Culture/Science improvements (museums, universities only if secondary focus)
Priority 6: Feature removal (chop for production if city rushing)
Priority 7: Route repair (only if pillaged)
```

**Note:** Exact priority weights are not documented in code; inferred from scoring function.

---

## 5. UI Integration

### 5.1 Build Progress Display

**File:** `UI_bc1/UnitPanel/UnitPanel.lua` (lines ~394-404)

**Function:** `getUnitBuildProgressData()`

```lua
function getUnitBuildProgressData()
    local buildProgress = pUnit:GetBuildProgress()
    local buildTime = pUnit:GetBuildTime()
    local turnsLeft = pUnit:GetBuildTurnsLeft()
    
    -- Display to UI
    ...
end
```

**Shows to Player:**
- Current build item (e.g., "Building Farm")
- Progress bar (X / buildTime)
- Turns remaining

### 5.2 Improvement Yields

**File:** `UI_bc1/Improvements/YieldIconManager.lua` (line ~44)

**Function:** `BuildAnchorYields()`

Displays improvement yield icons on tile when selected.

---

## 6. Recommendations for Improvement

### **Tier 1: Critical Clarity**

1. **Document CvBuilderTaskingAI.cpp** — Add code comments explaining:
   - Scoring formula and weighting constants
   - Route planning heuristics and distance thresholds
   - Decision tree for directive priority

2. **Fix Net Gold Issue** (Issue 3.1)
   - Modify scoring to use `gold_yield - maintenance_cost`
   - Test that gold improvements no longer appear attractive if net negative

3. **Create Data-Driven Route Heuristics** (Issue 3.2)
   - Move distance thresholds to database constants
   - Allow modding of route planning priority

### **Tier 2: Medium Priority**

4. **Fix Tech Distance Heuristic** (Issue 3.2)
   - Replace tree position with EconomicAI research timeline estimate
   - Reduce build time variance for late-game improvements

5. **Adjacency & Feature Interaction** (Issue 3.3)
   - Pre-compute cluster bonuses in `UpdateImprovementPlots()`
   - Score feature removal considering adjacency impact

6. **Document City Strategies**
   - Add constants to GameDefines for NEED/WANT/ENOUGH thresholds
   - Explain weight modifiers in CvCityStrategyAI

### **Tier 3: Nice-to-Have**

7. **Yield Per X Worked Plots**
   - Factor adjacency modifiers into scoring logic
   - Test on maps with cluster-focused improvements (e.g., farm adjacency bonuses)

8. **Unique Improvement Handling**
   - Clarify scoring for Great Person improvements
   - Add special weighting for civilization-unique improvements

9. **Performance Optimization**
   - Pathfinding for workers could be further optimized
   - Consider caching plot scores across turns if static

---

## 7. References

### **C++ Core Files**

- [CvBuilderTaskingAI.h](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.h) — Header & API
- [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp) — Implementation (~4,900 lines)
- [CvCityStrategyAI.cpp](../../CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp) (lines ~2355-2510) — Strategy triggers
- [CvEconomicAI.cpp](../../CvGameCoreDLL_Expansion2/CvEconomicAI.cpp) (lines ~1200-3050) — Worker production decisions
- [CvUnitProductionAI.cpp](../../CvGameCoreDLL_Expansion2/CvUnitProductionAI.cpp) (lines ~1258-1277) — Worker production gating

### **Database Files**

- `(1) Community Patch/Database Changes/WorldMap/Improvements/CoreBuildSweeps.sql` — CP build times
- `(2) Vox Populi/Database Changes/WorldMap/Improvements/BuildSweeps.sql` — VP build times & feature removal times
- `(2) Vox Populi/Database Changes/Civilizations/Huns.sql` — Civ-specific modifiers

### **Lua/UI Files**

- [UI_bc1/UnitPanel/UnitPanel.lua](../../UI_bc1/UnitPanel/UnitPanel.lua) (lines ~394-404) — Build progress display
- [UI_bc1/Improvements/YieldIconManager.lua](../../UI_bc1/Improvements/YieldIconManager.lua) (line ~44) — Improvement yields

### **Related Reviews**

- [Pathfinding Documentation](../unit-movement/UNIT_MOVEMENT_PATHFINDING.md) — Worker movement optimization
- [City Management Review](../city-management-review.md) — Production queuing & automation
- [Resources Review](../resources/RESOURCES_REVIEW.md) — Resource improvement integration
- [Economy Reviews](../economy/) — Food production, trade, maintenance

---

## 8. Testing & Validation Checklist

- [ ] Verify net gold calculation: place improvement with negative net gold, confirm builder avoids it
- [ ] Test route priority: place cities far from capital, confirm roads are built in expected order
- [ ] Validate feature removal: ensure forest chopping only prioritized when city rushing production
- [ ] Check worker production gating: trace transitions through NEED → WANT → ENOUGH strategies
- [ ] Test build time: verify formula matches displayed turns with various techs/difficulties
- [ ] Confirm unique improvements: check that civilization-unique improvements are scored correctly
- [ ] Adjacency bonus test: place two farms adjacent, confirm second farm is scored higher

---

**Document Status:** Initial review complete. See [Tier 1](#tier-1-critical-clarity) recommendations for next steps.

