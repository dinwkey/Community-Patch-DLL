# Tourism & Great Works: Detailed Code Recommendations
## Implementation Guide for Priority 1 & 2 Improvements

---

## 1. Consolidate Tourism Modifier Application

### Current Problem
Tourism modifiers scattered across codebase:
- `CvCultureClasses.cpp`: Base tourism calculation
- `CvTradeClasses.cpp:2422`: Trade route tourism
- `CvEspionageClasses.cpp:3314`: Espionage influence
- `CvPlayer.cpp`: Various instant yield calls

Risk of double-application or missed modifiers.

### Proposed Solution

Create unified modifier application in `CvPlayerCulture`:

```cpp
// In CvCultureClasses.h, CvPlayerCulture class:

// Apply tourism with all modifiers
// Returns: actual influence change applied (post-modifier)
int ApplyTourismTowardsCiv(PlayerTypes eTargetPlayer, int iBaseTourism, 
                            bool bApplyModifiers = true, 
                            bool bModifyForGameSpeed = true,
                            bool bShowPopup = false,
                            CvPlot* pPlot = NULL);

// Get tourism modifier without applying (for UI)
int GetTourismModifierWith(PlayerTypes eTargetPlayer, 
                           bool bIgnoreReligion = false, 
                           bool bIgnoreOpenBorders = false, 
                           bool bIgnoreTrade = false, 
                           bool bIgnorePolicies = false, 
                           bool bIgnoreIdeologies = false) const;
```

### Implementation Steps

1. **Move modifier calculation to single function:**
   ```cpp
   // CvCultureClasses.cpp - NEW function
   int CvPlayerCulture::ApplyTourismTowardsCiv(...)
   {
       int iInfluenceChange = iBaseTourism;
       
       if (!bApplyModifiers) {
           ChangeInfluenceOnTimes100(eTargetPlayer, iInfluenceChange * 100);
           return iInfluenceChange;
       }
       
       // Apply all modifiers in order
       int iModifierPercent = 100;
       
       // Religion
       iModifierPercent += GetTourismModifierSharedReligion(eTargetPlayer);
       
       // Trade routes
       iModifierPercent += GetTourismModifierTradeRoute();
       
       // Open borders
       iModifierPercent += GetTourismModifierOpenBorders();
       
       // Policies
       iModifierPercent += GetTourismModifierPolicies(eTargetPlayer);
       
       // Ideologies
       iModifierPercent += GetTourismModifierIdeologies();
       
       // Apply modifiers
       int iModifiedTourism = (iInfluenceChange * iModifierPercent) / 100;
       
       // Game speed scaling
       if (bModifyForGameSpeed) {
           iModifiedTourism = iModifiedTourism * GC.getGame().getGameSpeedInfo()->getTourismModifier() / 100;
       }
       
       // Apply
       int iRealInfluence = ChangeInfluenceOnTimes100(eTargetPlayer, 
                                                       iModifiedTourism * 100, 
                                                       false,  // modifiers already applied
                                                       false); // game speed already applied
       
       // Optional: Show popup
       if (bShowPopup && pPlot && pPlot->GetActiveFogOfWarMode() == FOGOFWARMODE_OFF) {
           InfluenceLevelTypes eLevel = GetInfluenceLevel(eTargetPlayer);
           char text[256];
           sprintf_s(text, "+%d [ICON_TOURISM]", iRealInfluence);
           SHOW_PLOT_POPUP(pPlot, GetID(), text);
       }
       
       return iRealInfluence;
   }
   ```

2. **Update all callers:**
   ```cpp
   // Before:
   int iTourismBlast = GetTourismBlastStrength();
   iTourismBlastAfterModifier = kUnitOwner.GetCulture()->ChangeInfluenceOn(eOwner, 
                                                                             iTourismBlast, 
                                                                             true, true);
   
   // After:
   int iTourismBlast = GetTourismBlastStrength();
   iTourismBlastAfterModifier = kUnitOwner.GetCulture()->ApplyTourismTowardsCiv(eOwner, 
                                                                                   iTourismBlast, 
                                                                                   true, true, 
                                                                                   true, 
                                                                                   pPlot);
   ```

3. **Create helper for modifier audit:**
   ```cpp
   // In CvPlayerCulture - for debugging modifier issues
   struct TourismModifierBreakdown {
       int iBase;
       int iReligion;
       int iTrade;
       int iOpenBorders;
       int iPolicies;
       int iIdeologies;
       int iFinal;
   };
   
   TourismModifierBreakdown GetTourismModifierBreakdown(PlayerTypes eTargetPlayer) const {
       TourismModifierBreakdown bd;
       bd.iBase = 100;
       bd.iReligion = GetTourismModifierSharedReligion(eTargetPlayer);
       bd.iTrade = GetTourismModifierTradeRoute();
       bd.iOpenBorders = GetTourismModifierOpenBorders();
       bd.iPolicies = GetTourismModifierPolicies(eTargetPlayer);
       bd.iIdeologies = GetTourismModifierIdeologies();
       bd.iFinal = bd.iBase + bd.iReligion + bd.iTrade + bd.iOpenBorders + bd.iPolicies + bd.iIdeologies;
       return bd;
   }
   ```

---

## 2. Implement Theming Bonus Caching

### Current Problem
`IsValidForForeignThemingBonus()` called repeatedly per building, iterating:
- All works in building (typically 2-4)
- All eras in game (15+)
- All civilizations (8+)
- All works in game (could be 100+)

**Complexity:** O(B × W × E × C × G) where B=buildings, W=works_per_building, E=eras, C=civs, G=total_works

### Proposed Solution

Cache foreign era/civ combinations per player:

```cpp
// In CvCultureClasses.h, CvPlayerCulture class:

struct ForeignWorkCombination {
    bool bErasSeen[NUM_ERAS];       // Which eras present from OTHER civs
    bool bCivsSeen[MAX_MAJOR_CIVS]; // Which civs present (excluding self)
    int iTurn;                       // Cache validity
};

// Member variable
std::map<PlayerTypes, ForeignWorkCombination> m_ForeignWorkCache;

// Method
bool IsForeignWorkCombinationValid(PlayerTypes eOtherCiv, EraTypes eEra) const {
    auto it = m_ForeignWorkCache.find(eOtherCiv);
    if (it == m_ForeignWorkCache.end()) {
        return false;
    }
    return it->second.bErasSeen[eEra];
}

// Rebuild cache (called when great work changes)
void UpdateForeignWorkCache();

// Clear cache (called at turn boundary or on work change)
void InvalidateForeignWorkCache() {
    m_ForeignWorkCache.clear();
}
```

### Implementation

```cpp
// CvCultureClasses.cpp

void CvPlayerCulture::UpdateForeignWorkCache()
{
    InvalidateForeignWorkCache();
    
    // Build cache for each other player
    for (int iLoopPlayer = 0; iLoopPlayer < MAX_MAJOR_CIVS; iLoopPlayer++) {
        PlayerTypes eLoopPlayer = (PlayerTypes)iLoopPlayer;
        if (eLoopPlayer == m_pPlayer->GetID() || !GET_PLAYER(eLoopPlayer).isAlive()) {
            continue;
        }
        
        ForeignWorkCombination combo;
        combo.iTurn = GC.getGame().getGameTurn();
        
        // Find all works by other civs in our empire
        int iCityLoop = 0;
        for (CvCity* pCity = m_pPlayer->firstCity(&iCityLoop); pCity != NULL; pCity = m_pPlayer->nextCity(&iCityLoop)) {
            for (int iBuildingClassLoop = 0; iBuildingClassLoop < GC.getNumBuildingClassInfos(); iBuildingClassLoop++) {
                BuildingClassTypes eBuildingClass = (BuildingClassTypes)iBuildingClassLoop;
                int iSlots = pCity->GetCityCulture()->GetNumFilledGreatWorkSlots(GREAT_WORK_SLOT_ANY);
                
                for (int iSlot = 0; iSlot < iSlots; iSlot++) {
                    int iGreatWorkIndex = pCity->GetCityCulture()->GetGreatWorkIndex(eBuildingClass, iSlot);
                    if (iGreatWorkIndex >= 0) {
                        CvGameCulture* pGameCulture = GC.getGame().GetGameCulture();
                        PlayerTypes eCreator = pGameCulture->GetGreatWorkCreator(iGreatWorkIndex);
                        EraTypes eEra = (EraTypes)GC.getGame().GetGameCulture()->GetGreatWorkEra(iGreatWorkIndex);
                        
                        if (eCreator != m_pPlayer->GetID()) {
                            combo.bCivsSeen[eCreator] = true;
                            if (eEra != NO_ERA) {
                                combo.bErasSeen[eEra] = true;
                            }
                        }
                    }
                }
            }
        }
        
        m_ForeignWorkCache[eLoopPlayer] = combo;
    }
}

// In theming calculation:
bool CvCultureClasses::IsValidForForeignThemingBonus(
    CvThemingBonusInfo *pBonusInfo, 
    EraTypes eEra, 
    vector<EraTypes> &aForeignErasSeen, 
    vector<EraTypes> &aErasSeen, 
    PlayerTypes ePlayer, 
    vector<PlayerTypes> &aForeignPlayersSeen, 
    vector<PlayerTypes> &aPlayersSeen, 
    PlayerTypes eOwner)
{
    // Check cache first
    CvPlayerCulture* pCulture = GET_PLAYER(eOwner).GetCulture();
    for (int iEra = 0; iEra < NUM_ERAS; iEra++) {
        if (pBonusInfo->RequiresEra((EraTypes)iEra)) {
            bool bHaveForeignEra = pCulture->IsForeignWorkCombinationValid(ePlayer, (EraTypes)iEra);
            if (!bHaveForeignEra) {
                return false; // Missing required foreign era
            }
        }
    }
    
    // Continue with existing logic for civs, etc.
    return true;
}
```

### Integration Points

- Call `InvalidateForeignWorkCache()` in:
  - `CvPlayerCulture::MoveWorkIntoSlot()`
  - `CvGameCulture::MoveGreatWorks()`
  - `CvGameCulture::SwapGreatWorks()`
  - Turn change (`CvPlayerCulture::DoTurn()`)

---

## 3. Implement Influence Decay (Optional but Recommended)

### Current Problem
- Influence only increases; never decreases
- Encourages "lock in" where dominant civs can't be challenged
- Culture victory becomes static after first civ reaches dominant

### Proposed Solution

Add per-turn decay to influence that doesn't receive tourism:

```cpp
// In CvCultureClasses.h, CvPlayerCulture class:

// Configuration
static const int INFLUENCE_DECAY_PER_TURN_TIMES100 = 25; // 0.25 per turn = -1 per 4 turns
static const int INFLUENCE_DECAY_MINIMUM_LEVEL = INFLUENCE_LEVEL_FAMILIAR; // Don't decay below familiar

// New method
void DoInfluenceDecay();
```

### Implementation

```cpp
// CvCultureClasses.cpp

void CvPlayerCulture::DoInfluenceDecay()
{
    if (!MOD_BALANCE_INFLUENCE_DECAY) {
        return; // Feature flag for mod balance
    }
    
    // Decay influence that didn't receive tourism this turn
    for (int iLoopPlayer = 0; iLoopPlayer < MAX_MAJOR_CIVS; iLoopPlayer++) {
        PlayerTypes eLoopPlayer = (PlayerTypes)iLoopPlayer;
        if (eLoopPlayer == m_pPlayer->GetID() || !GET_PLAYER(eLoopPlayer).isAlive()) {
            continue;
        }
        
        // Check if we gained tourism toward this player this turn
        int iThisTurnTourism = GetLastTurnInfluenceIPTTimes100(eLoopPlayer);
        
        // If no tourism, decay influence
        if (iThisTurnTourism <= 0) {
            int iCurrentInfluenceTimes100 = GetInfluenceOnTimes100(eLoopPlayer);
            int iDecayAmount = INFLUENCE_DECAY_PER_TURN_TIMES100;
            
            // Never decay below familiar level
            int iMinimumInfluenceTimes100 = GD_INT_GET(INFLUENCE_LEVEL_FAMILIAR) * 100;
            int iNewInfluenceTimes100 = std::max(iCurrentInfluenceTimes100 - iDecayAmount, 
                                                  iMinimumInfluenceTimes100);
            
            if (iNewInfluenceTimes100 < iCurrentInfluenceTimes100) {
                // Apply decay
                int iDecay = iCurrentInfluenceTimes100 - iNewInfluenceTimes100;
                ChangeInfluenceOnTimes100(eLoopPlayer, -iDecay);
                
                // Notify player if they drop an influence level
                InfluenceLevelTypes eOldLevel = GetInfluenceLevel(eLoopPlayer);
                InfluenceLevelTypes eNewLevel = eOldLevel; // Will recalc on next check
                if (eNewLevel < eOldLevel) {
                    CvNotifications* pNotifications = m_pPlayer->GetNotifications();
                    if (pNotifications) {
                        Localization::String strMessage = Localization::Lookup("TXT_KEY_NOTIFICATION_INFLUENCE_DECLINED");
                        strMessage << GET_PLAYER(eLoopPlayer).getCivilizationShortDescriptionKey();
                        pNotifications->Add(NOTIFICATION_CULTURE_VICTORY_SOMEONE_INFLUENTIAL, 
                                          strMessage.toUTF8(), 
                                          "",
                                          m_pPlayer->getCapitalCity()->getX(),
                                          m_pPlayer->getCapitalCity()->getY(),
                                          eLoopPlayer);
                    }
                }
            }
        }
    }
}

// Call from DoTurn:
void CvPlayerCulture::DoTurn()
{
    // ... existing code ...
    
    // At end of turn:
    DoInfluenceDecay();
}
```

### Database Configuration

Add to GameDefines.xml:

```xml
<Definition>
    <DefinitionType>INFLUENCE_DECAY_PER_TURN_TIMES100</DefinitionType>
    <Value>25</Value>
    <Description>Influence decay per turn when tourism stops (in 1/100ths)</Description>
</Definition>

<Definition>
    <DefinitionType>INFLUENCE_DECAY_MINIMUM_LEVEL</DefinitionType>
    <Value>2</Value>
    <Description>Minimum influence level (don't decay below FAMILIAR=2)</Description>
</Definition>

<Definition>
    <DefinitionType>MOD_BALANCE_INFLUENCE_DECAY</DefinitionType>
    <Value>1</Value>
    <Description>Enable influence decay when tourism stops</Description>
</Definition>
```

### Testing Scenarios

1. **Scenario A:** Reach Dominant on NPC, stop generating tourism
   - Expected: Influence decays 1 level per N turns
   - Verify: Decay rate matches define

2. **Scenario B:** Competing culture victory
   - Player A: 4 civs Influential, 2 civs Exotic
   - Player B: 3 civs Dominant, 3 civs Familiar
   - Player B's Dominant civs should decay if Player A increases tourism
   - Expected: Dynamic, competitive gameplay

---

## 4. Duplicate Work Placement Prevention

### Current Problem
`MoveWorkIntoSlot()` doesn't check if work already placed elsewhere.

### Proposed Solution

```cpp
// In CvCultureClasses.h:

// Validate great work placement
// Returns: true if valid, false if work already placed
bool IsValidGreatWorkPlacement(int iGreatWorkIndex) const;

// Get location of placed great work
// Returns: true if placed, fills out city/building/slot
bool GetGreatWorkLocation(int iGreatWorkIndex, int& iOutCityID, BuildingTypes& eOutBuilding, int& iOutSlot) const;
```

### Implementation

```cpp
// CvCultureClasses.cpp

bool CvGameCulture::IsValidGreatWorkPlacement(int iGreatWorkIndex) const
{
    // Check if work already placed in a building
    BuildingTypes eBuilding = NO_BUILDING;
    CvCity* pCity = GetGreatWorkCity(iGreatWorkIndex, eBuilding);
    
    // If in a building, return false (already placed)
    if (pCity != NULL && eBuilding != NO_BUILDING) {
        return false;
    }
    
    // Otherwise valid
    return true;
}

// In MoveWorkIntoSlot():
bool CvPlayerCulture::MoveWorkIntoSlot(int iWorkID, int iToCityID, BuildingTypes eToBuilding, 
                                       int iToSlot, vector<CvGreatWorkAvailableForUse>& works1, 
                                       vector<CvGreatWorkAvailableForUse>& works2, 
                                       const set<int>* toIgnore)
{
    // NEW: Validate work not already placed
    if (!GC.getGame().GetGameCulture()->IsValidGreatWorkPlacement(iWorkID)) {
        ASSERT(false, "Attempting to place great work that's already in another building!");
        return false;
    }
    
    // ... existing code ...
}
```

---

## 5. Create Tourism Modifier Registry (Optional, Lower Priority)

### Proposed Structure

```cpp
// In CvCultureClasses.h:

enum TourismModifierSource {
    TOURISM_MODIFIER_RELIGION,
    TOURISM_MODIFIER_TRADE_ROUTE,
    TOURISM_MODIFIER_OPEN_BORDERS,
    TOURISM_MODIFIER_POLICIES,
    TOURISM_MODIFIER_IDEOLOGIES,
    TOURISM_MODIFIER_VASSALAGE,
    TOURISM_MODIFIER_CORPORATION,
    NUM_TOURISM_MODIFIER_SOURCES
};

struct TourismModifier {
    TourismModifierSource eSource;
    int iValue;  // In percent (e.g., 50 = +50%)
    PlayerTypes eTargetPlayer;  // May be NO_PLAYER for global modifiers
};

// Registry in CvPlayerCulture:
class CvPlayerCulture {
    // ...
    
    // Add/remove modifiers
    void RegisterTourismModifier(TourismModifierSource eSource, int iValue, PlayerTypes eTarget = NO_PLAYER);
    void UnregisterTourismModifier(TourismModifierSource eSource, PlayerTypes eTarget = NO_PLAYER);
    
    // Query registry
    int GetTourismModifierValue(TourismModifierSource eSource, PlayerTypes eTarget = NO_PLAYER) const;
    int GetTotalTourismModifier(PlayerTypes eTarget) const;
    
private:
    std::map<std::pair<TourismModifierSource, PlayerTypes>, int> m_TourismModifiers;
};
```

---

## 6. Summary of Changes by File

### CvCultureClasses.h
- Add `ApplyTourismTowardsCiv()` method
- Add `UpdateForeignWorkCache()`, `InvalidateForeignWorkCache()`
- Add `GetTourismModifierBreakdown()` for debugging
- Add optional decay methods and config

### CvCultureClasses.cpp
- Implement `ApplyTourismTowardsCiv()`
- Implement cache update/invalidate logic
- Implement `DoInfluenceDecay()` (optional)
- Update `ThemeBuilding()` to use cache

### CvUnit.cpp
- Update tourism blast to call `ApplyTourismTowardsCiv()` instead of `ChangeInfluenceOn()`
- Update great work creation to call cache invalidation

### CvTradeClasses.cpp
- Update trade route tourism to call `ApplyTourismTowardsCiv()`

### CvEspionageClasses.cpp
- Update espionage tourism to call `ApplyTourismTowardsCiv()`

### CvPlayer.cpp
- Update instant yield tourism calls to use new path

---

## 7. Testing Checklist

- [ ] Unit test: `ApplyTourismTowardsCiv()` with no modifiers
- [ ] Unit test: `ApplyTourismTowardsCiv()` with single modifier
- [ ] Unit test: `ApplyTourismTowardsCiv()` with stacked modifiers
- [ ] Unit test: Cache invalidation on work change
- [ ] Integration test: Full game to culture victory
- [ ] Integration test: Influence decay scenario
- [ ] Regression test: Existing UI still displays correctly
- [ ] Performance test: Late-game theming bonus calculation
- [ ] AI test: AI still correctly evaluates culture victory path

---

**Status:** Recommended for implementation in next release  
**Estimated Effort:** Priority 1 = 20-40 hours | Priority 2 (decay) = 10-15 hours
