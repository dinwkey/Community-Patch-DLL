# Tourism & Great Works Mechanics Review
## Community Patch DLL & Vox Populi Analysis

**Date:** January 9, 2026  
**Focus:** Tourism, Great Works, Theming, Influence, and Culture Victory Mechanics

---

## Executive Summary

The Tourism & Great Works system in the Civ5 modpack (Community Patch + Vox Populi) is a complex, multi-layered system that rewards cultural development and provides an alternative victory path. The codebase shows mature implementation across C++ core mechanics, Lua UI, and database configuration. This review identifies architectural strengths, potential improvements, and optimization opportunities.

---

## 1. Architecture Overview

### 1.1 Core Components

#### **CvGameCulture** (Game-level)
- **Location:** [CvGameCultureClasses.h](CvGameCoreDLL_Expansion2/CvGameCultureClasses.h#L57)
- **Responsibility:** Game-wide Great Works registry and management
- **Key Functions:**
  - `CreateGreatWork()` - Creates new Great Works with era, creator, and type
  - `GetGreatWorkCity()` - Locates which city holds a specific Great Work
  - `GetGreatWorkCurrentThemingBonus()` - Calculates theming bonuses for placed works
  - `SwapGreatWorks()` - Enables work trading between players
  - `MoveGreatWorks()` - Relocates works within empire

#### **CvPlayerCulture** (Player-level)
- **Location:** [CvGameCultureClasses.h](CvGameCoreDLL_Expansion2/CvGameCultureClasses.h#L200+)
- **Responsibility:** Player-specific culture, tourism, and influence tracking
- **Key Systems:**
  - Tourism generation and modifiers
  - Influence tracking (Exotic → Familiar → Popular → Influential → Dominant)
  - Ideology spread mechanics
  - Great Work placement strategy (AI)
  - Automatic theming (VP feature)

#### **CvCityCulture** (City-level)
- **Location:** [CvGameCultureClasses.h](CvGameCoreDLL_Expansion2/CvGameCultureClasses.h#L382)
- **Responsibility:** City-level culture, tourism, and theming
- **Key Functions:**
  - `GetNumGreatWorks()` - Count filled slots
  - `IsThemingBonusPossible()` - Validates theming requirements
  - `GetThemingBonus()` - Returns theming yield multiplier
  - `GetThemingTooltip()` - UI tooltip generation
  - `UpdateThemingBonusIndex()` - Recalculates theming when works change

### 1.2 Data Flow

```
Great Person (Unit) Creates Great Work
    ↓
createGreatWork() → CvGameCulture::CreateGreatWork()
    ↓
CvGame holds GreatWorkList (vector<CvGreatWork>)
    ↓
Player/City: Place Great Work in Building Slot
    ↓
CvCityCulture: Calculate Theming Bonus (if applicable)
    ↓
Tourism & Culture Output Updated
    ↓
CvPlayerCulture: Calculate Influence on Other Civs
    ↓
Culture Victory Progress Updated
```

---

## 2. Tourism System Analysis

### 2.1 Tourism Generation Sources

**Multiple sources contribute to tourism:**

1. **Great Works** (Primary)
   - Base: [BASE_CULTURE_PER_GREAT_WORK](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp#L60) = 2-3 per turn (depending on CP/VP)
   - Base tourism: [BASE_TOURISM_PER_GREAT_WORK](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp#L65) = 2-3 per turn
   - Building type yields vary (Writing, Art, Artifact, Music slots)

2. **Theming Bonuses**
   - Building-specific theming (Era, Civ, Type combinations)
   - Multiplied by player-wide theming modifier (policies, traits)
   - Referenced in: [CvPolicyAI.cpp:2461-2469](CvGameCoreDLL_Expansion2/CvPolicyAI.cpp#L2461)

3. **Wonders** (via Great Works)
   - Wonders hold 4+ Great Works
   - Generate base yields + theming bonuses

4. **Miscellaneous Sources**
   - Historic events (wars, treaties)
   - Trade routes (tourism component)
   - Instant yields (events, policy selections)

### 2.2 Tourism Modifiers

**Key modifier sources** (from [CvCultureClasses.h:273-282](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L273)):

- **Religion:** Shared faith bonuses
- **Trade Routes:** Open borders and active routes
- **Policies & Ideologies:** Direct multipliers
- **Open Borders Agreements:** Diplomatic bonus
- **Game Speed:** Scales with game speed setting
- **Vassalage:** Vassal penalties on incoming tourism

**Function:** `ChangeInfluenceOnTimes100(PlayerTypes eOtherPlayer, int iBaseInfluence, bool bApplyModifiers, bool bModifyForGameSpeed, bool bNoDecimalValues = false)`  
**Location:** [CvCultureClasses.h:252](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L252)

### 2.3 Known Tourism Implementation Issues

#### **Issue 1: Tourism Per Turn Calculation Complexity**
- Multiple paths to apply modifiers (with/without game speed, decimal precision)
- Two variants of `ChangeInfluenceOn()`: overloaded versions with different semantics
- Risk of modifier double-application or omission if callers not careful
- **Impact:** Moderate - affects balance, not stability
- **Recommendation:** Consolidate to single canonical path; add integration tests

#### **Issue 2: Late-Game Tourism Stagnation**
- Once a civ reaches "Dominant" influence, further tourism has diminishing returns
- No "lock out" mechanism prevents players from remaining Dominant indefinitely
- **Impact:** Culture victory can feel anticlimactic after "Dominant" achieved
- **Recommendation:** Consider influence decay or breakpoint penalties

#### **Issue 3: Tourism Modifier Registration**
- Multiple systems apply tourism modifiers (policies, religion, trade, ideologies)
- No centralized modifier registry; scattered across CvPlayerCulture, CvDealAI, CvEspionageClasses, etc.
- Risk of missed interactions when adding new modifier sources
- **Recommendation:** Implement modifier aggregation helper or audit existing sources

---

## 3. Great Works System Analysis

### 3.1 Creation & Placement Flow

**Great Person Creates Work:**
1. Unit calls `createGreatWork()` ([CvUnit.cpp:9312](CvGameCoreDLL_Expansion2/CvUnit.cpp#L9312))
2. Gets `GreatWorkType` and `GreatWorkSlot` type
3. Finds best available slot via `GetClosestAvailableGreatWorkSlot()`
4. If slot exists: creates work and places immediately
5. If no slot: work stored in unit; requires manual placement via UI

**Great Works Registry:**
- Centralized in `CvGameCulture::m_CurrentGreatWorks` (vector)
- Indexed by integer; used globally for lookups
- Serialized/deserialized with game save

### 3.2 Great Work Organization

**Classes:**
- `NO_GREAT_WORK_CLASS = -1`
- `GREAT_WORK_CLASS_WRITING`
- `GREAT_WORK_CLASS_ART`
- `GREAT_WORK_CLASS_ARTIFACT`
- `GREAT_WORK_CLASS_MUSIC`

**Slots:**
- Building types define number and type of slots (XML `GreatWorkCount`)
- Multiple slots per building enable theming
- Wonders can hold many works (up to 4 in base game)

### 3.3 Known Great Works Implementation Issues

#### **Issue 1: No Duplicate Work Prevention**
- Same Great Work can theoretically be placed in multiple slots
- No validation that `IsGreatWorkCreated()` is checked before placement
- **Impact:** Low - unlikely in practice due to UI constraints, but possible via direct calls
- **Recommendation:** Add assertion in placement function to ensure work not already placed

#### **Issue 2: Work Swapping Complexity**
- Great Works can be swapped between players via `SwapGreatWorks()` ([CvGameCultureClasses.h:98](CvGameCoreDLL_Expansion2/CvGameCultureClasses.h#L98))
- Function takes 4 parameters: Player IDs + Work indices
- No validation that works are in correct buildings/slots
- Theming bonus recalculation may be delayed
- **Impact:** Moderate - can cause UI inconsistencies
- **Recommendation:** Add bounds checking; immediately recalculate theming; log swaps

#### **Issue 3: Theming Bonus Recalculation Trigger**
- `UpdateThemingBonusIndex()` called when works change
- No guarantee all places that modify works call this
- Risk of stale theming bonuses until next turn/save
- **Impact:** Moderate - cosmetic mostly, but can affect AI decisions
- **Recommendation:** Use dirty flag pattern; validate on access, not mutation

#### **Issue 4: AI Theming Strategy (VP)**
- `ThemeBuilding()` ([CvCultureClasses.cpp:1621](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp#L1621)) uses greedy algorithm
- Doesn't account for future works or changing priorities
- Foreign theming logic: `IsValidForForeignThemingBonus()` complex, no caching
- **Impact:** Moderate - AI may make suboptimal placements
- **Recommendation:** Add caching for foreign era/civ combinations; priority-based selection

---

## 4. Theming Bonus System

### 4.1 Theming Requirements

**Building-Specific Rules** (from database, Building_ThemingBonuses table):
- **Era Diversity:** Works from different eras
- **Civilization Diversity:** Works from different civs
- **Type Combinations:** Specific Great Work classes (Writing + Art, etc.)
- **Minority Foreign:** Majority works by other civs
- **Bonuses:** +20%, +50%, or higher (scaling with complexity)

**Implementation:**
- Stored in `Building_ThemingBonuses` SQL table
- VP adds automatic theming via `ThemeBuilding()` logic
- Lua UI tooltips built via `GetThemingTooltip()` ([CvCultureClasses.h:384](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L384))

### 4.2 Theming Bonus Calculation

**Per-City:**
```cpp
int CvCityCulture::GetThemingBonus(BuildingClassTypes eBuildingClass)
  → Looks up building-specific theming rules
  → Iterates current works in building
  → Validates era/civ/type requirements
  → Returns multiplier (20%, 50%, etc.)
```

**Per-Player:**
```cpp
int CvPlayerCulture::GetThemingBonusMultiplierTimes10000()
  → Sums traits and policy bonuses (e.g., +50% from Heritage policy)
  → Applies to all city theming bonuses
```

**Yield Application:**
- Tourism from theming = `Base_Tourism_Per_Work × Theming_Bonus % × Player_Modifier`
- Culture bonus applied similarly
- Shown in city banner + culture overview UI

### 4.3 Known Theming Implementation Issues

#### **Issue 1: Foreign Theming Rule Caching**
- `IsValidForForeignThemingBonus()` iterates all works, eras, and civs on every check
- Called during `ThemeBuilding()` which runs for every building in AI turn
- **Impact:** High - potential performance issue in late-game with many buildings
- **Recommendation:** Cache foreign era/civ combinations per player; invalidate on work change

#### **Issue 2: Theming Tooltip Localization**
- Tooltip built dynamically in C++ via `GetThemingTooltip()`
- No key-based localization; hardcoded text in code
- French/German players see English theming requirements
- **Impact:** Moderate - UX issue for non-English players
- **Recommendation:** Add TXT_KEY entries for theming rule descriptions; use Locale system

#### **Issue 3: Minority Foreign Theming Edge Case**
- Requires majority works by OTHER civs (not self)
- If player has only 1 building, this rule impossible to satisfy
- No warning or bypass in UI
- **Impact:** Low - rarely relevant, but confusing
- **Recommendation:** Add UI indicator "Impossible with current empire size"

#### **Issue 4: Theming Bonus Applies to Tourism Only**
- Culture bonus from theming not displayed separately
- Tourism from theming not distinguished from base work tourism
- Makes it hard for players to understand theming value
- **Impact:** Moderate - educational issue
- **Recommendation:** Add separate theming culture/tourism display in city screen

---

## 5. Influence System Analysis

### 5.1 Influence Levels & Mechanics

**Levels** (from [CvCultureClasses.h:133-140](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L133)):

| Level | Threshold | Effects |
|-------|-----------|---------|
| EXOTIC | < 1x culture | No bonuses |
| FAMILIAR | 1x - 2x | Modest effects |
| POPULAR | 2x - 3x | Significant bonuses (spy visibility, trade route preference) |
| INFLUENTIAL | 3x - 5x | Major effects (unit sight, espionage ease) |
| DOMINANT | > 5x | Victory-level (culture victory count, public opinion) |

**Mechanics:**
- Tourism per turn added as influence
- Influence accumulates over time per target civ
- Trend indicator (rising/static/falling) based on per-turn rate
- "Turns to Influential" calculator: `GetTurnsToInfluential()` ([CvCultureClasses.h:262](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L262))

### 5.2 Public Opinion & Ideology Spread

**VP Feature:** Public Opinion System
- Civs with high influence adopt player's ideology
- Can cause unhappiness to target civilization
- Represented via `PublicOpinionTypes` enum
- Calculated in `DoPublicOpinion()` ([CvCultureClasses.h:290](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L290))

**Impact on Culture Victory:**
- Each civ at "Influential" counts toward win condition
- Need to reach "Dominant" on majority of other civs
- Public opinion accelerates adoption (indirectly)

### 5.3 Known Influence Implementation Issues

#### **Issue 1: Influence Decay Not Implemented**
- Influence only increases or stagnates; never decreases without warfare
- Stopping tourism doesn't reduce enemy influence
- Leads to "locked" states where dominant civs can't be challenged
- **Impact:** High - affects late-game culture victory dynamics
- **Recommendation:** Implement per-turn decay (1-5% per turn if no new tourism) or "cooling off" periods

#### **Issue 2: Influence Per-Turn Calculation Complexity**
- Multiple code paths: `GetInfluencePerTurnTimes100()` calculates base
- `ChangeInfluenceOnTimes100()` applies modifiers and game speed
- Risk of modifiers applied twice if both functions called sequentially
- **Impact:** Moderate - affects AI decision-making
- **Recommendation:** Single unified calculation function; unit tests for modifier stacking

#### **Issue 3: Trade Route Tourism Not Integrated**
- Trade routes add tourism component
- Calculation in [CvTradeClasses.cpp:2422](CvGameCoreDLL_Expansion2/CvTradeClasses.cpp#L2422)
- Separate from main tourism system
- Can lead to missed interactions with modifiers
- **Impact:** Moderate - affects balance
- **Recommendation:** Consolidate trade route tourism into primary `AddTourismAllKnownCivsWithModifiers()` system

#### **Issue 4: Spy Influence Mechanics Scattered**
- Espionage system applies influence separately in [CvEspionageClasses.cpp:3314](CvGameCoreDLL_Expansion2/CvEspionageClasses.cpp#L3314)
- Propaganda diplomat logic in [CvCultureClasses.h:296](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L296)
- No unified influence application point
- **Impact:** Moderate - maintenance burden
- **Recommendation:** Create `ApplyInfluenceChange()` hub function; all sources call it

---

## 6. Culture Victory Condition

### 6.1 Win Condition

**Requirement:**
- Be "Influential" (threshold met, not necessarily "Dominant") over all other major civs
- Number of civs to influence: `GetNumCivsToBeInfluentialOn()`
- Currently influential: `GetNumCivsInfluentialOn()`

**UI Display:**
- Culture Overview screen tab: "Culture Victory" → shows progress toward goal
- Top panel tourism tooltip (when enabled)
- Notifications when reaching "Influential" on a new civ

### 6.2 Victory Progress Tracking

**Location:** [CvCultureClasses.h:102-104](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L102)
```cpp
int GetNumCivsInfluentialForWin() const;
bool GetReportedSomeoneInfluential() const;
void SetReportedSomeoneInfluential(bool bValue);
```

**Reported Flag:**
- Tracks whether a notification was shown for reaching Influential on someone
- Set to false on game start; set to true when threshold crossed
- Prevents spam notifications

### 6.3 Known Culture Victory Implementation Issues

#### **Issue 1: Influential vs. Dominant Confusion**
- Victory requires "Influential", not "Dominant"
- UI and tooltips sometimes conflate the terms
- Players may think they need "Dominant" on all civs
- **Impact:** Low - mostly educational
- **Recommendation:** Audit UI text; add clear definition in help screens

#### **Issue 2: No Early Warning System**
- No "you're on track for culture victory" notification
- Progress bar shows raw numbers, not percentage to win
- AI doesn't communicate culture threat credibly
- **Impact:** Moderate - affects difficulty balancing
- **Recommendation:** Add victory condition status tracker; highlight top cultural threat

#### **Issue 3: Ideology Flip Not Linked to Influence**
- Ideology spread and influence are semi-independent systems
- Player may reach "Influential" without ideology adoption, or vice versa
- Could confuse players about win condition requirements
- **Impact:** Low - mostly UI clarity
- **Recommendation:** Clarify in UI that ideology spread ≠ Influential status

---

## 7. Performance Considerations

### 7.1 Identified Performance Bottlenecks

#### **Theming Bonus Recalculation**
- Called per-building on work change
- Foreign theming validation iterates all works
- **Cost:** O(N_buildings × M_works × K_eras × L_civs) per work placement
- **Mitigation:** Cache foreign combinations

#### **Tourism Modifier Application**
- Multiple systems apply modifiers independently
- Each call re-sums: religion, trade, open borders, policies, ideologies
- **Cost:** O(N_civs × K_modifier_sources) per turn
- **Mitigation:** Aggregate into single cached modifier per civ

#### **Influence Per-Turn Calculation**
- `ChangeInfluenceOnTimes100()` called once per source per turn
- If 10+ sources (wonders, trades, espionage), O(10) recalculations per target per turn
- **Cost:** O(N_civs^2 × 10+) per turn
- **Mitigation:** Batch influence changes; apply once per turn

### 7.2 Recommended Profiling Areas

1. **Late-game (T300+):** Verify influence calculations don't spike CPU
2. **High-GW games (modded):** Test theming recalculation with 100+ works
3. **Many-civilization games:** Verify tourism modifier caching scales well

---

## 8. UI/UX Issues

### 8.1 Culture Overview Screen

**Current Implementation:**
- Lua-based UI (CP + VP versions differ slightly)
- Multiple tabs: Your Culture, Others' Works, Swap, Culture Victory, Player Influence, Historic Events
- Supports filtering, sorting, work swapping

**Issues:**
1. **Theming Tooltips Too Verbose:** Can't easily scan theming requirements across multiple buildings
   - Recommendation: Add compact view; grid layout of requirements
2. **Influence Graph Missing:** No visual representation of influence trends over time
   - Recommendation: Add sparkline or trend graph per civ
3. **Culture Victory Progress Unclear:** Raw numbers don't convey "how close am I?"
   - Recommendation: Add progress bar; % toward win

### 8.2 City Screen Integration

**Current Display:**
- Tourism per turn label
- Theming bonus indicator (+0, +20%, etc.)
- Detailed tooltip on hover

**Issues:**
1. **Theming Bonus Not Itemized:** Can't see theming culture vs. tourism separately
   - Recommendation: Add split display: "X culture + Y tourism from theming"
2. **Slot Utilization Unclear:** Can't see at a glance if all slots filled
   - Recommendation: Add progress "4/4 slots filled" indicator
3. **Building Theming Requirements Not Searchable:** Must hover each building
   - Recommendation: Add filter "show buildings with available theming"

### 8.3 Top Panel Tourism Display

**Current:**
- Tourism per turn icon
- Click to open Culture Overview
- Tooltip with great works count + influence status

**Issue:**
- Tooltip doesn't show which civs are "close to Influential"
- Recommendation: Add ranked list of influence progress (e.g., "Egypt 85%, Arabia 60%")

---

## 9. Database & Modding Considerations

### 9.1 Theming Bonus Definition

**Table:** `Building_ThemingBonuses`

**Columns:**
- `BuildingType`
- `Description` (text key)
- `RequiresEra`: Boolean
- `RequiresCivilization`: Boolean
- `RequiresClass`: Boolean (Great Work class)
- `ReqMajorityForeignCivs`: Boolean
- `ThemingBonus`: Percentage increase (20, 50, etc.)

**Impact:**
- Completely moddable; no hardcoded theming rules
- Easy to add new bonuses or modify existing ones
- Balancing requires playtesting to avoid OP theming

### 9.2 Great Work Creation

**Location of Work Definitions:**
- Great Work info in `GreatWorks` table (XML/SQL)
- Linked to Great Persons via GP class definitions
- Work type determines which buildings can hold it

**Modding Friendly:** Yes
- Can define new Great Work types
- New building types automatically support slots via `GreatWorkCount` field
- No C++ changes needed for new work types (unless new class needed)

### 9.3 Culture Victory Configuration

**Customizable Values:**
- Number of civs to influence: `GetNumCivsToBeInfluentialOn()` (hardcoded, but could be made a define)
- Influence level thresholds: Hardcoded in `GetInfluenceLevel()` logic
- Tourism per great work: `BASE_CULTURE_PER_GREAT_WORK`, `BASE_TOURISM_PER_GREAT_WORK`

**Recommendation:** Move influence thresholds to GameDefines for easier balance tuning

---

## 10. Testing & Validation Gaps

### 10.1 Missing Test Coverage

1. **Tourism Modifier Stacking**
   - No test for multiple modifiers applied simultaneously
   - Example: Religion + Trade + Policy all +50% each
   - Need to verify order of operations and no double-application

2. **Theming Edge Cases**
   - Single-building empire with minority foreign rule
   - Work swapping between civs at same influence level
   - Theming bonus loss when building captured

3. **Influence Transition**
   - Moving from Exotic → Dominant should show trend changes
   - Verify "Turns to Influential" accuracy at each level
   - Test with various tourism rates (fast vs. slow)

4. **Late-Game Culture Victory**
   - Test reaching culture victory with all civs Dominant
   - Test with some civs at Influential, others below
   - Test with ideology flips during final push

5. **Trade Route Integration**
   - Tourism from trade route shouldn't double-apply modifiers
   - Test with route to civ at different influence levels

### 10.2 Recommended Test Suite Additions

```cpp
// Unit tests (C++)
TEST(CvCultureClasses, TourismModifierStacking)
TEST(CvCultureClasses, ThemingBonusEdgeCases)
TEST(CvCultureClasses, InfluenceTransitions)
TEST(CvCultureClasses, GetTurnsToInfluentialAccuracy)

// Integration tests (Lua)
TestCultureVictoryPath() -- Full empire to victory
TestInfluenceDecay() -- Verify decay mechanics (if implemented)
TestThemingAutomation() -- VP auto-theme correctness
```

---

## 11. Proposed Improvements (Priority Order)

### Priority 1: Critical Bugs (Stability)
1. **Add duplicate work placement check** in `MoveWorkIntoSlot()`
   - Prevent same work in multiple slots
   - Add assertion + error return

2. **Consolidate tourism modifier application**
   - Single function: `ApplyTourismWithModifiers(eTargetPlayer, iBase, flags)`
   - All sources call this; reduces modifier double-application risk
   - Location: New method in CvPlayerCulture

3. **Implement theming bonus caching**
   - Cache foreign era/civ combinations per player
   - Invalidate on great work change or turn
   - Reduces late-game CPU usage

### Priority 2: Balance & Gameplay (Moderate Impact)
1. **Implement influence decay**
   - Per-turn decay: 1-2% if no new tourism
   - Allows challenging dominant civs
   - Prevents culture victory from being "locked in"

2. **Unified influence change hub**
   - New function: `ModifyInfluenceOn(eTargetPlayer, iChange, eSourceType, ...)`
   - All sources (tourism, espionage, events) call here
   - Enables future effects (notifications, interference logic)
   - Location: CvPlayerCulture method

3. **Tourism modifier registry**
   - Audit all modifier sources
   - Create modifier aggregation helper
   - Consolidate scattered code in CvDealAI, CvEspionageClasses, etc.

### Priority 3: UI/UX (Player Experience)
1. **Localize theming requirements**
   - Move theming rule descriptions to XML TXT_KEY entries
   - Update `GetThemingTooltip()` to use Locale system
   - Fixes non-English player experience

2. **Enhance culture victory progress display**
   - Add progress bar: "Influential on 4/6 civs (67%)"
   - Show "turns to victory" estimate
   - List all civs with influence levels

3. **Theming bonus itemization**
   - Display separately in city screen: "X culture + Y tourism from theming"
   - Show in city yield breakdown tooltip
   - Add per-building theming details in culture overview

4. **Trade route tourism clarity**
   - Separate trade route tourism from base tourism in tooltips
   - Show modifier application path (e.g., "+50% from religion")

### Priority 4: Performance Optimization (Late-game)
1. **Lazy-evaluate influence trends**
   - Only recalculate when tourism rate changes
   - Cache `GetInfluenceTrend()` result per player

2. **Batch theming updates**
   - Don't recalculate per work; batch at turn end
   - Or defer until city screen opened

---

## 12. Code Quality & Maintainability

### 12.1 Positive Aspects
- Clear separation of concerns (CvGameCulture, CvPlayerCulture, CvCityCulture)
- Well-organized Lua UI with multiple override points
- Extensive use of getters/setters for encapsulation
- Good coverage of edge cases in theming logic

### 12.2 Areas for Improvement
- **Scattered modifier logic:** Tourism modifiers applied in 4+ different files
  - Recommendation: Consolidate into CvPlayerCulture::GetTourismModifierWith()
- **Magic numbers:** Influence level thresholds hardcoded (1.0, 2.0, 3.0, 5.0)
  - Recommendation: Define as GameDefines
- **Inconsistent naming:** Mix of "Tourism", "Culture", "Influence" in method names
  - Recommendation: Add XML documentation to clarify terminology
- **No inline comments in theming logic:** Complex foreign era validation is opaque
  - Recommendation: Add detailed comments explaining each requirement check

### 12.3 Documentation Gaps
- No design document for Culture Victory system
- Influence level calculation logic not clearly documented
- Theming bonus algorithm not detailed in code comments
- **Recommendation:** Add `CultureVictory.md` to repository with design details

---

## 13. Conclusion & Recommendations

The Tourism & Great Works system is **architecturally sound** but shows signs of **incremental growth** without unified refactoring. Key recommendations:

1. **Immediate:** Consolidate tourism/influence modifiers into single path (Priority 1)
2. **Short-term:** Implement influence decay and localize UI text (Priority 2-3)
3. **Medium-term:** Add performance caching for theming + influence calculations (Priority 1 + 4)
4. **Long-term:** Consider culture victory balance review (do players achieve it too easily/hard?)

**Estimated Effort:**
- Priority 1 fixes: 20-40 hours (consolidation + caching)
- Priority 2 additions: 30-50 hours (decay + modifier audit)
- Priority 3 UI: 20-30 hours (localization + tooltips)
- Priority 4 optimization: 10-20 hours (caching + lazy evaluation)

**Risk Assessment:**
- Low risk: UI improvements, localization, documentation
- Medium risk: Theming caching (needs regression testing)
- Medium risk: Influence decay (may break balance; needs tuning)
- High risk: Modifier consolidation (touches many code paths; needs integration tests)

---

## 14. References

### Key Source Files
- [CvCultureClasses.h](CvGameCoreDLL_Expansion2/CvCultureClasses.h) - Main class definitions
- [CvCultureClasses.cpp](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp) - Implementation (6625 lines)
- [CvUnit.cpp:9312-13050](CvGameCoreDLL_Expansion2/CvUnit.cpp#L9312) - Great Work creation & tourism blasts
- [CvTradeClasses.cpp:2400-2500](CvGameCoreDLL_Expansion2/CvTradeClasses.cpp#L2400) - Trade route tourism
- [CultureOverview.lua](1%20Community%20Patch/Core%20Files/Overrides/CultureOverview.lua) - Lua UI (CP version)
- [CultureOverview.lua](2%20Vox%20Populi/Core%20Files/Overrides/CultureOverview.lua) - Lua UI (VP version)
- [Building_ThemingBonuses.sql](SQL/) - Theming bonus definitions

### Related Systems
- **Espionage & Culture:** [CvEspionageClasses.cpp](CvGameCoreDLL_Expansion2/CvEspionageClasses.cpp#L3314)
- **Ideology & Public Opinion:** [CvCultureClasses.h:290](CvGameCoreDLL_Expansion2/CvCultureClasses.h#L290)
- **Great Persons:** [CvUnit.h:1618+](CvGameCoreDLL_Expansion2/CvUnit.h#L1618)

---

**Document Version:** 1.0  
**Last Updated:** January 9, 2026  
**Prepared for:** Community Patch DLL Development Team
