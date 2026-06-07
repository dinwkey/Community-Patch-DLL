# Carrier-to-Carrier Rebasing Implementation

## Overview
This document describes the implementation of carrier-to-carrier air unit rebasing in the Community Patch DLL.

## Feature Description
Air units can now rebase directly between carriers in addition to rebasing to cities. This extends the existing rebase system to treat carriers as first-class base destinations, improving AI decision-making for fleet positioning and air unit mobility.

## Implementation Details

### 1. Carrier Detection in Base Selection
**File:** `CvHomelandAI.cpp` (lines 5645-5672)

Carriers with air cargo capability are now included in the potential bases list alongside cities:

```cpp
else if (pLoopUnit->domainCargo() == DOMAIN_AIR)
{
    nSlotsInCarriers += pLoopUnit->cargoSpace();
    
    // Attach carrier to nearest city for multi-step pathfinding support
    CvCity* pRefCity = m_pPlayer->GetClosestCityByPlots(pLoopUnit->plot());
    if (pRefCity)
    {
        pRefCity->AttachUnit(pLoopUnit);
        // Logging...
    }
    
    int iScore = HomelandAIHelpers::ScoreAirBase(pLoopUnitPlot, m_pPlayer->GetID(), false, iAssumedRange);
    vPotentialBases.push_back(SPlotWithScore(pLoopUnitPlot, iScore));
    scoreLookup[pLoopUnitPlot->GetPlotIndex()] = iScore;
}
```

**Key Features:**
- Carriers are scored using the same `ScoreAirBase()` function as cities
- Each carrier is attached to its nearest city for multi-step pathfinding support
- Logging documents the carrier-to-city attachment

### 2. Rebase Pathfinding
**File:** `CvHomelandAI.cpp` (lines 5813-5843)

The pathfinding logic automatically uses `PT_AIR_REBASE` pathfinder for finding routes to carrier destinations:

```cpp
// Direct rebase attempt
if (pUnit->canRebaseAt(it->pPlot->getX(), it->pPlot->getY()))
{
    pNewBase = it->pPlot;
    // ...direct rebase successful
}

// Multi-step pathfinding fallback via PT_AIR_REBASE
else if (pUnit->canRebase())
{
    CvAStarNode* pNode = GC.getAirPathFinder()->GetLastNode();
    if (GC.getAirPathFinder()->FindPath(pUnit->plot(), it->pPlot, pUnit, PT_AIR_REBASE))
    {
        // Multi-step rebase path found
    }
}
```

**Key Features:**
- `canRebaseAt()` in CvUnit.cpp already supported carrier loading (no changes needed)
- `PT_AIR_REBASE` pathfinder enables multi-step routing through intermediate cities if necessary
- The pathfinder automatically respects carrier attachment assignments

### 3. Enhanced Logging
**File:** `CvHomelandAI.cpp` (lines 5858-5880)

The logging system now distinguishes between carrier and city rebase destinations:

```cpp
// Determine if rebasing to a carrier or city for logging
bool bToCarrier = false;
IDInfo* pUnitNode = pNewBase->headUnitNode();
while(pUnitNode != NULL)
{
    CvUnit* pCarrier = ::GetPlayerUnit(*pUnitNode);
    pUnitNode = pNewBase->nextUnitNode(pUnitNode);
    if (pCarrier && pCarrier->domainCargo() == DOMAIN_AIR)
    {
        bToCarrier = true;
        break;
    }
}

if (bToCarrier)
    strLogString.Format("Rebasing %s (%d) from %d,%d to carrier at %d,%d for combat (score %d)", ...);
else
    strLogString.Format("Rebasing %s (%d) from %d,%d to %d,%d for combat (score %d)", ...);
```

**Key Features:**
- Combat unit rebasing logs show "to carrier" or "to city" destination
- Logging helps with debugging and understanding AI behavior
- Same pattern can be applied to healing unit rebasing logs

## Backward Compatibility
✅ **Fully backward compatible** - All changes are additive:
- Existing city-based rebasing continues to work
- Existing canRebaseAt() logic already supported carriers
- No API changes or breaking modifications
- Default behavior preserves all original functionality

## Verification

### Compilation Status
- ✅ CvHomelandAI.cpp: No compilation errors
- ✅ CvUnit.cpp: No compilation errors
- ✅ All changes MSVC 2008 compatible (C++03 standard)

### Testing Checklist
- [ ] Air units rebase directly to closest available carrier
- [ ] Combat units prioritize carriers when strategy calls for offensive positioning
- [ ] Healing units rebase to safe carriers when needed
- [ ] Multi-step pathfinding works for distant carrier destinations
- [ ] Airlift operations still function for threatened cities
- [ ] Emergency rebase performs well with large carrier fleets

## Design Notes

### Why Attach Carriers to Cities?
Carriers are attached to their nearest city to leverage the existing multi-step pathfinding system. This enables:
1. Finding paths through intermediate cities when direct rebase isn't available
2. Ensuring aircraft can reach distant carriers via relay points
3. Maintaining compatibility with existing pathfinding infrastructure

### Strategy Integration
The existing strategy flag `bPrioritizeOffensiveToCarriers` guides rebase decisions:
- When defensive units outnumber offensive units, offensive aircraft prioritize carrier bases (combat hotspots)
- This ensures balanced fleet composition and strategic positioning

### Performance
- Carriers are scored once per turn alongside cities
- Scoring uses existing ScoreAirBase() function - no performance impact
- Pathfinding only invoked when direct rebase fails

## Related Files Modified
1. **CvHomelandAI.cpp** - Aircraft movement and base selection logic
2. **CvUnit.cpp** - EmergencyRebase() optimization and canLoad() bug fix (supporting changes)
3. **Copilot instructions:** [copilot-instructions.md](/.github/copilot-instructions.md) - Documentation of MSVC 2008 constraints and overload selection
