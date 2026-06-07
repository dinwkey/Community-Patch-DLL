# Phase 2 Implementation Checklist

**Status:** Ready to begin  
**Estimated Time:** 90-120 minutes  
**Risk Level:** LOW-MEDIUM

---

## ✅ PHASE 2A: Religion System (247 lines)

### Implementation Steps

**[ ] 1. Locate CvReligionClasses.cpp and identify change locations**

File: `CvGameCoreDLL_Expansion2/CvReligionClasses.cpp`

Search for function:
- `ScorePantheonBeliefAtCity()` (multiple changes around line ~8700-8800)
- `ScoreBeliefAtCity()` (multiple changes around line ~8700-9050)

**[ ] 2. Change 2A.1: Building Class Happiness Loop (ScoreBeliefAtCity)**

Location: ~line 8710-8725  
Type: Loop optimization + early exit + variable caching  
Lines: +7 (net effect flattens code, minimal line increase)

Replace:
```cpp
// OLD: 
for(int jJ = 0; jJ < GC.getNumBuildingClassInfos(); jJ++)
{
    iTempValue = pEntry->GetBuildingClassHappiness(jJ) * iHappinessMultiplier;
    if(iMinFollowers > 0)
    {
        if(pCity->getPopulation() >= iMinFollowers)
        {
            iTempValue *= 2;
        }
    }
    iRtnValue += iTempValue;
}
```

With:
```cpp
// NEW: 
int iCityPopulation = pCity->getPopulation();
int iNumBuildingClasses = GC.getNumBuildingClassInfos();
for(int jJ = 0; jJ < iNumBuildingClasses; jJ++)
{
    int iBuildingClassHappiness = pEntry->GetBuildingClassHappiness(jJ);
    if(iBuildingClassHappiness == 0)
        continue;

    iTempValue = iBuildingClassHappiness * iHappinessMultiplier;
    if(iMinFollowers > 0 && iCityPopulation >= iMinFollowers)
    {
        iTempValue *= 2;
    }
    iRtnValue += iTempValue;
}
```

**[ ] 3. Change 2A.2: Resource Loop (ScoreBeliefAtCity)**

Location: ~line 8750-8760  
Type: Loop count caching  
Lines: +1

Replace:
```cpp
for (int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
```

With:
```cpp
int iNumResourceInfos = GC.getNumResourceInfos();
for (int iResourceLoop = 0; iResourceLoop < iNumResourceInfos; iResourceLoop++)
```

**[ ] 4. Change 2A.3: Building Class Yield Change Loop (ScoreBeliefAtCity)**

Location: ~line 9020-9035  
Type: Loop caching + early exit + variable reuse  
Lines: +10

Replace:
```cpp
// Building class yield change
for (int iJ = 0; iJ < GC.getNumBuildingClassInfos(); iJ++)
{
    BuildingClassTypes eBuildingClass = static_cast<BuildingClassTypes>(iJ);
    const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);

    iTempValue = pEntry->GetBuildingClassYieldChange(iJ, iI) * iEraBonus;
```

With:
```cpp
// Building class yield change - cache building class count and early exit for zero values
int iNumBuildingClassesInner = GC.getNumBuildingClassInfos();
for (int iJ = 0; iJ < iNumBuildingClassesInner; iJ++)
{
    int iBuildingYieldChange = pEntry->GetBuildingClassYieldChange(iJ, iI);
    if (iBuildingYieldChange == 0)
        continue;

    BuildingClassTypes eBuildingClass = static_cast<BuildingClassTypes>(iJ);
    const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);

    iTempValue = iBuildingYieldChange * iEraBonus;
```

**[ ] 5. Additional Religion Changes (ScorePantheonBeliefAtCity)**

Location: ~line 8650-8700  
Type: Building availability modifier refactoring  
Lines: Additional changes (logic restructuring, reordering)

**Key Pattern:** Flatten nested if/else structures, cache return values, early exit for zero values

---

## ✅ PHASE 2B: Diplomacy AI (67 lines)

### Implementation Steps

**[ ] 6. Locate CvDiplomacyAI.cpp**

File: `CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp`

**[ ] 7. Change 2B.1: Score Victory Detection (GetScoreVictoryProgress)**

Location: Need to find `GetScoreVictoryProgress()` function  
Type: Victory validity check  
Lines: +3-5

Add after initial validity checks:
```cpp
/// How close are we to achieving a Score victory?
int CvDiplomacyAI::GetScoreVictoryProgress() const
{
    if (!m_pPlayer->isAlive() || GC.getGame().getWinner() != NO_TEAM)
        return 0;

    // NEW: Check if score victory is actually enabled
    VictoryTypes eVictoryType = (VictoryTypes)GC.getInfoTypeForString("VICTORY_SCORE");
    if (eVictoryType == NO_VICTORY || !GC.getGame().isVictoryValid(eVictoryType))
        return 0;
    
    // ... rest of function ...
}
```

**[ ] 8. Change 2B.2: Deal Renewal Fix (DoSendStatementToPlayer)**

Location: ~line 30850 (deal renewal section)  
Type: Deal preparation  
Lines: +1

Add to deal renewal code:
```cpp
else
{
    CvDeal kDeal = *pDeal;
    CvGameDeals::PrepareRenewDeal(&kDeal);  // NEW: Prepare renewal deal
    
    if (GC.getGame().isReallyNetworkMultiPlayer() && MOD_ACTIVE_DIPLOMACY)
    {
        // ... existing code ...
    }
}
```

**[ ] 9. Change 2B.3: Deal Renewal Refactoring (DoRenewExpiredDeal)**

Location: ~line 34510-34540  
Type: Move PrepareRenewDeal call + simplify logic  
Lines: -10 (removes code)

Find this section:
```cpp
//Set as considered for renewal.
pCurrentDeal->m_iFinalTurn = -1;

int iValue = m_pPlayer->GetDealAI()->GetDealValue(pCurrentDeal);
if (iValue != INT_MAX)
{
    // Prepare the old deal for renewal BEFORE modifying it, so we reverse the correct old values
    CvGameDeals::PrepareRenewDeal(pCurrentDeal);

    bool bAbleToEqualize = false;
    // ... rest of code ...
}
```

Replace with (remove the PrepareRenewDeal call from here):
```cpp
//Set as considered for renewal.
pCurrentDeal->m_iFinalTurn = -1;

int iValue = m_pPlayer->GetDealAI()->GetDealValue(pCurrentDeal);
if (iValue != INT_MAX)
{
    bool bAbleToEqualize = false;
    // ... rest of code ...
}
```

**Note:** PrepareRenewDeal is now called in DoSendStatementToPlayer instead (step 8)

**[ ] 10. Change 2B.4: Cancel Renewal Deal Simplification (CancelRenewDeal)**

Location: ~line 49960-49975  
Type: Remove duplicate deal iteration logic  
Lines: -13 (removes code)

Remove this code block:
```cpp
// find the deal in m_CurrentDeal and mark it as canceled
CvGameDeals& kGameDeals = GC.getGame().GetGameDeals();
DealList::iterator it;
for (it = kGameDeals.m_CurrentDeals.begin(); it != kGameDeals.m_CurrentDeals.end(); ++it)
{
    if (*it == *pRenewalDeal)
    {
        it->m_bConsideringForRenewal = false;
    }
}
GC.getGame().GetGameDeals().DoUpdateCurrentDealsList();
```

Keep only:
```cpp
pRenewalDeal->m_bConsideringForRenewal = false;
```

---

## ✅ PHASE 2C: Tech System (149 lines)

### Implementation Steps

**[ ] 11. Locate CvTechClasses.cpp**

File: `CvGameCoreDLL_Expansion2/CvTechClasses.cpp`

**[ ] 12. Change 2C.1: Add Median Tech Cache to Reset() (~line 726)**

Type: Cache initialization  
Lines: +5

Add after `m_bWillHaveUUTechSoon = false;`:
```cpp
// Reset median tech cache (transient)
m_bMedianTechCacheValid = false;
m_iMedianTechCacheTurn = -1;
m_iMedianTechCacheValue = 0;
m_iMedianTechCacheVersion = -1;
```

**[ ] 13. Change 2C.2: Refactor GetMedianTechResearch() (~line 1781)**

Type: Cache-based optimization  
Lines: +30-40

Replace entire function body:
```cpp
// OLD:
int CvPlayerTechs::GetMedianTechResearch() const
{
    vector<int> aiTechCosts;
    int iRtnValue = 0;

    for(int iTechLoop = 0; iTechLoop < GC.getNumTechInfos(); iTechLoop++)
    {
        TechTypes eTech = (TechTypes)iTechLoop;

        if(CanResearch(eTech))
        {
            aiTechCosts.push_back(GetResearchCost(eTech));
        }
    }

    int iNumEntries = aiTechCosts.size();
    if(iNumEntries > 0)
    {
        std::stable_sort(aiTechCosts.begin(), aiTechCosts.end());

        // Odd number, take middle?
        if((iNumEntries / 2) * 2 != iNumEntries)
        {
            iRtnValue = aiTechCosts[iNumEntries / 2];
        }

        // Even number, average middle 2
        else
        {
            iRtnValue = (aiTechCosts[(iNumEntries - 1) / 2] + aiTechCosts[iNumEntries / 2]) / 2;
        }
    }

    return iRtnValue;
}

// NEW:
int CvPlayerTechs::GetMedianTechResearch() const
{
    // Versioned cache: recompute only if team tech set changed
    int iTeamVersion = GET_TEAM(m_pPlayer->getTeam()).GetTeamTechs()->GetTechSetVersion();
    if (m_bMedianTechCacheValid && m_iMedianTechCacheVersion == iTeamVersion)
    {
        return m_iMedianTechCacheValue;
    }

    vector<int> aiTechCosts;
    int iRtnValue = 0;

    for (int iTechLoop = 0; iTechLoop < GC.getNumTechInfos(); iTechLoop++)
    {
        TechTypes eTech = (TechTypes)iTechLoop;
        if (CanResearch(eTech))
        {
            aiTechCosts.push_back(GetResearchCost(eTech));
        }
    }

    const int iNumEntries = (int)aiTechCosts.size();
    if (iNumEntries > 0)
    {
        std::stable_sort(aiTechCosts.begin(), aiTechCosts.end());

        if ((iNumEntries & 1) == 1)
        {
            iRtnValue = aiTechCosts[iNumEntries / 2];
        }
        else
        {
            iRtnValue = (aiTechCosts[(iNumEntries - 1) / 2] + aiTechCosts[iNumEntries / 2]) / 2;
        }
    }

    // store cache
    m_iMedianTechCacheValue = iRtnValue;
    m_iMedianTechCacheVersion = iTeamVersion;
    m_bMedianTechCacheValid = true;
    return iRtnValue;
}
```

**Key changes:**
- Add cache validity check with versioning
- Use bitwise AND for odd/even check: `(iNumEntries & 1) == 1`
- Store result in cache before returning
- Cache invalidated when team tech set version changes

**[ ] 14. Change 2C.3: Add Cache Member Variables to CvTeamTechs**

Location: Constructor (~line 2125)  
Type: Member initialization  
Lines: +1

In initialization list, add:
```cpp
m_iTechSetVersion(0)
```

**[ ] 15. Change 2C.4: Initialize Version in Reset()**

Location: ~line 2188  
Type: Reset initialization  
Lines: +1

Add after `m_iNumTechs = 0;`:
```cpp
m_iTechSetVersion = 0;
```

**[ ] 16. Change 2C.5: Bump Version on Tech Acquisition (SetHasTech)**

Location: ~line 2290-2300  
Type: Cache invalidation  
Lines: +3

Add after `SetLastTechAcquired(eIndex);`:
```cpp
// bump version whenever ownership changes (especially on acquire)
m_iTechSetVersion++;
```

**[ ] 17. Change 2C.6: Add Overflow Cap (~line 2440)**

Location: ~line 2440-2460  
Type: Safety check  
Lines: +10

Find overflow calculation:
```cpp
iOverflow = iOverflow * 100 / iPlayerOverflowDivisorTimes100;
```

Add after:
```cpp
// Cap overflow to a reasonable bound to avoid integer saturation in extremely long games
// Use a dynamic cap based on player's science per turn (times100) to keep proportional
{
    const long long iSciencePerTurnTimes100 = (long long)GET_PLAYER(ePlayer).GetScienceTimes100();
    const long long iDynamicCap = std::max( (long long)10000, iSciencePerTurnTimes100 * 10 ); // at least 100 beakers, up to 10 turns of science
    if (iOverflow > iDynamicCap)
        iOverflow = iDynamicCap;
}
```

Then also bump version:
```cpp
m_pTeam->setHasTech(eIndex, true, ePlayer, true, true);
// tech acquired via research completion, bump version for cache invalidation
m_iTechSetVersion++;
```

**[ ] 18. Change 2C.7: Add GetTechSetVersion() Method (~line 2573)**

Type: New accessor method  
Lines: +5

Add after `GetTechs()` method:
```cpp
int CvTeamTechs::GetTechSetVersion() const
{
    return m_iTechSetVersion;
}
```

---

## ✅ PHASE 2D: Policy AI (6 lines)

### Implementation Steps

**[ ] 19. Locate CvPolicyAI.cpp**

File: `CvGameCoreDLL_Expansion2/CvPolicyAI.cpp`

**[ ] 20. Change 2D.1: Cache Unhappiness Computation (DoConsiderIdeologySwitch)**

Location: ~line 791-820  
Type: Performance optimization (compute once, reuse)  
Lines: +3

Find:
```cpp
bool bVUnhappy = pPlayer->IsEmpireVeryUnhappy();
bool bSUnhappy = pPlayer->IsEmpireSuperUnhappy();
if (bSUnhappy)
{
    int iNewUnhappiness = pPlayer->GetCulture()->ComputeHypotheticalPublicOpinionUnhappiness(ePreferredIdeology);
    // ... use iNewUnhappiness ...
}
else if (bVUnhappy)
{
    int iNewUnhappiness = pPlayer->GetCulture()->ComputeHypotheticalPublicOpinionUnhappiness(ePreferredIdeology);
    // ... use iNewUnhappiness ...
}
```

Replace with:
```cpp
bool bVUnhappy = pPlayer->IsEmpireVeryUnhappy();
bool bSUnhappy = pPlayer->IsEmpireSuperUnhappy();

// Pre-compute hypothetical unhappiness once (performance optimization)
int iNewUnhappiness = pPlayer->GetCulture()->ComputeHypotheticalPublicOpinionUnhappiness(ePreferredIdeology);

if (bSUnhappy)
{
    // ... use iNewUnhappiness ...
}
else if (bVUnhappy)
{
    // ... use iNewUnhappiness ...
}
```

---

## Build Verification

**[ ] 21. Run build (PowerShell)**

```powershell
.\build_vp_clang.ps1 -Config debug
```

Expected output:
- `Commit id update finished`
- `clang.cpp build finished`
- `Precompiled header build finished`
- `cpps build finished`
- `Linking dll...`
- DLL created: `clang-output/Debug/CvGameCore_Expansion2.dll`

**[ ] 22. Verify build results**

```powershell
Test-Path "clang-output/Debug/CvGameCore_Expansion2.dll"
Get-Item "clang-output/Debug/CvGameCore_Expansion2.dll" | Select-Object -ExpandProperty Length
```

Expected:
- File exists: TRUE
- File size: ~24-26 MB

**[ ] 23. Check for compilation errors**

```powershell
Select-String -Path "clang-output/Debug/build.log" -Pattern "error:" | Measure-Object
```

Expected:
- Error count: 0

---

## Commit & Documentation

**[ ] 24. Create git commit**

```bash
git add CvGameCoreDLL_Expansion2/CvReligionClasses.cpp
git add CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp
git add CvGameCoreDLL_Expansion2/CvTechClasses.cpp
git add CvGameCoreDLL_Expansion2/CvPolicyAI.cpp
git commit -m "Phase 2: Religion, Diplomacy & Tech Optimizations (247 + 67 + 63 lines)"
```

**[ ] 25. Update documentation**

- [ ] Update COMPLETION_SUMMARY.md
- [ ] Add Phase 2 entry with commit hash
- [ ] Update total code lines: +377
- [ ] Update build success rate: 11/11

---

## Rollback Plan (if needed)

If build fails after any step:

```bash
# Revert to last known good state
git reset --hard 79593f266  # After Phase 1 docs commit

# Or revert individual file
git checkout HEAD~1 CvGameCoreDLL_Expansion2/CvReligionClasses.cpp
```

---

## Time Estimate

| Phase | Time | Cumulative |
|-------|------|-----------|
| Phase 2A (Religion) | 30 min | 30 min |
| Phase 2B (Diplomacy) | 20 min | 50 min |
| Phase 2C (Tech) | 25 min | 75 min |
| Phase 2D (Policy) | 5 min | 80 min |
| Build & Verify | 10-20 min | 90-100 min |
| Commit & Docs | 10 min | 100-110 min |
| **Total** | **100-110 min** | **~2 hours** |

---

**Status:** Ready to begin  
**Created:** 2026-01-12  
**Next Action:** Start with Phase 2A (Religion)
