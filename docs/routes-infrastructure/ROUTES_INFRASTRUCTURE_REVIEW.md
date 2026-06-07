# Routes & Infrastructure: Issues and Improvements Review

**Date:** January 11, 2026  
**Scope:** Roads, railroads, routes, maintenance costs, route-based yields, and unit movement  
**Primary Files:** 
- `CvGameCoreDLL_Expansion2/CvCityConnections.cpp`
- `CvGameCoreDLL_Expansion2/CvTradeClasses.cpp`
- `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp`
- `CvGameCoreDLL_Expansion2/CvInfos.h` (CvRouteInfo)
- `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

---

## Executive Summary

This review identifies **2 critical issues** and **4 process/design improvements** affecting the roads, railroads, and route system in Civilization V Community Patch / Vox Populi:

### Critical Issues
1. **Trade route creation lacks war/path validation** — Routes can move through enemy territory after war declaration
2. **Negative trade route slot counts possible** — Modifiers ≤ −100% create nonsensical UI and logic errors

### High-Value Improvements
3. **Route maintenance vs. net yield not factored into builder scoring** — Workers may build routes that cost more to maintain than they generate
4. **Tech-based route movement speed improvements underdocumented** — Route changes hard-coded in SQL, unclear to modders
5. **Route planning heuristics not explained** — Builders have no clear strategy for prioritizing main routes vs. shortcuts vs. strategic routes
6. **Movement cost optimizations possible** — Road/railroad movement speeds could scale better with technology progression

---

## Issue Details

### Issue 1: Trade Route Creation Skips War/Path Checks ⚠️ CRITICAL

**Severity:** CRITICAL  
**Category:** Validation/Logic Flaw  
**File:** `CvGameCoreDLL_Expansion2/CvTradeClasses.cpp`  
**Related:** `CvGameTrade::CreateTradeRoute`, `CvGameTrade::CanCreateTradeRoute`

#### Problem

`CreateTradeRoute()` calls `HavePotentialTradePath()` to cache a path, then creates the trade route without re-running `IsValidTradeRoutePath()`. As soon as the war is declared or open borders drop, the cached path remains valid, and a new trade route can move through enemy territory for one turn before either player cancels it.

**Scenario:**
1. Player A and Player B are at peace, both with open borders
2. Trade route established: City A → City B, path caches as valid
3. Player A declares surprise war on Player B
4. At the start of next turn, Player B may create a new trade route (using old cached path) before the war notification is processed
5. Result: New route moves through now-closed borders until an explicit cancel happens

#### Impact

**Multiplayer (PRIMARY CONCERN):**
- War state propagation may have network/async timing delays
- Aggressive players could create routes during brief window before war state syncs across all clients
- Can create disputes over "illegal" routes moving through enemy territory
- Unfair economic advantage if new routes deliver gold before detection

**Single-Player (MITIGATED):**
- Turn-based structure ensures war state is known before AI's next turn
- AI should not attempt route creation after war is declared
- Risk is minimal unless there are state sync delays (unlikely in single-player)
- Not a major gameplay vulnerability due to turn ordering

#### Root Cause

Two separate validation functions:
1. `HavePotentialTradePath()` — checks connectivity, caches result
2. `IsValidTradeRoutePath()` — checks war status, open borders, resource availability

Only the first is called in `CreateTradeRoute()`. The second is called in `CanCreateTradeRoute()` (UI decision) but not at creation time.

#### Proposed Fix

**Recommended: Add war check at creation time (defensive, low-risk)**

```cpp
bool CvGameTrade::CreateTradeRoute(int iOriginPlayer, int iOriginCity, int iDestPlayer, int iDestCity, ...)
{
    // Existing path check
    if (!HavePotentialTradePath(iOriginPlayer, iOriginCity, iDestPlayer, iDestCity))
        return false;
    
    // NEW: Re-validate war/path status before creating
    // This provides defensive validation, especially important for multiplayer
    if (!IsValidTradeRoutePath(iOriginPlayer, iOriginCity, iDestPlayer, iDestCity))
        return false;
    
    // ... rest of creation logic
}
```

**Rationale:**
While single-player turn structure mitigates this issue, adding the check is a defensive programming best practice that:
- Ensures multiplayer robustness against network timing
- Documents the validation intent clearly
- Prevents edge cases (state sync delays, async calls)
- Minimal performance impact

#### Testing

**Single-Player Test:**
1. Create save with human player and AI at peace, trade route active
2. Declare surprise war on AI
3. On AI's next turn, verify AI does NOT create new trade routes through your territory
4. Verify existing routes are cancelled automatically
5. Verify AI respects war state when deciding on trade routes

**Multiplayer Test (if applicable):**
1. Create multiplayer save with two human players at peace
2. Player A declares war on Player B
3. Verify Player B cannot create new routes through Player A's territory
4. Test rapid war declaration + route creation to verify no window exists

#### Status
**OPEN** — Requires code change and testing

---

### Issue 2: Negative Trade Route Slot Counts ⚠️ CRITICAL

**Severity:** CRITICAL  
**Category:** Logic Error  
**File:** `CvGameCoreDLL_Expansion2/CvTradeClasses.cpp` (likely `GetNumTradeRoutesPossible()`)

#### Problem

`GetNumTradeRoutesPossible()` multiplies base slot count by `(100 + NumTradeRoutesModifier) / 100` but never clamps the result to ≥ 0. Policies or traits with modifier ≤ −100 return negative numbers.

**Example:**
- Base routes: 4
- Modifier: −150 (from policies/traits)
- Result: `4 * (100 − 150) / 100 = 4 * (−50) / 100 = −2`
- UI displays: "−2/4 trade routes available" (nonsensical)

#### Impact

- **UI Corruption:** Displays negative numbers in trade route counter
- **Logic Errors:** `GetNumTradeUnitsRemaining()` and other functions can return negative counts
- **Player Confusion:** Negative routes suggest a debt or penalty that doesn't exist
- **AI Exploits:** AI functions checking `NumTradeRoutesPossible > 0` may make incorrect decisions if count is negative

#### Root Cause

Modifiers are applied without bounds checking. The code assumes modifiers will never exceed −100%, but some civs or policy combos may do exactly that.

#### Proposed Fix

**Clamp after modifier application:**

```cpp
int CvPlayerTrade::GetNumTradeRoutesPossible() const
{
    int iNumRoutes = m_pPlayer->GetTradeRoutesBaseValue(); // e.g., 4
    
    // Apply modifier
    int iModifier = ... // sum of all trait/policy modifiers
    iNumRoutes = iNumRoutes * (100 + iModifier) / 100;
    
    // NEW: Clamp to non-negative
    iNumRoutes = max(0, iNumRoutes);
    
    return iNumRoutes;
}
```

**Alternative (more conservative):** Cap modifier at −99%:
```cpp
iModifier = max(iModifier, -99); // never go to −100% or worse
```

#### Testing

1. Create civ with trait: "−150% trade routes"
2. Load game with this civ
3. Open trade route screen
4. Verify trade route count displays as "0" or "0/0", not "−2"
5. Verify `GetNumTradeUnitsRemaining()` returns 0 or error gracefully
6. Try to create trade route
7. Verify error message is clear ("no trade route slots available")

#### Status
**OPEN** — Requires code change and testing

---

### Issue 3: Route Maintenance Not Included in Builder Scoring

**Severity:** HIGH  
**Category:** AI Decision-Making  
**File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp` (likely line ~3900 `ScorePlotBuild()`)

#### Problem

Builder AI scores improvements without subtracting maintenance costs from yields. However, the actual issue is more nuanced than simply "make routes unprofitable":

**Economic routes (roads)** should be profitable: yield −3 GPT is bad.  
**Military/logistics routes (railroads)** have strategic value beyond gold: −1 GPT may be worth it if 2x faster movement helps military positioning.

Current scoring ignores:
- Movement speed bonuses (railroads 2x faster than roads)
- Military strategy (war preparation, troop mobilization)
- Treasury state (can the empire afford negative-gold routes?)

```cpp
// Current implementation (line ~2068)
// TODO include net gold in all gold yield/maintenance computations

// Example: Trade Post yields +3 GPT but costs +2 GPT maintenance
// Scored as +3 (incorrect) instead of +1 (correct)

// But: Railroad yields −1 GPT, costs +0.5 GPT maintenance (net −1.5 GPT)
// Currently scored as −1, should also consider:
//   - 2x faster movement (strategic value ~500 points)
//   - War preparation (if enemy nearby, very valuable)
//   - Empire treasury state (can we afford it?)
```

#### Impact

**Economic side (negative):**
- Empire treasury can drain over time as builders build unprofitable routes
- Gold-focused cities waste effort on negative-gold improvements
- Late-game empires with many improvements may face unexpected gold penalties

**Strategic side (overlooked):**
- Railroads deprioritized even though their speed bonus is militarily valuable
- Military preparation routes not built at appropriate times
- Army mobilization speed suffers late-game due to road-only network

#### Root Cause

`ScorePlotBuild()` scores yields independently. It doesn't differentiate between:
- **Economic routes** (trade roads, caravan routes) — must be profitable
- **Military routes** (railroads, logistics) — speed/strategy > gold profitability

Maintenance is handled separately by city's financial system, not integrated into builder scoring. Additionally, there's no weighing of **strategic value** (movement speed, war preparation) vs. **economic value** (gold yield).

#### Proposed Fix

Modify `ScorePlotBuild()` to account for maintenance, strategic bonuses, and treasury state:

```cpp
int CvBuilderTaskingAI::ScorePlotBuild(CvPlot* pPlot, RouteTypes eRoute, ...)
{
    int iScore = 0;
    
    // 1. Calculate net gold (yield minus maintenance)
    int iGoldYield = GetProposedYield(pPlot, eRoute, YIELD_GOLD);
    CvRouteInfo* pkRouteInfo = GC.getRouteInfo(eRoute);
    int iMaintenance = 0;
    if (pkRouteInfo)
    {
        iMaintenance = pkRouteInfo->GetGoldMaintenance();
        iGoldYield -= iMaintenance;  // Net gold
    }
    
    // 2. Add movement speed bonus for fast routes
    int iMovementBonus = 0;
    if (eRoute == ROUTE_RAILROAD)
    {
        // Railroads are 2x faster than roads
        // Value this for military purposes even if gold is negative
        iMovementBonus = 500;  // Base strategic value
        
        // Reduce if empire is wealthy (already have resources)
        if (m_pPlayer->GetTreasury()->GetGoldPerTurn() > 50)
            iMovementBonus = 200;  // Still valuable but not desperate
    }
    
    // 3. Check if we can afford negative-gold routes
    bool bCanAffordNegative = (m_pPlayer->GetTreasury()->GetGoldPerTurn() > 25);
    
    // 4. Make decision
    if (iGoldYield < 0 && !bCanAffordNegative && iMovementBonus == 0)
    {
        // Cannot afford this route and no strategic value
        return -1;  // Don't build
    }
    
    // 5. Score: weight gold lower for military routes
    if (eRoute == ROUTE_RAILROAD && m_pPlayer->GetDiplomacyAI()->GetMilitaryPressure() > threshold)
    {
        // At war or military threat: prioritize speed
        iScore += iMovementBonus + (iGoldYield * 5);  // Speed matters more
    }
    else
    {
        // Peace: prioritize gold
        iScore += (iGoldYield * 10) + iMovementBonus;  // Gold matters more
    }
    
    return iScore;
}
```

**Key improvements:**
- Subtracts maintenance from gold yield
- Adds strategic movement bonus for railroads
- Checks treasury before building negative-gold routes
- Weights differently based on game situation (war vs. peace)
- Allows railroads even if unprofitable IF empire has surplus

#### Testing

**Economic Test (should NOT build unprofitable routes):**
1. Create improvement with −3 GPT net gold (high maintenance, low yield)
2. Verify builder does NOT prioritize this route
3. Create route with +1 GPT net gold
4. Verify builder scores it lower than +2 food farm
5. Load end-game save (many routes built)
6. Observe treasury should remain stable (not drain)
7. Compare AI empire's gold before/after fix — should improve or stabilize

**Military Test (SHOULD build railroads despite negative gold):**
1. Create scenario: AI empire with +10 GPT surplus, neighbor is aggressive
2. Verify AI DOES build railroads (−0.5 GPT) for military positioning
3. Verify railroads are prioritized when war is imminent
4. Load scenario with empty treasury (−5 GPT)
5. Verify AI does NOT build negative-gold railroads during financial crisis
6. When treasury recovers (>+25 GPT), verify railroads resume
7. Compare military unit mobilization speed (should improve with railroad coverage)

#### Status
**OPEN** — Requires code change, testing, and potential balance review

---

### Issue 4: Tech-Based Route Movement Improvements Underdocumented

**Severity:** MEDIUM  
**Category:** Documentation & Modding Clarity  
**File:** `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

#### Problem

Route movement speed is tuned via SQL with tech-based modifiers, but the logic is not explained. Modders cannot understand or adjust the progression without deep database knowledge.

**Current SQL:**
```sql
UPDATE Routes SET GoldMaintenance = 3 WHERE Type = 'ROUTE_RAILROAD';
UPDATE Routes SET Movement = 50, FlatMovement = 50 WHERE Type = 'ROUTE_ROAD';
UPDATE Routes SET Movement = 25, FlatMovement = 25 WHERE Type = 'ROUTE_RAILROAD';

INSERT INTO Route_TechMovementChanges (RouteType, TechType, MovementChange) VALUES
    ('ROUTE_ROAD', 'TECH_CONSTRUCTION', -10),    -- 50 → 40
    ('ROUTE_ROAD', 'TECH_GUNPOWDER', -5),       -- 40 → 25
    ('ROUTE_RAILROAD', 'TECH_COMBUSTION', -10), -- 25 → 15
    ...;
```

**Questions:**
- Why does TECH_CONSTRUCTION reduce road cost by 10?
- What's the design intent? (Faster commerce? Historical progression?)
- Should we add early-game tech (TECH_WHEEL) for pre-road infrastructure?
- Why are some techs commented out?

#### Impact

- **Modders cannot balance** route progression without recompiling or deep DB knowledge
- **Design intent unclear** — hard to extend or improve the system
- **Missed modding opportunities** — community cannot create alternative route systems
- **Inconsistency risk** — routes may not scale consistently with other improvements

#### Root Cause

Movement values and tech modifiers are in SQL but lack comments explaining the design. No GameDefines constants to make the progression clear.

#### Proposed Fix

**1. Add GameDefines constants:**

```cpp
// CvGameCoreDLLUtil/include/CustomMods.h or GameDefines.sql

#define ROUTE_ROAD_BASE_MOVEMENT           50  // Cost 50 movement points to use road
#define ROUTE_RAILROAD_BASE_MOVEMENT       25  // Railroads are 2x faster than roads

// Tech modifiers reduce movement cost (negative = improvement)
#define ROUTE_ROAD_TECH_CONSTRUCTION_MOD   -10 // −20% improvement
#define ROUTE_ROAD_TECH_GUNPOWDER_MOD      -5  // −10% improvement
#define ROUTE_RAILROAD_TECH_COMBUSTION_MOD -10 // −40% improvement (2.5x base → 1.5x)
```

**2. Add documentation comment in SQL:**

```sql
-- Route Movement Progression
-- Design Intent: Routes become faster with technology, representing engineering improvements.
--
-- Road Timeline:
--   - Base: 50 movement (2 tiles/turn for most units)
--   - CONSTRUCTION: −10 (40 cost, engineering basics)
--   - GUNPOWDER: −5 (35 cost, standardized roads)
--
-- Railroad Timeline:
--   - Base: 25 movement (4 tiles/turn, 2x speed of road)
--   - COMBUSTION: −10 (15 cost, steam engines)
--   - COMBINED_ARMS: −5 (10 cost, diesel/electric)
--   - MOBILE_TACTICS: −5 (5 cost, modern rail)
--
-- Philosophy: Encourages trade/communication increases as technology advances.
-- See ROUTES_INFRASTRUCTURE_REVIEW.md for detailed design discussion.

UPDATE Routes SET GoldMaintenance = 3 WHERE Type = 'ROUTE_RAILROAD';
UPDATE Routes SET Movement = 50, FlatMovement = 50 WHERE Type = 'ROUTE_ROAD';
...
```

**3. Create design doc:** `docs/infrastructure/ROUTE_PROGRESSION_DESIGN.md`

Explains:
- Historical precedent for each tech
- Balance goals (routes shouldn't be strictly dominant over ships)
- Interaction with units (cavalry, knights, trade units)
- Future extension possibilities (canals, airports, hyperloops)

#### Testing

1. Read design doc and verify comments match implementation
2. Change a GameDefine constant and confirm route movement updates correctly
3. Test game progression: verify roads are useful early, rails useful mid-game
4. Verify modders can create custom route progressions

#### Status
**OPEN** — Documentation update only, low risk

---

### Issue 5: Route Planning Strategy Undocumented

**Severity:** MEDIUM  
**Category:** AI Design & Documentation  
**File:** `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp` (lines ~200-350)

#### Problem

Builder AI decides which routes to build (main routes, shortcuts, strategic routes) using hard-coded heuristics without explanation.

**Unanswered Questions:**
- When is a route classified as "main" vs. "shortcut"?
- What distance threshold triggers strategic route planning?
- How are shortcut city pairs selected? (Closest? Highest trade yield?)
- How often are route plans recalculated?
- Why are some routes deprioritized after certain eras?

#### Impact

- **Route overbuilding or underbuilding** depending on map topology
- **Modders cannot override** route priorities (hard-coded logic)
- **Hard to debug** route building decisions in AI games
- **Inconsistent with other AI systems** (no clear strategy document)

#### Root Cause

Route planning uses inline heuristics, not data-driven priorities. Thresholds are magic numbers.

#### Proposed Fix

**1. Add GameDefines for route planning:**

```cpp
// CustomMods.h or GameDefines.sql

#define ROUTE_MAIN_PRIORITY_WEIGHT        100  // Capital to all cities
#define ROUTE_SHORTCUT_PRIORITY_WEIGHT     50  // City pairs close together
#define ROUTE_STRATEGIC_PRIORITY_WEIGHT    75  // Key resources (Iron, Oil, etc.)
#define ROUTE_TRADE_PRIORITY_WEIGHT        30  // High-gold city connections
#define ROUTE_CITY_DISTANCE_SHORTCUT       20  // Tiles; beyond this, not a shortcut
#define ROUTE_RECALC_FREQUENCY             10  // Turns; how often to recalc route plans
```

**2. Document decision tree in code:**

```cpp
/*
 * CvBuilderTaskingAI::UpdateRoutePlots() - Route Priority System
 * 
 * Routes are scored and prioritized in order:
 * 
 * 1. MAIN ROUTES (Priority 100)
 *    - Capital city to ALL other founded cities
 *    - Re-evaluated every turn
 *    - Highest urgency; always built if path available
 *    - Effect: ensures rapid commodity distribution from capital
 * 
 * 2. SHORTCUT ROUTES (Priority 50)
 *    - City pairs within ROUTE_CITY_DISTANCE_SHORTCUT tiles
 *    - Re-evaluated every ROUTE_RECALC_FREQUENCY turns
 *    - Lowers pathfinding cost between frequently-used city pairs
 *    - Effect: military mobilization, trade optimization
 * 
 * 3. STRATEGIC ROUTES (Priority 75)
 *    - Routes to strategic resources (Iron, Oil, Aluminum, Uranium)
 *    - Routes to key tiles for military/cultural victory
 *    - Re-evaluated every ROUTE_RECALC_FREQUENCY turns
 *    - Effect: ensures access to strategic resources early
 * 
 * 4. TRADE ROUTES (Priority 30)
 *    - Routes between high-gold city pairs
 *    - Evaluated if gold per turn drops below threshold
 *    - Lower priority than commodity distribution
 *    - Effect: commerce optimization in mid-late game
 * 
 * Recalculation: Plans are cached and only recalculated every
 * ROUTE_RECALC_FREQUENCY turns or after war/peace changes.
 */
void CvBuilderTaskingAI::UpdateRoutePlots()
{
    // Implementation details...
}
```

**3. Move thresholds to database:**

```sql
INSERT INTO GlobalParameters (ParameterName, ParameterValue) VALUES
    ('ROUTE_MAIN_PRIORITY', 100),
    ('ROUTE_SHORTCUT_PRIORITY', 50),
    ('ROUTE_STRATEGIC_PRIORITY', 75),
    ('ROUTE_TRADE_PRIORITY', 30),
    ('ROUTE_CITY_DISTANCE_SHORTCUT', 20),
    ('ROUTE_RECALC_FREQUENCY', 10);
```

**4. Create design doc:** `docs/infrastructure/ROUTE_PLANNING_STRATEGY.md`

Explains:
- Each route type and when it's built
- How priorities interact with other AI systems
- Performance implications
- How to override for custom civilizations

#### Testing

1. Read design doc and code comments; verify they align
2. Modify a GlobalParameter and confirm route priorities change
3. Create map with isolated cities; verify main routes are prioritized
4. Create map with clusters; verify shortcuts are built between clusters
5. Place strategic resource; verify routes build to it
6. Verify modders can adjust priorities

#### Status
**OPEN** — Documentation and minor code refactoring

---

### Issue 6: Movement Cost Scaling Not Optimized for Unit Types

**Severity:** MEDIUM  
**Category:** Performance & Gameplay Feel  
**File:** `CvGameCoreDLL_Expansion2/CvAStar.cpp`, `CvGameCoreDLL_Expansion2/CvCityConnections.cpp`

#### Problem

Route movement costs are uniform (e.g., all units pay 50 cost to use a road), but some unit types (merchants, trade units, galleys) are historically slower or faster on routes than military units. The current system doesn't account for this.

**Example:**
- A warrior uses a road: 50 movement cost
- A merchant uses the same road: still 50 movement cost (unrealistic)
- A ship uses a road: cannot use it (realistic, but hardcoded)

#### Impact

- **Historical accuracy:** Merchants should move faster on roads (caravans, riverboats)
- **Game balance:** Routes may or may not be useful for specific unit types
- **Pathfinding complexity:** Units that can't use routes take longer paths, slowing AI pathfinding
- **Player confusion:** Why is my merchant slower on the road than in the desert?

#### Root Cause

Movement cost is a property of the route, not a unit-type modifier. No system exists to apply unit-specific movement adjustments to routes.

#### Proposed Fix

**1. Add unit domain modifier to routes:**

```sql
-- RouteTypes table
INSERT INTO Routes (Type, Movement, FlatMovement, GoldMaintenance, DomainMovementModifier) VALUES
    ('ROUTE_ROAD', 50, 50, 0, 100),       -- Land units: 100% (no change)
    ('ROUTE_RAILROAD', 25, 25, 3, 100),   -- Land units: 100%
```

```sql
-- New table: Route_DomainMovementModifiers
CREATE TABLE IF NOT EXISTS Route_DomainMovementModifiers (
    RouteType TEXT,
    DomainType TEXT,
    MovementModifier INT DEFAULT 100  -- 100 = no change, 75 = 25% faster, 125 = 25% slower
);

INSERT INTO Route_DomainMovementModifiers (RouteType, DomainType, MovementModifier) VALUES
    ('ROUTE_ROAD', 'DOMAIN_LAND', 100),      -- Normal
    ('ROUTE_ROAD', 'DOMAIN_SEA', -1),        -- Cannot use
    ('ROUTE_RAILROAD', 'DOMAIN_LAND', 100),
    ('ROUTE_RAILROAD', 'DOMAIN_SEA', -1);
```

**2. Apply modifier in pathfinding:**

```cpp
int CvAStar::GetMovementCost(CvPlot* pFromPlot, CvPlot* pToPlot, CvUnit* pUnit)
{
    RouteTypes eRoute = pToPlot->getRouteType();
    int iBaseCost = GC.getRouteInfo(eRoute).getMovement();
    
    if (eRoute != NO_ROUTE && pUnit)
    {
        int iModifier = GetRouteMovementModifier(eRoute, pUnit->getDomainType());
        if (iModifier == -1)
            return IMPOSSIBLE_MOVE; // Unit cannot use this route
        
        iBaseCost = iBaseCost * iModifier / 100;
    }
    
    return iBaseCost;
}
```

#### Testing

1. Create road with 100% modifier for LAND, −1 for SEA
2. Move a warrior on the road: should use road cost (50)
3. Move a galley on the road: should error or reroute (cannot use)
4. Create modifier 75 (25% faster) for merchants
5. Move a merchant on the road: should use 50 * 75 / 100 = 37.5 cost (verify in logs)
6. Verify pathfinding still works correctly
7. Profile pathfinding performance (should be negligible impact)

#### Status
**OPEN** — Requires code change, database schema update, and testing

---

## Secondary Findings: Route Maintenance Tiers

**File:** `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

### Observation

Road maintenance is currently fixed at 0 GPT (line: `UPDATE Routes SET GoldMaintenance = 0 WHERE Type = 'ROUTE_ROAD';`) while railroads cost 3 GPT. This creates a strong incentive to use roads and deprioritize railroads despite their speed advantage.

### Suggestion

Consider dynamic maintenance based on empire size or era:
```sql
-- Early game: roads free (encourage building)
-- Mid game: roads cost 1 GPT per 5 roads (encourage efficiency)
-- Late game: roads cost 2 GPT per 10 roads (maintenance becomes factor)

-- Alternatively, roads could cost a fixed 0.5 GPT, railroads 2 GPT
-- This would make late-game infrastructure have meaningful cost
```

This is a balance/design decision, not a bug, but worth reviewing.

---

## Summary of Recommendations

### Priority 1: Critical Fixes (Must Fix)
| Issue | Effort | Risk | Impact |
|-------|--------|------|--------|
| Trade route creation war checks | MEDIUM | LOW | HIGH |
| Negative trade route slots | LOW | VERY LOW | HIGH |

### Priority 2: High-Value Improvements (Should Fix)
| Issue | Effort | Risk | Impact |
|-------|--------|------|--------|
| Route maintenance in builder scoring | MEDIUM | MEDIUM | HIGH |
| Tech progression documentation | LOW | VERY LOW | MEDIUM |
| Route planning strategy docs | MEDIUM | LOW | MEDIUM |

### Priority 3: Nice-to-Have (Consider)
| Issue | Effort | Risk | Impact |
|-------|--------|------|--------|
| Unit-type movement modifiers | HIGH | MEDIUM | MEDIUM |
| Route maintenance cost review | MEDIUM | HIGH | LOW |

---

## Cross-References

- [Trade & Economy Review](./economy/TRADE_ECONOMY_REVIEW.md) — Trade route mechanics
- [Improvements & Workers Review](./improvements-workers/IMPROVEMENTS_WORKERS_REVIEW.md) — Builder AI and route planning
- [Unit Movement Review](./unit-movement/IMPLEMENTATION_SUMMARY.md) — Pathfinding for roads/routes
- Vox Populi Mod Files:
  - `(1) Community Patch/Core Files/Overrides/Includes/TradeRouteHelpers.lua`
  - `(2) Vox Populi/Core Files/Overrides/TradeRouteHelpers.lua`
  - `(2) Vox Populi/Database Changes/WorldMap/Improvements/RouteChanges.sql`

---

**End of Review**  
**Next Steps:** See "Summary of Recommendations" for prioritized action plan.

