# Routes & Infrastructure: Implementation Checklist

## Issue 1: Trade Route War Validation ⚠️ CRITICAL (Multiplayer Focus)

### What to Fix
Add war/path validation check inside `CreateTradeRoute()` before allowing route creation.

**Note:** Primary concern is multiplayer. Single-player turn structure mitigates this (AI checks war status before its turn). Fix is defensive/robustness-oriented.

### Where to Find It
- **File:** `CvGameCoreDLL_Expansion2/CvTradeClasses.cpp`
- **Function:** `bool CvGameTrade::CreateTradeRoute(...)`
- **Related:** `bool CvGameTrade::IsValidTradeRoutePath(...)` — use this function for validation

### Code Pattern to Search For
```cpp
bool CvGameTrade::CreateTradeRoute(int iOriginPlayer, int iOriginCity, int iDestPlayer, int iDestCity, ...)
{
    // Look for: if (!HavePotentialTradePath(...))
    // This is where to add the new validation check
}
```

### Implementation Steps
1. [ ] Locate the line `if (!HavePotentialTradePath(...))` in `CreateTradeRoute()`
2. [ ] **After** this check, add a new line:
   ```cpp
   if (!IsValidTradeRoutePath(iOriginPlayer, iOriginCity, iDestPlayer, iDestCity))
       return false;
   ```
3. [ ] Build and test
4. [ ] Create test case: war declared, then trade route attempted

### Testing
```
Test Case: Single-Player War Handling
1. Load save: human player and AI at peace
2. Establish trade route to AI city (should succeed)
3. Declare war on AI (war state set immediately)
4. On AI's next turn, verify AI does NOT create routes through your territory
5. EXPECT: AI respects war state
6. RESULT: No new trade routes created by AI through your borders

Test Case: Route Cancellation (Existing Behavior)
1. Existing trade route active between cities
2. Declare war
3. EXPECT: Existing route auto-cancelled (this already works)
4. Merchant returns to origin
5. RESULT: No gold flows through war zone
```

### Risk: VERY LOW
- Minimal code change (one validation call)
- Only affects route creation validation
- No impact on existing routes (those auto-cancel already)
- Defensive programming (handles edge cases)
- Single-player mitigated by turn structure

---

## Issue 2: Negative Trade Route Slot Counts ⚠️ CRITICAL

### What to Fix
Clamp `GetNumTradeRoutesPossible()` to return ≥ 0.

### Where to Find It
- **File:** `CvGameCoreDLL_Expansion2/CvTradeClasses.cpp`
- **Function:** `int CvPlayerTrade::GetNumTradeRoutesPossible() const`
- **Related:** `GetNumTradeUnitsRemaining()`, UI trade route display

### Code Pattern to Search For
```cpp
int CvPlayerTrade::GetNumTradeRoutesPossible() const
{
    // Look for: multiplication by (100 + iModifier) / 100
    // This is where the clamping should be added
}
```

### Implementation Steps
1. [ ] Locate the line with: `* (100 + iModifier) / 100`
2. [ ] **After** the calculation, add a clamp line:
   ```cpp
   iNumRoutes = max(0, iNumRoutes);
   ```
3. [ ] Build and test
4. [ ] Create test case: civ with −150% modifier

### Testing
```
Test Case: Negative Routes Become Zero
1. Create civ with trait: "−150% international trade routes"
2. Load game with this civ
3. Check trade route screen
4. EXPECT: "0/4 routes" (not "−2/4" or negative number)
5. Try to create trade route
6. EXPECT: Error message "No available trade route slots"
```

### Risk: VERY LOW
- One-line fix
- Defensive coding (max function)
- No breaking changes

---

## Issue 3: Route Maintenance in Builder Scoring 🟡 HIGH

### What to Fix
Account for maintenance costs, movement speed bonuses, and treasury state in `ScorePlotBuild()`.

**Key insight:** Don't just deprioritize unprofitable routes—account for strategic value (railroads are 2x faster) and whether the empire can afford them.

### Where to Find It
- **File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp`
- **Function:** `int CvBuilderTaskingAI::ScorePlotBuild(...)`
- **Line Range:** ~3900-4200 (estimate; search for "GetProposedYield" or "YIELD_GOLD")

### Code Pattern to Search For
```cpp
int CvBuilderTaskingAI::ScorePlotBuild(CvPlot* pPlot, ...)
{
    // Look for: iScore with yield calculations
    // Look for: YIELD_GOLD loops
    // Look for: improvement/route type passed in
}
```

### Implementation Steps
1. [ ] Find the section where gold yield is scored
2. [ ] Locate the check: `if (eYield == YIELD_GOLD)`
3. [ ] **Inside this block**, add maintenance deduction:
   ```cpp
   if (eRoute != NO_ROUTE && eYield == YIELD_GOLD)
   {
       CvRouteInfo* pkRouteInfo = GC.getRouteInfo(eRoute);
       if (pkRouteInfo)
       {
           int iMaintenance = pkRouteInfo->GetGoldMaintenance();
           iYieldChange -= iMaintenance;  // Subtract maintenance from yield
       }
   }
   ```

4. [ ] Add movement bonus for railroads (strategic value):
   ```cpp
   int iMovementBonus = 0;
   if (eRoute == ROUTE_RAILROAD)
   {
       iMovementBonus = 500;  // Base strategic value
       
       // Reduce if empire is wealthy
       if (m_pPlayer->GetTreasury()->GetGoldPerTurn() > 50)
           iMovementBonus = 200;
   }
   ```

5. [ ] Check if empire can afford negative-gold routes:
   ```cpp
   bool bCanAffordNegative = (m_pPlayer->GetTreasury()->GetGoldPerTurn() > 25);
   
   if (iYieldChange < 0 && !bCanAffordNegative && iMovementBonus == 0)
   {
       return -1;  // Don't build
   }
   ```

6. [ ] Weight score based on situation (war vs. peace):
   ```cpp
   if (eRoute == ROUTE_RAILROAD && m_pPlayer->GetDiplomacyAI()->GetMilitaryPressure() > threshold)
   {
       // At war: speed matters more
       iScore += iMovementBonus + (iYieldChange * 5);
   }
   else
   {
       // Peace: gold matters more
       iScore += (iYieldChange * 10) + iMovementBonus;
   }
   ```

7. [ ] Build, test, and balance

### Testing
```
Test Case: Economic Routes (Must Be Profitable)
1. Create savegame with builder ready to build
2. Place two possible routes:
   - Route A: +2 GPT yield, −1 GPT maintenance (net +1)
   - Route B: +3 GPT yield, −3 GPT maintenance (net 0)
3. Score both routes
4. EXPECT: Route A scores higher than Route B
5. RESULT: Builder prioritizes more profitable routes

Test Case: Military Routes (Strategic Value Matters)
1. Load scenario: AI empire with +20 GPT surplus, aggressive neighbor
2. Verify AI DOES build railroads (−0.5 GPT) for military positioning
3. EXPECT: Railroads built despite negative gold (strategic value > cost)
4. RESULT: Army mobilization speed improves with railroad coverage

Test Case: Treasury Constraint
1. Load scenario: AI empire with −5 GPT (going bankrupt)
2. Verify AI does NOT build negative-gold routes
3. EXPECT: Only profitable routes built during financial crisis
4. When treasury recovers (>+25 GPT), verify railroads resume
5. RESULT: Builder respects empire's financial state

Test Case: Late Game Treasury
1. Load end-game save with many routes
2. Observe treasury at turn N
3. Note treasury at turn N+10
4. EXPECT: Treasury should be stable or improving (not decreasing)
5. Compare to baseline before fix
```

### Risk: MEDIUM-HIGH
- Touches scoring logic (impacts AI decisions)
- **Requires careful balance:** Railroads must be built for military purposes even when unprofitable
- **Requires treasury state integration:** Must respect financial constraints
- **Requires war pressure awareness:** Must prioritize speed during military threats
- Test thoroughly with multiple game situations (peace, war, financial crisis)
- May need iteration on movement bonus values and treasury thresholds

---

## Issue 4: Tech Route Movement Documentation 🟡 MEDIUM

### What to Fix
Add GameDefines constants and documentation to explain route tech progression.

### Where to Find It
- **File:** `CvGameCoreDLLUtil/include/CustomMods.h` OR `GameDefines.sql`
- **Related:** `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

### Implementation Steps

#### A. Add to CustomMods.h:
```cpp
// Route Movement Progression
#define ROUTE_ROAD_BASE_MOVEMENT           50
#define ROUTE_RAILROAD_BASE_MOVEMENT       25
#define ROUTE_ROAD_TECH_CONSTRUCTION_MOD   -10  // −20% improvement
#define ROUTE_ROAD_TECH_GUNPOWDER_MOD      -5   // −10% improvement
#define ROUTE_RAILROAD_TECH_COMBUSTION_MOD -10  // −40% improvement
```

#### B. Add to RouteChanges.sql:
```sql
-- Route Movement Progression
-- Design Intent: Routes become faster with technology, representing engineering advances.
--
-- Road (Base: 50 movement cost = 2 tiles/turn)
--   - CONSTRUCTION: −10 (40, representing standardized construction methods)
--   - GUNPOWDER: −5 (35, better engineering techniques)
--
-- Railroad (Base: 25 movement cost = 4 tiles/turn, 2x faster than road)
--   - COMBUSTION: −10 (15, steam engines + tracks)
--   - COMBINED_ARMS: −5 (10, diesel/electric engines)
--   - MOBILE_TACTICS: −5 (5, modern rail infrastructure)
--
-- Philosophy: Routes improve with technology, encouraging trade/communication growth.
-- Modders: Edit these values to adjust the route tech progression.

UPDATE Routes SET Movement = 50 WHERE Type = 'ROUTE_ROAD';
UPDATE Routes SET Movement = 25 WHERE Type = 'ROUTE_RAILROAD';
```

#### C. Create new doc file:
- **File:** `docs/infrastructure/ROUTE_PROGRESSION_DESIGN.md`
- **Content:**
  - Historical precedent for each tech modifier
  - Balance goals and interactions
  - Future extension possibilities

### Testing
```
Test Case: Documentation Clarity
1. Read CustomMods.h comments
2. Read RouteChanges.sql comments
3. Read ROUTE_PROGRESSION_DESIGN.md
4. Verify all three sources agree on the design
5. Check if a modder can understand the progression without asking
```

### Risk: VERY LOW
- Documentation only
- No code changes required
- Can be added without recompilation

---

## Issue 5: Route Planning Strategy Documentation 🟡 MEDIUM

### What to Fix
Add documentation and GameDefines constants for route planning priorities.

### Where to Find It
- **File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp`
- **Function:** `void CvBuilderTaskingAI::UpdateRoutePlots()`
- **Lines:** ~200-350 (estimate; search for "ConnectCitiesToCapital")

### Implementation Steps

#### A. Add to CustomMods.h:
```cpp
// Route Planning Priorities
#define ROUTE_MAIN_PRIORITY          100  // Capital to all cities
#define ROUTE_SHORTCUT_PRIORITY       50  // Close city pairs
#define ROUTE_STRATEGIC_PRIORITY      75  // Key resources
#define ROUTE_TRADE_PRIORITY          30  // High-gold cities
#define ROUTE_CITY_DISTANCE_SHORTCUT  20  // Distance threshold (tiles)
#define ROUTE_RECALC_FREQUENCY        10  // Turns between recalculation
```

#### B. Add to CvBuilderTaskingAI.cpp header:
```cpp
/*
 * Route Planning Strategy — Decision Tree
 *
 * Routes are prioritized in the following order:
 *
 * 1. MAIN ROUTES (Priority 100)
 *    - Capital → ALL other founded cities
 *    - Highest urgency, always built if path exists
 *    - Ensures rapid commodity distribution
 *
 * 2. SHORTCUT ROUTES (Priority 50)
 *    - City pairs within ROUTE_CITY_DISTANCE_SHORTCUT tiles
 *    - Lowers pathfinding cost between pairs
 *    - Speeds military mobilization and trade
 *
 * 3. STRATEGIC ROUTES (Priority 75)
 *    - Routes to strategic resources (Iron, Oil, Uranium)
 *    - Routes to key cultural/military tiles
 *    - Ensures resource access early
 *
 * 4. TRADE ROUTES (Priority 30)
 *    - Routes between high-gold city pairs
 *    - Built if commerce generation needs boost
 *    - Lower priority than commodity/strategic
 *
 * Recalculation: Plans cached and re-evaluated every
 * ROUTE_RECALC_FREQUENCY turns or after war/peace.
 */
```

#### C. Create new doc file:
- **File:** `docs/infrastructure/ROUTE_PLANNING_STRATEGY.md`
- **Content:**
  - Each route type with examples
  - How priorities interact with empire objectives
  - How to override for custom civs
  - Performance implications

### Testing
```
Test Case: Documentation Accuracy
1. Read CustomMods.h constants
2. Read CvBuilderTaskingAI.cpp comments
3. Read ROUTE_PLANNING_STRATEGY.md
4. Test on different map types:
   - Large island map (verify main routes work)
   - Cluster map (verify shortcuts built)
   - Resource-sparse map (verify strategic routes)
5. Verify AI route choices match documented priorities
```

### Risk: LOW
- Documentation and minor code comments
- No functional changes
- Can be added incrementally

---

## Issue 6: Unit-Type Movement Modifiers 🟡 MEDIUM

### What to Fix
Add `Route_DomainMovementModifiers` table to scale movement by unit domain.

### Where to Find It
- **File:** Database schema (Routes table)
- **Related:** `CvGameCoreDLL_Expansion2/CvAStar.cpp` — pathfinding movement calculation
- **Related:** `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

### Implementation Steps

#### A. Add new database table:
```sql
CREATE TABLE IF NOT EXISTS Route_DomainMovementModifiers (
    RouteType TEXT NOT NULL,
    DomainType TEXT NOT NULL,
    MovementModifier INT DEFAULT 100,  -- 100 = normal, 75 = 25% faster, 125 = 25% slower, -1 = cannot use
    PRIMARY KEY (RouteType, DomainType)
);

INSERT INTO Route_DomainMovementModifiers (RouteType, DomainType, MovementModifier) VALUES
    ('ROUTE_ROAD', 'DOMAIN_LAND', 100),          -- Normal
    ('ROUTE_ROAD', 'DOMAIN_SEA', -1),            -- Cannot use
    ('ROUTE_RAILROAD', 'DOMAIN_LAND', 100),      -- Normal
    ('ROUTE_RAILROAD', 'DOMAIN_SEA', -1);        -- Cannot use
```

#### B. Modify CvAStar.cpp:
```cpp
int CvAStar::GetMovementCost(CvPlot* pFromPlot, CvPlot* pToPlot, const CvUnit* pUnit)
{
    RouteTypes eRoute = pToPlot->getRouteType();
    if (eRoute == NO_ROUTE || pUnit == NULL)
        return GetBaseTileCost();  // No route, use normal terrain cost
    
    int iBaseCost = GC.getRouteInfo(eRoute).getMovement();
    
    // NEW: Apply domain-specific modifier
    int iModifier = GetRouteMovementModifier(eRoute, pUnit->getDomainType());
    
    if (iModifier == -1)
        return IMPOSSIBLE_MOVE;  // Unit cannot use this route
    
    iBaseCost = iBaseCost * iModifier / 100;
    
    return iBaseCost;
}

int CvAStar::GetRouteMovementModifier(RouteTypes eRoute, DomainTypes eDomain)
{
    // Query Route_DomainMovementModifiers table
    // Return modifier from database (default 100 if not found)
    // Implementation uses database lookup
}
```

### Testing
```
Test Case: Unit-Specific Movement on Routes
1. Create road with DOMAIN_LAND: 100%, DOMAIN_SEA: −1 (cannot use)
2. Move warrior on road: EXPECT cost = 50 (normal)
3. Move galley on road: EXPECT error or reroute (cannot use)
4. Create modifier: merchants 75% (25% faster)
5. Move merchant on road: EXPECT cost = 50 * 75 / 100 = 37.5
6. Verify pathfinding still works
7. Profile: EXPECT negligible performance impact

Test Case: Cross-Domain Route Logic
1. Create map with river (impassable to land)
2. Create road on one side, expecting land unit reroute
3. Create sea route on river (if applicable)
4. Verify pathfinding correctly chooses domain-appropriate routes
```

### Risk: MEDIUM
- Touches pathfinding (performance critical)
- Requires database schema change
- Requires careful testing for unit pathfinding
- Medium effort (code + database + testing)

---

## Summary Table

| Issue | Severity | Effort | Risk | Status |
|-------|----------|--------|------|--------|
| War validation | CRITICAL | MEDIUM | VERY LOW | ⏳ OPEN |
| Negative routes | CRITICAL | LOW | VERY LOW | ⏳ OPEN |
| Maintenance scoring | HIGH | HIGH | MEDIUM-HIGH | ⏳ OPEN |
| Tech documentation | MEDIUM | LOW | VERY LOW | ⏳ OPEN |
| Planning strategy | MEDIUM | MEDIUM | LOW | ⏳ OPEN |
| Unit modifiers | MEDIUM | HIGH | MEDIUM | ⏳ OPEN |

---

## Quick Start

To begin implementation:

1. **Week 1 (Critical):**
   - [ ] Issue 1: Add war validation check to `CreateTradeRoute()`
   - [ ] Issue 2: Add clamp to `GetNumTradeRoutesPossible()`
   - [ ] Build, test, commit

2. **Week 2-3 (High-Value):**
   - [ ] Issue 3: Subtract maintenance from builder scoring
   - [ ] Issue 4: Add GameDefines and comments for tech progression
   - [ ] Issue 5: Add GameDefines and comments for route planning
   - [ ] Build, test, commit

3. **Week 4+ (Nice-to-Have):**
   - [ ] Issue 6: Add unit-type movement modifiers
   - [ ] Build, test, commit

---

**Last Updated:** January 11, 2026

