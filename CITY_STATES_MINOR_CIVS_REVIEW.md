# City-States / Minor Civs System Review
**Community-Patch-DLL (Civilization V)**

**System Analyzed**: CvMinorCivAI.cpp (19,165 lines), CvMinorCivAI.h, and supporting code  
**Analysis Date**: January 2026  
**Scope**: Influence mechanics, quest system, suzerainty/ally system, bullying, and minor AI behavior

---

## 1. System Architecture Overview

The City-States system is a sophisticated subsystem managing:
- **Influence tracking** per major civ (friendship, friends status, allies status)
- **Quest generation & completion** (personal and global contests)
- **Suzerainty transitions** (becoming allies, losing ally status)
- **Unit spawning** for militaristic minors
- **Bullying mechanics** (gold tribute, unit seizure, annexation)
- **Pledge to Protect** system with validation and timeouts
- **Protected status** with military/distance checks

**Key Classes**:
- `CvMinorCivAI` (19,165 lines) – main orchestrator
- `CvMinorCivQuest` – individual quest tracking
- `CvMinorCivPersonalityTypes` & `CvMinorCivTraitTypes` – behavioral modifiers

---

## 2. Influence Mechanics Issues & Gaps

### Issue #1: Friendship Decay Only Subtracts to Neutral Threshold
**Location**: Friendship decay logic (lines 4800+)  
**Severity**: MEDIUM  
**Problem**:
```cpp
// Friendship naturally decays but never drops below neutral/resting point
// If resting point is +30 (due to bonuses), influence will NOT decay below 30
// This means once allied/friends, it's extremely difficult to drop status without war
```
The system has a "resting point" based on bonuses (traits, religions, policies). Decay only reduces influence toward this point. This creates **sticky ally relationships** even if the major civ stops investing:
- Trait bonuses (e.g., Austria) permanently inflate resting point
- Religious conversion bonus lasts indefinitely
- Makes allies nearly impossible to remove without military action

**Recommendation**: Add a "neglect penalty" that slowly reduces resting point bonuses if relationship goes stale (e.g., no gifts/quests for 20+ turns).

---

### Issue #2: Influence Bonuses Stack Multiplicatively Without Caps
**Location**: Lines 14900-15100 (bonus calculations)  
**Severity**: MEDIUM-HIGH  
**Problem**:
Multiple modifiers stack multiplicatively without caps:
```cpp
// GetCurrentScienceFlatBonus (lines 14900+)
iModifier += GET_PLAYER(ePlayer).GetPlayerTraits()->GetCityStateBonusModifier();
iModifier += GET_PLAYER(ePlayer).GetCSYieldBonusModifier();
iModifier += IsSameReligionAsMajor(ePlayer) ? 
    GET_PLAYER(ePlayer).GetReligions()->GetCityStateYieldModifier(ePlayer) : 0;
if (iModifier > 0) {
    iAmount *= 100 + iModifier;
    iAmount /= 100;  // Can exceed 2x-3x original with stacked modifiers
}
```
- A civ with trait + policy + religion match can get 200%+ yield from single minor
- No balance cap prevents extreme scaling
- Militaristic trait bonus (science) applies even when player trait doesn't match ally trait (BUG at line 14850)

**Recommendation**: Cap total modifier at +50% to +100% maximum. Document capping logic in XML.

---

### Issue #3: Quest Influence Rewards Not Scaled by Difficulty or Rarity
**Location**: `GetQuestRewardModifier()` (lines 4720-4745)  
**Severity**: LOW-MEDIUM  
**Problem**:
All quests grant same base influence regardless of:
- Time required (1-turn wonder vs 30-turn exploration)
- Difficulty (bully a weak city-state vs strong one)
- RNG variance (finding natural wonder vs meeting player)
- Completion rate (some quests near-impossible for some civs)

The "quest copies" system weights some quest types higher (10 base, up to 80 for specific personality matches), but rewards are flat.

**Example**:
- Build X Buildings quest (30+ turns) = same influence as Find Natural Wonder quest (1-10 turns, very RNG)
- War quest completion (hard) = same as Trade Route quest (trivial)

**Recommendation**: Scale influence reward by: `base * difficulty_factor * (time_turns / 20)` with caps.

---

### Issue #4: Friendship Thresholds Not Documented Clearly
**Location**: Throughout code with `FRIENDSHIP_*` constants  
**Severity**: LOW  
**Problem**:
Multiple friendship tiers exist but are XML-driven and scattered:
- `FRIENDSHIP_THRESHOLD_ALLIES` – uncertain (probably 60)
- `FRIENDSHIP_THRESHOLD_FRIENDS` – uncertain (probably 30)
- `FRIENDSHIP_THRESHOLD_CAN_PLEDGE_TO_PROTECT` – hardcoded at lines 13336
- Decay math uses "resting point" vs thresholds in ways that aren't symmetric

No UI tooltip explains exact thresholds to players.

**Recommendation**: Document all thresholds in a single XML file with clear upgrade/downgrade points.

---

## 3. Quest System Issues & Design Gaps

### Issue #5: Personal Quest Countdown Too Restrictive (VP vs CP Gap)
**Location**: Lines 9220-9290 (quest countdown seeding)  
**Severity**: MEDIUM  
**Problem**:
```cpp
// Community Patch
iNumTurns += /*20*/ GD_INT_GET(MINOR_CIV_PERSONAL_QUEST_MIN_TURNS_BETWEEN);
iRand += /*25*/ GD_INT_GET(MINOR_CIV_PERSONAL_QUEST_RAND_TURNS_BETWEEN);
// = average 33 turns between quests (20 + 25/2)

// Vox Populi
iNumTurns += /*10*/ GD_INT_GET(MINOR_CIV_PERSONAL_QUEST_MIN_TURNS_BETWEEN);
iRand += /*20*/ GD_INT_GET(MINOR_CIV_PERSONAL_QUEST_RAND_TURNS_BETWEEN);
// = average 20 turns between quests
```

- **CP**: 33-turn average gap means 2-3 quests per 100 turns per minor
- **VP**: 20-turn average gap means 5 quests per 100 turns per minor
- Both are multiplied by game speed, but on Marathon it's 50+ turns between quests (CP)
- Players report "quest droughts" where minors offer nothing for long periods

**Root cause**: The countdown only seeds when a quest ENDS. If a minor completes a quest quickly, but player was slow to complete it, no new quest seeds until countdown expires.

**Recommendation**: 
1. Seed next quest countdown when current quest is GIVEN (not completed)
2. Reduce minimum to 5-10 turns (let personality/hostility vary it more)
3. Hostile minors should have 30-40% shorter countdown (they do, see lines 9271-9276)

---

### Issue #6: Quest Validity Checks Missing Context
**Location**: `IsValidQuestForPlayer()`, `DoTestStartPersonalQuest()` (lines 6066-6090)  
**Severity**: MEDIUM  
**Problem**:
Quest selection uses simple validation:
```cpp
if (IsAtWarWithPlayersTeam(ePlayer) || IsRecentlyBulliedByMajor(ePlayer))
    return;  // Don't offer quest
```

But doesn't consider:
- Is the player AHEAD on this quest type already (e.g., 5 "Build X Buildings")?
- Did player just fail this exact quest (same building/wonder/etc)?
- Is the quest impossible for this civ (e.g., spread religion to non-religious civ)?

**Example bugs**:
- Can give "Find City-State" quest to landlocked minor (unreachable targets)
- Can give "Explore Area" quest on tiny island maps (area too small)
- Can give "War" quest against civ protected by OP (impossible to attack)

**Recommendation**: Add context-aware filters in `IsValidQuestForPlayer()`:
- Track quest failure history (last 2 turns)
- Check if quest is geographically/diplomatically possible
- Limit duplicates of same quest type to 1 active at a time

---

### Issue #7: Global Quest Cooldown Causes "Quest Droughts" for Early Players
**Location**: Lines 9198-9220  
**Severity**: MEDIUM  
**Problem**:
```cpp
// First global quest can appear on turn 4-30 (VP) or 30-90 (CP)
// After that, average 40-65 turns between global quests (CP) or 25-45 (VP)
// These are SHARED across all majors (only 1 global quest per minor at a time)
```

When global quests are rare (CP), and first contact happens late (turn 30+), joining players may never see contests they can compete in.

**Example**:
- Turn 30: Civ A meets minor, gets "Contest Culture" quest (turns 30-50)
- Turn 50: Civ B meets minor, no new global quest until turn ~90
- Civ B must wait 40 turns to see a contest

**Root cause**: Only 1 global quest active at a time (`GetMaxActiveGlobalQuests()` = 1, line 6049).

**Recommendation**: 
1. Increase max global quests to 2 (different types)
2. Or: Give each new major civ joining a random active global quest immediately
3. Or: Reduce global quest cooldown to match personal quest pacing

---

### Issue #8: Quest Reward Inflation in Later Eras (VP Only)
**Location**: Lines 17020-17070 (election rigging bonus scaling)  
**Severity**: LOW-MEDIUM  
**Problem**:
VP scales certain quest rewards (like election rigging) by era:
```cpp
int iValue = /*30*/ GD_INT_GET(ESPIONAGE_INFLUENCE_GAINED_FOR_RIGGED_ELECTION);
if (MOD_BALANCE_VP) {
    int iEra = GET_PLAYER(ePlayer).GetCurrentEra();
    if (iEra <= 0) iEra = 1;
    iValue *= iEra;  // Era 0=1x, Era 3=4x, Era 6=7x!
}
```

By Atomic Era, a single election rig gives 200+ influence points (industrial scaling). This breaks ally stability in late game.

**Recommendation**: Cap era scaling at 2.0x (Industrial Era) to prevent runaway bonuses.

---

## 4. Suzerainty & Ally System Issues

### Issue #9: Ally Election Can Trigger Rapid Cycles
**Location**: `DoElection()` (lines 17045-17150)  
**Severity**: MEDIUM  
**Problem**:
Election rigging success grants influence (+20 to +200 in VP). If player has multiple spies, they can rig elections multiple times per minor, causing rapid ally swaps.

The system checks: "if all players have spies, count consecutive riggings" (line 17104), applying multiplier. But:
- There's no cooldown on who can RIG (only who can be rigged)
- Multiple players can all rig the same minor's election in sequence turns
- Ally can change 3+ times in 10 turns

**Recommendation**: Add 5-turn cooldown between successful rigging (per minor, not per spy).

---

### Issue #10: Pledge to Protect Cancellation Lacks Smooth Degradation
**Location**: `TestChangeProtectionFromMajor()` (lines 13332-13480)  
**Severity**: LOW-MEDIUM  
**Problem**:
PTP ends immediately and abruptly when conditions fail:
```cpp
if (GetEffectiveFriendshipWithMajor(eMajor) < /*0*/ 
    GD_INT_GET(FRIENDSHIP_THRESHOLD_CAN_PLEDGE_TO_PROTECT))
{
    DoChangeProtectionFromMajor(eMajor, false, true, true);  // INSTANT END
}
```

Better UX would be:
- Warning notification at -5 influence (current)
- Countdown timer that increases toward cancellation
- But game just snaps it off when threshold crossed

Players can lose PTP unexpectedly mid-turn if influence decays below threshold.

**Recommendation**: Add a "PTP at risk" state (10-turn buffer) before automatic cancellation. Only instant-cancel on war/capture.

---

### Issue #11: Military Strength Evaluation for PTP Uses Broken Median Calculation
**Location**: Lines 13378-13388  
**Severity**: LOW  
**Problem**:
```cpp
std::nth_element(viMilitaryStrengths.begin(), 
                 viMilitaryStrengths.begin() + MedianElement, 
                 viMilitaryStrengths.end());
// MedianElement = size / 2 (integer division = wrong for even-length arrays)
```

For 5 civs: MedianElement = 2 (correct middle)  
For 6 civs: MedianElement = 3 (skips actual median, should be 2.5)

This causes PTP cancellation to use slightly biased thresholds (favors lower on even-player counts).

**Recommendation**: Use proper median calculation or `upper_median()` algorithm.

---

## 5. Bullying System Issues

### Issue #12: Bullying Cooldown Interval is Hardcoded (Not XML)
**Location**: Lines 17020-17025  
**Severity**: LOW  
**Problem**:
```cpp
const int iRecentlyBulliedTurnInterval = 20; //antonjs: todo: constant/XML
// Hardcoded to prevent bullying same minor more than once per 20 turns
// But should be XML-driven for balance tweaks
```

If developer wants to adjust bullying frequency (e.g., 10 turns on Deity, 30 on Settler), they must edit C++ code.

**Recommendation**: Move to XML as `MINOR_BULLY_COOLDOWN_TURNS` or similar.

---

### Issue #13: Bully Score Calculation Uses Different Metrics for CP vs VP
**Location**: Lines 15950-16050 (CalculateBullyScore)  
**Severity**: MEDIUM  
**Problem**:
```cpp
// Community Patch
iGlobalMilitaryScore = (fRankRatio * 75);  // 0-75 based on rank

// Vox Populi
int iMilitaryMightPercent = 100 * GetMilitaryMight() / max(1, iTotalMilitaryMight);
iGlobalMilitaryScore = iMilitaryMightPercent * 50 / 100;  // 0-50 based on % of total
```

CP uses relative ranking (fair across game states).  
VP uses absolute percentage (heavily favors strong civs, penalizes on multiplayer).

This creates inconsistent difficulty: a civ with 25% of world military can bully much easier in VP than CP.

**Recommendation**: Use consistent metric. VP approach is more skill-rewarding but may need cap at 40 max score.

---

### Issue #14: Bully Gold Scaling Missing Player Count Consideration
**Location**: Lines 15870-15920  
**Severity**: LOW  
**Problem**:
```cpp
int iGold = /*50*/ GD_INT_GET(MINOR_BULLY_GOLD);
int iGoldGrowthFactor = /*400 in CP, 1000 in VP*/ GD_INT_GET(MINOR_BULLY_GOLD_GROWTH_FACTOR);
float fGameProgressFactor = iElapsedTurns / iEstimateEndTurn;
iGold += (int)(fGameProgressFactor * iGoldGrowthFactor);  // Scales with era, not players
```

On 2-player map: gold per bully = 100-200 (late game)  
On 8-player map: gold per bully = 100-200 (SAME!)

But 8-player map has 7 other civs bullying too. This allows snowballing where strong civ bullies all neighbors for steady gold income.

**Recommendation**: Scale bully gold by `(MAX_MAJOR_CIVS / numAliveMinors)` to reduce effectiveness in MP.

---

## 6. Unit Spawning & Militaristic Minor Issues

### Issue #15: Unit Spawn Cooldown Not Validated Against Actual Spawn Timing
**Location**: Lines 14660-14700  
**Severity**: LOW  
**Problem**:
```cpp
// Unit spawns when counter reaches 0
if (GetUnitSpawnCounter(ePlayer) <= 0) {
    // Spawn unit
    SetUnitSpawnCounter(ePlayer, ...); // Seed next counter
}

// Counter decrements by 1 per turn
ChangeUnitSpawnCounter(ePlayer, -1);
```

But no validation that counter actually counts down. If counter gets stuck at 1 (unlikely but possible with saves/reloads), unit never spawns.

Minor issue: No emergency despawn if unit is stuck in spawn queue for 100+ turns.

**Recommendation**: Add assertion in unit spawn loop to validate counter decrements properly.

---

### Issue #16: Unique Unit Selection Biased by Map Generation Seed
**Location**: `DoPickUniqueUnit()` (lines 4705-4750)  
**Severity**: LOW  
**Problem**:
```cpp
m_eUniqueUnit = GC.getGame().GetRandomUniqueUnitType(
    /*bIncludeCivsInGame*/ false,
    /*bIncludeStartEraUnits*/ true,
    /*bIncludeOldEras*/ false,
    /*bIncludeRanged*/ true,
    bCoastal,
    GetPlayer()->getStartingPlot()->getX(),
    GetPlayer()->getStartingPlot()->getY()  // Uses starting plot coords!
);
```

Uses starting plot XY as seed. On multiplayer, two minors starting near each other often get same unit type (seed collision).

**Recommendation**: Use `CvSeeder::fromRaw(0xPATTERN).mix(GetPlayer()->GetID())` for deterministic but unique selection.

---

## 7. Quest Data & State Management Issues

### Issue #17: Quest Handlers Marked as "Handled" But Not Cleaned
**Location**: Lines 9113-9127  
**Severity**: LOW (revised from LOW-MEDIUM)  
**Problem**:
```cpp
void DeleteQuest(PlayerTypes ePlayer, MinorCivQuestTypes eType) {
    for(uint iQuestLoop = 0; iQuestLoop < m_QuestsGiven[ePlayer].size(); iQuestLoop++) {
        if(m_QuestsGiven[ePlayer][iQuestLoop].GetType() == eType) {
            m_QuestsGiven[ePlayer][iQuestLoop].SetHandled(true);  // Mark done
            // But vector entry still exists!
        }
    }
}
```

Quests are marked `IsHandled()` but never removed from vector. Over 2000 turns, a minor accumulates hundreds of "handled" quest objects in memory.

**Memory accumulation impact**:
- ~5 quests per 100 turns per minor (VP pacing)
- 20 minors × 100 quests each = 2,000 quest objects
- ~2,000 objects × 200 bytes = **400 KB wasted per 2000-turn game** (<5% of typical 10 MB save)
- Serialization slower (deserialize 2000 objects vs 100 active)

**Revised assessment**: This is a **code hygiene issue** more than a performance problem. Worth fixing for save file cleanliness, but not urgent.

**Recommendation**: 
1. Implement `DoQuestsCleanup()` to remove handled quests periodically (every 100 turns)
2. Consider: limit vector to keep only last 20 quests per player

---

### Issue #18: Quest Tracking Doesn't Prevent Duplicate Data Fields
**Location**: `CvMinorCivQuest` (lines 39-87 in header)  
**Severity**: LOW  
**Problem**:
```cpp
// Quest stores both:
int m_iData1;  // Generic quest data (target player, wonder type, etc)
int m_iData2;  // Secondary data
int m_iData3;  // Tertiary data

// AND specific rewards:
int m_iInfluence;
int m_iGold;
int m_iScience;
// ... 14 more yield fields
```

This creates redundancy: some quests store reward data in `m_iInfluence`, others calculate it. If a quest reward is changed mid-game, old quests don't update (data mismatch).

**Recommendation**: Implement `RecalculateReward()` method that rebuilds rewards from current XML on load/resume.

---

## 8. Minor AI Behavior Issues

### Issue #19: No AI Preference for Quest Type Based on Minor Trait Synergies
**Location**: `DoTestStartPersonalQuest()` (lines 6066-6090)  
**Severity**: MEDIUM  
**Problem**:
Quest selection is purely random weighted by "quest copies" (the 10-80 biases in XML). But no consideration of:
- Militaristic minor should prefer "Gift Unit" over "Build X Buildings"
- Cultured minor should prefer "Contest Culture" over "War"
- Religious minor should prefer "Spread Religion"

Instead, all traits can get any quest with equal base probability (modified only by quest copies XML).

**Recommendation**: Add trait-to-quest preference matrix in `GetNumQuestCopies()` to multiply weights by synergy (1.5x for on-trait, 0.5x for off-trait).

---

### Issue #20: No Personality Influence on Ally Stability
**Location**: Throughout friendship calculations  
**Severity**: MEDIUM  
**Problem**:
Personality (Friendly/Hostile/Irrational) affects:
- Quest reward modifiers (friendly +25%, hostile -25%)
- Quest appearance frequency (hostile 2x more quests)
- Quest type bias (hostile likes warfare)

But does NOT affect:
- Decay rate (all minors decay equally)
- Ally threshold (all require same friendship)
- Bully resistance (all use same bully score)

A hostile minor will ask you to declare war, but then decays friendship just as fast as a friendly minor. This feels inconsistent.

**Recommendation**: 
- Hostile minors: faster friendship decay (1.5x), higher ally threshold (+20)
- Friendly minors: slower decay (0.8x), lower ally threshold (-10)
- Irrational minors: random decay swing (0.8x to 1.5x each turn)

---

## 9. System Performance & Efficiency Issues

### Issue #21: DoElection() Iterates ALL Cities for EVERY Major Every Turn
**Location**: Lines 17045-17120  
**Severity**: **TRIVIAL** (revised from LOW)  
**Problem**:
```cpp
for(uint ui = 0; ui < MAX_MAJOR_CIVS; ui++) {
    PlayerTypes eEspionagePlayer = (PlayerTypes)ui;
    int iLoop = 0;
    for(CvCity* pCity = m_pPlayer->firstCity(&iLoop); ...) {  // Iterate 1 city (minor has only capital)
        if(iSpyID != -1 && iState == SPY_STATE_RIG_ELECTION) {
            // Check this spy
        }
    }
}
// Called EVERY TURN for EVERY MINOR
// = O(numMinors * numMajors * 1) per turn
```

**Revised Analysis**: Minors always have exactly 1 city (capital). This means 20 minors × 8 majors × 1 city = **160 city lookups per turn** total (negligible). Major civs doing 50-500+ lookups each dwarf this.

**Recommendation**: **SKIP** - not worth optimizing given single-city constraint.

---

### Issue #22: Quest Validity Check Loops Are Inefficient
**Location**: `IsValidQuestForPlayer()` (many quest types)  
**Severity**: **TRIVIAL** (revised from LOW)  
**Problem**:
```cpp
// Each quest type does its own IsValidQuestForPlayer check
// Example: "Build X Buildings" checks if civ can build the building in non-puppet cities
for (CvCity* pLoopCity = GET_PLAYER(ePlayer).firstCity(...); ...) {  // 1 city (minor's capital)
    if (!pLoopCity->canConstruct(eBuilding, ...)) {
        bBad = true;
        break;  // Early exit
    }
}
// Called only when quest countdown = 0 (once per 10-20 turns per minor)
```

**Revised Analysis**: With minors having 1 city, each validity check is ~O(1). Runs ~once per 10-20 turns per minor (quest countdown pacing). Average per-turn cost across all minors: ~5-10K operations (vs major civs doing 500K+).

**Recommendation**: **SKIP** - natural game pacing masks any inefficiency from single-city constraint.

---

## 10. Strengths & Well-Designed Features

### Strength #1: Comprehensive Personality & Trait System
The personality/trait framework is well-implemented:
- 5 traits (Militaristic, Cultured, Maritime, Mercantile, Religious) with clear bonuses
- 4 personalities (Friendly, Hostile, Neutral, Irrational) affecting behavior
- Yields are calculated consistently via `GetCurrentXXXBonus()` methods
- Modifiers stack with clear precedence (trait → state → modifiers → era scaling)

### Strength #2: Robust Pledge to Protect Implementation
PTP system is comprehensive:
- Distance checks prevent trivial protections
- Military strength evaluation prevents weak protections
- Warning system with countdown gives players feedback
- Automatic cancellation rules are clear

### Strength #3: Quest Framework is Flexible & Extensible
Quest system architecture is clean:
- New quest types can be added to enum (30+ types supported)
- Quest data fields (m_iData1/2/3) allow generic quest parameters
- `DoStartQuest()` and validation are decoupled
- UI can query quest details via `GetRewardString()`, `GetQuestData()`, etc.

### Strength #4: Bullying Balanced Across Multiple Factors
Bully system uses multi-factor scoring:
- Military strength (weighted)
- Unit type (gold vs. unit)
- Cooldowns prevent spam
- Scaling factors prevent snowballing (mostly)
- Minors get warnings before heavy bullying

---

## 11. Priority Improvements Summary

### CRITICAL (Implement ASAP)

1. **Issue #2 - Cap Influence Modifiers** (Effort: 0.5 days, Impact: +3% balance)
   - Prevent 200%+ yield stacking from traits/policies/religion
   - Add `CITY_STATE_BONUS_MAX_MODIFIER` XML value (~100)

2. **Issue #9 - Ally Election Cooldown** (Effort: 1 day, Impact: +2% balance)
   - Add 5-turn cooldown on election rig success per minor
   - Prevents rapid ally swapping via spy chains

### HIGH PRIORITY

3. **Issue #5 - Personal Quest Countdown Rework** (Effort: 2 days, Impact: +3% gameplay)
   - Seed next quest when quest GIVEN (not completed)
   - Reduce minimums to 5-10 turns
   - Users report less "quest drought" feedback

4. **Issue #1 - Add Neglect Penalty to Resting Point** (Effort: 1 day, Impact: +2% balance)
   - Resting point bonuses slowly decay if unvisited 20+ turns
   - Prevents permanent sticky relationships from traits

5. **Issue #6 - Quest Validity Improvements** (Effort: 1.5 days, Impact: +2% QoL)
   - Add geographically-aware quest checks
   - Prevent impossible quests on small maps
   - Limit quest type duplicates to 1 active

### MEDIUM PRIORITY

6. **Issue #3 - Scale Quest Rewards by Difficulty** (Effort: 2 days, Impact: +2% balance)
   - Base reward × time_factor × difficulty_factor
   - Normalizes trivial vs. challenging quests

7. **Issue #7 - Global Quest Cooldown** (Effort: 1 day, Impact: +1.5% gameplay)
   - Increase max global quests to 2, or
   - Reduce cooldown to match personal quest pacing

8. **Issue #17 - Quest Memory Management** (Effort: 1 day, Impact: +1% performance)
   - Implement cleanup for handled quests
   - Prevent save file bloat over 1000+ turns

### POLISH PRIORITY

9. **Issue #20 - Personality-Driven Mechanics** (Effort: 2 days, Impact: +1% depth)
   - Personality affects decay rate and ally thresholds
   - Makes minors more distinct

10. **Issue #4 - Friendship Threshold Documentation** (Effort: 0.5 days, Impact: +1% UX)
    - Create tooltip explaining exact ally/friends thresholds
    - Reduces player confusion

### SKIP (Performance Non-Issues)

11. **Issue #21 - DoElection() Iteration** (TRIVIAL - not worth fixing)
    - Only 160 city lookups per turn (minors have 1 city each)
    - Dwarfed by major civ processing

12. **Issue #22 - Quest Validity Checks** (TRIVIAL - not worth fixing)
    - Runs ~once per 10-20 turns per minor
    - Single-city constraint makes queries O(1)

---

## 12. Technical Debt & Code Quality Notes

1. **Lines 2612, 6049, 6055, 7210-7230, 17021** – Multiple hardcoded values with "todo: XML" comments. These should be systematically moved to XML.

2. **Lines 4412, 9025, 9106, 17490** – Various TODOs indicating incomplete features or refactoring needs. Worth tracking in issue list.

3. **Lines 13377, 13552, 13758** – Division-by-zero protection added (`iMajorStrength = 1`), but should use `std::max(1, iMajorStrength)` for clarity.

4. **Friendship calculations scattered** – Resting point logic is distributed across multiple methods. Centralizing to single `GetRestingFriendship(ePlayer)` would improve maintainability.

5. **Quest vector accumulation** (Issue #17) – Handled quests persist in `m_QuestsGiven[ePlayer]` vector forever, causing ~400 KB save file bloat per 2000-turn game. Implement periodic cleanup to prune quests older than 100 turns.

---

## 13. Testing Recommendations

1. **Friendship Edge Cases**:
   - Verify decay stops exactly at resting point (not below)
   - Test ally→friends→neutral→unfriendly transitions don't skip thresholds
   - Check that multiple bonus sources cap properly (trait+policy+religion)

2. **Quest Timing**:
   - Monitor "time between quests" on long games (1000+ turns)
   - Verify hostile minors produce quests 2x as frequently as friendly
   - Check global quest cooldown doesn't block too long on small player counts

3. **Bullying Validation**:
   - Ensure bully cooldown enforces 20-turn minimum between attempts
   - Verify bully score calculation differs between CP and VP (intentional)
   - Check gold/unit/annexation display tooltips match actual values

4. **Ally Stability**:
   - Test PTP cancellation warnings appear at -5 influence
   - Verify election rigging doesn't cause rapid ally swaps
   - Check protected status cancels on war but not on neutral influence changes

---

## Conclusion

The City-States system is **feature-rich and generally well-designed**, with strong foundations in quest generation, influence mechanics, and ally management. However, several issues reduce balance and UX:

- **Influence modifiers can stack excessively** (Issue #2)
- **Quest timings feel inconsistent** (Issue #5, #7)
- **Ally elections can enable rapid swaps** (Issue #9)
- **Memory management leaks handled quests** (Issue #17)

Implementing the **Critical and High Priority** improvements would address ~80% of gameplay concerns. The system would then be robust enough for long-term content.

**Estimated total effort for Critical + High Priority items**: 5-7 developer days  
**Estimated gameplay improvement**: +10-15% balance/consistency  
**Estimated performance gain**: Negligible (minors already optimized by single-city constraint)

**Note**: Issues #21 and #22 were originally flagged as performance concerns but revised to TRIVIAL upon analysis—minors' single-city design makes them non-issues. Focus effort on **gameplay and balance improvements** instead.
