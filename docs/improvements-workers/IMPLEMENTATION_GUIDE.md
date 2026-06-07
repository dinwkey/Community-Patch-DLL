# Improvements & Workers: Implementation Guide

## Overview

This guide provides step-by-step instructions for implementing fixes and improvements to the builder AI, improvement scoring, and worker production systems.

---

## Implementation: Net Gold Scoring Fix

**Priority:** 1 (Critical)  
**Time Estimate:** 3-4 hours  
**Files Affected:** `CvBuilderTaskingAI.cpp`

### **Step 1: Locate Scoring Function**

Open [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp) and find `ScorePlotBuild()` around line 3900.

Current signature:
```cpp
pair<int, int> CvBuilderTaskingAI::ScorePlotBuild(CvPlot* pPlot, ImprovementTypes eImprovement, BuildTypes eBuild, ...)
```

### **Step 2: Add Maintenance Deduction**

Locate the section where gold yields are scored. It looks like:

```cpp
// Current (line ~3950)
int iGoldYield = pProposedYields[YIELD_GOLD] - pCurrentYields[YIELD_GOLD];
iScore += iGoldYield * GOLD_WEIGHT; // GOLD_WEIGHT is some multiplier
```

**Modify to:**

```cpp
// Fixed version
int iGoldYield = pProposedYields[YIELD_GOLD] - pCurrentYields[YIELD_GOLD];

// Deduct maintenance cost
CvImprovementEntry* pkImprovement = GC.getImprovementInfo(eImprovement);
if (pkImprovement)
{
    int iMaintenanceCost = pkImprovement->GetMaintenanceNeededAmount();
    iGoldYield -= iMaintenanceCost;
}

// Score net gold (penalize negative)
int iNetGoldScore = iGoldYield;
if (iGoldYield < 0)
{
    // Reduce negative gold improvements to 50% weight
    iNetGoldScore = (iGoldYield * 50) / 100;
}

iScore += iNetGoldScore * GOLD_WEIGHT;

// Optional: Log for debugging
if (m_bLogging)
{
    CvString strLog;
    strLog.Format("Plot(%d,%d) %s: Gross Gold=%d, Maintenance=%d, Net=%d, Score Contrib=%d",
        pPlot->getX(), pPlot->getY(),
        (pkImprovement ? pkImprovement->GetType() : "NONE"),
        pProposedYields[YIELD_GOLD],
        (pkImprovement ? pkImprovement->GetMaintenanceNeededAmount() : 0),
        iNetGoldScore,
        iNetGoldScore * GOLD_WEIGHT);
    LogYieldInfo(strLog, m_pPlayer);
}
```

### **Step 3: Handle Great Person Improvements**

Some Great Person improvements (e.g., National Wonders) don't have maintenance. Verify:

```cpp
// Safety check - if GetMaintenanceNeededAmount() doesn't exist, use default
int iMaintenanceCost = 0;
if (pkImprovement && pkImprovement->GetMaintenanceNeededAmount != NULL)
{
    iMaintenanceCost = pkImprovement->GetMaintenanceNeededAmount();
}
```

### **Step 4: Test the Change**

#### **Unit Test 1: Negative Gold Improvement**

1. Open map or scenario
2. Create custom improvement: "+3 GPT, +5 GPT maintenance cost"
3. Place builder near this improvement tile
4. **Expected:** Builder ignores this improvement, prioritizes others
5. **Actual:** [Test result here]

#### **Unit Test 2: Positive Gold Improvement**

1. Create custom improvement: "+5 GPT, +2 GPT maintenance cost" (net +3)
2. **Expected:** Builder prioritizes this (net positive)
3. **Actual:** [Test result here]

#### **System Test: Late-Game Treasury**

1. Start game on standard difficulty
2. Play to late game (Renaissance era+)
3. Observe empire treasury trend
4. **Before Fix:** Treasury may decline as maintenance exceeds income
5. **After Fix:** Treasury should remain stable or increase

### **Step 5: Compile & Verify**

```bash
# Build the DLL
python build_vp_clang.py --config debug

# Expected output
# clang-output/debug/CvGameCore_Expansion2.dll
# clang-output/debug/CvGameCore_Expansion2.pdb
```

---

## Implementation: Route Planning Documentation

**Priority:** 2 (Medium)  
**Time Estimate:** 2-3 hours  
**Files Affected:** `CvBuilderTaskingAI.cpp`, `CvDefines.h`, SQL database

### **Step 1: Add GameDefines Constants**

Open [CvDefines.h](../../CvGameCoreDLL_Expansion2/CvDefines.h) and add:

```cpp
// Builder Route Planning Priorities (lines ~8950)
#define ROUTE_MAIN_PRIORITY           100    // Capital to all cities
#define ROUTE_SHORTCUT_PRIORITY        50    // City pairs within distance
#define ROUTE_STRATEGIC_PRIORITY       75    // To key resources
#define ROUTE_CITY_DISTANCE_SHORTCUT   20    // Tiles; pairs closer than this get shortcut routes
#define ROUTE_RECALC_FREQUENCY         10    // Turns between route plan recalculations
#define ROUTE_PRIORITY_MULTIPLIER      2     // How much higher main routes vs shortcuts
```

### **Step 2: Document Decision Tree**

Open [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp) header section (around line 1-100) and add:

```cpp
/*
 * ROUTE PLANNING SYSTEM
 * 
 * Routes are categorized and prioritized:
 * 
 * 1. MAIN ROUTES (Capital → All Other Cities)
 *    Priority: ROUTE_MAIN_PRIORITY (100)
 *    Purpose: Enable trade, connect empire, unlock city connections
 *    Recalc: Every ROUTE_RECALC_FREQUENCY turns
 *    
 * 2. SHORTCUT ROUTES (City Pair Optimization)
 *    Priority: ROUTE_SHORTCUT_PRIORITY (50)
 *    Condition: Cities within ROUTE_CITY_DISTANCE_SHORTCUT tiles (20)
 *    Purpose: Reduce travel time between nearby cities, enable trade
 *    Recalc: Every ROUTE_RECALC_FREQUENCY turns
 *    
 * 3. STRATEGIC ROUTES (To Key Resources)
 *    Priority: ROUTE_STRATEGIC_PRIORITY (75)
 *    Condition: Resource is Strategic (iron, oil, uranium, horses, etc.)
 *    Purpose: Enable strategic resource access, military supply lines
 *    Recalc: When new strategic resources discovered
 *    
 * 4. TRADE ROUTES (High-Gold City Pairs)
 *    Priority: ROUTE_PRIORITY_MULTIPLIER * base priority
 *    Condition: Trade route between cities exceeds gold threshold
 *    Purpose: Maximize gold income
 *    Recalc: Every ROUTE_RECALC_FREQUENCY turns
 *
 * Implementation: UpdateRoutePlots() iterates through each category,
 * scores potential routes, and populates m_plotRoutePurposes map.
 */
```

### **Step 3: Locate Route Planning Code**

Find the `UpdateRoutePlots()` method (around line 800-1100):

```cpp
void CvBuilderTaskingAI::UpdateRoutePlots(void)
{
    // Find these sections:
    // 1. ConnectCitiesToCapital() — Main routes
    // 2. ConnectCitiesForShortcuts() — Shortcut routes
    // 3. Strategic resource connection logic
    // 4. Trade route scoring
}
```

### **Step 4: Add Route Purpose Comments**

In each route addition, add inline comments:

```cpp
// Main routes: Capital to all cities (ROUTE_MAIN_PRIORITY)
ConnectCitiesToCapital(pPlayerCapital, pTargetCity, eBuild, eRoute);

// Shortcut routes: Nearby city pairs (ROUTE_SHORTCUT_PRIORITY)
if (iDistance <= GD_INT_GET(ROUTE_CITY_DISTANCE_SHORTCUT))
{
    ConnectCitiesForShortcuts(pCity1, pCity2, eBuild, eRoute);
}

// Strategic routes: Key resources (ROUTE_STRATEGIC_PRIORITY)
if (pPlot->getResourceType() != NO_RESOURCE && 
    GC.getResourceInfo(pPlot->getResourceType())->isStrategic())
{
    ConnectPointsForStrategy(pOriginCity, pPlot, eBuild, eRoute);
}
```

### **Step 5: Update AddRoutePlots Method**

Find `AddRoutePlots()` around line 3200 and ensure it uses the constants:

```cpp
void CvBuilderTaskingAI::AddRoutePlots(CvPlot* pStartPlot, CvPlot* pTargetPlot, RouteTypes eRoute, int iValue, const SPath& path, RoutePurpose ePurpose, bool bUseRivers)
{
    // Adjust value based on purpose
    int iAdjustedValue = iValue;
    switch (ePurpose)
    {
        case MAIN_ROUTE:
            iAdjustedValue = iValue * GD_INT_GET(ROUTE_PRIORITY_MULTIPLIER);
            break;
        case SHORTCUT_ROUTE:
            iAdjustedValue = iValue;
            break;
        case STRATEGIC_ROUTE:
            iAdjustedValue = iValue * 75 / 100; // 75% of main route priority
            break;
    }
    
    // ... rest of method
}
```

### **Step 6: Test the Documentation**

1. Open source code in VS Code
2. Navigate to `UpdateRoutePlots()` method
3. Verify comments explain each route type clearly
4. Read as external developer — is logic clear?

### **Step 7: Compile & Verify**

```bash
python build_vp_clang.py --config debug
```

---

## Implementation: Tech Distance Heuristic Fix

**Priority:** 3 (Medium-High)  
**Time Estimate:** 4-6 hours  
**Files Affected:** `CvBuilderTaskingAI.cpp`, `CvEconomicAI.cpp`

### **Step 1: Understand Current Implementation**

Locate `GetRouteBuildTime()` around line 3200-3300:

```cpp
int CvBuilderTaskingAI::GetRouteBuildTime(PlannedRoute plannedRoute, const CvUnit* pUnit = (CvUnit*)NULL) const
{
    // Current: uses tech tree position as heuristic
    int iEstimatedTechTurns = // based on tree position, hacky!
    return iEstimatedTechTurns;
}
```

### **Step 2: Add EconomicAI Method (if missing)**

In [CvEconomicAI.h](../../CvGameCoreDLL_Expansion2/CvEconomicAI.h), verify method exists:

```cpp
// In public methods section
int GetEstimatedTechResearchTurn(TechTypes eTech) const;
```

If missing, add to header around line ~250:

```cpp
// Estimated research timeline
int GetEstimatedTechResearchTurn(TechTypes eTech) const; // Returns turn when tech expected to be researched
```

### **Step 3: Implement EconomicAI Method**

In [CvEconomicAI.cpp](../../CvGameCoreDLL_Expansion2/CvEconomicAI.cpp), add implementation:

```cpp
int CvEconomicAI::GetEstimatedTechResearchTurn(TechTypes eTech) const
{
    CvPlayer* pPlayer = &GET_PLAYER(m_ePlayer);
    CvTeamTechs* pTeamTechs = GET_TEAM(pPlayer->getTeam()).GetTeamTechs();
    
    // Already researched
    if (pTeamTechs->HasTech(eTech))
        return GC.getGame().getGameTurn();
    
    // Estimate based on current science rate and tech cost
    CvTechEntry* pkTechInfo = GC.getTechInfo(eTech);
    if (!pkTechInfo)
        return GC.getGame().getGameTurn() + 1000; // default far future
    
    int iScienceRate = pPlayer->GetTotalYield(YIELD_SCIENCE); // or similar
    if (iScienceRate <= 0)
        iScienceRate = 1; // avoid divide by zero
    
    int iTechCost = pkTechInfo->GetResearchCost();
    int iRemainingCost = iTechCost; // minus current progress if available
    
    int iTurnsToComplete = iRemainingCost / iScienceRate;
    if (iRemainingCost % iScienceRate != 0)
        iTurnsToComplete++; // ceil
    
    // Cap at reasonable value to avoid absurdly distant estimates
    return GC.getGame().getGameTurn() + std::min(iTurnsToComplete, 200);
}
```

### **Step 4: Refactor GetRouteBuildTime**

Replace heuristic with EconomicAI call:

```cpp
int CvBuilderTaskingAI::GetRouteBuildTime(PlannedRoute plannedRoute, const CvUnit* pUnit = NULL) const
{
    // OLD: int iRouteBuildDelay = /* hacky tree position based estimate */
    
    // NEW: Use EconomicAI to estimate tech unlock time
    // (This assumes route requires new tech)
    
    CvEconomicAI* pEconAI = m_pPlayer->GetEconomicAI();
    if (!pEconAI)
        return 50; // fallback
    
    RouteTypes eRoute = plannedRoute.second.first;
    int iTechUnlockTurn = pEconAI->GetEstimatedTechResearchTurn(TechUnlockedByRoute(eRoute));
    int iCurrentTurn = GC.getGame().getGameTurn();
    int iRouteBuildDelay = std::max(0, iTechUnlockTurn - iCurrentTurn);
    
    // Cap at reasonable values
    if (iRouteBuildDelay > 100)
        iRouteBuildDelay = 100; // very distant tech
    
    // Adjust for current unit work rate
    if (pUnit)
        iRouteBuildDelay = iRouteBuildDelay * 100 / pUnit->workRate(false);
    
    return iRouteBuildDelay;
}
```

### **Step 5: Add Helper: TechUnlockedByRoute**

Add utility function:

```cpp
// In CvBuilderTaskingAI private methods
TechTypes CvBuilderTaskingAI::TechUnlockedByRoute(RouteTypes eRoute) const
{
    CvRouteInfo* pkRouteInfo = GC.getRouteInfo(eRoute);
    if (!pkRouteInfo)
        return NO_TECH;
    
    // Typically TECH_WRITING for roads, TECH_RAILROAD for railroads
    return (TechTypes)pkRouteInfo->getTechPrereq();
}
```

### **Step 6: Test the Fix**

#### **Unit Test 1: Early Tech Tech Rush**

1. Create scenario where TECH_RAILROAD is available at start
2. Verify route builder immediately plans railroad routes
3. **Expected:** Route building time = current turn + 1 (not some distant future)

#### **Unit Test 2: Late Tech Delay**

1. Create scenario where TECH_RAILROAD is late (no starting techs toward it)
2. Verify route builder defers railroad route building
3. **Expected:** Route building delay = estimated tech research time (50+ turns)

### **Step 7: Compile & Verify**

```bash
python build_vp_clang.py --config debug
```

---

## Implementation: City Strategy Documentation

**Priority:** 4 (Low)  
**Time Estimate:** 1-2 hours  
**Files Affected:** `CvCityStrategyAI.cpp`

### **Step 1: Add Comments to Strategy Checks**

Open [CvCityStrategyAI.cpp](../../CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp) around line 2358:

```cpp
/// "Need Tile Improvers" City Strategy: Do we REALLY need to train some Workers?
// Trigger Condition: Empire has fewer workers than required ratio
// Formula: iNumWorkers * 100 < iCurrentNumCities * 67
// Example: 2 workers, 5 cities → 2*100=200 < 5*67=335 → TRIGGER
// Effect: Blocks all non-worker production, forces worker to front of queue
// Recovery: Released when NO_WORKER_AFTER_DISBAND_DURATION expires (default 10 turns after worker dies)
bool CityStrategyAIHelpers::IsTestCityStrategy_NeedTileImprovers(AICityStrategyTypes eStrategy, CvCity* pCity)
{
    // ... existing code
}
```

### **Step 2: Document Transition States**

Add a table comment before the three functions:

```cpp
/*
 * WORKER PRODUCTION STATE MACHINE
 * 
 * State         | Trigger Condition           | Effect                    | Next State
 * --------------|-----------------------------|--------------------------|-----------
 * NEED          | workers < cities * 0.67     | Force worker production   | WANT
 * WANT          | workers >= cities * 0.67    | Prefer worker production  | ENOUGH
 * ENOUGH        | workers >= cities * 1.50    | Block worker production   | WANT
 * 
 * Special Case: If worker dies, state reverts to NEED for 10 turns (NO_WORKER_AFTER_DISBAND_DURATION)
 */
```

### **Step 3: Extract Constants**

Create a macro definition section:

```cpp
// Worker Strategy Thresholds
#define WORKER_NEED_RATIO           67   // 67% of cities require workers (NEED triggers)
#define WORKER_WANT_RATIO           100  // 100% of cities can use workers (WANT triggers)
#define WORKER_ENOUGH_RATIO         150  // 150% of cities → block production (ENOUGH triggers)
#define NO_WORKER_AFTER_DISBAND_DURATION 10 // turns to wait before re-training
```

### **Step 4: Update Code to Use Constants**

Modify strategy checks:

```cpp
// OLD
if(iModdedNumWorkers <= iCurrentNumCities || iModdedNumWorkers == 0)
    return true;

// NEW
if((iNumWorkers * 100) <= (iCurrentNumCities * WORKER_NEED_RATIO) || iNumWorkers == 0)
    return true;
```

### **Step 5: Test Documentation Clarity**

1. Read comments aloud to a colleague
2. Ask: "Can you understand when each strategy triggers?"
3. Verify answer is "yes"

---

## Validation Checklist

- [ ] **Net Gold Fix**
  - [ ] Negative gold improvements avoided in test scenario
  - [ ] Positive gold improvements prioritized
  - [ ] End-game treasury remains stable (late-game test)
  - [ ] No regressions in other improvement types

- [ ] **Route Planning Documentation**
  - [ ] GameDefines constants added
  - [ ] Comments added to `UpdateRoutePlots()`
  - [ ] Route categories clearly explained
  - [ ] Code compiles without warnings

- [ ] **Tech Distance Heuristic**
  - [ ] `GetEstimatedTechResearchTurn()` implemented in EconomicAI
  - [ ] `GetRouteBuildTime()` refactored to use new method
  - [ ] Early tech rush scenario works correctly
  - [ ] Late tech delay scenario defers routes appropriately

- [ ] **City Strategy Documentation**
  - [ ] Comments added to three strategy functions
  - [ ] State machine table added
  - [ ] Constants extracted and used
  - [ ] Documentation is clear to external reader

---

## Compilation & Testing

### **Local Compilation**

```bash
# Build with clang (recommended for quick iteration)
python build_vp_clang.py --config debug

# Check for build errors
cat clang-output/debug/build.log | grep -i error
```

### **Game Testing**

1. Copy DLL to mod folder: `(1) Community Patch Core`
2. Launch Civilization V
3. Enable mod in Mod Manager
4. Load scenario or start new game
5. Observe builder behavior and worker production

### **Logging**

Enable builder AI logging:

1. Edit `My Games\Sid Meier's Civilization 5\config.ini`
2. Add: `LogLogging = 1`, `LogAI = 1`, `LogBuilder = 1`
3. Logs appear in `My Games\Sid Meier's Civilization 5\Logs\BuilderTaskingYieldLog.csv`

---

## References

- [Main Review](./IMPROVEMENTS_WORKERS_REVIEW.md)
- [Issues & Fixes](./ISSUES_AND_FIXES.md)
- [CvBuilderTaskingAI.cpp](../../CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp)
- [CvCityStrategyAI.cpp](../../CvGameCoreDLL_Expansion2/CvCityStrategyAI.cpp)
- [Build Instructions](../../DEVELOPMENT.md)

