# Moroccan UA Trade Plundering Review

## Issue Summary
Morocco's Unique Ability "Gateway to Africa" includes the `CanPlunderWithoutWar` trait that allows Moroccan units to plunder other players' trade routes **without being at war**. Currently, the system **does not check friendly/hostile status** before plundering, meaning Morocco can plunder even friendly or allied civs' trade units. Additionally, there is **no feedback mechanism** for the plundered player—they discover it happened through a notification, but have no way to make Morocco stop or face consequences.

---

## Current Implementation Analysis

### 1. **Moroccan UA Definition**
**File:** `(2) Vox Populi\Database Changes\Civilizations\Morocco.sql`

```sql
UPDATE Traits
SET
	NoTradeRouteProximityPenalty = 1,
	CanPlunderWithoutWar = 1
WHERE Type = 'TRAIT_GATEWAY_AFRICA';
```

The trait enables:
- No trade route proximity penalty for Morocco's own routes
- Ability to plunder without declaring war

---

### 2. **Plundering Logic - Trade Route Check**
**File:** `CvUnit.cpp` (lines 9103-9180)

```cpp
bool CvUnit::canPlunderTradeRoute(const CvPlot* pPlot, bool bOnlyTestVisibility) const
{
    // ... validation checks ...
    
    if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
    {
        PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
        if (eTradeUnitDest != m_eOwner)  // <- Only checks: is it NOT going to Morocco?
        {
            return true;  // Can plunder if destination is not self
        }
    }
}
```

**Problem:** The check only verifies that the trade route is **not destined for Morocco itself**. It does **NOT** check:
- Diplomatic status (ally, friendly, afraid, etc.)
- Open borders agreement
- Any form of relationship
- War status (though it's called "WithoutWar", it should still respect allies!)

---

### 3. **Actual Plundering Execution**
**File:** `CvTradeClasses.cpp` (lines 4964-5169)

The `PlunderTradeRoute()` function handles consequences:

```cpp
if (m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    // Diplo penalty with owner
    if (pPlunderPlot->isVisible(eOwningTeam))
    {
        GET_PLAYER(eOwningPlayer).GetDiplomacyAI()->ChangeNumTradeRoutesPlundered(m_pPlayer->GetID(), 3);
    }
    // Diplo penalty with destination civ if not at war
    if (eOwningPlayer != eDestPlayer && GET_PLAYER(eDestPlayer).isMajorCiv() 
        && pPlunderPlot->isVisible(eDestTeam) 
        && !GET_TEAM(m_pPlayer->getTeam()).isAtWar(eDestTeam))
    {
        GET_PLAYER(eDestPlayer).GetDiplomacyAI()->ChangeNumTradeRoutesPlundered(m_pPlayer->GetID(), 1);
    }
}
```

**Current State:**
- ✅ **Does** apply diplomatic penalties (+3 and +1 to trade routes plundered counter)
- ✅ **Does** notify the owner and destination player
- ❌ **Does NOT** allow the plundered player to request Morocco stop or declare demands
- ❌ **No escalation mechanism** (like discovering a spy)

---

## Issues Identified

### Issue 1: **No Friendly/Allied Status Check**
**Severity:** HIGH

Morocco plunders even **friendly**, **allied**, or **vassal** civs without any consideration. This is exploitative and breaks roleplay/realism.

**Current problematic code flow:**
1. Morocco unit approaches a trade route
2. System checks: "Is it going to Morocco?" → No
3. System allows plundering → Success (regardless of diplomatic status)

### Issue 2: **No Interactive Response System**
**Severity:** MEDIUM

Unlike other clandestine actions (spy discovery, religion spread), plundering has:
- ✅ One-way notification
- ❌ No popup asking plundered player's response
- ❌ No option to demand cessation
- ❌ No conditional consequences (e.g., automatic peace breaks)
- ❌ No ransom/negotiation option

---

## Recommended Solutions

### Solution 1: **Check Diplomatic Status Before Plundering (Recommended)**

**Locations to modify:**
1. `CvUnit::canPlunderTradeRoute()` - Add diplomatic check
2. `CvUnit::plunderTradeRoute()` - Mirror the check

**Implementation approach:**

```cpp
// In CvUnit::canPlunderTradeRoute() around line 9158
if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
{
    PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
    if (eTradeUnitDest != m_eOwner)
    {
        // NEW: Check if we should plunder based on diplomatic status
        PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
        
        // Do NOT plunder allies, vassals, or players we're afraid of
        if (eTradeUnitOwner != NO_PLAYER)
        {
            TeamTypes eOwnerTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
            TeamTypes eOwnerTeam = m_pPlayer->getTeam();
            
            // Check alliance
            if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam) && 
                GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) > ALLIANCE_LEVEL_NONE)
            {
                return false;  // Don't plunder allies
            }
            
            // Check vassal status
            if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) || 
                GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
            {
                return false;  // Don't plunder vassals or overlords
            }
            
            // Check if we're afraid of this player
            if (GET_PLAYER(m_eOwner).GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
            {
                return false;  // Don't plunder civs we fear
            }
            
            return true;  // Can plunder otherwise (neutral/rival/enemy)
        }
    }
}
```

**Advantages:**
- Simple, clear logic
- Prevents unrealistic scenarios (plundering allies)
- Maintains UA uniqueness (still plunders rivals/neutrals without war)
- Minimal performance impact

---

### Solution 2: **Add Response/Escalation System (Optional Enhancement)**

**Add a popup notification system similar to spy discovery:**

```cpp
// In CvPlayerTrade::PlunderTradeRoute() after line 5048
if (!bValidTarget)
    continue;

// NEW: Check if victim wants to respond diplomatically
PlayerTypes eVictimPlayer = eTradeUnitOwner;
if (GET_PLAYER(eVictimPlayer).isHuman() && !m_pPlayer->isHuman())
{
    // Notify victim with choice to:
    // 1. Demand cessation (diplomatic penalty on Morocco)
    // 2. Declare war immediately
    // 3. Ignore it
    
    CvNotifications* pNotifications = GET_PLAYER(eVictimPlayer).GetNotifications();
    if (pNotifications)
    {
        // Create interactive notification with popup asking for response
        // This would require new localization keys and UI handling
    }
}
```

**Advantages:**
- Mirrors spy discovery mechanics
- Gives victims agency
- More interactive gameplay
- Can create narrative moments

**Disadvantages:**
- Requires UI changes
- More complex to implement
- Requires new text keys

---

### Solution 3: **Stricter Automatic War Declaration (Alternative)**

Make plundering an **automatic act of war** if the plundered player chooses to respond:

```cpp
// After detecting plundering, player gets 3 turns to demand:
// "Morocco plundered your trade route. Demand they stop or declare war?"
//
// If plundered player declares war, auto-declaration occurs
// If they ignore, diplomatic penalty increases over time
```

---

## Test Scenarios

### Before Fix:
1. **Scenario A**: Morocco at peace with France
   - Creates Berber Cavalry
   - Plunders France's trade routes to Spain
   - **Current result**: Plundering succeeds, France just gets notification
   - **Expected result**: Should fail or face consequences

2. **Scenario B**: Morocco is allied with England
   - **Current result**: Can still plunder England's routes
   - **Expected result**: Should NOT be able to plunder

### After Fix (with Solution 1):
1. **Scenario A**: Plundering attempt FAILS with message "Cannot plunder trade route—player is at peace"
2. **Scenario B**: Plundering attempt FAILS with message "Cannot plunder trade route—allied nation"
3. **Scenario C**: Morocco vs. Neutral/Rival civ → Plundering SUCCEEDS (UA preserved)

---

## Implementation Priority

| Solution | Priority | Effort | Impact |
|----------|----------|--------|--------|
| Solution 1 (Diplomatic Check) | **HIGH** | Low | High - Fixes core exploit |
| Solution 2 (Escalation System) | MEDIUM | High | Medium - Better UX |
| Solution 3 (Auto War) | LOW | Medium | Low - May be too harsh |

**Recommendation**: Implement Solution 1 immediately (simple, effective), then consider Solution 2 as quality-of-life enhancement.

---

## Files to Modify

1. **CvUnit.cpp** (lines 9103-9180)
   - Add diplomatic checks to `canPlunderTradeRoute()`
   - Mirror checks in `plunderTradeRoute()`

2. **CvLuaPlayer.cpp** (around line 10894)
   - Update `lGetReasonPlunderTradeRouteDisabled()` to include new diplomatic reasons

3. **Text localization** (if using Solution 2)
   - Add new tooltip/notification strings for diplomatic block reasons

---

## Notes for Implementation

⚠️ **IMPORTANT**: Per copilot-instructions.md, **any C++ changes must**:
1. Build successfully with `clang-cl` first: `..\build_vp_clang.ps1 -Config debug`
2. Verify VC9 (Visual C++ 2008 SP1) compatibility
3. Test by copying DLL to `Community Patch Core` mod and running Civ5

✅ **Comment added in code** (line 9105 in CvUnit.cpp) notes:
> "If you change anything here, make sure to also update CvLuaPlayer::lGetReasonPlunderTradeRouteDisabled and CvPlayerTrade::PlunderTradeRoute"

This means **three places must be synchronized** when making changes.
