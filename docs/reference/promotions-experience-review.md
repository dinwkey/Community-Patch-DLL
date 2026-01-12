# Promotions & Experience Review

## Overview
- Summarizes the unit experience and promotion system, including XP accumulation, promotion trees/choices, veteran bonuses, and upgrade mechanics.
- Hooks are documented; this file records issues/improvements encountered while reviewing the system architecture.

## Deep Dive

### Experience System

- **XP Accumulation & Level Calculation:** Units gain experience (stored as `m_iExperienceTimes100` to avoid floating-point precision loss) after combat, and level up when `getExperienceTimes100() / 100 >= experienceNeeded()`. The experience requirement grows quadratically: each level *L* requires `(1+2+...+L) * EXPERIENCE_PER_LEVEL * GameSpeedFactor` XP. Modifiers from traits (e.g., `getLevelExperienceModifier()`) scale the base cost. The system uses an integer accumulator (`changeExperienceTimes100()`) to preserve fractional XP without floating-point arithmetic ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L17717-L17750]).
  
- **Promotion Ready & Level Up Flow:** After any XP gain or combat outcome, `testPromotionReady()` checks if a unit has enough XP and is not out of attacks; if so, it sets `m_bPromotionReady = true`, which triggers a UI notification and halts automation. Human players then choose a promotion via UI; AI is routed through lua callbacks. Level-up grants an instant yield via `doInstantYield(INSTANT_YIELD_TYPE_LEVEL_UP)` to the origin city or capital ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L25171-L25186]).

- **Promotion Eligibility:** A promotion is available if:
  1. The unit is promotion-ready (`isPromotionReady()`),
  2. The unit has all prerequisite promotions,
  3. The promotion is valid for the unit's combat type,
  4. If a leader unit is present and the promotion is tied to that leader, the leader's promotion is used instead.
  - The prerequisite chain allows OR-gates (up to 9 alternatives) so a promotion can require *any one* of several parent promotions ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13470-L13480]).

- **Warlord / Leader Experience Sharing:** Great Generals and other leader units (marked with `GetLeaderPromotion()` and `GetLeaderExperience()` > 0) can grant experience to adjacent units on the same plot. When `giveExperience()` is called, it distributes `getStackExperienceToGive()` XP evenly among eligible units, with remainders given to the first units in the stack. The experience bonus scales with stack size via `WARLORD_EXTRA_EXPERIENCE_PER_UNIT_PERCENT` ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13689-L13725]).

- **Free Promotions & Starting Veterans:** When a unit is created (during production or capture), it inherits free promotions from its unit entry via `GetFreePromotions(i)`. These are applied automatically and do not consume promotion slots. Era-based promotions (e.g., "upgraded to Musketeer") are also applied on era transition if the unit's entry has them configured ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1933-L1940]).

### Promotion Trees & Choice

- **Promotion Data Structure:** Each promotion is a `CvPromotionEntry` with:
  - A single prerequisite (`GetPrereqPromotion()`) and up to 9 OR-options (`GetPrereqOrPromotion1()` through `GetPrereqOrPromotion9()`).
  - Boolean flags: `IsLeader()`, `IsCannotBeChosen()`, `IsLostWithUpgrade()`, `IsNotWithUpgrade()`, `IsInstaHeal()`, etc.
  - Modifiers: attack/defense bonuses, terrain effects, yield changes, XP bonuses, capture chances, and many contextual bonuses (terrain modifiers, unit-type modifiers, etc.).
  - Serialized state: the unit's `CvUnitPromotions` bitfield tracks which promotions are active; the engine only caches the enabled promotions for quick lookup via `IsHasPromotion(ePromotion)` ([CvGameCoreDLL_Expansion2/CvPromotionClasses.h#L135-L330], [CvGameCoreDLL_Expansion2/CvPromotionClasses.cpp#L3635-L3700]).

- **Promotion Acquisition & Constraints:**
  - Non-leader promotions are earned by spending one promotion slot per level-up; the player/AI selects from available options.
  - A promotion is forbidden (`IsCannotBeChosen()`) if it is auto-granted only (e.g., plague effects, religious unit passives).
  - An earned promotion may be lost on upgrade if the new unit type does not support it; however, `IsLostWithUpgrade()` = true forces loss, while `IsNotWithUpgrade()` = true prevents the new unit from gaining it (even as a free promotion) ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1933-L1940]).

- **Promotion Modifiers & Bonuses:**
  - **Combat modifiers:** `GetCombatPercent()`, `GetCityAttackPercent()`, `GetCityDefensePercent()`, `GetRangedDefenseMod()`, terrain-specific bonuses (hills, rivers, coast), and per-level combat scaling via `GetCombatModPerLevel()`.
  - **Healing:** `GetEnemyHealChange()`, `GetNeutralHealChange()`, `GetFriendlyHealChange()`, `GetSameTileHealChange()`, `GetAdjacentTileHealChange()` control out-of-combat HP recovery rates.
  - **Experience & Yields:** `GetExperiencePercent()` amplifies XP gain; `GetYieldFromKills()`, `GetYieldFromBarbarianKills()`, `GetYieldFromCombatExperienceTimes100()` grant gold/science on kill outcomes; `GetYieldFromAncientRuins()`, `GetYieldFromTRPlunder()` modify goody-hut and ruin yields.
  - **Movement & Range:** `GetMovesChange()`, `GetRangeChange()`, `GetExtraNavalMoves()`, `GetMoveDiscountChange()` adjust unit mobility; `GetIgnoreTerrainCost()`, `GetIgnoreTerrainDamage()` toggle movement effects.
  - **Air & Naval:** `GetNumInterceptionChange()`, `GetAirInterceptRangeChange()`, `GetCargoChange()` modify air/naval unit capabilities.
  - **Unit Combat Modifiers (per-type):** Arrays `GetUnitCombatModifierPercent(i)`, `GetUnitCombatModifierPercentAttack(i)`, `GetUnitCombatModifierPercentDefense(i)` and `GetUnitClassModifierPercent(i)`, etc., allow promotions to boost effectiveness against specific unit combats or classes ([CvGameCoreDLL_Expansion2/CvPromotionClasses.h#L181-L330]).

- **Veteran Bonuses & Special Promotions:**
  - Promotions like "Veteran" or "Battle-Hardened" are often implemented as free promotions granted on unit creation (via XP sources or conquered units).
  - Some promotions are tied to specific unit traits (e.g., city-state military units grant bonuses after loyalty missions).
  - The XP-to-yield system (`GetYieldFromCombatExperienceTimes100()`) converts a unit's earned XP into yields at the origin city or capital, incentivizing leveled units.
  - Instant-heal promotions (`IsInstaHeal()`) are applied once at promotion time and reduce damage by `INSTA_HEAL_RATE` (or a fraction in VP mode); other promotions may grant fractional healing per turn via `changeHealth()` mechanics ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13520-L13540]).

### Upgrade Mechanics

- **Upgrade Chain & Type Resolution:** A unit's upgrade target is determined by `GetUpgradeUnitType()`, which iterates through unit classes and selects the first match where the player has the required tech and resources. If a unit has no upgrade, `NO_UNIT` is returned. The new unit type must match the old unit's class in order to be considered a valid upgrade (not a sidegrade) ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13909-L14000]).

- **Upgrade Conditions:** A unit can upgrade if:
  1. `isReadyForUpgrade()` = unit has remaining movement points,
  2. `CanUpgradeTo(eUnitType)` = the target unit is valid, the player has tech/resources, and optionally the unit can upgrade in the current territory (cs vassals have restrictions),
  3. The gold cost is available or the upgrade is free.
  - If a unit is in territory that blocks upgrades (e.g., a city-state vassal in minor territory), `CanUpgradeInTerritory()` will fail unless `CanUpgradeCSVassalTerritory()` is true ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13745-L13890]).

- **Upgrade Promotion Transfer & Loss:** When a unit upgrades via `DoUpgradeTo()`:
  1. The new unit is created via `initUnit(..., bIsUpgrade=true)`, which applies free promotions after filtering:
     - Promotions with `IsLostWithUpgrade()` = true are dropped.
     - Promotions with `IsNotWithUpgrade()` = true are prevented (even if free on the new unit type).
     - Other promotions are carried forward (preserved in `convert()` unless explicitly dropped).
  2. Experience is transferred: `convert()` calls `setExperienceTimes100(pUnit->getExperienceTimes100())` to carry XP to the new unit ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1930-L1950], [CvGameCoreDLL_Expansion2/CvUnit.cpp#L14061-L14120]).
  3. Movement/attacks are optionally preserved if the new unit has `GetCanMoveAfterUpgrade()` = true; otherwise, the unit ends its turn ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14140-L14160]).
  4. Special events: If a unit has `IsCultureFromExperienceDisbandUpgrade()`, the XP is converted to culture instead; militaristic city-states penalize the upgrade by delaying unit spawn ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14117-L14135]).
  5. Lua hooks `GAMEEVENT_UnitUpgraded` or `UnitUpgraded` are fired before the old unit is killed ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14103-L14115]).

- **Cost & Gold:** The upgrade cost is calculated via `upgradePrice(eUnitType)`, which factors in:
  - The difference in base production cost between the old and new units.
  - Any trait or policy modifiers that affect upgrade costs (e.g., reduced cost for certain civilizations).
  - The cost is logged to the treasury and deducted immediately; free upgrades (often for unique units or events) bypass the gold check ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14061-L14080]).

- **Unit Conversion & Lua Hooks:** The `convert(pOldUnit, bIsUpgrade=true)` function is the final step:
  - Copies health, XP, promotions, and other state from the old unit to the new unit.
  - Fires the old unit's death cleanup and the new unit's initialization.
  - Graphics are updated via `setupGraphical()` so the new unit renders correctly.
  - The old unit is marked for deletion; the new unit assumes its position and selectable state ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14140-L14160]).

## Issues

1. **Implicit Promotion Loss on Upgrade:** When a unit is upgraded, promotions with `IsLostWithUpgrade()` = true are silently dropped without notification or compensation. Players may not realize that expensive promotions are being lost. A tooltip or warning would help ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1933-L1940]).

2. **XP Carryover Ambiguity:** When a unit with a large XP bank upgrades, the new unit inherits the full XP amount but may have a *different* level cap or XP scaling if the new unit type has different stats. This can result in unexpected behavior (e.g., the new unit may be over-leveled or unable to use its XP bank effectively). The rules for XP scaling on unit conversion are not explicitly documented ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L2063], [CvGameCoreDLL_Expansion2/CvUnit.cpp#L1889-L1901]).

3. **Warlord XP Distribution Edge Cases:** The warlord experience distribution in `giveExperience()` iterates only units on the same plot. If a multi-hex stack spans multiple plots, only the stack on the warlord's plot benefits. This is likely intentional but could lead to unintuitive gameplay if a player expects experience to propagate across a split stack ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13689-L13710]).

4. **Free Promotion Application Order:** When a unit is created or upgraded, free promotions are applied in a specific order (unit entry defaults, era promotions, etc.). If two free promotions are mutually exclusive (or conflict at the data level), the engine does not prevent conflicts—it relies on mod data integrity. This could cause silent failures if a unit gains a promotion it shouldn't ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1933-L1950]).

5. **Promotion Readiness After Experience Gift:** When a non-leader unit gains experience from a leader (e.g., in `giveExperience()`), each unit calls `testPromotionReady()` independently. However, if multiple units gain XP simultaneously, the UI may spam multiple "promotion ready" notifications or the player may be forced to promote several units in rapid succession, which could be confusing ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13714-L13720]).

6. **No Prevention of Over-Leveling:** There is no hard cap on unit level in the engine. A unit with a very large XP bank can theoretically level indefinitely if the player continues to add XP sources. This is usually managed via mod design (setting promotion limits in XML), but the code does not enforce a level cap ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L25171-L25186]).

7. **Promotion Slot Exhaustion:** If a unit reaches max level but has no more available promotions (all prerequisites are locked or unavailable), the unit becomes "stuck" in promotion-ready state and may block automation. There is no mechanism to auto-release the unit from promotion-ready if no valid promotions exist ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13477-L13480]).

## Improvements

1. **Clarify XP Scaling & Unit Conversion Rules:** Add an in-code comment in `convert()` documenting how XP is carried over and whether any scaling is applied based on unit type differences. Include a note on whether XP banks can cause units to spawn over-leveled ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1930-L1950], [CvGameCoreDLL_Expansion2/CvUnit.cpp#L2063]).

2. **Document Promotion Loss Visibly:** When a unit upgrades and loses promotions due to `IsLostWithUpgrade()`, add a debug log or Lua event that identifies which promotions were dropped and why. This helps modders and players understand the upgrade impact ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L14103-L14115]).

3. **Validate Promotion Exclusivity:** Add a startup check (in XML loading or init) to detect if any unit can gain two mutually exclusive free promotions (e.g., two promotions with conflicting flags like `IsLeader()` and a melee combat bonus). Warn modders if such conflicts are detected ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L1930-L1950]).

4. **Batch Promotion Notifications:** When a warlord grants experience to multiple units, collect all units that become promotion-ready and fire a single batched notification (e.g., "3 units ready to promote") instead of separate alerts. This reduces UI spam ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13714-L13720]).

5. **Implement Promotion Slot Overflow Safeguard:** In `testPromotionReady()`, check if the unit has at least one valid promotion available before setting `m_bPromotionReady = true`. If no valid promotions exist, log a warning and reset the flag, allowing the unit to resume automation ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L25171-L25186]).

6. **Add Unit-Level Cap as Config Parameter:** Introduce a configurable constant (similar to `EXPERIENCE_PER_LEVEL`) that defines a maximum unit level cap. Apply the cap in `testPromotionReady()` so that units cannot exceed the configured level, preventing edge-case over-leveling scenarios ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L17717-L17750]).

7. **Explicit Warlord Coverage Documentation:** Add a comment in `giveExperience()` noting that the function only grants XP to units on the same plot as the warlord. If multi-plot stacks are intended to benefit, consider a revised implementation that iterates adjacent plots or uses a radius check ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13689-L13710]).

8. **Simplify Prerequisite Resolution:** The current prerequisite system (1 required + up to 9 OR-options) is powerful but complex. Consider documenting or refactoring the promotion eligibility check in `canPromote()` to make the logic more transparent, perhaps with a dedicated function `GetAvailablePromotions()` that returns a list of eligible promotions for UI or AI purposes ([CvGameCoreDLL_Expansion2/CvUnit.cpp#L13470-L13480]).

