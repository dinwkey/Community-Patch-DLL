# Route Strategic Weighting Analysis: Option A vs Option B

**Date:** January 11, 2026  
**Context:** Enhancement decision for Issue 3 (railroad strategic value)  
**Status:** Analysis for future reference

## Overview

After implementing the base Issue 3 fix (movement speed bonus for railroads), identified need for strategic location weighting. Two approaches were analyzed:

- **Option A:** Location-based multiplier in Stage 2 (road/railroad selection)
- **Option B:** Strategic route planning in Stage 1 (which routes to plan)

---

## Current Architecture

### Stage 1: Route Planning
Routes are pre-planned based on economic purpose:
- Connect to capital for gold
- Shortcut between nearby cities
- Connect strategic resources
- Connect to trade partners

Routes stored in:
- `m_plannedRoutePurposes` - Purpose of each route
- `m_plannedRouteAdditiveValues` - Initial economic values
- `m_plannedRouteNonAdditiveValues` - Non-additive bonuses

### Stage 2: Route Type Selection (Current Location)
For each already-planned route, decide: **ROAD or RAILROAD?**

Current flow:
1. Get initial route values (economic)
2. Subtract maintenance costs
3. Apply movement bonus (uniform across all routes)
4. Compare: RAILROAD if value > ROAD

---

## Option A: Location-Based Multiplier (Stage 2)

### Concept
Add location-based weighting to `iMovementSpeedBonus` when selecting route type.

### Implementation Location
`GetRouteDirectives()` around lines 1720-1780

### What It Does
Modify movement bonus based on plot location:
```cpp
// Pseudocode
int iStrategicMultiplier = EvaluateLocationValue(plotPair);
iMovementSpeedBonus = (iMovementSpeedBonus * (100 + iStrategicMultiplier)) / 100;
```

Location factors:
- Distance to enemy borders (closer = higher multiplier)
- Number of nearby enemies
- Whether location is in threatened city radius
- Whether plot is in choke point
- Whether route is defending colony/forward settlement

### Pros ✅
- **Simple:** Just modify one parameter
- **Low Risk:** Only affects one decision point
- **Easy Testing:** Fewer variables to verify
- **Fast:** Can complete in 2-4 hours
- **Easy Debugging:** Isolated changes
- **No Side Effects:** Doesn't affect other systems
- **Fits Current Scope:** Natural extension of Issue 3

### Cons ❌
- **Limited Scope:** Only decides ROAD vs RAILROAD, not WHETHER to build
- **Symptom Treatment:** Doesn't fix bad route planning
- **Can't Prioritize:** Critical routes built same priority as non-critical
- **Inefficient:** Might build unimportant railroads while critical roads incomplete
- **Late Stage:** Route already planned; we only tweak type

### Example Impact
```
Bad Route Planned: Farm connection in peaceful interior
Option A Result: Builds RAILROAD instead of ROAD (doesn't help much)
Better Result: Never plan the route in first place
```

### Risk Assessment: **VERY LOW**
- Only adds multiplication to existing bonus
- Location check is read-only (no state changes)
- Graceful degradation if location data unavailable

---

## Option B: Strategic Route Planning (Stage 1)

### Concept
Increase initial route values for strategically important paths. Routes near conflicts get higher base values, get planned first, get built first.

### Implementation Location
Route planning functions (earlier in call stack):
- `UpdateRoutePlots()` - Main planning function
- `ConnectCitiesForStrategy()` - Strategic connection logic
- `AddRoutePlots()` - Route value assignment

### What It Does
Modify initial route values based on strategic importance:
```cpp
// Pseudocode
int iStrategicBonus = EvaluateRouteStrategy(
    pOriginCity, pTargetCity, 
    eRoute,  // ROAD or RAILROAD
    m_pPlayer
);
iInitialValue += iStrategicBonus;
```

Strategic bonuses for:
- Routes defending threatened cities (+200-500 points)
- Routes near enemy borders (+200-300 points)
- Routes through choke points (+100-200 points)
- Routes connecting forward settlements (+150-400 points)
- Routes enabling offensive positioning (+100-300 points)

### Pros ✅
- **Comprehensive:** Affects which routes get planned
- **Root Cause Fix:** Prioritizes building critical routes first
- **Better Allocation:** Limited builders focused on strategic needs
- **Systemic:** Aligns with existing builder AI strategy system
- **Flexible:** Can apply to both ROAD and RAILROAD equally
- **War Responsive:** Routes near threats get prioritized earlier
- **Long-term Value:** Affects all future planning

### Cons ❌
- **Complex:** Requires deep understanding of planning system
- **High Risk:** Changes affect multiple decision points
- **Hard Testing:** Many variables and interactions to verify
- **Long Development:** 1-2 weeks instead of 2-4 hours
- **Fragile:** More likely to break existing AI behavior
- **Requires New Code:** Location analysis functions needed
- **Harder Debugging:** Complex interactions with existing logic

### Example Impact
```
Current Behavior:
  Routes Planned: 1. Border, 2. Trade, 3. Interior
  All planned equally; we tweak later ones to roads

Option B Result:
  Routes Planned: 1. Border (strategic), 2. Trade, 3. Interior
  Border gets built first with railroad; others with roads
```

### Risk Assessment: **MEDIUM-HIGH**
- Changes affect route planning system (complex)
- Interactions with trade route logic possible
- Needs extensive testing across scenarios
- Could cause builders to ignore profitable routes

---

## Comparison Table

| Factor | Option A | Option B |
|--------|----------|----------|
| **Development Time** | 2-4 hours | 1-2 weeks |
| **Complexity** | Low | High |
| **Risk** | Very Low | Medium-High |
| **Scope** | Tactical (type selection) | Strategic (route prioritization) |
| **Testing Effort** | Low (4 test cases) | High (20+ scenarios) |
| **Maintenance** | Easy | Complex |
| **Effectiveness** | 60-70% | 85-95% |
| **Side Effects** | Unlikely | Possible |
| **Debugging Difficulty** | Easy | Very Hard |
| **Impact on Gameplay** | Moderate | Significant |
| **Fits Current Issue** | Yes | No (larger scope) |
| **Dependencies** | None | Location analysis system |

---

## Technical Considerations

### Option A Implementation Details

**Location Evaluation Needed:**
```cpp
// Calculate distance to nearest enemy
int iNearestEnemyDistance = GetDistanceToNearestEnemy(plotPair);
// Returns: >10 tiles = safe, 5-10 = moderate threat, <5 = critical

// Check for threatened cities
int iThreatenedCitiesNearby = CountThreatenedCitiesNearby(plotPair);
// Returns: 0-3 count of friendly cities under threat

// Evaluate if choke point
bool bIsChokePoint = IsStrategicChokePoint(plotPair);
// Returns: true if route critical for movement

// Calculate multiplier (0-100 percentage increase)
int iStrategicMultiplier = 0;
if (iNearestEnemyDistance < 5) iStrategicMultiplier += 50;
if (iThreatenedCitiesNearby > 0) iStrategicMultiplier += 25 * iThreatenedCitiesNearby;
if (bIsChokePoint) iStrategicMultiplier += 30;

// Cap at reasonable value
iStrategicMultiplier = min(iStrategicMultiplier, 100);  // Max 2x bonus
```

**Existing Functions Available:**
- `IsChokePoint()` - Already exists for plots
- `IsThreatenedByBarbarians()` - City threat check available
- `plotDistance()` - Distance calculation available
- `GetDiplomacyAI()->GetMilitaryAggressivePosture()` - Already used in current code

**No New Dependencies:** All required functions already exist in codebase

### Option B Implementation Details

**Would Require New Functions:**
```cpp
// New strategic analysis system needed
int EvaluateRouteStrategy(
    CvCity* pOriginCity,
    CvCity* pTargetCity,
    RouteTypes eRoute,
    CvPlayer* pPlayer
);

// Would need to analyze:
// - Territory control around route
// - Strategic value of cities
// - Enemy positioning relative to route
// - Long-term strategic goals
```

**Complex Interactions With:**
- Route planning algorithm (main loop)
- City prioritization system
- Strategic focus system (if present)
- Military positioning AI
- Resource allocation system

**Would Require Extensive Testing:**
- Different map types
- Different era stages
- Different civilization types
- Different AI difficulties
- Multiplayer scenarios
- Multiple simultaneous threats

---

## Recommendation

### Immediate Action: **Implement Option A**
**Rationale:**
- Extends current Issue 3 work naturally
- Low risk, high confidence
- Can ship in 2-4 hours
- Provides immediate gameplay improvement
- Foundation for Option B work

**Time Estimate:** 2-4 hours implementation + 1 hour build + 1 hour testing = 4-6 hours total

**Risk Level:** VERY LOW (isolated change)

### Future Action: **Plan Option B for Next Phase**
**When to Do It:**
- After Issue 3 ships and is validated
- As separate issue/feature (Issue 4 or later)
- Requires dedicated analysis phase first
- Should include comprehensive test plan

**Time Estimate:** 1-2 weeks for full implementation and testing

**Preparation:**
- Study route planning system in depth
- Document existing strategic logic
- Design new strategic analysis system
- Create comprehensive test scenarios

---

## Decision History

**Date:** January 11, 2026  
**Situation:** Issue 3 implemented with uniform movement bonus
**Question:** Should movement bonus consider strategic location?
**Analysis:** Two approaches identified with different tradeoffs
**Decision:** Option A recommended for immediate value; Option B for future strategic improvement

---

## Related Issues

- **Issue 1:** Trade route war validation (separate, defensive fix)
- **Issue 2:** Negative trade route slots (separate, clamp fix)
- **Issue 3:** Route maintenance & strategic value (current implementation)
- **Issue 4+:** Future enhancements (could include Option B analysis)

---

## Future References

When implementing Option A, reference:
- `GetDistanceToNearestEnemy()` or similar existing method
- `IsChokePoint()` existing implementation
- Military posture calculation (lines 1748-1770)
- Multiplier application pattern (existing in code)

When planning Option B, reference:
- `UpdateRoutePlots()` function structure
- `m_plannedRouteAdditiveValues` storage
- Strategic bonus patterns in existing code
- City threat detection system

