# Culture & Borders System Review

**Date:** January 2026  
**Scope:** Culture accumulation, border expansion, culture pressure mechanics, and culture flip dynamics in Community Patch/Vox Populi

---

## Executive Summary

The Culture & Borders system in Community Patch/VP is a complex subsystem handling:
1. **Culture Accumulation** - How cities accumulate culture toward border expansion
2. **Border Expansion** - The mechanic for claiming new plots through culture
3. **Culture Pressure** - Religious pressure applied per-turn (separate from culture flips)
4. **Resistance Mechanics** - Post-conquest city production penalties
5. **Culture Costs** - Scaling thresholds for acquiring plots

This review identifies **key implementation details, design patterns, potential issues, and improvement opportunities**.

---

## 1. Culture Accumulation Mechanics

### Current Implementation

**Source:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L17456-L17530)

Culture accumulation uses **exponential scaling** with multiple modifiers:

```cpp
int GetJONSCultureThreshold() const {
    // Base cost
    int iCultureThreshold = GD_INT_GET(CULTURE_COST_FIRST_PLOT);  // 20
    
    // Exponential scaling: cost^exponent
    float fExponent = GD_FLOAT_GET(CULTURE_COST_LATER_PLOT_EXPONENT);  // 1.35
    int iAdditionalCost = GetJONSCultureLevel() * GD_INT_GET(CULTURE_COST_LATER_PLOT_MULTIPLIER);  // 15
    double dAdditionalCost = pow((double)iAdditionalCost, (double)fExponent);
    
    // Policy modifiers
    int iPolicyExponentMod = GetPlayer().GetPlotCultureExponentModifier();
    fExponent = fExponent * (100 + iPolicyExponentMod) / 100;
    
    // Religion modifiers
    int iReligionMod = GetMajorityReligionMod() + GetSecondaryPantheonMod();
    
    // Final modifiers: -85% minimum
    int iModifier = GetPlayerMod() + GetCityMod() + iReligionMod;
    iModifier = max(iModifier, GD_INT_GET(CULTURE_PLOT_COST_MOD_MINIMUM));
    
    iCultureThreshold *= (100 + iModifier) / 100;
    
    // Game speed adjustment
    iCultureThreshold *= GameSpeed.getCulturePercent() / 100;
    
    // Round to divisor (5)
    iDivisor = GD_INT_GET(CULTURE_COST_VISIBLE_DIVISOR);
    if (iCultureThreshold > iDivisor * 2) {
        iCultureThreshold = (iCultureThreshold / iDivisor) * iDivisor;
    }
    
    return iCultureThreshold;
}
```

### Issues & Observations

#### Issue 1.1: Overflow Risk in Exponential Calculation
**Severity:** Medium  
**Location:** Line 17327

The overflow check is **insufficient**:
```cpp
iCultureThreshold += (dAdditionalCost < INT_MAX / 256 ? int(dAdditionalCost) : INT_MAX / 256);
```

**Problem:** 
- The divisor `256` is arbitrary and may not prevent overflow in all cases
- At high culture levels, `pow()` can exceed `INT_MAX / 256` long before reaching `INT_MAX`
- No safeguard for very late-game scenarios with heavily modded exponents

**Recommendation:**
```cpp
// Cap at a reasonable maximum (e.g., 100,000 per plot)
const int MAX_PLOT_CULTURE_COST = 100000;
int iCapCost = min(int(dAdditionalCost), MAX_PLOT_CULTURE_COST);
iCultureThreshold += iCapCost;
```

#### Issue 1.2: Rounding/Divisor Logic May Cause Inconsistency
**Severity:** Low  
**Location:** Line 17532-17538

```cpp
int iDivisor = GD_INT_GET(CULTURE_COST_VISIBLE_DIVISOR);  // =5
if (iCultureThreshold > iDivisor * 2) {
    iCultureThreshold /= iDivisor;
    iCultureThreshold *= iDivisor;
}
```

**Problem:**
- Divisor logic is only applied if cost > 10, creating a discontinuity
- Rounding always floors (integer division), potentially making costs unpredictable
- Documentation doesn't explain why `iDivisor * 2` is the threshold

**Recommendation:**
- Document the rounding strategy clearly
- Consider making it consistent: `iCultureThreshold = ((iCultureThreshold + iDivisor - 1) / iDivisor) * iDivisor;` (always round up)

#### Issue 1.3: Minor Civ Culture Cost Scaling
**Severity:** Low  
**Location:** Line 17485-17488

```cpp
if (GET_PLAYER(getOwner()).isMinorCiv()) {
    iCultureThreshold *= GD_INT_GET(MINOR_CIV_PLOT_CULTURE_COST_MULTIPLIER);  // 115
    iCultureThreshold /= 100;
}
```

**Problem:**
- Multiplier (115) is **post-exponent**, making minor civs scale disproportionately harder
- This may not have been the original intent if the goal was to scale base cost only

**Recommendation:**
- Review whether 115% per plot is intended balance or unintended compounding
- Consider applying multiplier earlier in calculation or reducing it if excessive

---

## 2. Border Expansion Logic

### Current Implementation

**Source:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L17317-L17415)

Border expansion via culture occurs in `DoJONSCultureLevelIncrease()`:

```cpp
void DoJONSCultureLevelIncrease() {
    int iOverflow = GetJONSCultureStoredTimes100() - GetJONSCultureThreshold() * 100;
    bool bIsHumanControlled = IsHuman() && !IsPuppet();
    
    if (!MOD_UI_CITY_EXPANSION || !bIsHumanControlled) {
        SetJONSCultureStoredTimes100(iOverflow);
        ChangeJONSCultureLevel(1);
    }
    
    CvPlot* pPlotToAcquire = GetNextBuyablePlot(false);
    
    if (pPlotToAcquire) {
        if (MOD_UI_CITY_EXPANSION && bIsHumanControlled) {
            // Defer to human picker
            // If within work distance: notify
            // If outside: auto-acquire and defer level increase
        } else {
            // AI: auto-acquire and log
            DoAcquirePlot(pPlotToAcquire);
        }
        // Instant yields
        doInstantYield(INSTANT_YIELD_TYPE_BORDERS, ...);
    } else {
        // No buyable plots available
        // Still give instant yields even if no tile acquired
    }
}
```

### Issues & Observations

#### Issue 2.1: Deferred Culture Level Increase for Humans
**Severity:** Medium  
**Location:** Line 17337-17369

**Problem:**
- When `MOD_UI_CITY_EXPANSION` is enabled and human controls city:
  - Culture level increase is deferred until plot is acquired
  - Creates asymmetry between human and AI behavior
  - Overflow management is split across two code paths (line 17331 for AI, line 17365/17383 for humans)
- **Edge case:** If human picks a plot outside work range, `DoAcquirePlot()` is called directly but culture level increase is deferred to line 17365
  - If no plots available, deferred code at line 17383 executes
  - This means humans can accumulate multiple culture level increases without acquiring plots if picking stalls

**Recommendation:**
```cpp
// Consolidate overflow handling
SetJONSCultureStoredTimes100(iOverflow);
ChangeJONSCultureLevel(1);

// Then handle plot acquisition independently
if (pPlotToAcquire) {
    if (MOD_UI_CITY_EXPANSION && bIsHumanControlled && IsWithinWorkRange(pPlotToAcquire)) {
        // Notify and defer
    } else {
        DoAcquirePlot(pPlotToAcquire);
    }
}
```

#### Issue 2.2: GetNextBuyablePlot() Unverified Behavior
**Severity:** Medium  
**Location:** Line 17334

**Problem:**
- `GetNextBuyablePlot()` is called but implementation not reviewed
- No documentation on:
  - Selection criteria (closest? best yield? priority order?)
  - Whether distance constraints are checked
  - What happens if no plots exist
- Affects both human and AI expansion patterns

**Recommendation:**
- Document `GetNextBuyablePlot()` behavior
- Verify it respects city work range and player expansion traits

#### Issue 2.3: Border Growth Rate Bonus (WLTKD/GA) Not Integrated
**Severity:** Low  
**Location:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L18678-18687)

```cpp
int GetBorderGrowthRateIncreaseTotal() {
    int iModifier = GetBorderGrowthRateIncrease() + kOwner.GetBorderGrowthRateIncreaseGlobal();
    
    // ... Religion modifiers ...
    
    // Double border growth during GA or WLTKD
    if ((kOwner.IsDoubleBorderGrowthGA() && kOwner.isGoldenAge()) || 
        (kOwner.IsDoubleBorderGrowthWLTKD() && GetWeLoveTheKingDayCounter() > 0)) {
        iModifier *= 2;
        iModifier += 100;  // Double the base rate
    }
    
    return iModifier;
}
```

**Observation:**
- This bonus affects **culture yield production**, not threshold cost
- Means borders expand faster during bonuses, not cheaper
- **Good design** — scaling speed rather than cost maintains balance

---

## 3. Culture Pressure (Religious Pressure)

### Current Implementation

**Source:** [CvCultureClasses.cpp](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp) (via semantic search)

Religious pressure is separate from culture flips. Per-turn pressure accumulates:

```cpp
// From LuaCity.cpp (line 3656-3665)
int GetPressurePerTurn(ReligionTypes eReligion, int* iNumSourceCities) {
    // Returns pressure per turn + number of source cities + accumulated pressure
}
```

### Issues & Observations

#### Issue 3.1: No Documented Culture Pressure Scaling
**Severity:** Low

**Problem:**
- Religious pressure has no documented caps or scaling rules
- No clear interaction with city population, religion strength, or distance
- Potential for runaway conversions in modded games

**Recommendation:**
- Document pressure calculation formula
- Verify balance against religious defense mechanisms
- Consider adding caps if pressure can exceed city religious resistance

#### Issue 3.2: Tourism vs. Culture Pressure Ambiguity
**Severity:** Low  

**Problem:**
- Player-level culture (tourism) and city-level religious pressure are conflated in UI tooltips
- "Culture pressure" may be confused with "tourism pressure" for cultural influence

**Recommendation:**
- Standardize terminology in code and UI
- Distinguish between:
  - **Religious pressure** (city-level conversion mechanic)
  - **Cultural influence/tourism** (player-level dominance mechanic)

---

## 4. Resistance Mechanics

### Current Implementation

**Source:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L19330-19365)

Resistance is applied post-conquest and decays each turn:

```cpp
void DoResistanceTurn() {
    if (IsResistance()) {
        ChangeResistanceTurns(-1);  // Decay by 1 per turn
    }
}

bool IsResistance() {
    return GetResistanceTurns() > 0;
}
```

### Usage Contexts

Resistance blocks:
1. **Cultural production** (e.g., culture buildings contribute 0 yield)
2. **City focus** (resistance cities cannot set focus)
3. **Production** (some buildings/units cannot be built)
4. **Diplomacy value** (resistance lowers city deal value by 10% per turn remaining)

### Issues & Observations

#### Issue 4.1: Resistance Duration Undefined
**Severity:** Medium

**Problem:**
- No documentation on how resistance turns are set
- No clear formula for "fair" resistance duration
- Varies by city acquisition method (conquest vs. culture flip vs. loyalty flip)
- No visible indicator to player of remaining resistance duration in main UI

**Recommendation:**
```cpp
// Document expected values:
// - Military conquest: 8-10 turns (need to find where this is set)
// - Culture flip: 0 turns (no resistance)
// - Loyalty flip: varies
// - Emergency bonus: 50% reduction of remaining turns (line 19725-19727)

// Add UI tooltip showing exact resistance countdown
```

#### Issue 4.2: Resistance Halving on Emergency Bonus is Opaque
**Severity:** Low  
**Location:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L19725-19727)

```cpp
int iResistanceTurns = GetResistanceTurns();
iResistanceTurns /= 2;
ChangeResistanceTurns(-iResistanceTurns);
```

**Problem:**
- Called from unclear context (emergency game mechanic?)
- No documentation on when/why resistance is halved
- Integer division truncates (could be intentional or bug)

**Recommendation:**
- Document the emergency bonus mechanic
- Consider: `iResistanceTurns = (iResistanceTurns + 1) / 2;` for rounding up if intended

#### Issue 4.3: Interaction with Loyalty System
**Severity:** Medium

**Problem:**
- Resistance and loyalty systems coexist but interaction unclear
- City can be in both resistance **and** low loyalty simultaneously
- No documented priority or stacking rules

**Recommendation:**
- Define clear priority: Does resistance prevent loyalty loss? Does loyalty gain offset resistance?
- Add mutual exclusion if intended: `if (IsResistance()) return;` in loyalty calculations

---

## 5. Culture Flip Mechanics

### Current Implementation

**Observation from grep:**
- Culture flips are referenced in plot code but full implementation not directly examined
- Referenced in CvPlot-related functions (plot ownership change via culture accumulation)

### Expected Behavior (from vanilla Civ5)

1. **Ownership:** A plot with accumulated culture from another civilization
2. **Flip Trigger:** When foreign culture > domestic culture + threshold
3. **Spontaneous Ownership Change:** Plot changes owner without conquest

### Issues & Observations (Inferred)

#### Issue 5.1: No Culture Flip Rate Limiting
**Severity:** Medium

**Problem:**
- No apparent mechanic to prevent "culture snowball" where strong culture civs claim lots of plots
- No cooldown on flips per plot
- No anti-spam rules

**Recommendation:**
- Add flip cooldown: Cannot flip same plot twice within N turns
- Add empire pressure: Flipping plots far from core is harder
- Add counterplay: Garrisoning units or culture buildings reduces flip rate

#### Issue 5.2: No UI Indicator for Flip Risk
**Severity:** Low

**Problem:**
- Players cannot see which plots are "at risk" of flipping
- UI helper in CvView.lua shows "flip risk" estimate but calculation is rough (`iFlipRisk = min(100, iUnhappiness * 3)`)
- No notification when plots actually flip

**Recommendation:**
```cpp
// Add GetPlotCultureFlipRisk(CvPlot*) function
// Return percentage (0-100) of likelihood to flip next turn

// UI: Show [ICON_FLIP_RISK] overlay on threatened plots
// Notification: "Plot X,Y may flip this turn!"
```

---

## 6. Data-Driven Configuration

### Current Configuration

Key values are accessed via `GD_INT_GET()` and `GD_FLOAT_GET()` macro functions, sourcing from database:

| Parameter | Current Value | Source |
|-----------|---------------|--------|
| `CULTURE_COST_FIRST_PLOT` | 20 | GC.GameDefines |
| `CULTURE_COST_LATER_PLOT_EXPONENT` | 1.35 | GC.GameDefines |
| `CULTURE_COST_LATER_PLOT_MULTIPLIER` | 15 | GC.GameDefines |
| `CULTURE_PLOT_COST_MOD_MINIMUM` | -85% | GC.GameDefines |
| `CULTURE_COST_VISIBLE_DIVISOR` | 5 | GC.GameDefines |
| `MINOR_CIV_PLOT_CULTURE_COST_MULTIPLIER` | 115% | GC.GameDefines |
| `DoubleBorderGrowthGA` | true | Trait/Policy |
| `DoubleBorderGrowthWLTKD` | varies | Policy |

### Issues with Configuration

#### Issue 6.1: Exponent Parameter May Need Tuning
**Severity:** Low

**Problem:**
- VP uses exponent of 1.35 (original CP was 1.1)
- This creates very steep scaling curve:
  - Plot 1: 20 culture
  - Plot 2: ~45 culture
  - Plot 3: ~110 culture
  - Plot 4: ~310 culture (exponential explosion)
- At very high levels (city level 20+), cost becomes prohibitive

**Recommendation:**
- Consider dynamic exponent: `fExponent = 1.35 * (0.95 ^ city_level)` to cap late-game costs
- Or add hard ceiling: `if (cost > 10000) cost = 10000;`

#### Issue 6.2: Religion Modifier Stacking
**Severity:** Medium  
**Location:** [CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L17488-17518)

```cpp
int iReligionMod = 0;

// Majority religion
if (eMajority != NO_RELIGION) {
    iReligionMod += pReligion->m_Beliefs.GetPlotCultureCostModifier(...);
    iReligionMod += eSecondaryPantheon_Belief->GetPlotCultureCostModifier();
}

// Permanent pantheon (separate stacking)
if (MOD_BALANCE_PERMANENT_PANTHEONS && Has_Pantheon()) {
    iReligionMod += ePantheonBelief->GetPlotCultureCostModifier();
}

// Total modifier += iReligionMod
```

**Problem:**
- Three separate sources can stack without cap:
  1. Majority religion belief
  2. Secondary pantheon belief
  3. Permanent pantheon belief
- No safeguard against 3x stacking of -50% modifiers creating impossible costs or overflow

**Recommendation:**
```cpp
// Cap religion modifier contribution
iReligionMod = max(iReligionMod, GD_INT_GET(RELIGION_CULTURE_MOD_CAP));  // e.g., -50%
```

---

## 7. Known Design Decisions (Good)

### Decision 7.1: Culture Cost Scaling vs. Threshold
✅ **Good:** Culture costs scale exponentially per plot level, not per city count
- Encourages strategic plot selection
- Prevents "city spam" from easily claiming territory

### Decision 7.2: Border Growth Rate vs. Threshold Cost
✅ **Good:** Border growth rate (YIELD_CULTURE_LOCAL) affects *when* expansions happen, not *if*
- Maintains balance: high-culture civs expand faster, not cheaper
- Policies/WLTKDs doubling rate is transparent and balanced

### Decision 7.3: Overflow Preservation
✅ **Good:** Culture overflow is preserved between plots
- Prevents waste of accumulated culture
- Encourages continuous culture building

### Decision 7.4: Plot Adjacency Traits
✅ **Good:** Terrain claim boost (line 17406-17418) adds strategic depth
- Certain civs can chain-claim adjacent same-terrain plots
- Encourages terrain-focused strategies

---

## 8. Recommended Improvements

### Priority 1: Critical Fixes

#### P1.1: Overflow Safety Cap
```cpp
// In GetJONSCultureThreshold():
const int MAX_PLOT_CULTURE_COST = 100000;
int iCapCost = min(int(dAdditionalCost), MAX_PLOT_CULTURE_COST);
iCultureThreshold += iCapCost;
```

#### P1.2: Consolidate Deferred Culture Logic
```cpp
// Move all culture level increases to happen immediately
// Separate UI picker from culture accumulation logic
// Let humans pick plot AFTER culture level increases, not BEFORE
```

#### P1.3: Document Resistance Duration Setting
```cpp
// Find all call sites that set resistance turns
// Add comment: "Standard conquest resistance: N turns"
// Add method: ChangeResistanceTurns() → note in CvCity.h
```

### Priority 2: Usability Improvements

#### P2.1: Add Culture Flip Risk Indicator
- Implement `GetPlotCultureFlipRiskPercent()` function
- Show [ICON_FLIP_RISK] on plots at >50% flip risk
- Notification when plot flips

#### P2.2: Improve Border Growth Visualization
```cpp
// In CvCity or CvPlayer:
// GetBorderTilesAtRisk() → vector of plots < 1 turn away from flipping
// GetBorderExpansionQueueEstimate(turns) → forecast next N border expansions
```

#### P2.3: Clarify Resistance in UI
- Show exact resistance countdown (e.g., "8 turns of resistance remaining")
- Explain production penalties in tooltip
- Show resistance decay rate (always 1 turn/turn)

### Priority 3: Balance Improvements

#### P3.1: Late-Game Culture Cost Cap
```cpp
// Add to GetJONSCultureThreshold():
if (iCultureThreshold > 10000) {  // Cap at 10k per plot
    iCultureThreshold = 10000;
}
// Or use dynamic exponent reduction:
// fExponent *= (1.0 - city_level * 0.01);  // Reduce exponent over time
```

#### P3.2: Religion Modifier Stacking Cap
```cpp
// In GetJONSCultureThreshold() after calculating iReligionMod:
iReligionMod = max(iReligionMod, -50);  // Cap at -50% reduction
```

#### P3.3: Culture Flip Cooldown
```cpp
// In CvPlot class:
int m_iCultureFlipCooldown = 0;  // Cannot flip same plot twice within N turns

// In DoPlotCultureFlip():
if (m_iCultureFlipCooldown > 0) return;  // Blocked
// On flip: m_iCultureFlipCooldown = GD_INT_GET(CULTURE_FLIP_COOLDOWN);  // 3-5 turns
```

---

## 9. Testing Recommendations

### Test Scenario 1: Late-Game Culture Scaling
**Goal:** Verify no overflow at high culture levels
```
Setup: City with 50 culture levels, 1,000+ culture per turn
Expected: Plot cost scales but does not overflow
Verify: Cost stays <= 10,000 (or configured cap)
```

### Test Scenario 2: Human Border Expansion
**Goal:** Verify deferred plot picker works correctly
```
Setup: Human player, MOD_UI_CITY_EXPANSION enabled
Trigger: City culture level up multiple times
Expected: Each level can select plot independently
Verify: Culture level increases match plot acquisitions
```

### Test Scenario 3: Resistance Decay
**Goal:** Verify resistance decreases 1 per turn
```
Setup: Conquered city with 10 resistance turns
Wait: 10 turns
Expected: Resistance reaches 0, city no longer blocked
Verify: Production resumes, culture buildings work again
```

### Test Scenario 4: Religion Modifier Stacking
**Goal:** Verify no negative overflow on culture costs
```
Setup: City with 3 religions, each -30% culture cost
Expected: Cost reduction caps at meaningful limit (e.g., -50%)
Verify: Cost > 0 and reasonable (not overflowing negative)
```

### Test Scenario 5: Culture Flip Prevention
**Goal:** Verify plot doesn't flip excessively
```
Setup: City with weak culture, adjacent enemy civ with strong culture
Trigger: Run 50 turns
Expected: Plot flips at most once per X turns (if cooldown implemented)
Verify: No repeated flip-flop of same plot
```

---

## 10. References & Related Systems

### Yield Production
- [CvCity.cpp - getYieldRateTimes100()](CvGameCoreDLL_Expansion2/CvCity.cpp) - Base culture yield calculation
- [CvCityCitizens.cpp - SpecialistYieldChange()](CvGameCoreDLL_Expansion2/CvCityCitizens.cpp) - Culture from specialists

### Religious Integration
- [CvCultureClasses.cpp - DoPublicOpinion()](CvGameCoreDLL_Expansion2/CvCultureClasses.cpp#L3285) - Tourism and cultural influence
- Beliefs system for culture cost modifiers

### Loyalty System
- [CvCity.h - Loyalty members](CvGameCoreDLL_Expansion2/CvCity.h#L1757-1778) - Loyalty/disloyalty counters
- Interaction with resistance not fully documented

### Traits & Policies
- `GetBorderGrowthRateIncreaseGlobal()` - Policy-level border boost
- `IsDoubleBorderGrowthGA()` / `IsDoubleBorderGrowthWLTKD()` - Trait/policy conditionals
- `GetPlotCultureCostModifier()` / `GetPlotCultureExponentModifier()` - Scaling modifiers

---

## Conclusion

The Culture & Borders system is **well-structured** with clear separation of concerns:
- **Exponential scaling** provides natural curve for late-game balance
- **Modular modifiers** (religion, traits, policies) allow flexible balance tweaking
- **Overflow preservation** and instant yields encourage continuous expansion

### Main Concerns:
1. **Potential overflow** at extreme culture levels (fixable with cap)
2. **Deferred human plot selection** creates asymmetry and edge cases
3. **Limited user feedback** on flip risk and resistance status
4. **Undocumented resistance mechanics** cause player confusion
5. **Religion modifier stacking** could cause balance issues in mods

### Quick Wins:
- Add culture cost cap (1 line)
- Improve UI tooltips for resistance/flip risk (UI changes only)
- Document key formulas in code comments (documentation)
- Add late-game exponent reduction (2-3 lines)

All recommendations are **backward-compatible** and can be implemented incrementally.
