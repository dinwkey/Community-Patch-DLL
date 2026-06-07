# Trade & Economy Review

**Date:** January 10, 2026
**Scope:** `CvGameTrade`, `CvPlayerTrade`, `CvCityConnections` in `CvGameCoreDLL_Expansion2`.

## Findings

1. **Route creation skips war/open-border checks.** `CvGameTrade::CreateTradeRoute` calls `HavePotentialTradePath` and builds traders without re-running `IsValidTradeRoutePath` (which vets war status and valid connections). As soon as the cached path survives a declaration of war or an open-border drop, a new trade route can move through enemy terrain before either player has a chance to cancel it. Guard `CreateTradeRoute` with the same war/path verification logic that `CanCreateTradeRoute` uses to ensure routes are never created mid-war or while borders are closed.

2. **Negative trade-route slot counts possible.** `CvPlayerTrade::GetNumTradeRoutesPossible` multiplies the base slot count by `(100 + NumTradeRoutesModifier)/100` but never clamps the result. Policies or traits that assign a modifier ≤ −100 can therefore return a negative number of available routes, confusing `GetNumTradeUnitsRemaining`, UI widgets, and other consumers. Apply `iNumRoutes = max(0, iNumRoutes)` after modifiers to keep the count non-negative and avoid nonsensical displays.

## Next Steps

1. Confirm whether trade-route creation is already guarded elsewhere (e.g., caller contracts) and add the missing war/path validation inside `CreateTradeRoute` if necessary.
2. Clamp `CvPlayerTrade::GetNumTradeRoutesPossible` after applying the modifier so the result is never below zero, then observe whether any civs/policies rely on negative values (unlikely).
