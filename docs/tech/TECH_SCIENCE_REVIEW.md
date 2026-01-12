# Technology / Science System Review
## Issues & Improvements for Tech Costs, Research Progress, Beakers, Prerequisites & Effects

**Date:** January 9, 2026  
**Scope:** Community Patch DLL & Vox Populi  
**Version:** CvGameCoreDLL_Expansion2 | VP v17

---

## 1. TECH COST CALCULATION SYSTEM

### 1.1 Current Implementation
The tech cost system applies **cascading multipliers** to the base research cost in the following order:

#### Formula (CvTeamTechs::GetResearchCost & CvPlayerTechs::GetResearchCost):
```
Final Cost = BaseCost × (Player Modifiers) × (City Count Modifier) × (Team Modifiers)
```

#### Multiplier Chain:
1. **Base Research Cost** (XML: Technologies.ResearchCost)
2. **Player Modifiers** (calculateResearchModifier):
   - Individual policy/promotion effects
   - Difficulty handicap: `ResearchPercent` (100-200 range typical)
   - Per-era multiplier: `ResearchPerEraModifier` × CurrentEra
   - AI bonus/penalty: `AIResearchPercent` & `AIResearchPerEraModifier`
   - World map difficulty: `getResearchPercent()`
   - Game speed: `getResearchPercent()` (significant: Slower speeds have higher %)
   - Start era bonus: `getResearchPercent()`
   - **Team size penalty** (CRITICAL): `TECH_COST_EXTRA_TEAM_MEMBER_MODIFIER` per extra team member
   - **Civ6 Eureka modifier** (if MOD_CIV6_EUREKAS enabled): Reduces cost per eureka trigger

3. **City Count Modifier** (applies per effective city):
   - Base: `World.NumCitiesTechCostMod` (default 40, varies by map size)
   - Individual: `Player.GetTechCostXCitiesModifier()`
   - Formula: `+mod × (NumEffectiveCities)` to final cost
   - **PUPPET HANDLING**: Uses `!MOD_BALANCE_PUPPET_CHANGES` flag (treats puppets differently based on mod version)

4. **Team Modifiers** (CvTeamTechs::GetResearchCost):
   - **Known Tech Reduction** (catch-up mechanic): Reduces cost if other civs have researched the tech
     - Base: `TECH_COST_TOTAL_KNOWN_TEAM_MODIFIER` (30 in CP, 10 in VP)
     - Extra catch-up bonus: `TechCatchUpMod` × CurrentEra (for humans)
     - AI bonus: `AITechCatchUpMod` × CurrentEra (applied to AI civs)
   - **Handicap Level Multiplier**: `getHandicapInfo().getResearchPercent()`
   - **Era Scaling**: `getHandicapInfo().getResearchPerEraModifier() × CurrentEra`

#### Key Issues Identified:

**Issue 1.1a: Compound Growth in Late Game**
- The city count modifier stacks **additively** with base cost, not multiplicatively
- A player with 20 cities in Industrial Era sees costs balloon from:
  - Ancient: 30% increase (minimal)
  - Industrial: 800%+ increase (prohibitive)
- **Problem**: Makes tall empires impossible; encourages wide over tall
- **Severity**: HIGH - Affects core gameplay balance

**Issue 1.1b: Era Scaling Multipliers Stack Aggressively**
- `ResearchPerEraModifier × CurrentEra` compounds with catch-up mechanics
- Example: VP difficulty with -10 ResearchPerEraModifier in Industrial (Era 17):
  - Catch-up penalty alone: `-170%` if 50% of civs have researched
  - Total multiplier can exceed 300-400% of base
- **Problem**: Catch-up mechanic becomes **punitive** rather than catch-up
- **Severity**: MEDIUM - Intended but may be too aggressive

**Issue 1.1c: City Count Modifier Precision Loss**
- Uses `int` arithmetic; rounding errors compound across calculations
- Line 2418: `iNumCitiesMod = iNumCitiesMod * GET_PLAYER(ePlayer).GetNumEffectiveCities(...)`
- For large city counts, result can overflow (uses `long long` in SetResearchProgressTimes100 to mitigate)
- **Problem**: Inconsistency between GetResearchCost and SetResearchProgressTimes100 calculations
- **Severity**: LOW-MEDIUM - Rounding errors typically <1% but can accumulate

**Issue 1.1d: Team Member Penalty Not Visible to Players**
- `TECH_COST_EXTRA_TEAM_MEMBER_MODIFIER` penalizes multiplayer teams
- Value: 50 in CP, 100 in VP per extra member
- **Not exposed in tooltips or UI**
- **Problem**: Players unaware why allied research is slower
- **Severity**: LOW - Design choice but poor UX

---

### 1.2 Player-Level Modifiers (calculateResearchModifier)

**Location**: CvPlayer.cpp (calculateResearchModifier)

#### Components:
1. Civ traits & bonuses (policy, building, wonder modifiers)
2. Promotion/unit science bonuses (Great Scientist discount)
3. Trade route bonuses (science yields converted to research reduction)
4. Religious pressure / faith conversions
5. Espionage effects (can temporarily boost or reduce)

#### Improvements Identified:

**Improvement 1.2a: Asymmetric Trade Route Science**
- Trade routes provide **both** science yield AND a research cost reduction
- Double-dipping effect not clearly communicated
- VP has GreatScientistBeakerModifier (25-50%) to amplify this further
- **Opportunity**: Consider separate tracking for "science yield" vs "research acceleration"

**Improvement 1.2b: Missing Modifiers in Tooltip**
- Many multipliers (promotions, policies, wonders) don't appear in tech cost tooltip
- Example: Babylon civ bonus `GetTechResearchModifier()` hidden in UI
- **Opportunity**: Expand TechPopup.lua to show all modifier sources

---

## 2. RESEARCH PROGRESS & OVERFLOW SYSTEM

### 2.1 Progress Tracking

**Location**: CvTeamTechs::SetResearchProgressTimes100 / GetResearchProgressTimes100

#### Current System:
- Progress tracked in hundredths (Times100) to preserve precision
- Progress = Beakers accumulated / 100
- Stored per-team, not per-player (shared across team members)
- Overflow from previous tech automatically added to next tech's progress

#### Formula for Completion:
```
Overflow = (CurrentProgress - (CostWithModifiers × 100)) / OverflowDivisor
```

#### Issues:

**Issue 2.1a: Overflow Divisor Mechanism Unclear**
- Line 2427-2436: Complex conditional logic for overflow handling
  ```cpp
  if (iOverflow > iPlayerOverflow) {
      iOverflow = (iOverflow - iPlayerOverflow) + (iPlayerOverflow * 100 / iPlayerOverflowDivisorTimes100);
  } else {
      iOverflow = iOverflow * 100 / iPlayerOverflowDivisorTimes100;
  }
  ```
- Purpose: Distinguish between beakers from THIS turn vs accumulated overflow
- **Problem**: Logic is brittle; edge cases with multi-player teams not well tested
- **Severity**: MEDIUM - Potential for overflow bugs in edge cases

**Issue 2.1b: Team-Level Progress vs Player-Level Contributions**
- Progress is **team-shared**, but individual players accumulate overflow
- When a tech completes, overflow is added to INDIVIDUAL player (line 2439)
- **Problem**: If player A researches 80% and player B researches 20%, player B gets full overflow
- **Fairness Issue**: Players in a team don't see proportional overflow return
- **Severity**: MEDIUM - Design flaw in multiplayer co-op

**Issue 2.1c: No Progress Cap / Integer Overflow Risk**
- Line 2429: `m_paiResearchProgressTimes100[eIndex] = range(iNewValue, 0, INT_MAX);`
- With large science output (>10,000/turn), can hit INT_MAX in late game
- Line 2418 uses `long long` to prevent overflow during calculation, but storage is `int`
- **Problem**: Potential crash or data corruption in long games with mods increasing science
- **Severity**: LOW - Unlikely in vanilla but critical for science-heavy mods

---

### 2.2 Beaker Calculation & Collection

**Location**: CvPlayer.cpp (various DoTurn methods)

#### Sources of Beakers:
1. **City Science Yields**
   - Primary: `GetYieldRateTimes100(YIELD_SCIENCE)` per city
   - Modified by: buildings, specialists, tile improvements, trade routes, espionage
   
2. **Great Scientist Conversion**
   - Field: `BaseBeakersTurnsToCount` (3 turns for scientists in VP)
   - Beakers granted: `GetDiscoverScience(eUnit)`
   - **No UI tooltip for how this is calculated**

3. **Trade Route Science Bonuses**
   - Internal trade: small % of origin city's science output
   - International trade: scales with tech difference & influence
   - Source: `GetInternationalTradeRouteScience()`

4. **Event/Wonder Grants**
   - Bonuses from world wonders (Oxford, Porcelain Tower, etc.)
   - Event-based grants (not tracked in code)

#### Issues:

**Issue 2.2a: Beaker Calculation Not Synchronized Across UI**
- TopPanel.lua calculates: `sciencePerTurnTimes100 = g_activePlayer:GetScienceTimes100()`
- TechPopup.lua uses: `pTeamTechs:GetResearchCost(techID)` + progress
- **Problem**: If science yield sources change mid-turn, UI shows stale values
- **Severity**: LOW - Cosmetic, but confusing for players

**Issue 2.2b: Great Scientist Beaker Bonus Not Moddable Easily**
- Hard-coded turn count (3) in BaseBeakersTurnsToCount
- VP adds `GreatScientistBeakerModifier` policy bonus (25-50%)
- **Opportunity**: Expose as tunable define like `SCIENTIST_BEAKER_TURN_RATE`

**Issue 2.2c: Science Overflow Not Visualized**
- Line 626 (CvPlayer.cpp): `changeOverflowResearch(iScience)` happens silently
- Players see only "X beakers towards next tech" but not overflow accumulation
- **Opportunity**: Add UI indicator for pending overflow research

---

## 3. TECHNOLOGY PREREQUISITES & DEPENDENCIES

### 3.1 Prerequisite Systems

**Location**: CvTechClasses.cpp (CvPlayerTechs::CanResearch)

#### Two Prerequisite Types:
1. **OR-Prerequisites** (`Technology_ORPrereqTechs`)
   - Player needs **at least ONE** of the listed techs
   - Example: Writing can come from Alphabet **OR** Drama
   - Max defined by `NUM_OR_TECH_PREREQS` (6 in CP/VP)

2. **AND-Prerequisites** (`Technology_PrereqTechs`)
   - Player needs **ALL** of the listed techs
   - Example: Medicine requires both Biology **AND** Alchemy
   - Max defined by `NUM_AND_TECH_PREREQS` (6 in CP/VP)

#### Prerequisite Resolution:

**Function**: CvTeamTechs::GetTechsToResearchFor (recursive)
- Computes full dependency chain to a target tech
- Used by AI for tech planning
- **Limit**: `iMaxSearchDepth` parameter prevents infinite recursion

#### Issues:

**Issue 3.1a: No Prerequisite Conflict Detection**
- No validation that OR/AND chains don't create impossible scenarios
- Example vulnerability: Tech A requires (B **OR** C) **AND** (D **OR** E)
  - If (B, D) are mutually exclusive via other chains, could create dead-end
- **Problem**: Mod creators can accidentally break tech trees
- **Severity**: MEDIUM - No error checking at parse time

**Issue 3.1b: UI Doesn't Show Full Prerequisite Chain**
- TechTree.lua shows direct prerequisites only
- Doesn't recursively display (e.g., "need Writing → Alphabet → Mining")
- **Opportunity**: Expand TechTree.lua to show full dependency path

**Issue 3.1c: CanResearch Logic Has Priority Bug**
- Line 1438-1459: OR-prereqs checked before AND-prereqs
- If player meets **one OR prereq but missing an AND**, marked as researchable (incorrect)
  ```cpp
  bFoundValid = true; // Found an OR prereq
  // ...later...
  if(ePrereq != NO_TECH) {
      // AND prereq check—should happen first!
  }
  ```
- **Problem**: Edge case where tech marked researchable but isn't
- **Severity**: MEDIUM - Rare but creates UI confusion

**Improvement 3.1d: Repeat Tech Prerequisites**
- `IsRepeat()` flag allows re-researching a tech
- BUT: Prerequisites always required on re-research
- **Opportunity**: Add `IsRepeatPrereqOptional()` flag for mods

---

## 4. TECHNOLOGY EFFECTS & APPLICATIONS

### 4.1 Known Tech Applications

#### Domains Where Tech Status Matters:
1. **Unit/Building Unlock** (primary)
   - `PrereqAndTech` + `PrereqAndTechs(0-5)` on Units/Buildings
   - Checked in: CvPlayer::canConstruct(), isTechRequiredForUnit()

2. **Improvement Construction** (secondary)
   - Via Build entries with `TechPrereq` field
   - Example: Oil Well requires Chemistry

3. **AI Strategy Gating** (tertiary)
   - `EconomicAI::DoTurn()` checks `GetTechPrereq()` / `GetTechObsolete()`
   - Strategies only active if prereq researched & obsolete not researched

4. **Great Person Unlocks**
   - Writers Guild / Artists Guild / Musicians Guild all have tech gates
   - CvEconomicAI lines 4948-4972: checks via `GetPrereqAndTech()`

5. **Trade Route Modifiers**
   - `GetNumInternationalTradeRoutesChange()` unlocks via tech
   - Checked in: CvPlayer::GetMaxTrade()

#### Issues:

**Issue 4.1a: Inconsistent Tech Requirement Checking**
- `HasTech()` sometimes checks **team techs**, sometimes **player techs**
- Example: CvPlayer::canConstruct() uses `HasTech()` (player)
- But: CvEconomicAI uses `GetTeamTechs()->HasTech()` (team)
- **Problem**: In some code paths, tech granted to team but player still blocked
- **Severity**: LOW - Mostly cosmetic, but edge case in hot-join multiplayer

**Issue 4.1b: No Tech Effect Tooltip Registry**
- Tech effects are scattered across:
  - Unit/Building definitions
  - Hardcoded game logic
  - Policy modifiers
  - Wonder bonuses
- **Opportunity**: Create centralized effect resolver for tooltips

**Issue 4.1c: Feature/Terrain Tech Obsolescence Not Tracked**
- Some features/terrain have `TechObsolete` field
- Changes like fallout cleanup: "advances with tech X"
- **Not visible in TechTree UI**
- **Opportunity**: Display environmental effects in tech hover

---

## 5. EUREKA & INSPIRATION SYSTEM (MOD_CIV6_EUREKAS)

### 5.1 Current Implementation

**Status**: Optional feature (default OFF), Civ6-inspired

#### Mechanics:
- Each tech has `EurekaPerMillion` value (0-1000000)
- Counter incremented by game events: city build, unit kill, tech trade, etc.
- Formula (line 2511): 
  ```cpp
  EurekaDiscount = (1000000 - (EurekaPerMillion × Counter) / NumTeamMembers) / 10000
  ```
- Result: 0-100 percentage point discount to cost

#### Issues:

**Issue 5.1a: Eureka Counter Not Exposed to UI**
- Counter exists but never shown in tech tooltip
- Players don't know if they're halfway to eureka discount
- **Opportunity**: Add TXT_KEY_EUREKA_PROGRESS to TechPopup.lua

**Issue 5.1b: Team Member Scaling Unexpected**
- Dividing by `NumTeamMembers` means 2-player team gets **half** the discount per action
- Intended (balance), but not explained anywhere
- **Opportunity**: Document in tooltip or help text

**Issue 5.1c: Eureka Events Hardcoded in Multiple Files**
- Unit kill triggers: CvUnit.cpp (scattered)
- City build triggers: CvCity.cpp (scattered)
- Trade triggers: CvDealAI.cpp
- **Opportunity**: Centralize in CvTeamTechs::DoEurekaCheck(eEvent) pattern

---

## 6. TECHNOLOGY PRIORITY & WEIGHTING (AI)

### 6.1 Tech AI System

**Location**: CvTechAI.cpp

#### Weighting System:
1. **Flavor-Based Weighting**
   - AddFlavorWeights(): Applies flavor (FLAVOR_EXPANSION, FLAVOR_SCIENCE, etc.) to each tech
   - Weights propagated recursively to prerequisites with degradation
   - Weight divisor: `TECH_WEIGHT_PROPAGATION_LEVELS` (default 3 levels deep)

2. **Cost Reweighting** (ReweightByCost)
   - If overflow science > 2× per-turn rate → prefer expensive techs
   - Adjusts weight inversely to turns-left
   - Prevents "wasting" science on cheap late-game techs

3. **Final Tech Selection** (RecommendNextTech)
   - Highest weight wins (or random if tied)
   - Logged to debug files

#### Issues:

**Issue 6.1a: Weight Propagation Algorithm Unintuitive**
- Line 295: `iPropagatedWeight /= iPrereqCount` **only if not IsRepeat()**
- Repeat techs don't split weight → distorts priority
- **Problem**: AI might over-value repeatable prerequisites
- **Severity**: MEDIUM - Affects AI tech pacing

**Issue 6.1b: No Observable Weighting in UI**
- UI shows recommended next tech but not **why**
- Would help mod creators debug tech trees
- **Opportunity**: Add debug overlay showing tech weights

**Issue 6.1c: Flavor Degradation Hardcoded**
- Propagation % hardcoded at function call sites
- No central "propagation rate" tuning
- **Opportunity**: Expose as define (e.g., TECH_WEIGHT_PROPAGATION_PERCENT)

---

## 7. RESEARCH AGREEMENT & TECH TRADING

### 7.1 Trade Mechanics

**Location**: CvDealAI.cpp, CvPlayer::DoResearchAgreement()

#### Trade Types:
1. **Research Agreement**
   - Both players commit gold per turn
   - Both receive same flat beaker bonus when completed
   - Bonus = `GetMedianTechResearch()` / number of eligible techs
   
2. **Direct Tech Trade**
   - Immediate exchange of one tech for another
   - Requires `IsTechTrading()` enabled (tech-dependent)
   - Cost: Gold + demand modifiers

#### Issues:

**Issue 7.1a: MedianTechResearch Calculation Inefficient**
- Recalculated every trade negotiation
- Iterates through ALL techs checking CanResearch()
- **Problem**: Expensive operation in late game (300+ techs)
- **Opportunity**: Cache result, invalidate on tech research

**Issue 7.1b: No Trading Treaty Transparency**
- Players can't see AI tech ratios offered in trade
- No tooltip showing "AI wants X, offering Y because Z"
- **Opportunity**: Add CvDealViewer enhancement for tech trades

**Issue 7.1c: Tech Trade Imbalance with Cost Modifiers**
- Tech A costs 5000 beakers for Player 1, 3000 for Player 2
- Trade value doesn't account for this disparity
- **Opportunity**: Normalize trade value to base research costs

---

## 8. DIFFICULTY & HANDICAP SCALING

### 8.1 Handicap-Based Modifiers

**Location**: HandicapInfos.xml, CoreDifficultyChanges.xml

#### Key Modifiers:
- `ResearchPercent`: 60-200% (default 100%)
- `ResearchPerEraModifier`: -20 to 0 (becomes MORE expensive in later eras on lower difficulties)
- `TechCatchUpMod`: 0-100 (bonus catch-up reduction per era)
- `AIResearchPercent` / `AIResearchPerEraModifier`: Independent AI scaling

#### Issues:

**Issue 8.1a: Catch-Up Modifier Scales Inversely with Difficulty**
- Intended: lower difficulties get catch-up help
- **Problem**: TechCatchUpMod NOT applied to humans on high difficulty
  - Line 16888 (CvPlayer.cpp): `iExtraCatchUP = getHandicapInfo().getTechCatchUpMod();`
  - Only applies to player's own handicap, **not game handicap for balance**
- **Opportunity**: Clarify if catch-up should be symmetric

**Issue 8.1b: Era Scaling Compounds Disproportionately**
- Era 3 (Classical): ResearchPerEraModifier × 3 = -60% (mild)
- Era 18 (Information): ResearchPerEraModifier × 18 = -360% (brutal)
- **Problem**: Difficulty doesn't scale linearly with progression
- **Severity**: MEDIUM - Dwarfs other modifiers in modern era
- **Opportunity**: Cap or make logarithmic

**Issue 8.1c: No Handicap for Science-Deficit Civs**
- Civs with low FLAVOR_SCIENCE (e.g., Zulu) get no catch-up bonus
- **Opportunity**: Add FLAVOR_SCIENCE → catch-up mapping

---

## 9. SUMMARY OF CRITICAL ISSUES

| Issue | Component | Severity | Impact | Fix Complexity |
|-------|-----------|----------|--------|-----------------|
| 1.1a | City count modifier growth | HIGH | Balance: tall empires impossible | MEDIUM |
| 1.1b | Era scaling aggressiveness | MEDIUM | Catch-up too punitive | LOW |
| 1.1c | Int overflow in rounding | LOW-MEDIUM | Edge case in very long games | HIGH |
| 2.1a | Overflow divisor logic | MEDIUM | Potential bugs in multiplayer | HIGH |
| 2.1b | Team progress distribution | MEDIUM | Unfair in co-op teams | MEDIUM |
| 3.1c | CanResearch priority bug | MEDIUM | Tech tree inconsistency | MEDIUM |
| 5.1a | Eureka UI exposure | LOW | UX: players unaware of progress | LOW |
| 6.1a | Weight propagation | MEDIUM | AI tech pacing off | MEDIUM |
| 8.1b | Era scaling compounds | MEDIUM | Late-game difficulty spike | MEDIUM |

---

## 10. RECOMMENDED IMPROVEMENTS

### Quick Wins (LOW EFFORT):
1. **Add Eureka Progress Tooltip** → Line 2170 of InfoTooltipInclude.lua
2. **Document Team Member Penalty** → Add to TechPopup.lua hover text
3. **Cache MedianTechResearch** → Invalidate on tech research in CvPlayerTechs

### Medium Effort:
4. **Fix CanResearch Priority** → Check AND-prereqs before OR-prereqs
5. **Add Debug Tech Weight Overlay** → Extend TechTree.lua
6. **Clarify Catch-Up Scaling** → Add tunable cap to ResearchPerEraModifier

### High Effort (Consider for Next Version):
7. **Refactor City Count Modifier** → Change from additive to multiplicative option
8. **Centralize Eureka Events** → Create CvTeamTechs::DoEurekaCheck() dispatcher
9. **Add Tech Effect Registry** → Query system for tooltips (unit/building unlocks)
10. **Team Overflow Fairness** → Track per-player contributions, distribute overflow proportionally

---

## 11. TESTING RECOMMENDATIONS

### Unit Tests:
- [ ] Test tech cost calculation with 40+ cities
- [ ] Test overflow divisor with multi-player teams
- [ ] Test CanResearch with complex prereq chains
- [ ] Test eureka discount formula at boundaries (0%, 100%)

### Integration Tests:
- [ ] Verify research completion triggers all callbacks
- [ ] Verify UI tooltips match actual cost calculations
- [ ] Verify AI tech selection on every difficulty level
- [ ] Verify trade routes don't double-count science

### Regression Tests:
- [ ] Save/load research progress without data corruption
- [ ] Verify catch-up mechanic activates at correct thresholds
- [ ] Verify Great Scientist beaker conversion consistent across UI
- [ ] Verify multiplayer team research allocation fair

---

## Appendix A: Key Code Locations

| System | File | Function | Lines |
|--------|------|----------|-------|
| Tech Cost | CvTechClasses.cpp | GetResearchCost() | 2456-2524 |
| Player Cost Mod | CvPlayer.cpp | calculateResearchModifier() | 16875-16893 |
| Progress Tracking | CvTeamTechs | SetResearchProgressTimes100() | 2380-2439 |
| Prerequisite Check | CvPlayerTechs | CanResearch() | 1412-1484 |
| Tech AI Weighting | CvTechAI.cpp | ReweightByCost() | 327-365 |
| Eureka Discount | CvTeamTechs | GetEurekaDiscount() | 2532-2541 |
| UI - Tech Popup | TechPopup.lua | - | 80-130 |
| UI - TechTree | TechTree.lua | - | 626-630 |
| UI - TopPanel | TopPanel.lua (EUI) | - | 984-990 |

---

## Appendix B: Related Configuration Files

- `(1) Community Patch/Database Changes/Difficulty/CoreDifficultyChanges.xml` - Handicap scaling
- `(2) Vox Populi/Database Changes/Difficulty/DifficultyChanges.xml` - VP-specific scaling
- `(1) Community Patch/Database Changes/NewCustomModOptions.xml` - Feature flags (CIV6_EUREKAS, etc.)
- `(2) Vox Populi/Database Changes/Policies/Rationalism.sql` - GreatScientistBeakerModifier
- `CvGameCoreDLL_Expansion2/Lua/TechPopup.lua` - Tech info UI

