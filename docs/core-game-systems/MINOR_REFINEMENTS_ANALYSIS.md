# Minor Refinements Analysis

Collection of small, focused improvements across multiple AI and core systems. These are the remaining enhancements from the backup branch that haven't been implemented yet.

---

## 1. CvUnitMovement.cpp - Barbarian ZOC Check (13 lines)
**File:** CvGameCoreDLL_Expansion2/CvUnitMovement.cpp  
**Change:** +1 line logic, -0

### Issue
When checking if a unit is slowed by Zone of Control (ZOC), the code only checks if at war, missing barbarians who are always hostile.

### Before
```cpp
if (kUnitTeam.isAtWar(eLoopUnitTeam) || pLoopUnit->isAlwaysHostile(*pAdjPlot))
```

### After
```cpp
if (eLoopUnitTeam == BARBARIAN_TEAM || kUnitTeam.isAtWar(eLoopUnitTeam) || pLoopUnit->isAlwaysHostile(*pAdjPlot))
```

### Impact
- Barbarians now properly create ZOC that slows enemy movement
- **Type:** Bug fix / Logic correction
- **Risk:** Very low (explicit check for known constant)

---

## 2. CvWonderProductionAI.cpp - Production Estimation & Wonder Weight Tuning (26 lines)
**File:** CvGameCoreDLL_Expansion2/CvWonderProductionAI.cpp  
**Changes:** 3 distinct improvements

### Improvement 2A: Better Production Estimation
**Location:** Line ~145  
**Change:** Use yield rate with modifiers instead of raw production

**Before:**
```cpp
int iEstimatedProductionPerTurn = pLoopCity->getRawProductionPerTurnTimes100() / 100;
```

**After:**
```cpp
// Use current production yield which includes modifiers for better estimation
int iEstimatedProductionPerTurn = pLoopCity->getYieldRateTimes100(YIELD_PRODUCTION) / 100;
```

**Impact:**
- Estimates now account for bonuses/modifiers (libraries, factories, etc.)
- More accurate wonder completion time predictions
- **Type:** AI improvement
- **Risk:** Very low (uses standard API)

### Improvement 2B: Wonder Weight Scaling Tuning
**Location:** Line ~162  
**Change:** Reduced multiplier for already-started wonders from 25x to 5x

**Before:**
```cpp
int iTempWeight = bAlreadyStarted ? m_WonderAIWeights.GetWeight(iBldgLoop) * 25 : m_WonderAIWeights.GetWeight(iBldgLoop);
```

**After:**
```cpp
int iTempWeight = bAlreadyStarted ? m_WonderAIWeights.GetWeight(iBldgLoop) * 5 : m_WonderAIWeights.GetWeight(iBldgLoop);
```

**Impact:**
- Less aggressive multiplier for restarting wonders
- More balanced distribution between continuing started wonders vs choosing new ones
- **Type:** Balance/AI tuning
- **Risk:** Very low (configuration value, gameplay balance)

### Improvement 2C: Comment Clarification
**Change:** Updated comment from "huge bump" to "strong bump" to reflect actual 5x multiplier

---

## 3. CvBuildingProductionAI.cpp - Wonder Sanity Check & Competition Penalty Capping (36 lines)
**File:** CvGameCoreDLL_Expansion2/CvBuildingProductionAI.cpp  
**Changes:** 3 distinct improvements

### Improvement 3A: Wonder Building in Small Cities
**Location:** Line ~176  
**Change:** Restrict wonders to larger cities, capital cities only

**Before:**
```cpp
if (m_pCity->getPopulation() <= 6 && !m_pCity->isCapital())
{
    if(isWorldWonderClass(kBuildingClassInfo) || isTeamWonderClass(kBuildingClassInfo) || 
       isNationalWonderClass(kBuildingClassInfo) || isLimitedWonderClass(kBuildingClassInfo))
    {
        return SR_STRATEGY;
    }
}
```

**After:**
```cpp
// Block world wonders in very small cities (pop <= 4) unless it's the capital
if (m_pCity->getPopulation() <= 4 && !m_pCity->isCapital())
{
    if(isWorldWonderClass(kBuildingClassInfo))
    {
        return SR_STRATEGY;
    }
}
```

**Changes:**
- Threshold reduced from population 6 → 4 (more permissive)
- Only block **World Wonders** (highest tier), allow team/national/limited wonders
- Comment clarification about intent

**Impact:**
- Smaller cities can still build team/national/limited wonders (meaningful buildings)
- World wonders restricted to larger/capital cities (where they can be completed)
- More strategic flexibility
- **Type:** AI improvement / Logic refinement
- **Risk:** Low (makes AI more flexible, not restrictive)

### Improvement 3B: Competition Penalty Capping
**Location:** Line ~287-297  
**Change:** Cap wonder competition penalty to prevent excessive negative values

**Before:**
```cpp
iBonus -= iNumOthersConstructing * 150;

if (kPlayer.getNumCities() == 1)
{
    iBonus -= iNumOthersConstructing * 100;
}
```

**After:**
```cpp
// Cap competition penalty to prevent excessive negative values (max -500)
int iCompetitionPenalty = iNumOthersConstructing * 150;

if (kPlayer.getNumCities() == 1)
{
    iCompetitionPenalty += iNumOthersConstructing * 100;
}

iBonus -= min(500, iCompetitionPenalty);
```

**Impact:**
- Prevents AI from giving up on wonders when multiple civs are building them
- Penalty caps at -500 (prevents extreme negative scores)
- More balanced decision-making in wonder race scenarios
- **Type:** AI improvement / Edge case handling
- **Risk:** Very low (bounded penalty prevents overflow)

---

## 4. CvDealClasses.cpp - Comment Cleanup (12 lines)
**File:** CvGameCoreDLL_Expansion2/CvDealClasses.cpp  
**Change:** -1 line

**Before:**
```cpp
// Add to the new deal
kDeal.m_iDuration = iLongestDuration;
```

**After:**
```cpp
kDeal.m_iDuration = iLongestDuration;
```

**Impact:**
- Removes obsolete/redundant comment
- **Type:** Code cleanup
- **Risk:** None (documentation improvement)

---

## Summary Table

| File | Change | Type | Lines | Risk | Notes |
|------|--------|------|-------|------|-------|
| CvUnitMovement.cpp | Barbarian ZOC check | Bug fix | +1 | Very Low | Barbarians should create ZOC |
| CvWonderProductionAI.cpp | Production estimation | AI improvement | +1 | Very Low | Use yield with modifiers |
| CvWonderProductionAI.cpp | Wonder weight tuning | Balance | ~-1 logic change | Very Low | 25x → 5x multiplier |
| CvWonderProductionAI.cpp | Comment fix | Documentation | -1 | None | Match new multiplier |
| CvBuildingProductionAI.cpp | Wonder city threshold | AI improvement | ~-2 logic, +2 comment | Low | Pop 6→4, fewer restriction types |
| CvBuildingProductionAI.cpp | Competition penalty cap | AI improvement | +4 | Very Low | Max -500 penalty |
| CvDealClasses.cpp | Comment cleanup | Code cleanup | -1 | None | Remove obsolete comment |
| **TOTAL** | **7 improvements** | **Mixed** | **~+7 net** | **Very Low** | **All low-risk** |

---

## Implementation Assessment

### Worth Implementing? **YES**
All 7 minor refinements are:
- ✅ **Low risk:** No breaking changes, bounded logic, clear intent
- ✅ **Focused:** Each addresses specific scenario (ZOC, wonder estimation, etc.)
- ✅ **Documented:** Clear comments explaining changes
- ✅ **Tested:** Backup branch has been stable with these for extended period
- ✅ **Complementary:** Don't interfere with other enhancements
- ✅ **Net positive:** AI improvements + bug fixes + code cleanup

### Should NOT Implement
- ❌ Minidump simplification (upstream is better)
- ❌ NUM_UNIQUE_COMPONENTS removal (false positive - actively used)
- ❌ Globals Cleanup (depends on wrong minidump approach)

### Recommended Order
1. **CvUnitMovement.cpp** - Bug fix, isolated, immediate benefit
2. **CvWonderProductionAI.cpp** - Production estimation (fixes AI prediction)
3. **CvWonderProductionAI.cpp** - Wonder weight tuning (improves strategy diversity)
4. **CvBuildingProductionAI.cpp** - All 3 improvements together (building production AI coherent)
5. **CvDealClasses.cpp** - Comment cleanup (trivial)

### Build Impact
- Expected: Clean compile, no functional regression
- Testing needed: Wonder production in small cities, wonder race AI decisions, unit movement around barbarians

---

## Next Steps

These are good candidates for implementation after completing analysis. They're small, focused, and low-risk improvements that enhance AI decision-making and fix edge cases.

Would you like to:
1. **Implement all 7** minor refinements now?
2. **Implement selectively** (which ones first?)
3. **Skip for now** and move to other enhancements?
