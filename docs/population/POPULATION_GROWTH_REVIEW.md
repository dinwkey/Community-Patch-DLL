# Population & Growth Review

**Date:** January 11, 2026  
**Scope:** Food, housing, growth rate, citizen assignment, yields per citizen

---

## Executive Summary
Population management in Vox Populi balances three components: food accumulation/drop (which drives the growth clock), growth rate modifiers (both empire‑wide and local), and the assignment of citizens to plots/specialists, which determines the yields that feed/maintain/capitalize the city. The code for these systems lives in `CvCity`/`CvPlayer` (food/growth maths) and `CvCityCitizens` (assignment heuristics), while per‑pop yield trackers enable buildings/policies to scale rewards with population. This review inspects those paths, flags one blocking bug, and surfaces a couple of areas worth tightening.

## Food Flow
`CvCity::doGrowth()` runs every turn ([CvCity.cpp](CvCity.cpp#L31091-L31126)); it adds `getYieldRateTimes100(YIELD_FOOD)` to the food stock, checks the growth threshold from `CvPlayer::getGrowthThreshold()`, and either grows, caps food when growth is blocked, or kills a citizen when starving. The net food per turn comes from `getFoodPerTurnBeforeConsumptionTimes100()` minus `getFoodConsumptionTimes100()`, both of which compile yields from tiles/specialists/processes and adjust for era‑scaled specialist costs or buildings that cap non‑specialist starvation ([CvCity.cpp](CvCity.cpp#L15984-L16095), [CvCity.cpp](CvCity.cpp#L23223-L23240)). Because all food math happens in x100 precision, the UI can safely expose `getYieldRateTimes100(YIELD_FOOD)` and `getFoodTurnsLeft()` without losing precision.

## Growth Rate Stack
Every city gets a percentage modifier on the food surplus via `CvCity::getGrowthMods()` ([CvCity.cpp](CvCity.cpp#L16095-L16270)). This method aggregates capital/player growth bonuses, WLTKD, unit supply penalties, religion, local happiness, and the empire happiness penalty. The threshold itself comes from `CvPlayer::getGrowthThreshold()`, which combines base growth, exponential pop scaling, game speed, start era, and handicap factors ([CvPlayer.cpp](CvPlayer.cpp#L44772-L44813)). Growth is therefore capped both by how much food is stored and by the percentage modifiers that apply to the excess.

## Citizen Assignment
`CvCityCitizens::DoTurn()` recalculates the city focus every turn, toggling between food, production, gold, and GP modes based on economic state and happiness ([CvCityCitizens.cpp](CvCityCitizens.cpp#L214-L366)). It then routes into `DoReallocateCitizens()`, which runs a base assignment, re‑runs with higher food weighting when needed, and calls `OptimizeWorkedPlots()` via `GetBestCityPlotWithValue()` before refreshing per‑plot yields ([CvCityCitizens.cpp](CvCityCitizens.cpp#L2195-L2246), [CvCityCitizens.cpp](CvCityCitizens.cpp#L1838-L1885)). The optimizer knows about forced work overrides and specialist slots, but it doesn’t yet surface per‑pop bonuses stored in `CvCity::GetYieldPerPopTimes100()` ([CvCity.cpp](CvCity.cpp#L26093-L26118)).

## Yields Per Citizen
`CvCity::GetYieldPerPopTimes100()` and `GetYieldPerPopInEmpireTimes100()` track flat bonuses that scale with city or empire population, letting beliefs/buildings reward every citizen or the entire empire simultaneously ([CvCity.cpp](CvCity.cpp#L26093-L26188)). These totals feed into `getBaseYieldRateTimes100()` and therefore affect the yield-per-turn numbers used in `doGrowth()` and the citizen optimizer, but the values are currently siloed inside `CvCity` and never surfaced for tooltip or AI tuning.

## Housing Capacity (Missing)
Growth currently depends solely on food and the forced‑growth flag. `CvCity::doGrowth()` will happily bump population as long as food and happiness allow it, with no built‑in cap for housing or other infrastructure ([CvCity.cpp](CvCity.cpp#L31091-L31126)). Introducing a housing field (e.g., `m_iHousingCapacity`) and refusing to call `changePopulation(1)` when population equals that capacity would cleanly extend the growth stack and allow future buildings/policies to grant extra housing without touching the food math.

---

## Findings

### Issue — forced growth never unlocks by design pointer
The "unlock only one city per turn" branch in `CvCityCitizens::DoTurn()` checks `IsForcedAvoidGrowth()` **and** `thisPlayer.unlockedGrowthAnywhereThisTurn()` before flipping the flag back on ([CvCityCitizens.cpp](CvCityCitizens.cpp#L368-L377)). Because `unlockedGrowthAnywhereThisTurn()` only becomes true inside that same branch, the condition can never be satisfied and the branch never executes. As a result, once a city is forced to avoid growth it only unlocks again when the entire `if` falls through (i.e., when the potential unhappiness becomes zero), so the intended per‑turn cap is dead code. In practice the city either stays locked longer than necessary or unlocks immediately once the empire is happy, bypassing the “one city per turn” goal.

### Potential Improvements
1. **Lock growth behind housing.** Before calling `changePopulation(1)` in `CvCity::doGrowth()`, compare the current population against a housing cap (a new `CvCity` field, or even `GetHappinessFromHousing()` once added). That would let buildings (e.g., apartments) grant extra population headroom without abusing `getMaxFoodKeptPercent` to stash food ([CvCity.cpp](CvCity.cpp#L31091-L31126)).
2. **Surface per‑pop yields for assignment/UI.** `CvCity::GetYieldPerPopTimes100()` and `GetYieldPerPopInEmpireTimes100()` already accumulate bonuses that scale with population ([CvCity.cpp](CvCity.cpp#L26093-L26188)). Feeding those values into the `GetBestCityPlotWithValue()` scoring function or presenting them in the city panel (maybe alongside `GetPlotValue()`) would make it easier to judge whether a tile/specialist swap pays off before the next `DoReallocateCitizens()` run ([CvCityCitizens.cpp](CvCityCitizens.cpp#L1838-L1885)).

---

### Next Steps
1. Fix the `unlockedGrowthAnywhereThisTurn()` condition in `CvCityCitizens.cpp#L368-L377` so that the city can exit the forced avoid state once per turn as intended.  
2. Consider adding a housing cap and per‑pop yield visibility, then re‑run the population review after those hooks land.
