# City Management Review

## Overview
This note documents how the Community Patch implementation handles the major city-management systems: production and build queues, citizen work/specialist assignment, garrison tracking, and automation flags. The goal is to keep the code flow clear for future reviewers or contributors.

## Production & Build Queue
- `CvCity::setProduction`/`changeProduction` map the current production item to either `CvCityUnits`, `CvCityBuildings`, or the project runner so that the stored production always tracks the active build.
- `pushOrder` validates queue entries, updates the owner/team `*Making` counters, bubbles non-process orders ahead of any process entries, and notifies the UI (including rerunning `startHeadOrder` when the head changes).
- `popOrder` handles finishing or removing builds, including re-adding saved orders, producing the completed item, decrementing the `*Making` counters, and cleaning any stale entries before restarting the queue head.

## Specialist Assignment
- `CvCityCitizens::SetNoAutoAssignSpecialists` resets forced specialists and triggers `DoReallocateCitizens` to ensure manual toggles apply immediately.
- When adding specialists (`DoAddSpecialistToBuilding`), the code first ensures an unassigned citizen exists, demoting citizens or specialists as needed, increments building/specialist counters, and applies the yield effects so city stats stay in sync.
- `DoSpecialists` runs each turn, advancing GP points per specialist and spawning a Great Person when the threshold is reached (with progress tracked in `ChangeSpecialistGreatPersonProgressTimes100`).

## City Guard / Garrison
- `CvCity::SetGarrison` replaces the stored unit safely, updates the garrison ID/last-turn timestamp, and toggles culture, happiness, maintenance, and religious pressure modifiers whenever a garrison is added or removed.
- `NeedsGarrison` is true when the city is under siege or exposed (unless the player is still in early expansion), so AI city-defense decisions hook into `GetMilitaryAI()->IsExposedToEnemy`.
- `HasGarrison`/`GetGarrisonedUnit` guard against stale IDs, log inconsistencies, and support an override ID for UI purposes while still returning the owning player’s land unit.

## Automation
- Production automation (`setProductionAutomated`) clears the queue when toggled on/off and calls `AI_chooseProduction` if nothing is currently set, so the AI refills the queue quickly.
- Citizen automation is tracked in `CvCityCitizens` flags: automated cities let the AI reallocates citizens, while manual mode (no-auto-specialists) clears forced specialists and forces a reallocation so player overrides take effect immediately.

## References
- Production routing and queue handling: [CvGameCoreDLL_Expansion2/CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L12351-L12440), [CvGameCoreDLL_Expansion2/CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L28981-L29239)
- Specialist automation and GP spawning: [CvGameCoreDLL_Expansion2/CvCityCitizens.cpp](CvGameCoreDLL_Expansion2/CvCityCitizens.cpp#L520-L604), [CvGameCoreDLL_Expansion2/CvCityCitizens.cpp](CvGameCoreDLL_Expansion2/CvCityCitizens.cpp#L2880-L3035)
- Garrison management: [CvGameCoreDLL_Expansion2/CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L16330-L16670)
- Automation toggles: [CvGameCoreDLL_Expansion2/CvCity.cpp](CvGameCoreDLL_Expansion2/CvCity.cpp#L22174-L22198), [CvGameCoreDLL_Expansion2/CvCityCitizens.cpp](CvGameCoreDLL_Expansion2/CvCityCitizens.cpp#L520-L604)
