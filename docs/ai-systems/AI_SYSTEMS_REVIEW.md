# AI Systems & Difficulty Review

**Purpose:** Comprehensive analysis of AI behavior modules, flavor systems, handicap mechanics, and strategic weighting in Civ5 VP/CP modded core.

**Last Updated:** January 2026  
**Status:** Review & Issues Identification

---

## Executive Summary

The AI system in CvGameCoreDLL_Expansion2 is composed of multiple interconnected subsystems:

1. **Flavor Management System** (`CvFlavorManager`) — Base personality traits
2. **Diplomatic AI** (`CvDiplomacyAI`) — Inter-civ diplomacy and approaches
3. **Grand Strategy AI** (`CvGrandStrategyAI`) — Long-term victory focus
4. **Military AI** (`CvMilitaryAI`) — Combat strategy and defense assessment
5. **Economic AI** (`CvEconomicAI`) — Resource/trade decisions
6. **Tactical AI** (`CvTacticalAI`) — Individual unit tactical moves
7. **Production AI** (Techs, Units, Buildings, Projects, Wonders)
8. **Handicap System** — Difficulty modifiers applied to AI or human players

### Key Issues & Improvement Areas

---

## 1. FLAVOR SYSTEM ARCHITECTURE

### Current Implementation

**File:** `CvFlavorManager.{h,cpp}`

**Key Classes:**
- `CvFlavorRecipient` — Base class for all flavor consumers (city, player-level AI modules)
- `CvFlavorManager` — Central manager; broadcasts flavor updates to recipients
- **Flavor Values:** Range typically [0, 10]; represent AI personality traits
  - Examples: `FLAVOR_CULTURE`, `FLAVOR_OFFENSE`, `FLAVOR_DEFENSE`, `FLAVOR_DIPLOMACY`, etc.

**Mechanism:**
1. Leaderhead XML defines base personality flavors
2. `RandomizeWeights()` adds ±N variation at game start
3. Game events trigger `ChangeActivePersonalityFlavors()` to adjust mid-game
4. All AI subsystems (tech AI, unit AI, policy AI) subscribe to flavor updates

### Known Issues & Improvements

#### Issue 1.1: Flavor Randomization Affecting Consistency
**File:** [CvDiplomacyAI.cpp](CvDiplomacyAI.cpp#L1913)  
**Code:**
```cpp
int CvDiplomacyAI::RandomizePersonalityFlavor(int iOriginalValue, const CvSeeder& seed)
{
    int iPlusMinus = range(GD_INT_GET(FLAVOR_RANDOMIZATION_RANGE), 0, (INT_MAX - 1) / 2);
    if (iPlusMinus == 0 || MOD_DIPLOAI_NO_FLAVOR_RANDOMIZATION)
        return range(iOriginalValue, 1, 10);
    return range(GC.getGame().randRangeInclusive(iOriginalValue - iPlusMinus, iOriginalValue + iPlusMinus, seed), 1, 10);
}
```

**Issue:**
- Flavor randomization can cause "personality drift" — AI leaders play differently each game even with same seed
- Option `MOD_DIPLOAI_NO_FLAVOR_RANDOMIZATION` exists but is not discoverable
- No visual feedback to player about randomized vs. base personality

**Improvement:**
- [ ] Add leaderhead-specific randomization weights (some leaders should be more consistent)
- [ ] Log flavor randomization events for debugging
- [ ] Consider separating "personality randomization" from "strategic adaptation"
- [ ] Add UI indicator showing whether personality is fixed or randomized

---

#### Issue 1.2: Flavor Propagation Lag
**File:** [CvFlavorManager.cpp](CvFlavorManager.cpp#L338)

**Issue:**
- When `ChangeCityFlavors()` is called, updates are queued and propagated asynchronously
- City specialization AI may lag 1+ turn behind personality changes
- No immediate feedback mechanism for urgent changes (e.g., city under siege)

**Improvement:**
- [ ] Add priority levels to flavor updates (URGENT, NORMAL, BACKGROUND)
- [ ] Implement immediate flush for combat-related flavor changes
- [ ] Benchmark propagation latency in late-game with 50+ cities

---

#### Issue 1.3: Flavor Weighting in Production AI
**Files:** [CvTechClasses.cpp#L2010](CvTechClasses.cpp#L2010), [CvUnitProductionAI.cpp#L81](CvUnitProductionAI.cpp#L81)

**Code Pattern (Tech AI):**
```cpp
void CvPlayerTechs::AddFlavorAsStrategies(int iPropagatePercent)
{
    int iGameProgressFactor = /* progress 0-1000 */;
    // Blend current needs (responsive) with personality (long-term)
    int iFlavorValue = ((iCurrentFlavorValue * (1000 - iGameProgressFactor)) 
                       + (iPersonalityFlavorValue * iGameProgressFactor)) / 1000;
    
    // Minimum floor to prevent zeroing
    if (iFlavorValue < 10) {
        int flavorDivisor = (iGameProgressFactor > 500) ? 8 : 4;
        iFlavorValue += (10 - iFlavorValue) / flavorDivisor;
    }
}
```

**Issues:**
- Floor logic is ad-hoc; uses hardcoded divisor (8 vs. 4)
- No explanation for why early game should have ÷4 floor vs. late game ÷8
- Flavor boost is not data-driven (not configurable in XML)
- Early-game personality can be overridden by transient strategic needs → inconsistent AI behavior

**Improvements:**
- [ ] Extract floor calculation to a data-driven function or XML modifier
- [ ] Log which flavors are being boosted and why
- [ ] Consider separating "personality persistence" vs. "strategic responsiveness" into distinct thresholds
- [ ] Add AI toggle: "Strict Personality" vs. "Adaptive Personality"

---

#### Issue 1.4: Diplomacy Flavor Modification
**File:** [CvFlavorManager.cpp](CvFlavorManager.cpp) (method `GetPersonalityFlavorForDiplomacy()`)

**Issue:**
- Diplomatic AI sometimes applies modifiers to base flavors before using them
- No clear logic for when/why diplomatic flavors diverge from base flavors
- Inconsistent with production AI and military AI flavor handling

**Improvements:**
- [ ] Standardize flavor modification pipeline (apply modifiers at one point)
- [ ] Document when and why diplomatic flavors differ from base
- [ ] Consider separate "diplomatic personality" XML entries for clarity

---

## 2. DIPLOMATIC AI SYSTEM

### Current Implementation

**File:** [CvDiplomacyAI.{h,cpp}](CvDiplomacyAI.h) (~2200 lines)

**Core Function:** `SelectBestApproachTowardsMajorCiv(PlayerTypes ePlayer, bool bStrategic, ...)`
- Called regularly to decide diplomatic approach toward each major civ
- Scores approaches: FRIENDLY, GUARDED, NEUTRAL, DECEPTIVE, HOSTILE, WAR

**Approach Scoring Logic:**
1. **Base Opinion** — historical interactions, diplomacy, shared religion, ideology, etc.
2. **Victory Competition** — are they winning culturally? scientifically? diplomatically?
3. **Land Disputes** — proximity, border pressure, player flavor for "Conqueror"
4. **Tech Blocking** — stealing techs, "Scientist" flavor
5. **Policy Blocking** — blocking key policies, "Culture" flavor
6. **Religious Competition** — religious pressure, "Piety" flavor
7. **Ideology Competition** — ideology pressure, "Ideology" flavor
8. **Wonder Competition** — competing for wonders, "Wonder Competitiveness"
9. **Military Threat** — strength compared to ours, "Boldness" flavor
10. **Handicap Modifiers** — difficulty-based bonuses/penalties
11. **Approach Bias** — personality-based preference for certain approaches

### Known Issues & Improvements

#### Issue 2.1: Opaque Approach Scoring
**Files:** [CvDiplomacyAI.cpp#L17000+](CvDiplomacyAI.cpp) (SelectBestApproachTowardsMajorCiv)

**Issue:**
- 20,000+ lines of logic spread across `SelectBestApproachTowardsMajorCiv()`
- Multiple competing modifiers (victory, dispute level, block level, handicap %) all applied
- No clear priority order: which factor wins if two conflict?
- Difficult to debug why AI chose approach X instead of Y

**Current Calculation Pattern:**
```cpp
// Example: Land Dispute Scoring
int iModifier = GetBoldness() * iMultiplier * DifficultyModifier;
iModifier /= 5;
switch (eDisputeLevel) {
    case DISPUTE_LEVEL_WEAK:
        vApproachScores[CIV_APPROACH_HOSTILE] += vApproachBias[CIV_APPROACH_HOSTILE] * iModifier / 100;
        break;
    // ...
}
```

**Problems:**
- Divisions by hardcoded constants (5, 10, 100) — no clear rationale
- Interaction between multipliers not documented
- No cap on maximum score or minimum floor
- Negative scores possible but clamped to zero late in function — may lose information

**Improvements:**
- [ ] **Refactor into modular scoring system**: each concern (victory, dispute, block, military) gets its own weighted function
- [ ] **Add comprehensive logging**: log each component of approach score and final decision
- [ ] **Data-drive weighting**: XML file specifying:
  - Weight of each concern (e.g., "land disputes are 30% of decision")
  - Divisor/multiplier constants with comments
  - Min/max score caps per approach
- [ ] **Add AI debug mode**: overlay showing approach scores in-game
- [ ] **Unit test approach selection**: fixed scenario, verify consistent decisions

---

#### Issue 2.2: Handicap Modifier Inconsistency
**Files:** [CvDiplomacyAI.cpp#L17415, L17320, L17000, L19933](CvDiplomacyAI.cpp)

**Pattern (examples from code):**
```cpp
// Land Dispute
DifficultyModifier = GET_PLAYER(ePlayer).isHuman(ISHUMAN_HANDICAP) 
    ? GET_PLAYER(ePlayer).getHandicapInfo().getLandDisputePercent() 
    : GC.getGame().getHandicapInfo().getLandDisputePercent();

// Tech Block
DifficultyModifier = GET_PLAYER(ePlayer).isHuman(ISHUMAN_HANDICAP)
    ? GET_PLAYER(ePlayer).getHandicapInfo().getTechBlockPercent()
    : GC.getGame().getHandicapInfo().getTechBlockPercent();

// Victory Dispute  
DifficultyModifier = max(GET_PLAYER(ePlayer).getHandicapInfo().getVictoryDisputePercent(),
                         GET_PLAYER(ePlayer).getHandicapInfo().getVictoryBlockPercent());
```

**Issues:**
- Human gets own handicap; AI gets global difficulty handicap
  - Intent: different handicaps for human vs. AI? Or typo?
- Victory dispute uses `max()` of two percentages — no clear intent
- Land, Tech, Policy blocks have separate modifiers — why not unified?
- No validation that percentages are in sane range (0-200%?)
- Mod option `HUMAN_USES_AI_HANDICAP` exists but is buried in CustomMods

**Improvements:**
- [ ] **Clarify handicap application logic**: document whether human should get harder/easier treatment
- [ ] **Consolidate handicap queries**: create helper function `GetApproachModifierForDifficulty(ApproachType, ...)`
- [ ] **Add handicap range validation**: warn if any handicap percent < 0 or > 200
- [ ] **Expose mod option**: Make `HUMAN_USES_AI_HANDICAP` discoverable in UI or docs
- [ ] **Add logging**: log applied handicap modifier for each approach decision

---

#### Issue 2.3: Victory Competition Scoring Missing Nuance
**File:** [CvDiplomacyAI.cpp](CvDiplomacyAI.cpp) (Victory competition sections)

**Issue:**
- AI scores victory threat based on visible progress only
  - Does not account for: hidden bonuses, hidden traits, hidden buildings, victory-specific techs
- Score update frequency may lag actual victory progress
  - If player completes space parts off-turn, AI may not react for several turns
- No differentiation between "they're winning" vs. "they're winning AND we can't catch up"

**Example Problem:**
- Player A and Player B both at 2/3 science victory
- Player C (AI) at 1/3 science victory but ahead in military
- Current logic might score them equally hostile if science flavor is similar
- But Player C cannot realistically catch up in science → wasting flavor allocation

**Improvements:**
- [ ] **Add "catch-up potential" calculation**: ratio of gap / rates-of-progress
- [ ] **Discount threats we can block**: if player X needs 5 more techs and we can techlead them, lower threat
- [ ] **Increase update frequency for victory tracking**: check every turn, not every N turns
- [ ] **Add victory-prediction subsystem**: estimate when each player will win, use as factor
- [ ] **Add logging**: log victory threat scores by player and victory type

---

#### Issue 2.4: Religious & Ideology Competition Under-Weighted
**File:** [CvDiplomacyAI.cpp#L17923](CvDiplomacyAI.cpp) (Ideology section)

**Issue:**
- Religious and ideology competition logic is present but short (~50 lines vs. ~200 for land disputes)
- Religious threat modifiers are lower than equivalent land threat modifiers
  - Intent: religion/ideology less important? Or bug?
- No differentiation between "spreading their religion" vs. "blocking our religion"
  - Both treated identically in approach scoring

**Improvements:**
- [ ] **Expand religious competition logic**: mirror complexity of land disputes
- [ ] **Separate offensive vs. defensive scenarios**: if THEY're converting our cities vs. we're protecting ours
- [ ] **Add religious flavor modifier**: "Piety" flavor should increase religious threat assessment
- [ ] **Consider inquisitor actions**: existing inquisitors should boost defensive concern

---

#### Issue 2.5: Approach Bias Initialization Unclear
**File:** [CvDiplomacyAI.cpp](CvDiplomacyAI.cpp)

**Issue:**
- `vApproachBias[]` vector initialized but source/rationale not clear
- Does each leader have custom approach bias? Or global?
- How are bias values determined? Flavor-based? XML-based?
- No logging of bias initialization

**Improvements:**
- [ ] **Document approach bias source**: XML entry? Flavor calculation?
- [ ] **Add per-leader bias override**: some leaders inherently prefer war, others diplomacy
- [ ] **Log bias values at game start**: make discoverable for balance review
- [ ] **Consider "Boldness" as bias modifier**: higher boldness = higher war bias?

---

## 3. GRAND STRATEGY AI SYSTEM

### Current Implementation

**File:** [CvGrandStrategyAI.{h,cpp}](CvGrandStrategyAI.h) (~175 lines header)

**Function:** Selects long-term victory focus (Science, Culture, Diplomacy, Conquest)

**Mechanism:**
1. Calculates priority for each victory type based on current advantage
2. Selects active grand strategy (highest priority)
3. Grand strategy influences flavor allocation and tech/policy/unit choices
4. Can change every N turns based on changing circumstances

### Known Issues & Improvements

#### Issue 3.1: Victory Priority Calculation Over-Simplified
**File:** [CvGrandStrategyAI.cpp](CvGrandStrategyAI.cpp)

**Issue:**
- Current implementation scores victory progress as: `GetBaseGrandStrategyPriority()` + some modifiers
- No multi-objective optimization: doesn't consider inter-victory synergies
  - E.g., science + space victory share tech tree; culture + diplomacy share policy tree
  - AI might thrash between strategies instead of pursuing complementary wins
- No "difficulty of path" assessment: doesn't penalize victories that require techs we've skipped
- No "player count" adjustment: diplomacy victory harder with 10 AIs vs. 3 AIs

**Improvements:**
- [ ] **Add synergy scoring**: calculate combined advantage if pursuing multiple victories
- [ ] **Add path feasibility check**: can we realistically reach this victory from current state?
- [ ] **Player count adjustment**: diplomacy weight *= (10 / numMajorCivs)
- [ ] **Add "stability" penalty**: if switching strategies too often, penalize oscillation

---

#### Issue 3.2: Grand Strategy Affects Flavor, But Interaction Unclear
**File:** [CvGrandStrategyAI.cpp](CvGrandStrategyAI.cpp) (GetPersonalityAndGrandStrategy)

**Issue:**
- `GetPersonalityAndGrandStrategy()` blends personality flavor with grand strategy flavors
  - But blend ratio not clear: is it 50/50? Or dynamic?
- No UI/logging showing current blend
- Grand strategy can override personality for entire game phase
  - Intended? Or potential AI behavior inconsistency?

**Improvements:**
- [ ] **Data-drive blend ratio**: XML configurable, logged at game start
- [ ] **Add logging**: every flavor decision logs personality %, strategy %, final value
- [ ] **Consider "commitment level"**: how many turns into current strategy before it overrides personality?

---

## 4. MILITARY AI SYSTEM

### Current Implementation

**Files:** [CvMilitaryAI.{h,cpp}](CvMilitaryAI.h), [CvTacticalAI.{h,cpp}](CvTacticalAI.h)

**Subsystems:**
- `CvMilitaryAI` — Strategic military decisions (unit count, army types, defensive posture)
- `CvTacticalAI` — Tactical combat moves (individual unit orders)

### Known Issues & Improvements

#### Issue 4.1: Defense State Calculation Disconnected from Actual Threat
**File:** [CvMilitaryAI.cpp](CvMilitaryAI.cpp)

**Issue:**
- `DefenseState` enum: ENOUGH, NEUTRAL, NEEDED, CRITICAL
- Calculation based on: ally/enemy unit counts, city defensive structures
- Does NOT account for:
  - Enemy army composition (ranged units are more threatening)
  - Enemy proximity (units 10 tiles away vs. 1 tile away)
  - Enemy movement speed (horses faster than archers)
  - Current threats to allied/minor civs (AI might shift defense to help allies)

**Improvements:**
- [ ] **Proximity-weighted threat**: double-count units within 5 tiles of our cities
- [ ] **Unit composition adjustment**: ranged units worth 1.5x, siege units worth 2x in threat calc
- [ ] **Predict enemy movements**: if enemy army is moving toward us, increase threat
- [ ] **Allied threat assessment**: if allies under attack, boost our defense state if "cooperative AI"

---

#### Issue 4.2: Tactical AI Lacks Long-Term Planning
**File:** [CvTacticalAI.cpp](CvTacticalAI.cpp)

**Issue:**
- Tactical AI decides unit moves on a turn-by-turn basis
- No multi-turn planning: doesn't set up flanking maneuvers, pincer attacks, staged bombardment
- Will not retreat strategically from losing battles (thrashes)
- No coordination between armies: two armies might ignore each other if they don't share immediate targets

**Improvements:**
- [ ] **Add multi-turn operation planning**: compute optimal attack sequence and follow it
- [ ] **Add retreat threshold**: if losing > 20% of army and no allies nearby, retreat
- [ ] **Add army coordination**: nearby armies should support each other
- [ ] **Benchmark vs. human players**: did AI win/lose more games after change?

---

#### Issue 4.3: Flavor Weighting Not Documented
**File:** [CvMilitaryAI.cpp](CvMilitaryAI.cpp)

**Issue:**
- Military decisions are influenced by flavors (OFFENSE, DEFENSE, EXPANSION)
- But which flavor is primary for each decision?
- No clear priority: if OFFENSE=8 and DEFENSE=9, which wins?

**Improvements:**
- [ ] **Document flavor weights**: which flavors affect which military decisions?
- [ ] **Add XML config**: specify flavor priority for each military decision type
- [ ] **Log flavor influence**: when making military decision, log which flavors contributed

---

## 5. ECONOMIC AI SYSTEM

### Current Implementation

**File:** [CvEconomicAI.{h,cpp}](CvEconomicAI.h)

**Subsystems:**
- Strategic decision-making (focus on gold, growth, happiness)
- Trade route targeting
- Gold spending prioritization

### Known Issues & Improvements

#### Issue 5.1: Trade Route Logic May Ignore Flavor
**File:** [CvEconomicAI.cpp](CvEconomicAI.cpp)

**Issue:**
- Trade route selection based on gold yield
- Does not account for diplomatic flavor or grand strategy
  - E.g., if pursuing diplomacy victory, trade routes to city-states or other civs should be boosted
  - If pursuing expansion, should avoid sending routes to enemy civs

**Improvements:**
- [ ] **Flavor-aware trade route selection**: multiply gold value by diplomacy/economy flavor
- [ ] **Grand strategy awareness**: boost routes aligned with active strategy
- [ ] **Diplomatic consideration**: avoid routes to civs we're hostile to (unless selling peace)

---

#### Issue 5.2: Gold Spending Priority Not Modular
**File:** [CvEconomicAI.cpp](CvEconomicAI.cpp)

**Issue:**
- Gold spending decision (unit rush, building rush, city-state gift) happens in single function
- Priority between options not clear or data-driven
- Flavor influence (if any) not obvious

**Improvements:**
- [ ] **Modularize spending decisions**: each option gets scored with clear formula
- [ ] **Data-drive priorities**: XML file with category weights and flavor modifiers
- [ ] **Log spending decisions**: when AI spends gold, log why and which options were considered

---

## 6. HANDICAP SYSTEM

### Current Implementation

**Files:** [CvDllHandicapInfo.{h,cpp}](CvDllHandicapInfo.h), database queries

**Handicap Properties:**
- **Construction Bonus** (`AIConstructPercent`)
- **Unit Training Bonus** (`AITrainPercent`)
- **Happiness/Unhappiness Modifiers**
- **Unit Supply Modifier**
- **Various "Block" Percentages** (tech blocking, policy blocking, land dispute %, etc.)
- **Approach Modifiers** (how difficulty affects diplomatic approach scoring)

### Known Issues & Improvements

#### Issue 6.1: Handicap Percentage Naming Inconsistent
**Files:** Multiple

**Issue:**
- Mix of naming conventions:
  - `getTechBlockPercent()` vs. `getVictoryDisputePercent()` vs. `getAggressionIncrease()`
  - Some are "%", some are "Increase" — unclear if additive or multiplicative
  - No range validation in code (assume 0-200%?)

**Improvements:**
- [ ] **Standardize naming**: all percentages use `get*Percent()`, all additions use `get*Increase()`
- [ ] **Document each handicap**: XML file with description, min, max, example values
- [ ] **Add validation**: log warning if any handicap outside expected range
- [ ] **Version migration**: if changing handicap structure, provide upgrade path from old saves

---

#### Issue 6.2: Difficulty Balancing Opaque
**File:** [game-mechanics.md](../reference/game-mechanics.md)

**Issue:**
- No centralized documentation of how difficulty affects AI behavior
- Each AI module accesses handicap differently
- No tools to analyze balance across difficulties

**Improvements:**
- [ ] **Create difficulty balance doc**: list all handicap modifiers by difficulty level
- [ ] **Create balance testing tool**: sim 100 games per difficulty, log win rates and victory types
- [ ] **Add "difficulty preview"**: show expected AI bonuses in difficulty selection screen
- [ ] **Consider curve**: is progression from Settler to Immortal smooth, or do some difficulties feel unfair?

---

#### Issue 6.3: Human vs. AI Handicap Handling Inconsistent
**Files:** Multiple (CvDiplomacyAI.cpp, etc.)

**Issue:**
- Some calculations check `GET_PLAYER(ePlayer).isHuman(ISHUMAN_HANDICAP)` and apply different handicap
- Unclear intent: should humans play at handicap too? Or only AI?
- Related option: `MOD_HUMAN_USES_AI_HANDICAP` — but hard to discover

**Improvements:**
- [ ] **Document intent**: are humans supposed to get handicap bonuses or not?
- [ ] **Standardize logic**: create helper function `GetEffectiveHandicap(PlayerTypes ePlayer)`
- [ ] **Expose option**: make `HUMAN_USES_AI_HANDICAP` visible in game setup or mod menu
- [ ] **Test both paths**: verify game balance with and without option enabled

---

## 7. STRATEGIC WEIGHTING SYSTEM

### Current Implementation

**Concept:** AI subsystems (Tech AI, Unit AI, Policy AI) score options based on "flavors"

**Example (Tech AI):**
1. Each tech has XML entries: `<FlavorValue FlavorType="FLAVOR_SCIENCE">50</FlavorValue>`
2. AI calculates current flavor values (from personality + grand strategy + events)
3. Score each tech: `tech_score += tech_flavor_value * ai_flavor_value` (summed over all flavors)
4. Pick tech with highest score

### Known Issues & Improvements

#### Issue 7.1: Flavor Scores Not Normalized
**Files:** [CvUnitProductionAI.cpp#L81](CvUnitProductionAI.cpp#L81), [CvTechAI.cpp](CvTechAI.cpp), [CvPolicyAI.cpp](CvPolicyAI.cpp)

**Issue:**
- AI flavor values typically [0, 10], but tech flavor values can be [0, 100]
- If AI has 5 active flavors and each tech has 5 flavor values, score can range wildly
  - Tech A: 50+50+50+50+50 = 250
  - Tech B: 1+1+1+1+1 = 5
  - Ratio: 50:1
- No normalization: AI might exclusively pick high-flavor techs and ignore others
- Flavor weighting differences not obvious from vanilla gameplay

**Improvements:**
- [ ] **Normalize scores**: divide by number of flavors, or cap maximum score
- [ ] **Add non-flavor factors**: consider prerequisite techs, strategic importance, blockers
- [ ] **Add randomization**: top 3 options should be viable, not just top 1
- [ ] **Log scores**: when making tech/unit/building/policy choice, log top 5 options and their scores

---

#### Issue 7.2: Flavor Propagation Delayed
**Files:** [CvFlavorManager.cpp](CvFlavorManager.cpp), [CvPlayerTechs.cpp](CvPlayerTechs.cpp)

**Issue:**
- When flavor changes, might take 1+ turn to propagate to all subsystems
- Urgent flavor changes (e.g., "we're under attack! boost DEFENSE") might not be reflected in unit production immediately
- Related to Issue 1.2 above

**Improvements:**
- [ ] **Add priority levels to flavors**: URGENT (apply immediately), NORMAL (next turn), BACKGROUND (batch)
- [ ] **Immediate flush for military**: if DEFENSE flavor boosted due to threat, flush to military AI now
- [ ] **Benchmark**: measure propagation latency, target < 1 turn for urgent changes

---

#### Issue 7.3: Flavor Weights Not Discoverable
**Files:** Multiple

**Issue:**
- Which techs have highest flavor values? Requires XML parsing
- Which AI civs prioritize which flavors? Requires XML + code inspection
- No in-game UI or debug overlay showing flavor-based scoring
- No AI debugging mode for balance review

**Improvements:**
- [ ] **Create flavor analysis tool**: parse XML, generate spreadsheet of tech/unit/building flavor values
- [ ] **Add AI debug overlay**: in-game, press key to see flavor scores for current choice
- [ ] **Export flavor data**: on game load, generate CSV of all flavor-based scores for analysis
- [ ] **Create balance report**: identify techs/units/buildings with extreme flavor imbalances

---

## 8. CROSS-SYSTEM INTEGRATION ISSUES

### Issue 8.1: Flavor vs. Grand Strategy vs. Difficulty - Priority Unclear
**Files:** Multiple

**Issue:**
- A tech choice is influenced by:
  1. AI personality flavors (e.g., player likes science)
  2. Grand strategy (player pursuing science victory → want more science techs)
  3. Difficulty handicap (higher difficulty AI gets bonuses)
  4. Current events (under attack → want military techs)
- When these conflict, which takes priority?

**Example Conflict:**
- AI personality: SCIENCE = 8 (loves science)
- Grand strategy: CONQUEST (attacking a neighbor)
- Difficulty: high (AI gets +30% to military)
- Current event: low on hammers

Which tech does AI choose? SCIENCE tech (personality) or MILITARY tech (strategy + difficulty)?

**Improvements:**
- [ ] **Document priority order**: personality < strategy < difficulty < events (or define custom order)
- [ ] **Add weighting formula**: each factor gets explicit weight, combined with known formula
- [ ] **Log priority resolution**: when factors conflict, log final choice and which factor "won"
- [ ] **Make configurable**: XML option to set priority order

---

### Issue 8.2: City-Level vs. Player-Level Flavor Management Unclear
**Files:** [CvFlavorManager.cpp](CvFlavorManager.cpp), [CvCityStrategyAI.cpp](CvCityStrategyAI.cpp)

**Issue:**
- Both city strategy AI and player-level AI use flavor system
- But interaction between the two not clear
  - City gets player-level flavors + city-specific flavors?
  - Or separate flavor instances for city?
  - Or city flavors propagated up to player level?

**Improvements:**
- [ ] **Document flavor hierarchy**: how do player, city, and building/unit flavors interact?
- [ ] **Add visibility**: log which flavors are active at which level
- [ ] **Consider consolidation**: single flavor system might be simpler

---

## 9. TESTING & BALANCE

### Issue 9.1: No Automated AI Balance Testing
**File:** N/A (no existing system)

**Issue:**
- Balance reviewed by manual play or intuition
- No systematic testing of AI behavior across:
  - All leaders (20+)
  - All difficulties (8)
  - All map sizes (5)
  - Multiple random seeds
- AI might play differently on small maps vs. large maps, unexplored

**Recommendations:**
- [ ] **Create AI test harness**: simulate 1000s of games, log outcomes by leader/difficulty/map
- [ ] **Track metrics**: win rate by victory type, avg game length, cultural/military/diplomatic interactions
- [ ] **Compare difficulties**: at each difficulty, AI should be ~10-15% harder to beat than one below
- [ ] **Identify outliers**: flag leaders/difficulties with unusual behavior

---

### Issue 9.2: No AI Debugging Mode
**File:** N/A (no existing system)

**Issue:**
- When AI makes unexpected decision, hard to debug
- No logs of internal scoring
- No overlay showing AI thought process

**Recommendations:**
- [ ] **Add debug overlay**: press key to see:
  - Current personality flavors
  - Current grand strategy (and priority scores)
  - Current diplomatic approaches to each civ (and scoring breakdown)
  - Current military defense state and threat assessment
- [ ] **CSV export**: export all AI decisions and scores for a game to file for analysis
- [ ] **Breakpoint system**: set breakpoint "if AI chooses wrong tech", break into debugger

---

## 10. RECOMMENDATIONS FOR PRIORITIZATION

### High Priority (Critical Issues)

1. **Refactor Diplomatic AI Scoring** (Issue 2.1)
   - Current 20K line function is unmaintainable
   - Leads to hard-to-debug AI behavior
   - Moderate effort for major clarity improvement

2. **Handicap Modifier Consistency** (Issue 6.1, 6.3)
   - Naming and application inconsistent
   - May introduce balancing bugs
   - Low effort, high safety payoff

3. **Add AI Debug Mode** (Issue 9.2)
   - Essential for future balance work
   - Moderate effort, high long-term ROI

### Medium Priority (Important Improvements)

4. **Flavor Normalization & Weighting** (Issue 7.1)
   - May explain unusual AI behavior in production decisions
   - Moderate effort, impacts tech/unit/policy/wonder choices

5. **Grand Strategy Victory Priority Calculation** (Issue 3.1)
   - May cause AI to thrash between strategies
   - Moderate effort, affects long-term AI planning

6. **Documentation & Data-Driving** (Issues 2.3, 2.4, 2.5)
   - Make AI balancing more accessible
   - Export balance decisions to XML
   - Low-to-moderate effort, high maintainability payoff

### Lower Priority (Polish & Future)

7. **Military Tactical AI Planning** (Issue 4.2)
   - Improves combat realism
   - Higher effort, lower priority than strategic fixes

8. **Religious/Ideology Competition Expansion** (Issue 2.4)
   - Religious AI feels weaker than other concerns
   - Moderate effort, lower balance impact

9. **Performance & Caching** (Not detailed in this review)
   - AI subsystems might thrash or recalculate unnecessarily
   - Requires profiling to identify bottlenecks

---

## 11. APPENDIX: QUICK REFERENCE

### Key Files by System

| System | Files | Lines |
|--------|-------|-------|
| **Flavor Management** | `CvFlavorManager.{h,cpp}` | ~500 |
| **Diplomatic AI** | `CvDiplomacyAI.{h,cpp}` | ~2500 |
| **Grand Strategy AI** | `CvGrandStrategyAI.{h,cpp}` | ~500 |
| **Military AI** | `CvMilitaryAI.{h,cpp}` | ~500 |
| **Tactical AI** | `CvTacticalAI.{h,cpp}` | ~1000 |
| **Economic AI** | `CvEconomicAI.{h,cpp}` | ~800 |
| **Tech AI** | `CvTechClasses.cpp`, `CvTechAI.{h,cpp}` | ~1000 |
| **Unit Production AI** | `CvUnitProductionAI.{h,cpp}` | ~300 |
| **Building Production AI** | `CvBuildingProductionAI.{h,cpp}` | ~300 |
| **Policy AI** | `CvPolicyAI.{h,cpp}` | ~500 |
| **Wonder Production AI** | `CvWonderProductionAI.{h,cpp}` | ~200 |
| **Handicap Info** | `CvDllHandicapInfo.{h,cpp}`, database | ~500 |

### Flavor Types (Common)
- `FLAVOR_SCIENCE`, `FLAVOR_GOLD`, `FLAVOR_OFFENSE`, `FLAVOR_DEFENSE`
- `FLAVOR_CULTURE`, `FLAVOR_DIPLOMACY`, `FLAVOR_RELIGION`, `FLAVOR_HAPPINESS`
- `FLAVOR_PRODUCTION`, `FLAVOR_GROWTH`, `FLAVOR_EXPANSION`, `FLAVOR_WONDER`

### Difficulty Levels
- Settler, Warlord, Prince, King, Emperor, Immortal, Deity, Custom

### Key Constants
- Flavor range: [0, 10] (or [-10, 10] with modifiers)
- Approach scores: typically [0, 1000+] (normalized differently across systems)
- Handicap percentages: typically [50%, 200%] but ranges vary

---

## Next Steps

1. **Assign reviewers** to each high-priority issue
2. **Create GitHub issues** for each improvement
3. **Prioritize implementation roadmap**
4. **Create unit tests** for refactored systems
5. **Schedule balance testing** after major changes

---

*End of AI Systems & Difficulty Review*
