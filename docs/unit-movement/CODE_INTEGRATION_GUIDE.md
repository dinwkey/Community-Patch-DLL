# Unit Movement & Pathfinding: Code Integration Guide

This document provides code examples, integration patterns, and recipes for working with the unit movement and pathfinding system.

---

## 1. Basic Pathfinding Setup

### Example 1: Simple Unit Movement Path

**Scenario:** Find a path for a settler from point A to point B.

```cpp
#include "CvAStar.h"
#include "CvPlot.h"
#include "CvUnit.h"

CvUnit* pSettler = GET_PLAYER(PLAYER_HUMAN).getUnit(0);  // Some settler
CvPlot* pStart = pSettler->plot();
CvPlot* pDest = GC.getMap().plot(50, 50);  // Target plot

// Create pathfinder (typically a singleton in the game)
CvPathFinder& pathfinder = GC.getPathFinder();

// Configure for unit movement
SPathFinderUserData config(pSettler, 0, INT_MAX);  // Unit, flags=0, max turns=INT_MAX
if (pathfinder.Configure(config)) {
    // Generate path
    bool bFound = pathfinder.GeneratePath(
        pStart->getX(), pStart->getY(),
        pDest->getX(), pDest->getY()
    );
    
    if (bFound) {
        SPath path = pathfinder.GetCurrentPath();
        
        // Use path for movement
        for (int i = 1; i < path.vPlots.size(); i++) {
            pSettler->PushMission(MISSION_MOVE_TO, 
                                   path.vPlots[i].x, 
                                   path.vPlots[i].y);
        }
    } else {
        LOGFILEMGR.GetLog("path_failures.log")->Msg(
            "Could not find path from (%d,%d) to (%d,%d)",
            pStart->getX(), pStart->getY(), 
            pDest->getX(), pDest->getY()
        );
    }
}
```

---

### Example 2: Pathfinding with Flags

**Scenario:** Find path for AI explorer avoiding enemy territory, prioritizing new tiles.

```cpp
CvUnit* pExplorer = GET_PLAYER(PLAYER_1).getUnit(1);
CvPlot* pDest = GC.getMap().plot(75, 75);

CvPathFinder& pathfinder = GC.getPathFinder();

// Set up movement flags
int iFlags = 0;
iFlags |= CvUnit::MOVEFLAG_MAXIMIZE_EXPLORE;      // Reveal new tiles
iFlags |= CvUnit::MOVEFLAG_IGNORE_DANGER;         // Explorers are brave
iFlags |= CvUnit::MOVEFLAG_NO_ENEMY_TERRITORY;    // Avoid enemy lands

SPathFinderUserData config(pExplorer, iFlags, 5);  // Max 5 turns ahead
if (pathfinder.Configure(config)) {
    bool bFound = pathfinder.GeneratePath(
        pExplorer->getX(), pExplorer->getY(),
        pDest->getX(), pDest->getY()
    );
    
    if (bFound) {
        SPath path = pathfinder.GetCurrentPath();
        // Explorer avoids enemy territory while exploring
        // ... move along path ...
    }
}
```

---

### Example 3: Pathfinding for Siege Unit

**Scenario:** Ranged unit (catapult) approaching enemy city; should stop 1 tile away.

```cpp
CvUnit* pCatapult = GET_PLAYER(PLAYER_HUMAN).getUnit(42);
CvCity* pEnemyCity = GET_PLAYER(PLAYER_3).getCapitalCity();

// Siege units often can't reach exact destination (blocked by enemies)
int iFlags = 0;
iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;  // Stop 1 tile away
iFlags |= CvUnit::MOVEFLAG_NO_DEFENSIVE_SUPPORT; // Don't expect support

SPathFinderUserData config(pCatapult, iFlags, INT_MAX);
if (pathfinder.Configure(config)) {
    bool bFound = pathfinder.GeneratePath(
        pCatapult->getX(), pCatapult->getY(),
        pEnemyCity->getX(), pEnemyCity->getY()
    );
    
    if (bFound) {
        SPath path = pathfinder.GetCurrentPath();
        // Catapult moves to ring 1 position and can bombard
        
        // Verify catapult can actually bombard from final position
        CvPlot* pFinalPlot = GC.getMap().plot(
            path.vPlots.back().x, 
            path.vPlots.back().y
        );
        
        if (pCatapult->canEverRangeStrikeAt(pEnemyCity->plot(), pFinalPlot)) {
            // Safe to move here
        }
    }
}
```

---

## 2. Movement Cost Calculations

### Example 4: Compute Single-Step Cost

**Scenario:** Determine movement point cost for moving from forest to hills.

```cpp
#include "CvUnitMovement.h"

CvUnit* pUnit = GET_PLAYER(PLAYER_HUMAN).getUnit(0);
CvPlot* pFromPlot = GC.getMap().plot(30, 30);  // Forest
CvPlot* pToPlot = GC.getMap().plot(31, 30);    // Hills

// Get unit's base moves for turn
int iMaxMoves = pUnit->baseMoves(false) * GD_INT_GET(MOVE_DENOMINATOR);

// Compute cost
int iCost = CvUnitMovement::GetCostsForMove(
    pUnit,
    pFromPlot,
    pToPlot,
    INT_MAX,  // Default terrain multiplier
    INT_MAX   // Default terrain adder
);

if (iCost == INT_MAX) {
    printf("Cannot move to hills plot\n");
} else {
    int iMovesLeft = iMaxMoves - iCost;
    printf("Cost: %d / %d MP (%.1f tiles)\n", 
           iCost, iMaxMoves, 
           (float)iCost / GD_INT_GET(MOVE_DENOMINATOR));
}
```

---

### Example 5: Check ZOC Blocking

**Scenario:** Verify if unit can move through enemy zone of control.

```cpp
#include "CvUnitMovement.h"

CvUnit* pUnit = GET_PLAYER(PLAYER_0).getUnit(5);
CvPlot* pFromPlot = GC.getMap().plot(40, 40);
CvPlot* pToPlot = GC.getMap().plot(41, 40);

bool bSlowedByZOC = CvUnitMovement::IsSlowedByZOC(pUnit, pFromPlot, pToPlot);

if (bSlowedByZOC) {
    // Moving to pToPlot will slow unit (costs extra, possibly ends turn)
    printf("Unit is slowed by ZOC!\n");
} else {
    // Normal movement cost applies
    printf("No ZOC penalty\n");
}
```

---

### Example 6: Check Embarkation Cost

**Scenario:** Determine cost for unit to embark on water.

```cpp
CvUnit* pUnit = GET_PLAYER(PLAYER_HUMAN).getUnit(0);
CvPlot* pLandPlot = GC.getMap().plot(50, 50);    // Land
CvPlot* pCoastPlot = GC.getMap().plot(51, 50);   // Coastal water

// Method 1: Use GetCostsForMove (includes all effects)
int iCost = CvUnitMovement::GetCostsForMove(pUnit, pLandPlot, pCoastPlot);

if (iCost == INT_MAX) {
    printf("Embarkation ends turn (normal behavior)\n");
} else if (iCost == 1 * GD_INT_GET(MOVE_DENOMINATOR)) {
    printf("Flat-cost embarkation (trait or promotion)\n");
} else if (iCost == GD_INT_GET(MOVE_DENOMINATOR) / 10) {
    printf("Free embarkation with cover charge (city bonus)\n");
} else {
    printf("Embarkation costs %d / %d MP\n", iCost, GD_INT_GET(MOVE_DENOMINATOR));
}

// Method 2: Check canEmbarkOnto for validation
if (!pUnit->canEmbarkOnto(*pLandPlot, *pCoastPlot)) {
    printf("Unit cannot embark (maybe no ocean unit, or immobile)\n");
}
```

---

## 3. Working with Movement Flags

### Example 7: Build Custom Flag Set for AI Movement

**Scenario:** AI decides which movement flags to use based on unit type and situation.

```cpp
int GetAIMovementFlags(CvUnit* pUnit, CvPlot* pDestPlot)
{
    int iFlags = 0;
    
    // All AI units: ignore danger on movement (decisions made elsewhere)
    iFlags |= CvUnit::MOVEFLAG_IGNORE_DANGER;
    
    // Combat units: prepare to attack if needed
    if (pUnit->IsCombatUnit()) {
        iFlags |= CvUnit::MOVEFLAG_ATTACK;
        
        // Melee units: don't bother with support (too complex for pathfinding)
        if (!pUnit->IsCanAttackRanged()) {
            iFlags |= CvUnit::MOVEFLAG_NO_DEFENSIVE_SUPPORT;
        }
        
        // Ranged units besieging a city: stop nearby
        if (pDestPlot && pDestPlot->isEnemyCity(*pUnit)) {
            iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;
        }
    }
    
    // Civilian units: avoid neutral hostile units
    if (pUnit->getUnitCombatType() == NO_UNITCOMBAT) {
        iFlags |= CvUnit::MOVEFLAG_DONT_STACK_WITH_NEUTRAL;
    }
    
    // Naval units: don't embark onto land
    if (pUnit->getDomainType() == DOMAIN_SEA) {
        iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;
    }
    
    return iFlags;
}

// Usage
CvUnit* pAIUnit = GET_PLAYER(PLAYER_1).getUnit(0);
int iFlags = GetAIMovementFlags(pAIUnit, pAIUnit->plot());
```

---

### Example 8: Selective ZOC for Escorted Civilian

**Scenario:** Civilian unit with escort should not be slowed by escort's ZOC.

```cpp
#include "CvUnitMovement.h"

CvUnit* pCivilian = pEscort->GetEscortingCivilian();
CvUnit* pEscort = pCivilian->getEscort();

if (pCivilian && pEscort) {
    // Compute path for civilian, ignoring escort's ZOC
    
    CvPathFinder& pathfinder = GC.getPathFinder();
    
    // Build ignore list containing escort's current plot
    PlotIndexContainer plotsToIgnore;
    plotsToIgnore.push_back(pEscort->plot()->GetPlotIndex());
    
    // Create config with selective ZOC
    SPathFinderUserData config(pCivilian, CvUnit::MOVEFLAG_SELECTIVE_ZOC, INT_MAX);
    config.plotsToIgnoreForZOC = plotsToIgnore;
    
    if (pathfinder.Configure(config)) {
        bool bFound = pathfinder.GeneratePath(
            pCivilian->getX(), pCivilian->getY(),
            pDest->getX(), pDest->getY()
        );
        
        if (bFound) {
            // Civilian can move without escort slowing it down
            SPath path = pathfinder.GetCurrentPath();
        }
    }
}
```

---

## 4. Advanced Pathfinding Techniques

### Example 9: Reachable Plots (All Positions Reachable in N Turns)

**Scenario:** Find all plots a unit can reach in exactly 2 turns.

```cpp
CvUnit* pUnit = GET_PLAYER(PLAYER_HUMAN).getUnit(0);
int iMaxTurns = 2;

CvPathFinder& pathfinder = GC.getPathFinder();

// No specific destination; pathfinder will find all reachable plots
SPathFinderUserData config(pUnit, 0, iMaxTurns);
if (pathfinder.Configure(config)) {
    // GeneratePath with invalid destination (-1, -1) returns all reachable
    pathfinder.GeneratePath(
        pUnit->getX(), pUnit->getY(),
        -1, -1  // Invalid destination triggers reachable plots mode
    );
    
    // Query reachable plots from internal pathfinder state
    // (Note: this requires custom implementation; standard pathfinder 
    //  doesn't expose reachable set directly)
    
    for (int iX = 0; iX < GC.getMap().getGridWidth(); iX++) {
        for (int iY = 0; iY < GC.getMap().getGridHeight(); iY++) {
            const CvAStarNode* pNode = pathfinder.GetNode(iX, iY);
            if (pNode && pNode->m_iTurns <= iMaxTurns && pNode->m_iTurns >= 0) {
                CvPlot* pPlot = GC.getMap().plotUnchecked(iX, iY);
                printf("Reachable: (%d, %d) in %d turns\n", 
                       iX, iY, pNode->m_iTurns);
            }
        }
    }
}
```

---

### Example 10: Path Verification

**Scenario:** Verify that a stored path is still valid (unit didn't move, obstacles didn't appear).

```cpp
CvUnit* pUnit = GET_PLAYER(PLAYER_HUMAN).getUnit(0);
SPath cachedPath = /* ... stored path from earlier ... */;

CvPathFinder& pathfinder = GC.getPathFinder();

// Reconfigure for same unit
SPathFinderUserData config(pUnit, 0, INT_MAX);
if (pathfinder.Configure(config)) {
    bool bPathStillValid = pathfinder.VerifyPath(cachedPath);
    
    if (bPathStillValid) {
        printf("Cached path is still usable\n");
        // Continue using cachedPath
    } else {
        printf("Path blocked; recompute\n");
        // Recompute path
    }
}
```

---

## 5. Performance Patterns

### Example 11: Batch Pathfinding for Multiple Units

**Scenario:** Compute paths for all AI units without overwhelming the system.

```cpp
void ComputeAIMovement(CvPlayer* pPlayer)
{
    CvPathFinder& pathfinder = GC.getPathFinder();
    
    int iUnitsProcessed = 0;
    int iMaxPerTurn = 10;  // Limit to 10 units per turn
    
    for (int iUnitLoop = 0; iUnitLoop < pPlayer->getNumUnits(); iUnitLoop++) {
        if (iUnitsProcessed >= iMaxPerTurn) {
            break;  // Spread pathfinding across turns
        }
        
        CvUnit* pUnit = pPlayer->getUnit(iUnitLoop);
        if (!pUnit || !pUnit->canMove()) {
            continue;
        }
        
        // Pathfind for this unit
        CvPlot* pDest = pUnit->GetAITargetPlot();
        if (pDest) {
            SPathFinderUserData config(pUnit, 0, 3);  // Max 3 turns
            if (pathfinder.Configure(config)) {
                if (pathfinder.GeneratePath(
                    pUnit->getX(), pUnit->getY(),
                    pDest->getX(), pDest->getY())) {
                    
                    pUnit->SetPath(pathfinder.GetCurrentPath());
                    iUnitsProcessed++;
                }
            }
        }
    }
}
```

---

### Example 12: Caching and Reusing Paths

**Scenario:** Store computed path and reuse if unit/destination unchanged.

```cpp
struct CachedPath {
    int iUnitID;
    int iDestX, iDestY;
    SPath path;
    int iTurnComputed;
};

std::vector<CachedPath> g_vPathCache;

SPath GetOrComputePath(CvUnit* pUnit, CvPlot* pDest)
{
    int iTurn = GC.getGame().getGameTurn();
    
    // Check cache
    for (const auto& cached : g_vPathCache) {
        if (cached.iUnitID == pUnit->GetID() &&
            cached.iDestX == pDest->getX() &&
            cached.iDestY == pDest->getY() &&
            cached.iTurnComputed == iTurn) {
            
            // Path is fresh; verify it
            CvPathFinder& pathfinder = GC.getPathFinder();
            SPathFinderUserData config(pUnit, 0, INT_MAX);
            if (pathfinder.Configure(config)) {
                if (pathfinder.VerifyPath(cached.path)) {
                    return cached.path;  // Still valid
                }
            }
        }
    }
    
    // Not cached or invalid; recompute
    CvPathFinder& pathfinder = GC.getPathFinder();
    SPathFinderUserData config(pUnit, 0, INT_MAX);
    if (pathfinder.Configure(config)) {
        if (pathfinder.GeneratePath(
            pUnit->getX(), pUnit->getY(),
            pDest->getX(), pDest->getY())) {
            
            SPath newPath = pathfinder.GetCurrentPath();
            
            // Cache it
            CachedPath cached = {pUnit->GetID(), pDest->getX(), pDest->getY(), newPath, iTurn};
            g_vPathCache.push_back(cached);
            
            // Limit cache size
            if (g_vPathCache.size() > 100) {
                g_vPathCache.erase(g_vPathCache.begin());
            }
            
            return newPath;
        }
    }
    
    return SPath();  // Empty path (no solution)
}
```

---

## 6. Debugging & Logging

### Example 13: Log Movement Decisions

**Scenario:** Debug why unit moved somewhere unexpected.

```cpp
void LogMovementDecision(CvUnit* pUnit, CvPlot* pDestination)
{
    FILogFile* pLog = LOGFILEMGR.GetLog("unit_movement_debug.log");
    if (!pLog) return;
    
    pLog->Msg("=== MOVEMENT DECISION ===");
    pLog->Msg("Unit: %s (ID %d, Owner: %s)", 
              pUnit->getName().c_str(), pUnit->GetID(), 
              GET_PLAYER(pUnit->getOwner()).getName());
    pLog->Msg("Current Position: (%d, %d)", pUnit->getX(), pUnit->getY());
    pLog->Msg("Destination: (%d, %d)", pDestination->getX(), pDestination->getY());
    pLog->Msg("Base Moves: %d", pUnit->baseMoves(false));
    pLog->Msg("Moves Remaining: %d", pUnit->movesLeft());
    pLog->Msg("Domain: %s", pUnit->getDomainType() == DOMAIN_LAND ? "Land" : 
                            pUnit->getDomainType() == DOMAIN_SEA ? "Sea" : "Air");
    pLog->Msg("Embarked: %s", pUnit->isEmbarked() ? "Yes" : "No");
    
    // Log nearby threats
    for (int iX = pUnit->getX() - 3; iX <= pUnit->getX() + 3; iX++) {
        for (int iY = pUnit->getY() - 3; iY <= pUnit->getY() + 3; iY++) {
            CvPlot* pPlot = GC.getMap().plotCheckInvalid(iX, iY);
            if (pPlot && pPlot->isVisibleEnemyUnit(pUnit)) {
                pLog->Msg("  Enemy nearby: (%d, %d)", iX, iY);
            }
        }
    }
    pLog->Msg("=========================\n");
}
```

---

### Example 14: Performance Profiling

**Scenario:** Measure pathfinding time and log slow paths.

```cpp
#include <ctime>

void ProfilePathfinding(CvUnit* pUnit, CvPlot* pDest)
{
    clock_t tStart = clock();
    
    CvPathFinder& pathfinder = GC.getPathFinder();
    SPathFinderUserData config(pUnit, 0, INT_MAX);
    
    if (pathfinder.Configure(config)) {
        if (pathfinder.GeneratePath(
            pUnit->getX(), pUnit->getY(),
            pDest->getX(), pDest->getY())) {
            
            clock_t tEnd = clock();
            double dElapsed = (double)(tEnd - tStart) / CLOCKS_PER_SEC * 1000;  // ms
            
            SPath path = pathfinder.GetCurrentPath();
            
            if (dElapsed > 10.0) {  // Warn if > 10ms
                FILogFile* pLog = LOGFILEMGR.GetLog("pathfinder_slow.log");
                pLog->Msg("Slow pathfind: %.2f ms for %s (distance %d, path length %d)", 
                          dElapsed, pUnit->getName().c_str(), 
                          plotDistance(pUnit->getX(), pUnit->getY(), pDest->getX(), pDest->getY()),
                          path.vPlots.size());
            }
        }
    }
}
```

---

## 7. Common Pitfalls & Solutions

### Pitfall 1: Not Handling INT_MAX Costs

**Problem:**
```cpp
int iCost = CvUnitMovement::GetCostsForMove(pUnit, pFrom, pTo);
int iMovesLeft = iMaxMoves - iCost;  // iMovesLeft = huge negative if iCost == INT_MAX!
```

**Solution:**
```cpp
int iCost = CvUnitMovement::GetCostsForMove(pUnit, pFrom, pTo);
if (iCost == INT_MAX) {
    // Unit cannot move to destination; embarkation or other restriction
    return false;
}
int iMovesLeft = iMaxMoves - iCost;
```

---

### Pitfall 2: Forgetting to Configure Pathfinder

**Problem:**
```cpp
CvPathFinder& pathfinder = GC.getPathFinder();
// Missing: pathfinder.Configure(config);
pathfinder.GeneratePath(x1, y1, x2, y2);  // Pathfinder state is wrong!
```

**Solution:**
```cpp
CvPathFinder& pathfinder = GC.getPathFinder();
SPathFinderUserData config(pUnit, iFlags, iMaxTurns);
if (!pathfinder.Configure(config)) {
    // Configuration failed
    return false;
}
pathfinder.GeneratePath(x1, y1, x2, y2);  // Correct
```

---

### Pitfall 3: Assuming All Plots in Path Are Passable

**Problem:**
```cpp
SPath path = pathfinder.GetCurrentPath();
for (const auto& plot : path.vPlots) {
    pUnit->PushMission(MISSION_MOVE_TO, plot.x, plot.y);  // May fail if plot became impassable!
}
```

**Solution:**
```cpp
SPath path = pathfinder.GetCurrentPath();
for (const auto& plot : path.vPlots) {
    CvPlot* pPlot = GC.getMap().plot(plot.x, plot.y);
    if (!pUnit->canMoveInto(*pPlot)) {
        // Path is blocked; stop here or recompute
        break;
    }
    pUnit->PushMission(MISSION_MOVE_TO, plot.x, plot.y);
}
```

---

## 8. Migration Guide (From Vanilla to VP/Community Patch)

### Vanilla Code → VP Code

| Vanilla Pattern | VP/Community Patch | Notes |
|-----------------|-------------------|-------|
| `unit->getMovesLeft()` | `unit->movesLeft()` | Same function |
| `MovementPath` struct | `SPath` struct | Different structure; use `vPlots` member |
| No ZOC flags | `MOVEFLAG_SELECTIVE_ZOC` | New feature; optional |
| Multiplicative costs | Additive costs (with `MOD_BALANCE_SANE_UNIT_MOVEMENT_COST`) | More intuitive |
| No approximation | `MOVEFLAG_APPROX_TARGET_RING1/2` | Better siege behavior |

---

**Document Version:** 1.0  
**Last Updated:** January 2025  
**Maintenance:** Community Patch DLL developers
