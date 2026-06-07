# Health / Food Penalties Review

**Date:** January 9, 2026  
**Scope:** Food penalties, growth modifiers, starvation mechanics, unit supply penalties

---

## Executive Summary

Vox Populi does **not** have a traditional "health" system like Civilization IV. Instead, food/growth penalties come from:

1. **Famine Unhappiness** - Cities starving generate unhappiness (not health)
2. **Growth Modifiers** - Percentage modifiers affecting excess food after consumption
3. **Unit Supply System** - Military units over supply cap reduce both production AND growth
4. **Local Happiness Effects** - Local city happiness directly affects growth rates
5. **Food Consumption** - Specialists consume more food based on era

No "corruption" mechanic exists in VP. Distance-based penalties are handled through the **Isolation** unhappiness system (covered in happiness review).

---

## Food Calculation Flow

### Entry Point: `CvCity::doGrowth()`
**Location:** [CvCity.cpp](CvCity.cpp#L31088-L31150)

Called every turn to handle population growth/starvation:

```cpp
void CvCity::doGrowth()
{
    int iFoodPerTurn100 = getYieldRateTimes100(YIELD_FOOD); // x100 precision
    int iFoodReqForGrowth = growthThreshold();

    // Notification if starving
    if (iFoodPerTurn100 < 0)
    {
        // Send NOTIFICATION_STARVING
    }

    changeFoodTimes100(iFoodPerTurn100); // Add this turn's food to storage

    // Growth: food >= threshold
    if (getFood() >= iFoodReqForGrowth)
    {
        if (GetCityCitizens()->IsForcedAvoidGrowth())
            setFood(iFoodReqForGrowth); // Cap at threshold
        else
        {
            // Calculate food kept
            int iFoodKept = (iFoodReqForGrowth * getMaxFoodKeptPercent()) / 100;
            int iFoodStoreChange = max(0, iFoodReqForGrowth - iFoodKept);
            
            changeFood(-iFoodStoreChange);
            changePopulation(1);
            // Send NOTIFICATION_CITY_GROWTH if pop <= 5
        }
    }
    // Starvation: food <= 0 and negative per turn
    else if (getFoodTimes100() <= 0 && iFoodPerTurn100 < 0 && getPopulation() > 1)
    {
        changePopulation(-1);
    }
}
```

**Key Points:**
- Uses x100 precision to avoid rounding errors (250 = 2.50 food)
- Growth happens when stored food >= threshold
- Starvation only reduces population when storage is depleted
- Cannot starve below population 1

---

## Food Yield Calculation

### `CvCity::getYieldRateTimes100(YIELD_FOOD, ...)`
**Location:** [CvCity.cpp](CvCity.cpp#L23138) (approximately)

Multi-stage calculation:

```
1. Base Yield = Tiles + Buildings + Specialists + Processes + Policies + ...
2. Modified Yield = Base * (100 + YieldModifier) / 100
3. Post-Modifier Yield = Trade Routes + Other Flat Sources
4. Total Before Growth = Modified + Post-Modifier
5. Subtract Food Consumption
6. Apply Growth Modifiers to Excess (if positive)
7. Final Yield = Result after all modifiers
```

**Critical Detail:** Food consumption is subtracted **before** growth modifiers are applied. This means growth modifiers only affect excess food, not the consumption itself.

---

## Food Consumption

### Non-Specialist Food Consumption
**Function:** `getFoodConsumptionNonSpecialistTimes100()`

```cpp
// Typically 2.00 food per non-specialist citizen
int iConsumptionPerPop = /*2*/ GD_INT_GET(FOOD_CONSUMPTION_PER_POPULATION) * 100;
```

**Standard:** 2 food per citizen

**Exception:** If city has `IsNoStarvationNonSpecialist()` trait, consumption is capped at total food production before consumption, preventing starvation from non-specialists.

### Specialist Food Consumption
**Function:** `getFoodConsumptionSpecialistTimes100()`
**Location:** [CvCity.cpp](CvCity.cpp#L16044-L16070)

```cpp
int CvCity::getFoodConsumptionSpecialistTimes100() const
{
    int iFoodPerSpec = 0;
    if (MOD_BALANCE_VP)
    {
        // Specialists eat more food as game progresses
        // max(CurrentEra, 2) + 1, capped at 10
        iFoodPerSpec = max((int)GET_PLAYER(getOwner()).GetCurrentEra(), /*2*/ GD_INT_GET(FOOD_CONSUMPTION_PER_POPULATION)) + 1;
        iFoodPerSpec = min(iFoodPerSpec, 10) * 100;
    }
    else
    {
        iFoodPerSpec = /*2*/ GD_INT_GET(FOOD_CONSUMPTION_PER_POPULATION) * 100;
    }

    iFoodPerSpec += GET_PLAYER(getOwner()).GetSpecialistFoodChange() * 100;

    // Half specialist food policies
    if (GET_PLAYER(getOwner()).isHalfSpecialistFood())
        iFoodPerSpec /= 2;
    if (GET_PLAYER(getOwner()).isHalfSpecialistFoodCapital() && isCapital())
        iFoodPerSpec /= 2;

    return max(100, iFoodPerSpec); // Minimum 1 food per specialist
}
```

**Era-Based Scaling (VP):**
- Ancient/Classical: 3 food per specialist
- Medieval: 4 food
- Renaissance: 5 food
- Industrial: 6 food
- Modern: 7 food
- Atomic: 8 food
- Information: 9 food

**Design Rationale:** Prevents specialist spam in late game by increasing their food cost.

### Total Food Consumption
**Function:** `getFoodConsumptionTimes100(bool bIgnoreProcess, bool bAssumeNoReductionForNonSpecialists)`
**Location:** [CvCity.cpp](CvCity.cpp#L16074-L16095)

```cpp
int CvCity::getFoodConsumptionTimes100(bool bIgnoreProcess, bool bAssumeNoReductionForNonSpecialists) const
{
    int iFoodPerTurnBeforeConsumption = getFoodPerTurnBeforeConsumptionTimes100(bIgnoreProcess);
    int iSpecialists = GetCityCitizens()->GetTotalSpecialistCount();
    int iNonSpecialists = max(0, (getPopulation() - iSpecialists));

    int iConsumptionNonSpecialists = getFoodConsumptionNonSpecialistTimes100() * iNonSpecialists;
    
    // NoStarvationNonSpecialist trait caps consumption
    if (IsNoStarvationNonSpecialist() && !bAssumeNoReductionForNonSpecialists)
    {
        iConsumptionNonSpecialists = min(iFoodPerTurnBeforeConsumption, iConsumptionNonSpecialists);
    }

    int iTotalConsumption = max(100, iConsumptionNonSpecialists + getFoodConsumptionSpecialistTimes100() * iSpecialists);
    
    // Cannot starve if at size 1 and nothing stored
    // (implementation continues...)
    
    return iTotalConsumption;
}
```

**Anti-Starvation Protections:**
1. Minimum consumption 1 food (prevents division by zero)
2. Size 1 cities with no stored food cannot consume food
3. `IsNoStarvationNonSpecialist()` trait prevents non-specialist starvation

---

## Growth Modifiers

### `CvCity::getGrowthMods(CvString* toolTipSink, int iAssumedLocalHappinessChange)`
**Location:** [CvCity.cpp](CvCity.cpp#L16132-L16270)

Calculates **percentage modifier** applied to excess food (food after consumption). Only affects cities that are growing (positive food).

**Sources (in order):**

#### 1. **Capital Growth Bonus**
```cpp
if (isCapital())
{
    int iCapitalGrowthMod = GET_PLAYER(getOwner()).GetCapitalGrowthMod();
    iTotalMod += iCapitalGrowthMod; // From policies
}
```

#### 2. **Player Growth Modifier**
```cpp
int iCityGrowthMod = GET_PLAYER(getOwner()).GetCityGrowthMod();
iTotalMod += iCityGrowthMod; // From policies, traits
```

#### 3. **Golden Age Modifiers**
```cpp
if (GET_PLAYER(getOwner()).isGoldenAge())
{
    iTotalMod += GetGoldenAgeYieldMod(YIELD_FOOD);           // From buildings
    iTotalMod += GET_PLAYER(getOwner()).getGoldenAgeYieldMod(YIELD_FOOD); // From policies
    iTotalMod += GET_PLAYER(getOwner()).GetPlayerTraits()->GetGoldenAgeYieldModifier(YIELD_FOOD); // From traits
}
```

#### 4. **Unit Supply Penalty** (VP Only) ⚠️
**Location:** [CvCity.cpp](CvCity.cpp#L16138-L16145)

```cpp
int iSupply = GET_PLAYER(getOwner()).GetNumUnitsOutOfSupply();
if (MOD_BALANCE_VP && iSupply > 0)
{
    int iSupplyMod = GET_PLAYER(getOwner()).GetUnitGrowthMaintenanceMod();
    iTotalMod += iSupplyMod; // Negative modifier
}
```

**Calculation:** [CvPlayer.cpp](CvPlayer.cpp#L16536-L16548)
```cpp
int CvPlayer::calculateUnitGrowthMaintenanceMod() const
{
    int iUnitsOverSupply = GetNumUnitsOutOfSupply();
    if (iUnitsOverSupply > 0)
    {
        // Example: 4 units over supply * 5 = -20% growth
        int iMaintenanceMod = min(
            /*70*/ max(GD_INT_GET(MAX_UNIT_SUPPLY_GROWTH_MOD), 0),
            iUnitsOverSupply * /*5*/ max(GD_INT_GET(GROWTH_PENALTY_PER_UNIT_OVER_SUPPLY), 0)
        );
        return iMaintenanceMod * -1;
    }
    return 0;
}
```

**Key Values:**
- `GROWTH_PENALTY_PER_UNIT_OVER_SUPPLY` = 5 (VP) / 0 (CP - disabled)
- `MAX_UNIT_SUPPLY_GROWTH_MOD` = 70 (max penalty -70%)
- Formula: `-min(70, UnitsOverSupply * 5)`

**Example:** 
- 4 units over supply → -20% growth
- 10 units over supply → -50% growth
- 15+ units over supply → -70% growth (capped)

#### 5. **Event & Tourism Modifiers**
```cpp
int iGrowthEvent = GetGrowthFromEvent();
iTotalMod += iGrowthEvent;

int iGrowthTourism = GetGrowthFromTourism();
iTotalMod += iGrowthTourism;
```

#### 6. **Puppet Penalty**
```cpp
if (IsPuppet())
{
    int iTempMod = min(0, /*0*/ GD_INT_GET(PUPPET_GROWTH_MODIFIER));
    iTotalMod += iTempMod;
}
```

**Default:** 0% (puppets grow normally in VP, can be changed via defines)

#### 7. **Religion Modifiers**
```cpp
ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
if (eMajority != NO_RELIGION)
{
    const CvReligion* pReligion = GetCityReligions()->GetMajorityReligion();
    if (pReligion)
    {
        bool bAtPeace = GET_TEAM(getTeam()).getAtWarCount(false) == 0;
        iReligionGrowthMod = pReligion->m_Beliefs.GetCityGrowthModifier(bAtPeace, getOwner(), ...);
        
        // Trait: population boost from religion
        if (GET_PLAYER(getOwner()).GetPlayerTraits()->IsPopulationBoostReligion() && 
            eMajority == GET_PLAYER(getOwner()).GetReligions()->GetStateReligion(true))
        {
            int iFollowers = GetCityReligions()->GetNumFollowers(eMajority);
            iReligionGrowthMod += (iFollowers * /*0*/ GD_INT_GET(BALANCE_FOLLOWER_GROWTH_BONUS));
        }
        
        // Secondary pantheon
        BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
        if (eSecondaryPantheon != NO_BELIEF)
        {
            iReligionGrowthMod += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityGrowthModifier();
        }
    }
}
iTotalMod += iReligionGrowthMod;
```

#### 8. **Permanent Pantheons** (if MOD_BALANCE_PERMANENT_PANTHEONS)
```cpp
if (MOD_BALANCE_PERMANENT_PANTHEONS && GC.getGame().GetGameReligions()->HasCreatedPantheon(getOwner()))
{
    const CvReligion* pPantheon = GC.getGame().GetGameReligions()->GetReligion(RELIGION_PANTHEON, getOwner());
    BeliefTypes ePantheonBelief = GC.getGame().GetGameReligions()->GetBeliefInPantheon(getOwner());
    if (pPantheon != NULL && ePantheonBelief != NO_BELIEF)
    {
        // Check that our majority religion doesn't already have our pantheon belief
        const CvReligion* pReligion = GetCityReligions()->GetMajorityReligion();
        if (pReligion == NULL || !pReligion->m_Beliefs.IsPantheonBeliefInReligion(ePantheonBelief, ...))
        {
            iReligionGrowthMod += GC.GetGameBeliefs()->GetEntry(ePantheonBelief)->GetCityGrowthModifier();
        }
    }
}
```

#### 9. **Local Happiness Modifier** (VP Only) ⭐
**Location:** [CvCity.cpp](CvCity.cpp#L16207-L16237)

```cpp
if (MOD_BALANCE_VP)
{
    int iHappiness = getHappinessDelta() + iAssumedLocalHappinessChange;

    // Scale happiness effect
    if (iHappiness > 0)
        iHappiness *= /*2*/ GD_INT_GET(LOCAL_HAPPINESS_FOOD_MODIFIER);  // +2% per local happiness
    else
        iHappiness *= /*10*/ GD_INT_GET(LOCAL_UNHAPPINESS_FOOD_MODIFIER); // -10% per local unhappiness

    // If empire is unhappy, ignore positive local happiness
    if (GET_PLAYER(getOwner()).IsEmpireUnhappy())
    {
        if (iHappiness > 0)
            iHappiness = 0;

        // Add empire-wide unhappiness penalty
        iHappiness += GET_PLAYER(getOwner()).GetUnhappinessGrowthPenalty();
    }

    iHappiness = range(iHappiness, -100, 100); // Cap at ±100%
    iTotalMod += iHappiness;
}
```

**Key Values:**
- `LOCAL_HAPPINESS_FOOD_MODIFIER` = 2 (each local happiness → +2% growth)
- `LOCAL_UNHAPPINESS_FOOD_MODIFIER` = 10 (each local unhappiness → -10% growth)

**Example:**
- City with +5 local happiness → +10% growth
- City with -3 local unhappiness → -30% growth
- If empire unhappy, positive local happiness ignored

**Empire Unhappiness Penalty:**
From [CvPlayer.cpp](CvPlayer.cpp#L20363-L20370):
```cpp
int CvPlayer::GetUnhappinessGrowthPenalty() const
{
    if (MOD_BALANCE_VP)
    {
        // 2.5% penalty per point below threshold (50)
        return range(
            static_cast<int>(/*2.5f*/ GD_FLOAT_GET(GLOBAL_GROWTH_PENALTY_PER_UNHAPPY) *
                            (GetExcessHappiness() - GD_INT_GET(UNHAPPY_THRESHOLD))),
            -100, 0
        );
    }
    return 0;
}
```

**Example:** If empire happiness = 40 (10 below threshold 50):
- Penalty = 2.5 * (40 - 50) = 2.5 * -10 = **-25% growth**

#### 10. **Legacy Unhappiness Penalties** (Non-VP)
```cpp
else // !MOD_BALANCE_VP
{
    if (GET_PLAYER(getOwner()).IsEmpireVeryUnhappy())
    {
        int iMod = /*-100*/ GD_INT_GET(VERY_UNHAPPY_GROWTH_PENALTY);
        iTotalMod += iMod; // -100% growth (no growth at all)
    }
    else if (GET_PLAYER(getOwner()).IsEmpireUnhappy())
    {
        int iMod = /*-75*/ GD_INT_GET(UNHAPPY_GROWTH_PENALTY);
        iTotalMod += iMod; // -75% growth
    }
}
```

#### 11. **We Love The King Day (WLTKD)**
```cpp
if (GetWeLoveTheKingDayCounter() > 0)
{
    int iMod = /*25*/ GD_INT_GET(WLTKD_GROWTH_MULTIPLIER) + GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon();
    iTotalMod += iMod;
}
```

**Default:** +25% growth during WLTKD + trait bonuses

#### Final Return
```cpp
return max(-100, iTotalMod); // Cannot reduce growth below -100%
```

---

## Famine Unhappiness

### `CvCity::GetUnhappinessFromFamine()`
**Location:** [CvCity.cpp](CvCity.cpp#L21533-L21553)

Cities that are starving generate unhappiness:

```cpp
int CvCity::GetUnhappinessFromFamine() const
{
    if (IsPuppet() || IsResistance() || IsRazing())
        return 0;

    // Calculate net food (before growth mods)
    int iDiff = (getFoodPerTurnBeforeConsumptionTimes100() - getFoodConsumptionTimes100()) / 100;
    
    // Only applies if negative and not building a settler
    if (iDiff < 0 && !isFoodProduction())
    {
        iDiff *= -1; // Make positive for calculation

        float fUnhappiness = 0.00f;
        float fUnhappyPerDeficit = /*1.0f*/ GD_FLOAT_GET(UNHAPPINESS_PER_STARVING_POP);
        fUnhappiness += (float)iDiff * fUnhappyPerDeficit;

        int iLimit = MOD_BALANCE_UNCAPPED_UNHAPPINESS ? INT_MAX : getPopulation();
        return range((int)fUnhappiness, 0, iLimit);
    }

    return 0;
}
```

**Formula:** 
- Unhappiness = `abs(NetFood) * UNHAPPINESS_PER_STARVING_POP`
- Default: 1 unhappiness per food deficit
- Capped at city population (unless uncapped mode)

**Example:**
- City producing 10 food, consuming 14 food → -4 food → **4 unhappiness**
- City building settler (food production) → **0 unhappiness** (settlers exempt)

**Design Notes:**
- Starvation is punished via unhappiness, not directly via health
- Creates feedback loop: starvation → unhappiness → worse growth → more starvation
- Puppets/resistance/razing cities exempt (already have other penalties)

---

## Unit Supply System

### Overview
Players can maintain a limited number of military units without penalty. Units beyond this limit ("over supply") cause penalties to:
1. **Production** (all cities)
2. **Growth** (all cities, VP only)
3. **Gold maintenance** (separate system)

### Supply Cap Calculation

#### `CvPlayer::GetNumUnitsSupplied(bool bCheckWarWeariness)`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L16552-L16620) (approximately)

```cpp
int CvPlayer::GetNumUnitsSupplied(bool bCheckWarWeariness) const
{
    int iUnitSupply = GetNumUnitsSuppliedByHandicap();
    iUnitSupply += GetNumUnitsSuppliedByCities();
    iUnitSupply += GetNumUnitsSuppliedByPopulation();

    // Policies, Buildings, etc.
    iUnitSupply += GetBaseSupplyPerCity() * getNumCities();
    iUnitSupply += GetBaseSupplyPerPopulation() * getTotalPopulation();

    // War weariness reduction (if at war long enough)
    if (bCheckWarWeariness)
    {
        int iWarWearinessSupplyPenalty = GetWarWearinessSupplyPenalty();
        iUnitSupply -= iWarWearinessSupplyPenalty;
    }

    // Empire size penalty reduction (policies, buildings)
    // ... various modifiers ...

    return max(iUnitSupply / 100, 0);
}
```

#### `CvPlayer::GetNumUnitsOutOfSupply(bool bCheckWarWeariness)`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L16804-L16813)

```cpp
int CvPlayer::GetNumUnitsOutOfSupply(bool bCheckWarWeariness) const
{
    if (!isAlive() || isBarbarian())
        return 0;

    return std::max(0, GetNumUnitsToSupply() - GetNumUnitsSupplied(bCheckWarWeariness));
}
```

Where:
```cpp
int CvPlayer::GetNumUnitsToSupply() const
{
    return getNumMilitaryUnits() - getNumUnitsSupplyFree();
}
```

**Formula:**
```
UnitsToSupply = MilitaryUnits - FreeUnits
UnitsSupplied = (Handicap + Cities + Population + Modifiers) / 100
UnitsOverSupply = max(0, UnitsToSupply - UnitsSupplied)
```

### Production Penalty

#### `CvPlayer::calculateUnitProductionMaintenanceMod()`
**Location:** [CvPlayer.cpp](CvPlayer.cpp#L16502-L16514)

```cpp
int CvPlayer::calculateUnitProductionMaintenanceMod() const
{
    int iUnitsOverSupply = GetNumUnitsOutOfSupply();
    if (iUnitsOverSupply > 0)
    {
        // Example: 4 units over supply * 5 = -20% production
        int iMaintenanceMod = min(
            /*70*/ max(GD_INT_GET(MAX_UNIT_SUPPLY_PRODMOD), 0),
            iUnitsOverSupply * /*5*/ max(GD_INT_GET(PRODUCTION_PENALTY_PER_UNIT_OVER_SUPPLY), 0)
        );
        return iMaintenanceMod * -1;
    }
    return 0;
}
```

**Key Values:**
- `PRODUCTION_PENALTY_PER_UNIT_OVER_SUPPLY` = 5 (VP) / 10 (CP)
- `MAX_UNIT_SUPPLY_PRODMOD` = 70 (max penalty -70%)
- Applies to **all** production (units, buildings, wonders)

**Example (VP):**
- 4 units over → -20% production
- 10 units over → -50% production
- 14+ units over → -70% production (capped)

### Growth Penalty (VP Only)

**Already covered above in Growth Modifiers section #4.**

### Comparison: CP vs VP

| Aspect | Community Patch (CP) | Vox Populi (VP) |
|--------|---------------------|----------------|
| Production Penalty Per Unit | -10% | -5% |
| Growth Penalty Per Unit | 0% (disabled) | -5% |
| Max Production Penalty | -70% | -70% |
| Max Growth Penalty | N/A | -70% |

**Design Philosophy:**
- **CP:** Harsh production penalty, no growth penalty (encourages smaller armies)
- **VP:** Moderate penalties to both (allows larger armies but with scaling cost)

---

## No Corruption System

**Important:** Vox Populi does **NOT** have a corruption mechanic like some mods or older Civilization games.

**Distance-Based Penalties** are handled via:
1. **Isolation Unhappiness** - Cities without capital connection generate unhappiness
   - See [CvCity.cpp](CvCity.cpp#L21590-L21638)
   - Formula: `Population * UNHAPPINESS_PER_ISOLATED_POP` (default 0.34)
   - Exemptions: capital, trade route to capital, policy/trait bonuses
   
2. **No direct gold or production penalties from distance**

**Why No Corruption?**
- VP uses unhappiness as the primary empire expansion penalty
- Citizen needs (Distress, Poverty, etc.) scale with empire size via median modifiers
- Unit supply cap scales with cities/population, creating indirect expansion cost

---

## Food-Related Modifiers (Crime/Development)

### Crime & Development Yield Modifiers
**Location:** [CvCity.cpp](CvCity.cpp#L34990-L35010)

```cpp
void CvCity::SetYieldModifierFromCrime(YieldTypes eYield, int iValue)
{
    if (GetYieldModifierFromCrime(eYield) != iValue)
    {
        m_aiYieldModifierFromCrime[eYield] = iValue;
        UpdateCityYields(eYield);
    }
}

void CvCity::SetYieldModifierFromDevelopment(YieldTypes eYield, int iValue)
{
    if (GetYieldModifierFromDevelopment(eYield) != iValue)
    {
        m_aiYieldModifierFromDevelopment[eYield] = iValue;
        UpdateCityYields(eYield);
    }
}
```

**Usage:** These are **mod hooks** for custom mechanics:
- `YieldModifierFromCrime` - Negative yield modifiers from crime/disorder
- `YieldModifierFromDevelopment` - Positive yield modifiers from development

**Status:** Present in code but **not actively used** in base VP. Available for community mods.

**Potential Use Cases:**
- Espionage mod: enemy spies cause crime → food/production penalties
- Events mod: plague/disaster events reduce yields temporarily
- Building mods: specific buildings reduce crime or boost development

---

## Issues and Improvements

### Current Issues

#### 1. **Unit Supply Penalties Unclear**
**Problem:** Players don't understand why their growth/production is suddenly reduced.
- No clear UI indication of units over supply
- Penalty calculation opaque (5% per unit)
- War weariness interaction confusing

**Suggested Fix:**
- Add top panel indicator: "Units: 15/10 (5 over supply, -25% production/growth)"
- Add tooltip: "Each unit over supply reduces production and growth by 5%. Current penalty: -25%"
- Add city production tooltip section showing unit supply modifier
- Highlight in red when severely over supply (10+ units)

#### 2. **Specialist Food Consumption Scaling Hidden**
**Problem:** Players don't realize specialists cost more food in later eras.
- Era-based scaling (3-9 food) not communicated
- Late-game specialist spam looks viable but causes starvation
- No tooltip showing current specialist food cost

**Suggested Fix:**
- Add to specialist tooltip: "Food consumption: X per specialist (Era: Y)"
- Add projection: "Running X specialists = Y food/turn consumed"
- Show in city screen: "Specialist consumption: X/Y total food"
- Add warning when specialist consumption > city food production

#### 3. **Growth Modifier Stacking Confusing**
**Problem:** Too many growth modifiers stack multiplicatively without clear feedback.
- Local happiness: ±100%
- Empire unhappiness: -25%
- Unit supply: -20%
- Religion: +10%
- Golden age: +10%
- Final modifier unclear

**Suggested Fix:**
- Add growth tooltip breakdown showing each source:
  ```
  Growth Modifiers:
  - Base: 100%
  - Local happiness (+3): +6%
  - Empire unhappy (40/50): -25%
  - Units over supply (4): -20%
  - Religion: +10%
  - WLTKD: +25%
  ────────────────
  Total: 96% (excess food x 0.96)
  ```
- Show final effective food after all modifiers

#### 4. **Famine Unhappiness Feedback Loop**
**Problem:** Starvation creates cascading failure.
- Starvation → unhappiness → growth penalty → more starvation
- No clear way to break the cycle
- Especially punishing in late game with expensive specialists

**Suggested Fix:**
- Add building: "Reduces famine unhappiness by 50%"
- Add policy: "Cities receive +2 food when starving"
- Add UI warning **before** starvation: "This city will starve next turn"
- Show projected unhappiness from famine in city view

#### 5. **Food Production (Settler) Mechanics Unclear**
**Problem:** Food production (settlers/workers) has special rules not explained.
- Exempts from famine unhappiness
- Uses percentage of food production
- Modifiers apply differently

**Suggested Fix:**
- Add tooltip: "Food Production active: 50% of excess food → hammers"
- Show food→production conversion in production tooltip
- Clarify famine exemption: "No famine unhappiness while building settlers"

#### 6. **NoStarvationNonSpecialist Trait Undocumented**
**Problem:** Hidden trait prevents non-specialist starvation.
- Used by Venice/One City Challenge
- Not visible in UI
- Behavior confusing (specialists can starve, others cannot)

**Suggested Fix:**
- Add trait icon to city view when active
- Add tooltip: "Non-specialists cannot cause starvation"
- Show in food consumption breakdown: "Non-specialist consumption: X (capped at food production)"

### Potential Improvements

#### 1. **Dynamic Unit Supply Cap**
**Enhancement:** Make supply cap scale with game progress, not just empire size.

```cpp
// Current: Supply = Handicap + Cities + Population
// Proposed: Supply = Handicap + Cities + Population + (Era * 5)

int iEraSupplyBonus = (int)GET_PLAYER(getOwner()).GetCurrentEra() * 5;
iUnitSupply += iEraSupplyBonus;
```

**Rationale:** Late game should support larger armies naturally. Current system forces painful choices between military and economy.

#### 2. **Graduated Supply Penalties**
**Enhancement:** Non-linear penalty curve reduces harshness of first few units over supply.

```cpp
// Current: Linear -5% per unit
// Proposed: Graduated curve

int CalculateSupplyPenalty(int iUnitsOver)
{
    if (iUnitsOver <= 2)
        return iUnitsOver * 2;  // -2% each for first 2
    else if (iUnitsOver <= 5)
        return 4 + (iUnitsOver - 2) * 5;  // -5% each for next 3
    else
        return 19 + (iUnitsOver - 5) * 10; // -10% each beyond 5
}
```

**Example:**
- 1 over: -2%
- 2 over: -4%
- 3 over: -9%
- 4 over: -14%
- 5 over: -19%
- 10 over: -69% (approaching cap)

**Rationale:** Allows brief military buildups without crippling penalty, but still punishes excessive standing armies.

#### 3. **Local Food Bonuses**
**Enhancement:** Make food yield more valuable through local bonuses.

**Proposed Buildings:**
- Granary II: +1 food per farmed resource (wheat, cattle, etc.)
- Aqueduct II: +10% food in city
- Hospital: Specialists consume -1 food
- Hydroelectric Dam: +2 food from river tiles

**Proposed Policies:**
- Agricultural Communes: +15% food in cities with granary
- Green Revolution: +1 food from farms adjacent to city
- Nutrition Science: -1 specialist food consumption

#### 4. **Happiness-Affected Food Consumption**
**Enhancement:** Happy cities consume less food per pop (more efficient).

```cpp
int CvCity::getFoodConsumptionNonSpecialistTimes100() const
{
    int iConsumption = /*2*/ GD_INT_GET(FOOD_CONSUMPTION_PER_POPULATION) * 100;
    
    // Happy cities are more efficient
    int iHappinessDelta = getHappinessDelta();
    if (iHappinessDelta > 0)
    {
        // Reduce consumption by 5% per happiness (max -25%)
        int iReduction = min(25, iHappinessDelta * 5);
        iConsumption *= (100 - iReduction);
        iConsumption /= 100;
    }
    
    return max(100, iConsumption);
}
```

**Example:**
- +3 local happiness → -15% food consumption → 1.70 food/pop instead of 2.00
- Specialists unaffected (already have era scaling)

**Rationale:** Rewards cities that invest in happiness infrastructure. Creates positive feedback instead of only negative (current famine system).

#### 5. **Conditional Growth Penalties**
**Enhancement:** Make growth penalties context-aware.

**Proposed:**
- Unit supply penalty only applies during peacetime
  - Wartime: reduced or no growth penalty (but production penalty remains)
  - Rationale: Mobilization is acceptable during war
  
- Empire unhappiness penalty exempt from capital
  - Capital always at base growth rate
  - Rationale: Capital should be stable, remote cities suffer more

```cpp
int iSupplyMod = GET_PLAYER(getOwner()).GetUnitGrowthMaintenanceMod();
if (isCapital() || GET_TEAM(getTeam()).getAtWarCount(true) > 0)
    iSupplyMod /= 2; // Half penalty for capital or during war
iTotalMod += iSupplyMod;
```

#### 6. **Food Deficit Escalation**
**Enhancement:** Make starvation progressively worse instead of immediate disaster.

**Proposed Stages:**
1. **Mild Deficit** (-1 to -3 food): -10% growth, 1 unhappiness
2. **Moderate Deficit** (-4 to -7 food): -25% growth, 3 unhappiness
3. **Severe Deficit** (-8+ food): -50% growth, 5+ unhappiness

```cpp
int CvCity::GetUnhappinessFromFamine() const
{
    int iDiff = (getFoodPerTurnBeforeConsumptionTimes100() - getFoodConsumptionTimes100()) / 100;
    if (iDiff < 0 && !isFoodProduction())
    {
        iDiff *= -1;
        
        float fUnhappiness = 0.00f;
        if (iDiff <= 3)
            fUnhappiness = iDiff * 0.5f; // Mild: 0.5 per food
        else if (iDiff <= 7)
            fUnhappiness = 1.5f + (iDiff - 3) * 0.75f; // Moderate: 0.75 per food
        else
            fUnhappiness = 4.5f + (iDiff - 7) * 1.0f; // Severe: 1.0 per food
        
        int iLimit = MOD_BALANCE_UNCAPPED_UNHAPPINESS ? INT_MAX : getPopulation();
        return range((int)fUnhappiness, 0, iLimit);
    }
    return 0;
}
```

**Rationale:** Gives players time to react to food problems before catastrophic failure.

---

## Code Quality Observations

### Strengths
1. **Precision handling:** x100 arithmetic prevents rounding errors in food calculations
2. **Clear separation:** Food consumption, production, and modifiers are separate functions
3. **Defensive programming:** Max/min bounds prevent division by zero and overflow
4. **Performance:** Caching of supply calculations reduces per-turn overhead

### Weaknesses
1. **Magic numbers:** Many defines used without inline comments explaining rationale
2. **Complex stacking:** Growth modifiers applied in specific order without documentation
3. **Hidden mechanics:** Era-based specialist food cost buried in implementation
4. **Inconsistent naming:** Some functions use `getTurnsLeft`, others `PerTurn`, others `Times100`

### Suggested Refactors

#### 1. **Extract Growth Modifier Registry**
```cpp
struct GrowthModifierSource
{
    CvString sName;
    int (CvCity::*pGetModifierFunc)() const;
    bool bOnlyIfPositiveFood;
};

static const GrowthModifierSource g_GrowthModifiers[] = 
{
    { "TXT_KEY_GROWTH_MOD_CAPITAL", &CvCity::GetCapitalGrowthModifier, false },
    { "TXT_KEY_GROWTH_MOD_PLAYER", &CvCity::GetPlayerGrowthModifier, false },
    { "TXT_KEY_GROWTH_MOD_SUPPLY", &CvCity::GetSupplyGrowthModifier, false },
    // ...
};

int CvCity::getGrowthMods(CvString* toolTipSink, int iAssumedLocalHappinessChange) const
{
    int iTotalMod = 0;
    for (const GrowthModifierSource& source : g_GrowthModifiers)
    {
        int iMod = (this->*source.pGetModifierFunc)();
        iTotalMod += iMod;
        if (toolTipSink && iMod != 0)
            GC.getGame().BuildProdModHelpText(toolTipSink, source.sName, iMod);
    }
    return max(-100, iTotalMod);
}
```

Benefits: Easy to add modifiers, iterate for UI, export to Lua

#### 2. **Consolidate Food Calculation**
```cpp
struct FoodCalculation
{
    int iFoodProduction;         // From tiles/buildings/etc
    int iFoodConsumption;        // Population consumption
    int iFoodNetBeforeMods;      // Production - Consumption
    int iGrowthModifier;         // Percentage modifier
    int iFoodNetAfterMods;       // Final result
    
    bool bIsGrowing;             // iFoodNetAfterMods > 0
    bool bIsStarving;            // iFoodNetAfterMods < 0
    bool bIsFoodProduction;      // Building settler/worker
};

FoodCalculation CvCity::CalculateFoodDetails() const
{
    FoodCalculation result;
    result.iFoodProduction = getFoodPerTurnBeforeConsumptionTimes100();
    result.iFoodConsumption = getFoodConsumptionTimes100();
    result.iFoodNetBeforeMods = result.iFoodProduction - result.iFoodConsumption;
    result.iGrowthModifier = getGrowthMods();
    
    if (result.iFoodNetBeforeMods > 0)
    {
        result.iFoodNetAfterMods = result.iFoodNetBeforeMods * (100 + result.iGrowthModifier) / 100;
        result.bIsGrowing = true;
    }
    else
    {
        result.iFoodNetAfterMods = result.iFoodNetBeforeMods; // No mods when starving
        result.bIsStarving = true;
    }
    
    result.bIsFoodProduction = isFoodProduction();
    return result;
}
```

Benefits: Single source of truth, easy to export to UI/Lua, testable

#### 3. **Separate Supply Penalty Calculation**
```cpp
class UnitSupplyPenaltyCalculator
{
public:
    static int CalculateProductionPenalty(int iUnitsOver);
    static int CalculateGrowthPenalty(int iUnitsOver);
    static CvString GetPenaltyTooltip(int iUnitsOver);
    
private:
    static int ApplyPenaltyCurve(int iUnitsOver, int iPenaltyPerUnit, int iMaxPenalty);
};

int UnitSupplyPenaltyCalculator::CalculateProductionPenalty(int iUnitsOver)
{
    if (iUnitsOver <= 0) return 0;
    
    int iPenaltyPerUnit = max(GD_INT_GET(PRODUCTION_PENALTY_PER_UNIT_OVER_SUPPLY), 0);
    int iMaxPenalty = max(GD_INT_GET(MAX_UNIT_SUPPLY_PRODMOD), 0);
    
    return ApplyPenaltyCurve(iUnitsOver, iPenaltyPerUnit, iMaxPenalty) * -1;
}
```

Benefits: Easier to test, modify penalty curves, export to UI

---

## Lua API Gaps

### Missing City Functions
```cpp
// Food calculation details (for UI tooltips)
int CvCity::getFoodPerTurnBeforeConsumptionTimes100(bool bIgnoreProcess) const; // EXISTS but not exported
int CvCity::getFoodConsumptionTimes100(bool bIgnoreProcess, bool bAssumeNoReductionForNonSpecialists) const; // EXISTS but not exported
int CvCity::getFoodConsumptionSpecialistTimes100() const; // EXISTS but not exported
int CvCity::getFoodConsumptionNonSpecialistTimes100() const; // EXISTS but not exported

// Growth modifiers (for tooltip breakdown)
int CvCity::getGrowthMods(CvString* toolTipSink, int iAssumedLocalHappinessChange) const; // EXISTS but not fully exported

// Famine/starvation (for UI warnings)
int CvCity::GetUnhappinessFromFamine() const; // EXISTS but not exported
```

### Missing Player Functions
```cpp
// Unit supply details
int CvPlayer::GetNumUnitsOutOfSupply(bool bCheckWarWeariness) const; // EXISTS but not exported
int CvPlayer::GetNumUnitsSupplied(bool bCheckWarWeariness) const; // EXISTS but not exported
int CvPlayer::GetNumUnitsToSupply() const; // EXISTS but not exported
int CvPlayer::GetUnitProductionMaintenanceMod() const; // EXISTS but not exported
int CvPlayer::GetUnitGrowthMaintenanceMod() const; // EXISTS but not exported
```

### Recommended Lua Exports

**Add to CvLuaCity.cpp:**
```cpp
LUAAPIIMPL(City, getFoodPerTurnBeforeConsumptionTimes100)
LUAAPIIMPL(City, getFoodConsumptionTimes100)
LUAAPIIMPL(City, getFoodConsumptionSpecialistTimes100)
LUAAPIIMPL(City, GetUnhappinessFromFamine)
```

**Add to CvLuaPlayer.cpp:**
```cpp
LUAAPIIMPL(Player, GetNumUnitsOutOfSupply)
LUAAPIIMPL(Player, GetNumUnitsSupplied)
LUAAPIIMPL(Player, GetNumUnitsToSupply)
LUAAPIIMPL(Player, GetUnitProductionMaintenanceMod)
LUAAPIIMPL(Player, GetUnitGrowthMaintenanceMod)
```

---

## Testing Recommendations

### Unit Tests Needed

1. **Food Consumption**
   - Test: Specialist food cost scales with era
   - Test: Non-specialist food cost constant at 2
   - Test: NoStarvationNonSpecialist caps consumption
   - Test: Minimum 1 food consumption per turn

2. **Growth Modifiers**
   - Test: Local happiness scales correctly (2% per happy, 10% per unhappy)
   - Test: Empire unhappiness ignores local happiness bonuses
   - Test: Growth modifier capped at -100%
   - Test: Unit supply penalty scales linearly (5% per unit)
   - Test: Multiple modifiers stack additively

3. **Starvation**
   - Test: Population loss when food <= 0 and negative per turn
   - Test: No loss at population 1
   - Test: Famine unhappiness calculated correctly
   - Test: Settler production exempts from famine unhappiness

4. **Unit Supply**
   - Test: Supply cap calculated correctly (handicap + cities + population)
   - Test: Production penalty applies to all cities
   - Test: Growth penalty applies to all cities (VP only)
   - Test: War weariness reduces supply cap
   - Test: Free units excluded from count

### Integration Tests Needed

1. **Food Balance**
   - Settle city → verify initial food balance
   - Grow to pop 5 → verify consumption increases correctly
   - Assign specialists → verify era-based consumption
   - Run negative food → verify starvation and unhappiness
   - Build settler → verify food→production conversion

2. **Growth Progression**
   - Happy city → verify bonus growth speed
   - Unhappy city → verify penalty growth speed
   - Empire unhappy → verify empire-wide penalty
   - Units over supply → verify growth penalty
   - WLTKD → verify growth bonus

3. **Supply System**
   - Build units up to supply cap → verify no penalty
   - Build 5 units over → verify -25% production
   - Build 5 units over → verify -25% growth (VP)
   - Go to war → verify war weariness effect
   - Build supply buildings → verify cap increase

### Performance Tests

1. **Food Calculation**
   - Benchmark `getYieldRateTimes100(YIELD_FOOD)` with 20+ cities
   - Verify growth mods calculated once per turn (not per yield query)
   - Profile specialist food cost calculation (era lookup overhead)

2. **Supply Caching**
   - Verify `m_iNumUnitsSuppliedCached` updated correctly
   - Test cache invalidation on city change, unit change
   - Measure cache hit rate in typical game

---

## Conclusion

Vox Populi's food/growth system is **more sophisticated than a traditional health system**:

**Strengths:**
- Food consumption scales with game progress (era-based specialists)
- Multiple feedback loops (happiness affects growth, growth affects happiness)
- Supply cap creates meaningful strategic choices (military vs economy)
- Local happiness rewards city specialization

**Weaknesses:**
- Opaque calculations (many hidden modifiers)
- Harsh cascading failures (starvation → unhappiness → worse growth → more starvation)
- Unit supply penalties unclear to players
- Limited player agency in food optimization

**Highest Priority Fixes:**
1. Add unit supply UI indicators and tooltips
2. Show specialist food cost in tooltips
3. Add growth modifier breakdown to city screen
4. Add starvation warning before it happens
5. Export missing Lua API functions for UI modding

**Medium Priority:**
1. Graduated supply penalty curve (less harsh initially)
2. Dynamic supply cap (scales with era)
3. Food deficit escalation (progressive penalties)
4. Context-aware growth penalties (war exemption)

**Low Priority (Nice to Have):**
1. Happiness-affected food consumption
2. Local food bonus buildings
3. Supply penalty calculator class
4. Food calculation struct for UI

---

**Files Reviewed:**
- [CvCity.cpp](CvCity.cpp) - Food calculation, consumption, growth
- [CvPlayer.cpp](CvPlayer.cpp) - Unit supply, empire-wide penalties
- [CvCityCitizens.cpp](CvCityCitizens.cpp) - Food threshold, growth focus

**Lines of Code Analyzed:** ~1500+ lines across food/growth calculation functions
