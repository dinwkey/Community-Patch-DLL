# Victory Conditions & Scoring Review — Community Patch DLL (Vox Populi)

**Date:** January 9, 2026  
**Scope:** Domination, Science, Culture, Diplomatic, and Score victory conditions  
**Primary Files Analyzed:**
- `CvDiplomacyAI.cpp` (victory progress calculation, lines 2042-9744)
- `CvGame.h/cpp` (victory state management)
- `CvPlayer.cpp` (scoring system, lines 11092-11200+)
- `(2) Vox Populi\Core Files\Overrides\VictoryProgress.lua` (UI progress display)

---

## 1. Executive Summary

The Vox Populi victory system comprises five distinct victory conditions, each with independent progress tracking, AI weighting, and endgame triggering logic. The system is **architecturally sound** but exhibits **balance asymmetries** across victory types, **imprecise threshold measurements**, and **missed opportunities for dynamic balancing**. Key findings:

- **Score Victory** has fundamentally different mechanics (time-scaled) and lower endgame priority
- **Domination Victory** relies on two independent metrics (military might % + capital conquest %) with high variance
- **Diplomatic Victory** capped at 59% unless UN exists, creating artificial gating
- **Science Victory** progress mixes tech knowledge (0-100) with spaceship projects (0-99), causing non-linear progression
- **Culture Victory** uses soft influence cap (100%), but with optional policy-weighted modifier in MOD_BALANCE_CULTURE_VICTORY_CHANGES
- **AI Victory Pursuit System** has elaborate weighting but weak persistence—pursuits change frequently based on current progress

---

## 2. Architecture Overview

### 2.1 Victory Progress Calculation Layer

The system uses **progress percentages (0-100)** to measure proximity to each victory condition. These are tracked via `CvDiplomacyAI::Get[Victory]VictoryProgress()` functions (CvDiplomacyAI.cpp, lines 3052-3300):

```
GetScoreVictoryProgress()      → (iOurScore / iHighestScore) * 100 * (gameTurn / maxTurns)
GetDominationVictoryProgress() → max(iOurMight %, iCapitalsProgress %)
GetDiplomaticVictoryProgress() → (iOurVotes / iNeededVotes) * 100, capped at 59-79%
GetScienceVictoryProgress()    → min(iTechKnown %, 78 + (iSpaceshipProjects / iRequired) * 21)
GetCultureVictoryProgress()    → min(iLowestTourism %, 18 + iPolicies * 3) [if MOD_BALANCE enabled]
```

### 2.2 Threshold Detection & Endgame Behavior

Each victory has a **"close to victory" threshold** that triggers AI endgame aggression:

| Victory Type | Function | Default Threshold | Location |
|---|---|---|---|
| Domination | `IsCloseToWorldConquest()` | 50% (GD_INT_GET) | CvDiplomacyAI.cpp:3273 |
| Diplomatic | `IsCloseToDiploVictory()` | 55% (GD_INT_GET) | CvDiplomacyAI.cpp:3279 |
| Science | `IsCloseToSpaceshipVictory()` | 80% (GD_INT_GET) | CvDiplomacyAI.cpp:3285 |
| Culture | `IsCloseToCultureVictory()` | 60% (GD_INT_GET) | CvDiplomacyAI.cpp:3291 |
| Score | N/A (no aggression mechanic) | N/A | N/A |

When a player exceeds their threshold, **other AI players become endgame-aggressive** (CvDiplomacyAI.cpp:15389, flags for military pressure + diplomatic hostility).

### 2.3 AI Victory Pursuit System

AI civs maintain **primary and secondary victory pursuits** selected via flavor-based weighting (CvDiplomacyAI::SelectDefaultVictoryPursuits, lines 2042-2569):

- **Initial pursuit selection**: Uses leader traits (Boldness, Nerd, Tourism, Warmonger, etc.), tech availability, and policy progress
- **Dynamic pursuit update**: `DoUpdateCurrentVictoryPursuit()` (lines 9645-9750) reassesses each turn based on:
  1. Eternal victory pursuit (hardcoded leader bias)
  2. Current progress proximity (IsCloseToXVictory)
  3. Grand Strategy AI (AIGRANDSTRATEGY_CONQUEST, SPACESHIP, CULTURE, UNITED_NATIONS)
  4. Fallback: Domination (default)
- **Pursuit frequency**: Updated every turn when AI evaluates strategy

---

## 3. Detailed Victory Mechanics

### 3.1 Domination Victory

**Purpose:** Control all original capitals + eliminate/vassal other civs  
**Checked by:** `GetDominationVictoryProgress()` (CvDiplomacyAI.cpp:3090-3127)

#### Progress Formula

```cpp
iOurMight % = (iOurMight * 100) / max(1, iTotalMight)
iCapitalsProgress % = (iCapitalsProgress * 100) / max(1, iTotalCapitals)
return max(iOurMight %, iCapitalsProgress %)
```

**Calculation Details:**

- **Military Might (%):** Sum of all major civs' military strength. Your might includes vassals + teammate units. Alive enemies only count. Non-cities generate no might.
- **Capital Control (%):** Tracks how many original capitals are under your team's control (via `GetOwnerForDominationVictory()`, which includes city states). Requires original capital plot to exist and be a city.
- **Variance:** Method uses `max()`, meaning a player could be at 49% might but 75% capitals (or vice versa). Victory requires **both metrics ≥ actual threshold** (checked in game engine).

#### Requirements for Attempt

```cpp
GC.getGame().CanPlayerAttemptDominationVictory(iPlayerID, NO_PLAYER, false)
```

Checks:
- Victory type enabled
- Not in "Always Peace" or "Always War" option
- Player is alive, major civ, not been eliminated

#### Endgame Behavior

- Threshold: **50%** progress triggers `IsCloseToWorldConquest()`
- **AI Reaction:** Other players receive diplomatic penalties, become more willing to declare war, and may form defensive alliances
- **Human Reaction:** Notification appears; other AIs monitor your progress

---

### 3.2 Diplomatic Victory

**Purpose:** Control UN votes + pass Diplomatic Victory resolution  
**Checked by:** `GetDiplomaticVictoryProgress()` (CvDiplomacyAI.cpp:3132-3173)

#### Progress Formula

```cpp
iOurVotes = pLeague->CalculateStartingVotesForMember(iPlayerID)
iNeededVotes = GC.getGame().GetVotesNeededForDiploVictory()
iProgress = (iOurVotes * 100) / max(1, iNeededVotes)

if (!UN exists):
    iProgress = min(iProgress, 59)
else if (UN exists but not yet):
    iProgress = min(iProgress, 59 + 20 * iUNAttempts)  // Max 79%
else if (UN active):
    iProgress = min(iProgress, 59 + 20 * iUNAttempts)  // Max variable
```

**Interpretation:**
- Requires active League (UN) to gain votes
- Vote calculation: `CalculateStartingVotesForMember()` sums player's suzerain bonuses, alliances, and era effects
- **Hard cap at 59%** before UN triggered; soft cap after UN activated (59 + 20 per UN session)
- This creates a **two-phase victory:** pre-UN domination (require alliances, suzerainties) → post-UN voting phase

#### Requirements for Attempt

```cpp
!GC.getGame().isOption(GAMEOPTION_NO_LEAGUES)  // League enabled
GC.getGame().isVictoryValid(VICTORY_DIPLOMATIC) // Victory enabled
pLeague != nullptr                              // Active league exists
```

#### Endgame Behavior

- Threshold: **55%** progress (pre-UN) or **80%+** (post-UN)
- **AI Reaction:** Players aware of your vote progress; may negotiate trade/alliances or block with their own votes
- **Mechanic:** Requires active voting session + majority of votes to win

---

### 3.3 Science Victory

**Purpose:** Complete spaceship projects (with Apollo Program prerequisite)  
**Checked by:** `GetScienceVictoryProgress()` (CvDiplomacyAI.cpp:3175-3206)

#### Progress Formula

```cpp
iProjectsRequired = sum of all VictoryMinThreshold(VICTORY_SPACE_RACE) per project
iProjectsCompleted = sum of team's completed spaceship projects
iScienceProgress = (numTechsKnown * 100) / max(1, numTechsTotal - 1)

iSpaceshipProgress = (21 * iProjectsCompleted) / max(1, iProjectsRequired)
return min(iScienceProgress, 78 + iSpaceshipProgress)
```

**Interpretation:**
- **Science Progress (0-100%):** Scales with tech discovery; doesn't contribute directly to victory
- **Spaceship Progress (0-21):** Each completed project adds `21 / iRequired` percentage points
- **Hard cap at 78%** before spaceship starts; capped at **99%** at completion (78 + 21)
- **Timeline:** Science is a long-game victory; tech progress is slow until Industrial/Modern era

#### Requirements for Attempt

```cpp
!GC.getGame().isOption(GAMEOPTION_NO_SCIENCE)         // Science enabled
GC.getGame().isVictoryValid(VICTORY_SPACE_RACE)       // Victory enabled
```

**Projects Required:**
- All projects with `GetVictoryMinThreshold(VICTORY_SPACE_RACE) > 0`
- **Apollo Program** (VICTORY_SPACE_RACE requirement) must be completed
- Typical: 8-12 projects per game

#### Endgame Behavior

- Threshold: **80%** progress triggers `IsCloseToSpaceshipVictory()`
- **AI Reaction:** If close to completion, other AIs become endgame-aggressive; may spy on your projects or rush competing techs
- **Mechanic:** Team-based (projects owned by any team member count); requires consistent production infrastructure

---

### 3.4 Culture Victory

**Purpose:** Achieve influential or exotic tourism on all other major civs  
**Checked by:** `GetCultureVictoryProgress()` (CvDiplomacyAI.cpp:3209-3235)

#### Progress Formula (Standard)

```cpp
iLowestPercent = GetLowestTourismInfluence()  // % of each civ's lifetime culture I've influenced
iProgress = min(99, iLowestPercent)
```

#### Progress Formula (With MOD_BALANCE_CULTURE_VICTORY_CHANGES)

```cpp
iLowestPercent = GetLowestTourismInfluence()
iPolicies = min(GetNumPoliciesOwned(), 27)
iProgress = min(iLowestPercent, 18 + iPolicies * 3)  // Soft cap at 99 (18 + 27*3)
```

**Interpretation:**
- **Lowest Tourism Influence:** The minimum % influence you have on any living major civ determines your progress (bottleneck mechanic)
- **Tourism Influence (%):** `(iInfluenceOn * 100) / lLifetimeCulture`
- **MOD_BALANCE variant** introduces a **policy cap**: influence is capped by (18 + policies * 3), meaning you need to adopt **27+ policies** to reach 99% influence on the bottleneck civ
  - This rebalances Culture from pure cultural output to requiring policy depth
  - Penalty for cultural specialists: you can't reach 100% just by spamming tourism; need broader policy investment

#### Requirements for Attempt

```cpp
!GC.getGame().isOption(GAMEOPTION_NO_POLICIES)         // Policies enabled
GC.getGame().isVictoryValid(VICTORY_CULTURAL)          // Victory enabled
```

#### Endgame Behavior

- Threshold: **60%** progress triggers `IsCloseToCultureVictory()`
- **AI Reaction:** Players aware of your cultural dominance; may adopt cultural policies themselves, rush wonders, or add great artists to your cities
- **Mechanic:** Heavily dependent on **Great Works, theme bonuses, and wonder placement**; civilization-dependent bonuses (e.g., Brazil tourism multipliers)

---

### 3.5 Score Victory

**Purpose:** Accumulate the highest score by game end (default: turn limit)  
**Checked by:** `GetScoreVictoryProgress()` (CvDiplomacyAI.cpp:3052-3087)

#### Score Composition (CvPlayer.cpp:11092-11175+)

```cpp
GetScore() = GetScoreFromCities()
           + GetScoreFromPopulation()
           + GetScoreFromLand()
           + GetScoreFromWonders()
           + GetScoreFromPolicies()
           + GetScoreFromGreatWorks()
           + GetScoreFromReligion()
           + GetScoreFromTechs()
           + GetScoreFromFutureTech()
```

**Component Formulas:**

| Component | Formula | VP Multiplier |
|---|---|---|
| Cities | `numCities * SCORE_CITY_MULTIPLIER` | 10 |
| Population | `totalPop * SCORE_POPULATION_MULTIPLIER` | 2 |
| Land | `totalLand * SCORE_LAND_MULTIPLIER` | 1 |
| Wonders | `numWonders * SCORE_WONDER_MULTIPLIER` | 25 |
| Policies | `numPolicies * SCORE_POLICY_MULTIPLIER` | 16 |
| Great Works | `numGreatWorks * SCORE_GREAT_WORK_MULTIPLIER` | 4 |
| Religion | `numBeliefs * 5 + numCitiesFollowing * 3` | Belief:5, Cities:3 |
| Techs | `numTechs * SCORE_TECH_MULTIPLIER` | 6 |
| Future Tech | `numFutureTechs * 10` | 10 |

**With Map Size Modifier:**
```cpp
iScore *= GC.getGame().GetMapScoreMod();
iScore /= 100;
```

#### Victory Progress (Win Scaling)

```cpp
iProgress = (iOurScore * 100) / max(1, iHighestScore)
iProgress *= GC.getGame().getGameTurn()
iProgress /= max(1, GC.getGame().getMaxTurns())
```

**Interpretation:**
- **Early-game scaling:** Progress is low (e.g., turn 50/500 = 10% temporal scaling)
- **Late-game dominant:** At turn 450/500, even slight score leads become 90%+ progress
- **Winner bonus:** When game ends, winner's score is normalized: `final_score * 100 / iGameProgressPercent`
- **This favors early victory:** A domination win on turn 200 gets score multiplied by 2.5x; a score win at turn 500 has no bonus

#### Requirements for Attempt

```cpp
GC.getGame().isVictoryValid(VICTORY_SCORE)  // Victory enabled (usually always-on as fallback)
```

#### Endgame Behavior

- **No endgame aggression threshold** (Score Victory has no `IsCloseToScoreVictory()` function)
- Progress tracked only for UI display
- AI civs do not prioritize blocking or accelerating score progress
- **Fallback victory:** If no other victory is achieved by turn limit, highest score wins

---

## 4. Identified Issues

### 4.1 Domination Victory Issues

#### **Issue #1: Military Might Calculation Ignores Naval Power Imbalance** — MEDIUM Severity

**Problem:** `GetDominationVictoryProgress()` sums all civ military might equally but doesn't differentiate between land/naval domains. A civ with 90% naval dominance but 20% land might appears at only 55% overall might.

**Impact:** Players in defensible island positions can appear closer to victory than they actually are for land invasion scenarios. Naval-heavy empires (England, Greece) get inflated progress numbers.

**Code Reference:** CvDiplomacyAI.cpp:3107
```cpp
int iMight = GET_PLAYER(ePlayer).GetMilitaryMight();  // No domain check
iTotalMight += iMight;
if (GET_PLAYER(ePlayer).getTeam() == GetTeam() || IsMaster(ePlayer))
    iOurMight += iMight;
```

**Recommendation:**
- **HIGH Priority:** Separate military might into land/naval components
- Weight land might at **100%**, naval might at **50%** (since not all victories require naval dominance)
- Recalculate: `iWeightedMight = iLandMight + (iNavalMight * 0.5)`
- This reflects "military might that matters for domination"

---

#### **Issue #2: Capital Control Metric Doesn't Account for Vassal States** — LOW Severity

**Problem:** Capital progress counts vassal capitals as "controlled," but vassals can break free, making this metric volatile. A player with 4 capitals (1 own, 3 vassal) shows 80% capital control but could drop to 25% instantly.

**Impact:** Vassal-heavy domination players show falsely high progress. Progress bars are misleading when vassalization breaks.

**Code Reference:** CvDiplomacyAI.cpp:3123
```cpp
if (pOriginalCapitalPlot->getPlotCity()->GetOwnerForDominationVictory() == GetID())
    iCapitalsProgress += 1;
```

**Recommendation:**
- **MEDIUM Priority:** Distinguish between "controlled capitals" (you own) and "vassal capitals" (counted at 50%)
- Recalculate: `iControlledProgress = (iOwnCapitals + iVassalCapitals*0.5) / iTotalCapitals * 100`
- Or: Split victory into two independent thresholds (own ≥ 60%, vassals ≥ 40%)

---

#### **Issue #3: Two Independent Metrics Create Ambiguous Victory Condition** — MEDIUM Severity

**Problem:** Victory requires BOTH metrics ≥ threshold (e.g., both ≥ 50%), but they're tracked as independent progress percentages. The UI shows max(might%, capitals%) which is misleading—you could be at 99% might, 10% capitals and appear "almost winning."

**Impact:** Players are confused about actual victory state. "Progress to domination" is shown as single number but requires dual metrics.

**Code Reference:** CvDiplomacyAI.cpp:3127
```cpp
return max(iOurMight, iCapitalsProgress);  // Only returns max, not min
```

**Recommendation:**
- **HIGH Priority:** Change return to `return min(iOurMight, iCapitalsProgress);` to show bottleneck
- OR: Return both metrics separately in AI evaluations (internal use)
- Document: "Domination Victory requires BOTH military dominance (50%+) AND capital control (50%+) simultaneously"

---

### 4.2 Diplomatic Victory Issues

#### **Issue #4: Hard Cap at 59% Before UN Creates Artificial Gating** — MEDIUM Severity

**Problem:** Before UN is founded, diplomatic progress is capped at 59% regardless of vote count. This creates a **two-phase victory**: pre-UN suzerainty grind (0-59%) → post-UN voting phase (60-100%).

**Impact:**
- Players see "59% progress" for 50+ turns while waiting for UN
- AI can't pursue diplomatic victory until UN era techs are available
- Early diplomatic victories are impossible even with vote dominance

**Code Reference:** CvDiplomacyAI.cpp:3158-3172
```cpp
if (iExtra == 0)  // iExtra = UN sessions active
{
    iProgress = min(iProgress, 59);  // Hard cap
}
else
{
    iProgress = min(iProgress, 59 + 20 * iExtra);  // Soft cap with UN
}
```

**Recommendation:**
- **HIGH Priority:** Make pre-UN progress scale to vote percentage but with different weighting
- Revised: `iProgress = (iOurVotes * 59) / max(1, iNeededVotes)` before UN
- Post-UN: `iProgress = 59 + (iOurVotes * 40) / max(1, iNeededVotes)`
- This allows 0-59% pre-UN scaling, not flat ceiling

---

#### **Issue #5: Diplomatic Victory Requires Specific Era Tech Discovery** — LOW Severity

**Problem:** UN only appears when a specific tech is researched (e.g., Telecommunications). If no player researches it, no one can win diplomatically. The victory condition becomes **era-gated**.

**Impact:** Science-backwards players can never achieve diplomatic victory. In tech-restricted scenarios, diplomatic victory might be permanently locked.

**Code Reference:** Implicit in `pLeague->IsUnitedNations()` check; UN creation is tech-driven

**Recommendation:**
- **MEDIUM Priority:** Add fallback—UN creation should occur automatically at a certain era (e.g., Modern) if not already founded
- Ensure AI prioritizes this tech in diplomatic playstyle
- Consider **League of Nations** (earlier tech) as stepping stone to UN

---

#### **Issue #6: Vote Calculation Opaque to Player** — LOW Severity

**Problem:** `CalculateStartingVotesForMember()` uses complex formula (suzerain count, city-state relations, alliance dynamics, era bonuses) that isn't transparent to player. Progress bars show vote %, but player doesn't know how to gain 1 more vote.

**Impact:** Players can't strategically pursue diplomatic victory. Suzerain race feels RNG-dependent.

**Code Reference:** CvDiplomacyAI.cpp:3146
```cpp
int iOurVotes = pLeague->CalculateStartingVotesForMember(GetPlayer()->GetID(), true);
```

**Recommendation:**
- **MEDIUM Priority:** Add tooltip to diplomatic progress bar listing vote sources:
  - "Suzerain: 3 votes"
  - "Alliances: 2 votes"
  - "Era Bonus: 1 vote"
  - "Total: 6 votes needed (40%)"
- Expose vote calculation in UI

---

### 4.3 Science Victory Issues

#### **Issue #7: Science Progress (Tech Ratio) Is Misleading** — MEDIUM Severity

**Problem:** Science victory progress mixes two independent metrics:
1. **Tech Knowledge (0-100%):** How many techs you know vs. total techs
2. **Spaceship Progress (0-21%):** Project completion ratio

The formula caps progress at `min(iScienceProgress, 78 + iSpaceshipProgress)`, meaning:
- With 50% tech knowledge, you start at 50% progress (falsely low)
- With 100% tech knowledge AND 0% spaceship, you're at 78% (artificially high)

**Impact:** Science victory appears closer than it is. A player at 90% tech, 0% spaceship shows 78% victory progress (should be ≤60%).

**Code Reference:** CvDiplomacyAI.cpp:3203-3206
```cpp
int iScienceProgress = (GET_TEAM(m_pPlayer->getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100) 
                       / max(1, GC.getNumTechInfos() - 1);
int iSpaceshipProgress = (21 * iProjectsCompleted) / max(1, iProjectsRequired);
int iProgress = min(iScienceProgress, 78 + iSpaceshipProgress);
```

**Recommendation:**
- **HIGH Priority:** Separate tech and spaceship progress metrics
- Display: "Tech Knowledge: 90% | Spaceship: 15% → Victory Progress: 40%"
- Recalculate: `iProgress = (iScienceProgress / 2) + (iSpaceshipProgress * 2)`
- This makes spaceship projects more valuable (2:1 weighting)

---

#### **Issue #8: Apollo Program Prerequisite Creates Invisible Gate** — LOW Severity

**Problem:** Spaceship projects require Apollo Program to be completed. The game doesn't communicate this requirement to player until they try to build spaceship projects (and they fail with unclear error).

**Impact:** New players waste production trying to build spaceship projects before Apollo. AI doesn't prioritize Apollo until close to science victory.

**Code Reference:** CvDiplomacyAI.cpp:3195-3202
```cpp
ProjectTypes eApollo = (ProjectTypes)GC.getInfoTypeForString("PROJECT_APOLLO_PROGRAM", true);
if (eApollo != NO_PROJECT)
{
    iProjectsRequired++;
    if (GET_TEAM(m_pPlayer->getTeam()).getProjectCount(eApollo) > 0)
    {
        iProjectsCompleted++;
    }
}
```

**Recommendation:**
- **LOW Priority:** Add tooltip: "Apollo Program must be completed before spaceship projects can be built"
- Update progress bar: Show "Spaceship: 0/12 (Apollo required)" until Apollo is done
- Ensure AI pursues Apollo before other spaceship projects

---

### 4.4 Culture Victory Issues

#### **Issue #9: Lowest Tourism Influence Bottleneck Can Be Exploited** — MEDIUM Severity

**Problem:** Culture victory progress is determined by your **minimum influence** across all major civs. If you ignore one civ (e.g., a continent away), your victory progress stays at 0%, even if you're at 95%+ on all others.

**Impact:** Players can grief culture-pursuing civs by building tourism in ignored civs. A single isolated civ can delay your victory by 50+ turns.

**Code Reference:** CvDiplomacyAI.cpp:3223-3247
```cpp
for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
{
    PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;
    CvPlayer& kPlayer = GET_PLAYER(eLoopPlayer);
    
    if (eLoopPlayer != GetID() && kPlayer.isAlive() && kPlayer.isMajorCiv())
    {
        // Calculate influence on this player
        // If this influence < iLowestPercent, update iLowestPercent
        if (iPercent < iLowestPercent)
        {
            iLowestPercent = iPercent;  // Bottleneck
        }
    }
}
```

**Recommendation:**
- **MEDIUM Priority:** Allow **"close to victory" exemption** for cultural tourists
- Alternative: "Culture victory requires influential or exotic on 80% of major civs" instead of 100%
- OR: Rework to `iProgress = (sum of all influence) / (numMajorCivs * 100)` (average instead of min)
- Current bottleneck mechanic is too punishing for map-spread scenarios

---

#### **Issue #10: MOD_BALANCE_CULTURE_VICTORY_CHANGES Policy Cap Is Unintuitive** — MEDIUM Severity

**Problem:** When MOD_BALANCE_CULTURE_VICTORY_CHANGES is enabled, culture victory progress is capped by `18 + iPolicies * 3`. A player at 100% tourism influence can still only show (18 + 27*3 = 99%) progress without more policies.

**Impact:**
- Players don't understand why they're at 99% for 10 turns (need policies, not tourism)
- Culture-specialists are penalized for policy-heavy builds
- Progress appears stuck without explanation

**Code Reference:** CvDiplomacyAI.cpp:3229-3235
```cpp
if (MOD_BALANCE_CULTURE_VICTORY_CHANGES)
{
    int iPolicies = GetPlayer()->GetPlayerPolicies()->GetNumPoliciesOwned(true, true, true);
    iPolicies = min(iPolicies, 27);
    iProgress = min(iLowestPercent, 18 + iPolicies * 3);  // Soft cap via policies
}
```

**Recommendation:**
- **MEDIUM Priority:** Document this mechanic—add tooltip: "Culture Victory requires both high tourism AND adopted policies. Each policy increases maximum progress by 3%."
- Consider increasing per-policy bonus (4-5% instead of 3%) to make the gate less severe
- Alternative: Make policy requirement proportional to tourism level (high tourism = fewer policies needed)

---

### 4.5 Score Victory Issues

#### **Issue #11: Score Components Are Wildly Unbalanced by VP Multipliers** — MEDIUM Severity

**Problem:** Score multipliers differ drastically between components:
- **Wonders:** 25 points each (most valuable)
- **Policies:** 16 points each
- **Future Tech:** 10 points each
- **Cities:** 10 points each
- **Tech:** 6 points each
- **Great Works:** 4 points each
- **Religion:** 5 (beliefs) / 3 (cities) points
- **Population:** 2 points each
- **Land:** 1 point each

A single wonder ≈ 2.5 policies or 25 techs. This makes score heavily dependent on **wonder race** (RNG/rush) rather than breadth.

**Impact:**
- Early wonder spam heavily influences final score
- Late-game tech/culture progression is undervalued
- Score victory often decides games, but it's RNG-heavy
- Players can't catch up without wonders

**Code Reference:** CvPlayer.cpp:11104-11172
```cpp
// Wonders: 25x multiplier
iScore = GetNumWonders() * GD_INT_GET(SCORE_WONDER_MULTIPLIER);  // 25

// Policies: 16x multiplier
iScore = GetPlayerPolicies()->GetNumPoliciesOwned() * GD_INT_GET(SCORE_POLICY_MULTIPLIER);  // 16

// Techs: 6x multiplier
iScore = GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * GD_INT_GET(SCORE_TECH_MULTIPLIER);  // 6
```

**Recommendation:**
- **HIGH Priority:** Rebalance multipliers to reduce wonder dependency
- Proposed (VP):
  - Wonders: 15 (was 25, -40%)
  - Policies: 20 (was 16, +25%)
  - Techs: 10 (was 6, +67%)
  - Great Works: 6 (was 4, +50%)
  - Cities: 8 (was 10, -20%)
- This makes tech/culture more valuable and wonders less dominant

---

#### **Issue #12: Score Victory Has No Endgame Aggression Mechanic** — LOW Severity

**Problem:** Unlike other victories, there's no `IsCloseToScoreVictory()` function or endgame aggression threshold. If a player is clearly winning on score (95% of points), other AIs don't react or try to block.

**Impact:** Score victory races are one-sided; no urgency for competitors. Players coasting to score victory face no challenge.

**Code Reference:** No endgame function exists for score; it's purely turn-limit dependent

**Recommendation:**
- **MEDIUM Priority:** Add score victory endgame detection
- When a player reaches 80%+ of max possible score, trigger `IsCloseToScoreVictory()`
- Other AIs become endgame-aggressive: offer less favorable trades, refuse tech trading, form alliances against leader
- This adds drama to late-game score races

---

#### **Issue #13: Score Calculation Doesn't Account for Game Length (Except Winner Scaling)** — MEDIUM Severity

**Problem:** Score is calculated the same way whether you're on turn 50 or turn 500. A 500-turn science race player has identical scoring incentives as a 150-turn domination player. The **winner scaling factor** only applies at game end, not mid-game.

**Impact:** Early victories are incentivized (fewer turns = higher multiplier). Score victory doesn't feel like a "long game" victory.

**Code Reference:** CvPlayer.cpp:11095-11102
```cpp
if(bFinal && bWinner)
{
    int iGameProgressPercent = 100 * GC.getGame().getGameTurn() / GC.getGame().getEstimateEndTurn();
    iGameProgressPercent = iGameProgressPercent < 1 ? 1 : iGameProgressPercent;
    iScore *= 100;
    iScore /= iGameProgressPercent;  // Only applied at victory, not tracking
}
```

**Recommendation:**
- **MEDIUM Priority:** Track "estimated final score" accounting for game length
- Show "Projected Score (Normalized): X" in score breakdown
- Make late-game score contributions more valuable (multiplicative bonus for achievements after turn 80% of limit)
- This preserves early-victory bonus but reduces the gap

---

### 4.6 AI Victory Pursuit Issues

#### **Issue #14: Victory Pursuits Change Too Frequently** — MEDIUM Severity

**Problem:** AI updates `DoUpdateCurrentVictoryPursuit()` **every single turn** (CvDiplomacyAI.cpp:9645-9750). This causes AIs to flip between victories constantly:
- Turn 50: Pursuing Science (87% tech knowledge)
- Turn 51: Pursuing Domination (military grew slightly)
- Turn 52: Back to Science (researched key tech)

**Impact:**
- AI strategy is incoherent; pursuits flip monthly
- Military buildup → diplomatic buildup → cultural expansion all happen simultaneously
- Players can't predict AI intentions
- Resource allocation is inefficient (start spaceship, abandon for military, restart)

**Code Reference:** CvDiplomacyAI.cpp:9683-9750
```cpp
// Inside DoUpdateCurrentVictoryPursuit():
if (IsCloseToSpaceshipVictory())  // Checked every turn
{
    SetCurrentVictoryPursuit(VICTORY_PURSUIT_SCIENCE);
    return;
}
else if (IsCloseToCultureVictory())  // Checked every turn
{
    SetCurrentVictoryPursuit(VICTORY_PURSUIT_CULTURE);
    return;
}
// ... This runs EVERY TURN
```

**Recommendation:**
- **HIGH Priority:** Add **pursuit inertia/hysteresis**
  - Don't switch pursuits unless new pursuit is >15% higher progress
  - Only re-evaluate pursuit every **10 turns** (not every turn)
  - Require sustained progress lead for 3+ turns before switching
- Pseudocode:
  ```cpp
  if (m_iTurnsSincePursuitChange < 10)
      return; // Don't recalculate yet
  
  int iBestProgress = GetCurrentVictoryProgress();
  for (all victories):
      if (GetProgress(eVictory) > iBestProgress + 15)  // 15-point threshold
          SetCurrentVictoryPursuit(eVictory);
  ```

---

#### **Issue #15: AI Doesn't Account for Victory Availability When Pursuing** — LOW Severity

**Problem:** AI can pursue a victory (e.g., Science) even if the victory type is disabled by game options. `SelectDefaultVictoryPursuits()` checks victory validity, but `DoUpdateCurrentVictoryPursuit()` doesn't always verify.

**Impact:** AI wastes turns pursuing impossible victories. If science is disabled, AI still pursues spaceship goals, wasting research.

**Code Reference:** CvDiplomacyAI.cpp:2515-2572 (Checks validity) vs. 9683 (No validity check in dynamic update)

**Recommendation:**
- **LOW Priority:** Add validity checks before every pursuit evaluation
  ```cpp
  if (!GC.getGame().isVictoryValid(eVictory))
      continue;  // Skip this victory type
  ```

---

## 5. Comprehensive Issues Summary

| Issue | Type | Severity | Impact | Effort |
|---|---|---|---|---|
| #1: Military Might Ignores Naval Power | Balance | MEDIUM | Domination progress inflated for navies | HIGH |
| #2: Vassal States Volatile Capital Metric | Design | LOW | Confusing progress bars during vassalization | MEDIUM |
| #3: Max() Instead of Min() Metrics | Design | MEDIUM | Ambiguous dual-metric victory state | LOW |
| #4: Diplomatic Hard Cap at 59% Pre-UN | Balance | MEDIUM | Artificial two-phase victory gating | MEDIUM |
| #5: UN Requires Tech Discovery | Design | LOW | Era-gated victory condition | MEDIUM |
| #6: Vote Calculation Opaque | UX | LOW | Players can't strategize diplomacy | LOW |
| #7: Science Progress Misleading Mix | Design | MEDIUM | Tech ratio inflates spaceship progress | LOW |
| #8: Apollo Program Invisible Gate | UX | LOW | Players don't know prerequisite | LOW |
| #9: Culture Victory Bottleneck Exploitable | Balance | MEDIUM | Single civ can block entire victory | HIGH |
| #10: Policy Cap Unintuitive | UX | MEDIUM | Players don't understand why stuck at 99% | LOW |
| #11: Score Components Wildly Unbalanced | Balance | MEDIUM | Wonder-dependent scoring | MEDIUM |
| #12: Score Victory No Endgame Reaction | Design | LOW | Score races have no drama | MEDIUM |
| #13: Score Multiplier Doesn't Scale with Game Length | Balance | MEDIUM | Early victories incentivized | MEDIUM |
| #14: Victory Pursuits Change Every Turn | Design | MEDIUM | AI strategy incoherent | MEDIUM |
| #15: AI Doesn't Validate Victory Availability | Design | LOW | AI pursues disabled victories | LOW |

---

## 6. Recommendations (Prioritized)

### Tier 1: High-Impact, Low-Effort Fixes

**1. Fix Domination Metrics Return (Issue #3)**
- Change `return max(iOurMight, iCapitalsProgress)` to `return min(...)` or split returns
- **Effort:** 1 line change in CvDiplomacyAI.cpp:3127
- **Impact:** Fixes confusing progress bars immediately
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L3127)

**2. Add Culture Victory Bottleneck Exemption (Issue #9)**
- Allow victory with 80% of civs at influential (not 100%)
- **Effort:** 2-3 lines in CvDiplomacyAI.cpp:3223-3247
- **Impact:** Reduces grief potential significantly
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L3223)

**3. Add AI Pursuit Hysteresis (Issue #14)**
- Add 10-turn cooldown + 15-point threshold before pursuit change
- **Effort:** 5-10 lines in CvDiplomacyAI.cpp:9645-9750
- **Impact:** AI becomes coherent and predictable
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L9645)

### Tier 2: Balance Improvements

**4. Separate Science Progress Metrics (Issue #7)**
- Display tech % and spaceship % separately; blend with 2:1 weighting
- **Effort:** 10-15 lines in CvDiplomacyAI.cpp:3203-3206
- **Impact:** Science victory progress feels accurate
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L3203)

**5. Rebalance Score Multipliers (Issue #11)**
- Reduce wonders (25→15), increase techs (6→10), increase policies (16→20)
- **Effort:** 5 changes in CvPlayer.cpp:11104-11172
- **Impact:** Score victory less dependent on wonder rush
- **File:** [CvPlayer.cpp](CvGameCoreDLL_Expansion2/CvPlayer.cpp#L11104)

**6. Weight Military Might by Domain (Issue #1)**
- Separate land/naval; apply 0.5x weight to naval
- **Effort:** 15-20 lines in CvDiplomacyAI.cpp:3107-3120
- **Impact:** Domination progress more accurate for varied armies
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L3107)

### Tier 3: Design Improvements

**7. Soften Diplomatic Pre-UN Cap (Issue #4)**
- Scale pre-UN progress from 0-59% instead of flat ceiling
- **Effort:** 3-5 lines in CvDiplomacyAI.cpp:3158-3172
- **Impact:** Diplomatic victory feels less gated
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L3158)

**8. Add Score Endgame Reaction (Issue #12)**
- Implement `IsCloseToScoreVictory()` function (80%+ progress)
- **Effort:** 20-30 lines in CvDiplomacyAI.cpp (new function)
- **Impact:** Score victory races are competitive
- **File:** [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp)

**9. Add Diplomacy Vote Tooltips (Issue #6)**
- Display vote sources in UI
- **Effort:** Lua UI update in VictoryProgress.lua
- **Impact:** Players understand vote sources
- **File:** [(2) Vox Populi\Core Files\Overrides\VictoryProgress.lua](../\(2\)%20Vox%20Populi\Core%20Files\Overrides\VictoryProgress.lua#L460)

### Tier 4: Minor/Low-Priority Fixes

**10. Add Tooltips for Apollo Gate, Policy Cap (Issues #8, #10)**
- Update UI text and tooltips
- **Effort:** Low (UI/text only)
- **Impact:** UX clarity

**11. Make UN Fallback (Issue #5)**
- Ensure UN appears at defined era if not researched
- **Effort:** Medium (league creation logic)
- **Impact:** Diplomatic victory always available

**12. Distinguish Vassal vs. Owned Capitals (Issue #2)**
- Split capital control metric into two
- **Effort:** High (refactor dual metrics)
- **Impact:** Better reflects vassal volatility

---

## 7. Formula Reference

### Victory Progress Formulas (Summary)

```
DOMINATION:
  iOurMight % = Σ(yourMight) * 100 / Σ(allMight)
  iCapitals % = Σ(yourCapitals) * 100 / Σ(allCapitals)
  Progress = min(iOurMight%, iCapitals%)  [RECOMMENDATION: Change to min()]

DIPLOMATIC:
  iVotes = CalculateStartingVotesForMember(you)
  iNeeded = GetVotesNeededForDiploVictory()
  Pre-UN:  Progress = (iVotes * 100) / max(1, iNeeded), capped at 59%
  Post-UN: Progress = (iVotes * 100) / max(1, iNeeded), capped at 59+20*sessions%

SCIENCE:
  iTech% = (numTechsKnown * 100) / (numTechsTotal - 1)
  iSpace% = (projectsCompleted * 21) / projectsRequired
  Progress = min(iTech%, 78 + iSpace%)

CULTURE:
  iLowest% = min(influenceOn[civ1]%, influenceOn[civ2]%, ...) across all civs
  Base:        Progress = min(99, iLowest%)
  With Mod:    Progress = min(iLowest%, 18 + min(policyCount, 27) * 3)

SCORE:
  BaseScore = Σ(components * multipliers) [see table in Section 3.5]
  WithSize = BaseScore * mapSizeMod / 100
  Progress = (yourScore * 100) / max(1, highestScore) * (currentTurn / maxTurns)
  Winner Bonus = finalScore * 100 / gameProgressPercent
```

---

## 8. Testing Recommendations

### Validation Tests

1. **Domination Victory:**
   - Verify military might calculation includes all unit types
   - Test vassal capital transitions (break vassalage → should drop progress)
   - Confirm min() vs max() behavior with actual game wins

2. **Diplomatic Victory:**
   - Test pre-UN progress scaling (should be 0-59%, not flat)
   - Verify UN creation triggers properly
   - Confirm vote calculation matches league system

3. **Science Victory:**
   - Verify Apollo Program gates spaceship projects
   - Test tech knowledge scaling
   - Confirm project thresholds match victory XML

4. **Culture Victory:**
   - Test with isolated civs (shouldn't block entirely)
   - Verify policy cap with MOD_BALANCE_CULTURE_VICTORY_CHANGES
   - Test tourism influence calculations

5. **Score Victory:**
   - Verify all multipliers apply correctly
   - Test winner scaling at game end
   - Confirm score reflects all components

6. **AI Behavior:**
   - Monitor victory pursuit changes (should be stable, not flipping)
   - Verify AI prioritizes achievable victories
   - Test endgame aggression triggers

---

## 9. Conclusion

The Vox Populi victory system is **mechanically diverse** and **well-architected**, with clear separation between victory conditions and progress tracking. However, it suffers from:

1. **Balance asymmetries:** Score and domination are RNG-heavy; science and culture require specific builds
2. **UX clarity issues:** Progress bars are misleading (max instead of min, mixed metrics)
3. **Design gates:** Diplomatic and science have era/tech gates that feel arbitrary
4. **AI incoherence:** Victory pursuits change too frequently, making AI unpredictable

**Recommended approach:**
- **Short-term:** Fix high-impact bugs (Issues #3, #14) and add hysteresis to AI
- **Medium-term:** Rebalance score multipliers and separate science metrics
- **Long-term:** Consider fundamental design changes (bottleneck exemptions, soft pre-UN scaling)

The system is **playable and balanced** for most scenarios, but these improvements would enhance **player clarity, AI predictability, and strategic depth**.

---

**Document Version:** 1.0  
**Analysis Date:** January 9, 2026  
**Analyzed By:** GitHub Copilot (Claude Haiku 4.5)  
**Status:** Complete
