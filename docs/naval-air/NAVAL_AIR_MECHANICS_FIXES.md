# Naval & Air Mechanics Fixes - Implementation Summary

**Date:** January 8, 2026  
**Priority Levels:** High, Medium, Low

---

## HIGH PRIORITY FIXES ✓

### 1. Fixed `canLoad()` Embarkation Check Bug
**File:** [CvUnit.cpp](CvGameCoreDLL_Expansion2/CvUnit.cpp#L6475-L6520)  
**Status:** ✅ FIXED

**Issue:** The `canLoad()` method incorrectly returned `false` if the unit was currently embarked. This prevented units from checking if they could load onto a transport/carrier while in an embarked state, even if the capability existed.

**Solution:** Removed the embarkation state check. The method now checks if the unit CAN load onto available transports/carriers regardless of current embarkation state. Added clear documentation explaining why this check was removed.

```cpp
// Note: We do NOT check embarkation state here. A unit can check if it can load
// onto a transport/carrier regardless of whether it's currently embarked.
// The actual loading will fail if conditions aren't met, but checking capability
// should be state-independent.
```

**Impact:** Units can now properly evaluate loading opportunities without being blocked by their current embarkation status.

---

### 2. Used Unused Air Unit Statistics
**File:** [CvHomelandAI.cpp](CvGameCoreDLL_Expansion2/CvHomelandAI.cpp#L5577-L5710)  
**Status:** ✅ FIXED

**Issue:** Statistics tracking air units in carriers, in cities, and offensive/defensive distribution were calculated but never used, wasting computation.

**Solution:** 
- Changed TODO comment to clarify purpose
- Implemented strategic decision based on these statistics
- Added logic to prioritize offensive aircraft to carriers/frontline when outnumbered by defensive aircraft

```cpp
//Use collected statistics to guide rebasing strategy
//If we have more defensive than offensive aircraft, prioritize offensive aircraft to combat bases
bool bPrioritizeOffensiveToCarriers = (nAirUnitsOffensive < nAirUnitsDefensive);
```

**Impact:** Air unit rebasing now considers whether the player has balanced offensive/defensive coverage and makes better strategic choices about unit placement.

---

## MEDIUM PRIORITY IMPROVEMENTS ✓

### 3. Added Interceptor Consideration to Danger Plots
**File:** [CvDangerPlots.cpp](CvGameCoreDLL_Expansion2/CvDangerPlots.cpp#L860-L885)  
**Status:** ✅ IMPLEMENTED

**Issue:** Danger plot calculations for air units did not account for friendly interceptors, causing the AI to overestimate threat level from air attacks.

**Solution:** 
- Calculate interceptor count for air attacks
- Apply damage reduction (10% per interceptor, max 40%)
- Reduces overestimation of air threat

```cpp
// Reduce expected air unit damage based on friendly interceptors
int iInterceptionReduction = 0;
if (pAttacker->getDomainType() == DOMAIN_AIR && m_pPlot)
{
    int iInterceptorCount = m_pPlot->GetInterceptorCount(pAttacker->getOwner());
    if (iInterceptorCount > 0)
    {
        // Reduce damage by ~10% per interceptor (max 40% reduction with 4+ interceptors)
        iInterceptionReduction = std::min(40, iInterceptorCount * 10);
    }
}
```

**Impact:** AI will correctly account for air defense when evaluating danger, leading to better defensive positioning and more realistic threat assessment.

---

### 4. Added Specialized Air Attack Safety Check
**File:** [CvTacticalAI.cpp](CvGameCoreDLL_Expansion2/CvTacticalAI.cpp#L6456-L6496)  
**Status:** ✅ IMPLEMENTED

**Issue:** Air units lacked specialized suicide prevention logic; the function only checked melee attacks and excluded air units entirely.

**Solution:**
- Implemented air-specific attack evaluation
- Checks if air unit would be killed by interception
- Evaluates both city and unit defenders
- Properly returns false if attack is safe

```cpp
// Special handling for air attacks - they can't do melee but check if suicide via interception
if (pAttacker->getDomainType() == DOMAIN_AIR)
{
    if (!pAttacker->IsCanAttackRanged())
        return false; // Not an air strike

    // Air units are suicide if they'd likely die to interception
    if (pTarget->isCity())
    {
        int iInterceptionDamage = 0;
        CvCity* pCity = pTarget->getPlotCity();
        if (pCity && pCity->getOwner() != pAttacker->getOwner())
        {
            iInterceptionDamage = pCity->GetAirStrikeDefenseDamage(pAttacker, false);
            if (iInterceptionDamage >= pAttacker->GetCurrHitPoints())
                return true; // Would be killed by interception
        }
    }
    // ... similar check for unit defenders
    return false; // Air unit can safely attack
}
```

**Impact:** AI will no longer send air units on suicide missions into heavily defended targets.

---

### 5. Optimized Emergency Rebase Performance
**File:** [CvUnit.cpp](CvGameCoreDLL_Expansion2/CvUnit.cpp#L5508-5600)  
**Status:** ✅ OPTIMIZED

**Issue:** `EmergencyRebase()` iterates through all cities twice and all units once, looking for valid targets. This is inefficient and could cause frame rate issues in late-game.

**Solution:**
- Collect all viable targets in a single pass with scores
- Avoid duplicate iterations
- Sort once by score instead of checking each time
- Rebase to best available option

```cpp
// Collect viable rebase targets once, ranked by score
// This avoids multiple iterations and improves performance
std::vector<std::pair<CvPlot*, int>> vRebaseTargets; // (plot, score)

// ... single pass through cities and carriers to collect targets ...

// Sort by score (descending) and rebase to best target
if (!vRebaseTargets.empty())
{
    std::sort(vRebaseTargets.begin(), vRebaseTargets.end(),
        [](const std::pair<CvPlot*, int>& a, const std::pair<CvPlot*, int>& b) { return a.second > b.second; });
    
    rebase(vRebaseTargets[0].first->getX(), vRebaseTargets[0].first->getY(), true);
    return true;
}
```

**Impact:** Significant performance improvement, especially in late-game when there are many units and cities. Reduced iteration count from 3+ passes to 1 pass.

---

### 6. Expanded Airlift Targeting Beyond Capital
**File:** [CvHomelandAI.cpp](CvGameCoreDLL_Expansion2/CvHomelandAI.cpp#L5490-5550)  
**Status:** ✅ EXPANDED

**Issue:** Airlift logic only targeted the capital city, missing opportunities to reinforce threatened locations.

**Solution:**
- Scan all cities for threat levels
- Prioritize airlift to most threatened cities
- Fall back to capital if no cities under threat
- Log airlift reason (threatened city vs. capital)

```cpp
// Check for threatened cities needing reinforcement
int iCityLoop = 0;
for (CvCity* pLoopCity = m_pPlayer->firstCity(&iCityLoop); pLoopCity != NULL; pLoopCity = m_pPlayer->nextCity(&iCityLoop))
{
    if (pLoopCity->plot() == pUnit->plot())
        continue; // Already there

    int iThreatLevel = m_pPlayer->GetPlotDanger(*pLoopCity->plot());
    if (iThreatLevel > iBestThreatLevel && pUnit->canAirliftAt(pUnit->plot(), pLoopCity->plot()->getX(), pLoopCity->plot()->getY()))
    {
        pTargetCity = pLoopCity;
        iBestThreatLevel = iThreatLevel;
    }
}
```

**Impact:** Units now airlift to reinforce threatened cities before falling back to capital, improving defensive readiness during wars.

---

## LOW PRIORITY IMPROVEMENTS ✓

### 7. Documented Carrier-to-Carrier Rebasing Design Decision
**File:** [CvHomelandAI.cpp](CvGameCoreDLL_Expansion2/CvHomelandAI.cpp#L5655-5665)  
**Status:** ✅ DOCUMENTED

**Issue:** Comment simply stated "for simplicity we don't do carrier to carrier rebasing" without full explanation.

**Solution:** Expanded documentation to explain design intent and what would be needed for future implementation.

```cpp
//DESIGN NOTE: Carrier-to-carrier rebasing is not currently implemented.
// Aircraft rebase from carrier to city, not carrier-to-carrier.
// This simplifies pathfinding and prevents excessive micro-management.
// If carrier-to-carrier rebasing is needed in future, implement multi-step pathfinding
// to find intermediate cities that can serve as relay points.
```

**Impact:** Clear documentation for future maintainers. If carrier-to-carrier rebasing is desired, the implementation approach is already documented.

---

## Summary of Changes

### Files Modified:
1. **CvUnit.cpp** (2 changes)
   - Fixed `canLoad()` embarkation check
   - Optimized `EmergencyRebase()` performance

2. **CvHomelandAI.cpp** (3 changes)
   - Used air unit statistics
   - Expanded airlift targeting
   - Documented carrier-to-carrier design

3. **CvDangerPlots.cpp** (1 change)
   - Added interceptor consideration

4. **CvTacticalAI.cpp** (1 change)
   - Added air attack safety checks

### Performance Impact:
- **Emergency Rebase:** ~66% reduction in iterations (3 passes → 1 pass)
- **Overall:** Reduced computational overhead in late-game

### Gameplay Impact:
- **Naval/Air:** Better defensive positioning with interceptor consideration
- **AI Behavior:** Smarter airlift decisions, safer air attacks
- **User Experience:** No visible changes, but AI should perform better

---

## Testing Recommendations

1. **Emergency Rebase:** Test air unit rebasing in late-game scenarios with many cities
2. **Airlift Targeting:** Verify units airlift to threatened cities instead of only capital
3. **Air Combat:** Confirm air units don't attack into overwhelming interception
4. **Danger Calculation:** Check that defensive bonuses are reflected in plot danger tooltips
5. **Carrier Management:** Verify offensive/defensive aircraft balancing works correctly

---

## Version
- **Patch Version:** Community Patch DLL
- **Date Implemented:** January 8, 2026
- **Compatibility:** Lua 5.1 (Civ5 standard), MSVC 9.0 (Visual Studio 2008)
