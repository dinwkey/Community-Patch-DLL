# Morocco UA Fix - Ready-to-Use Code Snippets

This document contains ready-to-use code snippets for fixing the Moroccan UA trade plundering issue.

---

## File 1: CvUnit.cpp - canPlunderTradeRoute() Fix

**Location:** `CvGameCoreDLL_Expansion2/CvUnit.cpp`  
**Lines:** 9103-9180 (approximate)  
**Function:** `CvUnit::canPlunderTradeRoute()`

### FIND THIS (old code to replace):

```cpp
bool CvUnit::canPlunderTradeRoute(const CvPlot* pPlot, bool bOnlyTestVisibility) const
{
	// If you change anything here, make sure to also update CvLuaPlayer::lGetReasonPlunderTradeRouteDisabled and CvPlayerTrade::PlunderTradeRoute
	if (!IsCombatUnit())
	{
		return false;
	}

	if (pPlot == NULL)
	{
		return false;
	}

	if (isEmbarked())
	{
		return false;
	}

	if (MOD_GLOBAL_NO_OCEAN_PLUNDERING && pPlot->isWater() && !pPlot->isShallowWater())
	{
		return false;
	}

	if (GET_PLAYER(m_eOwner).GetTrade()->ContainsOpposingPlayerTradeUnit(pPlot))
	{
		std::vector<int> aiTradeUnitsAtPlot;
		aiTradeUnitsAtPlot = GET_PLAYER(m_eOwner).GetTrade()->GetOpposingTradeUnitsAtPlot(pPlot, false);

		bool bShowTooltip = false;
		for (uint uiTradeRoute = 0; uiTradeRoute < aiTradeUnitsAtPlot.size(); uiTradeRoute++)
		{
			PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
			if (eTradeUnitOwner == NO_PLAYER)
			{
				// invalid TradeUnit
				continue;
			}

			bool bCorporationInvulnerable = false;
			CorporationTypes eCorporation = GET_PLAYER(eTradeUnitOwner).GetCorporations()->GetFoundedCorporation();
			if (eCorporation != NO_CORPORATION)
			{
				CvCorporationEntry* pkCorporation = GC.getCorporationInfo(eCorporation);
				if (pkCorporation && pkCorporation->IsTradeRoutesInvulnerable())
				{
					bCorporationInvulnerable = true;
				}
			}

			if (!bCorporationInvulnerable)
			{
				TeamTypes eTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
				if (GET_TEAM(GET_PLAYER(m_eOwner).getTeam()).isAtWar(eTeam))
					return true;

				if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
				{
					PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
					if (eTradeUnitDest != m_eOwner)
					{
						return true;
					}
				}
			}

			// this TR cannot be plundered because we're not at war with the player or because of their corporation. The action button should be displayed with a tooltip explaining why the TR can't be plundered (unless there's another trade route here that can be plundered)
			bShowTooltip = true;
		}
		return bOnlyTestVisibility && bShowTooltip;
	}
	else
	{
		return false;
	}
}
```

### REPLACE WITH (new code with diplomatic checks):

```cpp
bool CvUnit::canPlunderTradeRoute(const CvPlot* pPlot, bool bOnlyTestVisibility) const
{
	// If you change anything here, make sure to also update CvLuaPlayer::lGetReasonPlunderTradeRouteDisabled and CvPlayerTrade::PlunderTradeRoute
	if (!IsCombatUnit())
	{
		return false;
	}

	if (pPlot == NULL)
	{
		return false;
	}

	if (isEmbarked())
	{
		return false;
	}

	if (MOD_GLOBAL_NO_OCEAN_PLUNDERING && pPlot->isWater() && !pPlot->isShallowWater())
	{
		return false;
	}

	if (GET_PLAYER(m_eOwner).GetTrade()->ContainsOpposingPlayerTradeUnit(pPlot))
	{
		std::vector<int> aiTradeUnitsAtPlot;
		aiTradeUnitsAtPlot = GET_PLAYER(m_eOwner).GetTrade()->GetOpposingTradeUnitsAtPlot(pPlot, false);

		bool bShowTooltip = false;
		for (uint uiTradeRoute = 0; uiTradeRoute < aiTradeUnitsAtPlot.size(); uiTradeRoute++)
		{
			PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
			if (eTradeUnitOwner == NO_PLAYER)
			{
				// invalid TradeUnit
				continue;
			}

			bool bCorporationInvulnerable = false;
			CorporationTypes eCorporation = GET_PLAYER(eTradeUnitOwner).GetCorporations()->GetFoundedCorporation();
			if (eCorporation != NO_CORPORATION)
			{
				CvCorporationEntry* pkCorporation = GC.getCorporationInfo(eCorporation);
				if (pkCorporation && pkCorporation->IsTradeRoutesInvulnerable())
				{
					bCorporationInvulnerable = true;
				}
			}

			if (!bCorporationInvulnerable)
			{
				TeamTypes eTeam = GET_PLAYER(eTradeUnitOwner).getTeam();
				if (GET_TEAM(GET_PLAYER(m_eOwner).getTeam()).isAtWar(eTeam))
					return true;

				if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
				{
					PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
					if (eTradeUnitDest != m_eOwner)
					{
						// NEW: Check diplomatic status before allowing plunder without war
						TeamTypes eMoroccoTeam = m_eOwner;
						TeamTypes eOwnerTeam = eTeam;
						
						// Do NOT plunder allied nations
						if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam) &&
							GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) >= ALLIANCE_LEVEL_DEFENSIVE_PACT)
						{
							bShowTooltip = true;
							continue;  // Skip this route - cannot plunder
						}
						
						// Do NOT plunder vassals or overlords
						if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) ||
							GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
						{
							bShowTooltip = true;
							continue;  // Skip this route - cannot plunder
						}
						
						// Do NOT plunder civs we're afraid of
						if (GET_PLAYER(m_eOwner).GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
						{
							bShowTooltip = true;
							continue;  // Skip this route - cannot plunder
						}
						
						// All checks passed - can plunder
						return true;
					}
				}
			}

			// this TR cannot be plundered because we're not at war with the player or because of their corporation. The action button should be displayed with a tooltip explaining why the TR can't be plundered (unless there's another trade route here that can be plundered)
			bShowTooltip = true;
		}
		return bOnlyTestVisibility && bShowTooltip;
	}
	else
	{
		return false;
	}
}
```

---

## File 2: CvUnit.cpp - plunderTradeRoute() Fix

**Location:** `CvGameCoreDLL_Expansion2/CvUnit.cpp`  
**Lines:** 9180-9238 (approximate)  
**Function:** `CvUnit::plunderTradeRoute()`

### FIND THIS (old code to replace):

```cpp
	bool bValidTarget = false;

	if (GET_TEAM(GET_PLAYER(m_eOwner).getTeam()).isAtWar(eTeam))
		bValidTarget = true;

	if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
	{
		PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
		if (eTradeUnitDest != m_eOwner)
		{
			bValidTarget = true;
		}
	}
	if (bValidTarget)
```

### REPLACE WITH (new code with diplomatic checks):

```cpp
	bool bValidTarget = false;

	if (GET_TEAM(GET_PLAYER(m_eOwner).getTeam()).isAtWar(eTeam))
		bValidTarget = true;

	if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsCanPlunderWithoutWar())
	{
		PlayerTypes eTradeUnitDest = GC.getGame().GetGameTrade()->GetDestFromID(aiTradeUnitsAtPlot[uiTradeRoute]);
		if (eTradeUnitDest != m_eOwner)
		{
			// NEW: Check diplomatic status before allowing plunder without war
			TeamTypes eMoroccoTeam = m_eOwner;
			TeamTypes eOwnerTeam = eTeam;
			
			// Do NOT plunder allied nations
			if (GET_TEAM(eMoroccoTeam).IsAtPeace(eOwnerTeam) &&
				GET_TEAM(eMoroccoTeam).GetAllianceStrength(eOwnerTeam) >= ALLIANCE_LEVEL_DEFENSIVE_PACT)
			{
				continue;  // Skip this route - cannot plunder
			}
			
			// Do NOT plunder vassals or overlords
			if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) ||
				GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
			{
				continue;  // Skip this route - cannot plunder
			}
			
			// Do NOT plunder civs we're afraid of
			if (GET_PLAYER(m_eOwner).GetDiplomacyAI()->IsAfraidOf(eTradeUnitOwner))
			{
				continue;  // Skip this route - cannot plunder
			}
			
			// All checks passed - can plunder
			bValidTarget = true;
		}
	}
	if (bValidTarget)
```

---

## File 3: SQL - Localization Strings

**Location:** Create new file or add to existing:  
`(2) Vox Populi/Database Changes/Text/en_US/Units/MoroccoUAFixes.sql`

```sql
-- ============================================
-- Morocco UA - Trade Plundering Fixes
-- Localization strings for diplomatic blockers
-- ============================================

UPDATE Locale_en_US
SET Text = 'Cannot plunder trade route of allied nation.'
WHERE Tag = 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED';

INSERT OR IGNORE INTO Locale_en_US (Language, Tag, Text)
VALUES 
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_ALLIED', 
    'Cannot plunder trade route of allied nation.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_VASSAL',
    'Cannot plunder trade route of vassal or overlord.'),
('en_US', 'TXT_KEY_MISSION_PLUNDER_TRADE_ROUTE_DISABLED_AFRAID',
    'We are too afraid of this civ to plunder their trade route.');
```

Or if using XML format (in `Text/en_US/UI/NewUIText.xml` or similar):

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

## File 4: CvLuaPlayer.cpp - Tooltip Update (Optional)

**Location:** `CvGameCoreDLL_Expansion2/Lua/CvLuaPlayer.cpp`  
**Function:** `lGetReasonPlunderTradeRouteDisabled()`  
**Lines:** ~10870-10900  

### ADD THIS CODE (before the final return statement):

```cpp
// Check for Morocco UA with diplomatic restrictions
if (pkPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
{
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

## Testing Checklist - Copy This

```
MOROCCO UA FIX - TEST PLAN
==========================

Scenario 1: Plunder Ally (should FAIL)
□ Create Morocco
□ Form defensive pact with neighbor
□ Create Berber Cavalry
□ Approach ally's trade route
□ Try to plunder
□ EXPECTED: Button disabled, tooltip says "Cannot plunder trade route of allied nation"
□ ACTUAL: _____________________

Scenario 2: Plunder Vassal (should FAIL)
□ Create Morocco
□ Make another civ a vassal (via diplomacy)
□ Create Berber Cavalry
□ Approach vassal's trade route
□ Try to plunder
□ EXPECTED: Button disabled, tooltip says "Cannot plunder trade route of vassal or overlord"
□ ACTUAL: _____________________

Scenario 3: Plunder Rival (should SUCCEED)
□ Create Morocco
□ Meet rival civ (no alliance)
□ Create Berber Cavalry
□ Approach rival's trade route
□ Try to plunder
□ EXPECTED: Button enabled, plundering works
□ ACTUAL: _____________________

Scenario 4: Plunder Enemy (should SUCCEED)
□ Create Morocco
□ Declare war on another civ
□ Create Berber Cavalry
□ Approach enemy's trade route
□ Try to plunder
□ EXPECTED: Button enabled, plundering works
□ ACTUAL: _____________________

Scenario 5: Own Trade Route (should NEVER plunder)
□ Create Morocco
□ Create trade route TO Morocco from another civ
□ Create Berber Cavalry
□ Approach own incoming trade route
□ Try to plunder
□ EXPECTED: Button disabled, tooltip says "Can't plunder routes going to your own city"
□ ACTUAL: _____________________

Crash Test:
□ Game launches without crash
□ No runtime errors in Civ5 debug log
□ Can complete full game turn
□ No UI glitches
```

---

## Build Command

```powershell
# From: C:\Users\Thomson\source\repos\Community-Patch-DLL

# Clean build
Remove-Item .\clang-output\Debug -Force -Recurse -ErrorAction SilentlyContinue

# Build
.\build_vp_clang.ps1 -Config debug

# Wait for completion (10-20 minutes)

# Verify success
if (Test-Path .\clang-output\Debug\CvGameCore_Expansion2.dll) {
    Write-Host "✓ Build successful!"
} else {
    Write-Host "✗ Build failed!"
    Get-Content .\clang-output\Debug\build.log | Select-Object -Last 100
}
```

---

## Installation Steps

```
1. Build the DLL (see above)
2. Copy: .\clang-output\Debug\CvGameCore_Expansion2.dll
   To:    .\(1) Community Patch\Assets\CvGameCore_Expansion2.dll
3. Optional: Copy .pdb file for debugging
   From: .\clang-output\Debug\CvGameCore_Expansion2.pdb
   To:   .\(1) Community Patch\Assets\CvGameCore_Expansion2.pdb
4. Launch Civ5
5. Enable mods: Community Patch + VP
6. Start game as Morocco
7. Test scenarios (see testing checklist)
```

---

## Troubleshooting

**Q: Build fails with "error: cannot find GetAllianceStrength"**  
A: Check that CvTeam.h includes alliance strength methods. May need to use different API depending on VP version.

**Q: Plundering still works for allies**  
A: Verify code was inserted in BOTH `canPlunderTradeRoute()` AND `plunderTradeRoute()` functions.

**Q: Tooltip still shows old message**  
A: Lua cache needs to be cleared. Delete `C:\Users\YourName\Documents\My Games\Civilization 5\Cache` and restart.

**Q: "ALLIANCE_LEVEL_DEFENSIVE_PACT" not found**  
A: Check CvEnums.h for correct enum name. May be `ALLIANCE_LEVEL_DEFENSE_PACT` or similar.

