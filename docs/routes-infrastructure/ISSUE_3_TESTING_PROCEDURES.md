# Issue 3 Testing Procedures: Route Maintenance & Strategic Value

**Implementation Date:** January 11, 2026  
**Modified Function:** `CvBuilderTaskingAI::GetRouteDirectives()`  
**Change Location:** Lines 1715-1759 in `CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp`

## Setup Requirements

1. **Rebuild DLL:** Build the modified CvGameCoreDLL_Expansion2 with DEBUG configuration
2. **Enable Logging:** In `My Games/Sid Meier's Civilization V/config.ini`, enable builder AI logging
3. **Test Scenario:** Create a test save or use Earth Standard map
4. **Single-Player Only:** Testing does not require MP setup

## Test Case 1: Economic Profitability (Expected Route: Rail)

### Scenario Setup
- Mid-game (Classical/Medieval era)
- Empire with 20+ GPT baseline income
- Two cities connected by road, no military threats
- Planned railroad route exists with +30 GPT economic benefit
- No pending wars or military pressure

### Test Execution
1. Load game and locate planned route between cities in builder AI logs
2. Verify railroad route value calculation
3. Check that railroad is selected over road

### Expected Results
```
Railroad Route Value = [Base Value: +30 GPT] - [Maintenance: ~10/tile] + [Movement Bonus: 500] 
                     = ~20 - 10 + 500 
                     = +510 (positive)
                     
Road Route Value    = [Base Value: +30 GPT] - [Maintenance: ~2/tile] 
                     = ~28 (positive)

DECISION: Railroad selected (510 > 28)
```

### Pass Criteria
- ✅ Railroad route value > road route value
- ✅ Railroad build directives generated
- ✅ Builders prioritize railroad construction

### Failure Indicators
- ❌ Road selected over railroad
- ❌ No movement bonus applied
- ❌ Incorrect maintenance deduction

---

## Test Case 2: Military Strategic Value (Expected Route: Rail)

### Scenario Setup
- Late Medieval/Renaissance era
- Empire with 15 GPT income (not wealthy)
- Located near aggressive neighbor civ (high military pressure)
- Planned railroad exists with **-20 GPT economic cost** (unprofitable)
- Military pressure: 70+ (simulates imminent war)

### Test Execution
1. Modify game scenario to trigger military pressure (build armies near border)
2. Check builder AI route value calculations in logs
3. Verify railroad is selected despite negative gold yield

### Expected Results
```
Railroad Route Value = [Base Value: -20 GPT] - [Maintenance: ~10/tile] + [Movement Bonus Calc]
                     
Movement Bonus Calculation (not wealthy, high military threat):
  - Base bonus: 500
  - Wealth factor: 100% (15 GPT < 25 GPT threshold) = 500
  - War multiplier: 150% (70 > 50) = 500 × 1.5 = 750
  
Railroad Value = -30 + 750 = +720 (POSITIVE!)

Road Route Value = [Base Value: -20 GPT] - [Maintenance: ~2/tile]
                 = -22 (NEGATIVE)

DECISION: Railroad selected (720 > -22)
```

### Pass Criteria
- ✅ iMilitaryPressure > 50 confirmed
- ✅ Movement bonus scaled to 750 (150% multiplier applied)
- ✅ Railroad value becomes positive due to war multiplier
- ✅ Railroad selected despite initial negative gold

### Failure Indicators
- ❌ iMilitaryPressure not detected correctly
- ❌ War multiplier not applied (bonus stays at 500)
- ❌ Road selected over railroad
- ❌ No movement bonus calculation visible in logs

---

## Test Case 3: Treasury Constraint (Expected Route: Road)

### Scenario Setup
- Late game scenario
- Empire losing 5 GPT per turn (treasury draining)
- Planned railroad with -40 GPT economic cost (unprofitable)
- Sufficient treasury to last ~10 turns more
- No immediate military threats

### Test Execution
1. Create scenario with negative gold per turn
2. Verify bCanAffordNegativeRoute = false
3. Check that railroad is penalized

### Expected Results
```
Gold Status: m_pPlayer->GetTreasury()->GetGoldPerTurn() = -5 (NEGATIVE!)
bCanAffordNegativeRoute = (-5 > 0) = FALSE

Railroad Route Value (before constraint) = -40 - 10 + 500 = +450
Treasury Constraint Applied: bCanAffordNegativeRoute=false && iRailroadValue<0
  → Does NOT apply (iRailroadValue is positive, so constraint doesn't trigger)
  
HOWEVER: If maintenance exceeds movement bonus:
Railroad Route Value (before constraint) = -40 - 10 + 350 = +300 (still positive)

If we had:
Railroad Route Value (before constraint) = -40 - 50 + 350 = -340
Treasury Constraint Applied: iRailroadValue = -340 × 50% = -170

DECISION: Road selected (positive > negative)
```

### Pass Criteria
- ✅ iNetGoldPerTurn < 0 detected
- ✅ bCanAffordNegativeRoute = false
- ✅ Negative-value routes are penalized (50% weight reduction)
- ✅ Road selected when net value becomes negative

### Failure Indicators
- ❌ Treasury constraint not triggered despite negative gold
- ❌ Negative routes still built when empire losing money
- ❌ bCanAffordNegativeRoute incorrectly calculated

---

## Test Case 4: Wealth-Based Bonus Scaling (Expected Route: Rail)

### Scenario Setup
- Test three wealth levels with same military/economic conditions
- Economic base value: +15 GPT (marginally profitable)
- Route maintenance: -12 GPT
- Military pressure: 0 (no war)

### Sub-Test 4a: Wealthy Empire (60+ GPT)

```
Wealth Check: iGoldPerTurnTimes100 = 60 × 100 = 6000 > 5000
Bonus Scaling: iMovementSpeedBonus = 500 × 40% = 200

Railroad Value = 15 - 12 + 200 = +203
Road Value     = 15 - 4 = +11

DECISION: Railroad selected (203 > 11)
```

### Sub-Test 4b: Reasonably Wealthy (35 GPT)

```
Wealth Check: iGoldPerTurnTimes100 = 35 × 100 = 3500 (2500 < 3500 < 5000)
Bonus Scaling: iMovementSpeedBonus = 500 × 70% = 350

Railroad Value = 15 - 12 + 350 = +353
Road Value     = 15 - 4 = +11

DECISION: Railroad selected (353 > 11)
```

### Sub-Test 4c: Poor Empire (10 GPT)

```
Wealth Check: iGoldPerTurnTimes100 = 10 × 100 = 1000 < 2500
Bonus Scaling: iMovementSpeedBonus = 500 × 100% = 500

Railroad Value = 15 - 12 + 500 = +503
Road Value     = 15 - 4 = +11

DECISION: Railroad selected (503 > 11)
```

### Pass Criteria
- ✅ Rich empires get reduced movement bonus (200 vs 500)
- ✅ Moderate empires get moderate bonus (350)
- ✅ Poor empires get full bonus (500)
- ✅ All cases choose railroad (but with different confidence levels)

### Failure Indicators
- ❌ Wealth thresholds not applied correctly
- ❌ Movement bonus always 500 (scaling not working)
- ❌ Bonus values incorrect (40%, 70%, 100% not applied)

---

## Verification Tools & Debug Output

### Check 1: Builder Log Review
```
Location: My Games/Sid Meier's Civilization V/Logs/BuilderLog.log

Look for entries like:
  BUILDER: Railroad Route Value Calculation: +520
  BUILDER: iMilitaryPressure = 65 (applying 150% war multiplier)
  BUILDER: Treasury constraint triggered: -40 value penalized to -20
```

### Check 2: Route Directive Generation
```
Location: Game debug console / log

Verify route directives are generated:
  BuilderDirective(BUILD_ROUTE, ROUTE_RAILROAD, pPlot)
  
Not generated if value ≤ 0:
  BuilderDirective(BUILD_ROUTE, ROUTE_ROAD, pPlot)
```

### Check 3: Code Breakpoints (Advanced)
In Visual Studio debugger, set breakpoint at line 1730:
```
BREAKPOINT: iMovementSpeedBonus = 500;

Watch Variables:
  - iGoldPerTurnTimes100 (should be GPT × 100)
  - iMovementSpeedBonus (should be 200, 350, or 500 after scaling)
  - iMilitaryPressure (should be 0-100 scale)
  - iRailroadValue (final value used for route selection)
```

---

## Known Issues & Edge Cases

### Edge Case 1: Completion Bonus
Railroad value gets bonus if ≤3 tiles missing (line 1715-1717). Movement bonus is added AFTER this bonus, so:
```
iRailroadValue += (100 - 25×iMissingTiles) + iMovementSpeedBonus
```

### Edge Case 2: Multiple Routes to Same Cities
If multiple railroad routes connect same pair of cities, movement bonus is applied to ALL of them (may cause overbuilding).

### Edge Case 3: Zero Maintenance Routes
Some modded routes might have 0 maintenance. Code handles this correctly (no deduction, full movement bonus).

### Edge Case 4: Very High Military Pressure
If iMilitaryPressure > 100 (shouldn't happen with current API), code still uses 150% multiplier. Consider clamping if API changes.

---

## Rollback Procedure (If Issues Found)

If the implementation causes unexpected behavior:

1. **Revert File:**
   ```
   git checkout HEAD -- CvGameCoreDLL_Expansion2/CvBuilderTaskingAI.cpp
   ```

2. **Rebuild:**
   ```
   .\build_vp_clang.ps1 -Config debug
   ```

3. **Test:** Verify railroads revert to economic-only scoring

---

## Success Criteria Summary

| Test Case | Metric | Pass Threshold |
|-----------|--------|-----------------|
| **Test 1** | Railroad selected over road | ✅ Always |
| **Test 2** | Railroad built despite -20 GPT | ✅ Always (with war pressure) |
| **Test 3** | Road selected when bankrupt | ✅ When net value < 0 |
| **Test 4a** | Wealthy: bonus = 200 | ✅ 60+ GPT |
| **Test 4b** | Moderate: bonus = 350 | ✅ 25-50 GPT |
| **Test 4c** | Poor: bonus = 500 | ✅ <25 GPT |

All test cases passing = Issue 3 successfully implemented.

---

## Report Template

After testing, use this template for results:

```
ISSUE 3 TEST REPORT
===================
Date: [Date]
Tester: [Name]
Scenario: [Map/Settings]

TEST RESULTS:
Test 1 (Economic): [PASS/FAIL] - [Notes]
Test 2 (Military): [PASS/FAIL] - [Notes]
Test 3 (Treasury): [PASS/FAIL] - [Notes]
Test 4 (Wealth): [PASS/FAIL] - [Notes]

ISSUES FOUND:
[List any anomalies]

NOTES:
[Any observations about game behavior]

APPROVED: [Y/N]
```
