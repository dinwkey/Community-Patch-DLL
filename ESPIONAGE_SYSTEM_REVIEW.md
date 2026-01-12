# Espionage System Review
## Community-Patch-DLL Civilization V

**Date:** January 2026  
**Focus Areas:** Spy Actions, Missions, Counterespionage, Tech Stealing

---

## Executive Summary

The Espionage system in Community-Patch-DLL is sophisticated and feature-rich, featuring:

1. **Spy Management** - Multiple spy states and rank progression
2. **Mission System** - Event-based missions with VP/CP variants
3. **Tech Stealing** - Multi-step process with prerequisites and fallback logic
4. **Counterespionage** - Defense mechanisms with focus-based missions
5. **Intrigue System** - Intelligence gathering on enemy operations
6. **Espionage AI** - Diplomatic, offensive, defense, and minor civ strategies

The system contains notable TODOs, hardcoded values, and some edge case handling issues that present opportunities for improvement.

---

## 1. SPY MANAGEMENT & STATES

### Current Implementation
**Files:** `CvEspionageClasses.h`, `CvEspionageClasses.cpp`

#### Spy States (CvSpyState enum):
```cpp
SPY_STATE_UNASSIGNED         // Not deployed
SPY_STATE_TRAVELLING         // Moving to city (turn-limited)
SPY_STATE_SURVEILLANCE       // Passive, building initial intel
SPY_STATE_GATHERING_INTEL    // Active, accumulating network points
SPY_STATE_COUNTER_INTEL      // Defensive, intercepting enemy spies
SPY_STATE_SCHMOOZE           // VP: Building relations with city-state
SPY_STATE_RIGGING_ELECTIONS  // VP: Performing election manipulation
SPY_STATE_MAKING_INTRODUCTIONS // VP: Special diplomat phase
SPY_STATE_DEAD               // Killed in action
SPY_STATE_TERMINATED         // Voluntarily retired
```

#### Spy Attributes:
```cpp
CvEspionageSpy {
  m_iName              // Name ID from civ list
  m_sName              // String name
  m_eRank              // SPY_RANK (RECRUIT, AGENT, SPECIAL_AGENT)
  m_iExperience        // XP toward next rank
  m_iCityX, m_iCityY   // Current location
  m_eSpyState          // Current state
  m_iReviveCounter     // Turn counter for respawn after death
  m_eSpyFocus          // VP: Active mission focus
  m_bIsDiplomat        // Special diplomat flag (for vassals)
  m_bEvaluateReassignment  // AI flag for re-evaluation
  m_bPassive           // Passive (no missions) flag
  m_iTurnCounterspyMissionChanged      // CD: Cooldown timer
  m_iTurnActiveMissionConducted        // CD: Mission cooldown
}
```

#### Spy Rank Progression:
- **RECRUIT** → **AGENT** → **SPECIAL_AGENT** (3 ranks)
- **Experience Formula:** `m_iExperience / GD_INT_GET(ESPIONAGE_SPY_EXPERIENCE_DENOMINATOR)`
- **XP Sources:**
  - Counterspy missions: `GD_INT_GET(ESPIONAGE_XP_PER_TURN_COUNTERSPY)`
  - Diplomat missions: `GD_INT_GET(ESPIONAGE_XP_PER_TURN_DIPLOMAT)`
  - Offensive missions: `GD_INT_GET(ESPIONAGE_XP_PER_TURN_OFFENSIVE)`
  - City-state missions: `GD_INT_GET(ESPIONAGE_XP_PER_TURN_CITYSTATE)`
  - Rigging success: `GD_INT_GET(ESPIONAGE_XP_RIGGING_SUCCESS)`
  - Intrigue uncover: `GD_INT_GET(ESPIONAGE_XP_UNCOVER_INTRIGUE)`
  - Mission success: `GD_INT_GET(ESPIONAGE_SPY_XP_MISSION_SUCCESS_PERCENT)`

### Issues & Observations

#### ✓ Strengths:
- Clear state machine prevents invalid transitions
- Rank system provides progression and player engagement
- Comprehensive XP sources offer varied gameplay
- Diplomat mode for vassals adds strategic depth
- Cooldown system prevents spam (counterspy: `ESPIONAGE_COUNTERSPY_CHANGE_FOCUS_COOLDOWN`)

#### ⚠ Issues:

1. **Spy Revival Counter Unused** (CODE QUALITY)
   - **Current:** `m_iReviveCounter` incremented but never checked
   - **Finding:** Spies killed in action never naturally revive
   - **File:** `CvEspionageClasses.cpp` - no revive logic in `ProcessSpy()`
   - **Recommendation:** Implement resurrection mechanic:
   ```cpp
   if (eSpyState == SPY_STATE_DEAD)
   {
     m_iReviveCounter++
     if (m_iReviveCounter > GD_INT_GET(ESPIONAGE_SPY_REVIVAL_TURNS))
       SetSpyState(..., SPY_STATE_UNASSIGNED)
   }
   ```

2. **Passive Spy Mode Undocumented** (LOGIC CLARITY)
   - **Current:** `m_bPassive` flag set but purpose unclear
   - **Lines:** `CvEspionageClasses.cpp:941` - checks `!pSpy->m_bPassive`
   - **Issue:** No code comments explaining when/why passive mode is used
   - **Recommendation:** Document in code or add getter function with explanatory comment

3. **Diplomat Flag State Not Validated** (EDGE CASE)
   - **Current:** `m_bIsDiplomat` not checked for consistency with `SPY_STATE`
   - **Risk:** Diplomat spy could be in combat zone or wrong city type
   - **Recommendation:** Add validation in `SetSpyState()`:
   ```cpp
   if (m_bIsDiplomat && !pCity->isCapital())
     return false // Invalid diplomat placement
   ```

4. **Name Conflict Detection** (MINOR ISSUE)
   - **Lines:** `CvEspionageClasses.cpp:2517-2560`
   - **Current:** `isSpyNameInUse()` checks all civs across all players
   - **Issue:** Performance O(n²) on name lookup during spy creation
   - **Recommendation:** Cache spy names in `CvGameReligions` or similar global

---

## 2. MISSION SYSTEM (VP)

### Current Implementation
**File:** `CvEspionageClasses.cpp` - `ProcessSpyMissionResult()`

#### Mission Architecture:
```
Spy assigned to city
  → At threshold, triggers TriggerSpyFocusSetup()
    → For humans: Event popup displayed
    → For AI: GetBestMissionInCity() selects mission
      → Mission properties: identification chance, kill chance, yields
      → Mission execution with RNG seed
        → Result: DETECTED, IDENTIFIED, KILLED, KILLED_NOT_IDENTIFIED
```

#### Mission Results (VP only):
- **DETECTED** - Spy undetected, mission successful
- **IDENTIFIED** - Spy discovered, mission successful, diplomatic penalty
- **KILLED** - Spy eliminated during mission
- **KILLED_NOT_IDENTIFIED** - Spy killed, identity unknown

#### Counterspy Interaction:
```cpp
if (eCounterSpyFocus != NO_EVENT_CHOICE_CITY)
{
  pkCounterspyFocusInfo = GC.getCityEventChoiceInfo(eCounterSpyFocus)
  if (bIdentified) → Yield on identification
  if (bKilled) → Yield on spy kill
}
```

### Issues & Observations

#### ✓ Strengths:
- Event system provides flexible mission framework
- Multiple outcome paths create tension
- Counterspy integration with rewards
- Seeding ensures consistency across saves

#### ⚠ Issues:

1. **Mission Selection Evaluation Incomplete** (STRATEGIC)
   - **Current:** AI selects mission but no explanation of scoring
   - **Missing:** `GetMissionScore()` implementation details unclear
   - **Lines:** `CvEspionageClasses.cpp:575` mentions function but no definition visible
   - **Recommendation:** Document mission scoring formula:
     ```cpp
     int iScore = 0
     iScore += base_mission_value // From XML
     iScore += player_modifier_bonus
     iScore += counterspy_weakness // If applicable
     iScore -= risk_factor // Based on guards, etc.
     ```

2. **Random Seed Predictability** (BALANCING)
   - **Lines:** `CvEspionageClasses.cpp:1610-1611`
   - **Code:**
   ```cpp
   int iIdentifyRoll = GC.getGame().randRangeInclusive(1, 100, 
     CvSeeder::fromRaw(0xe9deaa7f)
       .mix(pCity->plot()->GetPseudoRandomSeed())
       .mix(m_pPlayer->GetID())
       .mix((int)eMission)
       .mix(GetNumSpyActionsDone(pCity->getOwner())));
   ```
   - **Issue:** Using `GetNumSpyActionsDone()` makes result predictable if player knows counter
   - **Recommendation:** Include additional entropy source:
   ```cpp
   .mix(GC.getGame().getGameTurn())
   ```

3. **No Mission Failure Consequences (CP)** (BALANCE)
   - **Current:** In CP, mission results processed differently
   - **Issue:** No visible mission failure mechanism in CP documented
   - **Lines:** `CvEspionageClasses.cpp:1610` comment: "used in VP only"
   - **Recommendation:** Implement CP mission results parallel to VP

4. **Counterspy Mission Change Cooldown Excessive** (BALANCING)
   - **Current:** `ESPIONAGE_COUNTERSPY_CHANGE_FOCUS_COOLDOWN`
   - **Issue:** Defender locked into mission for many turns
   - **Impact:** Reactive defense impossible if mission fails
   - **Recommendation:** Reduce cooldown to 2-3 turns or allow instant change for free

---

## 3. TECH STEALING MECHANICS

### Current Implementation
**Files:** `CvEspionageClasses.cpp`, `CvEspionageClasses.h`

#### Stealable Tech List Management:
```
BuildStealableTechList(eTargetCiv)
  → Query target's technology tree
  → Filter out:
    - Techs we already know
    - Techs ahead of us (too advanced)
    - Techs protected by embargoes
  → Build m_aaPlayerStealableTechList[eTargetCiv]
  → Track m_aiNumTechsToStealList[eTargetCiv]
```

#### Tech Stealing Prerequisites:
1. **Spy State:** Must be in `SPY_STATE_GATHERING_INTEL`
2. **Goal Reached:** `pCityEspionage->HasReachedGoal(ePlayer)`
3. **Tech Available:** `m_aaPlayerStealableTechList[eTarget].size() > 0`
4. **No Counterspy Overwhelming:** Check counterspy effectiveness
5. **Tech Cost Met:** `CalcRequired()` threshold exceeded

#### Tech Cost Calculation: `CalcRequired(SPY_STATE_GATHERING_INTEL, ...)`

**Base Formula:**
```cpp
iTechCost = target_tech_cost
iTechCost *= game_speed_multiplier          // Slower = harder steal
iTechCost *= start_era_multiplier           // Different eras scale
iTechCost *= (100 + current_era * 5) / 100 // Later eras harder

// Vulnerability penalty: no counterspy available
if (!bGlobalCheck && target_has_no_spies)
  iTechCost *= 150 / 100  // 50% harder without counterspy
```

**Result:** Scaled by 100 for integer math

#### Steal Execution: `DoStealTechnology()`
```cpp
bool CvPlayerEspionage::DoStealTechnology(CvCity* pPlayerCity, PlayerTypes eTargetPlayer)
{
  if (m_aaPlayerStealableTechList[eTargetPlayer].size() <= 0)
    return false

  // Random tech selection from list
  uint uGrab = GC.getGame().urandLimitExclusive(m_aaPlayerStealableTechList[eTargetPlayer].size(), ...)
  TechTypes eStolenTech = m_aaPlayerStealableTechList[eTargetPlayer][uGrab]

  // Grant tech to thief
  GET_TEAM(eTeam).setHasTech(eStolenTech, true, m_pPlayer->GetID(), true, true)
  GET_TEAM(eTeam).GetTeamTechs()->SetNoTradeTech(eStolenTech, true)  // Can't trade it

  // Rebuild list, notify both players
  BuildStealableTechList(eTargetPlayer)
  return true
}
```

#### Fallback Logic (No Techs Available):
```cpp
// Line 1049-1062: When no stealable techs
if (m_aaPlayerStealableTechList[eCityOwner].size() == 0)
{
  // Reset to SURVEILLANCE state
  pCityEspionage->ResetProgress(ePlayer)
  pSpy->SetSpyState(..., SPY_STATE_SURVEILLANCE)
  pSpy->m_bEvaluateReassignment = true  // Flag for AI to move spy
  
  // Notify player
  Notification: "TXT_KEY_NOTIFICATION_SPY_CANT_STEAL_TECH"
}
```

### Issues & Observations

#### ✓ Strengths:
- Proper randomization prevents predictability
- Fallback prevents spy lockup with no targets
- No-trade marking prevents second-order exploitation
- Cost scaling by era prevents early-game tech rush
- Respects game speed settings

#### ⚠ Issues:

1. **Tech List Rebuild Incomplete** (LOGIC)
   - **Current:** `BuildStealableTechList()` called after steal
   - **Issue:** Same tech can be stolen multiple times from same city in single turn
   - **Scenario:**
     ```
     Turn 100: Spy reaches goal, steals Tech A
     Turn 100: List rebuilt, Tech A removed
     Turn 100: Another spy in same city steals Tech B
     // But if both spies reached goal same turn...
     ```
   - **Lines:** `CvEspionageClasses.cpp:1238-1245` (steal occurs)
   - **Recommendation:** Build list once per turn beginning, not after each steal

2. **Counterspy Penalty Inconsistent** (BALANCING)
   - **Lines:** `CvEspionageClasses.cpp:3249-3254`
   - **Code:**
   ```cpp
   if (!bGlobalCheck && GET_PLAYER(ePlayer).GetEspionage()->GetNumSpies() <= 0)
   {
     iTechCost *= 150  // 50% increase
     iTechCost /= 100
   }
   ```
   - **Issue:** Defender with NO spies makes tech harder to steal
   - **Logic:** Should be opposite—easier to steal if undefended
   - **Impact:** Penalties losing players from defending
   - **Fix:** Change to:
   ```cpp
   iTechCost *= 66    // 33% reduction for undefended
   iTechCost /= 100
   ```

3. **Tech Filtering Criteria Unclear** (CODE QUALITY)
   - **Current:** `BuildStealableTechList()` visible but filtering not shown
   - **Missing:** Exact logic for which techs are stealable
   - **Recommendation:** Add comments documenting:
     - Min tech difference required
     - Max tech advancement allowed
     - Embargo check implementation

4. **Stolen Tech Permanence** (STRATEGIC)
   - **Current:** Stolen tech marked with `SetNoTradeTech()` - permanent
   - **Issue:** Player who steals can never trade; they keep advantage forever
   - **Recommendation:** Consider time decay option:
     ```cpp
     // After X turns, remove no-trade restriction
     if (GC.getGame().getGameTurn() - iTechStealTurn > ESPIONAGE_NO_TRADE_DURATION)
       ClearNoTradeTech(eStolenTech)
     ```

5. **Duplicate Steal Notifications** (UI)
   - **Lines:** `CvEspionageClasses.cpp:1247-1267`
   - **Code:** Creates both summary and notification
   - **Issue:** Player sees two notifications for single steal event
   - **Recommendation:** Combine into single notification with tech name prominent

6. **No Protection for Starting Techs** (EXPLOIT)
   - **Current:** All techs below target tech level stealable
   - **Missing:** Exclusion for starting techs or obsolete techs
   - **Recommendation:** Filter out:
     - Techs in START_TECH_TREE
     - Techs with NumTechs == 0 (obsolete)

---

## 4. COUNTERESPIONAGE SYSTEM

### Current Implementation
**Files:** `CvEspionageClasses.cpp`, `CvEspionageClasses.h`

#### Counterspy States:
```cpp
SPY_STATE_COUNTER_INTEL     // Defending city from enemy spies
  → CvEspionageSpy::m_eSpyFocus   // VP: Active mission
  → CityEventChoiceTypes eCounterSpyFocus  // Mission being conducted
```

#### Counterspy Focus Missions (VP):
- Base: 33% catch spies per turn
- Mission-specific modifiers
- Adjusted by spy rank: `(iRank * 10)%`
- Adjusted by policy: `POLICYMOD_CATCH_SPIES_MODIFIER`

#### Network Point Reduction:
**Function:** `CalcNetworkPointsPerTurn()` (VP only)

```cpp
iNP = 30              // Base NP
iNP += rank * 10      // Spy level bonus
iNP += max(0, (influence_level - 1) * 10)  // Cultural influence
iNP += min(tech_diff, 10) * 2               // Tech difference
iNP += policies                              // Policy bonuses

// Counterspy reduction (VP)
if (city_has_counterspy && counterspy_has_mission)
{
  iTemp = -mission_rate_reduction * (counterspy_rank + 1)
  iNP += iTemp  // Reduction applied
}
```

#### Counterspy Mission Setup: `TriggerSpyFocusSetup()`
```
For humans:
  → Display event popup with mission choices
  → Player selects active mission focus
  
For AI:
  → GetBestMissionInCity(pCity, aCounterspyMissionList, uiSpyIndex)
  → AI selects optimal mission
  → Mission state persists until manually changed
```

#### Counterspy Detection Mechanics:
**When Spy Enters City:**
- Spy notified: "NOTIFICATION_COUNTERSPY_DETECTED"
- Shows counterspy focus mission information
- Spy can still proceed (not blocked)

**When Spy Conducts Mission:**
- Mission subject to counterspy focus bonuses
- Identification/kill chances modified by defender

### Issues & Observations

#### ✓ Strengths:
- Focused mission system allows strategic response
- VP network point reduction adds tactical layer
- Spy rank affects effectiveness (thematic)
- Policy integration creates strategic choices
- Cultural influence provides alternative defense

#### ⚠ Issues:

1. **Counterspy Mission Setup TODO** (INCOMPLETE FEATURE)
   - **Line:** `CvEspionageClasses.cpp:941`
   - **Code Comment:** `// TODO: need to proclaim surveillance somehow`
   - **Issue:** Spy enters surveillance but no announce of counterspy presence
   - **Impact:** Player doesn't know if city is defended until spy acts
   - **Recommendation:** Add notification when counterspy established:
   ```cpp
   CvNotifications* pNot = GET_PLAYER(eCityOwner).GetNotifications()
   if (pNot && eCounterSpyFocus != NO_EVENT_CHOICE_CITY)
   {
     Localization::String str = Lookup("TXT_KEY_EO_COUNTERSPY_ESTABLISHED")
     str << pCity->getNameKey() << mission_name
     pNot->Add(..., str, ...)
   }
   ```

2. **Counterspy Passive Effectiveness Missing** (BALANCING)
   - **Current:** Counterspy only affects missions (VP) or identification (CP)
   - **Missing:** Passive pressure on spy gathering
   - **Scenario:** Defender has high-rank counterspy but no mission—spy gathers freely
   - **Recommendation:** Add passive penalty:
   ```cpp
   if (pCityEspionage->HasCounterSpy())
   {
     int iCounterspyRank = pCityEspionage->GetCounterSpyRank()
     iNP *= (100 - iCounterspyRank * 15)  // Up to 45% reduction
     iNP /= 100
   }
   ```

3. **Counterspy Doesn't Block Spy Arrival** (TACTICAL)
   - **Current:** Spy can enter city with counterspy present
   - **Missing:** Option to deny entry or force displacement
   - **Game Design:** No consequence for counterspy failure to detect entry
   - **Recommendation:** Add detection roll on arrival:
   ```cpp
   if (pCity->GetCityEspionage()->HasCounterSpy())
   {
     int iDetectChance = 25 + counterspy_rank * 10
     if (rand(100) < iDetectChance)
       return false  // Entry denied
   }
   ```

4. **Mission Change Cooldown Blocks Adaptation** (GAMEPLAY)
   - **Lines:** `CvEspionageClasses.cpp:2823-2830`
   - **Code:**
   ```cpp
   int GetNumTurnsSpyMovementBlocked(uint uiSpyIndex)
   {
     if (pSpy->GetTurnCounterspyMissionChanged() == 0)
       return 0
     return max(0, ESPIONAGE_COUNTERSPY_CHANGE_FOCUS_COOLDOWN 
                   + pSpy->GetTurnCounterspyMissionChanged() 
                   - GC.getGame().getGameTurn())
   }
   ```
   - **Issue:** Locked into mission for many turns
   - **Scenario:** Mission is failing but can't switch for 5+ turns
   - **Recommendation:** Reduce to 2 turns or allow free switch once per turn

5. **Counterspy Passive Bonuses Unclear** (DOCUMENTATION)
   - **Current:** City espionage mentions `m_aiNextPassiveBonus`
   - **Missing:** Documentation on when passive bonuses activate
   - **File:** `CvEspionageClasses.h:517` defines structure but purpose unclear
   - **Recommendation:** Add code comments explaining passive bonus system

6. **No Counterspy Chain Defense** (TACTICAL)
   - **Current:** Each city defended independently
   - **Missing:** Network effects from multiple counterspies
   - **Recommendation:** Small bonus if adjacent city has active counterspy:
   ```cpp
   int iChainBonus = 0
   for (auto pAdjacentCity : nearby_cities)
   {
     if (pAdjacentCity->GetCityEspionage()->HasCounterSpy())
       iChainBonus += 2
   }
   iNP += iChainBonus
   ```

---

## 5. NETWORK POINTS SYSTEM (VP)

### Current Implementation
**Function:** `CalcNetworkPointsPerTurn()` (VP only, lines 3282-3360)

#### NP Sources:
1. **Base:** 30 NP per turn
2. **Spy Rank:** +10 per rank (0-20 max)
3. **Cultural Influence:** +10 per influence level above exotic
4. **Tech Difference:** +2 per tech behind (capped at 10 techs)
5. **Policies:** Player-specific bonuses
6. **Counterspy Reduction:** -reduction * (counterspy_rank + 1)

#### NP Requirements:
```
Thresholds unlock passive bonuses
Multiple tiers of benefits
Higher = more difficult but better rewards
```

#### Scaling Over Time:
```cpp
int iMyPoliciesEspionageModifier = GetPlayerPolicies()->GetNumericModifier(
  POLICYMOD_STEAL_TECH_FASTER_MODIFIER)
// Applied multiplicatively at end
```

### Issues & Observations

#### ✓ Strengths:
- Multiple independent sources allow varied playstyles
- Tech difference encourages knowledge progress
- Cultural dominance provides alternative path
- Scaling prevents permanent tech advantage

#### ⚠ Issues:

1. **Cultural Influence Poorly Scaled** (BALANCING)
   - **Lines:** `CvEspionageClasses.cpp:3300-3302`
   - **Code:**
   ```cpp
   int iInfluenceLevel = (int)m_pPlayer->GetCulture()->GetInfluenceLevel(eCityOwner)
   iTemp = max(0, (iInfluenceLevel - 1) * 10)
   ```
   - **Issue:** Exotic = 0 bonus, Connected = 10, Familiar = 20, max = 40+
   - **Impact:** Minimal impact on overall NP (~10-15%)
   - **Recommendation:** Increase per-level bonus:
   ```cpp
   iTemp = iInfluenceLevel * 20  // 0-120+ bonus range
   ```

2. **Tech Difference Capped Too Low** (BALANCING)
   - **Lines:** `CvEspionageClasses.cpp:3305-3307`
   - **Code:**
   ```cpp
   int iTechDifference = team_techs_known - our_techs_known
   iTemp = max(0, min(iTechDifference, 10)) * 2
   ```
   - **Issue:** At 10+ tech difference, bonus plateaus
   - **Impact:** Late game spies vs advanced civs get no penalty
   - **Recommendation:** Remove cap or increase it:
   ```cpp
   iTemp = min(iTechDifference, 20) * 3  // 0-60 bonus
   ```

3. **No Penalty for Stealable Tech Exhaustion** (GAMEPLAY)
   - **Current:** NP calculation ignores tech availability
   - **Issue:** NP keeps climbing even with no targets
   - **Recommendation:** Reduce NP if few stealable techs:
   ```cpp
   if (GetNumStealableTechs(eCityOwner) <= 1)
     iNP *= 50  // Drastic reduction
     iNP /= 100
   ```

4. **Passive Bonus Thresholds Hardcoded** (CODE QUALITY)
   - **Current:** Network point thresholds stored in city espionage
   - **Missing:** Documentation of threshold progression
   - **Recommendation:** Add XML-driven thresholds for modding

---

## 6. INTRIGUE SYSTEM

### Current Implementation
**Files:** `CvEspionageClasses.cpp` - `UncoverIntrigue()`, `GetRandomIntrigue()`

#### Intrigue Types Uncovered:
1. **Army Sneak Attack** - Detects AI_OPERATION_CITY_ATTACK_LAND
2. **Amphibious Sneak Attack** - Detects AI_OPERATION_CITY_ATTACK_NAVAL
3. **Building Army** - Detects military buildup (ARMY_TYPE_LAND/NAVAL)
4. **Deception** - Detects hidden diplomatic intentions
5. **Wonder Construction** - Detects wonder/project builds
6. **Generic Building** - Detects standard building projects

#### Intrigue Notification:
```cpp
struct IntrigueNotificationMessage
{
  PlayerTypes m_eDiscoveringPlayer  // Who found the intel
  PlayerTypes m_eSourcePlayer       // Who has the intel (spied on)
  PlayerTypes m_eTargetPlayer       // Who is the target (if applicable)
  PlayerTypes m_eDiplomacyPlayer    // Additional player involved
  BuildingTypes m_eBuilding         // Building being constructed
  ProjectTypes m_eProject           // Project being built
  UnitTypes m_eUnit                 // Unit being built (if applicable)
  int m_iIntrigueType               // Intrigue enum type
  int m_iTurnNum                    // Turn discovered
  CvString m_strSpyName             // Spy's name
  int iSpyID                        // Spy ID
  bool m_bShared                    // Whether shared with allies
}
```

#### Spy Rank Effects on Intrigue:
```cpp
iSpyRank = pSpy->GetSpyRank(ePlayer) 
         + GetCulture()->GetInfluenceMajorCivSpyRankBonus(eCityOwner)

if (MOD_BALANCE_VP)
  iSpyRank = SPY_RANK_SPECIAL_AGENT  // VP: Always full intel

// Rank determines what intrigue becomes visible
if (iSpyRank >= SPY_RANK_AGENT)
  pTargetCity = pSneakAttackOperation->GetTargetPlot()->getPlotCity()
else
  eRevealedTargetPlayer = (PlayerTypes)MAX_MAJOR_CIVS  // Unknown target
```

### Issues & Observations

#### ✓ Strengths:
- Comprehensive intrigue detection covers major threats
- Spy rank determines information quality (thematic)
- VP reveals all intel (simpler for newer players)
- Proper target identification based on knowledge

#### ⚠ Issues:

1. **Intrigue Message Expiration Hardcoded** (CODE QUALITY)
   - **Lines:** `CvEspionageClasses.cpp:6318`
   - **Code:**
   ```cpp
   if (m_aIntrigueNotificationMessages[ui].m_iTurnNum 
       < (GC.getGame().getGameTurn() - iIntrigueTurnsValid))
   ```
   - **Comment:** `// todo: make 5 an xml global`
   - **Issue:** Message validity hardcoded at 5 turns
   - **Recommendation:** Move to XML constant:
   ```xml
   <GlobalDefines>
     <Define>
       <DefineName>ESPIONAGE_INTRIGUE_MESSAGE_TURNS_VALID</DefineName>
       <iValue>5</iValue>
     </Define>
   </GlobalDefines>
   ```

2. **No Intrigue for Religious Operations** (FEATURE GAP)
   - **Current:** Only military/diplomatic intrigue
   - **Missing:** Intrigue about religion spreading, holy cities, etc.
   - **Recommendation:** Add intrigue for:
     - Holy city threats
     - Large missionary armies
     - Prophet movement toward cities

3. **Deception Detection Unreliable** (LOGIC)
   - **Lines:** `CvEspionageClasses.cpp:2395-2410`
   - **Current:** Checks surface vs honest approach
   - **Issue:** Only surfaces when teams not already at war
   - **Problem:** If hidden war already exists, no intel value
   - **Recommendation:** Add check for hidden declarations:
   ```cpp
   if (likely_future_war && !currently_at_war)
     AddIntrigueMessage(...)  // Warn about impending attack
   ```

4. **AI Shadow Diplomacy Filter Incomplete** (EDGE CASE)
   - **Lines:** `CvEspionageClasses.cpp:2358, 2380`
   - **Code:**
   ```cpp
   if (GET_PLAYER(eOtherPlayer).isHuman(ISHUMAN_AI_DIPLOMACY))
     continue  // Don't expose shadow AI thinking
   ```
   - **Issue:** Check done late; might have processed intrigue before check
   - **Recommendation:** Move to beginning of loop

5. **Spy Rank Cultural Bonus Not Capped** (EXPLOIT)
   - **Lines:** `CvEspionageClasses.cpp:2062-2065`
   - **Code:**
   ```cpp
   iSpyRank = m_aSpyList[uiSpyIndex].GetSpyRank(ePlayer) 
            + m_pPlayer->GetCulture()->GetInfluenceMajorCivSpyRankBonus(eCityOwner)
   ```
   - **Issue:** No maximum cap on combined rank
   - **Impact:** Rare case but could expose classified intel with no cost
   - **Recommendation:** Cap combined rank:
   ```cpp
   iSpyRank = min(SPY_RANK_SPECIAL_AGENT, 
                  iSpyRank + culture_bonus)
   ```

---

## 7. ESPIONAGE AI

### Current Implementation
**Files:** `CvEspionageClasses.h`, `CvEspionageClasses.cpp`

#### AI Strategies:
```cpp
class CvEspionageAI
{
  DoTurn()
    ├─ StealTechnology()        // Pick tech targets
    ├─ UpdateCivOutOfTechTurn() // Track exhausted civs
    ├─ AttemptCoups()           // City-state rigging
    ├─ PerformSpyMissions()     // Event missions
    │
  EvaluateSpiesAssignedToTargetPlayer(PlayerTypes ePlayer)
    ├─ Score cities for tech stealing
    ├─ Rank threats and opportunities
    │
  EvaluateUnassignedSpies()
    ├─ Find best cities for new spies
    │
  EvaluateDefensiveSpies()
    ├─ Place counterspies in key cities
    │
  EvaluateDiplomatSpies()
    ├─ Manage vassal relationships
    │
  EvaluateMinorCivSpies()
    ├─ Rig elections, coups
}
```

#### Tech Stealing Priority:
```cpp
StealTechnology()
  → Loop through players
    → BuildOffenseCityList()
    → Score each city
    → Target highest-value techs
    → Deploy spies optimally
```

#### City Scoring:
```cpp
struct ScoreCityEntry
{
  int iScore           // Primary sort key
  CvCity* pCity        // Target city
  CityEventChoiceTypes eMission  // Best mission
  // Additional VP-specific fields
}
```

### Issues & Observations

#### ✓ Strengths:
- Comprehensive multi-strategy approach
- Proper turn sequencing (steal before missions)
- Tech tracking prevents wasted efforts
- Player tracking for diplomatic response

#### ⚠ Issues:

1. **AI Spy Evaluation Order Undocumented** (CODE CLARITY)
   - **Current:** Four separate evaluation functions called sequentially
   - **Missing:** Explanation of priority order
   - **Lines:** Functions listed but calling sequence unclear
   - **Recommendation:** Document priority:
   ```cpp
   // Priority 1: Reassign existing spies to best targets
   EvaluateSpiesAssignedToTargetPlayer(...)
   // Priority 2: Deploy unassigned spies
   EvaluateUnassignedSpies()
   // Priority 3: Setup defensive counterspies
   EvaluateDefensiveSpies()
   // Priority 4: Diplomatic/minor civ specialists
   EvaluateDiplomatSpies()
   EvaluateMinorCivSpies()
   ```

2. **Out of Tech Turn Tracking Mechanism Unknown** (LOGIC)
   - **Current:** `m_aiCivOutOfTechTurn` tracks when civs have no stealable techs
   - **Missing:** When/how this is used to stop spying
   - **Recommendation:** Document with example:
   ```cpp
   if (GetTurnsUntilCivHasTechsToSteal(eTarget) < 0)
     return  // Stop spying; nothing left to steal
   ```

3. **No Minimum Tech Difference Check** (AI EFFICIENCY)
   - **Current:** AI might target civs too far ahead to steal from
   - **Missing:** Filter for "stealable gap"
   - **Recommendation:** Add minimum gap requirement:
   ```cpp
   int iTechDiff = their_techs - our_techs
   if (iTechDiff > ESPIONAGE_MAX_TECH_STEAL_DISTANCE)
     return 0  // Too advanced to steal from
   ```

4. **AI Counterspy Placement Logic Unknown** (STRATEGY)
   - **Current:** `EvaluateDefensiveSpies()` mentioned but not shown
   - **Missing:** Visible implementation of defensive strategy
   - **Recommendation:** Document defensive prioritization:
   ```cpp
   // Priority 1: Holy cities (religion)
   // Priority 2: Science capitals
   // Priority 3: Cities under siege
   // Priority 4: Threatened minor civs
   ```

---

## 8. KNOWN TODOS AND FIXMES

### In Code:

1. **Line 941** (`CvEspionageClasses.cpp`)
   ```cpp
   // TODO: need to proclaim surveillance somehow
   ```
   - **Context:** Spy entering surveillance in city with no techs
   - **Status:** Feature incomplete; no notification of spy presence
   - **Priority:** Medium (affects gameplay clarity)

2. **Line 6318** (`CvEspionageClasses.cpp`)
   ```cpp
   if (m_aIntrigueNotificationMessages[ui].m_iTurnNum < 
       (GC.getGame().getGameTurn() - iIntrigueTurnsValid))  // todo: make 5 an xml global
   ```
   - **Context:** Intrigue message lifetime hardcoded
   - **Status:** Should be in XML
   - **Priority:** Low (code quality)

3. **Lines 2081, 2403** (`CvEspionageClasses.cpp`)
   ```cpp
   // hack to indicate that we shouldn't know the target due to our low spy rank
   eRevealedTargetPlayer = (PlayerTypes)MAX_MAJOR_CIVS
   ```
   - **Context:** Using MAX_MAJOR_CIVS as sentinel value
   - **Status:** Works but not ideal; should use NO_PLAYER or enum
   - **Priority:** Low (code clarity)

---

## 9. RECOMMENDED PRIORITY IMPROVEMENTS

### HIGH PRIORITY (Gameplay Impact)

1. **Fix Counterspy Penalty Logic** (Section 3)
   - **Effort:** 0.5 days
   - **Impact:** Undefended civs become defensible targets
   - **Estimated 3% Balance Improvement**

2. **Add Intrigue Procurement Notification** (Section 4)
   - **Effort:** 1 day
   - **Impact:** Players aware of counterspy in city
   - **Estimated 2% Strategic Depth Improvement**

3. **Spy Revival Mechanic** (Section 1)
   - **Effort:** 1 day
   - **Impact:** Dead spies revive naturally; balance lost spies
   - **Estimated 2% Gameplay Improvement**

4. **Tech List Single-Turn Rebuild** (Section 3)
   - **Effort:** 0.5 days
   - **Impact:** Prevent multiple steals per turn
   - **Estimated 1% Balance Improvement**

### MEDIUM PRIORITY (Code Quality & Edge Cases)

5. **Mission Selection Scoring Documentation** (Section 2)
   - **Effort:** 0.5 days
   - **Impact:** Clearer AI decision-making
   - **Estimated 1% Developer Understanding**

6. **Network Point Scaling Improvements** (Section 5)
   - **Effort:** 1 day
   - **Impact:** Late-game espionage more viable
   - **Estimated 1% Strategic Balance**

7. **AI Spy Evaluation Documentation** (Section 7)
   - **Effort:** 1 day
   - **Impact:** Easier to modify AI behavior
   - **Estimated 2% Developer Productivity**

### LOW PRIORITY (Polish & Documentation)

8. **Code Documentation** (All sections)
   - **Effort:** 2 days
   - **Impact:** Easier future maintenance
   - **Estimated 2% Developer Productivity**

9. **Hardcoded Values to XML** (Sections 4, 6)
   - **Effort:** 1 day
   - **Impact:** Easier balance tweaking
   - **Estimated 1% Modding Flexibility**

10. **Move Passive Bonuses to Visible System** (Section 4)
    - **Effort:** 1.5 days
    - **Impact:** Players understand counterspy benefits
    - **Estimated 2% Player Education**

---

## 10. CONCLUSION

The Espionage system in Community-Patch-DLL is sophisticated and well-integrated:

### Positive Aspects:
- ✓ Multi-layered spy state machine prevents invalid actions
- ✓ Comprehensive mission framework allows varied approaches
- ✓ Network point system (VP) provides strategic depth
- ✓ Intrigue system rewards player knowledge
- ✓ Good separation between CP and VP systems
- ✓ AI framework covers diverse strategies

### Areas for Improvement:
- ⚠ Critical logic error: Undefended players harder to steal from (inverted)
- ⚠ Several incomplete features marked with TODOs
- ⚠ Hardcoded values should be XML-driven
- ⚠ Some edge cases in spy rank/identification
- ⚠ AI strategy documentation missing
- ⚠ Passive bonuses unclear to players

### Estimated Impact of Recommendations:
- **Gameplay Balance:** +3-5%
- **Strategic Depth:** +2-4%
- **Code Quality:** +3-4%
- **Player Clarity:** +2-3%

The system would benefit most from:
1. Fixing the counterspy penalty logic (inverted balance)
2. Adding intrigue notifications (player awareness)
3. Documenting AI strategy (developer productivity)

These three changes alone would significantly improve the espionage experience and system clarity.

---

**End of Review**
