# Diplomacy System Review

## Trade deals & strategic partners
- `SetStrategicTradePartner` explicitly filters out warmongers, competitors, plotters and sanction targets before flagging liberators, masters, policy-boosted civs, or vassals as preferred trade partners for ongoing deals.
  See [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L22120-L23090) for the candidate filtering and trade/land-dispute checks.
- The same approach builder adds friendly weight when ongoing trade yields (gold/culture/science) flow in our direction and when `GetGameDeals().GetDealValueWithPlayer` is positive, scaled by trade-favoring traits and era.
  Those contributions also appear in the demand logic that drives guarded/hostile weighting ahead of opportunity attacks.
  Refer to [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L18000-L19100) and [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L24800-L24920) for the full approach/demand math.
- Recent trade value is tracked per partner via `GetRecentTradeValue`/`SetRecentTradeValue`, capped by game-speed-adjusted opinion limits, and decays every turn with `ChangeRecentTradeValue`, so lingering deals keep influence as long as they remain active.
  Background reference: [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L4484-L45340).

## Declarations of Friendship, Defensive Pacts & Research
- `DoUpdatePlanningExchanges` loops over valid majors, cancels agreements when we are vassals or out of limits, and prefers befriending civs whose approach/opinion/opponent networks stay clear of sanctions, allied enemies, broken promises, or plotting behavior.
  It then selects DoFs, diplomacy friends, and defensive pacts through `GetPlayerWithHighestStrategicApproachValue`, relying on the same approach scores that already include trade, threat, and denouncement cues.
  Details live in [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L22120-L23740).
- `AvoidExchangesWithPlayer` stops agreements whenever teammates view a candidate as hostile, denounced, plotting, or preparing coop wars, which keeps our league/self team’s approach consistent with the alliance decisions above.
  (See [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L23740-L23790) for the guardrails.)

## War, peace & treaties
- `DoUpdatePeaceTreatyWillingness` determines whether we should offer or accept peace by checking critical state, peace blocks (vassals, persistent grudges), war progress, danger to cities, vassalization thresholds, and a peace score that grows when we've been losing or weary.
  It keeps logs, rejects peace when threatened by endangered enemies, and auto-makes peace with minors when in danger before handing winners off to treaty negotiation.
  The full scoring path is recorded in [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L25374-L26658).
- Once teams are ready to talk, `DoUpdatePeaceTreatyOffers` translates aggregated warscore adjustments into the least/more generous treaty (white peace → unconditional surrender) and stores both offer/accept willingness so that other systems know whether negotiations are on the table or blocked.
  See [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L26566-L26658).
- Peace stays blocked while recent treaties exist (handicap dampener), while we are in a DoF with a civ that recently denounced us, or while one side is always-war; `IsPeaceBlocked` and `GetPeaceBlockReason` provide the canonical enum for those states, and `IsWantsPeaceWithPlayer` checks willingness edges.
  (Capture the helpers around [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L4200-L4327) and the peace-block logic later in the file.)

## Denouncements & AI attitudes
- `IsDenouncedPlayer`/`SetDenouncedPlayer` plus `GetTurnsSinceDenouncedPlayer` track every active denouncement. They also recompute great people modifiers and drive the `IsDenouncingPlayer`/`GetNumDenouncements` helpers that the diplomacy engine consults when adjusting opinion weights.
  Friend-denouncement bookkeeping (who denounced us while we had a DoF) lives in `IsFriendDenouncedUs`, `SetFriendDenouncedUs`, and their turn counters, giving the AI a persistent memory of treaty betrayals.
  Refer to [CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L4200-L5335) for the full set of counters, tolerances, and helper queries.
- The approach builder penalizes a candidate whenever our friends/DPs denounce them (war/hostile/deceptive shifts) or when they denounce one of our own allies, potentially reducing friendly/neural weight hard enough to prevent new DoFs. It also stores religious/ideology boosts for shared enemies and handles dogpiling/military threats plus trade/demand cues described above.
  `GetDenounceWillingness` and personality flavors (boldness/meanness) feed into those shifts, which all take place inside the `[CvDiplomacyAI.cpp](CvGameCoreDLL_Expansion2/CvDiplomacyAI.cpp#L18000-L19080)` block.
- Because denouncements influence peace, alliances, and strategic trade partners, the AI revisits its approach scores every update to align future declarations and refusals with the most recent state, completing the attitude-feedback loop that ultimately governs war/peace/denouncement behavior.

*Saved for posterity in [docs/diplomacy-review.md](docs/diplomacy-review.md).*