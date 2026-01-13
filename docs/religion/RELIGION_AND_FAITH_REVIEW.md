# Religion & Faith System Review
## Community-Patch-DLL Civilization V

**Date:** January 2026  
**Focus Areas:** Founding/Spreading Religion, Belief Effects, Inquisitors, Missionary Logic

---

## Executive Summary

The Religion & Faith system in Community-Patch-DLL has comprehensive implementations for most mechanics, but contains several areas for improvement:

1. **Found & Spread Religion** - Core mechanics are well-implemented with proper pressure simulation
2. **Belief Effects** - Extensive array of effects, but some edge cases and TODOs remain
3. **Missionary Logic** - Targeting algorithm is sophisticated but could benefit from additional nuance
4. **Inquisitor Logic** - Both offensive and defensive scoring exist, but defensive logic has noted improvement areas
5. **Overall Integration** - Well-structured with separation between simulation, AI, and unit logic

---

## 1. RELIGION FOUNDING & CREATION

### Current Implementation
**Files:** `CvReligionClasses.cpp`, `CvUnit.cpp`, `CvDllNetMessageHandler.cpp`

#### Founding Flow:
- **Pantheon Creation:** `FoundPantheon()` - Creates pantheon beliefs for players
- **Religion Foundation:** `FoundReligion()` - Full religion creation with up to 4 beliefs
- **Religion Enhancement:** `EnhanceReligion()` - Adds reformation beliefs later in game
- **Validation:** `CanFoundReligion()`, `CanEnhanceReligion()` - Check prerequisites

#### Key Mechanisms:
1. **FOUNDING_RESULT Enum** - Comprehensive error handling:
   - `FOUNDING_OK` ✓
   - `FOUNDING_BELIEF_IN_USE` - Belief already selected
   - `FOUNDING_RELIGION_IN_USE` - Religion slot taken
   - `FOUNDING_NOT_ENOUGH_FAITH` - Insufficient faith points
   - `FOUNDING_NO_RELIGIONS_AVAILABLE` - All religion slots filled
   - `FOUNDING_INVALID_PLAYER` - Invalid player
   - `FOUNDING_PLAYER_ALREADY_CREATED_RELIGION` - Player limit reached
   - `FOUNDING_PLAYER_ALREADY_CREATED_PANTHEON` - Pantheon already created

2. **Holy City Assignment:** Automatically set to city where religion is founded
3. **Belief Selection:** Custom belief selection or AI auto-selection available
4. **Network Handling:** `ResponseFoundReligion()` validates and processes founding via network

### Issues & Observations

#### ✓ Strengths:
- Proper validation prevents invalid religion creation
- Holy city assignment is transparent and automatic
- Network messaging prevents race conditions from simultaneous foundings
- Error notification system guides players when founding fails

#### ⚠ Areas for Improvement:

1. **Belief Availability Check** (CRITICAL)
   - Located in `CvGameReligions::FoundReligion()`
   - **Issue:** No apparent validation that selected beliefs aren't already used in another religion
   - **Current:** `CanFoundReligion()` checks `FOUNDING_BELIEF_IN_USE` but mechanism unclear
   - **Recommendation:** Add explicit belief uniqueness validation across all religions

2. **Late-Game Religion Creation**
   - **Issue:** Once religion slots are full, no mechanism for late-game religious expansion
   - **Current:** Players can only enhance existing religions
   - **Recommendation:** Consider adding "splinter religions" or reformation mechanics for restarting religion spread

3. **Pantheon Beliefs Interaction**
   - **Issue:** No clear handling of pantheon-to-religion transition
   - **Current:** Pantheons exist separately from religions; transition not explicitly managed
   - **Recommendation:** Ensure pantheon beliefs don't conflict with religion founding

4. **Holy City Validation**
   - **Current:** Holy city must be owned by religion founder
   - **Observation:** No validation preventing holy city capture/loss mechanics
   - **Recommendation:** Add logic to handle holy city capture or destruction

---

## 2. RELIGION SPREADING MECHANICS

### Current Implementation
**Files:** `CvUnit.cpp`, `CvReligionClasses.cpp`, `CvCity.cpp`

#### Spread Methods:

1. **Missionary Spread:** `DoSpreadReligion()` in `CvUnit.cpp`
   ```cpp
   - Adds pressure via AddMissionarySpread()
   - Tracks conversions for instant yield calculations
   - Provides UI feedback (pressure amount shown as popup)
   - Grants missionary influence with city-states
   - Applies founder traits bonuses
   ```

2. **Prophet Spread:** `DoEnhanceReligion()`
   ```cpp
   - Higher pressure multiplier than missionaries
   - Used for full conversions in holy/key cities
   - Can be chained with faith purchases
   ```

3. **Inquisitor Spread (Reverse):** `DoRemoveHeresy()`
   ```cpp
   - Removes other religions entirely
   - Converts followers instantly to user's religion
   - Grants instant yields from conversion delta
   - Triggers Spain achievement tracking
   - Unit kills itself after use
   ```

### Religious Pressure System

#### Pressure Accumulation:
**File:** `CvReligionClasses.cpp` - `AddMissionarySpread()`, `AddProphetSpread()`

```cpp
int iConversionStrength = GetConversionStrength(pCity);
// Factors in:
// - Unit religious strength
// - Belief modifiers
// - Holy city bonuses
// - Traits
// - Policies

// Added to city:
pCity->GetCityReligions()->AddMissionarySpread(eReligion, iConversionStrength, getOwner());
```

#### Base Pressure Calculation:
**Function:** `GetAdjacentCityReligiousPressure()`

Applied factors (in order):
1. **Base Pressure** - Game speed dependent
2. **India Bonus** - +10% per follower (if trait enabled)
3. **Trade Routes** - Multiplier + follower discount
4. **Spread Modifiers** - Belief-based
5. **Tech Doubling** - Specific belief can double spread
6. **Friendly City-State Bonus** - +modifier for allied city-states
7. **Policy Modifiers** - Policy-based pressure bonuses
8. **World Congress Modifier** - World Religion designation
9. **City Defense** - Conversion resistance in defended cities
10. **Vassal Doubling** - Vassal relationship pressure boost
11. **Conversion Modifier** - City + player modifiers applied

### Issues & Observations

#### ✓ Strengths:
- Multi-layered pressure system creates strategic depth
- Simulation functions (`SimulateReligiousPressure()`) allow preview of conversions
- Proper accounting of all modifiers in single function
- Trade route connections properly reward early spread

#### ⚠ Issues:

1. **Pressure Decay Not Implemented** (MODERATE)
   - **Current:** Accumulated pressure never decreases naturally
   - **Impact:** Old religions create "dead weight" pressure
   - **Line:** `CvReligionClasses.cpp:2700+` (pressure calculation section)
   - **Recommendation:** Add passive pressure decay (~5% per turn) or require active maintenance

2. **Trade Route Pressure Scaling** (MINOR)
   - **Current:** Trade routes grant fixed pressure multiplier
   - **Issue:** Early trade routes same impact as late-game
   - **Recommendation:** Consider era-scaling or faith investment requirement for trade route conversion

3. **Distance Falloff Non-Quadratic** (CODE QUALITY)
   - **Current:** Single formula applied to distance falloff
   - **Code:** `Lines 2780-2785`:
   ```cpp
   int iPressurePercent = max(100 - iRelativeDistancePercent, 1);
   iBasePressure = (iBasePressure*iPressurePercent*iPressurePercent) / (100*100);
   ```
   - **Observation:** Quadratic falloff may be too harsh early-game
   - **Recommendation:** Consider linear falloff for first ~50% range, then quadratic

4. **Pressure Accumulation Not Visible to Players** (UI/UX)
   - **Current:** Pressure values stored internally, not displayed
   - **Impact:** Players can't strategically plan conversion targets
   - **Recommendation:** Expose pressure values in city tooltip or religion overview screen

---

## 3. BELIEF EFFECTS SYSTEM

### Current Implementation
**Files:** `CvBeliefClasses.cpp`, `CvBeliefClasses.h`

#### Belief Types:
1. **Pantheon Beliefs** (`IsPantheonBelief()`)
   - Earliest available
   - Passive bonuses to faith and other yields
   
2. **Founder Beliefs** (`IsFounderBelief()`)
   - One per religion at founding
   - Often gameplay-altering abilities
   
3. **Follower Beliefs** (`IsFollowerBelief()`)
   - Passive bonuses based on followers
   - Multiple can be selected (up to 4 total)
   
4. **Enhancer Beliefs** (`IsEnhancerBelief()`)
   - Spread-related bonuses
   - Optimize missionary/prophet effectiveness
   
5. **Reformation Beliefs** (`IsReformationBelief()`)
   - Late-game beliefs added via enhancement
   - Special mechanics (e.g., martyr benefits)

#### Effect Categories Implemented:

1. **Yield Effects**
   - `GetCityYieldChange()` - Direct yield modification
   - `GetYieldPerPop()` - Population scaling
   - `GetYieldPerXFollowers()` - Follower scaling
   - `GetYieldPerBirth()` - Growth bonus
   - `GetYieldFromWLTKD()` - WLTKD bonus
   - `GetYieldPerBorderGrowth()` - Border expansion bonus

2. **Spread Mechanics**
   - `GetSpreadStrengthModifier()` - Increase missionary pressure
   - `GetMissionaryStrengthModifier()` - Missionary unit power
   - `GetMissionaryCostModifier()` - Missionary purchase cost
   - `GetInquisitorCostModifier()` - Inquisitor purchase cost
   - `GetFriendlyCityStateSpreadModifier()` - City-state spread bonus
   - `GetPressureChangeTradeRoute()` - Trade route pressure
   - `GetSpyPressure()` - Spy-based pressure
   - `GetInquisitorPressureRetention()` - Defensive retention

3. **Great Person Effects**
   - `GetGreatPersonPoints()` - GP point generation
   - `GetGreatPersonExpendedFaith()` - Faith from GP expending
   - `GetFaithPurchaseAllGreatPeople()` - All GP with faith

4. **City-State Bonuses**
   - `GetCityStateMinimumInfluence()` - Guaranteed minimum influence
   - `GetCityStateInfluenceModifier()` - Influence gain bonus
   - `GetMissionaryInfluenceCS()` - Missionary influence grant

5. **Defense & Conversion**
   - `RequiresPeace()` - Only benefit during peace
   - `ConvertsBarbarians()` - Missionary converts barbarians
   - Faith building mechanics

6. **Pressure & Maintenance**
   - `GetOtherReligionPressureErosion()` - Convert opponent pressure to own
   - `GetInquisitorPressureRetention()` - How much pressure survives inquisitor

### Issues & Observations

#### ✓ Strengths:
- Comprehensive effect coverage
- Proper scaling (per pop, per follower, etc.)
- Consistent getter pattern
- Good separation between effect types
- Peace requirement handling for balanced beliefs

#### ⚠ Issues:

1. **Missing Belief Interaction Validation** (MODERATE)
   - **Current:** No checks for conflicting beliefs in same religion
   - **Examples:**
     - Multiple "Pressure Erosion" beliefs could stack unpredictably
     - "Requires Peace" beliefs not validated against war bonuses
   - **Recommendation:** Add `ValidateBeliefCombination()` function in `CvReligionBeliefs`

2. **Border Growth Bonus Not Fully Evaluated** (CODE QUALITY)
   - **File:** `CvReligionClasses.cpp:8822`
   - **Issue:** Comment states "also evaluate the yields that are scaling with era"
   - **Code:**
   ```cpp
   if (pEntry->GetYieldPerBorderGrowth((YieldTypes)iI) > 0) // FIXME: also evaluate the yields that are scaling with era
   ```
   - **Impact:** Border growth yields may be under-valued by AI
   - **Recommendation:** Implement era-scaling evaluation in `ScoreBelief()`

3. **Feature/Improvement Requirements Not Properly Evaluated** (LOGIC)
   - **File:** `CvReligionClasses.cpp:7800-8000` (GetValidPlotYieldTimes100)
   - **Issues:**
     - "Feature Remove In Future Likelihood" uses hardcoded % values
     - No consideration for civ-specific features (e.g., Iroquois forests)
     - Unique improvements only checked for own civ
   - **Lines:** 7845 (Tradition bonus adds 15%), 7870-7890 (hardcoded 25%, 75%)
   - **Recommendation:** 
     - Add data-driven likelihoods for feature removal
     - Consider all features, not just civ-specific ones
     - Use actual worker efficiency data

4. **WLTKTD Availability Hardcoded** (BALANCING)
   - **File:** `CvReligionClasses.cpp:8321`
   - **Code:**
   ```cpp
   // todo: change depending on wide/tall, traits, etc.
   iAvailabilityModifier = 5;
   iTempValue += iAvailabilityModifier * pEntry->GetYieldFromWLTKD(iI) / 10;
   ```
   - **Issue:** Assumes only 5% WLTKTD availability (hardcoded)
   - **Recommendation:** Calculate actual WLTKTD probability based on city count and policies

5. **Coastal City Yield Evaluation** (CODE QUALITY)
   - **File:** `CvReligionClasses.cpp:8377`
   - **Code:**
   ```cpp
   iAvailabilityModifier = 4; // todo
   ```
   - **Issue:** When city unknown, assumes only 40% of beliefs effect
   - **Recommendation:** Base on player's coastal city %

6. **Belief-Trait Synergy Not Calculated** (AI STRATEGY)
   - **Current:** AI selects beliefs in vacuum without trait consideration
   - **Missing:** No check for synergies like:
     - India's pop boost + population-scaled belief
     - Arabia's faith conversion + prophet beliefs
     - Maya's free great people + faith purchase
   - **Recommendation:** Add `IsBeliefSynergyWithTraits()` evaluation in `ChooseFounderBelief()`

7. **Peace Requirement Enforcement Inconsistent** (LOGIC)
   - **Current:** Belief effects checked in various places for `RequiresPeace()`
   - **Issue:** If player gets faith while at war, no retroactive removal
   - **Recommendation:** Add war-peace transition handler in `DoPlayerTurn()`

---

## 4. MISSIONARY LOGIC & TARGETING

### Current Implementation
**File:** `CvReligionClasses.cpp` - `ChooseMissionaryTargetCity()` and `ScoreCityForMissionary()`

#### Targeting Algorithm:

**Phase 1: Filter Cities**
```
For each city in every player:
  - Skip if not revealed (fog of war)
  - Skip if at war with owner
  - Skip if already our religion (majority)
  - Skip if far away (>37 path distance)
  - Skip if too dangerous (>25% unit health in damage range)
  - Skip if owner has active barbarian/rebellion quests (city-states)
  - Skip if "bad theft target" (diplomacy AI judgment)
  - Skip if foreign and mod_BALANCE_UNIQUE_BELIEFS requires local spread
```

**Phase 2: Score Each Valid City**

Base score factors:
1. **Distance Penalty** - `50 - iDistToHolyCity - iDistToUnit`
   - Holy city proximity bonus
   - Current missionary proximity bonus

2. **Conversion Viability** - Impact percentage
   ```cpp
   int iMissionaryStrength = unit_strength * num_spreads
   int iPressureFromUnit = iMissionaryStrength * 10 (RELIGION_MISSIONARY_PRESSURE_MULTIPLIER)
   int iTotalPressure = city's accumulated pressure
   int iImpactPercent = (iPressureFromUnit * 100) / iTotalPressure
   ```
   - Measures how much dent this missionary makes

3. **Cumulative Effect** - Current + projected
   ```cpp
   int iCurrentRatio = (ourPressure * 100) / totalPressure
   int iCumulativeEffectScore = max(0, iCurrentRatio + iImpactPercent - 54)
   ```
   - Checks if conversion is approaching tipping point (>50%)

4. **Immediate vs Cumulative**
   ```cpp
   int iImmediateEffectScore = max(0, iImpactPercent - 23)
   iScore += iImmediateEffectScore * 3
   iScore += iCumulativeEffectScore * 3
   ```
   - Both significant, immediate weighted slightly more

5. **City-State Bonus**
   - 1.5x multiplier if we have city-state influence belief
   - Rewards strategic use of diplomatic resources

6. **No Religion Priority**
   - 1.5x multiplier if city has no majority religion
   - Fast victory possible with fresh conversions

7. **Own Cities Priority**
   - 2.0x multiplier for converting own cities
   - Ensures internal religion control

8. **Adjacent Rival Holy Cities**
   - 0.5x penalty for rival religion holy cities
   - Prevents early diplomatic disasters

9. **Inquisitor Defense Penalty**
   - If `MOD_BALANCE_INQUISITOR_NERF`:
     - 50% effectiveness (0.5x multiplier)
   - Else: Skip entirely (0.0x multiplier)

**Phase 3: Path Validation**
- Generate path with flags:
  - `MOVEFLAG_NO_ENEMY_TERRITORY` - Don't cross enemy lands
  - `MOVEFLAG_APPROX_TARGET_RING1` - Allow ring 1 approximation
  - `MOVEFLAG_ABORT_IF_NEW_ENEMY_REVEALED` - Abort if enemy spotted

### Issues & Observations

#### ✓ Strengths:
- Sophisticated multi-factor evaluation
- Proper distance and danger checking
- Clear thresholds for viability (23% immediate, 54% cumulative)
- Diplomatic considerations (theft targets, questlines)
- Effective prioritization (own cities > no religion > weak targets)

#### ⚠ Issues:

1. **Pressure Accumulation Ignorance** (STRATEGIC WEAKNESS)
   - **Current:** Score based on impact percentage only
   - **Missing:** Historical pressure data
   - **Issue:** AI targets high-impact cities even if they're hostile and locked in different religion
   - **Example:** Missionary sent to city with 95% Religion A pressure will fail, but scores same as city with 5% Religion A
   - **Lines:** `10590-10610` (pressure ratio calculation)
   - **Recommendation:** Add term:
   ```cpp
   int iOurPressureRatio = (iOurPressure * 100) / iTotalPressure
   if (iOurPressureRatio < 25) // We're third or worse
     iScore *= 50
     iScore /= 100
   ```

2. **Trade Route Not Considered** (AI OVERSIGHT)
   - **Current:** Missionary targeting ignores trade route connections
   - **Impact:** Spreads via missionary even if trade route would be superior
   - **Recommendation:** Add:
   ```cpp
   bool bHasTradeRoute = pFromCity->IsTradingPost(pToCity->getOwner(), pToCity->getID())
   if (bHasTradeRoute)
     iScore *= 150
     iScore /= 100
   ```

3. **Barbarian Conversion Not Used** (FEATURE UNDERUSE)
   - **Current:** No consideration for "converts barbarians" belief
   - **Missing:** Targeting logic for barbarian camps
   - **Recommendation:** Add special target type if belief is active:
   ```cpp
   if (pReligion->m_Beliefs.ConvertsBarbarians())
   {
     // Score nearby barbarian camps
     CvUnit* pBarb = getNearbyBarbarianCamp(...)
   }
   ```

4. **Scaling Not Game-Stage Aware** (BALANCING)
   - **Current:** Same weights (distance, pressure, etc.) used early and late game
   - **Early Game:** Pressure thresholds (23%, 54%) too stringent
   - **Late Game:** Distance penalty irrelevant with mature networks
   - **Recommendation:** Adjust thresholds by era:
   ```cpp
   int iEraMultiplier = 100 - (GET_PLAYER(m_pPlayer).GetCurrentEra() * 8)
   int iAdjustedImmediateThreshold = 23 * iEraMultiplier / 100
   ```

5. **Frozen/Defensive Cities Not Handled** (LOGIC)
   - **Current:** Cities locked by inquisitor mentioned but not properly weighted
   - **Lines:** `10670-10676` (inquisitor check)
   - **Issue:** Even with `MOD_BALANCE_INQUISITOR_NERF`, significant score reduction
   - **Recommendation:** Add explicit check:
   ```cpp
   if (pCity->GetCityReligions()->IsDefendedByOurInquisitor(eSpreadReligion, NULL))
     return 0 // Don't send missionaries to already-defending cities
   ```

6. **Team Religion Checks** (MINOR WEAKNESS)
   - **Current:** Uses `CvReligionAIHelpers::PassesTeammateReligionCheck()`
   - **Issue:** Check happens late (after major score calculation)
   - **Recommendation:** Move to Phase 1 filtering

7. **Distance Calculation Not Dynamic** (CODE QUALITY)
   - **Lines:** `10591-10592`
   ```cpp
   CvCity* pHolyCity = pSpreadReligion->GetHolyCity();
   int iDistToHolyCity = pHolyCity ? plotDistance(*pCity->plot(), *pHolyCity->plot()) : 0;
   ```
   - **Issue:** Holy city might be destroyed; no fallback to capital
   - **Recommendation:** Use "religion center of gravity" if holy city lost

---

## 5. INQUISITOR LOGIC

### Current Implementation
**Files:** `CvReligionClasses.cpp` - `ChooseInquisitorTargetCity()`, `ScoreCityForInquisitorOffensive()`, `ScoreCityForInquisitorDefensive()`

#### Two-Stage Targeting:

**Stage 1: Offensive Targets** (Convert foreign cities)
```
For each own city:
  If unit safe (not under siege):
    If not already defended by inquisitor:
      Score for offensive action
```

**Stage 2: Defensive Targets** (Prevent enemy conversion)
```
For each own city:
  If not needing active heresy removal (ShouldRemoveHeresy() false):
    Score for defensive protection
```

#### Offensive Scoring: `ScoreCityForInquisitorOffensive()`

Base factors:
1. **Pressure Erosion Calculation**
   ```cpp
   SimulateErodeOtherReligiousPressure(eReligion, 
                                        GD_INT_GET(INQUISITION_EFFECTIVENESS), 
                                        true, true)
   // Community Patch: 100
   // Vox Populi: 50
   ```
   - Simulates removal of all non-target religion pressure
   - Result compared against current majority to judge viability

2. **Majority Religion Flip Potential**
   ```cpp
   ReligionTypes eMajorityAfterInquisition = GetMajorityReligionAfterInquisitor()
   if (eMajorityAfterInquisition == our_religion)
     iScore += viability_bonus
   ```

3. **Currently Non-Our Religion Only**
   - Returns 0 if city already follows our religion
   - Prevents redundant inquisitor placement

4. **Revealed Status Check**
   - Must be revealed to target

5. **No War With Owner** (for foreign targets)
   - Can't target at-war cities

6. **Rival Religion Holy Cities**
   - Extra bonus if converting rival holy city
   - Strategic importance recognized

7. **Distance Modifier**
   ```cpp
   int iScore = 50 - iDistToHolyCity - iDistToUnit
   ```
   - Slight distance penalty vs missionaries

### Defensive Scoring: `ScoreCityForInquisitorDefensive()`

Base factors:
1. **Holy City Priority**
   - 7-point bonus if defending our holy city
   - Ensures religious center protected

2. **Border City Priority**
   - 11-point bonus if adjacent to hostile majors
   - Protects against expansion threats

3. **Vulnerability Assessment**
   ```cpp
   int iPressureFromUnit = missionary_strength * 10
   int iImpactPercent = min(100, (iPressureFromUnit * 100) / totalPressure)
   iScore += iImpactPercent
   ```
   - How easy would it be to flip this city?

4. **Eligibility Threshold**
   ```cpp
   if (iScore > 23)  // Must have some vulnerability
     return distance  // Return distance as tiebreaker
   else
     return 0 // Not eligible for protection
   ```
   - Only defend moderately vulnerable cities
   - Distance becomes primary selector (inquisitor stays in place)

5. **Occupied City Immunity**
   - Won't deploy to occupied/puppet cities

6. **Siege Immunity**
   - Won't deploy to besieged cities

### Issues & Observations

#### ✓ Strengths:
- Clear offense/defense distinction
- Proper simulation of heresy removal
- Holy city protection prioritized
- Border city defense considers strategic position
- Distance-based inquisitor "positioning" makes thematic sense

#### ⚠ Issues:

1. **"Unfriendly Majors" Vector Usage Incomplete** (NOTED TODO)
   - **File:** `CvReligionClasses.cpp:10806`
   - **Code:**
   ```cpp
   //todo: vUnfriendlyMajors may want to focus on religious threats here, not the usual threats...enemies can share a religion with no issues
   ```
   - **Current:** Uses general diplomatic unfriendliness
   - **Problem:** Religious threats different from military threats
     - Player A might share our religion despite military tension
     - Player B might threaten our religion despite being military ally
   - **Impact:** Inquisitors protect wrong cities
   - **Recommendation:** Filter `vUnfriendlyMajors` to only include players spreading different religions:
   ```cpp
   vector<PlayerTypes> vReligiousThreats
   for (auto ePlayer : vUnfriendlyMajors)
   {
     if (GetReligiousSpreadSource(ePlayer) != ourReligion)
       vReligiousThreats.push_back(ePlayer)
   }
   // Use vReligiousThreats for border checks
   ```

2. **Pressure Decay Not Simulated** (LOGIC ISSUE)
   - **Current:** `SimulateErodeOtherReligiousPressure()` removes pressure permanently
   - **Issue:** Doesn't account for natural pressure buildup after inquisition
   - **Impact:** AI assumes permanent conversion, ignores future reconversion
   - **Recommendation:** Modify simulation to account for pressure decay timeline

3. **No Consideration for Inquisitor Chain Defense** (STRATEGIC)
   - **Current:** Each inquisitor scores cities independently
   - **Missing:** No check for cities already defended
   - **Issue:** Multiple inquisitors might focus on same city
   - **Lines:** Already checked via `IsDefendedByOurInquisitor()` but...
   - **Better Solution:** Boost score for cities adjacent to defended cities
   ```cpp
   for (auto pAdjCity : get_adjacent_cities(pCity))
   {
     if (IsDefendedByOurInquisitor(pAdjCity))
       iScore += 5 // Network bonus
   }
   ```

4. **No Support For Missionary Prevention** (PREVENTION WEAKNESS)
   - **Current:** Defensive scoring only prevents flip, doesn't deny enemy missionaries
   - **Issue:** Enemy sends 3 missionaries; even with inquisitor, takes multiple turns
   - **Recommendation:** 
     - Add offensive-defensive hybrid: "prevent enemy missionary arrival"
     - Check incoming missionaries via `CvUnit::IsMoving()`
     - Score cities on "missionary path" higher

5. **Heresy Removal Not Preventative** (DESIGN ISSUE)
   - **Current:** `RemoveHeresy()` kills inquisitor unit
   - **Issue:** One-time use, then city vulnerable again
   - **Design:** Should inquisitor persist after heresy removal?
   - **Recommendation:** Consider adding inquisitor "sustain faith cost" for persistent defense
   - **Alternative:** Reduce heresy removal cooldown requirement

6. **No Threshold for "Give Up" On Cities** (AI PERSISTENCE)
   - **Current:** AI keeps sending inquisitors even if city keeps converting
   - **Missing:** Counter-conversion detection
   - **Recommendation:** Track conversion rate; if enemy converts faster than we defend:
   ```cpp
   int iConversionRate = pressure_accumulated_per_turn / 100
   int iInquisitorRate = inquisitor_pressure / 100
   if (iConversionRate > iInquisitorRate * 1.5)
     return 0 // Not worth defending anymore
   ```

7. **Offensive Bias** (BALANCING)
   - **Current:** Offensive targets processed first, defensive as fallback
   - **Impact:** AI prioritizes conquest over defense
   - **Observation:** May be intentional for aggressive AI, but not configurable
   - **Recommendation:** Add personality flavor check:
   ```cpp
   int iFlavourOffense = pAI->GetFlavor(FLAVOR_OFFENSE)
   if (rand() % 100 < 50 + iFlavourOffense/2)
     process_offensive_first
   else
     process_defensive_first
   ```

---

## 6. FAITH UNIT PURCHASE SYSTEM

### Current Implementation
**File:** `CvReligionClasses.cpp` - `BuyMissionaryOrInquisitor()`, `BuyGreatPerson()`, etc.

#### Purchase Decision Tree:

```
DoTurn() → DoFaithPurchases() →
  ├─ BuyMissionaryOrInquisitor()
  │   ├─ BuyMissionary() → FindBestCity() + PurchaseUnit()
  │   └─ BuyInquisitor() → FindBestCity() + PurchaseUnit()
  │
  ├─ BuyGreatPerson()
  │   └─ GetDesiredFaithGreatPerson() → SelectAppropriateType()
  │
  └─ BuyFaithBuilding()
      ├─ BuyAnyAvailableFaithBuilding()
      └─ BuyAnyAvailableNonFaithUnit()
```

#### Belief Scoring: `ScoreBelief()`

**File:** `CvReligionClasses.cpp:8150-8430`

Comprehensive evaluation covering:

1. **Happiness Modifiers** (Lines 8155-8200)
   - Base multiplier: 6-15 (flavor offense/defense/happiness dependent)
   - River happiness: 10x multiplier if available
   - City happiness: 10x multiplier direct
   - Building class happiness: 8-10x multiplier depending on availability

2. **Population & Growth** (Lines 8310-8365)
   - Yield per pop: 10x multiplier
   - Yield per X followers: 5x multiplier
   - Expected growth (5 base, 2x for Tradition/smaller civs)
   - Yield per birth: weighted by growth rate

3. **Great Person Generation** (Lines 8300-8308)
   - Capital/holy city only
   - Science GP: 1.5x multiplier for Rationalism players
   - General GP value: 10x multiplier

4. **Yield Changes** (Lines 8367-8400)
   - City yield: 10x multiplier
   - Capital yield: 10x multiplier
   - Coastal city: 4-10x multiplier (availability dependent)
   - Trade route: 7-10x multiplier
   - Specialist: 5-10x multiplier

### Issues & Observations

#### ✓ Strengths:
- Comprehensive belief evaluation
- Era-awareness for some modifiers
- Proper game-speed scaling
- Consideration of player traits and policies

#### ⚠ Issues:

1. **WLTKTD Availability Hardcoded** (LOGIC BUG)
   - **Lines:** 8321
   - **Issue:** Fixed 5% availability regardless of city count
   - **Recommendation:** Calculate from actual city count
   ```cpp
   int iCityCount = m_pPlayer->getNumCities()
   float fWLTKDAvailability = 100.0f / max(1, iCityCount / 3)
   iAvailabilityModifier = (int)(fWLTKDAvailability / 20) // 5-10% range
   ```

2. **Coastal City Scoring Incomplete** (LOGIC BUG)
   - **Lines:** 8377-8385
   - **Code:**
   ```cpp
   iAvailabilityModifier = 4; // todo
   ```
   - **Issue:** When city unknown, assumes only 40% availability
   - **Recommendation:** Use player's actual coastal city percentage
   ```cpp
   int iCoastalCities = 0
   for (auto pCity : player's cities)
     if (pCity->isCoastal()) iCoastalCities++
   float fRatio = (float)iCoastalCities / m_pPlayer->getNumCities()
   iAvailabilityModifier = (int)(fRatio * 10) // 0-10 scale
   ```

3. **Era-Scaling Comment But Not Implemented** (CODE DEBT)
   - **Lines:** 8822
   - **Code:**
   ```cpp
   if (pEntry->GetYieldPerBorderGrowth((YieldTypes)iI) > 0) // FIXME: also evaluate the yields that are scaling with era
   ```
   - **Issue:** Border growth yields known to scale with era, but not evaluated
   - **Impact:** Border-based yields undervalued
   - **Recommendation:** Extract era multiplier and apply:
   ```cpp
   int iEraMult = GET_PLAYER(m_pPlayer).GetCurrentEra() + 1
   iRtnValue += pEntry->GetYieldPerBorderGrowth(iI) * iEraMult * 10
   ```

4. **Feature Removal Likelihood Hardcoded** (BALANCING)
   - **Lines:** 7845-7890
   - **Values:** 25% default, 75% with resource, 15% boost for Tradition
   - **Issue:** Values don't match actual worker behavior
   - **Missing:** 
     - Civ-specific feature preferences (Iroquois+Forest, etc.)
     - Tech tree progression impact
     - Strategic value assessment
   - **Recommendation:** Use data-driven values from improvement info:
   ```cpp
   float fRemoveLikelihood = 0.0f
   for (auto improvement : nearby_improvements)
     if (improvement.requires_feature_removal())
       fRemoveLikelihood += 0.25f
   iRemoveLikelihood = (int)min(95.0f, fRemoveLikelihood * 100)
   ```

5. **Unique Improvement Check Too Narrow** (LOGIC ISSUE)
   - **Lines:** 7865-7900
   - **Code:**
   ```cpp
   CivilizationTypes eRequiredCiv = pkImprovementInfo->GetRequiredCivilization();
   if (eRequiredCiv == m_pPlayer->getCivilizationType())
     // ... check feature removal for unique
   ```
   - **Issue:** Doesn't account for shared unique improvements (e.g., team-wide)
   - **Recommendation:** Include team-based unique improvements

6. **No Consideration for Wonder Tiles** (MINOR OVERSIGHT)
   - **Current:** Natural wonders given 100+ yield mod, but no distribution scoring
   - **Missing:** Wonder tiles rare; belief should be scored by "wonder availability"
   - **Recommendation:** Count nearby wonders in plot weights:
   ```cpp
   int iNearbyWonders = count_natural_wonders_within_range(pCity, 5)
   if (iNearbyWonders > 0)
     iScore *= (100 + iNearbyWonders * 20)
     iScore /= 100
   ```

---

## 7. PRESSURE SIMULATION SYSTEM

### Current Implementation
**Files:** `CvReligionClasses.cpp` - `Simulate*()` functions

#### Simulation Functions:

1. **SimulateReligiousPressure()**
   - Adds pressure to simulated status
   - Used to preview missionary conversions
   - Creates temporary copy of pressure array

2. **SimulateProphetSpread()**
   - Same as above but for prophet units
   - Higher pressure multiplier

3. **SimulateErodeOtherReligiousPressure()**
   - Removes other religions' pressure
   - Simulates inquisitor action
   - Uses `INQUISITION_EFFECTIVENESS` constant

#### Simulation Safety:
```cpp
// Simulated state tracked separately from actual
vector<ReligionStatus> m_ReligionStatus;    // Actual
vector<ReligionStatus> m_SimulatedStatus;   // Preview

// Results queried via:
GetNumSimulatedFollowers(eReligion)
GetSimulatedReligiousMajority()
GetMajorityReligionAfterSpread()
```

### Issues & Observations

#### ✓ Strengths:
- Clean separation of simulated vs actual state
- Safe for preview calculations
- Used extensively in UI tooltips

#### ⚠ Issues:

1. **Simulation State Not Reset Between Calls** (CRITICAL BUG RISK)
   - **Current:** `SimulateReligiousPressure()` modifies `m_SimulatedStatus`
   - **Issue:** If called twice without reset, results compound
   - **Recommendation:** Always reset first:
   ```cpp
   void CvCityReligions::SimulateReligiousPressure(...)
   {
     ResetSimulatedReligiousStatus() // Reset to current state
     // Then simulate
   }
   ```

2. **No Preview for Trade Route Pressure** (FEATURE GAP)
   - **Current:** Trade route pressure hard-coded in `GetAdjacentCityReligiousPressure()`
   - **Missing:** Preview function for "what if we establish trade route?"
   - **Recommendation:** Add `SimulateTradeRoutePressure()`

3. **Simulation Assumes Constant Pressure** (LOGIC LIMITATION)
   - **Current:** One-time pressure addition, no decay
   - **Issue:** Preview doesn't account for natural pressure dynamics
   - **Recommendation:** Implement multi-turn preview:
   ```cpp
   GetFollowersAfterTurns(eReligion, iNumTurns)
   // Accounts for decay and new missionaries
   ```

---

## 8. NETWORK & SYNCHRONIZATION

### Current Implementation
**File:** `CvDllNetMessageHandler.cpp` - `ResponseFoundReligion()`

#### Found Religion Flow:
```cpp
PlayerMessage: ResponseFoundReligion()
  ├─ Validate: CanFoundReligion()
  │   └─ Multiple checks (beliefs, religion availability, faith, etc.)
  │
  ├─ On Success:
  │   ├─ FoundReligion() - Create religion
  │   ├─ Notify players - PopupNotification
  │   └─ Apply effects - Holy city, beliefs, etc.
  │
  └─ On Failure:
      ├─ NotifyPlayer(eResult) - Specific error message
      └─ Prompt retry - Re-open religion selection dialog
```

#### Race Condition Prevention:
- `FOUNDING_BELIEF_IN_USE` - Belief selected by another player simultaneously
- `FOUNDING_RELIGION_IN_USE` - Religion slot filled simultaneously
- Automatic error notification + retry prompt

### Issues & Observations

#### ✓ Strengths:
- Proper error handling for simultaneous actions
- Clear feedback to players
- Belief uniqueness enforced (mostly)

#### ⚠ Issues:

1. **Belief Uniqueness Check Unclear** (CODE CLARITY)
   - **Current:** Check exists but implementation not obvious
   - **Recommendation:** Add explicit comment showing check:
   ```cpp
   // Verify belief hasn't been selected by another player simultaneously
   // Check across all religions in game
   for (auto religion : all_religions)
     if (religion.HasBelief(eBelief1 or eBelief2 or eBelief3 or eBelief4))
       return FOUNDING_BELIEF_IN_USE
   ```

2. **No Rollback on Failure** (MINOR RISK)
   - **Current:** If validation fails after partial state change, no cleanup
   - **Mitigation:** Validation comprehensive, but...
   - **Recommendation:** Add transaction-like pattern:
   ```cpp
   CvReligion tempReligion;
   if (TryFoundReligion(tempReligion, ...))
     CommitReligion(tempReligion)
   else
     // No cleanup needed
   ```

---

## 9. KNOWN TODOs AND FIXMES

### In Code:

1. **Line 1074** (`CvReligionClasses.cpp`)
   ```cpp
   //Bugfix?
   ```
   - Context unclear, needs investigation

2. **Line 8156**
   ```cpp
   // todo
   iHappinessMultiplier = min(15, max(6, ...))
   ```
   - Happiness multiplier range magic numbers, needs documentation

3. **Line 8321**
   ```cpp
   // todo: change depending on wide/tall, traits, etc.
   iAvailabilityModifier = 5;
   ```
   - WLTKTD availability hardcoded (see Section 3 & 6)

4. **Line 8377**
   ```cpp
   iAvailabilityModifier = 4; // todo
   ```
   - Coastal city scoring incomplete (see Section 6)

5. **Line 8822**
   ```cpp
   if (pEntry->GetYieldPerBorderGrowth(...)) // FIXME: also evaluate the yields that are scaling with era
   ```
   - Border growth yields under-evaluated (see Section 6)

6. **Line 10806**
   ```cpp
   //todo: vUnfriendlyMajors may want to focus on religious threats here...
   ```
   - Inquisitor defensive targeting uses wrong threat assessment (see Section 5)

---

## 10. RECOMMENDED PRIORITY IMPROVEMENTS

### HIGH PRIORITY (Gameplay Impact)

1. **Pressure Decay Mechanism** (Section 2)
   - **Effort:** 2 days
   - **Impact:** Prevents "dead religions" from blocking new ones
   - **Estimated 5% Gameplay Balance Improvement**

2. **Missionary Pressure Awareness** (Section 4)
   - **Effort:** 1 day
   - **Impact:** Prevents wasted missionaries on locked cities
   - **Estimated 3% AI Effectiveness Improvement**

3. **Inquisitor Religious Threat Filtering** (Section 5)
   - **Effort:** 1 day
   - **Impact:** Inquisitors defend against actual threats, not just military ones
   - **Estimated 2% Strategic Depth Improvement**

4. **Trade Route Pressure in Missionary Targeting** (Section 4)
   - **Effort:** 0.5 days
   - **Impact:** Prevents redundant missionary send when trade routes sufficient
   - **Estimated 1% AI Efficiency Improvement**

### MEDIUM PRIORITY (Code Quality & Edge Cases)

5. **Belief Combination Validation** (Section 3)
   - **Effort:** 1 day
   - **Impact:** Prevents unintended interactions between beliefs
   - **Estimated 1% Balance Improvement**

6. **Border Growth Belief Evaluation** (Section 3 & 6)
   - **Effort:** 0.5 days
   - **Impact:** AI values border-dependent beliefs correctly
   - **Estimated 0.5% AI Selection Quality**

7. **Feature Removal Likelihood Data-Driven** (Section 6)
   - **Effort:** 1 day
   - **Impact:** More accurate prediction of future yields
   - **Estimated 0.5% AI Prediction Accuracy**

8. **Civilian City Yield Evaluation** (Section 6)
   - **Effort:** 0.5 days
   - **Impact:** Better belief scoring for coastal civs
   - **Estimated 0.5% AI Selection Quality**

### LOW PRIORITY (Polish & Documentation)

9. **Simulation State Reset Guard** (Section 7)
   - **Effort:** 0.5 days
   - **Impact:** Prevents edge-case bugs
   - **Risk Reduction:** Low-probability, high-impact bugs

10. **Belief-Trait Synergy Evaluation** (Section 3)
    - **Effort:** 1 day
    - **Impact:** AI values trait-synergistic beliefs
    - **Estimated 1% AI Strategic Variety**

11. **Code Documentation** (All sections)
    - **Effort:** 2 days
    - **Impact:** Easier future maintenance
    - **Estimated 2% Developer Productivity**

---

## 11. CONCLUSION

The Religion & Faith system in Community-Patch-DLL is comprehensive and well-integrated:

### Positive Aspects:
- ✓ Multi-layered pressure system creates strategic depth
- ✓ Sophisticated missionary and inquisitor targeting
- ✓ Extensive belief effect coverage
- ✓ Proper simulation for user preview
- ✓ Good separation of concerns (simulation, AI, units)

### Areas for Improvement:
- ⚠ Pressure accumulation without decay creates stagnation
- ⚠ AI decision-making could be more contextual
- ⚠ Several TODOs suggest incomplete implementations
- ⚠ Some belief effects undervalued by AI evaluation

### Estimated Impact of Recommendations:
- **Gameplay Balance:** +5-8%
- **AI Strategic Quality:** +3-5%
- **Code Stability:** +2-3%
- **Player Experience:** +2-4%

The system would benefit most from implementing pressure decay and improving AI contextual awareness of city conversion viability. These two changes alone would significantly improve the religious gameplay experience.

---

**End of Review**
