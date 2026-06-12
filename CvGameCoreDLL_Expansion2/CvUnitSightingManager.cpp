/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.
	All other marks and trademarks are the property of their respective owners.
	All rights reserved.
	------------------------------------------------------------------------------------------------------- */


#include "CvGameCoreDLLPCH.h"
#include "CvUnitSightingManager.h"

#include "CvCity.h"
#include "CvDiplomacyAI.h"
#include "CvMap.h"
#include "CvPlayer.h"
#include "CvPlot.h"
#include "CvTeam.h"
#include "CvUnit.h"

CvUnitSightingManager::CvUnitSightingManager()
	: m_pPlayer(NULL)
{
}

void CvUnitSightingManager::Init(CvPlayer* pPlayer)
{
	m_pPlayer = pPlayer;
	m_Sightings.clear();
	m_SightingIndex.clear();
}

/// Compute classification flags for a unit.
unsigned char CvUnitSightingManager::ComputeFlags(CvUnit* pUnit) const
{
	unsigned char uFlags = 0;

	if (pUnit->isEmbarked())
		uFlags |= SIGHTING_FLAG_EMBARKED;
	if (pUnit->IsFortified() || pUnit->IsGarrisoned())
		uFlags |= SIGHTING_FLAG_FORTIFIED;
	if (pUnit->GetCurrHitPoints() < (pUnit->GetMaxHitPoints() / 2))
		uFlags |= SIGHTING_FLAG_DAMAGED;
	if (pUnit->IsCanAttackRanged())
		uFlags |= SIGHTING_FLAG_RANGED;

	// Siege classification: city bombard AI or city special
	UnitAITypes eAIType = pUnit->AI_getUnitAIType();
	if (eAIType == UNITAI_CITY_BOMBARD || eAIType == UNITAI_CITY_SPECIAL)
		uFlags |= SIGHTING_FLAG_SIEGE;

	if (pUnit->getDomainType() == DOMAIN_SEA)
		uFlags |= SIGHTING_FLAG_NAVAL;
	if (pUnit->getDomainType() == DOMAIN_AIR)
		uFlags |= SIGHTING_FLAG_AIR;

	return uFlags;
}

/// Get or create a sighting record for the given unit.
UnitSighting* CvUnitSightingManager::GetOrCreateSighting(CvUnit* pUnit)
{
	std::pair<PlayerTypes, int> unitKey = MakeKey(pUnit->getOwner(), pUnit->GetID());
	std::map<std::pair<PlayerTypes, int>, int>::iterator it = m_SightingIndex.find(unitKey);

	if (it != m_SightingIndex.end())
	{
		return &m_Sightings[it->second];
	}

	// Need to create a new sighting — check capacity
	if ((int)m_Sightings.size() >= AI_MAX_UNIT_SIGHTINGS)
	{
		// Evict the oldest (most expired) sighting
		int iOldestIdx = 0;
		short iOldestTurn = 32767;
		for (int i = 0; i < (int)m_Sightings.size(); i++)
		{
			if (m_Sightings[i].lastSeenTurn < iOldestTurn)
			{
				iOldestTurn = m_Sightings[i].lastSeenTurn;
				iOldestIdx = i;
			}
		}

		// Remove the evicted entry from the index
		std::pair<PlayerTypes, int> oldKey = MakeKey(m_Sightings[iOldestIdx].owner, m_Sightings[iOldestIdx].unitId);
		m_SightingIndex.erase(oldKey);

		// Replace in-place
		UnitSighting& sighting = m_Sightings[iOldestIdx];
		memset(&sighting, 0, sizeof(UnitSighting));
		sighting.unitId = pUnit->GetID();
		sighting.unitType = pUnit->getUnitType();
		sighting.owner = pUnit->getOwner();
		sighting.turnStartX = -1;  // Sentinel: no movement tracked yet
		sighting.turnStartY = -1;
		m_SightingIndex[unitKey] = iOldestIdx;
		return &sighting;
	}

	// Append new sighting
	UnitSighting sighting;
	memset(&sighting, 0, sizeof(UnitSighting));
	sighting.unitId = pUnit->GetID();
	sighting.unitType = pUnit->getUnitType();
	sighting.owner = pUnit->getOwner();
	sighting.turnStartX = -1;  // Sentinel: no movement tracked yet
	sighting.turnStartY = -1;

	int iNewIdx = (int)m_Sightings.size();
	m_Sightings.push_back(sighting);
	m_SightingIndex[unitKey] = iNewIdx;
	return &m_Sightings[iNewIdx];
}

/// Called when an enemy unit is observed at a plot (no movement context).
void CvUnitSightingManager::OnUnitSeen(CvUnit* pUnit)
{
	if (!m_pPlayer || !pUnit || !pUnit->IsCombatUnit())
		return;

	// Don't track our own units or allies
	if (pUnit->getOwner() == m_pPlayer->GetID())
		return;
	if (GET_PLAYER(pUnit->getOwner()).getTeam() == m_pPlayer->getTeam())
		return;

	UnitSighting* pSighting = GetOrCreateSighting(pUnit);
	if (!pSighting)
		return;

	pSighting->x = (short)pUnit->getX();
	pSighting->y = (short)pUnit->getY();
	pSighting->lastSeenTurn = (short)GC.getGame().getGameTurn();
	pSighting->health = (unsigned char)((pUnit->GetCurrHitPoints() * 100) / max(1, pUnit->GetMaxHitPoints()));
	pSighting->flags = ComputeFlags(pUnit);
	pSighting->movementPoints = (unsigned char)min(255, pUnit->maxMoves() / max(1, GD_INT_GET(MOVE_DENOMINATOR)));
	// No movement context when revealed by fog lift, so clear direction data.
	pSighting->lastDeltaX = 0;
	pSighting->lastDeltaY = 0;
	pSighting->avgDX = 0;
	pSighting->avgDY = 0;
}

/// Called when an enemy unit moves. Captures direction and updates position
/// depending on visibility from the observer's perspective.
void CvUnitSightingManager::OnUnitMoved(CvUnit* pUnit, CvPlot* pFrom, CvPlot* pTo)
{
	if (!m_pPlayer || !pUnit || !pUnit->IsCombatUnit())
		return;

	// Don't track our own units or allies
	if (pUnit->getOwner() == m_pPlayer->GetID())
		return;
	if (GET_PLAYER(pUnit->getOwner()).getTeam() == m_pPlayer->getTeam())
		return;

	TeamTypes eObserverTeam = m_pPlayer->getTeam();

	bool bCanSeeFrom = (pFrom != NULL) && pFrom->isVisible(eObserverTeam);
	bool bCanSeeTo   = (pTo != NULL) && pTo->isVisible(eObserverTeam);

	// Must see at least one endpoint
	if (!bCanSeeFrom && !bCanSeeTo)
		return;

	UnitSighting* pSighting = GetOrCreateSighting(pUnit);
	if (!pSighting)
		return;

	// === Turn-start tracking (fix for multi-step moves) ===
	// On first step of a turn, record the origin position.
	// Subsequent steps compute heading from turnStart → current destination,
	// giving the true overall heading regardless of intermediate waypoints.
	if (pSighting->turnStartX < 0)
	{
		// First movement this turn — record starting position
		if (bCanSeeFrom && pFrom)
		{
			pSighting->turnStartX = (short)pFrom->getX();
			pSighting->turnStartY = (short)pFrom->getY();
		}
		else if (bCanSeeTo && pTo)
		{
			// Can't see origin — use destination as fallback start
			pSighting->turnStartX = (short)pTo->getX();
			pSighting->turnStartY = (short)pTo->getY();
		}
	}

	// Capture movement direction: overall heading from turn-start if available
	if (bCanSeeFrom && pFrom && pTo)
	{
		if (pSighting->turnStartX >= 0)
		{
			// Overall heading from turn-start to current step destination
			int destX = bCanSeeTo ? pTo->getX() : pFrom->getX();
			int destY = bCanSeeTo ? pTo->getY() : pFrom->getY();
			int dx = destX - (int)pSighting->turnStartX;
			int dy = destY - (int)pSighting->turnStartY;
			if (dx != 0 || dy != 0)
			{
				pSighting->lastDeltaX = (char)max(-127, min(127, dx));
				pSighting->lastDeltaY = (char)max(-127, min(127, dy));
			}
		}
		else
		{
			// No turn-start recorded — fall back to single-step delta
			pSighting->lastDeltaX = (char)(pTo->getX() - pFrom->getX());
			pSighting->lastDeltaY = (char)(pTo->getY() - pFrom->getY());
		}
	}
	else
	{
		// Direction is unknown if we didn't see the origin; clear stale direction.
		pSighting->lastDeltaX = 0;
		pSighting->lastDeltaY = 0;
		pSighting->avgDX = 0;
		pSighting->avgDY = 0;
	}

	// Update position and state if we can see the destination
	if (bCanSeeTo)
	{
		pSighting->x = (short)pTo->getX();
		pSighting->y = (short)pTo->getY();
		pSighting->lastSeenTurn = (short)GC.getGame().getGameTurn();
		pSighting->health = (unsigned char)((pUnit->GetCurrHitPoints() * 100) / max(1, pUnit->GetMaxHitPoints()));
		pSighting->flags = ComputeFlags(pUnit);
		pSighting->movementPoints = (unsigned char)min(255, pUnit->maxMoves() / max(1, GD_INT_GET(MOVE_DENOMINATOR)));
	}
	else if (bCanSeeFrom && pFrom)
	{
		// We saw the origin but not the destination; treat origin as last confirmed.
		pSighting->x = (short)pFrom->getX();
		pSighting->y = (short)pFrom->getY();
		pSighting->lastSeenTurn = (short)GC.getGame().getGameTurn();
		pSighting->health = (unsigned char)((pUnit->GetCurrHitPoints() * 100) / max(1, pUnit->GetMaxHitPoints()));
		pSighting->flags = ComputeFlags(pUnit);
		pSighting->movementPoints = (unsigned char)min(255, pUnit->maxMoves() / max(1, GD_INT_GET(MOVE_DENOMINATOR)));
	}
	// else: unit entered fog — keep last confirmed position, but we captured the direction above

	// Infer intent from context (only meaningful when we had a direction)
	if (bCanSeeFrom)
	{
		pSighting->predictedIntent = (unsigned char)InferUnitIntent(pSighting, pUnit);
	}
}

/// Called when a unit is destroyed — remove from tracking.
void CvUnitSightingManager::OnUnitDestroyed(PlayerTypes eOwner, int iUnitId)
{
	std::pair<PlayerTypes, int> unitKey = MakeKey(eOwner, iUnitId);
	std::map<std::pair<PlayerTypes, int>, int>::iterator it = m_SightingIndex.find(unitKey);

	if (it == m_SightingIndex.end())
		return;

	int iIdx = it->second;
	int iLast = (int)m_Sightings.size() - 1;

	if (iIdx != iLast && iLast >= 0)
	{
		// Swap with last element to avoid gaps
		m_Sightings[iIdx] = m_Sightings[iLast];

		// Update the swapped element's index
		std::pair<PlayerTypes, int> swappedKey = MakeKey(m_Sightings[iIdx].owner, m_Sightings[iIdx].unitId);
		m_SightingIndex[swappedKey] = iIdx;
	}

	m_Sightings.pop_back();
	m_SightingIndex.erase(it);
}

/// Remove all sightings that have expired (not seen for too long).
/// Also resets per-turn tracking state for surviving sightings and
/// finalizes the previous turn's heading into the EMA.
void CvUnitSightingManager::CleanupExpired(int currentTurn)
{
	for (int i = (int)m_Sightings.size() - 1; i >= 0; i--)
	{
		if (m_Sightings[i].IsExpired(currentTurn))
		{
			OnUnitDestroyed((PlayerTypes)m_Sightings[i].owner, (int)m_Sightings[i].unitId);
		}
		else
		{
			// Finalize previous turn's overall heading into EMA before resetting
			UnitSighting& s = m_Sightings[i];
			if (s.turnStartX >= 0 && (s.lastDeltaX != 0 || s.lastDeltaY != 0))
			{
				if (s.avgDX == 0 && s.avgDY == 0)
				{
					// First direction data ever — seed EMA directly
					s.avgDX = s.lastDeltaX;
					s.avgDY = s.lastDeltaY;
				}
				else
				{
					// Blend: 62.5% new + 37.5% old
					s.avgDX = (char)(((int)s.avgDX * 3 + (int)s.lastDeltaX * 5) / 8);
					s.avgDY = (char)(((int)s.avgDY * 3 + (int)s.lastDeltaY * 5) / 8);
				}
			}

			// Reset turn-start sentinel for the new turn
			s.turnStartX = -1;
			s.turnStartY = -1;
		}
	}
}

/// Update ghost predictions (called each turn).
void CvUnitSightingManager::UpdateGhosts(int currentTurn)
{
	// For now, ghosts are passively managed — their uncertainty radius grows
	// automatically via GetUncertaintyRadius(). Future: add active prediction
	// refinement here if needed.
	(void)currentTurn;
}

// === Queries ===

/// Find a specific sighting by owner and unitId.
UnitSighting* CvUnitSightingManager::GetSighting(PlayerTypes eOwner, int iUnitId)
{
	std::pair<PlayerTypes, int> unitKey = MakeKey(eOwner, iUnitId);
	std::map<std::pair<PlayerTypes, int>, int>::iterator it = m_SightingIndex.find(unitKey);
	if (it != m_SightingIndex.end())
		return &m_Sightings[it->second];
	return NULL;
}

const UnitSighting* CvUnitSightingManager::GetSighting(PlayerTypes eOwner, int iUnitId) const
{
	std::pair<PlayerTypes, int> unitKey = MakeKey(eOwner, iUnitId);
	std::map<std::pair<PlayerTypes, int>, int>::const_iterator it = m_SightingIndex.find(unitKey);
	if (it != m_SightingIndex.end())
		return &m_Sightings[it->second];
	return NULL;
}

/// Get all hostile sightings (ghosts and confirmed) near a plot within iRadius.
int CvUnitSightingManager::GetHostileSightingsNearPlot(CvPlot* pPlot, int iRadius, std::vector<UnitSighting*>& results)
{
	results.clear();
	if (!pPlot || !m_pPlayer)
		return 0;

	int iCenterX = pPlot->getX();
	int iCenterY = pPlot->getY();
	PlayerTypes eUs = m_pPlayer->GetID();

	for (int i = 0; i < (int)m_Sightings.size(); i++)
	{
		UnitSighting& s = m_Sightings[i];

		// Skip our own units (shouldn't be here, but defensive)
		if ((PlayerTypes)s.owner == eUs)
			continue;

		// Must be hostile
		if (!GET_TEAM(m_pPlayer->getTeam()).isAtWar(GET_PLAYER((PlayerTypes)s.owner).getTeam()))
			continue;

		int iDist = plotDistance(iCenterX, iCenterY, (int)s.x, (int)s.y);
		if (iDist <= iRadius)
		{
			results.push_back(&s);
		}
	}

	return (int)results.size();
}

/// Get ghosts in the predicted search cone — units that entered fog moving
/// in direction (dirX, dirY) from (centerX, centerY).
int CvUnitSightingManager::GetGhostsInSearchCone(int centerX, int centerY,
	int dirX, int dirY, int maxDist,
	std::vector<UnitSighting*>& results)
{
	results.clear();
	int currentTurn = GC.getGame().getGameTurn();

	for (int i = 0; i < (int)m_Sightings.size(); i++)
	{
		UnitSighting& s = m_Sightings[i];

		// Only ghosts (not confirmed, not expired)
		if (s.IsConfirmed(currentTurn) || s.IsExpired(currentTurn))
			continue;

		int iDist = plotDistance(centerX, centerY, (int)s.x, (int)s.y);
		if (iDist > maxDist)
			continue;

		int dx = (int)s.x - centerX;
		int dy = (int)s.y - centerY;
		if (dirX != 0 || dirY != 0)
		{
			int dot = dx * dirX + dy * dirY;
			if (dot <= 0)
				continue;
		}

		results.push_back(&s);
	}

	return (int)results.size();
}

/// Count siege sightings (confirmed or ghost) near a city.
int CvUnitSightingManager::CountSiegeUnitsNearCity(const CvCity* pCity, int iRadius, bool bAllowImminentCheck) const
{
	if (!pCity)
		return 0;

	int iCount = 0;
	int iCX = pCity->getX();
	int iCY = pCity->getY();
	int currentTurn = GC.getGame().getGameTurn();
	CvDiplomacyAI* pDiploAI = (m_pPlayer != NULL) ? m_pPlayer->GetDiplomacyAI() : NULL;
	TeamTypes eOurTeam = (m_pPlayer != NULL) ? m_pPlayer->getTeam() : NO_TEAM;

	for (int i = 0; i < (int)m_Sightings.size(); i++)
	{
		const UnitSighting& s = m_Sightings[i];

		if (s.IsExpired(currentTurn))
			continue;

		if (!(s.flags & SIGHTING_FLAG_SIEGE))
			continue;

		PlayerTypes eOwner = (PlayerTypes)s.owner;
		if (eOwner == NO_PLAYER)
			continue;

		bool bHostileOrAtWar = false;
		if (pDiploAI && eOurTeam != NO_TEAM)
		{
			TeamTypes eOwnerTeam = GET_PLAYER(eOwner).getTeam();
			if (GET_TEAM(eOurTeam).isAtWar(eOwnerTeam))
				bHostileOrAtWar = true;
			else if (!GET_PLAYER(eOwner).isMinorCiv() && pDiploAI->GetCivApproach(eOwner) == CIV_APPROACH_HOSTILE)
				bHostileOrAtWar = true;
			else if (bAllowImminentCheck && !GET_PLAYER(eOwner).isMinorCiv() && pDiploAI->IsAttackLikelyImminent(eOwner))
				bHostileOrAtWar = true;
		}

		if (!bHostileOrAtWar)
			continue;

		int iDist = plotDistance(iCX, iCY, (int)s.x, (int)s.y);
		if (iDist <= iRadius)
			iCount++;
	}

	return iCount;
}

/// Check if a plot is within the predicted search cone for a fog ghost.
bool CvUnitSightingManager::IsPlotInSearchCone(const UnitSighting* pGhost, int plotX, int plotY, int currentTurn) const
{
	if (!pGhost)
		return false;

	// Distance check
	int iDist = plotDistance((int)pGhost->x, (int)pGhost->y, plotX, plotY);
	int iUncertainty = pGhost->GetUncertaintyRadius(currentTurn);
	if (iDist > iUncertainty)
		return false;

	// If unit was stationary, search all directions
	if (pGhost->lastDeltaX == 0 && pGhost->lastDeltaY == 0)
		return true;

	// Direction check — dot product
	int dx = plotX - (int)pGhost->x;
	int dy = plotY - (int)pGhost->y;
	int dot = dx * (int)pGhost->lastDeltaX + dy * (int)pGhost->lastDeltaY;

	// Determine cone width based on unit type
	bool bIsNaval = (pGhost->flags & SIGHTING_FLAG_NAVAL) != 0;
	bool bIsFast  = (pGhost->movementPoints >= 4);

	if (bIsNaval || bIsFast)
	{
		// ~90-degree cone — only forward direction
		return dot > 0;
	}
	else
	{
		// ~120-degree cone — forward plus some side movement
		int iMagnitude = abs((int)pGhost->lastDeltaX) + abs((int)pGhost->lastDeltaY);
		return dot > -(iMagnitude * iDist / 3);
	}
}

/// Could this ghost potentially threaten a specific city?
bool CvUnitSightingManager::CouldGhostThreatenCity(const UnitSighting* pGhost, CvCity* pCity, int currentTurn) const
{
	if (!pGhost || !pCity)
		return false;

	int turnsMissing = currentTurn - (int)pGhost->lastSeenTurn;

	// Project ghost position along heading
	int projectedX = (int)pGhost->x + (int)pGhost->lastDeltaX * turnsMissing;
	int projectedY = (int)pGhost->y + (int)pGhost->lastDeltaY * turnsMissing;

	// Distance from projected position to city
	int distToCity = plotDistance(projectedX, projectedY, pCity->getX(), pCity->getY());

	// Within strike range?  +2 for attack range
	int strikeRange = (int)pGhost->movementPoints + 2;
	return distToCity <= strikeRange;
}

/// Infer intent from unit context and war state.
UnitPredictedIntent CvUnitSightingManager::InferUnitIntent(const UnitSighting* pSighting, CvUnit* pUnit) const
{
	if (!pSighting || !pUnit || !m_pPlayer)
		return UNIT_INTENT_UNKNOWN;

	PlayerTypes eOwner = (PlayerTypes)pSighting->owner;
	CvDiplomacyAI* pDiploAI = m_pPlayer->GetDiplomacyAI();
	if (!pDiploAI)
		return UNIT_INTENT_UNKNOWN;

	// Damaged units likely retreating
	if (pSighting->health < 50)
		return UNIT_INTENT_RETREAT;

	// Siege units are for city attacks
	if (pSighting->flags & SIGHTING_FLAG_SIEGE)
		return UNIT_INTENT_ATTACK_CITY;

	// Check if at war
	if (!pDiploAI->IsAtWar(eOwner))
		return UNIT_INTENT_PATROL;  // Not at war — probably patrolling

	// War state: our perspective against the unit's owner
	WarStateTypes eWarState = pDiploAI->GetWarState(eOwner);

	// If we're winning, the enemy is likely retreating
	if (eWarState == WAR_STATE_OFFENSIVE || eWarState == WAR_STATE_NEARLY_WON)
		return UNIT_INTENT_RETREAT;

	// If we're losing, the enemy is likely attacking
	if (eWarState == WAR_STATE_DEFENSIVE || eWarState == WAR_STATE_NEARLY_DEFEATED)
		return UNIT_INTENT_ATTACK_CITY;

	// Early war: WarScore is near zero because no combat has happened yet.
	// Healthy enemy units should be assumed attacking — they just went to war.
	if (eWarState == WAR_STATE_STALEMATE || eWarState == WAR_STATE_CALM)
	{
		int iNumTurnsAtWar = GET_TEAM(m_pPlayer->getTeam()).GetNumTurnsAtWar(GET_PLAYER(eOwner).getTeam());
		if (iNumTurnsAtWar <= 5)
			return UNIT_INTENT_ATTACK_CITY;
	}

	// Distance-based intent: if unit is close to one of our cities, it's likely attacking
	{
		CvPlot* pUnitPlot = GC.getMap().plot((int)pSighting->x, (int)pSighting->y);
		if (pUnitPlot)
		{
			CvCity* pNearestCity = m_pPlayer->GetClosestCityByPlots(pUnitPlot);
			if (pNearestCity)
			{
				int iDist = plotDistance(pNearestCity->getX(), pNearestCity->getY(),
					(int)pSighting->x, (int)pSighting->y);
				if (iDist <= 5)
					return UNIT_INTENT_ATTACK_CITY;
			}
		}
	}

	return UNIT_INTENT_UNKNOWN;
}

/// Count enemy units with observed movement heading toward a city.
/// Uses dot product of (lastDeltaX/Y) vs (unit -> city) vector; positive = converging.
/// If eEnemy is NO_PLAYER, counts ALL hostile players' units.
int CvUnitSightingManager::CountUnitsConvergingOnCity(const CvCity* pCity, int iRadius, PlayerTypes eEnemy) const
{
	if (!pCity || !m_pPlayer)
		return 0;

	int iCX = pCity->getX();
	int iCY = pCity->getY();
	int currentTurn = GC.getGame().getGameTurn();
	TeamTypes eOurTeam = m_pPlayer->getTeam();
	int iCount = 0;

	for (int i = 0; i < (int)m_Sightings.size(); i++)
	{
		const UnitSighting& s = m_Sightings[i];

		if (s.IsExpired(currentTurn))
			continue;

		PlayerTypes eOwner = (PlayerTypes)s.owner;
		if (eOwner == NO_PLAYER)
			continue;

		// Filter by specific enemy or any hostile player
		if (eEnemy != NO_PLAYER)
		{
			if (eOwner != eEnemy)
				continue;
		}
		else
		{
			// Must be at war with us
			if (!GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(eOwner).getTeam()))
				continue;
		}

		// Distance check
		int iDist = plotDistance(iCX, iCY, (int)s.x, (int)s.y);
		if (iDist > iRadius)
			continue;

		// Direction check: need a non-zero movement vector
		if (s.lastDeltaX == 0 && s.lastDeltaY == 0)
		{
			// No direction data — count nearby at-war units anyway if close enough
			// (within 3 tiles = imminently threatening the city regardless of heading)
			if (iDist <= 3)
				iCount++;
			continue;
		}

		// Dot product: movement vector vs (unit -> city) vector
		// Prefer EMA (smoothed multi-turn trend) when available
		int dxToCity = iCX - (int)s.x;
		int dyToCity = iCY - (int)s.y;
		char dirX = (s.avgDX != 0 || s.avgDY != 0) ? s.avgDX : s.lastDeltaX;
		char dirY = (s.avgDX != 0 || s.avgDY != 0) ? s.avgDY : s.lastDeltaY;
		int iDot = (int)dirX * dxToCity + (int)dirY * dyToCity;

		// Positive dot = heading toward city
		if (iDot > 0)
			iCount++;
	}

	return iCount;
}

/// Detect coordinated attack on a city: 4+ units converging, or 2+ with siege.
bool CvUnitSightingManager::IsCoordinatedAttackOnCity(const CvCity* pCity, PlayerTypes eEnemy) const
{
	if (!pCity)
		return false;

	int iConverging = CountUnitsConvergingOnCity(pCity, 6, eEnemy);
	if (iConverging >= 4)
		return true;

	if (iConverging >= 2 && CountSiegeUnitsNearCity(pCity, 6, false) >= 1)
		return true;

	return false;
}

/// City-aware intent inference: uses dot product of movement direction vs.
/// unit-to-city vector to classify approach / retreat more accurately.
/// This gives city defense better targeting: a wounded unit moving TOWARD
/// the city is still attacking, and a healthy unit moving AWAY after a
/// failed assault is retreating.
UnitPredictedIntent CvUnitSightingManager::InferUnitIntentNearCity(const UnitSighting* pSighting, int iCityX, int iCityY, int iCurrentTurn, bool bMovedThisTurn) const
{
	if (!pSighting || !m_pPlayer)
		return UNIT_INTENT_UNKNOWN;

	PlayerTypes eOwner = (PlayerTypes)pSighting->owner;
	CvDiplomacyAI* pDiploAI = m_pPlayer->GetDiplomacyAI();

	// Siege units are ALWAYS attacking cities — never deprioritize them
	if (pSighting->flags & SIGHTING_FLAG_SIEGE)
		return UNIT_INTENT_ATTACK_CITY;

	// Not at war — patrol, regardless of direction
	if (pDiploAI && !pDiploAI->IsAtWar(eOwner))
		return UNIT_INTENT_PATROL;

	// === Directional analysis ===
	// Use dot product of (movement vector) and (unit → city vector)
	// Positive dot = moving toward city, negative = moving away
	// Prefer EMA (smoothed multi-turn trend) when available; fall back to lastDelta
	bool bIsFresh = (pSighting->lastSeenTurn == (short)iCurrentTurn);
	char dirX = (pSighting->avgDX != 0 || pSighting->avgDY != 0) ? pSighting->avgDX : pSighting->lastDeltaX;
	char dirY = (pSighting->avgDX != 0 || pSighting->avgDY != 0) ? pSighting->avgDY : pSighting->lastDeltaY;
	bool bHasDirection = bMovedThisTurn && bIsFresh && (dirX != 0 || dirY != 0);
	int iDot = 0;
	if (bHasDirection)
	{
		int dxToCity = iCityX - (int)pSighting->x;
		int dyToCity = iCityY - (int)pSighting->y;
		iDot = (int)dirX * dxToCity + (int)dirY * dyToCity;
	}

	bool bMovingToward = (bHasDirection && iDot > 0);
	bool bMovingAway   = (bHasDirection && iDot < 0);
	bool bDamaged      = (pSighting->health < 50);
	bool bLightDamage  = (pSighting->health < 75);

	// War state context (if available)
	WarStateTypes eWarState = WAR_STATE_STALEMATE;
	if (pDiploAI)
		eWarState = pDiploAI->GetWarState(eOwner);

	bool bWeAreWinning = (eWarState == WAR_STATE_OFFENSIVE || eWarState == WAR_STATE_NEARLY_WON);
	bool bWeAreLosing  = (eWarState == WAR_STATE_DEFENSIVE || eWarState == WAR_STATE_NEARLY_DEFEATED);

	// Early war override: WarScore is near zero because no combat has happened yet.
	// Treat healthy enemy units near our cities as attacking during the opening turns.
	bool bEarlyWar = false;
	if (!bWeAreWinning && !bWeAreLosing)
	{
		int iNumTurnsAtWar = GET_TEAM(m_pPlayer->getTeam()).GetNumTurnsAtWar(GET_PLAYER(eOwner).getTeam());
		bEarlyWar = (iNumTurnsAtWar <= 5);
		if (bEarlyWar)
			bWeAreLosing = true;  // Assume defensive posture — treat enemies as attacking
	}

	// --- Decision matrix: combine direction + health + war state ---

	// Case 1: Moving AWAY from city
	if (bMovingAway)
	{
		// Wounded + moving away = almost certainly retreating
		if (bDamaged)
			return UNIT_INTENT_RETREAT;

		// Lightly damaged + moving away + we're winning = retreating
		if (bLightDamage && bWeAreWinning)
			return UNIT_INTENT_RETREAT;

		// Healthy but moving away while we're winning = probably withdrawing
		if (bWeAreWinning)
			return UNIT_INTENT_RETREAT;

		// Healthy, moving away, stalemate or we're losing — could be
		// repositioning/flanking, don't classify as retreating
		return UNIT_INTENT_UNKNOWN;
	}

	// Case 2: Moving TOWARD city
	if (bMovingToward)
	{
		// Moving toward city = attacking, even if wounded!
		// A wounded catapult pushing toward is still a siege threat
		if (bWeAreLosing)
			return UNIT_INTENT_ATTACK_CITY;

		// Any unit approaching during wartime is likely attacking
		return UNIT_INTENT_ATTACK_CITY;
	}

	// Case 3: No directional data (unit hasn't moved since we started tracking,
	// or we only saw the destination, not origin)
	// Fall back to health + war state heuristic (same as original InferUnitIntent)
	if (bDamaged)
	{
		// Wounded with no movement info — lean toward retreat unless we're losing
		if (bWeAreLosing)
			return UNIT_INTENT_UNKNOWN;  // Could go either way
		return UNIT_INTENT_RETREAT;
	}

	if (bWeAreWinning)
		return UNIT_INTENT_RETREAT;

	if (bWeAreLosing)
		return UNIT_INTENT_ATTACK_CITY;

	return UNIT_INTENT_UNKNOWN;
}

// === Serialization ===

void CvUnitSightingManager::Write(FDataStream& kStream) const
{
	int iCount = (int)m_Sightings.size();
	kStream << iCount;

	for (int i = 0; i < iCount; i++)
	{
		const UnitSighting& s = m_Sightings[i];
		kStream << s.unitId;
		kStream << s.unitType;
		kStream << s.owner;
		kStream << s.x;
		kStream << s.y;
		kStream << s.lastSeenTurn;
		kStream << s.health;
		kStream << s.flags;
		kStream << s.lastDeltaX;
		kStream << s.lastDeltaY;
		kStream << s.movementPoints;
		kStream << s.predictedIntent;
		kStream << s.turnStartX;
		kStream << s.turnStartY;
		kStream << s.avgDX;
		kStream << s.avgDY;
	}
}

void CvUnitSightingManager::Read(FDataStream& kStream)
{
	m_Sightings.clear();
	m_SightingIndex.clear();

	int iCount = 0;
	kStream >> iCount;

	// Safety clamp
	if (iCount < 0)
		iCount = 0;
	if (iCount > AI_MAX_UNIT_SIGHTINGS)
		iCount = AI_MAX_UNIT_SIGHTINGS;

	m_Sightings.resize(iCount);

	for (int i = 0; i < iCount; i++)
	{
		UnitSighting& s = m_Sightings[i];
		kStream >> s.unitId;
		kStream >> s.unitType;
		kStream >> s.owner;
		kStream >> s.x;
		kStream >> s.y;
		kStream >> s.lastSeenTurn;
		kStream >> s.health;
		kStream >> s.flags;
		kStream >> s.lastDeltaX;
		kStream >> s.lastDeltaY;
		kStream >> s.movementPoints;
		kStream >> s.predictedIntent;
		kStream >> s.turnStartX;
		kStream >> s.turnStartY;
		kStream >> s.avgDX;
		kStream >> s.avgDY;

		// Rebuild index
		std::pair<PlayerTypes, int> unitKey = MakeKey(s.owner, s.unitId);
		m_SightingIndex[unitKey] = i;
	}
}
