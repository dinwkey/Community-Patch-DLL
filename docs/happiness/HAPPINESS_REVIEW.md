# Happiness / Amenities System Review

**Date:** January 9, 2026  
**Scope:** Empire happiness mechanics, penalties, luxury distribution

---

## Executive Summary

Vox Populi uses a **citizen-based happiness system** that differs significantly from vanilla Civ5. Instead of a simple empire-wide happiness vs unhappiness counter, VP calculates:

1. **Total Happy Citizens** from empire sources (luxuries, buildings, policies, religion, etc.)
2. **Total Unhappy Citizens** from local city needs (Distress, Poverty, Illiteracy, Boredom) + special sources
3. **Happiness Ratio** = (Happy Citizens / Unhappy Citizens) * 50, displayed as 0-100%

This system distributes empire happiness to cities based on need, creates local happiness feedback, and uses median-based thresholds for unhappiness calculation.

---

## Core Architecture

### Entry Point: `CvPlayer::CalculateNetHappiness()`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L19967-L20005)

Called whenever happiness changes (building constructed, policy adopted, city founded, etc.). Coordinates the entire happiness calculation:

```cpp
void CvPlayer::CalculateNetHappiness()
{
    // Reset cached unit supply values
    m_iNumUnitsSuppliedCached = -1;
    m_iNumUnitsSuppliedCachedWarWeariness = -1;

    DoUpdateTotalHappiness();        // Calculate empire-wide happiness sources
    DoUpdateTotalUnhappiness();      // Calculate total unhappiness

    if (MOD_BALANCE_VP)
    {
        // VP uses citizen-based ratio system
        int iUnhappyCitizens = GetUnhappinessFromCitizenNeeds();
        if (iUnhappyCitizens == 0)
            m_iHappinessTotal = 100;  // Perfect happiness if no unhappy citizens
        else
        {
            int iHappyCitizens = GetHappinessFromCitizenNeeds();
            // Ratio formula: (Happy / Unhappy) * 50, capped at 100
            int iPercent = min(200, (iHappyCitizens * 100) / max(1, iUnhappyCitizens));
            m_iHappinessTotal = iPercent / 2;
        }
    }
    else
    {
        // Legacy system: simple difference
        m_iHappinessTotal = GetHappiness() - GetUnhappiness();
    }
}
```

---

## Happiness Sources (Empire-Wide)

### `DoUpdateTotalHappiness()`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L19733-L19806)

Calculates total empire happiness from various sources:

#### 1. **Base Happiness**
- Handicap starting happiness: `getHandicapInfo().getHappinessDefault()`
- Game speed starting happiness: `GetStartingHappiness()`
- AI bonus happiness: `getHandicapInfo().getAIHappinessDefault()`

#### 2. **Luxury Resources** (VP Only)
```cpp
if (MOD_BALANCE_VP)
{
    m_iHappiness += GetBonusHappinessFromLuxuriesFlat();
}
```

**Function:** `GetBonusHappinessFromLuxuriesFlat()` ([CvPlayer.cpp](CvPlayer.cpp#L21494-L21533))

**Per-Luxury Calculation:**
- Base: `pkResourceInfo->getHappiness()` (typically 4)
- Bonus: `+GetExtraHappinessPerLuxury()` (from policies/traits)
- Monopoly bonus: If player has global monopoly + `pInfo->getMonopolyHappiness() > 0`
- Minor civ bonus: If resource from minors + `IsMinorResourceBonus()`, multiply by `MINOR_POLICY_RESOURCE_HAPPINESS_MULTIPLIER` (100 in VP, 150 in CP)

**Key Implementation:**
```cpp
int CvPlayer::GetHappinessFromLuxury(ResourceTypes eResource, bool bIncludeImport) const
{
    // No happiness if banned by World Congress
    if (GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned(GetID(), eResource))
        return 0;

    CvResourceInfo* pkResourceInfo = GC.getResourceInfo(eResource);
    if (pkResourceInfo && pkResourceInfo->getResourceUsage() == RESOURCEUSAGE_LUXURY)
    {
        // Any extras?
        if (getNumResourceAvailable(eResource, bIncludeImport) > 0)
            return pkResourceInfo->getHappiness();
        // Netherlands UA: retain 50% if you trade away last copy
        else if (GetPlayerTraits()->GetLuxuryHappinessRetention() > 0 && getResourceExport(eResource) > 0)
            return (pkResourceInfo->getHappiness() * GetLuxuryHappinessRetention()) / 100;
    }
    return 0;
}
```

#### 3. **Legacy Sources (Non-VP Only)**
- `GetHappinessFromResources()`
- `GetHappinessFromResourceVariety()` - bonus per extra luxury type
- `GetHappinessFromCities()` - local city happiness

#### 4. **Other Empire Sources** (Both Systems)
- Religion: `GetHappinessFromReligion()`
- Natural Wonders: `GetHappinessFromNaturalWonders()`
- Minor Civs: `GetHappinessFromMinorCivs()`
- Annexed Minors: `GetHappinessFromAnnexedMinors()`
- Leagues: `GetHappinessFromLeagues()`
- Vassals: `GetHappinessFromVassals()`
- Military Units: `GetHappinessFromMilitaryUnits()`
- Wars with Majors: `GetHappinessFromWarsWithMajors()`
- City Connections: `GetHappinessFromTradeRoutes()`
- Events: Sum of `pLoopCity->GetEventHappiness()`

#### 5. **Happiness Distribution to Cities** (VP Only)
After calculating total happiness, VP distributes it to individual cities:

```cpp
if (MOD_BALANCE_VP)
    DistributeHappinessToCities();
```

---

## Happiness Distribution System (VP)

### `DistributeHappinessToCities()`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L19809-L19905)

**Algorithm:**

1. **Single City Case:** All happiness goes to that city
2. **Multiple Cities:**
   - Exclude puppets (unless Venice), razing, resistance, and occupied cities
   - Calculate "Need Score" = `Population - LocalHappiness` for each city
   - Sort cities by Need (descending) and by Population (descending)
   
3. **Distribution Phase 1: Fill Needs**
   - Iterate through cities sorted by Need
   - Give 1 happiness to each city with Need > 0
   - Repeat until all cities are full or happiness runs out
   
4. **Distribution Phase 2: Distribute Excess**
   - If happiness remains, distribute evenly by population
   - Higher population cities get more happiness per round-robin pass

**Implementation:**
```cpp
void CvPlayer::DistributeHappinessToCities()
{
    // ... validation and single-city case ...

    CvWeightedVector<CvCity*> CitiesSortedByNeed;
    CvWeightedVector<CvCity*> CitiesSortedByPopulation;

    for (CvCity* pLoopCity = firstCity(&iLoop); pLoopCity != NULL; pLoopCity = nextCity(&iLoop))
    {
        if (CityStrategyAIHelpers::IsTestCityStrategy_IsPuppetAndAnnexable(pLoopCity))
            continue;  // Ignore puppets

        pLoopCity->ResetHappinessFromEmpire();

        if (pLoopCity->IsRazing() || pLoopCity->IsResistance() || 
            (pLoopCity->IsOccupied() && !pLoopCity->IsNoOccupiedUnhappiness()))
            continue;  // Don't distribute to these cities

        int iPopulation = pLoopCity->getPopulation();
        int iLocalHappiness = pLoopCity->GetLocalHappiness(0, /*bExcludeEmpireContributions*/ true);
        int iScore = iPopulation - iLocalHappiness;
        if (iScore > 0)
            CitiesSortedByNeed.push_back(pLoopCity, iScore);

        CitiesSortedByPopulation.push_back(pLoopCity, iPopulation);
    }

    // Phase 1: Fill needs in descending order
    while (!bAllCitiesFull)
    {
        for (int i = 0; i < CitiesSortedByNeed.size(); i++)
        {
            int iNeedScore = CitiesSortedByNeed.GetWeight(i);
            if (iNeedScore > 0)
            {
                pLoopCity->ChangeHappinessFromEmpire(1);
                iHappiness--;
                CitiesSortedByNeed.SetWeight(i, iNeedScore - 1);
            }
            if (iHappiness == 0) return;
        }
        // Check if all cities full
    }

    // Phase 2: Distribute excess by population
    while (true)
    {
        for (int i = 0; i < CitiesSortedByPopulation.size(); i++)
        {
            pLoopCity->ChangeHappinessFromEmpire(1);
            iHappiness--;
            if (iHappiness == 0) return;
        }
    }
}
```

**Design Notes:**
- Prioritizes cities with the greatest unmet need (pop > local happiness)
- Prevents large cities from monopolizing all happiness
- Ensures small struggling cities get help first
- Once all cities are "content", distributes excess proportionally by population

---

## Unhappiness Sources

### `DoUpdateTotalUnhappiness()`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L21628-L21674)

**VP vs Legacy Differences:**

#### **Legacy (Non-VP) System:**
- City count unhappiness: `GetUnhappinessFromCityCount()`
- Captured city count: `GetUnhappinessFromCapturedCityCount()`
- City population: `GetUnhappinessFromCityPopulation()`
- City buildings: `GetUnhappinessFromCityBuildings()`
- Occupied cities: `GetUnhappinessFromOccupiedCities()`
- Units (builders): `GetUnhappinessFromUnits()`

All values are multiplied by modifiers and divided by 100 before summing.

#### **VP System:**
All legacy sources **disabled** for VP. Instead:

1. **Public Opinion Unhappiness:** `GetCulture()->GetPublicOpinionUnhappiness()`
   - Ideology pressure from other civs
   
2. **JFD Special Sources** (if MOD_BALANCE_CORE_JFD):
   - `GetUnhappinessFromCityJFDSpecial()` - includes specialists

3. **War Weariness:** `GetUnhappinessFromWarWeariness()`

4. **Citizen Needs (Calculated at City Level):**
   - Distress (defense/production need)
   - Poverty (gold need)
   - Illiteracy (science need)
   - Boredom (culture need)
   - Religious Unrest
   - Famine
   - Isolation
   - Pillaged Tiles
   - Buildings
   - Occupation
   - Empire distribution (negative feedback)

**Implementation:**
```cpp
int CvPlayer::DoUpdateTotalUnhappiness(CvCity* pAssumeCityAnnexed, CvCity* pAssumeCityPuppeted)
{
    int iUnhappiness = 0;

    if (!MOD_BALANCE_VP)
    {
        // Legacy calculations (city count, population, buildings, etc.)
        // ...
        iUnhappiness /= 100;
    }

    iUnhappiness += GetCulture()->GetPublicOpinionUnhappiness();

    if (MOD_BALANCE_CORE_JFD)
        iUnhappiness += GetUnhappinessFromCityJFDSpecial();

    if (MOD_BALANCE_VP)
        iUnhappiness += GetUnhappinessFromWarWeariness();

    SetUnhappiness(iUnhappiness);
    return iUnhappiness;
}
```

---

## City-Level Happiness/Unhappiness

### City Happiness Sources

#### `CvCity::GetLocalHappiness(int iPopMod, bool bExcludeEmpireContributions)`
**Location:** [CvCity.cpp](CvCity.cpp#L20011-L20060) (approximately)

Returns sum of:
- Happiness from empire distribution (if not excluded)
- Base happiness from buildings
- Happiness from building classes
- Happiness from policies
- Happiness from religion
- Unmodded happiness from buildings (misc sources)
- Handicap bonuses for capital

### City Unhappiness Sources

#### `CvCity::GetUnhappinessAggregated()`
**Location:** [CvCity.cpp](CvCity.cpp#L20192-L20290)

**Special Cases:**
- **Razing/Resistance:** Return full population as unhappiness
- **Puppets:** Return `population / UNHAPPINESS_PER_X_PUPPET_CITIZENS` + specialist unhappiness

**Normal Cities (VP):**
Unhappiness is capped at city population. Sources checked in order (stop when cap reached):

1. **Occupation:** `GetUnhappinessFromOccupation()`
2. **Buildings:** `GetUnhappinessFromBuildings()`
3. **Empire Distribution:** `GetUnhappinessFromEmpire()` - negative feedback from empire happiness
4. **Isolation:** `GetUnhappinessFromIsolation()`
5. **Religious Unrest:** `GetUnhappinessFromReligiousUnrest()`
6. **Pillaged Tiles:** `GetUnhappinessFromPillagedTiles()`
7. **Famine:** `GetUnhappinessFromFamine()`
8. **Poverty:** `GetPoverty(false)`
9. **Illiteracy:** `GetIlliteracy(false)`
10. **Boredom:** `GetBoredom(false)`
11. **Distress:** `GetDistress(false)`

**Performance Optimization:** Sources are ordered so expensive calculations (citizen needs) are done last, and the loop exits early if population cap is reached.

---

## Citizen Needs Mechanics (VP)

The four citizen needs (Distress, Poverty, Illiteracy, Boredom) use a **median-based threshold system**:

### How Median Thresholds Work

Each need compares the city's yield production against a "median" threshold based on:
- **Base median:** Defined in global defines
- **Modifiers:** Empire size, tech progress, city-specific bonuses
- **Threshold formula:** `Median * (100 + TotalModifiers) / 100`

If city yield is below the threshold, unhappiness is generated proportionally to the deficit.

### Example: Distress (Defense/Production Need)

#### `CvCity::GetDistress(bool bForceRecalc, int iAssumedExtraYieldRate)`
**Location:** [CvCity.cpp](CvCity.cpp#L21239-L21270) (approximately)

**Calculation Steps:**
1. Get raw distress: `GetDistressRaw(bForceRecalc, iAssumedExtraYieldRate)`
2. Apply flat reduction: `iDistress -= (GetDistressFlatReduction() + kPlayer.GetDistressFlatReductionGlobal())`
3. Cap at 0: `return max(0, iDistress)`

#### `CvCity::GetDistressRaw(bool bForceRecalc, int iAssumedExtraYieldRate)`

**Calculation:**
```cpp
int iDistressRaw = 0;

// Get median threshold
float fBasicNeedsMedian = GetBasicNeedsMedian(bForceRecalc, 0);

// Get city's defense yield (production)
int iCityDefense = getYieldRateTimes100(YIELD_PRODUCTION, false, false) / 100;
if (bForceRecalc)
    iCityDefense += iAssumedExtraYieldRate;  // Simulate growth

// If below median, calculate deficit
if (iCityDefense < fBasicNeedsMedian)
{
    float fDeficit = fBasicNeedsMedian - (float)iCityDefense;
    iDistressRaw = (int)ceil(fDeficit);
}

return iDistressRaw;
```

#### `CvCity::GetBasicNeedsMedian(bool bForceRecalc, int iAdditionalModifier)`

**Median Calculation:**
```cpp
// Base median from global defines
float fBasicNeedsMedian = /*6.0f*/ GD_FLOAT_GET(BALANCE_BASIC_NEEDS_MEDIAN);

// Get total modifier for this yield
int iTotalModifier = GetTotalNeedModifierForYield(YIELD_PRODUCTION, bForceRecalc);
iTotalModifier += iAdditionalModifier;

// Get player global modifier
iTotalModifier += kPlayer.GetBasicNeedsMedianModifierGlobal();

// Apply modifiers
fBasicNeedsMedian *= (100 + iTotalModifier);
fBasicNeedsMedian /= 100;

return fBasicNeedsMedian;
```

**Modifiers Include:**
- Empire size penalty (more cities = higher thresholds)
- Tech progress penalty (more advanced techs = higher thresholds)
- City-specific bonuses (buildings, policies)
- Player global bonuses (traits, policies)

### Similar Mechanics for Other Needs

- **Poverty:** Uses `YIELD_GOLD` and `GetGoldMedian()`
- **Illiteracy:** Uses `YIELD_SCIENCE` and `GetScienceMedian()`
- **Boredom:** Uses `YIELD_CULTURE` and `GetCultureMedian()`

All four follow the same pattern: `if (yield < median) { unhappiness = ceil(median - yield) }`

---

## Happiness Thresholds and Penalties

### Threshold System
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L20401-L20432)

VP uses absolute thresholds instead of relative comparisons:

```cpp
// Unhappy threshold: happiness ratio < 50
bool CvPlayer::IsEmpireUnhappy() const
{
    if (MOD_BALANCE_VP)
        return GetExcessHappiness() < /*50*/ GD_INT_GET(UNHAPPY_THRESHOLD);
    return GetExcessHappiness() < 0;
}

// Very unhappy threshold: happiness ratio < 35
bool CvPlayer::IsEmpireVeryUnhappy() const
{
    if (MOD_BALANCE_VP)
        return GetExcessHappiness() < /*35*/ GD_INT_GET(VERY_UNHAPPY_THRESHOLD);
    return GetExcessHappiness() <= /*-10*/ GD_INT_GET(VERY_UNHAPPY_THRESHOLD);
}

// Super unhappy threshold: happiness ratio < 20 (triggers revolts)
bool CvPlayer::IsEmpireSuperUnhappy() const
{
    if (MOD_BALANCE_VP)
        return GetExcessHappiness() < /*20*/ GD_INT_GET(SUPER_UNHAPPY_THRESHOLD);
    return GetExcessHappiness() <= /*-20*/ GD_INT_GET(SUPER_UNHAPPY_THRESHOLD);
}
```

### Penalties

#### **Growth Penalty**
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L20363-L20370)

```cpp
int CvPlayer::GetUnhappinessGrowthPenalty() const
{
    if (MOD_BALANCE_VP)
    {
        // Penalty = 2.5% per point below threshold (capped at -100%)
        return range(
            static_cast<int>(/*2.5f*/ GD_FLOAT_GET(GLOBAL_GROWTH_PENALTY_PER_UNHAPPY) *
                            (GetExcessHappiness() - GD_INT_GET(UNHAPPY_THRESHOLD))),
            -100, 0
        );
    }
    return 0;
}
```

**Example:** If happiness = 40 (10 below threshold):
- Penalty = 2.5 * (40 - 50) = 2.5 * -10 = -25% growth

#### **Settler Cost Penalty**
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L20373-L20382)

```cpp
int CvPlayer::GetUnhappinessSettlerCostPenalty() const
{
    if (MOD_BALANCE_VP)
    {
        return range(
            static_cast<int>(/*2.5f*/ GD_FLOAT_GET(GLOBAL_SETTLER_PRODUCTION_PENALTY_PER_UNHAPPY) *
                            (GetExcessHappiness() - GD_INT_GET(UNHAPPY_THRESHOLD))),
            /*-75*/ GD_INT_GET(UNHAPPY_MAX_UNIT_PRODUCTION_PENALTY), 0
        );
    }
    return 0;
}
```

**Example:** If happiness = 35 (15 below threshold):
- Penalty = 2.5 * (35 - 50) = -37.5% settler production (capped at -75%)

#### **Combat Strength Penalty**
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L20385-L20398)

```cpp
int CvPlayer::GetUnhappinessCombatStrengthPenalty() const
{
    if (!IsEmpireUnhappy())
        return 0;

    if (MOD_BALANCE_VP)
    {
        // -10 if unhappy, -20 if very unhappy
        return IsEmpireVeryUnhappy() ? 
               /*-20*/ GD_INT_GET(VERY_UNHAPPY_MAX_COMBAT_PENALTY) : 
               /*-10*/ GD_INT_GET(VERY_UNHAPPY_MAX_COMBAT_PENALTY) / 2;
    }

    // Legacy: scales with excess unhappiness
    int iPenalty = (-1 * GetExcessHappiness()) * /*-2*/ GD_INT_GET(VERY_UNHAPPY_COMBAT_PENALTY_PER_UNHAPPY);
    return max(iPenalty, /*-40*/ GD_INT_GET(VERY_UNHAPPY_MAX_COMBAT_PENALTY));
}
```

#### **Uprisings (City Revolts)**
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L20435-L20460)

When `IsEmpireSuperUnhappy()` returns true (happiness < 20):
- Uprising counter decrements each turn
- When counter reaches 0, `DoUprising()` is called
- A city with high unhappiness spawns barbarian units and may flip to another civ

---

## Issues and Improvements

### Current Issues

#### 1. **Luxury Distribution Opacity**
**Problem:** Players don't clearly see how individual luxuries contribute to happiness.
- `GetBonusHappinessFromLuxuriesFlat()` aggregates all luxury happiness into one number
- No per-luxury breakdown in UI
- Difficult to evaluate trade deals involving luxuries

**Suggested Fix:**
- Add Lua export: `GetHappinessFromLuxuryBreakdown()` returning table of {resource, happiness}
- Update UI tooltips to show per-luxury contributions
- Highlight which luxuries are affected by monopoly bonuses or minor civ bonuses

#### 2. **Empire Happiness Distribution Confusion**
**Problem:** The distribution algorithm is complex and not well-explained to players.
- Players don't understand why some cities get more empire happiness than others
- "Need Score" calculation is opaque
- No visual indication of which cities are prioritized

**Suggested Fix:**
- Add city banner tooltip showing:
  - "Need Score: X (Population Y - Local Happiness Z)"
  - "Empire Happiness Received: X (Priority: Need / Population)"
- Add empire happiness screen showing distribution breakdown

#### 3. **Median Threshold Scaling Unclear**
**Problem:** Players don't understand why citizen needs increase over time.
- Tech progress modifier is hidden
- Empire size modifier not explained
- No way to see what the current median threshold is for each need

**Suggested Fix:**
- Add tooltip to citizen need breakdown showing:
  - Base median value
  - Empire size modifier (+X%)
  - Tech progress modifier (+Y%)
  - City modifiers (+Z%)
  - Final threshold value
  - Current city yield vs threshold

#### 4. **Happiness Ratio Display Misleading**
**Problem:** The 0-100 happiness ratio is not intuitive.
- Players expect higher = better, but penalties start at 50
- No visual indication of thresholds at 50, 35, 20
- Hard to estimate how many more citizens you can support

**Suggested Fix:**
- Change display to show "Happiness Status":
  - 50+: "Content" (green)
  - 35-49: "Unhappy" (yellow, growth/settler penalties)
  - 20-34: "Very Unhappy" (orange, combat penalty)
  - <20: "Revolting" (red, uprising danger)
- Show exact happy/unhappy citizen counts
- Add projection: "Can support X more citizens before Unhappy"

#### 5. **Puppet Happiness Mechanics Unclear**
**Problem:** Different puppet happiness mechanics depending on settings.
- `MOD_BALANCE_PUPPET_CHANGES` changes formula drastically
- Netherlands UA interaction with puppets not documented
- Specialist unhappiness in puppets calculated differently

**Suggested Fix:**
- Consolidate puppet happiness into one clear formula
- Add tooltip explaining Venice exception
- Document trait interactions (Netherlands)

#### 6. **Unhappiness Cap at Population Not Communicated**
**Problem:** `GetUnhappinessAggregated()` caps unhappiness at city population, but players don't know.
- Hidden performance optimization
- Affects which unhappiness sources actually matter
- Order of evaluation determines which sources "count"

**Suggested Fix:**
- If cap is reached, add tooltip: "Unhappiness capped at population (X)"
- Show which unhappiness sources were skipped due to cap
- Consider making cap visible or removing it for clarity

### Potential Improvements

#### 1. **Dynamic Happiness Distribution**
**Enhancement:** Allow players to manually adjust happiness distribution bias.
- Add slider: "Prioritize Large Cities" <-> "Prioritize Small Cities"
- Add policy: "Empire Happiness distributed equally per city"
- Add building: "+X priority for this city when distributing happiness"

#### 2. **Luxury Synergy Bonuses**
**Enhancement:** Reward players for collecting diverse luxuries.
- Restore `GetHappinessFromResourceVariety()` in VP (currently disabled)
- Add building: "+1 Happiness per unique luxury resource"
- Add policy: "Each luxury after the first provides +1 bonus happiness"

#### 3. **Local Happiness Buildings More Impactful**
**Enhancement:** Make local city happiness more valuable vs empire happiness.
- Buildings that reduce citizen need medians (e.g., Granary reduces Distress by 20%)
- Buildings that provide flat reduction to specific needs (already exists but underused)
- Buildings that increase "Need Score" for happiness distribution priority

#### 4. **Happiness from Specialists Rework**
**Enhancement:** Currently specialists cause unhappiness (legacy) or are free (VP + traits).
- Add specialist type-specific happiness bonuses (e.g., Artists provide culture -> reduce Boredom)
- Scale specialist unhappiness by city size (small cities penalized more)
- Add building: "Specialists provide +0.5 happiness each"

#### 5. **Happiness Projection Tool**
**Enhancement:** Help players plan ahead.
- Add city screen widget: "After growth, happiness will be: X -> Y"
  - Already partially implemented in `getPotentialUnhappinessWithGrowth()`
- Add empire screen: "If you settle next city, happiness will be: X -> Y"
- Add trade screen: "If you accept this deal, happiness will be: X -> Y"

#### 6. **Modular Citizen Needs**
**Enhancement:** Allow mods to add custom citizen needs.
- Refactor Distress/Poverty/Illiteracy/Boredom into table-driven system
- Add XML: `<CitizenNeeds>` with Yield, Median, Modifiers
- Enable mods to add: Unrest (from espionage), Plague (from disease), etc.

#### 7. **Clearer Public Opinion Mechanic**
**Enhancement:** Public opinion unhappiness is opaque.
- Add breakdown showing which civs are pressuring you
- Show cultural influence level thresholds
- Add policy: "Reduce public opinion unhappiness by 50%"

#### 8. **War Weariness Feedback**
**Enhancement:** War weariness is a black box.
- Add tooltip showing which wars contribute most
- Show war duration and casualty impact
- Add building: "Reduces war weariness by X%"

---

## Code Quality Observations

### Strengths
1. **Clear separation of concerns:** Happiness calculation is isolated in dedicated functions
2. **Performance optimizations:** Caching, early exits, ordered evaluation
3. **Extensibility:** Easy to add new happiness sources via function calls
4. **VP/Legacy compatibility:** Clean branching for two different systems

### Weaknesses
1. **Documentation:** Many functions lack comments explaining formulas
2. **Magic numbers:** Hardcoded values like `/ 100` for percentage conversions
3. **Naming inconsistency:** `GetHappinessFromX` vs `GetUnhappinessFromX` vs `GetX` 
4. **Duplication:** Similar median calculation code repeated for each citizen need
5. **Complex dependencies:** Distribution system requires careful update ordering

### Suggested Refactors

#### 1. **Extract Median Calculation**
```cpp
// Current: Repeated code in GetDistress, GetPoverty, GetIlliteracy, GetBoredom
float fMedian = GetBasicNeedsMedian(bForceRecalc, 0);
// ...

// Suggested: Generic function
float CvCity::GetNeedMedian(YieldTypes eYield, bool bForceRecalc, int iAdditionalModifier) const
{
    float fBaseMedian = 0.0f;
    switch (eYield)
    {
        case YIELD_PRODUCTION: fBaseMedian = GD_FLOAT_GET(BALANCE_BASIC_NEEDS_MEDIAN); break;
        case YIELD_GOLD: fBaseMedian = GD_FLOAT_GET(BALANCE_GOLD_MEDIAN); break;
        case YIELD_SCIENCE: fBaseMedian = GD_FLOAT_GET(BALANCE_SCIENCE_MEDIAN); break;
        case YIELD_CULTURE: fBaseMedian = GD_FLOAT_GET(BALANCE_CULTURE_MEDIAN); break;
        default: return 0.0f;
    }

    int iTotalModifier = GetTotalNeedModifierForYield(eYield, bForceRecalc);
    iTotalModifier += iAdditionalModifier;
    iTotalModifier += GET_PLAYER(getOwner()).GetNeedMedianModifierGlobalForYield(eYield);

    return fBaseMedian * (100 + iTotalModifier) / 100;
}
```

#### 2. **Consolidate Citizen Need Calculation**
```cpp
// Current: Four nearly-identical functions (GetDistress, GetPoverty, etc.)

// Suggested: Generic function with enum
enum CitizenNeedTypes
{
    CITIZEN_NEED_DISTRESS,      // YIELD_PRODUCTION
    CITIZEN_NEED_POVERTY,       // YIELD_GOLD
    CITIZEN_NEED_ILLITERACY,    // YIELD_SCIENCE
    CITIZEN_NEED_BOREDOM,       // YIELD_CULTURE
    NUM_CITIZEN_NEED_TYPES
};

int CvCity::GetCitizenNeed(CitizenNeedTypes eNeed, bool bForceRecalc, int iAssumedExtraYieldRate) const
{
    YieldTypes eYield = GetYieldForNeed(eNeed);
    
    // Get median threshold
    float fMedian = GetNeedMedian(eYield, bForceRecalc, 0);
    
    // Get city yield
    int iYield = getYieldRateTimes100(eYield, false, false) / 100;
    if (bForceRecalc)
        iYield += iAssumedExtraYieldRate;
    
    // Calculate deficit
    if (iYield >= fMedian)
        return 0;
    
    int iDeficit = (int)ceil(fMedian - (float)iYield);
    
    // Apply flat reduction
    int iReduction = GetFlatReductionForNeed(eNeed);
    iReduction += GET_PLAYER(getOwner()).GetFlatReductionGlobalForNeed(eNeed);
    
    return max(0, iDeficit - iReduction);
}
```

#### 3. **Add Happiness Source Registry**
```cpp
// Current: Hardcoded list of sources in DoUpdateTotalHappiness

// Suggested: Table-driven approach
struct HappinessSource
{
    CvString sName;
    int (CvPlayer::*pGetHappinessFunc)() const;
    bool bVPOnly;
    bool bLegacyOnly;
};

static const HappinessSource g_HappinessSources[] = 
{
    { "TXT_KEY_HAPPINESS_LUXURIES", &CvPlayer::GetBonusHappinessFromLuxuriesFlat, true, false },
    { "TXT_KEY_HAPPINESS_RELIGION", &CvPlayer::GetHappinessFromReligion, false, false },
    // ...
};

void CvPlayer::DoUpdateTotalHappiness()
{
    m_iHappiness = GetBaseHappiness();
    
    for (const HappinessSource& source : g_HappinessSources)
    {
        if ((source.bVPOnly && !MOD_BALANCE_VP) || (source.bLegacyOnly && MOD_BALANCE_VP))
            continue;
        
        m_iHappiness += (this->*source.pGetHappinessFunc)();
    }
    
    // ... distribution ...
}
```

Benefits: Easy to add sources, iterate for UI, export to Lua

---

## Lua API Gaps

Current Lua exports are incomplete for UI modding. Needed additions:

### Missing Player Functions
```cpp
// Luxury breakdown
int CvPlayer::GetHappinessFromLuxury(ResourceTypes eResource) const; // EXISTS but not exported

// Citizen need totals (already exist, should export)
int CvPlayer::GetUnhappinessFromDistress() const;     // EXISTS, line 22292
int CvPlayer::GetUnhappinessFromPoverty() const;      // EXISTS, line 22302
int CvPlayer::GetUnhappinessFromIlliteracy() const;   // EXISTS, line 22282
int CvPlayer::GetUnhappinessFromBoredom() const;      // EXISTS, line 22272

// Distribution details
int CvPlayer::GetCityHappinessNeedScore(CvCity* pCity) const; // NEW
```

### Missing City Functions
```cpp
// Median thresholds (for UI display)
float CvCity::GetBasicNeedsMedian(bool bForceRecalc, int iAdditionalModifier) const; // EXISTS but not exported
float CvCity::GetGoldMedian(bool bForceRecalc, int iAdditionalModifier) const;       // EXISTS but not exported
float CvCity::GetScienceMedian(bool bForceRecalc, int iAdditionalModifier) const;    // EXISTS but not exported
float CvCity::GetCultureMedian(bool bForceRecalc, int iAdditionalModifier) const;    // EXISTS but not exported

// Raw need values before reduction (for tooltip breakdowns)
int CvCity::GetDistressRaw(bool bForceRecalc, int iAssumedExtraYieldRate) const;    // EXISTS but not exported
int CvCity::GetPovertyRaw(bool bForceRecalc, int iAssumedExtraYieldRate) const;     // EXISTS but not exported
// etc.
```

### Recommended Lua Exports

**Add to CvLuaPlayer.cpp:**
```cpp
LUAAPIIMPL(Player, GetHappinessFromLuxury)
LUAAPIIMPL(Player, GetUnhappinessFromDistress)
LUAAPIIMPL(Player, GetUnhappinessFromPoverty)
LUAAPIIMPL(Player, GetUnhappinessFromIlliteracy)
LUAAPIIMPL(Player, GetUnhappinessFromBoredom)
```

**Add to CvLuaCity.cpp:**
```cpp
LUAAPIIMPL(City, GetBasicNeedsMedian)
LUAAPIIMPL(City, GetGoldMedian)
LUAAPIIMPL(City, GetScienceMedian)
LUAAPIIMPL(City, GetCultureMedian)
LUAAPIIMPL(City, GetDistressRaw)
LUAAPIIMPL(City, GetPovertyRaw)
LUAAPIIMPL(City, GetIlliteracyRaw)
LUAAPIIMPL(City, GetBoredomRaw)
```

---

## Testing Recommendations

### Unit Tests Needed

1. **Happiness Distribution Algorithm**
   - Test: 1 city gets all happiness
   - Test: 2 cities with equal need get equal shares
   - Test: City with higher need gets priority
   - Test: Excess happiness distributed by population
   - Test: Puppets excluded
   - Test: Occupied/razing cities excluded
   - Test: Venice exception (puppets included)

2. **Median Threshold Calculation**
   - Test: Base median values correct
   - Test: Empire size modifier increases thresholds
   - Test: Tech progress modifier increases thresholds
   - Test: City modifiers apply correctly
   - Test: Modifiers stack multiplicatively

3. **Unhappiness Aggregation**
   - Test: Unhappiness capped at population
   - Test: Source ordering respected (expensive sources last)
   - Test: Razing/resistance returns full population
   - Test: Puppet formula matches config

4. **Luxury Happiness**
   - Test: Base luxury happiness (4 per luxury)
   - Test: Monopoly bonus applies when global monopoly held
   - Test: Minor civ bonus applies when resource from minor
   - Test: World Congress ban disables happiness
   - Test: Netherlands UA retains 50% when last copy traded

5. **Happiness Penalties**
   - Test: Growth penalty scales with unhappiness
   - Test: Settler penalty scales with unhappiness
   - Test: Combat penalty -10 when unhappy, -20 when very unhappy
   - Test: No penalties when happiness >= 50
   - Test: Uprising counter decrements when super unhappy

### Integration Tests Needed

1. **Full Happiness Cycle**
   - Found city -> verify happiness drops
   - Build luxury improvement -> verify happiness rises
   - Distribute happiness -> verify cities get correct amounts
   - Grow city -> verify citizen needs increase
   - Build need-reducing building -> verify unhappiness drops

2. **UI Consistency**
   - Top panel happiness matches calculation
   - City banner happiness matches city values
   - Tooltip breakdowns sum correctly
   - Projection tooltips accurate (growth, trade, settling)

3. **Performance**
   - Benchmark `CalculateNetHappiness()` with 20+ cities
   - Verify caching works (no recalculation when nothing changed)
   - Profile `GetUnhappinessAggregated()` early-exit optimization

---

## Conclusion

The VP happiness system is sophisticated and well-designed, but suffers from **opacity** and **complexity**. The core mechanics work correctly, but players struggle to understand:
- Why they have the happiness they do
- How to improve their happiness
- What will happen if they grow/settle/trade

**Highest Priority Fixes:**
1. Add median threshold display to citizen need tooltips
2. Add per-luxury happiness breakdown to UI
3. Add happiness distribution explanation to city tooltips
4. Change happiness display from 0-100 to status levels (Content/Unhappy/Very Unhappy/Revolting)

**Medium Priority:**
1. Export missing Lua API functions for UI modding
2. Consolidate duplicate median calculation code
3. Document complex formulas in code comments
4. Add happiness projection widgets

**Low Priority (Nice to Have):**
1. Dynamic happiness distribution policies
2. Luxury synergy bonuses
3. Modular citizen needs system
4. War weariness breakdown tooltips

---

**Files Reviewed:**
- [CvPlayer.cpp](CvPlayer.cpp) - Main happiness calculation
- [CvPlayer.h](CvPlayer.h) - Player happiness interface
- [CvCity.cpp](CvCity.cpp) - City-level happiness/unhappiness
- [CvCity.h](CvCity.h) - City happiness interface
- [CvLuaCity.cpp](Lua/CvLuaCity.cpp) - City Lua exports

**Lines of Code Analyzed:** ~3000+ lines across happiness calculation functions
