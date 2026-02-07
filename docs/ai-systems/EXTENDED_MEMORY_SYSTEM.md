# Extended Memory System — Implementation Specification

**Purpose:** Detailed specification for implementing a multi-turn memory system that enables the AI to detect patterns, track unit movements, and make decisions based on historical context rather than just current turn state.

**Last Updated:** February 2026  
**Status:** Ready for Implementation

**Related:**
- [AI_DEEP_REASONING.md](AI_DEEP_REASONING.md) — Parent architecture document
- [AI_SYSTEMS_REVIEW.md](AI_SYSTEMS_REVIEW.md) — Issues backlog

---

## Table of Contents

1. [Overview](#1-overview)
2. [Design Goals](#2-design-goals)
3. [Memory Budget](#3-memory-budget)
4. [Data Structures](#4-data-structures)
5. [Aggregate Snapshots (TurnSnapshot)](#5-aggregate-snapshots-turnsnapshot)
6. [Per-Unit Tracking (UnitSighting)](#6-per-unit-tracking-unitsighting)
7. [Fog of War Ghost System](#7-fog-of-war-ghost-system)
8. [Pattern Detection Functions](#8-pattern-detection-functions)
9. [Serialization (Save Compatibility)](#9-serialization-save-compatibility)
10. [Integration Points](#10-integration-points)
11. [Implementation Steps](#11-implementation-steps)

---

## 1. Overview

### Problem Statement

The current VP/CP AI makes decisions based on **immediate game state** without:
- Detecting military buildups over multiple turns
- Remembering broken promises and betrayals
- Tracking enemy unit movement patterns
- Predicting where fog-of-war units went

### Solution

A hybrid memory system with two components:

| Component | Purpose | Storage |
|-----------|---------|---------|
| **Aggregate Snapshots** | Per-civ summary stats over 10 turns | ~200 KB (43 civs) |
| **Per-Unit Tracking** | Individual unit sightings with fog prediction | ~5-25 KB |

**Total: ~225 KB typical** — trivial compared to 1.2-2 GB headroom (LAA on 64-bit Windows).

> **Note:** VP default is 43 major civs + 20 city states. Extended mods can go up to 62 major civs.

---

## 2. Design Goals

### Must Have
- ✅ Detect military buildups (units massing near borders over 3-5 turns)
- ✅ Track diplomatic shifts (approach changes from FRIENDLY → HOSTILE)
- ✅ Remember war outcomes (who won/lost recent wars)
- ✅ Predict fog-of-war unit movement (directional cones)
- ✅ Detect siege unit presence (high-confidence attack signal)
- ⚠️ Save game compatible (NOT in current implementation; new games required)

### Should Have
- ✅ Detect forward-settling / territorial creep (proximity changes)
- ✅ Track economic trends (GPT changes over time)
- ✅ Detect overextension (multiple simultaneous wars)
- ✅ Naval threat tracking (especially valuable due to high movement)

### Nice to Have
- 🔲 Training data logging for ML (separate CSV output)
- 🔲 Misprediction tracking for fog ghosts
- 🔲 Per-city threat assessment

---

## 3. Memory Budget

### Aggregate Snapshots

**Per-snapshot size (with 43 major civs — VP default):**

| Field | Size |
|-------|------|
| `turn` | 2B |
| `warState[43]` | 43B |
| `approach[43]` | 43B |
| `theirMilitaryNearUs[43]` | 43B |
| `theirMilitaryStrength[43]` | 43B |
| `proximity[43]` | 43B |
| `siegeUnitsNearUs[43]` | 43B |
| `navalUnitsNearUs[43]` | 43B |
| Scalar fields | 9B |
| **Total per snapshot** | **~312 bytes** |

> **Note:** City states (20 default) are tracked separately via minor civ diplomacy and don't need full memory snapshots.

**Total for all civs:**

| Config | 10 Turns | Total |
|--------|----------|-------|
| 43 civs (VP default) | 312B × 10 × 43 | **134 KB** |
| 62 civs (extended max) | 445B × 10 × 62 | **276 KB** |

### Per-Unit Tracking

| Scenario | Units Tracked | Storage |
|----------|---------------|---------|
| Peacetime | ~50 | 800B |
| Active war | ~150 | 2.4 KB |
| Worst case | ~300 | 4.8 KB |

### Combined Total

| Configuration | Aggregates | Per-Unit | Total |
|---------------|------------|----------|-------|
| 43 civs, peace (typical) | 134 KB | 1 KB | **135 KB** |
| 43 civs, active war | 134 KB | 5 KB | **139 KB** |
| 62 civs, war (extended) | 276 KB | 5 KB | **281 KB** |
| Worst case | 276 KB | 25 KB | **~300 KB** |

**Conclusion:** Fits easily within available memory headroom.

---

## 4. Data Structures

### Constants

```cpp
// In CvDiplomacyAI.h or a new CvAIMemory.h

static const int AI_MEMORY_DEPTH = 10;              // Turns of history
static const int AI_MAX_UNIT_SIGHTINGS = 300;       // Max tracked units
static const int AI_FOG_GHOST_EXPIRY_TURNS = 10;    // When to forget fog units
static const int AI_FOG_GHOST_USEFUL_TURNS = 5;     // When direction becomes unreliable
```

### Enums

```cpp
// Unit intent prediction (for fog ghosts)
enum UnitPredictedIntent
{
    UNIT_INTENT_UNKNOWN,
    UNIT_INTENT_ATTACK_CITY,
    UNIT_INTENT_ATTACK_UNIT,
    UNIT_INTENT_RETREAT,
    UNIT_INTENT_PATROL,
    UNIT_INTENT_REINFORCE,
    UNIT_INTENT_EXPLORE,
    
    NUM_UNIT_INTENTS
};

// Snapshot flags (bitfield)
enum SnapshotFlags
{
    SNAPSHOT_FLAG_AT_WAR        = 0x01,  // Currently in any war
    SNAPSHOT_FLAG_WINNING_WAR   = 0x02,  // Winning at least one war
    SNAPSHOT_FLAG_LOSING_WAR    = 0x04,  // Losing at least one war
    SNAPSHOT_FLAG_BUILDING_WONDER = 0x08,
    SNAPSHOT_FLAG_RECENTLY_DENOUNCED = 0x10,
    SNAPSHOT_FLAG_RECENTLY_BACKSTABBED = 0x20,
};
```

---

## 5. Aggregate Snapshots (TurnSnapshot)

### Structure Definition

```cpp
// Per-civ state snapshot captured each turn
// Size: ~312 bytes (43 civs) or ~445 bytes (62 civs)
struct TurnSnapshot
{
    // === Validity & Timestamp ===
    int16   turn;                                   // 2B — 0 = invalid/empty slot
    
    // === Per-Civ Arrays (sized by MAX_MAJOR_CIVS: 43 default, 62 extended) ===
    int8    warState[MAX_MAJOR_CIVS];               // 43-62B — WarStateTypes enum
    int8    approach[MAX_MAJOR_CIVS];               // 43-62B — CivApproachTypes enum
    uint8   theirMilitaryNearUs[MAX_MAJOR_CIVS];    // 43-62B — Unit count near our borders
    uint8   theirMilitaryStrength[MAX_MAJOR_CIVS];  // 43-62B — Scaled 0-255
    uint8   proximity[MAX_MAJOR_CIVS];              // 43-62B — PlayerProximityTypes enum
    uint8   siegeUnitsNearUs[MAX_MAJOR_CIVS];       // 43-62B — Siege = attack signal
    uint8   navalUnitsNearUs[MAX_MAJOR_CIVS];       // 43-62B — Coastal threat
    
    // === Our State (Scalars) ===
    uint8   militaryRank;                           // 1B — Our rank 1-43/62 (1 = strongest)
    uint8   numCities;                              // 1B — Our city count
    int16   goldPerTurn;                            // 2B — Our GPT (can be negative)
    uint8   numUnitsNearBorders;                    // 1B — Total enemy units near us (all civs)
    uint8   numWars;                                // 1B — How many wars we're fighting
    uint8   flags;                                  // 1B — SnapshotFlags bitfield
    uint8   padding;                                // 1B — Alignment
    
    // === Helpers ===
    bool IsValid() const { return turn > 0; }
};
```

### Circular Buffer Wrapper

```cpp
// Per-civ memory store with circular buffer
struct CivMemory
{
    TurnSnapshot history[AI_MEMORY_DEPTH];  // 10 turns × 312B = 3.12 KB (43 civs)
    uint8        currentIndex;              // Which slot is "newest" (0-9)
    uint8        validCount;                // How many slots are populated (0-10)
    
    // === Accessors ===
    
    // Get snapshot from N turns ago (0 = current, 9 = oldest)
    // Returns nullptr if that far back isn't valid yet
    TurnSnapshot* GetTurnsAgo(int iTurnsAgo)
    {
        if (iTurnsAgo >= validCount || iTurnsAgo >= AI_MEMORY_DEPTH)
            return nullptr;
        
        int index = (currentIndex - iTurnsAgo + AI_MEMORY_DEPTH) % AI_MEMORY_DEPTH;
        TurnSnapshot* pSnapshot = &history[index];
        
        return pSnapshot->IsValid() ? pSnapshot : nullptr;
    }
    
    // Get current turn's snapshot
    TurnSnapshot* GetCurrent() { return GetTurnsAgo(0); }
    
    // Advance to next slot (called at start of DoTurn)
    TurnSnapshot* AdvanceAndGetNew()
    {
        currentIndex = (currentIndex + 1) % AI_MEMORY_DEPTH;
        if (validCount < AI_MEMORY_DEPTH)
            validCount++;
        
        // Clear the new slot
        memset(&history[currentIndex], 0, sizeof(TurnSnapshot));
        return &history[currentIndex];
    }
};
```

### Capture Function

```cpp
// Called at START of CvDiplomacyAI::DoTurn() before state updates
void CvDiplomacyAI::CaptureMemorySnapshot()
{
    PlayerTypes eMyPlayer = GetID();
    CvPlayer* pMyPlayer = &GET_PLAYER(eMyPlayer);
    int iCurrentTurn = GC.getGame().getGameTurn();
    
    // Advance circular buffer
    TurnSnapshot* pSnap = m_Memory.AdvanceAndGetNew();
    pSnap->turn = (int16)iCurrentTurn;
    
    // === Capture per-civ data ===
    int iNumWars = 0;
    
    for (int iPlayer = 0; iPlayer < MAX_MAJOR_CIVS; iPlayer++)
    {
        PlayerTypes eOther = (PlayerTypes)iPlayer;
        if (!GET_PLAYER(eOther).isAlive() || eOther == eMyPlayer)
            continue;
        
        // War state
        pSnap->warState[iPlayer] = (int8)GetWarState(eOther);
        if (IsAtWarWith(eOther))
            iNumWars++;
        
        // Approach
        pSnap->approach[iPlayer] = (int8)GetCivApproach(eOther);
        
        // Military near us (raw combat unit count; intent filtering happens in detection)
        pSnap->theirMilitaryNearUs[iPlayer] = (uint8)min(255, CountCombatUnitsNearUs(eOther));
        
        // Their military strength (scaled to 0-255)
        int iTheirStrength = GET_PLAYER(eOther).GetMilitaryMight();
        int iMaxStrength = 10000;  // Reasonable cap
        pSnap->theirMilitaryStrength[iPlayer] = (uint8)(min(255, (iTheirStrength * 255) / iMaxStrength));
        
        // Proximity
        pSnap->proximity[iPlayer] = (uint8)GetPlayer()->GetProximityToPlayer(eOther);
        
        // Siege units near us
        pSnap->siegeUnitsNearUs[iPlayer] = (uint8)min(255, CountEnemySiegeUnitsNearUs(eOther));
        
        // Naval units near us
        pSnap->navalUnitsNearUs[iPlayer] = (uint8)min(255, CountEnemyNavalUnitsNearUs(eOther));
    }
    
    // === Capture our state ===
    pSnap->militaryRank = (uint8)CalculateMilitaryRank(eMyPlayer);
    pSnap->numCities = (uint8)min(255, pMyPlayer->getNumCities());
    pSnap->goldPerTurn = (int16)pMyPlayer->calculateGoldRate();
    pSnap->numUnitsNearBorders = (uint8)min(255, GetTotalEnemyUnitsNearBorders());
    pSnap->numWars = (uint8)iNumWars;
    
    // === Flags ===
    pSnap->flags = 0;
    if (iNumWars > 0) pSnap->flags |= SNAPSHOT_FLAG_AT_WAR;
    // ... other flags
}
```

---

## 6. Per-Unit Tracking (UnitSighting)

### Structure Definition

```cpp
// Individual unit sighting with fog prediction support
// Size: 16 bytes per unit
struct UnitSighting
{
    // === Identification ===
    uint16  unitId;             // 2B — Unit's ID (for tracking same unit)
    uint8   unitType;           // 1B — UnitTypes enum (warrior, catapult, etc.)
    uint8   owner;              // 1B — PlayerTypes — which civ owns it
    
    // === Position ===
    int16   x, y;               // 4B — Last confirmed position
    int16   lastSeenTurn;       // 2B — When we last SAW this unit
    
    // === State ===
    uint8   health;             // 1B — Health percentage (0-100)
    uint8   flags;              // 1B — Embarked, fortified, etc.
    
    // === Fog Prediction ===
    int8    lastDeltaX;         // 1B — Last movement X direction
    int8    lastDeltaY;         // 1B — Last movement Y direction
    uint8   movementPoints;     // 1B — Unit's movement capability
    uint8   predictedIntent;    // 1B — UnitPredictedIntent enum
    
    // === Helpers ===
    bool IsConfirmed(int currentTurn) const 
    { 
        return lastSeenTurn == currentTurn; 
    }
    
    bool IsExpired(int currentTurn) const 
    { 
        return (currentTurn - lastSeenTurn) > AI_FOG_GHOST_EXPIRY_TURNS; 
    }
    
    int GetUncertaintyRadius(int currentTurn) const
    {
        int turnsMissing = currentTurn - lastSeenTurn;
        return turnsMissing * movementPoints;
    }
};
```

### Sighting Flags

```cpp
enum UnitSightingFlags
{
    SIGHTING_FLAG_EMBARKED  = 0x01,
    SIGHTING_FLAG_FORTIFIED = 0x02,
    SIGHTING_FLAG_DAMAGED   = 0x04,  // Below 50% health
    SIGHTING_FLAG_RANGED    = 0x08,
    SIGHTING_FLAG_SIEGE     = 0x10,
    SIGHTING_FLAG_NAVAL     = 0x20,
    SIGHTING_FLAG_AIR       = 0x40,
};
```

### Storage Container

```cpp
// Unit sighting manager
class CvUnitSightingManager
{
public:
    // === Core Operations ===
    void OnUnitSeen(CvUnit* pUnit);
    void OnUnitMoved(CvUnit* pUnit, CvPlot* pFrom, CvPlot* pTo);
    void OnUnitLeftVisibility(CvUnit* pUnit);
    void OnUnitDestroyed(CvUnit* pUnit);
    void UpdateGhosts(int currentTurn);
    void CleanupExpired(int currentTurn);
    
    // === Queries ===
    UnitSighting* GetSighting(PlayerTypes eOwner, int iUnitId);
    std::vector<UnitSighting*> GetHostileSightingsNearPlot(CvPlot* pPlot, int iRadius);
    std::vector<UnitSighting*> GetGhostsInSearchCone(int centerX, int centerY, int dirX, int dirY, int maxDist);
    int CountSiegeUnitsNearCity(CvCity* pCity, int iRadius);
    
    // === Serialization ===
    void Read(FDataStream& kStream);
    void Write(FDataStream& kStream) const;
    
private:
    std::vector<UnitSighting> m_Sightings;
    
    // Fast lookup by (owner, unitId)
    std::map<uint32, int> m_SightingIndex;  // Key = (owner << 16) | unitId
    
    uint32 MakeKey(PlayerTypes eOwner, int iUnitId) const 
    { 
        return ((uint32)eOwner << 16) | (uint32)(iUnitId & 0xFFFF); 
    }
};
```

---

## 7. Fog of War Ghost System

### Movement Direction Capture

```cpp
// Called when we observe a unit move (even if destination is fog)
void CvUnitSightingManager::OnUnitMoved(CvUnit* pUnit, CvPlot* pFrom, CvPlot* pTo)
{
    PlayerTypes eObserver = m_pPlayer->GetID();
    
    // Was unit visible at start of move?
    if (!pFrom->isVisible(eObserver))
        return;
    
    UnitSighting* pSighting = GetOrCreateSighting(pUnit);
    
    // Capture movement direction (EVEN if destination is fog)
    pSighting->lastDeltaX = pTo->getX() - pFrom->getX();
    pSighting->lastDeltaY = pTo->getY() - pFrom->getY();
    
    // Is destination visible?
    if (pTo->isVisible(eObserver))
    {
        // Full update
        pSighting->x = pTo->getX();
        pSighting->y = pTo->getY();
        pSighting->lastSeenTurn = GC.getGame().getGameTurn();
    }
    else
    {
        // Unit entered fog — becomes ghost
        // Keep last confirmed position, but we captured the direction!
        // lastSeenTurn stays as previous turn
    }
    
    // Infer intent from context
    pSighting->predictedIntent = InferUnitIntent(pSighting, pUnit);
}
```

### Intent Inference

```cpp
UnitPredictedIntent CvUnitSightingManager::InferUnitIntent(UnitSighting* pSighting, CvUnit* pUnit)
{
    PlayerTypes eOwner = (PlayerTypes)pSighting->owner;
    
    // Damaged units likely retreating
    if (pSighting->health < 50)
        return UNIT_INTENT_RETREAT;
    
    // Siege units are for city attacks
    if (pSighting->flags & SIGHTING_FLAG_SIEGE)
        return UNIT_INTENT_ATTACK_CITY;
    
    // Check war state
    WarStateTypes eWarState = GetPlayer()->GetDiplomacyAI()->GetWarState(eOwner);
    
    if (eWarState == WAR_STATE_NEARLY_DEFEATED || eWarState == WAR_STATE_DEFENSIVE)
        return UNIT_INTENT_RETREAT;
    
    if (eWarState == WAR_STATE_OFFENSIVE || eWarState == WAR_STATE_NEARLY_WON)
        return UNIT_INTENT_ATTACK_CITY;
    
    // Check if heading toward our city
    CvCity* pNearestCity = FindNearestCityInDirection(pSighting);
    if (pNearestCity && pNearestCity->getOwner() == m_pPlayer->GetID())
        return UNIT_INTENT_ATTACK_CITY;
    
    return UNIT_INTENT_UNKNOWN;
}
```

### Directional Cone Search

```cpp
// Check if a plot is within the predicted search cone for a fog ghost
bool CvUnitSightingManager::IsPlotInSearchCone(UnitSighting* pGhost, int plotX, int plotY, int currentTurn)
{
    // Distance check
    int dx = plotX - pGhost->x;
    int dy = plotY - pGhost->y;
    int dist = plotDistance(pGhost->x, pGhost->y, plotX, plotY);
    
    int uncertainty = pGhost->GetUncertaintyRadius(currentTurn);
    if (dist > uncertainty)
        return false;
    
    // If unit was stationary, search all directions
    if (pGhost->lastDeltaX == 0 && pGhost->lastDeltaY == 0)
        return true;
    
    // Direction check — dot product
    // Positive if query plot is in same general direction as movement
    int dot = dx * pGhost->lastDeltaX + dy * pGhost->lastDeltaY;
    
    // Determine cone width based on unit type
    // Naval = tight cone (90°), land = wider cone (120-180°)
    bool isNaval = (pGhost->flags & SIGHTING_FLAG_NAVAL);
    bool isFastLand = (pGhost->movementPoints >= 4);
    
    if (isNaval || isFastLand)
    {
        // 90° cone — only forward direction
        return dot > 0;
    }
    else
    {
        // 120° cone — forward + some side
        int magnitude = abs(pGhost->lastDeltaX) + abs(pGhost->lastDeltaY);
        return dot > -(magnitude * dist / 3);
    }
}

// Query: Could this ghost threaten a specific city?
bool CvUnitSightingManager::CouldGhostThreatenCity(UnitSighting* pGhost, CvCity* pCity, int currentTurn)
{
    // Project ghost position along heading
    int turnsMissing = currentTurn - pGhost->lastSeenTurn;
    int projectedX = pGhost->x + pGhost->lastDeltaX * turnsMissing;
    int projectedY = pGhost->y + pGhost->lastDeltaY * turnsMissing;
    
    // Distance from projected position to city
    int distToCity = plotDistance(projectedX, projectedY, pCity->getX(), pCity->getY());
    
    // Within strike range?
    int strikeRange = pGhost->movementPoints + 2;  // +2 for attack range
    return distToCity <= strikeRange;
}
```

### Direction Value by Unit Type

| Unit Type | Movement | Direction Reliability | Why |
|-----------|----------|----------------------|-----|
| Trireme/Galley | 4 | ⭐ High | Open water, constrained |
| Caravel/Frigate | 5-6 | ⭐ High | Same |
| Battleship | 6-8 | ⭐ High | Can cross ocean quickly |
| Warrior (no road) | 2 | ⚠️ Low | Could be anywhere nearby |
| Warrior (road) | 3-4 | ⭐ Medium | Roads constrain path |
| Cavalry/Tank | 4-6 | ⭐ Medium-High | Fast, direction matters |
| Railroad unit | 10+ | ⭐ High | Very constrained path |

---

## 8. Pattern Detection Functions

### Military Buildup Detection

```cpp
// Detect if a player is massing troops near our borders (3+ turn trend)
// Hybrid: return false if intent filter says they're not likely targeting us
bool CvDiplomacyAI::IsPlayerBuildingUpNearUs(PlayerTypes ePlayer)
{
    if (!IsLikelyIntentAgainstUs(ePlayer)) return false;
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    TurnSnapshot* p3Ago = m_Memory.GetTurnsAgo(3);
    TurnSnapshot* p6Ago = m_Memory.GetTurnsAgo(6);
    
    if (!pNow || !p3Ago) return false;  // Not enough history
    
    int iNow = pNow->theirMilitaryNearUs[ePlayer];
    int i3Ago = p3Ago->theirMilitaryNearUs[ePlayer];
    int i6Ago = p6Ago ? p6Ago->theirMilitaryNearUs[ePlayer] : 0;
    
    // Trend: consistently increasing
    bool bRising3 = (iNow > i3Ago);
    bool bRising6 = p6Ago ? (i3Ago > i6Ago) : true;
    
    // Threshold: at least 5 more units than 6 turns ago
    bool bSignificant = (iNow - i6Ago >= 5);
    
    return bRising3 && bRising6 && bSignificant;
}
```

### Siege Unit Warning

```cpp
// Siege units near borders = very high confidence attack signal
bool CvDiplomacyAI::IsSiegeWarningActive(PlayerTypes ePlayer)
{
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    if (!pNow) return false;
    
    // Any siege near us is a red flag
    return pNow->siegeUnitsNearUs[ePlayer] >= 2;
}
```

### Forward-Settle Detection

```cpp
// Detect if player is creeping closer via city placement
bool CvDiplomacyAI::IsPlayerCreepingCloser(PlayerTypes ePlayer)
{
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    TurnSnapshot* p10Ago = m_Memory.GetTurnsAgo(9);  // ~10 turns back
    
    if (!pNow || !p10Ago) return false;
    
    // Proximity increased (FAR → CLOSE, or CLOSE → NEIGHBORS)
    return (pNow->proximity[ePlayer] > p10Ago->proximity[ePlayer]);
}
```

### Approach Change Detection

```cpp
// Detect sudden shift in diplomatic stance
bool CvDiplomacyAI::HasApproachChangedRecently(PlayerTypes ePlayer, int iWithinTurns)
{
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    if (!pNow) return false;
    
    int iCurrentApproach = pNow->approach[ePlayer];
    
    for (int i = 1; i <= iWithinTurns && i < AI_MEMORY_DEPTH; i++)
    {
        TurnSnapshot* pPast = m_Memory.GetTurnsAgo(i);
        if (pPast && pPast->approach[ePlayer] != iCurrentApproach)
            return true;
    }
    
    return false;
}

// Specifically detect shift toward hostility
bool CvDiplomacyAI::HasTurnedHostileRecently(PlayerTypes ePlayer, int iWithinTurns)
{
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    if (!pNow) return false;
    
    CivApproachTypes eNowApproach = (CivApproachTypes)pNow->approach[ePlayer];
    
    // Current must be hostile/war
    if (eNowApproach != CIV_APPROACH_HOSTILE && eNowApproach != CIV_APPROACH_WAR)
        return false;
    
    // Check if it was friendlier before
    for (int i = 1; i <= iWithinTurns && i < AI_MEMORY_DEPTH; i++)
    {
        TurnSnapshot* pPast = m_Memory.GetTurnsAgo(i);
        if (pPast)
        {
            CivApproachTypes ePastApproach = (CivApproachTypes)pPast->approach[ePlayer];
            if (ePastApproach == CIV_APPROACH_FRIENDLY || 
                ePastApproach == CIV_APPROACH_NEUTRAL)
                return true;
        }
    }
    
    return false;
}
```

### Overextension Check

```cpp
// Am I fighting too many wars at once?
bool CvDiplomacyAI::AmIOverextended()
{
    TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
    if (!pNow) return false;
    
    // More than 2 wars AND we're not strong
    return (pNow->numWars >= 2 && pNow->militaryRank > 20);
}
```

### Historical Threat Calculation

```cpp
// Calculate threat level for a past turn (from stored components)
int CvDiplomacyAI::GetHistoricalThreat(PlayerTypes ePlayer, int iTurnsAgo)
{
    TurnSnapshot* pSnap = m_Memory.GetTurnsAgo(iTurnsAgo);
    if (!pSnap) return 0;
    
    int iThreat = 0;
    
    // Their military strength (major factor)
    iThreat += pSnap->theirMilitaryStrength[ePlayer] * 2;
    
    // Units near our borders (major factor)
    iThreat += pSnap->theirMilitaryNearUs[ePlayer] * 15;
    
    // Siege units (huge red flag)
    iThreat += pSnap->siegeUnitsNearUs[ePlayer] * 30;
    
    // Proximity
    iThreat += pSnap->proximity[ePlayer] * 10;
    
    // Hostile approach
    CivApproachTypes eApproach = (CivApproachTypes)pSnap->approach[ePlayer];
    if (eApproach == CIV_APPROACH_HOSTILE) iThreat += 50;
    if (eApproach == CIV_APPROACH_WAR) iThreat += 100;
    
    return iThreat;
}

// Detect rising threat trend
bool CvDiplomacyAI::IsThreatRising(PlayerTypes ePlayer)
{
    int iNow = GetHistoricalThreat(ePlayer, 0);
    int i5Ago = GetHistoricalThreat(ePlayer, 5);
    
    // 30% increase is concerning
    return (iNow > i5Ago * 130 / 100);
}
```

### Combined Attack Prediction

```cpp
// High-confidence prediction that attack is imminent
bool CvDiplomacyAI::IsAttackLikelyImminent(PlayerTypes ePlayer)
{
    int iWarningSignals = 0;
    
    // Buildup near borders
    if (IsPlayerBuildingUpNearUs(ePlayer))
        iWarningSignals += 2;
    
    // Siege units present
    if (IsSiegeWarningActive(ePlayer))
        iWarningSignals += 3;  // Very strong signal
    
    // Recently turned hostile
    if (HasTurnedHostileRecently(ePlayer, 5))
        iWarningSignals += 2;
    
    // Getting closer via cities
    if (IsPlayerCreepingCloser(ePlayer))
        iWarningSignals += 1;
    
    // Threat level rising
    if (IsThreatRising(ePlayer))
        iWarningSignals += 1;
    
    // 4+ signals = high confidence
    return iWarningSignals >= 4;
}
```

---

## 9. Serialization (Save Compatibility)

### Current Implementation (Not Save Compatible)

The Phase 1 implementation does **not** attempt save compatibility. The memory
buffer is serialized unconditionally after the existing `CvDiplomacyAI` data,
so loading an older save (without the new memory data) will crash.

**Action:** start a fresh game after enabling this change.

### CivMemory Serialization

```cpp
template<typename Visitor>
void CivMemory::Serialize(Visitor& visitor)
{
    visitor(currentIndex);
    visitor(validCount);
    
    for (int i = 0; i < AI_MEMORY_DEPTH; i++)
    {
        visitor(history[i].turn);
        visitor(history[i].warState);
        visitor(history[i].approach);
        visitor(history[i].theirMilitaryNearUs);
        visitor(history[i].theirMilitaryStrength);
        visitor(history[i].proximity);
        visitor(history[i].siegeUnitsNearUs);
        visitor(history[i].navalUnitsNearUs);
        visitor(history[i].militaryRank);
        visitor(history[i].numCities);
        visitor(history[i].goldPerTurn);
        visitor(history[i].numUnitsNearBorders);
        visitor(history[i].numWars);
        visitor(history[i].flags);
    }
}
```

### UnitSightingManager Serialization

```cpp
void CvUnitSightingManager::Read(FDataStream& kStream)
{
    uint32 uiCount;
    kStream >> uiCount;
    
    m_Sightings.resize(uiCount);
    m_SightingIndex.clear();
    
    for (uint32 i = 0; i < uiCount; i++)
    {
        UnitSighting& s = m_Sightings[i];
        kStream >> s.unitId;
        kStream >> s.unitType;
        kStream >> s.owner;
        kStream >> s.x >> s.y;
        kStream >> s.lastSeenTurn;
        kStream >> s.health;
        kStream >> s.flags;
        kStream >> s.lastDeltaX >> s.lastDeltaY;
        kStream >> s.movementPoints;
        kStream >> s.predictedIntent;
        
        m_SightingIndex[MakeKey((PlayerTypes)s.owner, s.unitId)] = i;
    }
}

void CvUnitSightingManager::Write(FDataStream& kStream) const
{
    uint32 uiCount = (uint32)m_Sightings.size();
    kStream << uiCount;
    
    for (uint32 i = 0; i < uiCount; i++)
    {
        const UnitSighting& s = m_Sightings[i];
        kStream << s.unitId;
        kStream << s.unitType;
        kStream << s.owner;
        kStream << s.x << s.y;
        kStream << s.lastSeenTurn;
        kStream << s.health;
        kStream << s.flags;
        kStream << s.lastDeltaX << s.lastDeltaY;
        kStream << s.movementPoints;
        kStream << s.predictedIntent;
    }
}
```

---

## 10. Integration Points

### Where to Call Memory Functions

| Location | Call | Purpose |
|----------|------|---------|
| `CvDiplomacyAI::DoTurn()` START | `CaptureMemorySnapshot()` | Capture state before updates |
| `CvPlayer::doTurnUnits()` | `m_UnitSightings.UpdateGhosts()` | Grow fog uncertainty |
| `CvPlayer::doTurnUnits()` | `m_UnitSightings.CleanupExpired()` | Remove old ghosts |
| Unit movement notification | `OnUnitMoved()` | Track direction into fog |
| Unit visibility change | `OnUnitSeen()` / `OnUnitLeftVisibility()` | Update sightings |

### Decision Functions to Enhance

| Function | Enhancement |
|----------|-------------|
| `DoAggressiveMilitaryStatement()` | Check `AmIOverextended()` before provoking |
| `SelectBestApproachTowardsMajorCiv()` | Factor `HasTurnedHostileRecently()` |
| `GetWarProjection()` | Use `IsPlayerBuildingUpNearUs()` |
| `IsWantsSneakAttack()` | Check if target `IsThreatRising()` toward us |
| `GetDangerPlots()` | Include `CouldGhostThreatenCity()` for fog units |
| `DoUpdateWarTargets()` | Prioritize targets based on threat trends |

---

## 11. Implementation Steps

### Phase 1: Core Structures (1-2 days)

1. Add `TurnSnapshot` struct to `CvDiplomacyAI.h`
2. Add `CivMemory` struct with circular buffer
3. Add `m_Memory` member to `CvDiplomacyAI`
4. Implement `CaptureMemorySnapshot()` basic version
5. Add serialization (not save compatible)
6. Compile and verify no crashes

### Phase 2: Pattern Detection & Integration (DONE)

1. ~~Implement `IsPlayerBuildingUpNearUs()`~~ (done in Phase 1)
2. ~~Implement `IsSiegeWarningActive()`~~ (done in Phase 1)
3. ~~Implement `IsPlayerCreepingCloser()`~~ (done in Phase 1)
4. ~~Implement `HasTurnedHostileRecently()`~~ (done in Phase 1)
5. ~~Implement `AmIOverextended()`~~ (done in Phase 1)
6. ~~Add logging to verify detection works~~ — `LogMemorySnapshot()` writes to `DiplomacyAI_Memory_Log`
7. ~~Add `CountSiegeUnitsNearUs()` and `CountNavalUnitsNearUs()` helpers~~ — populate snapshot fields
8. ~~Wire patterns into AI decisions:~~
   - `DoAggressiveMilitaryStatement()`: skip if `AmIOverextended()`
   - `SelectBestApproachTowardsMajorCiv()`: boost guarded/hostile on `HasTurnedHostileRecently()`, large boost on `IsAttackLikelyImminent()`

### Phase 3: Unit Tracking (2-3 days)

1. Add `UnitSighting` struct
2. Add `CvUnitSightingManager` class
3. Hook into unit movement notifications
4. Implement fog ghost system
5. Implement directional cone search
6. Add serialization

### Phase 4: Deep Integration (2-3 days)

1. Add buildup warnings to war target prioritization
2. Add fog ghosts to danger plot calculation
3. Add siege warning to defensive positioning
4. Test in gameplay

### Phase 5: Tuning & Validation (1-2 days)

1. Adjust detection thresholds based on gameplay testing
2. Add logging for debugging
3. Verify save compatibility with old saves
4. Memory usage profiling
5. Documentation update

---

## Appendix A: Field Reference

### TurnSnapshot Fields (~312 bytes @ 43 civs, ~445 bytes @ 62 civs)

| Field | Type | Size | Getter to populate |
|-------|------|------|-------------------|
| `turn` | `int16` | 2B | `GC.getGame().getGameTurn()` |
| `warState[]` | `int8[43-62]` | 43-62B | `GetWarState(ePlayer)` |
| `approach[]` | `int8[43-62]` | 43-62B | `GetCivApproach(ePlayer)` |
| `theirMilitaryNearUs[]` | `uint8[43-62]` | 43-62B | `CountCombatUnitsNearUs(ePlayer)` (raw count) |
| `theirMilitaryStrength[]` | `uint8[43-62]` | 43-62B | `GET_PLAYER(ePlayer).GetMilitaryMight()` scaled |
| `proximity[]` | `uint8[43-62]` | 43-62B | `GetPlayer()->GetProximityToPlayer(ePlayer)` |
| `siegeUnitsNearUs[]` | `uint8[43-62]` | 43-62B | New function needed |
| `navalUnitsNearUs[]` | `uint8[43-62]` | 43-62B | New function needed |
| `militaryRank` | `uint8` | 1B | Calculate from `GetMilitaryMight()` ranking |
| `numCities` | `uint8` | 1B | `getNumCities()` |
| `goldPerTurn` | `int16` | 2B | `calculateGoldRate()` |
| `numUnitsNearBorders` | `uint8` | 1B | Sum of `theirMilitaryNearUs[]` |
| `numWars` | `uint8` | 1B | Count active wars |
| `flags` | `uint8` | 1B | Composite bitfield |

### UnitSighting Fields (16 bytes)

| Field | Type | Size | Source |
|-------|------|------|--------|
| `unitId` | `uint16` | 2B | `pUnit->GetID()` |
| `unitType` | `uint8` | 1B | `pUnit->getUnitType()` |
| `owner` | `uint8` | 1B | `pUnit->getOwner()` |
| `x, y` | `int16, int16` | 4B | `pUnit->getX()`, `pUnit->getY()` |
| `lastSeenTurn` | `int16` | 2B | `GC.getGame().getGameTurn()` |
| `health` | `uint8` | 1B | `pUnit->GetCurrHitPoints() * 100 / pUnit->GetMaxHitPoints()` |
| `flags` | `uint8` | 1B | Composite from unit state |
| `lastDeltaX` | `int8` | 1B | Movement delta X |
| `lastDeltaY` | `int8` | 1B | Movement delta Y |
| `movementPoints` | `uint8` | 1B | `pUnit->maxMoves() / GD_INT_GET(MOVE_DENOMINATOR)` |
| `predictedIntent` | `uint8` | 1B | Inferred from context |

---

## Appendix B: Testing Checklist

- [ ] New game: memory populates over first 10 turns
- [ ] Load old save: expected crash (not save compatible)
- [ ] Load new save: history preserved
- [ ] Buildup detection triggers when AI masses units
- [ ] Siege warning triggers when catapults appear
- [ ] Fog ghosts track naval units with correct direction
- [ ] Fog ghosts expire after 10 turns
- [ ] Memory usage stays under 500 KB
- [ ] No performance impact (< 1ms per turn for capture)
