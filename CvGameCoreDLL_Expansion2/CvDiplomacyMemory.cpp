/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.
	All other marks and trademarks are the property of their respective owners.
	All rights reserved.
	------------------------------------------------------------------------------------------------------- */

#include "CvGameCoreDLLPCH.h"
#include "CvDiplomacyMemory.h"
#include "CvDiplomacyAI.h"
#include "CvCity.h"
#include "CvPlayer.h"

CvDiplomacyMemory::CvDiplomacyMemory()
	: m_pDiplomacyAI(NULL)
	, m_bEvaluatingAttackLikelyImminent(false)
{
	memset(&m_Memory, 0, sizeof(m_Memory));
}

void CvDiplomacyMemory::Init(CvDiplomacyAI* pDiplomacyAI)
{
	m_pDiplomacyAI = pDiplomacyAI;
	m_bEvaluatingAttackLikelyImminent = false;
	memset(&m_Memory, 0, sizeof(m_Memory));
}

/// Write memory buffer to save stream.
void CvDiplomacyMemory::WriteMemorySystem(FDataStream& kStream) const
{
	// CivMemory header
	kStream << m_Memory.currentIndex;
	kStream << m_Memory.validCount;

	// Each snapshot
	for (int i = 0; i < AI_MEMORY_DEPTH; i++)
	{
		const TurnSnapshot& snap = m_Memory.history[i];
		kStream << snap.turn;
		kStream << snap.warState;
		kStream << snap.approach;
		kStream << snap.theirMilitaryNearUs;
		kStream << snap.theirMilitaryStrength;
		kStream << snap.proximity;
		kStream << snap.siegeUnitsNearUs;
		kStream << snap.navalUnitsNearUs;
		kStream << snap.militaryRank;
		kStream << snap.numCities;
		kStream << snap.goldPerTurn;
		kStream << snap.numUnitsNearBorders;
		kStream << snap.numWars;
		kStream << snap.flags;
		kStream << snap.padding;
	}
}

/// Read memory buffer from save stream.
void CvDiplomacyMemory::ReadMemorySystem(FDataStream& kStream)
{
	kStream >> m_Memory.currentIndex;
	kStream >> m_Memory.validCount;

	for (int i = 0; i < AI_MEMORY_DEPTH; i++)
	{
		TurnSnapshot& snap = m_Memory.history[i];
		kStream >> snap.turn;
		kStream >> snap.warState;
		kStream >> snap.approach;
		kStream >> snap.theirMilitaryNearUs;
		kStream >> snap.theirMilitaryStrength;
		kStream >> snap.proximity;
		kStream >> snap.siegeUnitsNearUs;
		kStream >> snap.navalUnitsNearUs;
		kStream >> snap.militaryRank;
		kStream >> snap.numCities;
		kStream >> snap.goldPerTurn;
		kStream >> snap.numUnitsNearBorders;
		kStream >> snap.numWars;
		kStream >> snap.flags;
		kStream >> snap.padding;
	}
}

/// Capture a snapshot of the current turn's diplomatic & military state into
/// the ring buffer. Called at the START of DoTurn() before state updates.
void CvDiplomacyMemory::CaptureMemorySnapshot()
{
	if (!m_pDiplomacyAI)
		return;

	PlayerTypes eMyPlayer = m_pDiplomacyAI->GetPlayer()->GetID();
	CvPlayer* pMyPlayer = m_pDiplomacyAI->GetPlayer();
	int iCurrentTurn = GC.getGame().getGameTurn();

	// Advance circular buffer and get a zeroed slot
	TurnSnapshot* pSnap = m_Memory.AdvanceAndGetNew();
	pSnap->turn = (short)iCurrentTurn;

	// === Per-civ data ===
	int iNumWars = 0;
	int iTotalUnitsNearUs = 0;

	for (int iPlayer = 0; iPlayer < MAX_MAJOR_CIVS; iPlayer++)
	{
		PlayerTypes eOther = (PlayerTypes)iPlayer;
		if (!GET_PLAYER(eOther).isAlive() || eOther == eMyPlayer)
			continue;

		// War state
		pSnap->warState[iPlayer] = (char)m_pDiplomacyAI->GetWarState(eOther);
		if (m_pDiplomacyAI->IsAtWar(eOther))
			iNumWars++;

		// Approach
		pSnap->approach[iPlayer] = (char)m_pDiplomacyAI->GetCivApproach(eOther);

		// Raw combat unit count near our borders (hybrid: intent filtering happens in detection)
		int iUnitCount = m_pDiplomacyAI->CountCombatUnitsNearUs(eOther);
		int iClamped = (iUnitCount > 255) ? 255 : iUnitCount;
		pSnap->theirMilitaryNearUs[iPlayer] = (unsigned char)iClamped;
		iTotalUnitsNearUs += iClamped;

		// Their military strength scaled to 0-255
		int iTheirMight = GET_PLAYER(eOther).GetMilitaryMight();
		int iMaxMight = 10000; // Reasonable cap
		int iScaled = (iTheirMight * 255) / max(1, iMaxMight);
		pSnap->theirMilitaryStrength[iPlayer] = (unsigned char)min(255, iScaled);

		// Proximity
		pSnap->proximity[iPlayer] = (unsigned char)pMyPlayer->GetProximityToPlayer(eOther);

		// Siege and naval units near us — counted via dedicated helpers
		int iSiege = m_pDiplomacyAI->CountSiegeUnitsNearUs(eOther);
		pSnap->siegeUnitsNearUs[iPlayer] = (unsigned char)min(255, iSiege);
		int iNaval = m_pDiplomacyAI->CountNavalUnitsNearUs(eOther);
		pSnap->navalUnitsNearUs[iPlayer] = (unsigned char)min(255, iNaval);
	}

	// === Our state ===
	pSnap->numCities = (unsigned char)min(255, pMyPlayer->getNumCities());
	pSnap->goldPerTurn = (short)pMyPlayer->calculateGoldRate();
	pSnap->numUnitsNearBorders = (unsigned char)min(255, iTotalUnitsNearUs);
	pSnap->numWars = (unsigned char)iNumWars;

	// Military rank: count how many alive major civs have higher military might
	int iOurMight = pMyPlayer->GetMilitaryMight();
	int iRank = 1;
	for (int iPlayer = 0; iPlayer < MAX_MAJOR_CIVS; iPlayer++)
	{
		PlayerTypes eOther = (PlayerTypes)iPlayer;
		if (eOther == eMyPlayer || !GET_PLAYER(eOther).isAlive())
			continue;
		if (GET_PLAYER(eOther).GetMilitaryMight() > iOurMight)
			iRank++;
	}
	pSnap->militaryRank = (unsigned char)iRank;

	// Flags
	pSnap->flags = SNAPSHOT_FLAG_NONE;
	if (iNumWars > 0)
		pSnap->flags |= SNAPSHOT_FLAG_AT_WAR;

	// Check for winning/losing wars
	for (int iPlayer = 0; iPlayer < MAX_MAJOR_CIVS; iPlayer++)
	{
		PlayerTypes eOther = (PlayerTypes)iPlayer;
		if (!m_pDiplomacyAI->IsAtWar(eOther))
			continue;

		WarStateTypes eWarState = m_pDiplomacyAI->GetWarState(eOther);
		if (eWarState == WAR_STATE_NEARLY_WON || eWarState == WAR_STATE_OFFENSIVE)
			pSnap->flags |= SNAPSHOT_FLAG_WINNING_WAR;
		if (eWarState == WAR_STATE_NEARLY_DEFEATED || eWarState == WAR_STATE_DEFENSIVE)
			pSnap->flags |= SNAPSHOT_FLAG_LOSING_WAR;
	}
}

/// Detect if a player is massing troops near our borders (3+ turn rising trend).
bool CvDiplomacyMemory::IsPlayerBuildingUpNearUs(PlayerTypes ePlayer) const
{
	if (!m_pDiplomacyAI || !m_pDiplomacyAI->IsLikelyIntentAgainstUs(ePlayer))
		return false;

	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	const TurnSnapshot* p3Ago = m_Memory.GetTurnsAgo(3);
	const TurnSnapshot* p6Ago = m_Memory.GetTurnsAgo(6);

	if (!pNow || !p3Ago) return false;

	int iNow  = pNow->theirMilitaryNearUs[ePlayer];
	int i3Ago = p3Ago->theirMilitaryNearUs[ePlayer];
	int i6Ago = p6Ago ? p6Ago->theirMilitaryNearUs[ePlayer] : 0;

	bool bRising3 = (iNow > i3Ago);
	bool bRising6 = p6Ago ? (i3Ago > i6Ago) : true;
	bool bSignificant = (iNow - i6Ago >= 5);

	return bRising3 && bRising6 && bSignificant;
}

/// Siege units near borders = very high-confidence attack signal.
bool CvDiplomacyMemory::IsSiegeWarningActive(PlayerTypes ePlayer) const
{
	if (!m_pDiplomacyAI || !m_pDiplomacyAI->IsLikelyIntentAgainstUs(ePlayer))
		return false;

	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	if (!pNow) return false;
	return pNow->siegeUnitsNearUs[ePlayer] >= 2;
}

/// Detect if player is creeping closer via city placement.
bool CvDiplomacyMemory::IsPlayerCreepingCloser(PlayerTypes ePlayer) const
{
	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	const TurnSnapshot* pOldest = m_Memory.GetTurnsAgo(AI_MEMORY_DEPTH - 1);
	if (!pNow || !pOldest) return false;
	return (pNow->proximity[ePlayer] > pOldest->proximity[ePlayer]);
}

/// Check if approach changed within the last N turns.
bool CvDiplomacyMemory::HasApproachChangedRecently(PlayerTypes ePlayer, int iWithinTurns) const
{
	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	if (!pNow) return false;

	int iCurrentApproach = pNow->approach[ePlayer];

	for (int i = 1; i <= iWithinTurns && i < AI_MEMORY_DEPTH; i++)
	{
		const TurnSnapshot* pPast = m_Memory.GetTurnsAgo(i);
		if (pPast && pPast->approach[ePlayer] != iCurrentApproach)
			return true;
	}
	return false;
}

/// Detect shift specifically toward hostility / war from friendlier stance.
bool CvDiplomacyMemory::HasTurnedHostileRecently(PlayerTypes ePlayer, int iWithinTurns) const
{
	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	if (!pNow) return false;

	CivApproachTypes eNowApproach = (CivApproachTypes)pNow->approach[ePlayer];
	if (eNowApproach != CIV_APPROACH_HOSTILE && eNowApproach != CIV_APPROACH_WAR)
		return false;

	for (int i = 1; i <= iWithinTurns && i < AI_MEMORY_DEPTH; i++)
	{
		const TurnSnapshot* pPast = m_Memory.GetTurnsAgo(i);
		if (pPast)
		{
			CivApproachTypes ePastApproach = (CivApproachTypes)pPast->approach[ePlayer];
			if (ePastApproach == CIV_APPROACH_FRIENDLY || ePastApproach == CIV_APPROACH_NEUTRAL)
				return true;
		}
	}
	return false;
}

/// Am I fighting too many wars relative to my strength?
bool CvDiplomacyMemory::AmIOverextended() const
{
	if (!m_pDiplomacyAI)
		return false;

	const TurnSnapshot* pNow = m_Memory.GetTurnsAgo(0);
	if (!pNow) return false;

	// Classic check: 2+ wars and weak military rank
	if (pNow->numWars >= 2 && pNow->militaryRank > 20)
		return true;

	// Coalition-aware check: if we're in 3+ wars regardless of military rank,
	// or 2+ wars where at least one enemy is strong, we're overextended
	if (pNow->numWars >= 3)
		return true;

	// Check if multiple hostile civs are likely to attack soon (coalition forming)
	if (pNow->numWars >= 1)
	{
		int iImminentAttacks = 0;
		for (int i = 0; i < MAX_MAJOR_CIVS; i++)
		{
			PlayerTypes ePlayer = (PlayerTypes)i;
			if (ePlayer == m_pDiplomacyAI->GetPlayer()->GetID() || !GET_PLAYER(ePlayer).isAlive())
				continue;
			if (m_pDiplomacyAI->GetPlayer()->IsAtWarWith(ePlayer))
				continue; // already at war, skip

			if (IsAttackLikelyImminent(ePlayer))
				iImminentAttacks++;
		}

		// Already in a war and another attack is likely? We're overextended.
		if (iImminentAttacks >= 1)
			return true;
	}

	return false;
}

/// Calculate threat level for a past turn from stored components.
int CvDiplomacyMemory::GetHistoricalThreat(PlayerTypes ePlayer, int iTurnsAgo) const
{
	const TurnSnapshot* pSnap = m_Memory.GetTurnsAgo(iTurnsAgo);
	if (!pSnap) return 0;

	int iThreat = 0;
	iThreat += pSnap->theirMilitaryStrength[ePlayer] * 2;
	iThreat += pSnap->theirMilitaryNearUs[ePlayer] * 15;
	iThreat += pSnap->siegeUnitsNearUs[ePlayer] * 30;
	iThreat += pSnap->proximity[ePlayer] * 10;

	CivApproachTypes eApproach = (CivApproachTypes)pSnap->approach[ePlayer];
	if (eApproach == CIV_APPROACH_HOSTILE)
		iThreat += 50;
	if (eApproach == CIV_APPROACH_WAR)
		iThreat += 100;

	return iThreat;
}

/// Detect 30%+ threat increase over last 5 turns.
bool CvDiplomacyMemory::IsThreatRising(PlayerTypes ePlayer) const
{
	int iNow  = GetHistoricalThreat(ePlayer, 0);
	int i5Ago = GetHistoricalThreat(ePlayer, 5);
	return (iNow > i5Ago * 130 / 100);
}

/// Aggregate coalition threat: count how many non-war hostile civs are likely to attack.
/// Returns a score where 0 = no coalition threat, higher = more dangerous.
/// Used to trigger pre-emptive diplomacy and defensive posture changes.
int CvDiplomacyMemory::GetCoalitionThreatScore() const
{
	if (!m_pDiplomacyAI)
		return 0;

	int iScore = 0;

	for (int i = 0; i < MAX_MAJOR_CIVS; i++)
	{
		PlayerTypes ePlayer = (PlayerTypes)i;
		if (ePlayer == m_pDiplomacyAI->GetPlayer()->GetID() || !GET_PLAYER(ePlayer).isAlive())
			continue;
		if (m_pDiplomacyAI->GetPlayer()->IsAtWarWith(ePlayer))
			continue; // already at war, counted separately

		// Each imminent attacker adds significantly to coalition threat
		if (IsAttackLikelyImminent(ePlayer))
		{
			iScore += 40;
			continue;
		}

		// Each hostile player building up adds moderately
		if (IsPlayerBuildingUpNearUs(ePlayer))
		{
			iScore += 20;
			if (IsThreatRising(ePlayer))
				iScore += 10;
			continue;
		}

		// Hostile/war approach players who are neighbors add a small amount
		CivApproachTypes eApproach = m_pDiplomacyAI->GetCivApproach(ePlayer);
		if (eApproach == CIV_APPROACH_WAR || eApproach == CIV_APPROACH_HOSTILE)
		{
			PlayerProximityTypes eProximity = m_pDiplomacyAI->GetPlayer()->GetProximityToPlayer(ePlayer);
			if (eProximity >= PLAYER_PROXIMITY_CLOSE)
				iScore += 10;
		}
	}

	return iScore;
}

/// Composite attack prediction: 4+ warning signals = high confidence.
bool CvDiplomacyMemory::IsAttackLikelyImminent(PlayerTypes ePlayer) const
{
	if (!m_pDiplomacyAI)
		return false;

	if (m_bEvaluatingAttackLikelyImminent)
		return false;

	struct ReentrancyGuard
	{
		bool& flag;
		ReentrancyGuard(bool& f) : flag(f) { flag = true; }
		~ReentrancyGuard() { flag = false; }
	} guard(m_bEvaluatingAttackLikelyImminent);

	if (!m_pDiplomacyAI->IsLikelyIntentAgainstUs(ePlayer))
		return false;

	int iWarningSignals = 0;

	if (IsPlayerBuildingUpNearUs(ePlayer))
		iWarningSignals += 2;

	if (IsSiegeWarningActive(ePlayer))
		iWarningSignals += 3;

	if (HasTurnedHostileRecently(ePlayer, 5))
		iWarningSignals += 2;

	if (IsPlayerCreepingCloser(ePlayer))
		iWarningSignals += 1;

	if (IsThreatRising(ePlayer))
		iWarningSignals += 1;

	// Multi-unit convergence: check if any of our cities have a coordinated attack from this player
	const CvUnitSightingManager& sightMgr = m_pDiplomacyAI->GetPlayer()->GetUnitSightingManager();
	int iLoop = 0;
	for (CvCity* pCity = m_pDiplomacyAI->GetPlayer()->firstCity(&iLoop); pCity != NULL; pCity = m_pDiplomacyAI->GetPlayer()->nextCity(&iLoop))
	{
		if (sightMgr.IsCoordinatedAttackOnCity(pCity, ePlayer))
		{
			iWarningSignals += 2;
			break;  // One city is enough evidence
		}
	}

	return iWarningSignals >= 4;
}
