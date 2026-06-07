# Improvements & Workers: Issues & Fixes

## Quick Reference

| Issue | Severity | Category | File | Line | Status |
|-------|----------|----------|------|------|--------|
| Net gold not factored in scoring | HIGH | Scoring | CvBuilderTaskingAI.cpp | ~2068 | OPEN |
| Tech distance heuristic is ad-hoc | MEDIUM | Route Planning | CvBuilderTaskingAI.cpp | ~2167 | OPEN |
| Adjacency/feature interactions | MEDIUM | Scoring | CvBuilderTaskingAI.cpp | ~3468 | OPEN |
| Route planning heuristics undocumented | MEDIUM | Documentation | CvBuilderTaskingAI.cpp | ~200-350 | OPEN |
| City strategy thresholds not clear | LOW | Documentation | CvCityStrategyAI.cpp | ~2358-2510 | OPEN |
| Pathfinding cost underestimate | LOW | Pathfinding | CvHomelandAI.cpp | Various | DOCUMENTED |
| Worker repair exploit possible | LOW | Exploit | CustomMods.h | ~93 | ADDRESSED |

---

## Issue Descriptions & Proposed Fixes

### Issue 1: Net Gold Not Included in Improvement Scoring

**Severity:** HIGH  
**Category:** Scoring Logic  
**File:** [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp#L2068)

#### **Problem**

Builder scoring uses gross gold yield from improvements without subtracting maintenance costs. This can cause builders to prioritize improvements that are net negative (yield +3 GPT but cost +4 GPT maintenance).

```cpp
// Current implementation (line ~2068)
// TODO include net gold in all gold yield/maintenance computations

// Example: Trade Post yields +3 GPT but costs +2 GPT maintenance
// Scored as +3 (incorrect) instead of +1 (correct)
```

#### **Impact**

- Empire treasury can drain over time as builders improve "profitable-looking" tiles
- Gold-focused cities may make suboptimal improvement choices
- Late-game empires with many improvements may face unexpected gold penalties

#### **Root Cause**

`ScorePlotBuild()` loops through yields (food, production, gold, etc.) and scores each independently. Maintenance costs are handled separately by the city's financial system, not by builder AI.

#### **Proposed Fix**

Modify `ScorePlotBuild()` to deduct maintenance from gold yield before scoring:

```cpp
int iGoldScore = iProposedGold; // proposed gold yield
CvImprovementEntry* pkImprovement = GC.getImprovementInfo(eImprovement);
if (pkImprovement)
{
    int iMaintenanceCost = pkImprovement->GetMaintenanceNeededAmount(); // or similar
    iGoldScore -= iMaintenanceCost;
}

// Score gold considering maintenance
if (iGoldScore < 0)
{
    // Don't score negative gold improvements unless strategic value is high
    iGoldScore = iGoldScore * 50 / 100; // half weight for negative
}
```

#### **Testing**

1. Create improvement with +3 GPT yield, +5 GPT maintenance
2. Place builder in city
3. Verify builder does NOT prioritize this improvement
4. Confirm builder chooses +2 food farm instead

---

### Issue 2: Tech Distance Heuristic is Ad-Hoc

**Severity:** MEDIUM  
**Category:** Route Planning  
**File:** [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp#L2167)

#### **Problem**

Route planning uses position in tech tree to estimate when a tech unlocks, but position is not a reliable predictor of actual research timeline.

```cpp
// Current code (line ~2167)
// A bit hacky, use tech position in tech tree to decide how far away it is

// Example: TECH_RAILROAD comes late in tree but may be researched early if player rushes it
// Example: TECH_WRITING comes early in tree but may be delayed if player prioritizes military
```

#### **Impact**

- Routes to resources unlocked by late techs may be built too early (wasted effort)
- Routes to resources unlocked by early-coming techs may be delayed (missed opportunity)
- Build time estimates for improvements unlock by these techs will be wildly off

#### **Root Cause**

`GetRouteBuildTime()` uses `GC.getTechInfo(eTech).getResearchCost()` divided by current science rate, but this doesn't account for:
- Player's current tech choices
- Science generation rate changes
- Techs already researched

#### **Proposed Fix**

Replace tree position with EconomicAI's expected research timeline:

```cpp
// In CvBuilderTaskingAI::GetRouteBuildTime()
CvEconomicAI* pEconAI = m_pPlayer->GetEconomicAI();
int iExpectedTechTurn = pEconAI->GetEstimatedTechResearchTurn(eTech);
int iCurrentTurn = GC.getGame().getGameTurn();

int iRouteBuildDelay = iExpectedTechTurn - iCurrentTurn;
if (iRouteBuildDelay <= 0)
    iRouteBuildDelay = 1; // already researched or very soon
else if (iRouteBuildDelay > 100)
    iRouteBuildDelay = 100; // cap at 100 turns for very distant techs
```

#### **Testing**

1. Create scenario with TECH_RAILROAD available early (via starting techs)
2. Verify route planning accounts for immediate railroad availability
3. Create scenario with TECH_RAILROAD delayed (player focuses military)
4. Verify route planning defers railroad route building

---

### Issue 3: Adjacency & Feature Interactions Not Considered

**Severity:** MEDIUM  
**Category:** Scoring Logic  
**File:** [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp#L3468)

#### **Problem**

Scoring doesn't account for how feature removal affects adjacency bonuses on nearby tiles.

```cpp
// Current code (line ~3468)
// TODO how to handle yield per X worked plots with feature Y...

// Example: Forest between two adjacent tiles with farms
// If forest is removed:
//   - Plot gains improved yields (good)
//   - But adjacent tile may lose adjacency bonus (bad if bonus > yield gain)
```

#### **Impact**

- Feature removal may break valuable adjacency bonuses
- Score can be artificially inflated if adjacency penalty not subtracted
- Builder may remove forests/jungles prematurely, breaking farm clusters

#### **Root Cause**

`ScorePlotBuild()` evaluates plot independently. Adjacency bonuses from other plots are not iterated.

#### **Proposed Fix**

Pre-compute adjacency impact before scoring:

```cpp
int CvBuilderTaskingAI::ScorePlotBuild(CvPlot* pPlot, ImprovementTypes eImprovement, ...)
{
    int iScore = 0;
    
    // Base yield score
    iScore += GetPlotYieldValue(pPlot, eImprovement);
    
    // Adjacency bonus for THIS plot
    iScore += GetAdjacencyBonus(pPlot, eImprovement);
    
    // Adjacency penalty to NEIGHBOR plots if feature is removed
    if (pPlot->getFeatureType() != NO_FEATURE)
    {
        for (int i = 0; i < NUM_DIRECTION_TYPES; ++i)
        {
            CvPlot* pNeighbor = plotDirection(pPlot->getX(), pPlot->getY(), (DirectionTypes)i);
            if (pNeighbor && pNeighbor->getOwner() == m_pPlayer->GetID())
            {
                int iLostBonus = GetAdjacencyBonus(pNeighbor, NO_FEATURE); // if this plot's feature goes
                iScore -= iLostBonus; // apply penalty
            }
        }
    }
    
    return iScore;
}
```

#### **Testing**

1. Create map with farm adjacency bonus (+1 food for each adjacent farm)
2. Place two farms adjacent to a forest
3. Score removing forest vs. placing a third farm adjacent
4. Verify forest removal score is lower due to adjacency penalty

---

### Issue 4: Route Planning Heuristics Undocumented

**Severity:** MEDIUM  
**Category:** Documentation  
**File:** [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp#L200-L350)

#### **Problem**

Route planning uses hard-coded decision logic without clear explanation.

**Unanswered Questions:**
- When is a route classified as "main" (capital to all cities) vs. "shortcut" (city pair optimization)?
- What distance threshold triggers strategic route planning?
- How are shortcut city pairs selected? (Closest? Most trade? Most military?)
- How often are route plans recalculated?

#### **Impact**

- Routes may be over/under-built depending on map topology
- Modders cannot understand or override route priorities
- Hard to debug route building decisions

#### **Root Cause**

Route planning logic is in-code heuristics, not data-driven. Thresholds are magic numbers.

#### **Proposed Fix**

1. **Add GameDefines constants** (in CvDefines.h):
   ```cpp
   #define ROUTE_MAIN_PRIORITY           100
   #define ROUTE_SHORTCUT_PRIORITY        50
   #define ROUTE_STRATEGIC_PRIORITY       75
   #define ROUTE_CITY_DISTANCE_SHORTCUT   20  // tiles
   #define ROUTE_RECALC_FREQUENCY         10  // turns
   ```

2. **Document decision tree** in CvBuilderTaskingAI.cpp header:
   ```cpp
   /*
    * Route Priority System:
    * 1. Main routes (capital to ALL cities) — high priority, always build
    * 2. Shortcut routes (city pairs within ROUTE_CITY_DISTANCE_SHORTCUT) — medium priority
    * 3. Strategic routes (to key resources, e.g., Iron, Oil) — lower priority
    * 4. Trade routes (high-gold city pairs) — if trade exceeds threshold
    */
   ```

3. **Move thresholds to SQL** (for modding):
   ```sql
   INSERT INTO GlobalParameters (ParameterName, ParameterValue) VALUES
       ('ROUTE_CITY_DISTANCE_SHORTCUT', 20),
       ('ROUTE_RECALC_FREQUENCY', 10),
       ...;
   ```

#### **Testing**

1. Read documentation and verify logic matches code
2. Change GlobalParameters and confirm route priorities shift
3. Test on large map: verify no redundant routes

---

### Issue 5: City Strategy Thresholds Not Clear

**Severity:** LOW  
**Category:** Documentation  
**File:** [CvCityStrategyAI.cpp](../../CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp#L2358-L2510)

#### **Problem**

City strategies `NEED_TILE_IMPROVERS`, `WANT_TILE_IMPROVERS`, `ENOUGH_TILE_IMPROVERS` use thresholds that are not documented.

```cpp
// Unclear threshold values
int iPerCityThreshold = pCityStrategy->GetWeightThreshold() + iWeightThresholdModifier; // 100?

// Unclear transition conditions
if(iModdedNumWorkers <= iCurrentNumCities || iModdedNumWorkers == 0)
    return true; // NEED triggered when what exactly?
```

#### **Impact**

- Hard to balance worker production across difficulty settings
- Modders cannot modify worker ratios without recompiling C++
- Difficult to explain to players why workers are/aren't being trained

#### **Root Cause**

Thresholds are in C++ code, not in game database. StrategyEntry weight threshold is pulled from XML but actual decision logic is code-based.

#### **Proposed Fix**

1. **Document thresholds in code comments:**
   ```cpp
   // AICITYSTRATEGY_NEED_TILE_IMPROVERS
   // Triggered when: iNumWorkers * 100 < iCurrentNumCities * 67 (67% threshold)
   // Effect: Block all non-worker production, force worker to front of queue
   // Recovery: Blocked for NO_WORKER_AFTER_DISBAND_DURATION turns after worker disbanded
   ```

2. **Add to AIDefines.sql** (game database):
   ```sql
   INSERT INTO AICityStrategies (Type, WeightThreshold) VALUES
       ('AICITYSTRATEGY_NEED_TILE_IMPROVERS', 67),     -- 67% workers per city
       ('AICITYSTRATEGY_WANT_TILE_IMPROVERS', 100),    -- 100% workers per city
       ('AICITYSTRATEGY_ENOUGH_TILE_IMPROVERS', 150);  -- 150% workers per city
   ```

#### **Testing**

1. Document each threshold
2. Compare to gameplay: verify workers are trained in expected quantities
3. Test on different difficulty settings

---

### Issue 6: Pathfinding Cost Underestimate for Slow Units

**Severity:** LOW  
**Category:** Pathfinding  
**File:** [CvHomelandAI.cpp](../../CvGameCoreDLL_Expansion2/CvHomelandAI.cpp) (various lines)

#### **Problem**

Pathfinding heuristic for slow units (workers, embarked units, ships) underestimates cost, causing wider search radius.

**Details:** See [docs/unit-movement/IMPLEMENTATION_SUMMARY.md](../../docs/unit-movement/IMPLEMENTATION_SUMMARY.md) for pathfinding optimization notes.

#### **Current Status**

**DOCUMENTED** — Improvement already identified and partially addressed in pathfinding optimization. Not a critical issue for builder performance in most games.

#### **Workaround**

None needed — pathfinding is functional but could be ~5-10% faster with better heuristics.

---

### Issue 7: Worker Repair Exploit (Addressed)

**Severity:** LOW  
**Category:** Exploit Prevention  
**File:** [CustomMods.h](../../CvGameCoreDLL_Expansion2/CustomMods.h#L93)

#### **Problem**

Workers could repair pillaged improvements in foreign lands, creating a pillage-repair loop exploit.

#### **Current Status**

**ADDRESSED** — CustomMods flag prevents workers from repairing in foreign lands:
```cpp
// Line 93 comment: "Prevents repairing improvements in foreign lands with Workers, blocking the pillage-repair loop exploit"
```

This is controlled by `MOD_UNITS_CAN_REPAIR_IMPROVEMENTS_ONLY_OWN_LANDS`.

#### **Verification**

- Confirm flag is enabled in VP default settings
- Test that pillaging foreign improvement cannot be repaired by player's workers

---

## Implementation Priority Matrix

| Issue | Severity | Effort | Impact | Priority |
|-------|----------|--------|--------|----------|
| Net gold scoring | HIGH | MEDIUM | HIGH | 1 |
| Route planning documentation | MEDIUM | LOW | MEDIUM | 2 |
| Tech distance heuristic | MEDIUM | MEDIUM | MEDIUM | 3 |
| City strategy thresholds | LOW | LOW | LOW | 4 |
| Adjacency/feature interactions | MEDIUM | HIGH | LOW | 5 |
| Pathfinding underestimate | LOW | MEDIUM | LOW | 6 |

---

## Recommended Action Plan

### **Phase 1: Critical Fixes** (1-2 weeks)

1. Fix net gold scoring (Issue 1)
   - Modify `ScorePlotBuild()` to subtract maintenance
   - Add unit test with negative-gold improvements
   - Verify empire treasury improves in end-game

2. Document route planning heuristics (Issue 4)
   - Add GameDefines constants
   - Update CvBuilderTaskingAI.cpp comments
   - Extract thresholds to SQL/database for modding

### **Phase 2: Medium-Term Improvements** (2-4 weeks)

3. Replace tech distance heuristic with EconomicAI timeline (Issue 2)
   - Modify `GetRouteBuildTime()` 
   - Cross-reference with CvEconomicAI estimates
   - Test on various tech strategies

4. Document city strategy thresholds (Issue 5)
   - Add comments to CvCityStrategyAI.cpp
   - Create table mapping strategies to worker ratios
   - Explain transitions between states

### **Phase 3: Nice-to-Have** (4+ weeks)

5. Implement adjacency/feature interaction scoring (Issue 3)
   - Pre-compute neighbor adjacency impact
   - Add performance testing (ensure no regression)
   - Test on adjacency-heavy scenarios

6. Profile pathfinding performance (Issue 6)
   - Measure current performance
   - Implement improved heuristic for slow units
   - Benchmark speed improvement

---

## References

- [Main Review: IMPROVEMENTS_WORKERS_REVIEW.md](./IMPROVEMENTS_WORKERS_REVIEW.md)
- [Pathfinding Details](../unit-movement/IMPLEMENTATION_SUMMARY.md#worker-pathfinding)
- [City Management](../city-management-review.md)
- [Economy Reviews](../economy/)

