# Moroccan UA Trade Plundering - Implementation Guide

## Overview
This document provides step-by-step implementation guidance for fixing the Moroccan UA trade plundering issue by adding diplomatic status checks.

---

## Step 1: Modify CvUnit::canPlunderTradeRoute()

**File:** `CvGameCoreDLL_Expansion2/CvUnit.cpp`  
**Current Lines:** 9103-9180  
**Changes Needed:** Add diplomatic status checks before allowing plunder

### Current problematic code (lines 9158-9168):
```cpp
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
    if (eTradeUnitDest != m_eOwner)
    {
        return true;
    }
}
```

### Proposed replacement (with diplomatic checks):
```cpp
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
    if (eTradeUnitDest != m_eOwner)
    {
        // NEW: Get the trade unit owner for diplomatic checks
        PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
        
        if (eTradeUnitOwner != NO_PLAYER)
        {
            TeamTypes eMoroccoTeam = m_pPlayer->getTeam();
            TeamTypes eOwnerTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
            
            // Do NOT plunder allied nations
            if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam))
            {
                // Check for formal alliance
                if (GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) >= ALLIANCE_LEVEL_DEFENSIVE_PACT)
                {
                    bShowTooltip = true;
                    continue;  // Skip this trade route, cannot plunder
                }
            }
            
            // Do NOT plunder vassals or overlords
            if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam))
            {
                bShowTooltip = true;
                continue;  // Skip this trade route, we're their vassal
            }
            if (GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
            {
                bShowTooltip = true;
                continue;  // Skip this trade route, they're our vassal
            }
            
            // Do NOT plunder civs we're afraid of
            if (GET_PLAYER(m_eOwner).GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
            {
                bShowTooltip = true;
                continue;  // Skip this trade route, we fear this civ
            }
            
            // All checks passed - can plunder rivals/neutrals/enemies
            return true;
        }
    }
}
```

---

## Step 2: Modify CvUnit::plunderTradeRoute()

**File:** `CvGameCoreDLL_Expansion2/CvUnit.cpp`  
**Current Lines:** 9180-9238  
**Changes Needed:** Mirror the diplomatic checks from canPlunderTradeRoute()

### Current problematic code (lines 9222-9228):
```cpp
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
    if (eTradeUnitDest != m_eOwner)
    {
        bValidTarget = true;
    }
}
```

### Proposed replacement:
```cpp
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
    if (eTradeUnitDest != m_eOwner)
    {
        // NEW: Get the trade unit owner for diplomatic checks
        PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
        
        if (eTradeUnitOwner != NO_PLAYER)
        {
            TeamTypes eMoroccoTeam = m_pPlayer->getTeam();
            TeamTypes eOwnerTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
            
            // Do NOT plunder allied nations
            if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam))
            {
                if (GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) >= ALLIANCE_LEVEL_DEFENSIVE_PACT)
                {
                    continue;  // Skip this trade route, cannot plunder ally
                }
            }
            
            // Do NOT plunder vassals or overlords
            if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) || 
                GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
            {
                continue;  // Skip this trade route, vassal relationship
            }
            
            // Do NOT plunder civs we're afraid of
            if (GET_PLAYER(m_eOwner).GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
            {
                continue;  // Skip this trade route, we fear this civ
            }
            
            // All checks passed - can plunder
            bValidTarget = true;
        }
    }
}
```

---

## Step 3: Update Lua Helper Function

**File:** `CvGameCoreDLL_Expansion2/Lua/CvLuaPlayer.cpp`  
**Function:** `lGetReasonPlunderTradeRouteDisabled()` (around line 10870)  
**Purpose:** Provide tooltip text explaining why plundering is disabled

### Current code context (lines 10870-10900):
The function checks various conditions and returns localized strings. We need to add checks for diplomatic conditions.

### Add new conditions before line 10895:
```cpp
// Check for Morocco UA with diplomatic restrictions
if (pkPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    // Check if target is an ally
    // This requires iterating through visible trade routes and checking their owners
    // For now, we can add a generic message if any visible route is protected
    
    CvGameTrade* pGameTrade = GC.getGame().GetGameTrade();
    std::vector<int> aiTradeUnitsAtPlot;
    aiTradeUnitsAtPlot = pkPlayer->GetTrade()->GetOpposingTradeUnitsAtPlot(pPlot, false);
    
    for (uint uiTradeRoute = 0; uiTradeRoute < aiTradeUnitsAtPlot.size(); uiTradeRoute++)
    {
        PlayerTypes eTradeUnitOwner = pGameTrade->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
        if (eTradeUnitOwner == NO_PLAYER)
            continue;
        
        TeamTypes eMoroccoTeam = pkPlayer->getTeam();
        TeamTypes eOwnerTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
        
        // Check alliance
        if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam) &&
            GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) >= ALLIANCE_LEVEL_DEFENSIVE_PACT)
        {
            lua_pushstring(L, "TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED");
            return 1;
        }
        
        // Check vassal
        if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) || 
            GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
        {
            lua_pushstring(L, "TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_VASSAL");
            return 1;
        }
        
        // Check afraid
        if (pkPlayer->GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
        {
            lua_pushstring(L, "TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_AFRAID");
            return 1;
        }
    }
}
```

---

## Step 4: Add Localization Strings

**Files affected:**
- `(2) Vox Populi/Database Changes/Text/en_US/Units/MissionTextChanges.sql`
- Or create a new SQL file: `Database Changes/Text/en_US/Units/MoroccoUAFixes.sql`

### Add new text keys:
```sql
-- Plunder Trade Route blockers for Morocco UA
INSERT OR REPLACE INTO LocalizedText (Language, Tag, Text)
VALUES 
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED', 
    'Cannot plunder trade route of allied nation.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_VASSAL',
    'Cannot plunder trade route of vassal or overlord.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_AFRAID',
    'We are too afraid of this civ to plunder their trade route.');
```

Or if using the `.modinfo` format for localization (in XML):
```xml
<Row Tag="TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED">
    <Text>Cannot plunder trade route of allied nation.</Text>
</Row>
<Row Tag="TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_VASSAL">
    <Text>Cannot plunder trade route of vassal or overlord.</Text>
</Row>
<Row Tag="TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_AFRAID">
    <Text>We are too afraid of this civ to plunder their trade route.</Text>
</Row>
```

---

## Step 5: Build and Test

### Build steps:
```powershell
# From Community-Patch-DLL root
cd C:\Users\Thomson\source\repos\Community-Patch-DLL

# Build with clang
.\build_vp_clang.ps1 -Config debug

# Wait for completion, check output
Get-Content clang-output/Debug/build.log | Select-Object -Last 50
```

### Expected build output:
```
Compiling: CvUnit.cpp
Compiling: CvLuaPlayer.cpp
Linking: CvGameCore_Expansion2.dll
[SUCCESS] clang-build: debug successful
```

### Manual testing:
1. Copy generated DLL to mod: `(1) Community Patch/Assets/CvGameCore_Expansion2.dll`
2. Launch Civ5 and enable Community Patch + VP
3. Start game as Morocco
4. Create Berber Cavalry unit
5. Test scenarios:
   - Try to plunder allied civ's trade route → Should FAIL
   - Try to plunder vassal's trade route → Should FAIL  
   - Try to plunder rival's trade route → Should SUCCEED
   - Try to plunder enemy's trade route → Should SUCCEED

---

## Diplo Penalty Adjustment (Optional)

The current code applies heavy diplo penalties when plundering without visibility. If diplomatic checks are added, consider adjusting penalties:

**File:** `CvTradeClasses.cpp` (lines 5052-5060)

### Current:
```cpp
if (m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    // Diplo penalty with owner
    if (pPlunderPlot->isVisible(eOwningTeam))
    {
        GET_PLAYER(eOwningPlayer).GetDiplomacyAI()->ChangeNumTradeRoutesPlundered(m_pPlayer->GetID(), 3);
    }
    // ...
}
```

### Optional adjustment (less penalty if we need relationship):
```cpp
if (m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    // Diplo penalty with owner (only if not already at war)
    if (pPlunderPlot->isVisible(eOwningTeam) && !GET_TEAM(m_pPlayer->getTeam()).isAtWar(eOwningTeam))
    {
        // Penalty for plundering peaceful civ's trade route
        GET_PLAYER(eOwningPlayer).GetDiplomacyAI()->ChangeNumTradeRoutesPlundered(m_pPlayer->GetID(), 3);
    }
}
```

This makes the diplo penalty only apply if we're not already at war.

---

## Verification Checklist

- [ ] Code compiles without errors (clang-cl debug)
- [ ] Code compiles without errors (clang-cl release)
- [ ] VC9 compatibility verified (if possible)
- [ ] DLL generated at `clang-output/Debug/CvGameCore_Expansion2.dll`
- [ ] DLL copied to Community Patch Core mod
- [ ] Civ5 launches without crash
- [ ] Morocco can still plunder rivals/neutrals/enemies
- [ ] Morocco cannot plunder allies
- [ ] Morocco cannot plunder vassals
- [ ] Morocco cannot plunder civs it fears
- [ ] Tooltips display correct blockage reasons
- [ ] Notifications still work correctly

---

## Notes & Caveats

1. **Alliance Strength Check**: Adjust `ALLIANCE_LEVEL_DEFENSIVE_PACT` to `ALLIANCE_LEVEL_RESEARCH_AGREEMENT` if you want stricter protection
2. **Fear Mechanic**: The `IsAfraidOf()` check might be too strict—consider changing to only block if fear level is critical
3. **Performance**: Diplomatic checks happen frequently; this should have minimal impact but monitor in large games
4. **Mod Compatibility**: This change only affects Morocco; other civs with `CanPlunderWithoutWar` (if any) will also be restricted
5. **UI**: Tooltip updates require the player to hover over the unit action button; ensure this works correctly

---

## Rollback Plan

If issues arise:
1. Revert changes to `CvUnit.cpp` and `CvLuaPlayer.cpp`
2. Rebuild DLL: `.\build_vp_clang.ps1 -Config debug`
3. Replace modified DLL with backed-up version
4. Test again

---

## Future Enhancements

After this fix is implemented and tested, consider:
1. **Interactive response** (popup asking victim's reaction)
2. **Escalation system** (automatic minor war declaration option)
3. **Ransom/negotiation** (allow victim to pay gold to stop)
4. **Visibility penalties** (discovered plundering breaks open borders)

