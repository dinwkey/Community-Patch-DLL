# Option B Deep Dive: Strategic Route Planning Architecture Analysis

**Date:** January 11, 2026  
**Context:** Planning for enhanced strategic route planning (Stage 1) after Option A completion  
**Research Status:** Codebase investigated for military-builder coordination

---

## Key Finding: No Direct Military-Builder Coordination

After investigating the codebase, **there is NO built-in coordination between military operations and builder tasking** for road/railroad repair or construction during combat or after city capture.

### Evidence:

1. **Builder Update Flow** (`Update()` function, lines 141-188):
   - Calls `UpdateRoutePlots()` 
   - Calls `UpdateCanalPlots()`
   - Calls `UpdateImprovementPlots()`
   - **No checks for:** active wars, city captures, unit losses, military operations

2. **Route Planning** (`UpdateRoutePlots()`, lines 1444-1600):
   - Plans routes based on city population and connectivity
   - Sorts cities by population (lines 1469-1483)
   - Connects cities for trade bonuses (lines 1515-1527)
   - **No checks for:** military threats, enemy proximity, siege status

3. **Route Value Calculation**:
   - Based on economic connectivity (GetCapitalConnectionValue)
   - Based on gold from trade villages
   - **No checks for:** military unit positioning, war status, unit movement benefits

4. **Priority Ordering** (`GetDirectives()`, lines 1235):
   - Returns `m_directives` array (pre-calculated)
   - No re-prioritization based on military events
   - No distinction between combat-critical and peaceful construction

5. **Repair Logic**:
   - Repairs happen alongside normal improvement tasks
   - No special priority boost for war-time repairs
   - No correlation with unit movement needs near combat zones

---

## What DOES Exist

### Positive Findings:
✅ **Route purposes are tracked:** `m_plannedRoutePurposes` distinguishes:
  - PURPOSE_CONNECT_CAPITAL
  - PURPOSE_SHORTCUT  
  - PURPOSE_CONNECT_MAJOR_RESOURCE
  - PURPOSE_CONNECT_STRATEGIC (exists but maybe underutilized)

✅ **Strategic routes are explicitly mentioned:** Code comments reference "Connect cities to capital and plan strategic routes" (line 1598)

✅ **Military pressure weighting exists** (from our Option A implementation):
  - DiplomacyAI provides GetMilitaryAggressivePosture()
  - Can be queried for threat assessment

✅ **Builder directives are scored:** Each directive has a score (m_iScore) and potential bonus (m_iPotentialBonusScore)

### Limitations:
❌ **No dynamic re-prioritization:** Once routes are planned in UpdateRoutePlots(), their relative importance doesn't change based on military events

❌ **No war-time adjustments:** Route values calculated once per turn, not recalculated when:
  - War declared
  - Unit losses occur
  - Cities captured or lost
  - Military units positioned for attack

❌ **No location-based strategic routes:** Strategic routes (PURPOSE_CONNECT_STRATEGIC) exist but aren't prioritized based on defensive needs

❌ **No coordination with military AI:** Builder AI doesn't query:
  - Which cities are under siege
  - Where armies are positioned
  - Which routes would benefit combat operations
  - When emergency infrastructure is needed

---

## Implications for Option B Implementation

### Current State: Economic-Driven Planning
```
UpdateRoutePlots():
  Sort cities by population (connectivity value)
  For each city pair:
    Calculate trade value
    Calculate movement bonus (generic)
    Decide: Road or Railroad based on economic gain
  Return: Ordered list of routes to build
```

### Option B Goal: Strategic Route Planning
```
UpdateRoutePlots() (Enhanced):
  Identify cities under threat or in war zones
  Calculate strategic importance for each city pair
  Add war-time bonuses to high-value routes
  Re-prioritize based on:
    - Enemy proximity
    - Military unit positioning
    - Defensive terrain control
    - Access to rally points
  Return: Strategically prioritized route list
```

---

## Technical Considerations for Option B

### 1. Where to Hook Strategic Analysis

**Option B-1: In UpdateRoutePlots() (Recommended)**
- Add strategic evaluation BEFORE route planning
- Identify threatened cities and strategic chokepoints
- Weight route pairs accordingly
- Minimal impact on existing logic

**Option B-2: In route value calculation (GetCapitalConnectionValue, lines 708-790)**
- Modify route values based on strategic location
- Consider enemy proximity when calculating trade value
- Requires more extensive changes

**Option B-3: In GetDirectives() (Most Invasive)**
- Re-sort directives based on military status each turn
- Most flexible but most risky
- Could break existing directive caching

### Recommendation: **Option B-1** (In UpdateRoutePlots)

---

## Required Analysis Functions for Option B

Would need to implement:

```cpp
// 1. Identify threatened cities
vector<CvCity*> GetThreatenedCities();
  // - Cities near enemy units
  // - Cities with low garrison
  // - Cities being razed or bombarded

// 2. Calculate route strategic value
int CalculateRouteStrategicValue(CvCity* pCity1, CvCity* pCity2, CvPlayer* pPlayer);
  // - Are both cities friendly?
  // - Is either city threatened?
  // - Does route enable troop concentration?
  // - Does route provide defensive coverage?

// 3. Identify strategic choke points
bool IsStrategicChokePoint(CvPlot* pPlot);
  // - Narrow passages through terrain
  // - Key mountain passes
  // - River crossing points
  // - Gateways to enemy territory

// 4. Assess military pressure
int GetRegionalMilitaryThreat(CvCity* pCity);
  // - Nearby enemy units
  // - Distance to enemy borders
  // - Enemy army composition
  // - Historical aggression patterns
```

---

## Data Available for Strategic Analysis

✅ **Existing game queries available:**
- `pCity->getPopulation()` - City size
- `pCity->IsRazing()` - Under siege  
- `plotDistance()` - Distance calculations
- `GET_PLAYER(eEnemy).getUnitCount()` - Army size
- `m_pPlayer->GetDiplomacyAI()->GetMilitaryAggressivePosture()` - Threat level (from Option A)
- `pPlot->IsChokePoint()` - If it exists in game (verify)
- `pCity->getDefenseModifier()` - Defensive strength

❌ **Would need new analysis:**
- Unit positioning relative to routes
- Expected attack vectors
- Strategic reserve rally points
- Supply line analysis

---

## Planning Effort Estimate for Option B

### Phase 1: Infrastructure (2-3 weeks)
- Implement threat identification system
- Implement strategic value calculator
- Add military pressure assessment
- Create comprehensive test framework

### Phase 2: Integration (1-2 weeks)
- Hook into UpdateRoutePlots()
- Modify route value calculation
- Adjust route purpose weighting
- Verify no performance regression

### Phase 3: Tuning & Balance (2-3 weeks)
- Test against multiple AI opponents
- Test different map types
- Test different eras and unit compositions
- Fine-tune strategic value weights

### Phase 4: Validation (1-2 weeks)
- Regression testing
- Edge case handling
- Performance profiling
- Documentation

**Total Estimate: 6-10 weeks**

---

## Risk Assessment for Option B

**HIGH RISKS:**
- ❌ Could cause AI to starve cities of improvements (over-focus on routes)
- ❌ Could cause route spam during wars (resource waste)
- ❌ Could cause performance issues (more calculations per turn)
- ❌ Could break existing route planning logic
- ❌ Requires extensive testing across scenarios

**MODERATE RISKS:**
- ⚠️ Tuning constants will be difficult
- ⚠️ Different map types might have different requirements
- ⚠️ Multiplayer balance different from single-player

**LOW RISKS:**
- ✅ Option A provides foundation (already tested)
- ✅ Existing route purpose system can be leveraged
- ✅ Military data already available in game

---

## Comparison: Current vs Option A vs Option B

| Aspect | Current | + Option A | + Option B |
|--------|---------|-----------|-----------|
| **Economic prioritization** | ✅ Yes | ✅ Yes | ✅ Yes |
| **Military value weighting** | ❌ No | ✅ 500-750 pts | ✅ Variable by location |
| **Strategic location aware** | ❌ No | ✅ Yes (bonus) | ✅ Yes (in planning) |
| **War-time prioritization** | ❌ No | ✅ Passive | ✅ Active |
| **Threatened city defense** | ❌ No | ❌ No | ✅ Yes |
| **Dynamic re-prioritization** | ❌ No | ❌ No | ✅ Yes |
| **Development effort** | - | 4-6 hours | 6-10 weeks |
| **Testing effort** | - | Low | High |
| **Risk level** | - | Very Low | Medium-High |

---

## Recommended Path Forward

### Immediate (Next Session)
1. ✅ **Option A completed and tested** - Location-based weighting live
2. Run 5+ test games with Option A to validate balance
3. Collect metrics on railroad building patterns

### Short-term (Next 2-4 weeks)
4. Decide if Option A is "good enough" or if Option B is needed
5. If Option B desired: Start Phase 1 (infrastructure design)

### Medium-term (Weeks 4-10, if pursuing Option B)
6. Implement threatened city detection
7. Implement route strategic value calculation
8. Hook into UpdateRoutePlots()
9. Extensive testing and tuning

### Alternative: Hybrid Approach
- Keep Option A as-is (quick win)
- Implement *only* threatened city bonus in Option B (simpler)
- Skip full strategic analysis (reduces complexity)
- Estimated effort: 2-3 weeks instead of 6-10

---

## Questions for Next Session

1. **Is Option A sufficient?** Test games will show if movement bonus alone drives good AI behavior
2. **Priority:** How important is war-time route prioritization vs economic routing?
3. **Scope:** Would threatened city bonus be valuable, or do we need full strategic analysis?
4. **Timeline:** Is this a short-term (next month) or long-term (next year) feature?

---

## Related Code References

- **Route planning:** `UpdateRoutePlots()` (line 1444)
- **Route value:** `GetCapitalConnectionValue()` (line 708)
- **City connections:** `CvCityConnections` class
- **Threat assessment:** `GetDiplomacyAI()->GetMilitaryAggressivePosture()` 
- **Movement speed calculation:** `GetMoveCostWithRoute()` (line 200)
- **Strategic routes:** PURPOSE_CONNECT_STRATEGIC (enum, location TBD)

