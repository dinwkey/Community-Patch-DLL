/*	-------------------------------------------------------------------------------------------------------
	© 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.  
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software 
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.  
	All other marks and trademarks are the property of their respective owners.  
	All rights reserved. 
	------------------------------------------------------------------------------------------------------- */
#include "CvGameCoreDLLPCH.h"
#include "CvTacticalAI.h"
#include "CvTacticalAnalysisMap.h"
#include "CvGameCoreUtils.h"
#include "CvAStar.h"
#include "CvEconomicAI.h"
#include "CvEnumSerialization.h"
#include "CvUnitCombat.h"
#include "CvGrandStrategyAI.h"
#include "cvStopWatch.h"
#include "CvMilitaryAI.h"
#include "CvCityTargetingAIHelpers.h"
#include "CvTypes.h"
#include "CvDiplomacyAI.h"
#include "CvArmyAI.h"
#include "CvAIOperation.h"
#include "CvBarbarians.h"
#include "CvUnitSightingManager.h"
#include "CvUnitMovement.h"

#include <iomanip>
#include <sstream>
#include <cmath>
#include "LintFree.h"

//for easier debugging
#ifdef VPDEBUG
#define TACTDEBUG 1
#endif

int gCurrentUnitToTrack = 0;
const unsigned char TACTICAL_COMBAT_MAX_TARGET_DISTANCE = 4; //not larger than 4, not smaller than 3
const int TACTICAL_COMBAT_CITADEL_BONUS = 67; //larger than 60 to override firstline/secondline difference
const short TACTICAL_COMBAT_IMPOSSIBLE_SCORE = (short) -1000;
const int TACTSIM_UNIQUENESS_CHECK_GENERATIONS = 3; //higher means check more siblings for permutations
const int TACTSIM_BREADTH_FIRST_GENERATIONS = 3; //switch to depth-first later
const int TACTSIM_MAX_UNITS = 13; //we have limited storage and time ...

//global memory for tactical simulation
CvTactPosStorage gTactPosStorage(6000);
CvSupportPosStorage gSupportPosStorage(60);
CvTactAssignmentStorage gAssignmentStorage(2000);
TCachedMovePlots gReachablePlotsLookup;
TCachedRangeAttackPlots gRangeAttackPlotsLookup;
TCachedDistanceToTargetPlots gDistanceToTargetPlots;
CvPlot* gTargetPlot;
vector<int> gLandEnemies, gSeaEnemies, gCities, gNewlyVisiblePlots;
vector<pair<int, int>> gCitadels;
vector<OptionWithScore<STacticalAssignment*>> gPossibleMoves, gPossibleRangedAttacks, gOverAllChoices;
vector<SComboMove> gMovesToAdd;
map<int, int> gBadUnitsCount;
SUnitIDValueContainer gEnemyDamageDealt;
TUnitFlagLookup gSafePlotCount;
int gDefaultUnitLossThreshold;
int gMedianUnitXP;
int gMinHpForTactsim;
PlayerTypes eLastTactSimPlayer = NO_PLAYER;

int GetRingPlotCountSafe(int iRing)
{
	if (iRing <= 0)
		return RING0_PLOTS;
	if (iRing < 6)
		return RING_PLOTS[iRing];

	return 1 + 3 * iRing * (iRing + 1);
}

//just some statistics
unsigned long gMovePlotsCacheHit = 0, gMovePlotsCacheMiss = 0;
unsigned long gAttackPlotsCacheHit = 0, gAttackPlotsCacheMiss = 0;
unsigned long gAttackCacheHit = 0, gAttackCacheMiss = 0;
unsigned long gDangerCacheHit = 0, gDangerCacheMiss = 0;
unsigned long giEquivalentPos = 0, giDifferentPos = 0;
unsigned long giValidEndPos = 0, giInvalidEndPos = 0;
int gCheckedPositions = 0;

void CheckDebugTrigger(int iUnitID)
{
	if (iUnitID == gCurrentUnitToTrack)
	{
		//put a breakpoint here if required
		OutputDebugString("match\n");
	}
}

bool IsAirUnitCommittedToActiveCarrierGroup(const CvUnit* pUnit)
{
	if (!pUnit || pUnit->getDomainType() != DOMAIN_AIR)
		return false;

	const CvUnit* pCarrier = pUnit->getTransportUnit();
	if (!pCarrier)
		return false;

	if (pCarrier->getArmyID() < 0)
		return false;

	CvPlayer& kOwner = GET_PLAYER(pUnit->getOwner());
	CvArmyAI* pArmy = kOwner.getArmyAI(pCarrier->getArmyID());
	if (!pArmy)
		return false;

	CvAIOperation* pOperation = kOwner.getAIOperation(pArmy->GetOperationID());
	if (!pOperation)
		return false;

	if (pOperation->GetOperationType() != AI_OPERATION_CARRIER_GROUP)
		return false;

	return pOperation->GetOperationState() != AI_OPERATION_STATE_ABORTED;
}

//=====================================
// CvTacticalTarget
//=====================================

/// This target make sense for this domain of unit/zone?
bool CvTacticalTarget::IsTargetValidInThisDomain(DomainTypes eDomain) const
{
	switch(GetTargetType())
	{
	case AI_TACTICAL_TARGET_NONE:
		return false;

	case AI_TACTICAL_TARGET_DEFENSIVE_BASTION:
	case AI_TACTICAL_TARGET_BARBARIAN_CAMP:
	case AI_TACTICAL_TARGET_IMPROVEMENT:
	case AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND:
	case AI_TACTICAL_TARGET_TRADE_UNIT_LAND:
	case AI_TACTICAL_TARGET_ENEMY_CITADEL:
	case AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE:
	case AI_TACTICAL_TARGET_GOODY:
		return eDomain == DOMAIN_LAND;

	case AI_TACTICAL_TARGET_BLOCKADE_POINT:
	case AI_TACTICAL_TARGET_TRADE_UNIT_SEA:
		return eDomain == DOMAIN_SEA;

	case AI_TACTICAL_TARGET_ENEMY_CITY:
	case AI_TACTICAL_TARGET_FRIENDLY_CITY:
	case AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN:
	case AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT:
		return true;
	}

	return false;
}

template<typename FocusArea, typename Visitor>
void CvFocusArea::Serialize(FocusArea& focusArea, Visitor& visitor)
{
	visitor(focusArea.m_iX);
	visitor(focusArea.m_iY);
	visitor(focusArea.m_iRadius);
	visitor(focusArea.m_iLastTurn);
}

FDataStream& operator<<(FDataStream& saveTo, const CvFocusArea& readFrom)
{
	CvStreamSaveVisitor serialVisitor(saveTo);
	CvFocusArea::Serialize(readFrom, serialVisitor);
	return saveTo;
}

FDataStream& operator>>(FDataStream& loadFrom, CvFocusArea& writeTo)
{
	CvStreamLoadVisitor serialVisitor(loadFrom);
	CvFocusArea::Serialize(writeTo, serialVisitor);
	return loadFrom;
}

//=====================================
// CvTacticalAI
//=====================================

/// Constructor
CvTacticalAI::CvTacticalAI(void)
{
}

/// Destructor
CvTacticalAI::~CvTacticalAI(void)
{
	Uninit();
}

/// Initialize
void CvTacticalAI::Init(CvPlayer* pPlayer)
{
	// Store off the pointer to the objects we need elsewhere in the game engine
	m_pPlayer = pPlayer;

	m_tacticalMap.Reset(m_pPlayer ? m_pPlayer->GetID() : NO_PLAYER);
	m_bImminentAttack = false;
	m_coastalLandAssaultCooldownUntilTurn.clear();

	// Initialize AI constants from XML
	m_iRecruitRange = /*8*/ GD_INT_GET(AI_TACTICAL_RECRUIT_RANGE);
	m_iLandBarbarianRange = max(1, GC.getGame().getHandicapInfo().getBarbarianLandTargetRange());
	m_iSeaBarbarianRange = max(1, GC.getGame().getHandicapInfo().getBarbarianSeaTargetRange());
}

/// Deallocate memory created in initialize
void CvTacticalAI::Uninit()
{
}

///
template<typename TacticalAI, typename Visitor>
void CvTacticalAI::Serialize(TacticalAI& tacticalAI, Visitor& visitor)
{
	visitor(tacticalAI.m_focusAreas);
	visitor(tacticalAI.m_tacticalMap);
}

/// Serialization read
void CvTacticalAI::Read(FDataStream& kStream)
{
	CvStreamLoadVisitor serialVisitor(kStream);
	Serialize(*this, serialVisitor);
}

/// Serialization write
void CvTacticalAI::Write(FDataStream& kStream) const
{
	CvStreamSaveVisitor serialVisitor(kStream);
	Serialize(*this, serialVisitor);
}

FDataStream& operator>>(FDataStream& stream, CvTacticalAI& tacticalAI)
{
	tacticalAI.Read(stream);
	return stream;
}
FDataStream& operator<<(FDataStream& stream, const CvTacticalAI& tacticalAI)
{
	tacticalAI.Write(stream);
	return stream;
}

/// Mark all the units that will be under tactical AI control this turn
void CvTacticalAI::RecruitUnits()
{
	int iLoop = 0;
	m_CurrentTurnUnits.clear();

	// Loop through our units
	for(CvUnit* pLoopUnit = m_pPlayer->firstUnit(&iLoop); pLoopUnit; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
	{
		// debugging hook
		if (gCurrentUnitToTrack == pLoopUnit->GetID())
			pLoopUnit->DumpDangerInNeighborhood();

		// we reset this every turn in order to spot units falling through the cracks
		// todo: ideally we have some persistency and prefer to assign the same tasks across turns ...
		pLoopUnit->setTacticalMove(AI_TACTICAL_MOVE_NONE);
		pLoopUnit->setHomelandMove(AI_HOMELAND_MOVE_NONE);

		// Never want immobile/dead units, explorers, ones that have already moved or automated human units
		if(!pLoopUnit->canUseForTacticalAI())
			continue;

		// reset mission AI so we don't see stale information (debugging only)
		pLoopUnit->SetMissionAI(NO_MISSIONAI,NULL,NULL);

		// we want all combat ready air units, except nukes (those go through operational AI)
		if (pLoopUnit->getDomainType() == DOMAIN_AIR)
		{
			//rebasing is done in homeland AI
			if (!ShouldRebase(pLoopUnit))
				m_CurrentTurnUnits.push_back(pLoopUnit->GetID());
		}
		// Now down to land and sea units
		else
		{
			if (pLoopUnit->getArmyID() != -1)
				//army units will be moved as part of the army moves
				pLoopUnit->setTacticalMove(AI_TACTICAL_OPERATION);
			else
				m_CurrentTurnUnits.push_back(pLoopUnit->GetID());
		}
	}

#if defined(MOD_CORE_DEBUGGING)
	if (MOD_CORE_DEBUGGING)
	{
		for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);
			if (!pUnit)
				continue;
			CvString msg = CvString::format("using %s %d at %d,%d for tactical ai. power is %d\n", pUnit->getName().c_str(), pUnit->GetID(), pUnit->getX(), pUnit->getY(), pUnit->GetPower() );
			LogTacticalMessage( msg );
		}
	}
#endif
}

/// Update the AI for units
// Forward declaration - defined later in this file
using namespace CityTargetingAIHelpers;

static bool IsMemoryAttackImminentForPlayer(const CvPlayer* pPlayer);
static void GetCoastalApproachCounts(const CvCity* pCity, int& iLandApproaches, int& iWaterApproaches);
static bool IsNavyLedCoastalAssaultTarget(const CvCity* pCity, bool& bIslandCity, bool& bNavalDominatedCity, int& iLandApproaches, int& iWaterApproaches);
static bool CanReachLandAttackPositionWithoutEmbark(const CvUnit* pUnit, const CvCity* pCity, bool bAllowOneTurnSetup);
static bool ShouldSkipLandUnitForCoastalCapture(const CvUnit* pUnit, const CvCity* pCity, bool bPreferNavalCapture, bool bLandAssaultCooldownActive);
static bool IsTrustedCoalitionPartner(const CvPlayer* pPlayer, PlayerTypes eOtherPlayer, PlayerTypes eTargetOwner);
static int GetCoalitionOutcomeWeight(const CvPlayer* pPlayer, PlayerTypes eOtherPlayer, const CvCity* pCity);
static void GetCoalitionPressureNearCity(const CvPlayer* pPlayer, const CvCity* pCity, int& iHelpfulPressure, int& iHarmfulPressure);
static bool ShouldAvoidRiskyCoalitionCapture(const CvPlayer* pPlayer, const CvCity* pCity, bool bCaptureOpportunityThisTurn, int iOurMeleeCount, int iHelpfulPressure, int iHarmfulPressure);
static bool ShouldDeferCaptureToCoalitionPartner(const CvPlayer* pPlayer, const CvCity* pCity, bool bCaptureOpportunityThisTurn, int iHelpfulPressure, int iHarmfulPressure);

void CvTacticalAI::Update()
{
	m_bImminentAttack = IsMemoryAttackImminentForPlayer(m_pPlayer);
	ExpireCoastalAssaultLandCooldowns();

	UpdateVisibility();
	DropOldFocusAreas();
	FindTacticalTargets();

	//do this after updating the target list!
	RecruitUnits();

	// Loop through each dominance zone assigning moves
	ProcessDominanceZones();
}

void CvTacticalAI::UpdateForHomelandSupport()
{
	m_bImminentAttack = IsMemoryAttackImminentForPlayer(m_pPlayer);
	ExpireCoastalAssaultLandCooldowns();

	UpdateVisibility();
	DropOldFocusAreas();
	FindTacticalTargets();
}

/// Clear up memory usage
void CvTacticalAI::CleanUp()
{
	m_AllTargets.clear();
	m_ZoneTargets.clear();

	// Memory optimization: clear global tactical simulation caches to prevent unbounded growth
	// These are rebuilt for each simulation and don't need to persist between turns
	gReachablePlotsLookup.clear();
	gRangeAttackPlotsLookup.clear();
	gSafePlotCount.clear();
	gBadUnitsCount.clear();
	gDistanceToTargetPlots.clear();

	// Every 10 turns, force capacity release on hash maps to prevent memory fragmentation in long games (32-bit)
	if (GC.getGame().getGameTurn() % 10 == 0)
	{
		TCachedMovePlots().swap(gReachablePlotsLookup);
		TCachedRangeAttackPlots().swap(gRangeAttackPlotsLookup);
		map<int, int>().swap(gBadUnitsCount);
		TUnitFlagLookup().swap(gSafePlotCount);
		TCachedDistanceToTargetPlots().swap(gDistanceToTargetPlots);
	}
}

/// Add a temporary focus of attention around a short-term target
void CvTacticalAI::AddFocusArea(CvPlot* pPlot, int iRadius, int iDuration)
{
	if (!pPlot)
		return;

	CvFocusArea zone;
	zone.m_iX = pPlot->getX();
	zone.m_iY = pPlot->getY();
	zone.m_iRadius = iRadius;
	zone.m_iLastTurn = GC.getGame().getGameTurn() + iDuration;

	m_focusAreas.push_back(zone);
}

/// Remove a temporary focus of attention we no longer need to track
void CvTacticalAI::DeleteFocusArea(CvPlot* pPlot)
{
	std::vector<CvFocusArea> zonesCopy(m_focusAreas);
	m_focusAreas.clear();

	// Copy back to original vector any whose coords don't match
	for(unsigned int iI = 0; iI < zonesCopy.size(); iI++)
		if(zonesCopy[iI].m_iX != pPlot->getX() || zonesCopy[iI].m_iY != pPlot->getY())
			m_focusAreas.push_back(zonesCopy[iI]);
}

/// Remove focus zones that have expired
void CvTacticalAI::DropOldFocusAreas()
{
	std::vector<CvFocusArea> zonesCopy(m_focusAreas);
	m_focusAreas.clear();

	// Copy back to original vector any that haven't expired
	for(unsigned int iI = 0; iI < zonesCopy.size(); iI++)
		if(zonesCopy[iI].m_iLastTurn >= GC.getGame().getGameTurn())
			m_focusAreas.push_back(zonesCopy[iI]);
}

/// Is this a city that an operation just deployed in front of?
bool CvTacticalAI::IsInFocusArea(const CvPlot* pPlot) const
{
	for(unsigned int iI = 0; iI < m_focusAreas.size(); iI++)
		if(plotDistance(pPlot->getX(),pPlot->getY(),m_focusAreas[iI].m_iX,m_focusAreas[iI].m_iY)<=m_focusAreas[iI].m_iRadius)
			return true;

	return false;
}

/// Setup knowledge of other players' seen plots
void CvTacticalAI::UpdateVisibility()
{
	const TeamTypes eTeam = m_pPlayer->getTeam();

	CvPlot* pLoopPlot;

	for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);
		pLoopPlot->ResetKnownVisibility();
	}

	for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);

		if (!pLoopPlot->isRevealed(eTeam))
			continue;

		UpdateVisibilityFromBorders(pLoopPlot);

		if (!pLoopPlot->isVisible(eTeam))
			continue;

		UpdateVisibilityFromUnits(pLoopPlot);
	}
}

/// Check if there are any units owned by other players in this tile and what they can see
void CvTacticalAI::UpdateVisibilityFromUnits(CvPlot* pPlot)
{
	if (!pPlot)
		return;

	const TeamTypes ePlayerTeam = m_pPlayer->getTeam();

	if (pPlot->getNumUnits() > 0)
	{
		CvUnit* pLoopUnit;
		TeamTypes eLoopUnitTeam;

		for (int iI = 0; iI < pPlot->getNumUnits(); iI++)
		{
			pLoopUnit = pPlot->getUnitByIndex(iI);
			if(pLoopUnit == NULL)
			{
				if(GC.getGame().isNetworkMultiPlayer())
				{
					CvString msg; CvString::format(msg, "*** PLOT UNIT DESYNC *** UpdateVisibilityFromUnits: NULL unit at index %d on plot (%d,%d). Plot reports %d units.",
						iI, pPlot->getX(), pPlot->getY(), pPlot->getNumUnits());
					gGlobals.getDLLIFace()->sendChat(msg, CHATTARGET_ALL, NO_PLAYER);
				}
			}
			PRECONDITION(pLoopUnit, "UpdateVisibilityFromUnits: Unit not found on plot, desync between plot unit list and actual unit positions");
			eLoopUnitTeam = pLoopUnit->getTeam();

			if (eLoopUnitTeam != ePlayerTeam && !pLoopUnit->isInvisible(ePlayerTeam, false))
			{
				pPlot->ChangeKnownAdjacentSight(eLoopUnitTeam, NO_TEAM, pLoopUnit->visibilityRange(), pLoopUnit->getFacingDirection(true));
			}
		}
	}

	if (pPlot->IsTradeUnitRoute())
	{
		PlotIndexContainer aiTradeUnitsAtPlot = m_pPlayer->GetTrade()->GetOpposingTradeUnitsAtPlot(pPlot, false);
		for (PlotIndexContainer::const_iterator it = aiTradeUnitsAtPlot.begin(); it != aiTradeUnitsAtPlot.end(); ++it)
		{
			PlayerTypes eTradeUnitOwner = GC.getGame().GetGameTrade()->GetOwnerFromID(*it);

			if (eTradeUnitOwner != NO_PLAYER && GET_PLAYER(eTradeUnitOwner).getTeam() != ePlayerTeam)
				pPlot->IncreaseKnownVisibilityCount(GET_PLAYER(eTradeUnitOwner).getTeam(), NO_TEAM);
		}
	}
}

void CvTacticalAI::UpdateVisibilityFromBorders(CvPlot* pPlot)
{
	const TeamTypes ePlayerTeam = m_pPlayer->getTeam();
	const TeamTypes ePlotTeam = pPlot->getTeam();
	const PlayerTypes eMinorCivAlly = ePlotTeam != NO_TEAM ? (GET_TEAM(ePlotTeam).isMinorCiv() ? GET_PLAYER(pPlot->getOwner()).GetMinorCivAI()->GetAlly() : NO_PLAYER) : NO_PLAYER;
	TeamTypes eMinorCivAllyTeam = eMinorCivAlly != NO_PLAYER ? GET_PLAYER(eMinorCivAlly).getTeam() : NO_TEAM;

	if (eMinorCivAllyTeam == ePlayerTeam)
		eMinorCivAllyTeam = NO_TEAM;

	if (ePlotTeam != NO_TEAM && ePlotTeam != ePlayerTeam)
	{
		pPlot->ChangeKnownAdjacentSight(ePlotTeam, eMinorCivAllyTeam, GD_INT_GET(PLOT_VISIBILITY_RANGE), NO_DIRECTION);
	}
}

// PRIVATE METHODS

/// Make lists of everything we might want to target with the tactical AI this turn
void CvTacticalAI::FindTacticalTargets()
{
	CvPlayerTrade* pPlayerTrade = m_pPlayer->GetTrade();
	bool bImminentAttack = m_bImminentAttack;

	bool bNoBarbsAllowedYet = GC.getGame().getGameTurn() < GC.getGame().GetBarbarianReleaseTurn();
	vector<PlayerTypes> vUnfriendlyMajors = m_pPlayer->GetUnfriendlyMajors();

	// Look at every tile on map
	for (int iI = 0; iI < GC.getMap().numPlots(); iI++)
	{
		CvPlot* pLoopPlot = GC.getMap().plotByIndexUnchecked(iI);
		bool bValidPlot = pLoopPlot->isRevealed(m_pPlayer->getTeam());

		// Make sure I am not a barbarian who can not move into owned territory this early in the game
		if (m_pPlayer->isBarbarian() && bNoBarbsAllowedYet && pLoopPlot->isOwned())
			continue;

		if (bValidPlot)
		{
			//camps are typically revealed but not visible; also the camp might since have been cleared but we don't know yet - so check if it is owned now
			bool bSuspectedBarbCamp = pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT) && !pLoopPlot->isOwned();

			CvTacticalTarget newTarget;
			newTarget.SetTargetX(pLoopPlot->getX());
			newTarget.SetTargetY(pLoopPlot->getY());
			newTarget.SetDominanceZone(GetTacticalAnalysisMap()->GetDominanceZoneID(iI));

			// Have a ...
			// ... friendly city?
			CvCity* pCity = pLoopPlot->getPlotCity();
			if (pCity != NULL)
			{
				if (m_pPlayer->GetID() == pCity->getOwner())
				{
					CvTacticalDominanceZone* pLandZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);
					CvTacticalDominanceZone* pWaterZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);
					int iBorderScore = (pLandZone ? pLandZone->GetBorderScore(DOMAIN_LAND) : 0) + (pWaterZone ? pWaterZone->GetBorderScore(DOMAIN_SEA) : 0);
					if (iBorderScore > 0)
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_FRIENDLY_CITY);
						newTarget.SetAuxIntData(iBorderScore);
						m_AllTargets.push_back(newTarget);
					}
				}

				// ... enemy city
				else if (atWar(m_pPlayer->getTeam(), pCity->getTeam()))
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_ENEMY_CITY);
					//barbarians don't care about cities much compared to normal players
					newTarget.SetAuxIntData( m_pPlayer->isBarbarian() ? 20 : 100);
					m_AllTargets.push_back(newTarget);
				}
			}
			else
			{
				// ... enemy combat unit? we allow visible ones and some we remember from the previous turn
				CvUnit* pUnit = pLoopPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true, true);
				bool bCanSeeUnit = pUnit && pLoopPlot->isVisible(m_pPlayer->getTeam()) && !pUnit->isInvisible(m_pPlayer->getTeam(), false);
				bool bRememberUnit = pUnit && m_pPlayer->IsVanishedUnit(pUnit->GetIDInfo());
				if (bCanSeeUnit || bRememberUnit)
				{
					//minors ignore barbarians until they are close to their borders
					if (!m_pPlayer->isMinorCiv() || !pUnit->isBarbarian() || pUnit->plot()->isAdjacentTeam(m_pPlayer->getTeam()))
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT);
						newTarget.SetUnitPtr(pUnit);
						newTarget.SetAuxIntData(50);
						m_AllTargets.push_back(newTarget);
					}
				}
				// ... unprotected enemy civilian?
				else if (pLoopPlot->isEnemyUnit(m_pPlayer->GetID(),false,true) && !pLoopPlot->isNeutralUnit(m_pPlayer->GetID(),true,true))
				{
					for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
					{
						CvUnit* pUnit = pLoopPlot->getUnitByIndex(iUnitLoop);

						//barbarians do not attack civilians before the first city was founded.
						if (!m_pPlayer->isBarbarian() || GET_PLAYER(pUnit->getOwner()).GetNumCitiesFounded() > 0)
						{
							newTarget.SetTargetType(IsHighPriorityCivilianTarget(&newTarget) ? AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN : AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN);
							newTarget.SetUnitPtr(pUnit);
							newTarget.SetAuxIntData(25);
							m_AllTargets.push_back(newTarget);
						}
					}
				}

				// ... barbarian camp? doesn't matter if it has a unit inside
				// note that minors ignore barb camps, cannot clear them anyway
				if (bSuspectedBarbCamp && (m_pPlayer->isMajorCiv() || m_pPlayer->isBarbarian()))
				{
					int iBaseScore = pLoopPlot->isVisible(m_pPlayer->getTeam()) ? 100 : 50;
					if (bImminentAttack)
						iBaseScore = max(10, iBaseScore / 2);
					newTarget.SetTargetType(AI_TACTICAL_TARGET_BARBARIAN_CAMP);
					newTarget.SetAuxIntData(iBaseScore - m_pPlayer->GetCityDistancePathLength(pLoopPlot));
					m_AllTargets.push_back(newTarget);
				}

				// ... unpopped goody hut? (ancient ruins)
				if(!m_pPlayer->isMinorCiv() && pLoopPlot->isRevealedGoody(m_pPlayer->getTeam()))
				{
					int iBaseScore = pLoopPlot->isVisible(m_pPlayer->getTeam()) ? 100 : 50;
					if (bImminentAttack)
						iBaseScore = max(10, iBaseScore / 2);
					newTarget.SetTargetType(AI_TACTICAL_TARGET_GOODY);
					newTarget.SetAuxIntData(iBaseScore - m_pPlayer->GetCityDistancePathLength(pLoopPlot));
					m_AllTargets.push_back(newTarget);
				}

				// Or citadels (for pillaging!)
				if (atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()) != NO_IMPROVEMENT &&
					GC.getImprovementInfo(pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()))->GetNearbyEnemyDamage() > /*10 in CP, 5 in VP*/ GD_INT_GET(ENEMY_HEAL_RATE) &&
					!pLoopPlot->IsImprovementPillaged())
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_ENEMY_CITADEL);
					newTarget.SetAuxIntData(bImminentAttack ? 60 : 80);
					m_AllTargets.push_back(newTarget);
				}
				
				// Or forts/defensive improvements with high defense modifier (for pillaging!)
				// This catches forts and similar defensive structures that don't deal damage
				ImprovementTypes eRevealedImprovement = pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam());
				if (atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					eRevealedImprovement != NO_IMPROVEMENT &&
					!pLoopPlot->IsImprovementPillaged())
				{
					CvImprovementEntry* pkImprovementInfo = GC.getImprovementInfo(eRevealedImprovement);
					// High-defense improvements (forts, etc.) that aren't already tagged as citadels
					if (pkImprovementInfo && pkImprovementInfo->GetDefenseModifier() >= 25 &&
						pkImprovementInfo->GetNearbyEnemyDamage() <= GD_INT_GET(ENEMY_HEAL_RATE))
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_ENEMY_CITADEL);
						// Priority based on defense strength - higher defense = higher priority to pillage
						int iScore = 40 + pkImprovementInfo->GetDefenseModifier() / 2;
						if (bImminentAttack)
							iScore = max(10, iScore - 15);
						newTarget.SetAuxIntData(iScore);
						m_AllTargets.push_back(newTarget);
					}
				}

				// ... enemy improvement?
				if (atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					pLoopPlot->getRevealedImprovementType(m_pPlayer->getTeam()) != NO_IMPROVEMENT &&
					!pLoopPlot->IsImprovementPillaged())
				{
					ResourceTypes eResource = pLoopPlot->getResourceType(m_pPlayer->getTeam());
					int iExtraScore = 0;
					//does this make a difference in the end?
					if (m_pPlayer->isBarbarian() || m_pPlayer->GetPlayerTraits()->IsWarmonger())
						iExtraScore = 20;

					ResourceUsageTypes eResourceUsage = (eResource != NO_RESOURCE) ? GC.getResourceInfo(eResource)->getResourceUsage() : RESOURCEUSAGE_BONUS;
					if (eResourceUsage == RESOURCEUSAGE_STRATEGIC)
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE);
						int iScore = 80 + iExtraScore;
						if (bImminentAttack)
							iScore = max(10, iScore - 20);
						newTarget.SetAuxIntData(iScore);
					}
					else if (eResourceUsage == RESOURCEUSAGE_LUXURY)
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE);
						int iScore = 40 + iExtraScore;
						if (bImminentAttack)
							iScore = max(10, iScore - 15);
						newTarget.SetAuxIntData(iScore);
					}
					else
					{
						newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT);
						int iScore = 5 + iExtraScore;
						if (bImminentAttack)
							iScore = max(1, iScore - 5);
						newTarget.SetAuxIntData(iScore);
					}

					m_AllTargets.push_back(newTarget);
				}

				// ... enemy trade route? (city connection - not caravan)
				// checking for city connection is not enough, some people (iroquois) don't need roads, so there isn't anything to pillage 
				if (atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()) &&
					pLoopPlot->getRevealedRouteType(m_pPlayer->getTeam()) != NO_ROUTE && 
					!pLoopPlot->IsRoutePillaged() && pLoopPlot->IsCityConnection() &&
					!GetTacticalAnalysisMap()->IsInEnemyDominatedZone(pLoopPlot))
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT);
					newTarget.SetAuxIntData(bImminentAttack ? 5 : 10);
					m_AllTargets.push_back(newTarget);
				}

				// ... enemy trade unit
				if (pLoopPlot->isVisible(m_pPlayer->getTeam()) && pPlayerTrade->ContainsEnemyTradeUnit(pLoopPlot))
				{
					newTarget.SetTargetType( pLoopPlot->isWater() ? AI_TACTICAL_TARGET_TRADE_UNIT_SEA : AI_TACTICAL_TARGET_TRADE_UNIT_LAND);
					newTarget.SetAuxIntData(bImminentAttack ? 20 : 35);
					m_AllTargets.push_back(newTarget);
				}

				// ... defensive bastion?
				if (m_pPlayer->GetID() == pLoopPlot->getOwner() && pLoopPlot->getDomain()==DOMAIN_LAND &&
					(pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false) >= 30 || pLoopPlot->IsChokePoint()) &&
					(!vUnfriendlyMajors.empty() && pLoopPlot->IsBorderLand(m_pPlayer->GetID(), vUnfriendlyMajors))
					)
				{
					newTarget.SetTargetType(AI_TACTICAL_TARGET_DEFENSIVE_BASTION);
					int iValue = pLoopPlot->defenseModifier(m_pPlayer->getTeam(), false, false);
					if (pLoopPlot->IsChokePoint())
						iValue *= 3;

					newTarget.SetAuxIntData(iValue);
					m_AllTargets.push_back(newTarget);
				}

				// ... friendly strategic resource improvement?
				if (m_pPlayer->GetID() == pLoopPlot->getOwner() &&
					pLoopPlot->getResourceType() != NO_RESOURCE && 
					pLoopPlot->getImprovementType() != NO_IMPROVEMENT && !pLoopPlot->IsImprovementPillaged())
				{
					if (pLoopPlot->getOwningCity() != NULL && !vUnfriendlyMajors.empty() && pLoopPlot->getOwningCity()->isBorderCity(vUnfriendlyMajors))
					{
						CvResourceInfo* pkResourceInfo = GC.getResourceInfo(pLoopPlot->getResourceType());
						if (pkResourceInfo && pkResourceInfo->getResourceUsage() == RESOURCEUSAGE_STRATEGIC)
						{
							newTarget.SetTargetType(AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND);
							newTarget.SetAuxIntData(1);
							m_AllTargets.push_back(newTarget);
						}
					}
				}

				//Enemy water plots?
				if (pLoopPlot->isRevealed(m_pPlayer->getTeam()) && pLoopPlot->isWater() && atWar(m_pPlayer->getTeam(), pLoopPlot->getTeam()))
				{
					CvCity* pOwningCity = pLoopPlot->getOwningCity();
					if (pOwningCity != NULL && pLoopPlot->isValidMovePlot(m_pPlayer->GetID(),true))
					{
						int iDistance = GET_PLAYER(pOwningCity->getOwner()).GetCityDistanceInPlots(pLoopPlot);
						//we want to stay for a while, so stay out of danger as far as possible
						if (iDistance < 4 && m_pPlayer->GetPossibleAttackers(*pLoopPlot,NO_TEAM).empty())
						{
							//try to stay away from land
							int iWeight = pLoopPlot->GetSeaBlockadeScore(m_pPlayer->GetID());

							//prefer close targets
							iWeight = max(1, iWeight - m_pPlayer->GetCityDistancePathLength(pLoopPlot));

							//try to support the troops
							if (pOwningCity->getDamage() > 0 || pOwningCity->isUnderSiege())
								iWeight *= 2;

							if (iWeight > 0)
							{
								newTarget.SetTargetType(AI_TACTICAL_TARGET_BLOCKADE_POINT);
								newTarget.SetAuxIntData(iWeight);
								m_NavalBlockadePoints.push_back(newTarget);
							}
						}
					}
				}
			}
		}
	}

	// POST-PROCESSING ON TARGETS

	// make sure high prio units have the higher scores
	UpdateTargetScores();

	//Let's clean up our naval target list.
	PrioritizeNavalTargetsAndAddToMainList();

	// since the combat simulation considers a whole area, we don't need to attack for individual units
	SortTargetListAndDropUselessTargets();

	if (GC.getLogging() && GC.getAILogging())
		DumpTacticalTargets();
}

/// Don't allow adjacent tiles to both be sentry points
void CvTacticalAI::PrioritizeNavalTargetsAndAddToMainList()
{
	// First, sort the sentry points by priority
	std::stable_sort(m_NavalBlockadePoints.begin(), m_NavalBlockadePoints.end());
	CvTacticalTarget newTarget;
	// Loop through all points in copy
	for (unsigned int iI = 0; iI < m_NavalBlockadePoints.size(); iI++)
	{
		// Is the target of an appropriate type?
		CvPlot* pPlot = GC.getMap().plot(m_NavalBlockadePoints[iI].GetTargetX(), m_NavalBlockadePoints[iI].GetTargetY());
		if (pPlot != NULL)
		{
			//Only keep top 10 targets.
			if (pPlot->getImprovementType() != NO_IMPROVEMENT || iI < 10)
			{
				newTarget.SetTargetType(AI_TACTICAL_TARGET_BLOCKADE_POINT);
				newTarget.SetTargetX(pPlot->getX());
				newTarget.SetTargetY(pPlot->getY());
				newTarget.SetDominanceZone(GetTacticalAnalysisMap()->GetDominanceZoneID(pPlot->GetPlotIndex()));
				newTarget.SetAuxIntData(m_NavalBlockadePoints[iI].GetAuxIntData());
				m_AllTargets.push_back(newTarget);
			}
		}
	}
	m_NavalBlockadePoints.clear();
}

/// Issue 4.2: Assess if unit should retreat based on losses sustained
static bool IsMemoryAttackImminentForPlayer(const CvPlayer* pPlayer)
{
	if (!pPlayer || !pPlayer->isMajorCiv())
		return false;

	CvDiplomacyAI* pDiploAI = pPlayer->GetDiplomacyAI();
	if (!pDiploAI)
		return false;

	PlayerTypes ePlayer = pPlayer->GetID();
	for (int iPlayer = 0; iPlayer < MAX_MAJOR_CIVS; iPlayer++)
	{
		PlayerTypes eOther = (PlayerTypes)iPlayer;
		if (eOther == ePlayer || !GET_PLAYER(eOther).isAlive())
			continue;

		if (GET_PLAYER(eOther).GetProximityToPlayer(ePlayer) < PLAYER_PROXIMITY_CLOSE)
			continue;

		if (pDiploAI->IsAttackLikelyImminent(eOther))
			return true;
	}

	return false;
}

static void GetCoastalApproachCounts(const CvCity* pCity, int& iLandApproaches, int& iWaterApproaches)
{
	iLandApproaches = 0;
	iWaterApproaches = 0;

	if (!pCity)
		return;

	for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
	{
		CvPlot* pAdjacentPlot = plotDirection(pCity->getX(), pCity->getY(), (DirectionTypes)iDir);
		if (!pAdjacentPlot)
			continue;

		if (pAdjacentPlot->isWater())
			iWaterApproaches++;
		else if (!pAdjacentPlot->isImpassable(pCity->getTeam()))
			iLandApproaches++;
	}
}

static bool IsNavyLedCoastalAssaultTarget(const CvCity* pCity, bool& bIslandCity, bool& bNavalDominatedCity, int& iLandApproaches, int& iWaterApproaches)
{
	bIslandCity = false;
	bNavalDominatedCity = false;
	iLandApproaches = 0;
	iWaterApproaches = 0;

	if (!pCity || !pCity->isCoastal())
		return false;

	GetCoastalApproachCounts(pCity, iLandApproaches, iWaterApproaches);
	bIslandCity = (iLandApproaches == 0);
	bNavalDominatedCity = (iWaterApproaches > iLandApproaches * 2);

	return bIslandCity || bNavalDominatedCity;
}

static bool CanReachLandAttackPositionWithoutEmbark(const CvUnit* pUnit, const CvCity* pCity, bool bAllowOneTurnSetup)
{
	if (!pUnit || !pCity || pUnit->getDomainType() != DOMAIN_LAND)
		return false;

	const int iMaxTurns = bAllowOneTurnSetup ? 1 : 0;
	const int iMoveFlags = CvUnit::MOVEFLAG_NO_EMBARK | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF;
	CvPlot* pCityPlot = pCity->plot();
	CvUnit* pMutableUnit = const_cast<CvUnit*>(pUnit);

	if (pUnit->IsCanAttackRanged())
	{
		if (pUnit->canRangeStrikeAt(pCity->getX(), pCity->getY()))
			return true;

		for (int iRange = pUnit->GetRange(); iRange > 0; iRange--)
		{
			std::vector<CvPlot*> vPlots = TacticalAIHelpers::GetPlotsForRangedAttack(pCityPlot, pUnit, iRange, false);
			for (size_t i = 0; i < vPlots.size(); i++)
			{
				CvPlot* pAttackPlot = vPlots[i];
				if (!pAttackPlot || pAttackPlot->isWater())
					continue;

				if (pMutableUnit->TurnsToReachTarget(pAttackPlot, iMoveFlags, iMaxTurns) <= iMaxTurns)
					return true;
			}
		}
	}
	else
	{
		for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
		{
			CvPlot* pAdjacentPlot = plotDirection(pCity->getX(), pCity->getY(), (DirectionTypes)iDir);
			if (!pAdjacentPlot || pAdjacentPlot->isWater() || pAdjacentPlot->isImpassable(pUnit->getTeam()))
				continue;

			if (pMutableUnit->TurnsToReachTarget(pAdjacentPlot, iMoveFlags, iMaxTurns) <= iMaxTurns)
				return true;
		}
	}

	return false;
}

static bool ShouldSkipLandUnitForCoastalCapture(const CvUnit* pUnit, const CvCity* pCity, bool bPreferNavalCapture, bool bLandAssaultCooldownActive)
{
	if (!pUnit || !pCity || pUnit->getDomainType() != DOMAIN_LAND || !pCity->isCoastal())
		return false;

	if (bLandAssaultCooldownActive && !CanReachLandAttackPositionWithoutEmbark(pUnit, pCity, true))
		return true;

	if (CanReachLandAttackPositionWithoutEmbark(pUnit, pCity, true))
		return false;

	const bool bCrossWaterExposure = pUnit->isEmbarked() || pUnit->plot()->isWater() || !CanReachLandAttackPositionWithoutEmbark(pUnit, pCity, false);
	if (!bCrossWaterExposure)
		return false;

	if (!bPreferNavalCapture && !pUnit->isEmbarked() && !pUnit->plot()->isWater())
		return false;

	if (pUnit->IsCanAttackRanged() || pUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
		return true;

	if (pUnit->isAmphibious())
		return false;

	const int iCityHPPercent = ((pCity->GetMaxHitPoints() - pCity->getDamage()) * 100) / max(1, pCity->GetMaxHitPoints());
	return iCityHPPercent > 15;
}

static bool IsTrustedCoalitionPartner(const CvPlayer* pPlayer, PlayerTypes eOtherPlayer, PlayerTypes eTargetOwner)
{
	if (!pPlayer || eOtherPlayer == NO_PLAYER || eTargetOwner == NO_PLAYER)
		return false;

	CvDiplomacyAI* pDiploAI = pPlayer->GetDiplomacyAI();
	if (!pDiploAI)
		return false;

	if (pDiploAI->GetCoopWarState(eOtherPlayer, eTargetOwner) >= COOP_WAR_STATE_PREPARING)
		return true;

	if (pDiploAI->IsFriendOrAlly(eOtherPlayer) || pDiploAI->IsDoFAccepted(eOtherPlayer))
		return true;

	return false;
}

static int GetCoalitionOutcomeWeight(const CvPlayer* pPlayer, PlayerTypes eOtherPlayer, const CvCity* pCity)
{
	if (!pPlayer || !pCity || eOtherPlayer == NO_PLAYER)
		return 0;

	PlayerTypes eTargetOwner = pCity->getOwner();
	if (!IsCoalitionRelevantCityTargetOwner(eTargetOwner))
		return 0;

	CvDiplomacyAI* pOurDiplo = pPlayer->GetDiplomacyAI();
	CvDiplomacyAI* pTheirDiplo = GET_PLAYER(eOtherPlayer).GetDiplomacyAI();
	if (!pOurDiplo || !pTheirDiplo)
		return 0;

	CvCity* pMutableCity = const_cast<CvCity*>(pCity);
	PlayerTypes eLiberationTarget = GET_PLAYER(eOtherPlayer).GetPlayerToLiberate(pMutableCity);
	CoalitionLiberationCase eLiberationCase = GetCoalitionLiberationCase(pCity, eLiberationTarget);
	if (eLiberationTarget != NO_PLAYER && pTheirDiplo->IsTryingToLiberate(pMutableCity))
	{
		int iWeight = 1;
		switch (eLiberationCase)
		{
		case COALITION_LIBERATION_CITY_STATE:
			iWeight = 4;
			break;
		case COALITION_LIBERATION_DEAD_MAJOR:
			iWeight = 5;
			break;
		case COALITION_LIBERATION_ORIGINAL_CAPITAL:
			iWeight = 3;
			break;
		case COALITION_LIBERATION_OTHER_MAJOR:
			iWeight = 2;
			break;
		default:
			break;
		}

		if (GET_PLAYER(eLiberationTarget).getTeam() == pPlayer->getTeam())
			iWeight += 1;

		if (IsTrustedCoalitionPartner(pPlayer, eOtherPlayer, eTargetOwner))
			iWeight += 1;

		return iWeight;
	}

	int iWeight = IsTrustedCoalitionPartner(pPlayer, eOtherPlayer, eTargetOwner) ? 1 : -1;
	switch (eLiberationCase)
	{
	case COALITION_LIBERATION_CITY_STATE:
		iWeight -= 3;
		break;
	case COALITION_LIBERATION_DEAD_MAJOR:
		iWeight -= 4;
		break;
	case COALITION_LIBERATION_ORIGINAL_CAPITAL:
		iWeight -= 2;
		break;
	case COALITION_LIBERATION_OTHER_MAJOR:
		iWeight -= 1;
		break;
	default:
		break;
	}

	if (GET_PLAYER(eOtherPlayer).GetPlayerTraits()->IsNoAnnexing())
		iWeight += 1;

	if (pTheirDiplo->IsGoingForWorldConquest() || pTheirDiplo->IsCloseToWorldConquest() || pOurDiplo->GetWarmongerThreat(eOtherPlayer) >= THREAT_MAJOR)
		iWeight -= 2;

	if (GET_PLAYER(eOtherPlayer).GetEconomicMight() > pPlayer->GetEconomicMight() * 11 / 10)
		iWeight -= 1;

	return iWeight;
}

static void GetCoalitionPressureNearCity(const CvPlayer* pPlayer, const CvCity* pCity, int& iHelpfulPressure, int& iHarmfulPressure)
{
	iHelpfulPressure = 0;
	iHarmfulPressure = 0;

	if (!pPlayer || !pCity)
		return;

	PlayerTypes eTargetOwner = pCity->getOwner();
	if (!IsCoalitionRelevantCityTargetOwner(eTargetOwner))
		return;

	for (int iPlotLoop = 0; iPlotLoop < RING3_PLOTS; iPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pCity->plot(), iPlotLoop);
		if (!pLoopPlot)
			continue;

		for (int iUnit = 0; iUnit < pLoopPlot->getNumUnits(); iUnit++)
		{
			CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnit);
			if (!pLoopUnit || pLoopUnit->isDelayedDeath())
				continue;

			PlayerTypes eOtherPlayer = pLoopUnit->getOwner();
			if (eOtherPlayer == pPlayer->GetID() || eOtherPlayer == eTargetOwner)
				continue;

			if (!GET_PLAYER(eOtherPlayer).isMajorCiv() || !GET_PLAYER(eOtherPlayer).isAlive())
				continue;

			if (!GET_PLAYER(eOtherPlayer).IsAtWarWith(eTargetOwner) || GET_PLAYER(eOtherPlayer).IsAtWarWith(pPlayer->GetID()))
				continue;

			if (!(pLoopUnit->IsCombatUnit() || pLoopUnit->IsCanAttackRanged()))
				continue;

			int iUnitPressure = pLoopUnit->IsCanAttackRanged() ? 1 : 2;
			if (plotDistance(*pLoopPlot, *pCity->plot()) <= 1)
				iUnitPressure++;

			if (GetCoalitionOutcomeWeight(pPlayer, eOtherPlayer, pCity) > 0)
				iHelpfulPressure += iUnitPressure;
			else
				iHarmfulPressure += iUnitPressure;
		}
	}
}

static bool ShouldAvoidRiskyCoalitionCapture(const CvPlayer* pPlayer, const CvCity* pCity, bool bCaptureOpportunityThisTurn, int iOurMeleeCount, int iHelpfulPressure, int iHarmfulPressure)
{
	if (!pPlayer || !pCity || !bCaptureOpportunityThisTurn)
		return false;

	if (!IsCoalitionRelevantCityTargetOwner(pCity->getOwner()))
		return false;

	if (GET_PLAYER(pCity->getOwner()).isMajorCiv() && (pCity->IsOriginalCapital() || pCity->getOriginalOwner() == pPlayer->GetID()))
		return false;

	if (pCity->getDamage() * 2 < pCity->GetMaxHitPoints())
		return false;

	const bool bMinorTarget = GET_PLAYER(pCity->getOwner()).isMinorCiv();
	const int iHarmfulPressureThreshold = bMinorTarget ? 4 : 3;
	const int iHelpfulPressureThreshold = bMinorTarget ? 5 : 4;

	if (iHarmfulPressure >= iHarmfulPressureThreshold)
		return true;

	if (iHelpfulPressure >= iHelpfulPressureThreshold && iHarmfulPressure > 0 && iOurMeleeCount <= 1)
		return true;

	return false;
}

static bool ShouldDeferCaptureToCoalitionPartner(const CvPlayer* pPlayer, const CvCity* pCity, bool bCaptureOpportunityThisTurn, int iHelpfulPressure, int iHarmfulPressure)
{
	if (!pPlayer || !pCity || !bCaptureOpportunityThisTurn)
		return false;

	if (ShouldPreferSelfLiberationCapture(pPlayer, pCity))
		return false;

	if (!IsSelfCaptureBurdensome(pPlayer, pCity))
		return false;

	const bool bMinorTarget = GET_PLAYER(pCity->getOwner()).isMinorCiv();
	const int iHelpfulPressureThreshold = bMinorTarget ? 3 : 2;
	return iHelpfulPressure >= iHelpfulPressureThreshold && iHelpfulPressure > iHarmfulPressure;
}

bool CvTacticalAI::IsCoastalAssaultLandCooldownActive(const CvCity* pCity) const
{
	if (!pCity)
		return false;

	std::map<int, int>::const_iterator it = m_coastalLandAssaultCooldownUntilTurn.find(pCity->plot()->GetPlotIndex());
	if (it == m_coastalLandAssaultCooldownUntilTurn.end())
		return false;

	return it->second >= GC.getGame().getGameTurn();
}

void CvTacticalAI::SetCoastalAssaultLandCooldown(const CvCity* pCity, int iCooldownTurns, const char* szReason)
{
	if (!pCity || iCooldownTurns <= 0)
		return;

	const int iExpiryTurn = GC.getGame().getGameTurn() + iCooldownTurns;
	m_coastalLandAssaultCooldownUntilTurn[pCity->plot()->GetPlotIndex()] = iExpiryTurn;

	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strLogString;
		strLogString.Format("Coastal land assault cooldown set for %s until turn %d%s%s",
			pCity->getNameNoSpace().c_str(), iExpiryTurn,
			szReason ? ": " : "",
			szReason ? szReason : "");
		LogTacticalMessage(strLogString);
	}
}

void CvTacticalAI::ExpireCoastalAssaultLandCooldowns()
{
	const int iCurrentTurn = GC.getGame().getGameTurn();
	for (std::map<int, int>::iterator it = m_coastalLandAssaultCooldownUntilTurn.begin(); it != m_coastalLandAssaultCooldownUntilTurn.end(); )
	{
		if (it->second < iCurrentTurn)
			m_coastalLandAssaultCooldownUntilTurn.erase(it++);
		else
			++it;
	}
}

bool CvTacticalAI::ShouldRetreatDueToLosses(const vector<CvUnit*>& vUnits)
{
	if(vUnits.empty())
		return false;
	
	const int RETREAT_DAMAGE_THRESHOLD = 20; // Retreat if losing > 20% of army
	const int iRetreatDamageThreshold = m_bImminentAttack ? 15 : RETREAT_DAMAGE_THRESHOLD;
	
	int iTotalHealth = 0;
	int iTotalMaxHealth = 0;
	
	for(size_t i = 0; i < vUnits.size(); i++)
	{
		const CvUnit* pUnit = vUnits[i];
		if(!pUnit) continue;
		iTotalHealth += pUnit->GetCurrHitPoints();
		iTotalMaxHealth += pUnit->GetMaxHitPoints();
	}
	
	if(iTotalMaxHealth == 0)
		return false;
	
	int iHealthPercent = (iTotalHealth * 100) / iTotalMaxHealth;
	
	// If army has lost > 20% health and no allies nearby, retreat (Issue 4.2)
	if(iHealthPercent < (100 - iRetreatDamageThreshold))
	{
		// Check if allies nearby can support (Issue 4.2: army coordination)
		int iAlliedSupport = 0;
		for(size_t i = 0; i < vUnits.size(); i++)
		{
			const CvUnit* pUnit = vUnits[i];
			if(!pUnit) continue;
			iAlliedSupport += FindNearbyAlliedUnits(const_cast<CvUnit*>(pUnit), 5, pUnit->getDomainType());
		}
		
		if(iAlliedSupport == 0)
		{
			return true; // Retreat
		}
	}
	
	return false;
}

/// Issue 4.2: Find nearby allied units that can provide support
int CvTacticalAI::FindNearbyAlliedUnits(CvUnit* pUnit, int iMaxDistance, DomainTypes eDomain)
{
	if(!pUnit) return 0;
	
	int iAlliedCount = 0;
	CvPlot* pPlot = pUnit->plot();
	if(!pPlot) return 0;
	
	TeamTypes eTeam = pUnit->getTeam();
	
	int iPlotX = pPlot->getX();
	int iPlotY = pPlot->getY();
	
	// Search nearby plots for allied units (Issue 4.2: army coordination)
	for(int iDX = -iMaxDistance; iDX <= iMaxDistance; iDX++)
	{
		for(int iDY = -iMaxDistance; iDY <= iMaxDistance; iDY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(iPlotX, iPlotY, iDX, iDY, iMaxDistance);
			if(!pLoopPlot) continue;
			
			if(plotDistance(iPlotX, iPlotY, pLoopPlot->getX(), pLoopPlot->getY()) > iMaxDistance)
				continue;
			
			for(int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
			{
				CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
				if(!pLoopUnit) continue;
				if(pLoopUnit == pUnit) continue; // don't count self
				if(pLoopUnit->getDomainType() != eDomain) continue;
				if(pLoopUnit->isDelayedDeath()) continue;

				TeamTypes eLoopTeam = pLoopUnit->getTeam();
				if (GET_TEAM(eLoopTeam).isAtWar(eTeam))
					continue;

				bool bAllied = false;

				// Same team always qualifies
				if (eLoopTeam == eTeam)
				{
					bAllied = true;
				}
				// Defensive-pact partner counts as support
				else if (GET_TEAM(eLoopTeam).IsHasDefensivePact(eTeam))
				{
					bAllied = true;
				}
				// City-state ally of the owner counts as support
				else if (GET_PLAYER(pLoopUnit->getOwner()).isMinorCiv())
				{
					CvMinorCivAI* pMinorAI = GET_PLAYER(pLoopUnit->getOwner()).GetMinorCivAI();
					if (pMinorAI && pMinorAI->IsAllies(pUnit->getOwner()))
						bAllied = true;
				}

				if(!bAllied)
					continue;
				
				// Count units that can fight (not civilians)
				if(!pLoopUnit->IsCombatUnit())
					continue;
				
				iAlliedCount++;
			}
		}
	}
	
	return iAlliedCount;
}

/// Issue 4.2: Find opportunity for coordinated attack with nearby allies
bool CvTacticalAI::FindCoordinatedAttackOpportunity(CvPlot* pTargetPlot, const vector<CvUnit*>& vAlliedUnits)
{
	if(!pTargetPlot || vAlliedUnits.empty())
		return false;
	
	const int COORDINATION_RANGE = 6; // Units within 6 tiles can coordinate
	
	// Count friendly units that can reach target
	int iFriendlyNearby = 0;
	for(size_t i = 0; i < vAlliedUnits.size(); i++)
	{
		const CvUnit* pUnit = vAlliedUnits[i];
		if(!pUnit) continue;
		
		int iDistance = plotDistance(pUnit->getX(), pUnit->getY(), pTargetPlot->getX(), pTargetPlot->getY());
		if(iDistance <= COORDINATION_RANGE && pUnit->canMoveInto(*pTargetPlot))
		{
			iFriendlyNearby++;
		}
	}
	
	// If multiple units can coordinate, proceed with attack (Issue 4.2: multi-unit planning)
	return (iFriendlyNearby >= 2);
}

void CvTacticalAI::ProcessDominanceZones()
{
	// Barbarian processing is straightforward -- just one big list of priorites and everything is considered at once
	if(m_pPlayer->isBarbarian())
	{
		ExtractTargetsForZone(NULL);
		AssignBarbarianMoves();
	}
	else
	{
		//high prio goes first
		AssignGlobalHighPrioMoves();

		//then confront the enemy in each tactical zone
		for(int iI = 0; iI < GetTacticalAnalysisMap()->GetNumZones(); iI++)
		{
			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByIndex(iI);

			PlotEmergencyPurchases(pZone);

			int iTargets = ExtractTargetsForZone(pZone);
			if (iTargets==0)
				continue;

			// If our presence in this zone has taken heavy damage and no nearby support exists, fall back early
			if (pZone->GetPosture() != TACTICAL_POSTURE_WITHDRAW)
			{
				vector<CvUnit*> vZoneUnits;
				for(list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
				{
					CvUnit* pZoneUnit = m_pPlayer->getUnit(*it);
					if(!pZoneUnit || !pZoneUnit->canUseForTacticalAI())
						continue;

					CvTacticalDominanceZone* pUnitZone = GetTacticalAnalysisMap()->GetZoneByPlot(pZoneUnit->plot());
					if (pUnitZone == pZone)
						vZoneUnits.push_back(pZoneUnit);
				}

				if (ShouldRetreatDueToLosses(vZoneUnits))
				{
					PlotWithdrawMoves(pZone);
					continue;
				}
			}

			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				CvCity* pZoneCity = pZone->GetZoneCity();
				strLogString.Format("Zone %d, %s, city of %s, posture %s, %d targets",  
					pZone ? pZone->GetZoneID() : -1, pZone->IsWater() ? "water" : "land",
					pZoneCity ? pZoneCity->getNameNoSpace().c_str() : "none", 
					postureNames[pZone->GetPosture()], iTargets);
				LogTacticalMessage(strLogString);
			}

			switch (pZone->GetPosture())
			{
			case TACTICAL_POSTURE_NONE:
				break; //no posture assigned so do nothing; TODO: Maybe this should be unreachable?
			case TACTICAL_POSTURE_WITHDRAW: //give up
				PlotWithdrawMoves(pZone);
				break;
			case TACTICAL_POSTURE_HEDGEHOG: //defend
				PlotHedgehogMoves(pZone);
				break;
			case TACTICAL_POSTURE_ATTRITION: //low risk attacks on units
				PlotAttritionAttacks(pZone);
				break;
			case TACTICAL_POSTURE_EXPLOIT_FLANKS: //try to kill enemy units
				PlotExploitFlanksMoves(pZone);
				break;
			case TACTICAL_POSTURE_STEAMROLL: //attack everything including cities
				PlotSteamrollMoves(pZone);
				break;
			case TACTICAL_POSTURE_SURGICAL_CITY_STRIKE: //go for the city first
				PlotSurgicalCityStrikeMoves(pZone);
				break;
			case TACTICAL_POSTURE_COUNTERATTACK: //concentrated fire on enemy units
				PlotCounterattackMoves(pZone);
				break;
			}
		}

		//second pass: bring in reinforcements
		for (int iI = 0; iI < GetTacticalAnalysisMap()->GetNumZones(); iI++)
			PlotReinforcementMoves(GetTacticalAnalysisMap()->GetZoneByIndex(iI));

		//now mid prio moves like capturing barb camps, pillaging
		AssignGlobalMidPrioMoves();

		//finally arrange our remaining idle units for defense
		AssignGlobalLowPrioMoves();
	}

	//failsafe
	ReviewUnassignedUnits();
}

/// Choose which tactics to run and assign units to it
void CvTacticalAI::AssignGlobalHighPrioMoves()
{
	ExtractTargetsForZone(NULL);

	//make some space near the frontline
	PlotHealMoves(true);
	//move armies first
	PlotOperationalArmyMoves();

	//garrisons sometimes make a sortie so we have to get them back
	PlotGarrisonMoves(2);
}

/// Choose which tactics to run and assign units to it
void CvTacticalAI::AssignGlobalMidPrioMoves()
{
	ExtractTargetsForZone(NULL);

	//air sweeps / attacks are already done during zone attacks, this is just for the remaining units
	PlotAirPatrolMoves();

	//score some goodies
	PlotGrabGoodyMoves();
	PlotCivilianAttackMoves();

	//make sure our frontline fortresses are occupied
	PlotBastionMoves(2);

	//now all attacks are done, try to move any unprocessed units out of harm's way
	PlotMovesToSafety(true);

	//try again now that other blocking units might have moved
	PlotHealMoves(false);

	//harass the enemy (plundering also happens during combat sim ...)
	PlotPillageMoves(AI_TACTICAL_TARGET_ENEMY_CITADEL, true);
	PlotPlunderTradeUnitMoves(DOMAIN_LAND);
	PlotPlunderTradeUnitMoves(DOMAIN_SEA);
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, true);
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT, true);
	PlotPillageMoves(AI_TACTICAL_TARGET_ENEMY_CITADEL, false);
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, false);
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT, false);
	PlotBlockadeMoves();
	
	// Counter-blockade: actively hunt enemy naval units blockading our cities
	// This is critical because blockaded cities can't heal
	PlotCounterBlockadeMoves();
}

/// Choose which tactics to run and assign units to it
void CvTacticalAI::AssignGlobalLowPrioMoves()
{
	ExtractTargetsForZone(NULL);

	//defense preparation for next turn
	PlotGuardImprovementMoves(2);

	//do this last after the units in need have already moved
	PlotNavalEscortMoves();

	// C1 fix: Refresh pending transits (scan embarked units, assess risk) before convoy escort
	CvStrategicGeographyMap* pGeoMut = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	if (pGeoMut)
		pGeoMut->RefreshPendingTransits();

	// Convoy escort for high-risk inter-island transits
	PlotConvoyEscortMoves();

	// Phase I-6: Intercept enemy invasion convoys before they land
	PlotAntiInvasionMoves();

	// Position naval units defensively around threatened coastal cities
	PlotCoastalDefenseMoves();

	// Phase I-7: Station ships at identified strait chokepoints
	PlotStraitDefenseMoves();

	// Drift idle naval units toward patrol stations (island civ posture)
	PlotNavalPatrolStationMoves();

	//civilians move out of harms way last, when all potential defenders are set in place
	PlotMovesToSafety(false);
}

/// Choose which tactics to run and assign units to it (barbarian version)
void CvTacticalAI::AssignBarbarianMoves()
{
	//even barbarians like their camps
	PlotBarbarianCampDefense();

	//barbarians don't have tactical zones, they just attack everything that moves
	PlotBarbarianAttacks();
	PlotCivilianAttackMoves();

	//barbarians like to plunder as well
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE, true);
	PlotPillageMoves(AI_TACTICAL_TARGET_IMPROVEMENT, true);
	PlotPlunderTradeUnitMoves(DOMAIN_LAND);
	PlotPlunderTradeUnitMoves(DOMAIN_SEA);

	//normal roaming to find targets
	PlotBarbarianRoaming();

	//safety comes last for the barbarians ...
	PlotMovesToSafety(true /*bCombatUnits*/);
	PlotMovesToSafety(false /*bCombatUnits*/);
}

/// Assign a group of units to take down each city we can capture
void CvTacticalAI::ExecuteCaptureCityMoves()
{
	// See how many moves of this type we can execute
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_CITY, AL_HIGH); pTarget!=NULL; pTarget = GetNextZoneTarget(AL_HIGH))
	{
		//mark the target whether the attack happened or not - we won't have a better chance this turn
		pTarget->SetLastAggLevel(AL_HIGH);

		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if(pPlot != NULL && pPlot->isCity())
		{
			m_CurrentMoveCities.clear();
			CvCity* pCity = pPlot->getPlotCity();

			//first try the land zone
			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, false);

			//does it look good there?
			if (!pZone || (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !pCity->isInDangerOfFalling()))
			{
				//try again with the water zone
				pZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);

				if (!pZone || (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !pCity->isInDangerOfFalling()))
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Zone %d, City of %s, is in enemy dominated zone - won't try to capture, X: %d, Y: %d, ",
							pZone ? pZone->GetZoneID() : -1, pCity->getNameNoSpace().c_str(), pCity->getX(), pCity->getY());
						LogTacticalMessage(strLogString);
					}

					continue;
				}
			}

			// Always recruit both naval and land based forces if available!
			if(FindUnitsWithinStrikingDistance(pPlot))
			{
				const int iCityDamageBeforeAttack = pCity->getDamage();
				int iRequiredDamage = pCity->GetMaxHitPoints() - pCity->getDamage();
				int iExpectedDamagePerTurn = ComputeTotalExpectedDamage(*pTarget);
				// Dynamic siege threshold based on situation assessment
				int iMaxSiegeTurns = 10; // base value

				// Factor 1: Blockade status - if city is blockaded it can't heal, so we can take longer
				bool bCityBlockaded = pCity->IsBlockadedWaterAndLand();
				if (bCityBlockaded)
					iMaxSiegeTurns += 5;

				// Factor 2: Zone dominance - if we dominate we can be more patient
				if (pZone)
				{
					if (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY)
						iMaxSiegeTurns += 4;
					else if (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_EVEN)
						iMaxSiegeTurns += 2;
					// If enemy dominated but city is in danger of falling, still allow attack (handled later)
				}

				// Factor 3: Garrison strength - stronger garrison means more counterattack damage, so act faster
				if (pCity->HasGarrison())
					iMaxSiegeTurns -= 2;

				// Factor 4: City strength affects how cautious we should be
				int iCityStrength = pCity->getStrengthValue() / 100;
				if (iCityStrength > 50)
					iMaxSiegeTurns -= 2;
				else if (iCityStrength < 25)
					iMaxSiegeTurns += 2;

				// Clamp to reasonable bounds
				iMaxSiegeTurns = max(5, min(20, iMaxSiegeTurns));

				// Calculate effective healing and required damage - blockaded cities don't heal
				int iCityHealingPerTurn = 0;
				if (!bCityBlockaded)
				{
					iCityHealingPerTurn = /*20 in CP, 8 in VP*/ GD_INT_GET(CITY_HIT_POINTS_HEALED_PER_TURN);
					if (MOD_BALANCE_VP)
						iCityHealingPerTurn += pCity->getPopulation();

					CvUnit* pGarrison = pCity->GetGarrisonedUnit();
					if (pGarrison)
					{
						iCityHealingPerTurn += pGarrison->ActualHealRate(pPlot, false);
						iRequiredDamage += pGarrison->GetMaxHitPoints() - pGarrison->getDamage();
						// TODO, a city can now have two garrisoned units..
					}
				}

				//assume the city heals each turn (unless blockaded)
				if (iExpectedDamagePerTurn < iRequiredDamage && (iExpectedDamagePerTurn - iCityHealingPerTurn) * iMaxSiegeTurns < iRequiredDamage)
				{
					if (GC.getLogging() && GC.getAILogging() && pZone)
					{
						CvString strLogString;
						strLogString.Format("Zone %d, too early for attacking %s, required damage %d, expected dmg/turn %d, max siege turns %d, city %s, heal/turn %d", 
							pZone ? pZone->GetZoneID() : -1, pCity->getNameNoSpace().c_str(), iRequiredDamage, iExpectedDamagePerTurn,
							iMaxSiegeTurns, bCityBlockaded ? "blockaded" : "not blockaded", iCityHealingPerTurn);
						LogTacticalMessage(strLogString);
					}
					continue;
				}

				//see whether we have melee units for capturing and calculate combined melee damage potential
				int iMeleeCount = 0;
				int iTotalMeleeDamage = 0;
				int iRangedDamageThisTurn = 0;
				int iNavalMeleeCount = 0;
				int iLandMeleeCount = 0;
				
				// Track melee units that can reach the city and their expected damage
				for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
				{
					CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
					if (!pUnit || !pUnit->canMove())
						continue;

					// Are we a melee unit?
					if (!pUnit->IsCanAttackRanged())
					{
						iMeleeCount++;
						// Track naval vs land melee for island city detection
						if (pUnit->getDomainType() == DOMAIN_SEA)
							iNavalMeleeCount++;
						else if (pUnit->getDomainType() == DOMAIN_LAND)
							iLandMeleeCount++;
						// Calculate expected damage from this melee unit - use stored value if available
						int iExpectedDamage = m_CurrentMoveUnits[iI].GetExpectedTargetDamage();
						if (iExpectedDamage > 0)
							iTotalMeleeDamage += iExpectedDamage;
					}
					else
					{
						// Track ranged damage that will soften the city first
						int iExpectedDamage = m_CurrentMoveUnits[iI].GetExpectedTargetDamage();
						if (iExpectedDamage > 0)
							iRangedDamageThisTurn += iExpectedDamage;
					}
				}

				// Island city detection: determine if this city requires naval siege
				// Count land vs water approaches to the city
				int iLandApproaches = 0;
				int iWaterApproaches = 0;
				GetCoastalApproachCounts(pCity, iLandApproaches, iWaterApproaches);
				bool bIslandCity = (iLandApproaches == 0);
				bool bNavalDominatedCity = (iWaterApproaches > iLandApproaches * 2); // Water approaches are dominant
				bool bPreferNavalCapture = pCity->isCoastal() && (bIslandCity || bNavalDominatedCity);
				
				// Naval-only siege bonus: if city is primarily naval-accessible and we have naval melee,
				// boost the expected damage to encourage the siege
				if ((bIslandCity || bNavalDominatedCity) && iNavalMeleeCount > 0 && pCity->isCoastal())
				{
					// Island cities REQUIRE naval melee to capture - give significant damage bonus
					if (bIslandCity)
					{
						// Add bonus damage estimation since naval units are critical here
						iExpectedDamagePerTurn += iExpectedDamagePerTurn / 4; // +25% damage expectation
						iMaxSiegeTurns += 3; // More patience for naval-only siege
					}
					else if (bNavalDominatedCity)
					{
						// Naval-dominated cities benefit from naval siege
						iExpectedDamagePerTurn += iExpectedDamagePerTurn / 10; // +10% damage expectation
						iMaxSiegeTurns += 1;
					}
				}

				// Better capture timing: determine if this is a capture opportunity
				// City HP after ranged attacks this turn
				int iCityHPAfterRanged = max(1, iRequiredDamage - iRangedDamageThisTurn);
				bool bCaptureOpportunityThisTurn = (iTotalMeleeDamage >= iCityHPAfterRanged) && (iMeleeCount > 0);
				
				// If city is very low (<= 25% HP) or in danger of falling, also consider it a capture opportunity
				int iCityMaxHP = pCity->GetMaxHitPoints();
				int iCityCurrentHP = iCityMaxHP - pCity->getDamage();
				bool bCityNearDeath = (iCityCurrentHP <= iCityMaxHP / 4) || pCity->isInDangerOfFalling();
				
				if (bCityNearDeath && iMeleeCount > 0)
					bCaptureOpportunityThisTurn = true;

				bool bLikelyNavalCaptureNextTurn = false;
				if (bPreferNavalCapture && iNavalMeleeCount > 0 && !bCaptureOpportunityThisTurn)
				{
					int iNetExpectedDamagePerTurn = max(0, iExpectedDamagePerTurn - iCityHealingPerTurn);
					bLikelyNavalCaptureNextTurn = (iNetExpectedDamagePerTurn > 0) &&
						((iNetExpectedDamagePerTurn * 2 >= iRequiredDamage) || (iCityCurrentHP <= iNetExpectedDamagePerTurn + pCity->GetMaxHitPoints() / 6));
				}

				// Special case: City is at 0 HP with no garrison - try paradrop to capture!
				// Paratroopers can't attack after dropping, but they CAN capture undefended cities
				if (iRequiredDamage <= 0 && !pCity->HasGarrison())
				{
					CvPlot* pCityPlot = pCity->plot();
					if (pCityPlot && FindParatroopersWithinStrikingDistance(pCityPlot, true))
					{
						if (ExecuteParadropCityCapture(pCity))
						{
							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("Paratrooping to capture undefended city %s at 0 HP, X: %d, Y: %d",
									pCity->getNameNoSpace().c_str(), pCity->getX(), pCity->getY());
								LogTacticalMessage(strLogString);
							}
							continue; // City captured, move to next target
						}
					}
				}

				if (iMeleeCount == 0 && iRequiredDamage <= 1)
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Zone %d, no melee units to capture %s", pZone ? pZone->GetZoneID() : -1, pCity->getNameNoSpace().c_str());
						LogTacticalMessage(strLogString);
					}

					continue;
				}

				int iHelpfulCoalitionPressure = 0;
				int iHarmfulCoalitionPressure = 0;
				GetCoalitionPressureNearCity(m_pPlayer, pCity, iHelpfulCoalitionPressure, iHarmfulCoalitionPressure);
				const bool bAvoidRiskyCoalitionCapture = ShouldAvoidRiskyCoalitionCapture(m_pPlayer, pCity, bCaptureOpportunityThisTurn, iMeleeCount, iHelpfulCoalitionPressure, iHarmfulCoalitionPressure);
				const bool bDeferCaptureToCoalition = ShouldDeferCaptureToCoalitionPartner(m_pPlayer, pCity, bCaptureOpportunityThisTurn, iHelpfulCoalitionPressure, iHarmfulCoalitionPressure);

				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Zone %d, attempting capture of %s, required damage %d, expected dmg/turn %d, max siege turns %d, city %s, melee count %d (naval %d, land %d), melee dmg %d, ranged dmg %d, capture opportunity: %s, coalition pressure helpful=%d harmful=%d%s%s%s%s",
						pZone ? pZone->GetZoneID() : -1, pCity->getNameNoSpace().c_str(), iRequiredDamage, iExpectedDamagePerTurn,
						iMaxSiegeTurns, bCityBlockaded ? "blockaded" : "not blockaded",
						iMeleeCount, iNavalMeleeCount, iLandMeleeCount, iTotalMeleeDamage, iRangedDamageThisTurn,
						bCaptureOpportunityThisTurn ? "YES" : "no", iHelpfulCoalitionPressure, iHarmfulCoalitionPressure,
						bDeferCaptureToCoalition ? " [DEFER TO ALLY]" : "",
						bAvoidRiskyCoalitionCapture ? " [AVOID RISKY FINISHER]" : "",
						bIslandCity ? " [ISLAND CITY]" : "",
						bNavalDominatedCity ? " [NAVAL-DOMINATED]" : "");
					LogTacticalMessage(strLogString);
				}

				// Coordinated blockade targeting: if city is not blockaded and we can't capture this turn,
				// try to move units to blockade positions first to prevent city healing
				if (!bCityBlockaded && iRequiredDamage > iExpectedDamagePerTurn)
				{
					// Find unblocked plots adjacent to the city
					vector<CvPlot*> vBlockadePlots;
					for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
					{
						CvPlot* pAdjacentPlot = plotDirection(pCity->getX(), pCity->getY(), (DirectionTypes)iDir);
						if (!pAdjacentPlot)
							continue;

						// Skip impassable plots
						if (pAdjacentPlot->isImpassable(m_pPlayer->getTeam()))
							continue;

						// Check if this plot is currently unblocked (no enemy unit present/nearby to blockade it)
						if (!pAdjacentPlot->isBlockaded(pCity->getOwner()))
							vBlockadePlots.push_back(pAdjacentPlot);
					}

					// Try to move units to fill blockade positions
					if (!vBlockadePlots.empty())
					{
						int iBlockadesMade = 0;
						for (size_t iPlot = 0; iPlot < vBlockadePlots.size(); iPlot++)
						{
							CvPlot* pBlockadePlot = vBlockadePlots[iPlot];
							
							// Find a suitable unit that can move to this plot
							for (unsigned int iUnit = 0; iUnit < m_CurrentMoveUnits.size(); iUnit++)
							{
								CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iUnit].GetID());
								if (!pUnit || !pUnit->canMove() || pUnit->TurnProcessed())
									continue;

								// Skip ranged units - they should be attacking, not blocking
								// Also skip very damaged units
								if (pUnit->IsCanAttackRanged() || pUnit->GetCurrHitPoints() < pUnit->GetMaxHitPoints() / 3)
									continue;

								// Check if unit can enter the plot
								if (!pUnit->canMoveInto(*pBlockadePlot, CvUnit::MOVEFLAG_DESTINATION))
									continue;

								// Check if unit is in the right domain
								if (!pUnit->isNativeDomain(pBlockadePlot))
									continue;

								// Check if unit can reach the plot this turn
								if (pUnit->TurnsToReachTarget(pBlockadePlot, CvUnit::MOVEFLAG_IGNORE_DANGER, 0) > 0)
									continue;

								// Move the unit to establish blockade
								pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBlockadePlot->getX(), pBlockadePlot->getY(), 
									CvUnit::MOVEFLAG_IGNORE_DANGER, false, false, MISSIONAI_TACTMOVE);
								
								if (pUnit->plot() == pBlockadePlot)
								{
									pUnit->SetTurnProcessed(true);
									iBlockadesMade++;

									if (GC.getLogging() && GC.getAILogging())
									{
										CvString strLogString;
										strLogString.Format("Unit %d moved to blockade position (%d,%d) for city %s",
											pUnit->GetID(), pBlockadePlot->getX(), pBlockadePlot->getY(), pCity->getNameNoSpace().c_str());
										LogTacticalMessage(strLogString);
									}
									break; // Move to next blockade plot
								}
							}
						}

						// Update blockade status after moving units
						if (iBlockadesMade > 0)
						{
							bCityBlockaded = pCity->IsBlockaded(NO_DOMAIN);
							if (bCityBlockaded)
							{
								iCityHealingPerTurn = 0; // City can no longer heal
								if (GC.getLogging() && GC.getAILogging())
								{
									CvString strLogString;
									strLogString.Format("Successfully established blockade of %s with %d units", 
										pCity->getNameNoSpace().c_str(), iBlockadesMade);
									LogTacticalMessage(strLogString);
								}
							}
						}
					}
				}

				if (bDeferCaptureToCoalition)
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Zone %d, deferring capture of %s to coalition partner due to low self-capture value/burden", pZone ? pZone->GetZoneID() : -1, pCity->getNameNoSpace().c_str());
						LogTacticalMessage(strLogString);
					}

					continue;
				}

				//finally do the attack
				// Use higher aggression when capture is achievable this turn - we want to press the advantage
				// AL_BRAVEHEART allows riskier attacks, appropriate when the city can be captured
				eAggressionLevel aggLevel = AL_MEDIUM;
				if (bCaptureOpportunityThisTurn && !bAvoidRiskyCoalitionCapture)
					aggLevel = AL_BRAVEHEART; // Go all-in for the capture
				else if (bAvoidRiskyCoalitionCapture)
					aggLevel = (iMeleeCount <= 1) ? AL_LOW : AL_MEDIUM;
				else if (iMeleeCount > 2)
					aggLevel = AL_HIGH;
				
				ExecuteAttackWithUnits(pPlot, aggLevel);

				if (pPlot->getOwner() != m_pPlayer->GetID() && bLikelyNavalCaptureNextTurn)
					ExecutePreCaptureEmbarkedSupportStaging(pPlot);

				if (pPlot->getOwner() != m_pPlayer->GetID() && bPreferNavalCapture)
				{
					int iCityDamageAfterAttack = pCity->getDamage();
					int iDamageDelta = iCityDamageAfterAttack - iCityDamageBeforeAttack;
					if (iDamageDelta < max(8, iExpectedDamagePerTurn / 4))
						SetCoastalAssaultLandCooldown(pCity, 2, "limited progress after coastal assault");
				}

				// Did it work?  If so, don't need a temporary dominance zone if had one here
				if (pPlot->getOwner() == m_pPlayer->GetID())
					DeleteFocusArea(pPlot);

				//do we have embarked units we need to put ashore
				if (FindEmbarkedUnitsAroundTarget(pPlot, 4))
					ExecuteLandingOperation(pPlot);
			}
		}
	}
}

/// Assign a unit to capture an undefended barbarian camp
void CvTacticalAI::PlotGrabGoodyMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_GOODY);

	//allow a fairly big range so we can clear islands as well unless we're at war and need the units otherwise
	//note the barbarians are excluded from that check
	int iRange = m_pPlayer->IsAtWarAnyMajor() ? 6 : 11;

	//ruins first
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_GOODY); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		CvUnit* pUnit =	FindUnitForThisMove(AI_TACTICAL_GOODY,pPlot,iRange);
		if (pUnit)
		{
			ExecuteMoveToPlot(pUnit, pPlot, false);

			if (pUnit->canMove())
				//can use this unit for other stuff, reset the tactmove to avoid spamming the log
				pUnit->setTacticalMove(AI_TACTICAL_MOVE_NONE);
			else
				UnitProcessed(pUnit->GetID());

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving %s %d to grab a goody, X: %d, Y: %d", pUnit->getName().c_str(), pUnit->GetID(), pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
	}

	//then barb camps, occupied or not
	ClearCurrentMoveUnits(AI_TACTICAL_BARBARIAN_CAMP);
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (FindUnitsForHarassing(pPlot,iRange,-1,-1,DOMAIN_LAND,false,true,3))
		{
			ExecuteBarbarianCampMove(pPlot);
			if( GC.getLogging() && GC.getAILogging() )
			{
				CvString strLogString;
				strLogString.Format("Trying to remove barbarian camp, X: %d, Y: %d", pPlot->getX(), pPlot->getY());
				LogTacticalMessage(strLogString);
			}
		}
	}
}

void CvTacticalAI::ExecuteBarbarianTheft()
{
	vector<CvUnit*> vUsedUnits;
	for (std::list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		CvCity* pCity = pUnit->plot()->GetAdjacentCity();
		if (pCity)
		{
			if (CvBarbarians::DoTakeOverCityState(pCity) || CvBarbarians::DoStealFromCity(pUnit, pCity))
				vUsedUnits.push_back(pUnit);
		}
	}
	//have to do this in two steps to keep our iterator happy
	for (size_t i=0; i<vUsedUnits.size(); i++)
	{
		vUsedUnits[i]->finishMoves();
		UnitProcessed(vUsedUnits[i]->GetID());
	}
}

/// Moved endangered units to safe hexes
void CvTacticalAI::PlotMovesToSafety(bool bCombatUnits)
{
	ClearCurrentMoveUnits(AI_TACTICAL_SAFETY);
	bool bImminentAttack = m_bImminentAttack;

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if(pUnit && pUnit->canUseForTacticalAI())
		{
			// try to flee or hide
			int iDangerLevel = pUnit->GetDanger();
			if(iDangerLevel > 0 || pUnit->plot()->IsKnownVisibleToEnemy(m_pPlayer->GetID()))
			{
				bool bAddUnit = false;
				if(bCombatUnits)
				{
					if (!pUnit->IsCombatUnit() || (pUnit->IsGarrisoned() && pUnit->getDomainType() != DOMAIN_SEA) || pUnit->getArmyID() != -1)
						continue;

					//if danger is high or we took a lot of damage last turn
					//counterintuitively, barbarians always flee, because if we get here it means we did not attack this turn!
					if(iDangerLevel>pUnit->GetMaxHitPoints() || pUnit->isProjectedToDieNextTurn() || pUnit->isBarbarian())
					{
						bAddUnit = true;
					}
					else if (bImminentAttack)
					{
						// Under imminent attack: retreat wounded units earlier to regroup
						int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / max(1, pUnit->GetMaxHitPoints());
						if (iHealthPercent < 85)
							bAddUnit = true;
					}
				}
				else
				{
					// Civilian (or embarked) units always flee from danger
					if(!pUnit->IsCanDefend())
					{
						bAddUnit = true;
					}
				}

				if (bAddUnit)
				{
					m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
					//we will later sort by "attack strength" so fake it
					//ranged/slow units should retreat first, ie have a high strength
					int iFakeStrength = (pUnit->IsCanAttackRanged() ? 30 : 20) - pUnit->baseMoves(false);
					m_CurrentMoveUnits.back().SetAttackStrength(iFakeStrength);
					m_CurrentMoveUnits.back().SetHealthPercent(1,1);
				}
			}
		}
	}

	//melee units should retreat last
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		ExecuteMovesToSafestPlot(pUnit);
	}
}

/// Move barbarians across the map
void CvTacticalAI::PlotBarbarianRoaming()
{
	if (!m_pPlayer->isBarbarian())
		return;

	ClearCurrentMoveUnits(AI_TACTICAL_BARBARIAN_ROAM);

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->canUseForTacticalAI())
			m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
	}

	if(m_CurrentMoveUnits.size() > 0)
		ExecuteBarbarianRoaming();
}

//attack military units and civilians without regard for tactical zones
void CvTacticalAI::PlotBarbarianAttacks()
{
	//the Execute* functions are generic, need to set the current tactical move before calling them
	ClearCurrentMoveUnits(AI_TACTICAL_BARBARIAN_HUNT);
	ExecuteBarbarianTheft();
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_BRAVEHEART); pTarget != NULL; pTarget = GetNextZoneTarget(AL_BRAVEHEART))
		ExecuteDestroyEnemyUnits(*pTarget, AL_BRAVEHEART);
	ExecuteCaptureCityMoves();
}

/// Plunder trade routes
void CvTacticalAI::PlotPlunderTradeUnitMoves(DomainTypes eDomain)
{
	ClearCurrentMoveUnits(AI_TACTICAL_PLUNDER);

	AITacticalTargetType eTargetType = (eDomain == DOMAIN_LAND) ? AI_TACTICAL_TARGET_TRADE_UNIT_LAND : AI_TACTICAL_TARGET_TRADE_UNIT_SEA;

	// Morocco UA: Check if we're in dire economic straits
	bool bDesperatForGold = false;
	if (m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
	{
		// Consider desperate if we have less than 50 gold (economic desperation)
		int iGold = m_pPlayer->GetTreasury()->GetGold();
		
		// Low gold: less than 50
		if (iGold < 50)
		{
			bDesperatForGold = true;
		}
	}

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(eTargetType); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		// Morocco UA: Skip plundering allies/vassals unless desperate
		if (m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar() && !bDesperatForGold)
		{
			if (!ShouldPlunderTradeRouteAtPlot(pPlot))
			{
				continue;  // Skip this trade route
			}
		}

		if (FindUnitsForHarassing(pPlot,0,GD_INT_GET(MAX_HIT_POINTS)/2,-1,eDomain,true,false,5,true))
		{
			// Queue best one up to capture it
			ExecutePlunderTradeUnit(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Plundering trade unit, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
	}
}

/// Process units that we recruited out of operational moves.
void CvTacticalAI::PlotOperationalArmyMoves()
{
	//just so that UnitProcessed() sets the right flags
	ClearCurrentMoveUnits(AI_TACTICAL_OPERATION);

	// move all units in operations
	std::vector<int> opsToKill;
	for (size_t i=0; i<m_pPlayer->getNumAIOperations(); i++)
	{
		CvAIOperation* pOp = m_pPlayer->getAIOperationByIndex(i);
		if (!pOp->DoTurn())
			opsToKill.push_back(pOp->GetID());
	}

	//clean up - have to do this in two steps so the iterator does not get invalidated
	for (size_t i=0; i<opsToKill.size(); i++)
		m_pPlayer->getAIOperation(opsToKill[i])->Kill();
}

/// Assigns units to pillage enemy improvements
void CvTacticalAI::PlotPillageMoves(AITacticalTargetType eTarget, bool bImmediate)
{
	ClearCurrentMoveUnits(AI_TACTICAL_PILLAGE);

	int iBaseDamage = /*25*/ GD_INT_GET(PILLAGE_HEAL_AMOUNT);
	CvString szTargetName = "";
	if(GC.getLogging() && GC.getAILogging())
	{
		if (eTarget == AI_TACTICAL_TARGET_ENEMY_CITADEL)
		{
			szTargetName = "Citadel";
			iBaseDamage = 0; //also undamaged units may pillage this
		}
		else if (eTarget == AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE)
		{
			szTargetName = "Improved Resource";
			iBaseDamage = 0; //also undamaged units may pillage this
		}
		else if (eTarget == AI_TACTICAL_TARGET_IMPROVEMENT)
		{
			szTargetName = "Improvement";
		}
	}

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(eTarget); pTarget != NULL; pTarget = GetNextZoneTarget())
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		int iMinDamage = iBaseDamage;
		CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByPlot(pPlot);
		if (pZone)
		{
			//do not move in to pillage when we are fleeing from the zone (but we may pillage while withdrawing)
			if (pZone->GetPosture() == TACTICAL_POSTURE_WITHDRAW)
				continue;

			if (pZone->IsWater())
				iMinDamage = 0;
			else if (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
				iMinDamage /= 2;
		}

		// Don't do it if an enemy unit became visible in the meantime
		if (pPlot->getVisibleEnemyDefender(m_pPlayer->GetID()) != NULL)
			continue;

		if (bImmediate)
		{
			// try paratroopers first, not because they are more effective, just because it looks cooler...
			if (eTarget != AI_TACTICAL_TARGET_IMPROVEMENT && FindParatroopersWithinStrikingDistance(pPlot,true))
			{
				// Queue best one up to capture it
				ExecuteParadropPillage(pPlot);

				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Paratrooping in to pillage %s, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
					LogTacticalMessage(strLogString);
				}

			}
			else if (FindUnitsForHarassing(pPlot, 0, GD_INT_GET(MAX_HIT_POINTS) / 3, GD_INT_GET(MAX_HIT_POINTS) - iMinDamage, DOMAIN_LAND, true, false, 1))
			{
				if (ExecutePillage(pPlot))
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Pillaging %s, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
						LogTacticalMessage(strLogString);
					}
				}
			}
		}
		else if (FindUnitsForHarassing(pPlot, 2, GD_INT_GET(MAX_HIT_POINTS) / 2, GD_INT_GET(MAX_HIT_POINTS) - iMinDamage, DOMAIN_LAND, false, false, 1))
		{
			//be careful when sending out single units ...
			CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByPlot(pPlot);
			
			CvUnit* pUnit = (m_CurrentMoveUnits.size() > 0) ? m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID()) : 0;
			if (!pUnit)
				continue;
			
			// In enemy-dominated zones, only send units that can handle the danger:
			// - ZOC-ignoring units (can slip past defenders)
			// - Fast units with move-after-attack (can escape)
			// - Recon units (designed for behind-enemy-lines operations)
			bool bCanOperateInEnemyZone = false;
			if (pUnit->IsIgnoreZOC())
				bCanOperateInEnemyZone = true;
			else if (pUnit->canMoveAfterAttacking() && pUnit->baseMoves(false) >= 4)
				bCanOperateInEnemyZone = true;
			else if (pUnit->AI_getUnitAIType() == UNITAI_EXPLORE || 
					 pUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE)
				bCanOperateInEnemyZone = true;
			
			// Block regular units from entering enemy zones, but allow special units
			if (pZone && pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY && !bCanOperateInEnemyZone)
				continue;
			
			// Even special units should avoid if there's no zone (unexplored)
			if (!pZone && !bCanOperateInEnemyZone)
				continue;

			if (pUnit->canMoveInto(*pPlot, CvUnit::MOVEFLAG_DESTINATION))
			{
				ExecuteMoveToPlot(pUnit, pPlot, true, CvUnit::MOVEFLAG_NO_EMBARK | CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER);

				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					if (bCanOperateInEnemyZone && pZone && pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
					{
						strLogString.Format("Sending %s %s into ENEMY ZONE for pillage (%s%s%s), X: %d, Y: %d",
							pUnit->getUnitInfo().GetDescription(), 
							pUnit->getName().GetCString(),
							pUnit->IsIgnoreZOC() ? "ZOC-ignore " : "",
							pUnit->canMoveAfterAttacking() ? "move-after-attack " : "",
							(pUnit->AI_getUnitAIType() == UNITAI_EXPLORE) ? "recon" : "",
							pTarget->GetTargetX(), pTarget->GetTargetY());
					}
					else
					{
						strLogString.Format("Moving toward %s for pillage, X: %d, Y: %d", szTargetName.GetCString(), pTarget->GetTargetX(), pTarget->GetTargetY());
					}
					LogTacticalMessage(strLogString);
				}
			}
		}
	}
}

/// Move barbarian ships to disrupt usage of water improvements
void CvTacticalAI::PlotBlockadeMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_BLOCKADE);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BLOCKADE_POINT); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (FindUnitsForHarassing(pPlot, 4, GD_INT_GET(MAX_HIT_POINTS)/2, -1, DOMAIN_SEA, false, false, 1))
		{
			// Queue best one up to capture it
			ExecuteNavalBlockadeMove(pPlot);

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving into enemy territory for a naval blockade with move to, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
	}
}

/// Hunt enemy naval units that are blockading our cities
/// Breaking blockades is critical - blockaded cities can't heal and lose trade income
void CvTacticalAI::PlotCounterBlockadeMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_BLOCKADE);
	const int iNavalBlockadeRange = range(GD_INT_GET(NAVAL_PLOT_BLOCKADE_RANGE), 0, 3);
	
	// Find all our blockaded cities
	int iCityLoop = 0;
	for (CvCity* pCity = m_pPlayer->firstCity(&iCityLoop); pCity != NULL; pCity = m_pPlayer->nextCity(&iCityLoop))
	{
		if (!pCity->GetCityCitizens()->AnyPlotBlockaded())
			continue;
			
		// Higher urgency if city is damaged (can't heal while blockaded)
		bool bCityDamaged = pCity->getDamage() > 0;
		bool bCityUnderSiege = pCity->isUnderSiege();

		CvPlot* pBestBlockadedPlot = NULL;
		CvPlot* pBestBlockaderPlot = NULL;
		CvUnit* pBestBlockader = NULL;
		int iBestBlockadeScore = -1;

		// Find the actual enemy naval unit causing a blockade on one of this city's workable sea plots.
		CvPlot* pCityPlot = pCity->plot();
		for (int iI = 0; iI < pCity->GetNumWorkablePlots(); iI++)
		{
			CvPlot* pBlockedPlot = pCity->GetCityCitizens()->GetCityPlotFromIndex(iI);
			if (!pBlockedPlot || !pBlockedPlot->isEffectiveOwner(pCity) || !pBlockedPlot->isWater())
				continue;

			if (!pBlockedPlot->isBlockaded(m_pPlayer->GetID()))
				continue;

			for (int iRing = RING0_PLOTS; iRing < RING_PLOTS[iNavalBlockadeRange]; iRing++)
			{
				CvPlot* pEnemyPlot = iterateRingPlots(pBlockedPlot, iRing);
				if (!pEnemyPlot || pEnemyPlot->getLandmass() != pBlockedPlot->getLandmass())
					continue;

				IDInfo* pUnitNode = pEnemyPlot->headUnitNode();
				while (pUnitNode != NULL)
				{
					CvUnit* pEnemy = ::GetPlayerUnit(*pUnitNode);
					pUnitNode = pEnemyPlot->nextUnitNode(pUnitNode);

					if (!pEnemy || !m_pPlayer->IsAtWarWith(pEnemy->getOwner()))
						continue;
					if (pEnemy->getDomainType() != DOMAIN_SEA || !pEnemy->isNativeDomain(pEnemyPlot))
						continue;
					if (!pEnemy->canEndTurnAtPlot(pBlockedPlot))
						continue;

					int iBlockadeScore = 100;
					iBlockadeScore += (pEnemyPlot == pBlockedPlot) ? 25 : 0;
					iBlockadeScore -= plotDistance(*pEnemyPlot, *pCityPlot) * 10;
					iBlockadeScore -= plotDistance(*pEnemyPlot, *pBlockedPlot) * 4;
					iBlockadeScore += pEnemy->IsCanAttackRanged() ? 5 : 0;

					if (iBlockadeScore > iBestBlockadeScore)
					{
						iBestBlockadeScore = iBlockadeScore;
						pBestBlockadedPlot = pBlockedPlot;
						pBestBlockaderPlot = pEnemyPlot;
						pBestBlockader = pEnemy;
					}
				}
			}
		}

		if (!pBestBlockader || !pBestBlockaderPlot)
			continue;

		// Found a blockading naval unit! Try to attack it with our naval units.
		// Use higher range for damaged/besieged cities.
		int iSearchRange = bCityUnderSiege ? 6 : (bCityDamaged ? 5 : 4);

		if (FindUnitsForHarassing(pBestBlockaderPlot, iSearchRange, GD_INT_GET(MAX_HIT_POINTS) / 3, -1, DOMAIN_SEA, false, false, 3))
		{
			// Prefer melee units over ranged - melee can also capture the city if opportunity arises.
			CvUnit* pBestMelee = NULL;
			CvUnit* pBestRanged = NULL;

			for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
			{
				CvUnit* pAttacker = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
				if (!pAttacker || pAttacker->TurnProcessed())
					continue;

				if (pAttacker->IsCanAttackRanged())
				{
					if (pAttacker->canRangeStrikeAt(pBestBlockaderPlot->getX(), pBestBlockaderPlot->getY()))
					{
						if (!pBestRanged || pAttacker->GetCurrHitPoints() > pBestRanged->GetCurrHitPoints())
							pBestRanged = pAttacker;
					}
				}
				else
				{
					if (pAttacker->canMoveInto(*pBestBlockaderPlot, CvUnit::MOVEFLAG_ATTACK))
					{
						if (!pBestMelee || pAttacker->GetCurrHitPoints() > pBestMelee->GetCurrHitPoints())
							pBestMelee = pAttacker;
					}
				}
			}

			// Prefer melee (can also clear the tile) unless it would die from the attack.
			CvUnit* pAttacker = NULL;
			if (pBestMelee)
			{
				int iSelfDamage = 0;
				TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pBestBlockader, pBestMelee, pBestBlockaderPlot, pBestMelee->plot(), iSelfDamage, true, 0, true);

				if (pBestMelee->GetCurrHitPoints() > iSelfDamage || !pBestRanged)
					pAttacker = pBestMelee;
				else
					pAttacker = pBestRanged;
			}
			else
			{
				pAttacker = pBestRanged;
			}

			if (pAttacker)
			{
				bool bIsMelee = !pAttacker->IsCanAttackRanged();

				if (bIsMelee)
				{
					pAttacker->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestBlockaderPlot->getX(), pBestBlockaderPlot->getY(), CvUnit::MOVEFLAG_ATTACK);

					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Counter-blockade: Melee unit %d attacking blockader near %s at (%d,%d) via blockaded plot (%d,%d)%s%s [can clear tile]",
							pAttacker->GetID(), pCity->getName().GetCString(), pBestBlockaderPlot->getX(), pBestBlockaderPlot->getY(),
							pBestBlockadedPlot ? pBestBlockadedPlot->getX() : -1, pBestBlockadedPlot ? pBestBlockadedPlot->getY() : -1,
							bCityDamaged ? " [CITY DAMAGED]" : "", bCityUnderSiege ? " [UNDER SIEGE]" : "");
						LogTacticalMessage(strLogString);
					}
				}
				else
				{
					pAttacker->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pBestBlockaderPlot->getX(), pBestBlockaderPlot->getY());

					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Counter-blockade: Ranged unit %d bombarding blockader near %s at (%d,%d) via blockaded plot (%d,%d)",
							pAttacker->GetID(), pCity->getName().GetCString(), pBestBlockaderPlot->getX(), pBestBlockaderPlot->getY(),
							pBestBlockadedPlot ? pBestBlockadedPlot->getX() : -1, pBestBlockadedPlot ? pBestBlockadedPlot->getY() : -1);
						LogTacticalMessage(strLogString);
					}
				}

				if (!pAttacker->canMove())
					UnitProcessed(pAttacker->GetID());
			}
		}
	}
}

/// Position naval units defensively around threatened coastal cities
/// This intercepts incoming naval threats before they can blockade or capture
void CvTacticalAI::PlotCoastalDefenseMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_ESCORT);
	
	// Find coastal cities that need naval defense
	int iCityLoop = 0;
	for (CvCity* pCity = m_pPlayer->firstCity(&iCityLoop); pCity != NULL; pCity = m_pPlayer->nextCity(&iCityLoop))
	{
		if (!pCity->isCoastal())
			continue;
		
		// Check the water zone for this city
		CvTacticalDominanceZone* pWaterZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);
		if (!pWaterZone)
			continue;

		// Prioritize cities under threat
		bool bEnemyNavalPresence = (pWaterZone->GetEnemyNavalUnitCount() > 0);
		bool bEnemyDominated = (pWaterZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY);
		bool bCityDamaged = (pCity->getDamage() > 0);
		bool bBlockaded = pCity->GetCityCitizens()->AnyPlotBlockaded();

		// A locally blockaded city still needs help even if the zone remains net-friendly.
		if (pWaterZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY && !bBlockaded)
			continue;
		
		// Skip if no naval threat
		if (!bEnemyNavalPresence && !bEnemyDominated && !bBlockaded)
			continue;
		
		// Find water plots adjacent to the city that need defense
		CvPlot* pCityPlot = pCity->plot();
		vector<CvPlot*> vDefensePositions;
		
		for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
		{
			CvPlot* pAdj = plotDirection(pCityPlot->getX(), pCityPlot->getY(), (DirectionTypes)iDir);
			if (!pAdj || !pAdj->isWater())
				continue;
			
			// Skip if there's already a friendly naval unit here
			CvUnit* pDefender = pAdj->getBestDefender(m_pPlayer->GetID());
			if (pDefender && pDefender->getDomainType() == DOMAIN_SEA)
				continue;
			
			// Skip if there's an enemy here (counter-blockade handles that)
			if (pAdj->isEnemyUnit(m_pPlayer->GetID(), true, true))
				continue;
			
			vDefensePositions.push_back(pAdj);
		}
		
		if (vDefensePositions.empty())
			continue;
		
		// Find naval units to position at these defensive spots
		// Higher search range for more threatened cities
		int iSearchRange = 3;
		if (bBlockaded || bCityDamaged)
			iSearchRange = 5;
		else if (bEnemyDominated)
			iSearchRange = 4;
		
		// Look for available naval units
		for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(*it);
			if (!pUnit || !pUnit->canUseForTacticalAI())
				continue;
			
			// Only naval combat units
			if (pUnit->getDomainType() != DOMAIN_SEA || !pUnit->IsCombatUnit())
				continue;
			
			// Don't use carriers or submarines for patrol
			if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->getInvisibleType() != NO_INVISIBLE)
				continue;
			
			// Don't pull from armies
			if (pUnit->getArmyID() != -1)
				continue;
			
			// Check if unit is close enough
			if (plotDistance(*pUnit->plot(), *pCityPlot) > iSearchRange * 2)
				continue;
			
			// Don't use heavily damaged units
			if (pUnit->shouldHeal(false))
				continue;
			
			// Find best defense position this unit can reach
			CvPlot* pBestPlot = NULL;
			int iBestScore = INT_MIN;
			
			// COMBINED ARMS DEFENSE: Check what city/garrison can attack for coordination
			CvUnit* pCityTarget = NULL;
			if (pCity->canRangeStrike() && !pCity->isMadeAttack())
				pCityTarget = pCity->getBestRangedStrikeTarget();
			
			for (size_t i = 0; i < vDefensePositions.size(); i++)
			{
				CvPlot* pDefPlot = vDefensePositions[i];
				
				// Can we reach it?
				int iTurns = pUnit->TurnsToReachTarget(pDefPlot, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF, iSearchRange);
				if (iTurns == MAX_INT)
					continue;
				
				// Score: closer is better, prefer positions that block likely attack vectors
				int iScore = 100 - iTurns * 20;

				// Penalize tiles that are already covered by real enemy attack vectors.
				// Adjacent contact alone misses naval ranged fire and multi-tile approach lanes.
				int iDanger = min(500, pUnit->GetDanger(pDefPlot));
				iScore -= min(60, iDanger / 3);

				std::vector<CvUnit*> vAttackers = m_pPlayer->GetPossibleAttackers(*pDefPlot, m_pPlayer->getTeam());
				int iRangedThreats = 0;
				int iNavalThreats = 0;
				for (size_t iAttacker = 0; iAttacker < vAttackers.size(); iAttacker++)
				{
					CvUnit* pAttacker = vAttackers[iAttacker];
					if (!pAttacker || !m_pPlayer->IsAtWarWith(pAttacker->getOwner()))
						continue;
					if (pAttacker->getDomainType() == DOMAIN_SEA)
						iNavalThreats++;
					if (pAttacker->IsCanAttackRanged() && plotDistance(*pAttacker->plot(), *pDefPlot) > 1)
						iRangedThreats++;
				}

				iScore -= iNavalThreats * 6;
				iScore -= iRangedThreats * 10;
				if (vAttackers.empty())
					iScore += 10;
				
				// Bonus for positions that face enemy territory
				for (int iDir2 = 0; iDir2 < NUM_DIRECTION_TYPES; iDir2++)
				{
					CvPlot* pCheck = plotDirection(pDefPlot->getX(), pDefPlot->getY(), (DirectionTypes)iDir2);
					if (pCheck && pCheck->isVisible(m_pPlayer->getTeam()))
					{
						if (pCheck->isEnemyUnit(m_pPlayer->GetID(), true, false))
							iScore += 30; // Block enemy approach
						else if (pCheck->getOwner() != NO_PLAYER && m_pPlayer->IsAtWarWith(pCheck->getOwner()))
							iScore += 15; // Face enemy territory
					}
				}
				
				// COORDINATED FIRE: If ranged, prefer positions where we could attack targets
				// from the destination plot, not just from the unit's current location.
				if (pUnit->IsCanAttackRanged())
				{
					// Check what enemies we can reach from this position
					for (int iRing = 1; iRing <= pUnit->GetRange(); iRing++)
					{
						for (int iIdx = RING_PLOTS[iRing-1]; iIdx < RING_PLOTS[iRing]; iIdx++)
						{
							CvPlot* pTargetPlot = iterateRingPlots(pDefPlot, iIdx);
							if (!pTargetPlot)
								continue;

							if (!pUnit->canEverRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY(), pDefPlot, false))
								continue;
							
							CvUnit* pEnemy = pTargetPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true);
							if (pEnemy)
							{
								// Bonus if we can attack a target the city is NOT attacking
								if (pCityTarget != pEnemy)
									iScore += 15;
								
								// Bonus if combined fire with city could kill
								if (pCityTarget == pEnemy)
								{
									int iCityDmg = pCity->rangeCombatDamage(pEnemy, false, NULL);
									int iUnused = 0;
									int iNavalDmg = pUnit->GetRangeCombatDamage(pEnemy, NULL, 0, iUnused, false);
									if (iCityDmg + iNavalDmg >= pEnemy->GetCurrHitPoints())
										iScore += 25; // Coordinated kill!
								}
							}
						}
					}
				}
				
				// Prefer ranged units slightly back, melee up front
				if (pUnit->IsCanAttackRanged() && iTurns == 0)
					iScore -= 10; // Ranged don't need to be right next to city
				else if (!pUnit->IsCanAttackRanged() && iTurns == 0)
					iScore += 20; // Melee should be up front
				
				if (iScore > iBestScore)
				{
					iBestScore = iScore;
					pBestPlot = pDefPlot;
				}
			}
			
			if (pBestPlot)
			{
				int iMoveResult = ExecuteMoveToPlot(pUnit, pBestPlot, false, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF);
				if (iMoveResult == INT_MAX)
					continue;

				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Coastal defense: Unit %d moving to defend %s at (%d,%d)%s%s",
						pUnit->GetID(), pCity->getName().GetCString(), pBestPlot->getX(), pBestPlot->getY(),
						bBlockaded ? " [BLOCKADED]" : "", bEnemyDominated ? " [ENEMY DOMINATED]" : "");
					LogTacticalMessage(strLogString);
				}

				// Remove this position from available list once a ship has actually taken or committed to it.
				vDefensePositions.erase(std::remove(vDefensePositions.begin(), vDefensePositions.end(), pBestPlot), vDefensePositions.end());

				// Reserve the ship for this defensive assignment even if it still has movement left.
				UnitProcessed(pUnit->GetID());

				// Stop if all positions filled
				if (vDefensePositions.empty())
					break;
			}
		}
	}
}

void CvTacticalAI::PlotNavalPatrolStationMoves()
{
	const CvStrategicGeographyMap* pGeo = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	if (!pGeo)
		return;
	if (pGeo->GetGeographicPosture() != GEO_POSTURE_ISLAND && pGeo->GetGeographicPosture() != GEO_POSTURE_ARCHIPELAGO)
		return;

	const std::vector<int>& vStations = pGeo->GetPatrolStations();
	if (vStations.empty())
		return;

	std::vector<CvPlot*> vTargets;
	for (size_t i = 0; i < vStations.size(); i++)
	{
		CvPlot* pPlot = GC.getMap().plotByIndex(vStations[i]);
		if (!pPlot || !pPlot->isWater() || pPlot->isLake())
			continue;
		if (!pPlot->isValidMovePlot(m_pPlayer->GetID()))
			continue;

		CvUnit* pDefender = pPlot->getBestDefender(m_pPlayer->GetID());
		if (pDefender && pDefender->getDomainType() == DOMAIN_SEA)
			continue;

		vTargets.push_back(pPlot);
	}

	if (vTargets.empty())
		return;

	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || !pUnit->canUseForTacticalAI())
			continue;
		if (pUnit->getDomainType() != DOMAIN_SEA || !pUnit->IsCombatUnit())
			continue;
		if (pUnit->getArmyID() != -1)
			continue;
		if (pUnit->shouldHeal(false))
			continue;
		if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->getInvisibleType() != NO_INVISIBLE)
			continue;

		CvPlot* pBestTarget = NULL;
		int iBestTurns = INT_MAX;

		for (size_t iTarget = 0; iTarget < vTargets.size(); iTarget++)
		{
			CvPlot* pTarget = vTargets[iTarget];
			int iTurns = pUnit->TurnsToReachTarget(pTarget, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF, 4);
			if (iTurns == MAX_INT || iTurns > 4)
				continue;
			if (iTurns < iBestTurns)
			{
				iBestTurns = iTurns;
				pBestTarget = pTarget;
			}
		}

		if (!pBestTarget)
			continue;

		if (pUnit->plot() == pBestTarget)
		{
			if (pUnit->canSentry(pBestTarget))
				pUnit->PushMission(CvTypes::getMISSION_ALERT());
			else
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
			UnitProcessed(pUnit->GetID());
		}
		else
		{
			if (ExecuteMoveToPlot(pUnit, pBestTarget, false, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF) != INT_MAX)
				UnitProcessed(pUnit->GetID());
		}

		vTargets.erase(std::remove(vTargets.begin(), vTargets.end(), pBestTarget), vTargets.end());
		if (vTargets.empty())
			break;
	}
}

/// Phase I-5: Convoy escort assignment for high-risk inter-island transits
void CvTacticalAI::PlotConvoyEscortMoves()
{
	const CvStrategicGeographyMap* pGeo = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	if (!pGeo)
		return;

	// Works for all postures, but most valuable for island/archipelago
	const std::vector<PendingTransit>& vTransits = pGeo->GetPendingTransits();
	if (vTransits.empty())
		return;

	// Group transits by destination & risk level
	std::map<int, std::vector<const PendingTransit*>> convoyGroups; // Keyed by dest plot index
	for (size_t i = 0; i < vTransits.size(); i++)
	{
		const PendingTransit& transit = vTransits[i];
		if (transit.eRisk != TRANSIT_RISK_MEDIUM && transit.eRisk != TRANSIT_RISK_HIGH)
			continue; // LOW RISK = no escort needed
		convoyGroups[transit.iDestPlotIndex].push_back(&transit);
	}

	if (convoyGroups.empty())
		return;

	// Collect available naval combat units for escort duty
	std::vector<CvUnit*> vEscorts;
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || !pUnit->canUseForTacticalAI())
			continue;
		if (pUnit->getDomainType() != DOMAIN_SEA || !pUnit->IsCanAttack())
			continue;
		if (pUnit->getArmyID() != -1)
			continue;
		if (pUnit->shouldHeal(false))
			continue;
		// Reserve carriers/subs for other duties
		if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->getInvisibleType() != NO_INVISIBLE)
			continue;

		vEscorts.push_back(pUnit);
	}

	if (vEscorts.empty())
		return;

	// Assign escorts to convoys
	for (std::map<int, std::vector<const PendingTransit*>>::iterator it = convoyGroups.begin(); it != convoyGroups.end(); ++it)
	{
		const std::vector<const PendingTransit*>& vConvoy = it->second;
		if (vConvoy.empty())
			continue;

		// Determine required escorts based on risk and convoy size
		eTransitRisk eMaxRisk = TRANSIT_RISK_LOW;
		bool bHasHighValue = false;
		for (size_t i = 0; i < vConvoy.size(); i++)
		{
			if (vConvoy[i]->eRisk > eMaxRisk)
				eMaxRisk = vConvoy[i]->eRisk;
			if (vConvoy[i]->bHighPriority)
				bHasHighValue = true;
		}

		int iEscortsNeeded = (eMaxRisk == TRANSIT_RISK_HIGH && bHasHighValue) ? 2 : 1;
		int iEscortsAssigned = 0;

		// Find closest escorts to this convoy
		CvPlot* pConvoyOrigin = GC.getMap().plotByIndex(vConvoy[0]->iOriginPlotIndex);
		if (!pConvoyOrigin)
			continue;

		std::vector<CvUnit*> vAssigned;
		for (int iPass = 0; iPass < iEscortsNeeded && !vEscorts.empty(); iPass++)
		{
			CvUnit* pBestEscort = NULL;
			int iBestDist = INT_MAX;

			for (size_t i = 0; i < vEscorts.size(); i++)
			{
				CvUnit* pEscort = vEscorts[i];
				// M7 fix: use TurnsToReachTarget for actual reachability, not straight-line distance
				int iTurns = pEscort->TurnsToReachTarget(pConvoyOrigin, CvUnit::MOVEFLAG_APPROX_TARGET_RING1, 5);
				if (iTurns < iBestDist)
				{
					iBestDist = iTurns;
					pBestEscort = pEscort;
				}
			}

			if (pBestEscort && iBestDist <= 5)
			{
				vAssigned.push_back(pBestEscort);
				vEscorts.erase(std::remove(vEscorts.begin(), vEscorts.end(), pBestEscort), vEscorts.end());
				iEscortsAssigned++;
			}
			else
				break; // No more escorts in range
		}

		// Move assigned escorts to convoy staging area
		for (size_t i = 0; i < vAssigned.size(); i++)
		{
			CvUnit* pEscort = vAssigned[i];
			if (ExecuteMoveToPlot(pEscort, pConvoyOrigin, false, CvUnit::MOVEFLAG_APPROX_TARGET_RING1) != INT_MAX)
				UnitProcessed(pEscort->GetID());
		}
	}
}

/// Phase I-6: Intercept enemy invasion convoys detected by the strategic geography map.
/// Assigns available naval combat units to attack clusters of enemy embarked units
/// approaching our coast. Prioritizes clusters containing settlers.
void CvTacticalAI::PlotAntiInvasionMoves()
{
	const CvStrategicGeographyMap* pGeo = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	if (!pGeo || !pGeo->IsInvasionImminent())
		return;

	const std::vector<CvStrategicGeographyMap::DetectedConvoy>& vConvoys = pGeo->GetDetectedConvoys();

	// Sort convoys: settler convoys first, then by proximity to our coast
	std::vector<size_t> vSortOrder;
	for (size_t i = 0; i < vConvoys.size(); i++)
		vSortOrder.push_back(i);

	// Simple insertion sort by priority (settler first, then closest)
	for (size_t i = 1; i < vSortOrder.size(); i++)
	{
		size_t key = vSortOrder[i];
		int j = (int)i - 1;
		while (j >= 0)
		{
			size_t cmp = vSortOrder[j];
			bool bKeyBetter = false;
			if (vConvoys[key].bHasSettler && !vConvoys[cmp].bHasSettler)
				bKeyBetter = true;
			else if (vConvoys[key].bHasSettler == vConvoys[cmp].bHasSettler &&
			         vConvoys[key].iNearestCoastDist < vConvoys[cmp].iNearestCoastDist)
				bKeyBetter = true;

			if (!bKeyBetter)
				break;
			vSortOrder[j + 1] = vSortOrder[j];
			j--;
		}
		vSortOrder[j + 1] = key;
	}

	// Collect available naval combat units
	std::vector<CvUnit*> vInterceptors;
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || !pUnit->canUseForTacticalAI())
			continue;
		if (pUnit->getDomainType() != DOMAIN_SEA || !pUnit->IsCanAttack())
			continue;
		if (pUnit->getArmyID() != -1)
			continue;
		if (pUnit->shouldHeal(false))
			continue;
		if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->getInvisibleType() != NO_INVISIBLE)
			continue;

		vInterceptors.push_back(pUnit);
	}

	if (vInterceptors.empty())
		return;

	// Assign interceptors to convoys
	for (size_t iConvoy = 0; iConvoy < vSortOrder.size() && !vInterceptors.empty(); iConvoy++)
	{
		const CvStrategicGeographyMap::DetectedConvoy& convoy = vConvoys[vSortOrder[iConvoy]];
		CvPlot* pConvoyCenter = GC.getMap().plotByIndex(convoy.iCenterPlotIndex);
		if (!pConvoyCenter)
			continue;

		// Try to find embarked enemy units near the center to attack directly
		// Search the area around the convoy center for actual targets
		CvPlot* pBestTarget = NULL;
		int iBestTargetPriority = 0;

		for (int iRing = 0; iRing <= 3; iRing++)
		{
			int iStart = (iRing == 0) ? 0 : RING_PLOTS[iRing - 1];
			int iEnd = RING_PLOTS[iRing];
			for (int iIdx = iStart; iIdx < iEnd; iIdx++)
			{
				CvPlot* pCheck = iterateRingPlots(pConvoyCenter, iIdx);
				if (!pCheck || !pCheck->isWater())
					continue;
				if (!pCheck->isVisible(m_pPlayer->getTeam()))
					continue;

				// Look for enemy embarked units
				for (int iUnitIdx = 0; iUnitIdx < pCheck->getNumUnits(); iUnitIdx++)
				{
					CvUnit* pEnemy = pCheck->getUnitByIndex(iUnitIdx);
					if (!pEnemy || !pEnemy->isEmbarked())
						continue;
					if (!m_pPlayer->IsAtWarWith(pEnemy->getOwner()))
						continue;

					int iPriority = 10; // Base priority
					if (pEnemy->AI_getUnitAIType() == UNITAI_SETTLE)
						iPriority = 100; // Settlers are #1 target
					else if (pEnemy->IsGreatPerson())
						iPriority = 80;

					if (iPriority > iBestTargetPriority)
					{
						iBestTargetPriority = iPriority;
						pBestTarget = pCheck;
					}
				}
			}
		}

		if (!pBestTarget)
			pBestTarget = pConvoyCenter; // Fallback: move toward convoy center

		// Assign up to 3 interceptors per convoy (proportional to convoy size)
		int iMaxAssign = min(3, max(1, convoy.iEmbarkCount / 2));
		int iAssigned = 0;

		for (size_t iUnit = 0; iUnit < vInterceptors.size() && iAssigned < iMaxAssign; )
		{
			CvUnit* pUnit = vInterceptors[iUnit];
			int iTurns = pUnit->TurnsToReachTarget(pBestTarget, CvUnit::MOVEFLAG_APPROX_TARGET_RING1, 5);
			if (iTurns == MAX_INT || iTurns > 5)
			{
				iUnit++;
				continue;
			}

			// Move toward target
			if (ExecuteMoveToPlot(pUnit, pBestTarget, false, CvUnit::MOVEFLAG_APPROX_TARGET_RING1) == INT_MAX)
			{
				iUnit++;
				continue;
			}

			UnitProcessed(pUnit->GetID());

			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Anti-invasion: Unit %d intercepting convoy at (%d,%d), enemy=%d, count=%d%s, dist=%d",
					pUnit->GetID(), pBestTarget->getX(), pBestTarget->getY(),
					(int)convoy.eEnemy, convoy.iEmbarkCount,
					convoy.bHasSettler ? " [SETTLER]" : "", iTurns);
				LogTacticalMessage(strLogString);
			}

			iAssigned++;
			vInterceptors.erase(vInterceptors.begin() + iUnit);
			// Don't increment iUnit since we erased the current element
		}
	}
}

/// Phase I-7: Station naval units at identified strait chokepoints.
/// Width-1 straits get 1 ranged + 1 melee; wider straits up to 3 ships.
/// Ships at strait positions enter sentry/alert mode to block passage.
void CvTacticalAI::PlotStraitDefenseMoves()
{
	const CvStrategicGeographyMap* pGeo = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	if (!pGeo)
		return;

	const std::vector<CvStrategicGeographyMap::StraitDefensePosition>& vPositions = pGeo->GetStraitDefensePositions();
	if (vPositions.empty())
		return;

	// Cap strait defense fleet at 30% of total navy to avoid over-commitment
	int iTotalNavy = 0;
	int iStraitAssigned = 0;
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit())
			iTotalNavy++;
	}
	int iMaxStraitFleet = max(2, iTotalNavy * 30 / 100);

	// Build available position list (skip already-occupied positions)
	std::vector<const CvStrategicGeographyMap::StraitDefensePosition*> vOpenPositions;
	for (size_t i = 0; i < vPositions.size(); i++)
	{
		CvPlot* pPlot = GC.getMap().plotByIndex(vPositions[i].iPlotIndex);
		if (!pPlot || !pPlot->isWater())
			continue;
		if (!pPlot->isValidMovePlot(m_pPlayer->GetID()))
			continue;

		// Check if already garrisoned by our naval unit
		CvUnit* pDefender = pPlot->getBestDefender(m_pPlayer->GetID());
		if (pDefender && pDefender->getDomainType() == DOMAIN_SEA)
		{
			iStraitAssigned++; // Count as already assigned
			continue;
		}

		vOpenPositions.push_back(&vPositions[i]);
	}

	if (vOpenPositions.empty())
		return;

	// Sort: narrower straits first (more critical)
	for (size_t i = 1; i < vOpenPositions.size(); i++)
	{
		const CvStrategicGeographyMap::StraitDefensePosition* key = vOpenPositions[i];
		int j = (int)i - 1;
		while (j >= 0 && vOpenPositions[j]->iWidth > key->iWidth)
		{
			vOpenPositions[j + 1] = vOpenPositions[j];
			j--;
		}
		vOpenPositions[j + 1] = key;
	}

	// Assign naval units to strait positions
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end() && !vOpenPositions.empty(); ++it)
	{
		if (iStraitAssigned >= iMaxStraitFleet)
			break;

		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || !pUnit->canUseForTacticalAI())
			continue;
		if (pUnit->getDomainType() != DOMAIN_SEA || !pUnit->IsCombatUnit())
			continue;
		if (pUnit->getArmyID() != -1)
			continue;
		if (pUnit->shouldHeal(false))
			continue;
		if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->getInvisibleType() != NO_INVISIBLE)
			continue;

		// Find closest open strait position within 4 turns
		const CvStrategicGeographyMap::StraitDefensePosition* pBest = NULL;
		int iBestTurns = INT_MAX;
		size_t iBestIdx = 0;

		for (size_t iPos = 0; iPos < vOpenPositions.size(); iPos++)
		{
			CvPlot* pTarget = GC.getMap().plotByIndex(vOpenPositions[iPos]->iPlotIndex);
			if (!pTarget)
				continue;

			int iTurns = pUnit->TurnsToReachTarget(pTarget, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF, 4);
			if (iTurns == MAX_INT || iTurns > 4)
				continue;

			if (iTurns < iBestTurns)
			{
				iBestTurns = iTurns;
				pBest = vOpenPositions[iPos];
				iBestIdx = iPos;
			}
		}

		if (!pBest)
			continue;

		CvPlot* pTarget = GC.getMap().plotByIndex(pBest->iPlotIndex);
		if (pUnit->plot() == pTarget)
		{
			if (pUnit->canSentry(pTarget))
				pUnit->PushMission(CvTypes::getMISSION_ALERT());
			else
				pUnit->PushMission(CvTypes::getMISSION_SKIP());
			UnitProcessed(pUnit->GetID());
		}
		else
		{
			if (ExecuteMoveToPlot(pUnit, pTarget, false, CvUnit::MOVEFLAG_IGNORE_STACKING_SELF) != INT_MAX)
				UnitProcessed(pUnit->GetID());
		}

		if (GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("Strait defense: Unit %d heading to strait at (%d,%d), width=%d, dist=%d",
				pUnit->GetID(), pTarget->getX(), pTarget->getY(), pBest->iWidth, iBestTurns);
			LogTacticalMessage(strLogString);
		}

		vOpenPositions.erase(vOpenPositions.begin() + iBestIdx);
		iStraitAssigned++;
	}
}

void CvTacticalAI::PlotCivilianAttackMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_CAPTURE);
	ExecuteCivilianAttackMoves(AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN);
	ExecuteCivilianAttackMoves(AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN);
}

/// Assigns units to capture undefended civilians
void CvTacticalAI::ExecuteCivilianAttackMoves(AITacticalTargetType eTargetType)
{
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(eTargetType); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		// See what units we have who can reach target this turn
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		// Try paratroopers first - they can drop directly on the civilian to capture it
		// This is especially effective for settlers which are high-value targets
		if (FindParatroopersWithinStrikingDistance(pPlot, true))
		{
			if (ExecuteParadropCivilian(pPlot))
			{
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Paratrooping to capture %s civilian, X: %d, Y: %d",
						eTargetType == AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN ? "high priority" : "low priority",
						pTarget->GetTargetX(), pTarget->GetTargetY());
					LogTacticalMessage(strLogString);
				}
				continue; // Captured, move to next target
			}
		}

		// Standard ground approach
		if(FindUnitsForHarassing(pPlot,1,GD_INT_GET(MAX_HIT_POINTS)/2,-1,DOMAIN_LAND,false,false,1))
		{
			for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
			{
				CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
				if (!pUnit || !pUnit->canMoveInto(*pPlot, CvUnit::MOVEFLAG_ATTACK))
					continue;
				//don't allow humans to use civilians as bait to lure units out of camps
				if (pUnit->GetDanger() == 0 || pUnit->plot()->getImprovementType()!=(ImprovementTypes)GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
				{
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pPlot->getX(), pPlot->getY(), CvUnit::MOVEFLAG_NO_EMBARK);

					// Delete this unit from those we have to move
					if (!pUnit->canMove())
						UnitProcessed(m_CurrentMoveUnits[i].GetID());

					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						switch (eTargetType)
						{
						case AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN:
							strLogString.Format("Attacking high priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
								pTarget->GetTargetY());
							break;
						case AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN:
							strLogString.Format("Attacking low priority civilian, X: %d, Y: %d", pTarget->GetTargetX(),
								pTarget->GetTargetY());
							break;
						default:
							UNREACHABLE(); // Unsupported `eTargetType`.
						}
						LogTacticalMessage(strLogString);
					}

					break;
				}
			}
		}
	}
}

/// Assigns units to heal
void CvTacticalAI::PlotHealMoves(bool bFirstPass)
{
	ClearCurrentMoveUnits(AI_TACTICAL_HEAL);

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || !pUnit->canUseForTacticalAI())
			continue;

		if (pUnit->shouldHeal(bFirstPass))
			m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
	}

	if(m_CurrentMoveUnits.size() > 0)
		ExecuteHeals(bFirstPass);
}

/// Assigns a barbarian to go protect an undefended camp
void CvTacticalAI::PlotBarbarianCampDefense()
{
	ClearCurrentMoveUnits(AI_TACTICAL_BARBARIAN_CAMP);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		//for the barbarian player AI_TACTICAL_TARGET_BARBARIAN_CAMP does not automatically mean the camp is empty of _barbarian_ defenders (check is only for enemy units)
		CvUnit* currentDefender = pPlot->getBestDefender(BARBARIAN_PLAYER);
		if (currentDefender)
		{
			if (currentDefender->CanUpgradeRightNow(true) && !currentDefender->IsHurt())
			{
				CvUnit* pNewUnit = currentDefender->DoUpgrade(true);
				if (pNewUnit)
					UnitProcessed(pNewUnit->GetID());
			}
			else if (currentDefender->IsCanAttackRanged())
			{
				//don't leave camp
				TacticalAIHelpers::PerformRangedOpportunityAttack(currentDefender);
				currentDefender->PushMission(CvTypes::getMISSION_SKIP());
			}
			else
			{
				//capture unprotected civilians if we can return to camp in the same turn
				CvPlot** neighbors = GC.getMap().getNeighborsShuffled(pPlot);
				for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
				{
					CvPlot* pNeighbor = neighbors[i];
					if (!pNeighbor)
						continue;

					if (!pNeighbor->isEnemyUnit(m_pPlayer->GetID(), true, true, true, true) &&
						pNeighbor->isEnemyUnit(m_pPlayer->GetID(), false, true, true, true) &&
						currentDefender->TurnsToReachTarget(pPlot,0,1)==0)
					{
						currentDefender->PushMission(CvTypes::getMISSION_MOVE_TO(), pNeighbor->getX(), pNeighbor->getY());
						currentDefender->PushMission(CvTypes::getMISSION_MOVE_TO(), pPlot->getX(), pPlot->getY());
						break;
					}
				}

				//melee may attack but never leave camp (because they won't return then)
				TacticalAIHelpers::PerformOpportunityAttack(currentDefender,false);
				currentDefender->PushMission(CvTypes::getMISSION_SKIP());
			}

			UnitProcessed(currentDefender->GetID());
		}
		else if (FindUnitsForHarassing(pPlot,5,-1,-1,DOMAIN_LAND,false,false,1))
		{
			CvUnit* pUnit = (m_CurrentMoveUnits.size() > 0) ? m_pPlayer->getUnit(m_CurrentMoveUnits.begin()->GetID()) : 0;
			ExecuteMoveToPlot(pUnit,pPlot);
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Moving to protect camp, X: %d, Y: %d", pTarget->GetTargetX(), pTarget->GetTargetY());
				LogTacticalMessage(strLogString);
			}
		}
	}
}

/// Make a defensive move to garrison a city
void CvTacticalAI::PlotGarrisonMoves(int iNumTurnsAway)
{
	ClearCurrentMoveUnits(AI_TACTICAL_GARRISON);
	bool bImminentAttack = m_bImminentAttack;
	int iMajorWars = m_pPlayer->CountNumDangerousMajorsAtWarWith(true, false);
	bool bMultiFrontWar = (iMajorWars >= 2);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_FRIENDLY_CITY); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		CvCity* pCity = pPlot->getPlotCity();
		if(!pCity)
			continue;

		// it's possible that the city did not perform a ranged attack this turn yet although enemies are present
		// this depends on the tactical posture ... so let's try again here as a safety net
		CvUnit* pEnemyPlot = pCity->getBestRangedStrikeTarget();
		if (pEnemyPlot)
			pCity->rangeStrike(pEnemyPlot->getX(), pEnemyPlot->getY());

		// PROACTIVE GARRISON: Check for enemy military buildup even if city isn't at border
		// Look at threat value and enemy aggressive posture to detect incoming attacks early
		int iThreatLevel = pCity->getThreatValue();
		bool bHighThreat = (iThreatLevel >= 50); // High threat from diplomacy/proximity
		bool bAggressiveNeighbor = false;
		bool bMemoryImminent = false;
		bool bMemorySiege = false;
		bool bMemoryBuildup = false;
		CvDiplomacyAI* pDiploAI = m_pPlayer->GetDiplomacyAI();
		
		// Check for aggressive posture and memory signals from nearby players
		// Memory signals are scoped per-city: only apply if the threatening player has forces
		// geographically relevant to THIS city (within ~15 tiles)
		for (int iPlayer = 0; iPlayer < MAX_CIV_PLAYERS; iPlayer++)
		{
			PlayerTypes eOther = (PlayerTypes)iPlayer;
			if (eOther == m_pPlayer->GetID() || !GET_PLAYER(eOther).isAlive() || GET_PLAYER(eOther).isMinorCiv())
				continue;
			
			// Only consider nearby civs for city-level garrison decisions
			if (GET_PLAYER(eOther).GetProximityToPlayer(m_pPlayer->GetID()) < PLAYER_PROXIMITY_CLOSE)
				continue;
			
			if (pDiploAI->GetMilitaryAggressivePosture(eOther) >= AGGRESSIVE_POSTURE_MEDIUM)
				bAggressiveNeighbor = true;

			// Memory signals only matter for this city if the enemy is geographically close
			CvCity* pEnemyNearCity = GET_PLAYER(eOther).GetClosestCity(pPlot, 15, false);
			if (pEnemyNearCity)
			{
				if (pDiploAI->IsAttackLikelyImminent(eOther))
					bMemoryImminent = true;
				if (pDiploAI->IsSiegeWarningActive(eOther))
					bMemorySiege = true;
				if (pDiploAI->IsPlayerBuildingUpNearUs(eOther))
					bMemoryBuildup = true;
			}

			if (bAggressiveNeighbor && bMemoryImminent && bMemorySiege && bMemoryBuildup)
				break;
		}
		
		// Count enemy units in wider area (proactive detection)
		int iNearbyEnemyUnits = 0;
		for (int i = RING3_PLOTS; i < RING5_PLOTS; i++)
		{
			CvPlot* pNearby = iterateRingPlots(pPlot, i);
			if (pNearby && pNearby->isVisible(m_pPlayer->getTeam()))
			{
				CvUnit* pUnit = pNearby->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true);
				if (pUnit && pUnit->IsCombatUnit())
					iNearbyEnemyUnits++;
			}
		}
		bool bEnemyBuildupNearby = (iNearbyEnemyUnits >= 3);
		
		// COASTAL THREAT: Check for naval threats to coastal cities
		bool bCoastalThreat = false;
		int iNearbyEnemyNaval = 0;
		if (pCity->isCoastal())
		{
			// Check water zone dominance
			CvTacticalDominanceZone* pWaterZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);
			if (pWaterZone)
			{
				// Enemy dominates our water zone or has significant naval presence
				if (pWaterZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
					bCoastalThreat = true;
				else if (pWaterZone->GetEnemyNavalUnitCount() > pWaterZone->GetFriendlyNavalUnitCount())
					bCoastalThreat = true;
			}
			
			// Also count enemy naval units in wider rings (proactive detection)
			for (int i = RING2_PLOTS; i < RING5_PLOTS; i++)
			{
				CvPlot* pNearby = iterateRingPlots(pPlot, i);
				if (pNearby && pNearby->isWater() && pNearby->isVisible(m_pPlayer->getTeam()))
				{
					CvUnit* pUnit = pNearby->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true);
					if (pUnit && pUnit->IsCombatUnit() && pUnit->getDomainType() == DOMAIN_SEA)
						iNearbyEnemyNaval++;
				}
			}
			if (iNearbyEnemyNaval >= 2)
				bCoastalThreat = true;
			
			// Blockaded = definitely under coastal threat
			if (pCity->GetCityCitizens()->AnyPlotBlockaded())
				bCoastalThreat = true;
		}

		// ignore core cities here (handled by homeland ai)
		// BUT proactively garrison if we detect emerging threat!
		bool bProactiveThreat = bHighThreat || bAggressiveNeighbor || bEnemyBuildupNearby || bCoastalThreat ||
			bMemoryImminent || bMemorySiege || bMemoryBuildup;

		// Hard capital garrison rule: the capital should not be skipped when it borders
		// or is exposed to an enemy at war. If the capital is deep inland with no enemy
		// border contact, the homeland AI can handle it normally.
		// Losing the capital is catastrophic (2x war value, +40 capitulation score).
		bool bIsCapitalAtWar = false;
		if (pCity->isCapital() && m_pPlayer->IsAtWarAnyMajor())
		{
			// Check if the capital borders or is exposed to any enemy we're at war with
			bIsCapitalAtWar = pCity->isBorderCity() || m_pPlayer->GetMilitaryAI()->IsExposedToEnemy(pCity, NO_PLAYER);
		}

		bool bBlockaded = pCity->GetCityCitizens()->AnyPlotBlockaded();
		bool bExposedToEnemy = m_pPlayer->GetMilitaryAI()->IsExposedToEnemy(pCity, NO_PLAYER);
		bool bSkipAsCoreCity = !bIsCapitalAtWar && !pCity->isBorderCity() && !bBlockaded && !bExposedToEnemy && !bProactiveThreat;

		// Multi-front wars are brittle: don't skip strategic interior cities as aggressively.
		// Keep tactical garrison coverage for floodgates/chokepoints/second-line cities even
		// when they are not currently border or exposed by the path model.
		if (bSkipAsCoreCity && bMultiFrontWar)
		{
			const CvStrategicGeographyMap* pGeoMap = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
			const StrategicCityAnalysis* pAnalysis = pGeoMap ? pGeoMap->GetCityAnalysis(pCity->GetID()) : NULL;
			bool bStrategicInterior = pAnalysis && (pAnalysis->bIsSecondLine || pAnalysis->bIsFloodgate || pAnalysis->bIsChokepointCity);

			// Slightly lower threshold in multi-front wars to react earlier to pressure shifts.
			if (bStrategicInterior || iThreatLevel >= 35 || bMemoryImminent || bMemorySiege || bMemoryBuildup)
				bSkipAsCoreCity = false;
		}

		// Strategic reserve cities (computed by MilitaryAI) should never be skipped.
		// These are second-line/floodgate/chokepoint cities identified as needing a
		// permanent garrison even when they are not on the current front line.
		if (bSkipAsCoreCity && m_pPlayer->GetMilitaryAI()->IsStrategicReserveCity(pCity->GetID()))
			bSkipAsCoreCity = false;

		if (bSkipAsCoreCity)
			continue;

		for (int iI = 0; iI < pPlot->getNumUnits(); iI++)
		{
			CvUnit* pUnit = pPlot->getUnitByIndex(iI);
			// Naval units in cities shouldn't stay idle
			if (pUnit->getDomainType() == DOMAIN_SEA)
			{
				if (pUnit->getOwner() != m_pPlayer->GetID())
					continue;

				m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
			}
		}

		//note that garrisons do not need to be "recruited" into tactical AI
		CvUnit* pGarrison = pCity->GetGarrisonedUnit();

		// Allow valid land or naval garrisons, but don't use recon units as garrisons
		if (pGarrison && (!pGarrison->CanGarrison() || pGarrison->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE))
			pGarrison = NULL;

		if (pGarrison)
		{
			if (pGarrison->CanUpgradeRightNow(false) && !pGarrison->IsHurt())
			{
				// Don't upgrade if we will go over supply
				if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pGarrison->isNoSupply())
				{
					CvUnit* pNewUnit = pGarrison->DoUpgrade();
					if (pNewUnit)
						UnitProcessed(pNewUnit->GetID());
				}
			}

			//sometimes we have an accidental garrison ...
			if (pGarrison->AI_getUnitAIType() == UNITAI_EXPLORE || pGarrison->isDelayedDeath() || pGarrison->TurnProcessed() || pGarrison->getArmyID()!=-1)
				continue;

			//first check how many enemies are around and look for vulnerable targets
			int iEnemyCount = 0;
			int iWoundedEnemyCount = 0;
			int iEnemyRangedCount = 0; // includes both siege and ranged units
			int iEstimatedRangedDamage = 0;
			for (int i = RING0_PLOTS; i < RING3_PLOTS; i++)
			{
				CvPlot* pNeighbor = iterateRingPlots(pPlot, i);
				if (pNeighbor && pNeighbor->isEnemyUnit(m_pPlayer->GetID(), true, true))
				{
					iEnemyCount++;
					CvUnit* pEnemy = pNeighbor->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), pGarrison, true);
					if (pEnemy)
					{
						if (pEnemy->IsHurt() && pEnemy->GetCurrHitPoints() < pEnemy->GetMaxHitPoints() / 2)
							iWoundedEnemyCount++;
						
						// Both siege and ranged units can attack the city/garrison without retaliation
						// Melee garrison must account for this incoming damage
						if (pEnemy->IsCanAttackRanged())
						{
							iEnemyRangedCount++;
							if (pEnemy->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
							{
								// Siege does heavy damage to cities/garrisons
								iEstimatedRangedDamage += 25;
							}
							else
							{
								// Regular ranged units do moderate damage
								iEstimatedRangedDamage += 15;
							}
						}
					}
				}
			}

			bool bCityInDanger = pCity->isInDangerOfFalling();
			bool bIsRangedGarrison = pGarrison->IsCanAttackRanged();
			bool bAllowLeaveCity = false;
			const CvMilitaryAI* pMilitaryAI = m_pPlayer->GetMilitaryAI();
			const CvStrategicGeographyMap* pGeoMap = pMilitaryAI ? pMilitaryAI->GetStrategicGeographyMap() : NULL;
			const StrategicCityAnalysis* pCityAnalysis = pGeoMap ? pGeoMap->GetCityAnalysis(pCity->GetID()) : NULL;
			bool bMustHoldStrategicCity = pCity->isCapital()
				|| (pMilitaryAI && pMilitaryAI->IsStrategicReserveCity(pCity->GetID()))
				|| (pCityAnalysis && (pCityAnalysis->bIsFloodgate || pCityAnalysis->bIsChokepointCity));

			if (bIsRangedGarrison)
			{
				// Ranged garrisons can attack safely without taking damage
				// Allow leaving city only if few enemies and not last city
				bAllowLeaveCity = (iEnemyCount < 2 && m_pPlayer->getNumCities() > 1 && !bMustHoldStrategicCity);
				TacticalAIHelpers::PerformOpportunityAttack(pGarrison, bAllowLeaveCity);
			}
			else
			{
				// Melee garrisons CAN attack from inside the city (adjacent enemies only)
				// They take damage but stay garrisoned if they kill or don't kill
				// This is valuable for finishing off wounded enemies!
				
				// IMPORTANT: Consider total risk = combat damage + incoming ranged/siege damage
				// If attacking would leave us too weak to survive bombardment, don't attack
				int iSafeHPThreshold = iEstimatedRangedDamage + 20; // buffer for survival
				bool bCanSafelyAttack = (pGarrison->GetCurrHitPoints() > iSafeHPThreshold);
				
				// Leaving the city is risky - it becomes vulnerable
				// Only leave if: few enemies, not in danger, not last city, and good opportunity
				bAllowLeaveCity = (iWoundedEnemyCount > 0 && iEnemyCount < 2 && !bCityInDanger && m_pPlayer->getNumCities() > 1 && !bMustHoldStrategicCity);
				
				if (bCityInDanger && m_pPlayer->getNumCities() > 1 && !bMustHoldStrategicCity)
				{
					// City is falling - consider escaping to preserve the unit
					// But first try attacking from garrison - might get a kill and survive
					// Only escape if there's actually a safe place to go
					CvPlot* pEscapePlot = TacticalAIHelpers::FindSafestPlotInReach(pGarrison, false, false).first;
					int iEscapeThreshold = bImminentAttack ? (pGarrison->GetCurrHitPoints() * 2 / 3) : (pGarrison->GetCurrHitPoints() / 2);
					if (pEscapePlot && pEscapePlot != pPlot && pGarrison->GetDanger(pEscapePlot) < iEscapeThreshold)
					{
						// Try attacking from garrison first - might get a valuable kill
						// Pass bAllowMovement=false so we stay garrisoned
						bool bAttacked = false;
						if (!bImminentAttack)
							bAttacked = TacticalAIHelpers::PerformOpportunityAttack(pGarrison, false);
						
						// If we didn't attack or still have moves, escape
						if (pGarrison->canMove())
						{
							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("Melee garrison %d escaping from falling city %s to (%d,%d)%s",
									pGarrison->GetID(), pCity->getName().GetCString(), pEscapePlot->getX(), pEscapePlot->getY(),
									bAttacked ? " after attacking" : "");
								LogTacticalMessage(strLogString);
							}
							ExecuteMoveToPlot(pGarrison, pEscapePlot);
							UnitProcessed(pGarrison->GetID());
							continue;
						}
					}
					else
					{
						// No safe escape - attack from garrison if possible (last stand)
						TacticalAIHelpers::PerformOpportunityAttack(pGarrison, false);
					}
				}
				else if (bCanSafelyAttack || iEnemyRangedCount == 0)
				{
					// City not in immediate danger AND either:
					// - No ranged/siege threat, so safe to attack from garrison
					// - We have enough HP to survive combat + ranged bombardment
					// Allow attacking FROM the garrison (bAllowMovement=false means adjacent only, stays garrisoned)
					// But only allow LEAVING the garrison if conditions are right
					TacticalAIHelpers::PerformOpportunityAttack(pGarrison, bAllowLeaveCity);
				}
				else
				{
					// Ranged/siege threat present and attacking would leave us too weak
					// Skip attacking to preserve HP for surviving bombardment
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Melee garrison %d in %s skipping attack - %d ranged units threaten ~%d damage, HP=%d",
							pGarrison->GetID(), pCity->getName().GetCString(), iEnemyRangedCount, iEstimatedRangedDamage, pGarrison->GetCurrHitPoints());
						LogTacticalMessage(strLogString);
					}
				}
			}

			//no need to call SetProcessed() because the unit was never in currentTurnUnits
			//do not call finishMoves() else the garrison will not heal!
			pGarrison->PushMission(CvTypes::getMISSION_SKIP());
			pGarrison->SetTurnProcessed(true);

			//don't try to find a better garrison while under siege
			if (pCity->isUnderSiege())
				continue;
		}

		//prefer ranged land units as garrisons ...
		bool bWantGarrison = (pGarrison == NULL || !pGarrison->isNativeDomain(pPlot) || !pGarrison->IsCanAttackRanged() || pGarrison->GetRange() < 2);
		
		// PROACTIVE: Also want garrison if we detect emerging threat even if current garrison is OK
		if (!bWantGarrison && bProactiveThreat && pGarrison == NULL)
			bWantGarrison = true;

		// Capital at war ALWAYS wants a ranged garrison — a melee garrison can't counter enemy siege
		if (!bWantGarrison && bIsCapitalAtWar)
			bWantGarrison = true;
		
		if ( bWantGarrison && (pCity->NeedsGarrison() || bProactiveThreat || bIsCapitalAtWar) )
		{
			// Use longer search range for cities with detected threat
			// This allows reinforcing a city before the attack arrives
			int iSearchRange = iNumTurnsAway;
			if (bProactiveThreat && !pCity->HasGarrison())
			{
				// Higher urgency = willing to pull from further away
				if (bEnemyBuildupNearby)
					iSearchRange = max(iNumTurnsAway, 4); // Enemy already massing - high urgency
				else if (bCoastalThreat)
					iSearchRange = max(iNumTurnsAway, 4); // Naval threat to coastal city - high urgency
				else if (bAggressiveNeighbor)
					iSearchRange = max(iNumTurnsAway, 3); // Aggressive posture - moderate urgency
				else if (bHighThreat)
					iSearchRange = max(iNumTurnsAway, 3); // High threat level - moderate urgency
				
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Proactive garrison for %s: threat=%d, aggressive=%d, buildup=%d, coastal=%d (naval=%d), search=%d turns",
						pCity->getName().GetCString(), iThreatLevel, bAggressiveNeighbor ? 1 : 0, 
						iNearbyEnemyUnits, bCoastalThreat ? 1 : 0, iNearbyEnemyNaval, iSearchRange);
					LogTacticalMessage(strLogString);
				}
			}
			
			// Grab units that make sense for this move type
			CvUnit* pUnit = FindUnitForThisMove(AI_TACTICAL_GARRISON, pPlot, iSearchRange);
			bool bNeedEmergencyParadrop = false;
			if (pUnit)
			{
				//move out old garrison if necessary
				if (pGarrison && pUnit->CanSafelyReachInXTurns(pPlot, 0))
				{
					CvPlot* pFreePlot = pCity->GetPlotForNewUnit(pGarrison->getUnitType(), false);
					if (pFreePlot)
						//do not set it processed, it should be considered for other tactical moves later
						ExecuteMoveToPlot(pGarrison, pFreePlot, false);
				}

				//if the old garrison did not move out this will fail (possibly also for other reasons)
				// Use danger-aware pathing: prefer safe routes (roads) over short ones through
				// enemy-adjacent terrain. This prevents reinforcements from walking through
				// dangerous forest/hill tiles near enemy forces when a longer road exists.
				int iGarrisonMoveFlags = CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER;
				int iTurnsLeft = ExecuteMoveToPlot(pUnit, pPlot, true, iGarrisonMoveFlags);
				bool bHeadingToGarrison = (iTurnsLeft != INT_MAX) && (pUnit->GetMissionAIPlot() == pPlot);

				//if everything went according to plan ...
				if (bHeadingToGarrison)
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Unit %d, moving to garrison, X: %d, Y: %d, Priority: %d, Turns Away: %d", 
							pUnit->GetID(), pTarget->GetTargetX(), pTarget->GetTargetY(), pTarget->GetAuxIntData(), iTurnsLeft);
						LogTacticalMessage(strLogString);
					}

					//just in case we're doing this with enemy arounds and have movement left
					TacticalAIHelpers::PerformOpportunityAttack(pUnit);
				}
				else
				{
					bNeedEmergencyParadrop = true;
				}
			}
			// No ground unit available in range - try paradrop reinforcement for urgent cases
			// Paratroopers can drop directly into a city that desperately needs a garrison
			if ((bNeedEmergencyParadrop || !pUnit) && (pCity->isInDangerOfFalling() || (pCity->NeedsGarrison() && bProactiveThreat)))
			{
				if (FindParatroopersWithinStrikingDistance(pPlot, false)) // Don't check danger - we WANT to drop into danger
				{
					// Find a paratrooper that can drop into the city
					for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
					{
						CvUnit* pParatrooper = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
						if (!pParatrooper)
							continue;

						// Execute the paradrop
						pParatrooper->PushMission(CvTypes::getMISSION_PARADROP(), pPlot->getX(), pPlot->getY());

						if (GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Paratrooper %d paradropping to reinforce threatened city %s, X: %d, Y: %d, InDanger: %d",
								pParatrooper->GetID(), pCity->getName().GetCString(), pPlot->getX(), pPlot->getY(),
								pCity->isInDangerOfFalling() ? 1 : 0);
							LogTacticalMessage(strLogString);
						}

						UnitProcessed(pParatrooper->GetID());
						break; // Only need one paratrooper
					}
				}
			}
		}
	}

	for (size_t iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if (!pUnit)
			continue;

		bool bUpgraded = false;
		if (pUnit->CanUpgradeRightNow(false) && !pUnit->IsHurt())
		{
			// Don't upgrade if we will go over supply
			if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pUnit->isNoSupply())
			{
				CvUnit* pNewUnit = pUnit->DoUpgrade();
				if (pNewUnit)
				{
					bUpgraded = true;
					UnitProcessed(pNewUnit->GetID());
				}
			}
		}

		if (!bUpgraded)
			TacticalAIHelpers::PerformOpportunityAttack(pUnit, true);
	}
}

/// Establish a defensive bastion adjacent to a city
void CvTacticalAI::PlotBastionMoves(int iNumTurnsAway)
{
	ClearCurrentMoveUnits(AI_TACTICAL_GUARD);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_DEFENSIVE_BASTION); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		if (!TacticalAIHelpers::IsCloseToContestedBorder(m_pPlayer,pPlot))
			continue;

		// Grab units that make sense for this move type
		CvUnit* pUnit = FindUnitForThisMove(AI_TACTICAL_GUARD, pPlot, iNumTurnsAway);

		//move may fail if the plot is already occupied (can happen if another unit moved there during this turn)
		if (pUnit && ExecuteMoveToPlot(pUnit, pPlot, true, CvUnit::MOVEFLAG_SAFE_EMBARK_ONLY)==0 && pUnit->plot() == pPlot)
		{
			if (pUnit->CanUpgradeRightNow(false) && !pUnit->IsHurt())
			{
				// Don't upgrade if we will go over supply
				if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pUnit->isNoSupply())
				{
					CvUnit* pNewUnit = pUnit->DoUpgrade();
					if (pNewUnit)
						UnitProcessed(pNewUnit->GetID());
				}
			}
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Bastion, X: %d, Y: %d, Priority: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), pTarget->GetAuxIntData(), iNumTurnsAway);
				LogTacticalMessage(strLogString);
			}
		}
	}
}

/// Make a defensive move to guard an improvement
void CvTacticalAI::PlotGuardImprovementMoves(int iNumTurnsAway)
{
	ClearCurrentMoveUnits(AI_TACTICAL_GUARD);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_IMPROVEMENT_TO_DEFEND); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		// Grab units that make sense for this move type
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
		CvUnit* pUnit = FindUnitForThisMove(AI_TACTICAL_GUARD, pPlot, iNumTurnsAway);

		//move may fail if the plot is already occupied (can happen if another unit moved there during this turn)
		if (pUnit && ExecuteMoveToPlot(pUnit, pPlot, true, CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER)==0 && pUnit->plot() == pPlot)
		{
			if (pUnit->CanUpgradeRightNow(false) && !pUnit->IsHurt())
			{
				// Don't upgrade if we will go over supply
				if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pUnit->isNoSupply())
				{
					CvUnit* pNewUnit = pUnit->DoUpgrade();
					if (pNewUnit)
						UnitProcessed(pNewUnit->GetID());
				}
			}
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Guard Improvement, X: %d, Y: %d, Turns Away: %d", pTarget->GetTargetX(), pTarget->GetTargetY(), iNumTurnsAway);
				LogTacticalMessage(strLogString);
			}
		}
	}
}

/// Set fighters to intercept
void CvTacticalAI::PlotAirPatrolMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_AIRPATROL);
	std::vector<CvPlot*> checkedPlotList;
	
	// COMBINED ARMS AIR DEFENSE: Track cities under threat for priority interception
	std::vector<CvCity*> vThreatenedCities;
	int iCityLoop = 0;
	for (CvCity* pCity = m_pPlayer->firstCity(&iCityLoop); pCity != NULL; pCity = m_pPlayer->nextCity(&iCityLoop))
	{
		// Check if city is under siege or threatened
		bool bThreatened = pCity->isUnderSiege() || pCity->getDamage() > 0 || pCity->isInDangerOfFalling();
		if (!bThreatened)
		{
			// Check for nearby enemy air units that could attack this city
			int iEnemyAir = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pCity->plot(), 10, true, true);
			if (iEnemyAir > 0)
				bThreatened = true;
		}
		
		if (bThreatened)
			vThreatenedCities.push_back(pCity);
	}

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->getDomainType()==DOMAIN_AIR && pUnit->canUseForTacticalAI())
		{
			// Am I eligible to intercept? We only commandeered units which won't be rebased
			if(pUnit->canAirPatrol(NULL))
			{
				CvPlot* pUnitPlot = pUnit->plot();
				int iNumNearbyBombers = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), false/*bCountFighters*/, true/*bCountBombers*/);
				int iNumNearbyFighters = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), true/*bCountFighters*/, false/*bCountBombers*/);
				int iNumPlotNumAlreadySet = std::count(checkedPlotList.begin(), checkedPlotList.end(), pUnitPlot);

				// To at least intercept once if only one bomber found.
				if (iNumNearbyBombers == 1)
					iNumNearbyBombers++;
				
				// CITY DEFENSE COORDINATION: Boost interception priority for fighters at threatened cities
				// This ensures air cover prioritizes protecting cities under siege
				bool bProtectingThreatenedCity = false;
				for (size_t i = 0; i < vThreatenedCities.size(); i++)
				{
					CvCity* pThreatCity = vThreatenedCities[i];
					// Fighter can protect this city if based there or within interception range
					if (pUnitPlot->getPlotCity() == pThreatCity ||
						plotDistance(*pUnitPlot, *pThreatCity->plot()) <= pUnit->GetAirInterceptRange())
					{
						bProtectingThreatenedCity = true;
						
						// Extra interceptors wanted for threatened cities
						if (pThreatCity->isUnderSiege())
							iNumNearbyBombers += 3; // More air cover for cities under siege
						else if (pThreatCity->getDamage() > 0)
							iNumNearbyBombers += 2; // Damaged city needs protection
						else
							iNumNearbyBombers += 1; // General threat
						break;
					}
				}

				// TODO: we should not just use any interceptor but the best one (depending on promotions etc)
				int maxInterceptorsWanted = (iNumNearbyBombers / 2) + (iNumNearbyFighters / 4);
				
				// Minimum interceptors for locations protecting threatened cities
				if (bProtectingThreatenedCity && maxInterceptorsWanted < 2)
					maxInterceptorsWanted = 2;
				
				if (iNumPlotNumAlreadySet < maxInterceptorsWanted)
				{
					checkedPlotList.push_back(pUnitPlot);
					m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
				}
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteAirPatrolMoves();
	}
}

/// Spend money to buy defenses
void CvTacticalAI::PlotEmergencyPurchases(CvTacticalDominanceZone* pZone)
{
	if(!pZone)
		return;

	CvCity* pCity = pZone->GetZoneCity();
	if (!pCity || pCity->getOwner() != m_pPlayer->GetID())
		return;

	// Don't waste money if there's no hope
	if (pCity->isInDangerOfFalling())
		return;

	// Sometimes buying a unit is useless
	bool bWantUnits = true;
	if (MOD_BALANCE_PURCHASED_UNIT_DAMAGE && pCity->getDamage() * 2 > pCity->GetMaxHitPoints())
		bWantUnits = false;

	// Handle water zones - proactive naval defense buildup
	if (pZone->IsWater())
	{
		if (!pCity->isCoastal())
			return;
		
		// Check for naval threats
		bool bEnemyNavalPresence = (pZone->GetEnemyNavalUnitCount() > 0);
		bool bEnemyDominated = (pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY);
		bool bBlockaded = pCity->GetCityCitizens()->AnyPlotBlockaded();
		
		// PROACTIVE: Also check for aggressive naval neighbors
		bool bNavalThreatDetected = false;
		for (int iPlayer = 0; iPlayer < MAX_CIV_PLAYERS && !bNavalThreatDetected; iPlayer++)
		{
			PlayerTypes eOther = (PlayerTypes)iPlayer;
			if (eOther == m_pPlayer->GetID() || !GET_PLAYER(eOther).isAlive())
				continue;
			
			// At war with them?
			if (!m_pPlayer->IsAtWarWith(eOther))
				continue;
			
			// Do they have naval strength nearby?
			CvTacticalDominanceZone* pEnemyZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);
			if (pEnemyZone && pEnemyZone->GetEnemyNavalStrength() > pEnemyZone->GetFriendlyNavalStrength())
				bNavalThreatDetected = true;
		}
		
		// Count enemy naval units in wider area (proactive detection) - range 3-5 tiles out
		int iNearbyEnemyNaval = 0;
		CvPlot* pCityPlot = pCity->plot();
		for (int i = RING3_PLOTS; i < RING5_PLOTS; i++)
		{
			CvPlot* pNearby = iterateRingPlots(pCityPlot, i);
			if (pNearby && pNearby->isWater() && pNearby->isVisible(m_pPlayer->getTeam()))
			{
				CvUnit* pUnit = pNearby->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true);
				if (pUnit && pUnit->IsCombatUnit() && pUnit->getDomainType() == DOMAIN_SEA)
					iNearbyEnemyNaval++;
			}
		}
		bool bEnemyNavalBuildupNearby = (iNearbyEnemyNaval >= 2);
		
		// Buy naval defenders if threatened
		if ((bEnemyNavalPresence || bEnemyDominated || bBlockaded || bNavalThreatDetected || bEnemyNavalBuildupNearby) && bWantUnits)
		{
			if (!MOD_BALANCE_UNIT_INVESTMENTS)
			{
				// Buy naval melee (can capture cities) if we don't have naval presence
				if (bBlockaded || pZone->GetFriendlyNavalUnitCount() < pZone->GetEnemyNavalUnitCount() + 1)
				{
					m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_ATTACK_SEA, pCity);
					
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Proactive naval defense: Buying ATTACK_SEA for %s (enemy naval: %d, friendly: %d)%s%s%s",
							pCity->getName().GetCString(), pZone->GetEnemyNavalUnitCount(), pZone->GetFriendlyNavalUnitCount(),
							bBlockaded ? " [BLOCKADED]" : "", bEnemyNavalBuildupNearby ? " [BUILDUP]" : "",
							bNavalThreatDetected ? " [THREAT]" : "");
						LogTacticalMessage(strLogString);
					}
				}
			}
		}
		return;
	}

	// Land zone handling (original code)
	// If we need additional units - ignore the supply limit here, we're probably losing units anyway
	if (pZone->GetOverallDominanceFlag()>TACTICAL_DOMINANCE_FRIENDLY || pCity->isUnderSiege())
	{
		if (!MOD_BALANCE_BUILDING_INVESTMENTS)
			m_pPlayer->GetMilitaryAI()->BuyEmergencyBuilding(pCity);

		if (!MOD_BALANCE_UNIT_INVESTMENTS)
		{
			//only buy ranged if there's no garrison
			//otherwise it will be placed outside of the city and most probably die instantly
			if (!pCity->HasGarrison())
				m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_RANGED, pCity);
			else if (bWantUnits)
			{
				//buy defensive land units
				if (!MOD_AI_UNIT_PRODUCTION)
					m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(GC.getGame().randRangeExclusive(0, 5, CvSeeder(pCity->plot()->GetPseudoRandomSeed())) < 2 ? UNITAI_COUNTER : UNITAI_DEFENSE, pCity);
				else //AI Unit Production : Counter is AA only now
					m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_DEFENSE, pCity);
			}
		}
	}
	
	// ALSO check if this land zone city needs naval defense (coastal city with naval threat)
	if (pCity->isCoastal() && bWantUnits && !MOD_BALANCE_UNIT_INVESTMENTS)
	{
		CvTacticalDominanceZone* pWaterZone = GetTacticalAnalysisMap()->GetZoneByCity(pCity, true);
		if (pWaterZone)
		{
			bool bNavalThreat = (pWaterZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY) ||
				(pWaterZone->GetEnemyNavalUnitCount() > pWaterZone->GetFriendlyNavalUnitCount());
			bool bBlockaded = pCity->GetCityCitizens()->AnyPlotBlockaded();
			
			if ((bNavalThreat || bBlockaded) && (bBlockaded || pWaterZone->GetFriendlyNavalUnitCount() < 2))
			{
				m_pPlayer->GetMilitaryAI()->BuyEmergencyUnit(UNITAI_ATTACK_SEA, pCity);
				
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Naval defense for land zone: Buying ATTACK_SEA for coastal city %s%s",
						pCity->getName().GetCString(), bBlockaded ? " [BLOCKADED]" : "");
					LogTacticalMessage(strLogString);
				}
			}
		}
	}
}

/// Move naval units over top of unprotected embarked units
void CvTacticalAI::PlotNavalEscortMoves()
{
	ClearCurrentMoveUnits(AI_TACTICAL_ESCORT);

	std::vector<CvUnit*> vTargetUnits;
	int iLoop = 0;

	// Build set of units already in a pending convoy (skip those)
	const CvStrategicGeographyMap* pGeo = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
	std::set<int> convoyUnits;
	if (pGeo)
	{
		const std::vector<PendingTransit>& vTransits = pGeo->GetPendingTransits();
		for (size_t i = 0; i < vTransits.size(); i++)
		{
			if (vTransits[i].eRisk == TRANSIT_RISK_MEDIUM || vTransits[i].eRisk == TRANSIT_RISK_HIGH)
				convoyUnits.insert(vTransits[i].iUnitID);
		}
	}

	// Collect embarked units NOT in a convoy
	for(CvUnit* pLoopUnit = m_pPlayer->firstUnit(&iLoop); pLoopUnit != NULL; pLoopUnit = m_pPlayer->nextUnit(&iLoop))
	{
		if (pLoopUnit->isEmbarked() && convoyUnits.find(pLoopUnit->GetID()) == convoyUnits.end())
			vTargetUnits.push_back(pLoopUnit);
	}

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->canUseForTacticalAI() && !pUnit->shouldHeal(false))
		{
			// Am I a naval combat unit?
			if(pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCanAttack())
			{
				//any embarked unit close by?
				int iMaxDist = pUnit->getMoves();
				for (size_t i=0; i<vTargetUnits.size(); i++)
				{
					if (plotDistance(*pUnit->plot(),*vTargetUnits[i]->plot())<=iMaxDist)
					{
						m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
						break;
					}
				}
			}
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		ExecuteEscortEmbarkedMoves(vTargetUnits);
	}
}

// PLOT MOVES FOR ZONE TACTICAL POSTURES

/// Win an attrition campaign with bombardments
void CvTacticalAI::PlotAttritionAttacks(CvTacticalDominanceZone* pZone)
{
	(void)pZone; //unused but can be inspected
	ClearCurrentMoveUnits(AI_TACTICAL_ATTRITION);

	//todo: the targets are sorted in a very rough "how bad can they hit us" order
	//but we should probably sort them in a "how bad can we hit them" order
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_LOW); pTarget!=NULL; pTarget = GetNextZoneTarget(AL_LOW))
		ExecuteDestroyEnemyUnits(*pTarget,AL_LOW);

	//don't expose our units for city attacks here; if we are likely to succeed we don't call PlotAttritionAttacks
}

/// Defeat enemy units by using our advantage in numbers
void CvTacticalAI::PlotExploitFlanksMoves(CvTacticalDominanceZone* pZone)
{
	(void)pZone; //unused but can be inspected
	ClearCurrentMoveUnits(AI_TACTICAL_FLANKATTACK);

	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_MEDIUM); pTarget!=NULL; pTarget = GetNextZoneTarget(AL_MEDIUM))
		ExecuteDestroyEnemyUnits(*pTarget, AL_MEDIUM);

	//just in case there is a city ... it can happen that the city is wide open and the defenders are on another island
	ExecuteCaptureCityMoves();
}

/// We have more overall strength than enemy, defeat his army first
void CvTacticalAI::PlotSteamrollMoves(CvTacticalDominanceZone* pZone)
{
	(void)pZone; //unused but can be inspected
	ClearCurrentMoveUnits(AI_TACTICAL_STEAMROLL);

	// See if there are any kill attacks we can make.
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_HIGH); pTarget != NULL; pTarget = GetNextZoneTarget(AL_HIGH))
		ExecuteDestroyEnemyUnits(*pTarget, AL_HIGH);

	// Now go after the city
	ExecuteCaptureCityMoves();
}

/// We should be strong enough to take out the city before the enemy can whittle us down with ranged attacks
void CvTacticalAI::PlotSurgicalCityStrikeMoves(CvTacticalDominanceZone* pZone)
{
	(void)pZone; //unused but can be inspected
	ClearCurrentMoveUnits(AI_TACTICAL_SURGICAL_STRIKE);

	// Attack the city first
	ExecuteCaptureCityMoves();

	// Take any other really good attacks we've set up
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_MEDIUM); pTarget != NULL; pTarget = GetNextZoneTarget(AL_MEDIUM))
		ExecuteDestroyEnemyUnits(*pTarget, AL_MEDIUM);
}

/// Build a defensive shell around this city
void CvTacticalAI::PlotHedgehogMoves(CvTacticalDominanceZone* pZone)
{
	ClearCurrentMoveUnits(AI_TACTICAL_HEDGEHOG);

	// Be careful with our units, we don't have so many
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_LOW); pTarget != NULL; pTarget = GetNextZoneTarget(AL_LOW))
		ExecuteDestroyEnemyUnits(*pTarget, AL_LOW);

	// exception : early reinforcement before attacks in other zones are considered
	PlotReinforcementMoves(pZone);
}

/// Try to push back the invader
void CvTacticalAI::PlotCounterattackMoves(CvTacticalDominanceZone* pZone)
{
	(void)pZone; //unused but can be inspected
	ClearCurrentMoveUnits(AI_TACTICAL_COUNTERATTACK);

	// Attack priority unit targets
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT, AL_MEDIUM); pTarget != NULL; pTarget = GetNextZoneTarget(AL_MEDIUM))
		ExecuteDestroyEnemyUnits(*pTarget, AL_MEDIUM);

}

/// Withdraw out of current dominance zone
void CvTacticalAI::PlotWithdrawMoves(CvTacticalDominanceZone* pZone)
{
	if (!pZone)
		return;

	ClearCurrentMoveUnits(AI_TACTICAL_WITHDRAW);

	// Loop through all recruited units
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->canUseForTacticalAI())
		{
			// Recent, healthy deployments need to attack!
			if (pUnit->IsRecentlyDeployedFromOperation() && (pUnit->GetCurrHitPoints() > pUnit->GetMaxHitPoints()/2))
				continue;

			// Am I in the current dominance zone?
			// Units in other dominance zones need to fend for themselves, depending on their own posture
			CvTacticalDominanceZone* pUnitZone = GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->plot());
			if (pUnitZone != pZone)
				continue;

			// However, zones might overlap borders, so double check that we don't give up our border forts
			if (pZone->GetTerritoryType() != TACTICAL_TERRITORY_FRIENDLY)
			{
				if (TacticalAIHelpers::IsPlayerCitadel(pUnit->plot(), pUnit->getOwner()) && pUnit->getDomainType() == DOMAIN_LAND)
					continue;

				if (pUnit->plot()->IsFriendlyTerritory(pUnit->getOwner()) && !pUnit->plot()->IsAdjacentOwnedByEnemy(pUnit->getTeam()))
					continue;
			}

			m_CurrentMoveUnits.push_back(CvTacticalUnit(pUnit->GetID()));
			//we will later sort by "attack strength" so fake it
			//ranged/slow units should retreat first, ie have a high strength
			int iFakeStrength = (pUnit->IsCanAttackRanged() ? 30 : 20) - pUnit->baseMoves(false);
			m_CurrentMoveUnits.back().SetAttackStrength(iFakeStrength);
			m_CurrentMoveUnits.back().SetHealthPercent(1, 1);
		}
	}

	if(m_CurrentMoveUnits.size() > 0)
	{
		//melee units should retreat last
		std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());
		ExecuteWithdrawMoves();
	}
}

/// Close units in on primary target of this dominance zone
void CvTacticalAI::PlotReinforcementMoves(CvTacticalDominanceZone* pTargetZone)
{
	ClearCurrentMoveUnits(AI_TACTICAL_REINFORCE);

	//sometimes there is nothing to do ...
	if (!pTargetZone || pTargetZone->GetPosture() == TACTICAL_POSTURE_WITHDRAW)
		return;

	//don't try to reinforce wilderness zones
	CvCity* pZoneCity = pTargetZone->GetZoneCity();
	if (!pZoneCity)
		return;
	bool bPreposition = false;
	if (pZoneCity->getTeam() == m_pPlayer->getTeam() && m_bImminentAttack)
	{
		if (pZoneCity->isBorderCity() || m_pPlayer->GetMilitaryAI()->IsExposedToEnemy(pZoneCity, NO_PLAYER))
			bPreposition = true;
	}
	
	//don't try to reinforce neutral zones if there are no enemies
	if (pZoneCity->getTeam() != m_pPlayer->getTeam() && pTargetZone->GetTotalEnemyUnitCount()==0)
		return;

	//sometimes we do not need further reinforcement - should we check whether we still need siege units specifically?
	bool bNeedMeleeOnly = false;
	if (pTargetZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY && pTargetZone->GetRangedDominanceFlag(100) == TACTICAL_DOMINANCE_FRIENDLY)
	{
		if (pTargetZone->GetFriendlyMeleeStrength() == 0) //get in melee units to capture a city!
			bNeedMeleeOnly = true;
		else if (!bPreposition)
			return;
	}

	//sometimes it's pointless, too far out
	if (!pTargetZone->HasNeighborZone(m_pPlayer->GetID()) && pTargetZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
		return;

	// we want units which are somewhat close (so we don't deplete other combat zones) 
	// do not set a player - that way we can traverse unrevealed plots and foreign territory
	SPathFinderUserData data(NO_PLAYER, PT_ARMY_MIXED, NO_PLAYER, GetRecruitRange());
	CvPlot* pTargetPlot = pZoneCity->plot();

	ReachablePlots relevantPlots = GC.GetStepFinder().GetPlotsInReach(pTargetPlot, data);

	int iMoveUnitsAlreadyInZone = 0;
	for (ReachablePlots::const_iterator it = relevantPlots.begin(); it != relevantPlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked( it->iPlotIndex );
		for (int i = 0; i < pPlot->getNumUnits(); i++)
		{
			CvUnit* pUnit = pPlot->getUnitByIndex(i);
			if (pUnit->getOwner()==m_pPlayer->GetID() && pUnit->canUseForTacticalAI())
			{
				CvTacticalDominanceZone* pUnitZone = GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->plot());
				if (pUnitZone && pUnitZone != pTargetZone)
				{
					//we should not pull units from zones which need them
					if (pUnitZone->GetOverallDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY && pUnitZone->GetPosture() != TACTICAL_POSTURE_WITHDRAW)
						if (!pPlot->isCity() || pPlot->getPlotCity()->isInDangerOfFalling() || pUnit->getDomainType() == DOMAIN_SEA)
							if (!pUnit->isEmbarked()) //cannot fight if embarked, so we can take it!
								continue;
				}

				// Skip ranged if we only need melee
				if (bNeedMeleeOnly && pUnit->IsCanAttackRanged())
					continue;

				// Carriers have special moves
				if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
					continue;

				// Do not move siege units into enemy dominated zones ... wait until we have some cover!
				if (pUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD && pTargetZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
					continue;

				//don't send generals / admirals across the map in an uncontrolled and potentially unescorted manner
				//it's enough if we recruit them for armies or combat sim
				if (pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || (pUnit->IsSapper() && pUnit->IsCivilianUnit()))
					continue;

				//conversely, don't leave generals out in the cold
				if (pUnit->IsCoveringFriendlyCivilian())
				{
					//a bit weird to just push a mission and not make an explicit plot/execute pair for this but should be ok
					pUnit->PushMission(CvTypes::getMISSION_SKIP());
					UnitProcessed(pUnit->GetID());
					continue;
				}

				// Proper domain of unit?
				// Note that coastal cities have two zones, so we will call this method twice
				if ((pTargetZone->IsWater() && pUnit->getDomainType() == DOMAIN_SEA) ||
					(!pTargetZone->IsWater() && pUnit->getDomainType() == DOMAIN_LAND))
				{
					// don't use near-dead units to attack ... misuse the flag here to be more careful when attacking
					if (pUnit->shouldHeal(pTargetZone->GetTerritoryType() == TACTICAL_TERRITORY_FRIENDLY))
						continue;

					//don't run away if there's other work to do (will eventually be handled by ExecuteAttackWithUnits)
					bool bHaveFarTarget = false;
					vector<pair<CvPlot*,bool>> altTargets = TacticalAIHelpers::GetTargetsInRange(pUnit, false, false);
					for (size_t j = 0; j < altTargets.size(); j++)
						bHaveFarTarget |= (plotDistance(*altTargets[j].first, *pTargetPlot) > TACTICAL_COMBAT_MAX_TARGET_DISTANCE);

					if (bHaveFarTarget)
						continue;

					CvTacticalUnit unit(pUnit->GetID());
					unit.SetMovesToTarget(it->iPathLength);
					m_CurrentMoveUnits.push_back(unit);

					if (m_pPlayer->GetTacticalAI()->GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->plot()) == pTargetZone)
						iMoveUnitsAlreadyInZone++;
				}
			}
		}
	}

	if (m_CurrentMoveUnits.size() > 0)
	{
		vector<CvUnit*> vUnits;
		int iAvailableMoveUnitsAlreadyInZone = 0;
		for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit( m_CurrentMoveUnits[i].GetID() );
			if (pUnit->canUseNow())
			{
				vUnits.push_back(pUnit);
				if (m_pPlayer->GetTacticalAI()->GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->plot()) == pTargetZone)
					iAvailableMoveUnitsAlreadyInZone++;
			}
		}

		//if we only have a single unit to work with in total, this is a case for pillage moves or the like
		if (pTargetZone->GetTotalFriendlyUnitCount() + (int)vUnits.size() - iAvailableMoveUnitsAlreadyInZone < 2)
			return;

		PositionUnitsAroundTarget(vUnits,pTargetPlot);
	}
}

/// Log that we couldn't find assignments for some units
void CvTacticalAI::ReviewUnassignedUnits()
{
	// Loop through all remaining units.
	// Do not call UnitProcessed() from here as it may invalidate our iterator
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->canUseForTacticalAI())
		{
			//don't overwrite army moves ... everything else is fair game
			if (pUnit->getArmyID()==-1)
				pUnit->setTacticalMove(AI_TACTICAL_UNASSIGNED);

			//there shouldn't be any danger but just in case
			CvPlot* pSafePlot = pUnit->GetDanger() > pUnit->ActualHealRate(pUnit->plot()) ? TacticalAIHelpers::FindSafestPlotInReach(pUnit, true).first : NULL;
			if (pSafePlot)
			{
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pSafePlot->getX(), pSafePlot->getY());

				if (pUnit->CanUpgradeRightNow(false) && !pUnit->IsHurt())
				{
					// Don't upgrade if we will go over supply
					if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pUnit->isNoSupply())
					{
						CvUnit* pNewUnit = pUnit->DoUpgrade();
						if (pNewUnit)
						{
							pNewUnit->SetTurnProcessed(true);
						}
					}
				}

				if (!pUnit->canMove())
					pUnit->SetTurnProcessed(true);
			}

			//do not skip the turn, finish moves, set the turn processed flag for units which we didn't move
			//homeland AI will take care of them!
		}
	}

	//fallback - if a city can bombard a unit but the corresponding target was suppressed, we do the attack here
	int iLoop;
	for (CvCity* pLoopCity = m_pPlayer->firstCity(&iLoop); pLoopCity != NULL; pLoopCity = m_pPlayer->nextCity(&iLoop))
	{
		CvUnit* pTarget = pLoopCity->getBestRangedStrikeTarget();
		if (pTarget)
			pLoopCity->doTask(TASK_RANGED_ATTACK, pTarget->getX(), pTarget->getY(), false);
	}
}

// OPERATIONAL AI SUPPORT FUNCTIONS

CvUnit* SwitchEscort(CvUnit* pCivilian, CvPlot* pNewEscortPlot, CvUnit* pEscort, CvArmyAI* pThisArmy)
{
	CvUnit* pPlotDefender = pNewEscortPlot->getBestDefender(pCivilian->getOwner());

	//Maybe we just make this guy our new escort, eh?
	if(pPlotDefender && pPlotDefender->getArmyID() == -1 && pPlotDefender->getDomainType() == pCivilian->getDomainType() && pPlotDefender->AI_getUnitAIType() != UNITAI_CITY_BOMBARD)
	{
		int iSlot = pThisArmy->RemoveUnit(pEscort->GetID(),true);
		if (iSlot>=0)
		{
			pThisArmy->AddUnit(pPlotDefender->GetID(), iSlot, pThisArmy->GetSlotInfo(iSlot).m_requiredSlot);
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("SingleHexOperationMoves: Switched escort to get things going.");
				GET_PLAYER(pCivilian->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
			}

			return pPlotDefender;
		}
		else
			CUSTOMLOG("SwitchEscort: Failed to remove unit from army!");
	}

	return NULL;
}

/// Move a single stack (civilian plus escort) to its destination
void CvTacticalAI::PlotArmyMovesEscort(CvArmyAI* pThisArmy)
{
	if (!pThisArmy)
		return;

	CvAIOperation* pOperation = GET_PLAYER(pThisArmy->GetOwner()).getAIOperation(pThisArmy->GetOperationID());
	if (!pOperation)
		return;

	//the unit to be escorted is always the first one
	CvUnit* pCivilian = pThisArmy->GetFirstUnit();
	//the second unit would be the first escort
	CvUnit* pEscort = pThisArmy->GetNextUnit(pCivilian);
	//additional escorts
	std::vector<CvUnit*> vExtraEscorts;
	CvUnit* pExtraEscort = pThisArmy->GetNextUnit(pEscort);
	while (pExtraEscort)
	{
		vExtraEscorts.push_back(pExtraEscort);
		pExtraEscort = pThisArmy->GetNextUnit(pExtraEscort); 
	}

	// No civilian? that's a problem
	if(!pCivilian || !pCivilian->IsCivilianUnit())
	{
		return;
	}

	// ESCORT AND CIVILIAN MEETING UP
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE || 
		pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_CATCH_UP)
	{
		// Check to make sure escort can get to civilian
		if(pOperation->GetMusterPlot() != NULL)
		{
			//check if the civilian is in danger
			if ( pCivilian->GetDanger() > 0 )
			{
				//try to move to safety
				CvPlot* pBetterPlot = TacticalAIHelpers::FindSafestPlotInReach(pCivilian,true).first;
				if (pBetterPlot)
				{
					ExecuteMoveToPlot(pCivilian,pBetterPlot);
					return;
				}
			}

			//civilian and escort have not yet met up
			if(pEscort)
			{
				//civilian is already there
				if(pCivilian->plot() == pOperation->GetMusterPlot())
				{
					//another military unit is blocking our escort ... find another muster plot
					if(pCivilian->plot()->GetNumCombatUnits() > 0)
					{
						CvUnit* pNewEscort = SwitchEscort(pCivilian,pCivilian->plot(),pEscort,pThisArmy);
						if (pNewEscort)
							pOperation->CheckTransitionToNextStage();
						else //did not switch
						{
							//Let's have them move forward, see if that clears things up.
							ExecuteMoveToPlot(pCivilian, pOperation->GetTargetPlot(),true,CvUnit::MOVEFLAG_APPROX_TARGET_RING1|CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER);
							ExecuteMoveToPlot(pEscort, pOperation->GetTargetPlot(),true,CvUnit::MOVEFLAG_APPROX_TARGET_RING1);

							//try again next turn
							pOperation->SetMusterPlot(pOperation->GetTargetPlot());

							if(GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("SingleHexOperationMoves: Forced movement to get things going.");
								LogTacticalMessage(strLogString);
							}
						}
					}
					else
					{
						//move escort towards civilian
						if (ExecuteMoveToPlot(pEscort, pCivilian->plot())>0)
						{
							//d'oh. escort cannot reach us
							CvUnit* pNewEscort = SwitchEscort(pCivilian,pCivilian->plot(),pEscort,pThisArmy);

							if (pEscort==pNewEscort)
								pOperation->SetToAbort(AI_ABORT_LOST_PATH);

							return;	
						}

						UnitProcessed(pCivilian->GetID());
					}
				}
				else
				{
					//both must move
					CvPlot* pMuster = pOperation->GetMusterPlot();
					ExecuteMoveToPlot(pCivilian, pMuster);
					ExecuteMoveToPlot(pEscort, pOperation->GetMusterPlot(),true,pEscort->canMoveInto(*pMuster)?0:CvUnit::MOVEFLAG_APPROX_TARGET_RING1);
				}

				if(pOperation->GetOperationState()!=AI_OPERATION_STATE_ABORTED && GC.getLogging() && GC.getAILogging())
				{
					CvString strTemp;
					CvString strLogString;
					strTemp = GC.getUnitInfo(pEscort->getUnitType())->GetDescription();
					strLogString.Format("Moving escorting %s to civilian for operation, Civilian X: %d, Civilian Y: %d, X: %d, Y: %d", strTemp.GetCString(), pCivilian->plot()->getX(), pCivilian->plot()->getY(), pEscort->getX(), pEscort->getY());
					LogTacticalMessage(strLogString);
				}
			}
			else
			{
				//no escort
				if (pCivilian->plot() == pOperation->GetMusterPlot())
					pOperation->CheckTransitionToNextStage();
				else if (pCivilian->GetDanger(pOperation->GetMusterPlot())<INT_MAX)
					//continue moving. if this should fail, we just freeze and wait for better times
					ExecuteMoveToPlot(pCivilian,pOperation->GetMusterPlot());
			}
		}
	}

	// MOVING TO TARGET ... or really close
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_MOVING_TO_DESTINATION ||
		pThisArmy->GetArmyAIState() == ARMYAISTATE_AT_DESTINATION)
	{
		int iMoveFlags = CvUnit::MOVEFLAG_NO_ENEMY_TERRITORY;
		//if necessary and possible, avoid plots where our escort cannot follow
		if (pEscort)
		{
			if (!pOperation->GetTargetPlot()->isNeutralUnit(pEscort->getOwner(), true, true))
				iMoveFlags |= CvUnit::MOVEFLAG_DONT_STACK_WITH_NEUTRAL;
		}
		else
		{
			iMoveFlags |= CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER;
		}
	
		// the escort leads the way
		bool bPathFound = false;
		bool bContinueOperation = true;
		CvString strLogString;
		if(pEscort)
		{
			//the target plot may be a city, so we need to check if the escort can actually go there
			//but the civilian uses the same flags so dump the escort when we're already there
			CvPlot* pTargetPlot = pOperation->GetTargetPlot();
			if (!pEscort->canMoveInto(*pTargetPlot) && !pEscort->plot()->isAdjacent(pTargetPlot))
				iMoveFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;

			bool bHavePathEscort = pEscort->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
			if(bHavePathEscort)
			{
				CvPlot* pCommonPlot = pEscort->GetPathEndFirstTurnPlot();
				//need to check if civilian can enter because of unrevealed tiles in path
				if(pCommonPlot != NULL && pCivilian->canMoveInto(*pCommonPlot,CvUnit::MOVEFLAG_DESTINATION))
				{
					int iTurns = INT_MAX;
					bool bHavePathCivilian = pCivilian->GeneratePath(pCommonPlot, iMoveFlags, 5, &iTurns);
					if (bHavePathCivilian)
					{
						bPathFound = true;

						if (iTurns > 0)
							//escort seems to be faster than the civilian, slow down
							pCommonPlot = pCivilian->GetPathEndFirstTurnPlot();

						//we know they can stack
						ExecuteMoveToPlot(pEscort, pCommonPlot);
						ExecuteMoveToPlot(pCivilian, pCommonPlot);

						if (GC.getLogging() && GC.getAILogging())
						{
							strLogString.Format("%s now at (%d,%d). Moving towards (%d,%d) with escort %s. escort leading.",
								pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(),
								pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY(), pEscort->getName().c_str());
						}
					}
				}
			}
			else
			{
				//civilian leads the way since escort seems to be blocked
				//but maybe we can at least find a way for this turn
				bool bHavePathCivilian = pCivilian->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
				if(bHavePathCivilian)
				{
					CvPlot* pCommonPlot = pCivilian->GetPathEndFirstTurnPlot();
					if(pCommonPlot != NULL)
					{
						int iTurns = INT_MAX;
						if (!pEscort->canMoveInto(*pCommonPlot))
							iMoveFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;

						bool bHavePathEscort = pEscort->GeneratePath(pCommonPlot, iMoveFlags, 5, &iTurns);
						if (bHavePathEscort)
						{
							bPathFound = true;

							if (iTurns > 0)
								//civilian seems to be faster than the escort, slow down
								pCommonPlot = pEscort->GetPathEndFirstTurnPlot();

							//we know they can stack
							ExecuteMoveToPlot(pEscort, pCommonPlot);
							ExecuteMoveToPlot(pCivilian, pCommonPlot);
						}
						else
						{
							//our escort can't move into the next path plot. maybe it's blocked by a friendly unit?
							CvUnit* pNewEscort = SwitchEscort(pCivilian,pCommonPlot,pEscort,pThisArmy);
							if (pNewEscort)
							{
								ExecuteMoveToPlot(pCivilian, pCommonPlot);
								pNewEscort->PushMission(CvTypes::getMISSION_SKIP());
								UnitProcessed(pNewEscort->GetID());
							}
						}
					}
				}
			}
			
			if(!bPathFound)
			{
				//we have a problem, apparently civilian and escort must split up
				//use a special flag here to make sure we're not stuck in a dead end with limited sight (can happen with embarked units)
				if (ExecuteMoveToPlot(pCivilian, pOperation->GetTargetPlot(), false, (iMoveFlags | CvUnit::MOVEFLAG_CONTINUE_TO_CLOSEST_PLOT))==INT_MAX)
				{
					pOperation->SetToAbort(AI_ABORT_LOST_PATH);
					strLogString.Format("%s stuck at (%d,%d), cannot find safe path to target. aborting.", 
						pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY() );
					bContinueOperation = false;
				}
				//try to stay close
				else if (ExecuteMoveToPlot(pEscort, pCivilian->plot(), false)==INT_MAX)
				{
					MoveToEmptySpaceNearTarget(pEscort, pCivilian->plot(), pCivilian->plot()->getDomain(), 12);
					strLogString.Format("%s at (%d,%d) separated from escort %s at (%d,%d)",
						pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(),
						pEscort->getName().c_str(), pEscort->getX(), pEscort->getY());
				}
				else
				{
					strLogString.Format("%s at (%d,%d) had an issue moving to its target but it was resolved",
						pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY());
				}
			}
		}
		else //no escort
		{
			bool bHavePathCivilian = pCivilian->GeneratePath(pOperation->GetTargetPlot(), iMoveFlags);
			if(bHavePathCivilian)
			{
				CvPlot* pTurnTarget = pCivilian->GetPathEndFirstTurnPlot();
				if(pTurnTarget != NULL)
				{
					if (pCivilian->GetDanger(pTurnTarget) == INT_MAX)
					{
						CvPlot* pAlternativeTarget = TacticalAIHelpers::FindSafestPlotInReach(pCivilian, true, true).first;
						if (pAlternativeTarget)
							pTurnTarget = pAlternativeTarget;
					}
					else
					{
						//maybe we can find ourselves an escort!
						CvUnit* pDefender = pTurnTarget->getBestDefender(m_pPlayer->GetID());
						if (pDefender && pDefender->getArmyID() == -1 && pDefender->getDomainType() == pCivilian->getDomainType())
						{
							pThisArmy->AddUnit(pDefender->GetID(), 1, pThisArmy->GetSlotInfo(1).m_requiredSlot);
							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("SingleHexOperationMoves: Grabbed an escort along the way.");
							}
							pDefender->SetTurnProcessed(true);
						}
					}

					ExecuteMoveToPlot(pCivilian, pTurnTarget);
					if(GC.getLogging() && GC.getAILogging())
					{
						strLogString.Format("%s now at (%d,%d). Moving normally towards (%d,%d) without escort.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY() );
					}
				}
			}
			else
			{
				if (MoveToEmptySpaceNearTarget(pCivilian, pOperation->GetTargetPlot(), DOMAIN_LAND, INT_MAX, true))
				{
					if(GC.getLogging() && GC.getAILogging())
						strLogString.Format("%s now at (%d,%d). Moving to empty space near target (%d,%d) without escort.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY(), pOperation->GetTargetPlot()->getX(), pOperation->GetTargetPlot()->getY() );
				}
				else
				{
					pOperation->SetToAbort(AI_ABORT_LOST_PATH);
					if(GC.getLogging() && GC.getAILogging())
						strLogString.Format("%s at (%d,%d). Aborted operation. No path to target for civilian.",  pCivilian->getName().c_str(), pCivilian->getX(), pCivilian->getY() );
				}
			}
		}

		// now we're done, if the operation was cancelled, let the units be handled by Homeland AI
		if (bContinueOperation)
		{
			UnitProcessed(pCivilian->GetID());
			if (pEscort)
				UnitProcessed(pEscort->GetID());
		}

		// logging
		if(GC.getLogging() && GC.getAILogging())
		{
			LogTacticalMessage(strLogString);
		}
	}

	//move any additional escorts near the civilian
	for (size_t i=0; i<vExtraEscorts.size(); i++)
	{
		CvUnit* pUnit = vExtraEscorts[i];
		MoveToEmptySpaceNearTarget( pUnit, pCivilian->plot(), NO_DOMAIN, 23 );
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strTemp;
			CvString strLogString;
			strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
			strLogString.Format("Moving additional escorting %s to civilian for operation, Civilian X: %d, Civilian Y: %d, X: %d, Y: %d", strTemp.GetCString(), pCivilian->plot()->getX(), pCivilian->plot()->getY(), pUnit->getX(), pUnit->getY());
			LogTacticalMessage(strLogString);
		}
		UnitProcessed(pUnit->GetID());
	}
}

/// Move a large army to its destination against an enemy target
void CvTacticalAI::PlotArmyMovesCombat(CvArmyAI* pThisArmy)
{
	if (!pThisArmy)
		return;

	CvAIOperation* pOperation = GET_PLAYER(pThisArmy->GetOwner()).getAIOperation(pThisArmy->GetOperationID());
	if (!pOperation || pOperation->GetMusterPlot() == NULL)
		return;

	//where do we want to go
	CvPlot* pThisTurnTarget = pOperation->ComputeTargetPlotForThisTurn(pThisArmy);
	if (pThisTurnTarget == NULL)
	{
		pOperation->SetToAbort(AI_ABORT_LOST_PATH);
		return;
	}

	//this may force detours, but whatever
	if (CheckForEnemiesNearArmy(pThisArmy))
	{
		//try to keep our units together, do not move on while there are enemies around, it's too dangerous
		pThisTurnTarget = pThisArmy->GetCenterOfMass(true);
		pOperation->LogOperationSpecialMessage("Contact with enemy!");
	}

	// RECRUITING
	if(pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE || 
		pThisArmy->GetArmyAIState() == ARMYAISTATE_WAITING_FOR_UNITS_TO_CATCH_UP)
	{
		// This is where we try to gather. Don't use the center of mass here, it may drift anywhere 
		ExecuteGatherMoves(pThisArmy,pThisTurnTarget);
	}

	// MOVING TO TARGET
	else if(pThisArmy->GetArmyAIState() == ARMYAISTATE_MOVING_TO_DESTINATION)
	{
		//if this operation has a specific target player
		if (pOperation->GetEnemy() != NO_PLAYER)
		{
			//getting too close to another enemy?
			if (GC.getGame().GetClosestCityDistanceInPlots(pThisTurnTarget) < 3)
			{
				PlayerTypes eCityOwner = GC.getGame().GetClosestCityOwnerByPlots(pThisTurnTarget);
				if (eCityOwner != pOperation->GetEnemy() && m_pPlayer->IsAtWarWith(eCityOwner))
					pOperation->SetToAbort(AI_ABORT_TOO_DANGEROUS);
			}
		}

		//try to arrange the units somewhat closer to the target
		ExecuteGatherMoves(pThisArmy,pThisTurnTarget);
	}
}

//workaround to make units from disbanded armies accessible to tactical AI in the same turn
void CvTacticalAI::AddCurrentTurnUnit(CvUnit * pUnit)
{
	if (pUnit && pUnit->canMove())
		m_CurrentTurnUnits.push_back( pUnit->GetID() );
}

//make sure our units come in a defined order (important for reproducability, don't want to sort pointers!)
struct PrSortByUnitId
{
	bool operator()(const CvUnit* lhs, const CvUnit* rhs) const { return lhs->GetID() < rhs->GetID(); }
};

/// Queues up attacks on enemy units on or adjacent to army's desired center

bool CvTacticalAI::ExecutePreCaptureEmbarkedSupportStaging(CvPlot* pTargetPlot)
{
	if (!pTargetPlot || !pTargetPlot->isCity())
		return false;

	CvCity* pTargetCity = pTargetPlot->getPlotCity();
	if (!pTargetCity || !pTargetCity->isCoastal())
		return false;

	if (m_pPlayer->GetPlayerTraits()->GetFreeUnitOnConquest() != NO_UNIT)
		return false;

	struct SStageChoice
	{
		SStageChoice(CvUnit* pUnit_, CvPlot* pPlot_, int iScore_) : pUnit(pUnit_), pPlot(pPlot_), iScore(iScore_) {}
		CvUnit* pUnit;
		CvPlot* pPlot;
		int iScore;
		bool operator<(const SStageChoice& rhs) const { return iScore > rhs.iScore; }
	};

	std::vector<SStageChoice> choices;
	for (std::list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (!pUnit || pUnit->TurnProcessed() || pUnit->getArmyID() != -1)
			continue;

		if (pUnit->getDomainType() != DOMAIN_LAND || !pUnit->canMove())
			continue;

		if (!(pUnit->IsCanAttackRanged() || pUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD))
			continue;

		if (pUnit->GetCurrHitPoints() < pUnit->GetMaxHitPoints() * 2 / 3)
			continue;

		for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
		{
			CvPlot* pStagePlot = plotDirection(pTargetCity->getX(), pTargetCity->getY(), (DirectionTypes)iDir);
			if (!pStagePlot || !pStagePlot->isWater())
				continue;

			const int iMoveFlags = CvUnit::MOVEFLAG_SAFE_EMBARK_ONLY | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF;
			if (pUnit->TurnsToReachTarget(pStagePlot, iMoveFlags, 0) > 0)
				continue;

			int iDanger = pUnit->GetDanger(pStagePlot);
			if (iDanger > pUnit->GetCurrHitPoints() / 3)
				continue;

			int iScore = pUnit->GetCurrHitPoints() - iDanger;
			iScore += pUnit->IsCanAttackRanged() ? 80 : 30;
			iScore += pStagePlot->GetNumFriendlyUnitsAdjacent(m_pPlayer->getTeam(), DOMAIN_SEA, true) * 25;
			iScore += pStagePlot->IsFriendlyUnitAdjacent(m_pPlayer->getTeam(), true) ? 10 : 0;

			choices.push_back(SStageChoice(pUnit, pStagePlot, iScore));
		}
	}

	if (choices.empty())
		return false;

	std::stable_sort(choices.begin(), choices.end());
	SStageChoice best = choices.front();
	int iTurnsLeft = ExecuteMoveToPlot(best.pUnit, best.pPlot, false, CvUnit::MOVEFLAG_SAFE_EMBARK_ONLY | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF);
	if (iTurnsLeft == INT_MAX)
		return false;

	UnitProcessed(best.pUnit->GetID());

	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strLogString;
		strLogString.Format("Pre-capture staging: %s moved to safe embark plot (%d,%d) for likely naval capture of %s",
			best.pUnit->getName().GetCString(), best.pPlot->getX(), best.pPlot->getY(), pTargetCity->getNameNoSpace().c_str());
		LogTacticalMessage(strLogString);
	}

	return true;
}
bool CvTacticalAI::CheckForEnemiesNearArmy(CvArmyAI* pArmy)
{
	if (!pArmy)
		return false;

	set<CvUnit*, PrSortByUnitId> ourUnitsInitial;
	// Cache enemy attackers per plot to avoid redundant GetPossibleAttackers calls
	map<int, vector<CvUnit*>> cachedEnemyAttackers;
	set<CvPlot*, PrSortByPlotIndex> allEnemyPlots;

	CvUnit* pUnit = pArmy->GetFirstUnit();
	while (pUnit)
	{
		if (!pUnit->canUseNow() || pUnit->GetCurrHitPoints()<pUnit->GetMaxHitPoints() / 2)
		{
			pUnit = pArmy->GetNextUnit(pUnit);
			continue;
		}

		//can we attack somebody?
		vector<pair<CvPlot*, bool>> targets = TacticalAIHelpers::GetTargetsInRange(pUnit);
		
		//who can attack us? Use cache to avoid redundant lookups
		int iPlotIndex = pUnit->plot()->GetPlotIndex();
		if (cachedEnemyAttackers.find(iPlotIndex) == cachedEnemyAttackers.end())
			cachedEnemyAttackers[iPlotIndex] = m_pPlayer->GetPossibleAttackers(*pUnit->plot(), m_pPlayer->getTeam());
		
		vector<CvUnit*>& vEnemyAttackers = cachedEnemyAttackers[iPlotIndex];

		if (targets.empty() && vEnemyAttackers.empty())
		{
			pUnit = pArmy->GetNextUnit(pUnit);
			continue;
		}

		//this unit can be attacked, remember it
		ourUnitsInitial.insert(pUnit);

		// Collect enemy plots for later
		for (size_t i = 0; i < vEnemyAttackers.size(); i++)
			allEnemyPlots.insert(vEnemyAttackers[i]->plot());

		for (size_t i = 0; i < vEnemyAttackers.size(); i++)
		{
			//now here's the trick, also include our non-army units which happen to be around
			vector<CvUnit*> vOurAttackersAndAllies = GET_PLAYER(vEnemyAttackers[i]->getOwner()).GetPossibleAttackers(*vEnemyAttackers[i]->plot(), vEnemyAttackers[i]->getTeam());
			for (size_t j = 0; j < vOurAttackersAndAllies.size(); j++)
				if (vOurAttackersAndAllies[j]->getOwner()==m_pPlayer->GetID())
					ourUnitsInitial.insert(vOurAttackersAndAllies[j]);
		}

		pUnit = pArmy->GetNextUnit(pUnit);
	}

	if (ourUnitsInitial.empty())
		return false;

	//now that we have a set of units find the center of mass
	int x = 0;
	int y = 0;
	for (set<CvUnit*, PrSortByUnitId>::iterator it = ourUnitsInitial.begin(); it != ourUnitsInitial.end(); ++it)
	{
		x += (*it)->getX();
		y += (*it)->getY();

		// Use cache if available, otherwise fetch and cache
		int iPlotIndex = (*it)->plot()->GetPlotIndex();
		if (cachedEnemyAttackers.find(iPlotIndex) == cachedEnemyAttackers.end())
			cachedEnemyAttackers[iPlotIndex] = m_pPlayer->GetPossibleAttackers(*(*it)->plot(), m_pPlayer->getTeam());

		vector<CvUnit*>& vEnemyAttackers = cachedEnemyAttackers[iPlotIndex];
		for (size_t i = 0; i < vEnemyAttackers.size(); i++)
			allEnemyPlots.insert(vEnemyAttackers[i]->plot());
	}
	x = (x * 100) / ourUnitsInitial.size();
	y = (y * 100) / ourUnitsInitial.size();

	//now find the closest enemy plot to our center of mass
	CvPlot* pCoM = GC.getMap().plot((x + 50) / 100, (y + 50) / 100);
	CvPlot* pClosestEnemyPlot = NULL;
	int iMinDist = INT_MAX;
	for (set<CvPlot*, PrSortByPlotIndex>::const_iterator it = allEnemyPlots.begin(); it != allEnemyPlots.end(); ++it)
	{
		int iDist = plotDistance(*pCoM, *(*it));
		if (iDist < iMinDist)
		{
			pClosestEnemyPlot = *it;
			iMinDist = iDist;
		}
	}

	if (pClosestEnemyPlot == NULL)
		return false;

	//ignore units which are VERY far out; combat sim will ignore the "worst" units if necessary
	vector<CvUnit*> ourUnitsFinal;
	for (set<CvUnit*, PrSortByUnitId>::const_iterator it = ourUnitsInitial.begin(); it != ourUnitsInitial.end(); ++it)
	{
		if (plotDistance(*pCoM, *(*it)->plot()) < 7)
			ourUnitsFinal.push_back(*it);
	}

	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		strMsg.Format("Performing opportunity attack with army %d and friends", pArmy->GetID());
		LogTacticalMessage(strMsg);
	}

	//we probably didn't see all enemy units, so doublecheck ... don't get drawn into the wrong fight
	CvCity* pClosestCity = GC.getGame().GetClosestCityByPlots(pClosestEnemyPlot, NO_PLAYER);
	CvTacticalDominanceZone* pZone = m_pPlayer->GetTacticalAI()->GetTacticalAnalysisMap()->GetZoneByCity(pClosestCity,pArmy->GetType()!=ARMY_TYPE_LAND);
	if (pZone && pZone->GetZoneCity() && pZone->GetTerritoryType()==TACTICAL_TERRITORY_ENEMY && pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_ENEMY)
		return false;

	return TacticalAIHelpers::FindAndExecuteBestUnitAssignments(m_pPlayer->GetID(), ourUnitsFinal, pClosestEnemyPlot, AL_MEDIUM);
}

void CvTacticalAI::ExecuteGatherMoves(CvArmyAI * pArmy, CvPlot * pTurnTarget)
{
	if (!pArmy || !pTurnTarget)
		return;

	vector<CvUnit*> vUnits;
	CvUnit* pUnit = pArmy->GetFirstUnit();
	while (pUnit)
	{
		if (pUnit->canUseNow()) //ignore units used during CheckForEnemiesNearArmy
			vUnits.push_back(pUnit);

		pUnit = pArmy->GetNextUnit(pUnit);
	}

	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		strMsg.Format("Gathering army %d", pArmy->GetID());
		for (size_t i = 0; i < pArmy->GetNumFormationEntries(); i++)
		{
			CvArmyFormationSlot* slot = pArmy->GetSlotStatus(i);
			strMsg += CvString::format("; unit %d", slot->GetUnitID());
		}
		LogTacticalMessage(strMsg);
	}

	//we used to pass the army's target plot as a fallback target
	//but for sneak attacks the target plot may be unreachable
	//so we just go step by step
	PositionUnitsAroundTarget(vUnits, pTurnTarget);
}

// ROUTINES TO PROCESS AND SORT TARGETS

void CvTacticalAI::DumpTacticalTargets()
{
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT); pTarget!=NULL; pTarget = GetNextZoneTarget())
	{
		CvUnit* pUnit = pTarget->GetUnitPtr();
		CvString strMsg;
		strMsg.Format("Enemy %s, id %d at (%d,%d), score %d, damage %d", 
			pUnit->getName().c_str(), pUnit->GetID(), pTarget->GetTargetX(), pTarget->GetTargetY(),
			pTarget->GetAuxIntData(), pUnit->getDamage());
		LogTacticalMessage(strMsg);
	}
}

// adjust the score so that we can sort the targets
// keep in mind that the tactical combat sim will also take into account other enemies around the target
// and try to do as much damage as possible. so we only need a rough scoring here.
void CvTacticalAI::UpdateTargetScores()
{
	// Check if we have air units available for tactical use
	// If so, we should prioritize clearing enemy AA/interceptors with ground forces first
	bool bHaveAirUnits = false;
	int iAirUnitCount = 0;
	bool bImminentAttack = m_bImminentAttack;
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); ++it)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(*it);
		if (pUnit && pUnit->getDomainType() == DOMAIN_AIR && pUnit->canMove() && pUnit->IsCanAttackRanged())
		{
			bHaveAirUnits = true;
			iAirUnitCount++;
		}
	}

	for(vector<CvTacticalTarget>::iterator it = m_AllTargets.begin(); it != m_AllTargets.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plot(it->GetTargetX(), it->GetTargetY());
		if(it->GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT)
		{
			//try to attack target close to our units first
			vector<CvUnit*> myUnits = pPlot->GetAdjacentFriendlyCombatUnits(m_pPlayer->getTeam(), 3, pPlot->getDomain());
			int iScore = it->GetAuxIntData() + (int)myUnits.size();

			//try to attack targets close to our cities first
			int iDist = m_pPlayer->GetCityDistanceInPlots(pPlot);
			if (iDist != INT_MAX)
			{
				int iDistScore = max(0, 5 - iDist);
				iScore += iDistScore;
			}

			if (bImminentAttack)
			{
				// Penalize distant enemy targets outside our territory
				if (pPlot->getTeam() != m_pPlayer->getTeam())
				{
					int iDistancePenalty = (iDist == INT_MAX) ? 0 : max(0, iDist - 2);
					iScore -= iDistancePenalty * 2;
				}

				// Graduated bonus for enemies close to our cities (not just adjacent)
				if (iDist != INT_MAX && iDist <= 3)
					iScore += max(1, 12 - iDist * 3); // +9 at dist 1, +6 at dist 2, +3 at dist 3
			}

			it->SetAuxIntData(max(1, iScore));
			
			CvUnit* pTargetUnit = it->GetUnitPtr();
			if (!pTargetUnit)
				continue;
			
			// SEAD (Suppression of Enemy Air Defenses): Prioritize AA units when we have air power
			// Ground/naval forces should clear interceptors before our bombers go in
			if (bHaveAirUnits && pTargetUnit->canIntercept() && pTargetUnit->getDomainType() != DOMAIN_AIR)
			{
				// This is a land-based AA unit (Mobile SAM, AA Gun, etc.)
				// Prioritize it so ground forces clear it before air strikes
				int iAABonus = 20;
				
				// Higher bonus if we have more air units waiting
				if (iAirUnitCount >= 3)
					iAABonus += 15;
				else if (iAirUnitCount >= 2)
					iAABonus += 8;
				
				// Check if any of our units (including naval ranged) can attack this AA
				// Naval ranged units can attack land targets!
				bool bNearOurForces = (myUnits.size() > 0);
				bool bNavalCanAttack = false;
				
				// Check for naval ranged units that can strike this land AA target
				for (list<int>::iterator unitIt = m_CurrentTurnUnits.begin(); unitIt != m_CurrentTurnUnits.end(); ++unitIt)
				{
					CvUnit* pUnit = m_pPlayer->getUnit(*unitIt);
					if (pUnit && pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCanAttackRanged())
					{
						// Check if naval unit can range strike this land target
						if (pUnit->canRangeStrikeAt(pPlot->getX(), pPlot->getY()))
						{
							bNavalCanAttack = true;
							break;
						}
					}
				}
				
				if (bNearOurForces || bNavalCanAttack)
					iAABonus += 10;
				
				// Extra bonus if naval can attack - combined arms flexibility
				if (bNavalCanAttack)
					iAABonus += 5;
				
				// Check interception range - AA units covering critical areas are priority
				int iInterceptRange = pTargetUnit->GetAirInterceptRange();
				if (iInterceptRange >= 4)
					iAABonus += 10; // Long-range AA is very dangerous
				else if (iInterceptRange >= 2)
					iAABonus += 5;
				
				it->SetAuxIntData(it->GetAuxIntData() + iAABonus);
				
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("SEAD target: %s at (%d,%d), AA range %d, priority boosted by %d (air units: %d, naval can attack: %s)",
						pTargetUnit->getName().GetCString(), pPlot->getX(), pPlot->getY(), 
						iInterceptRange, iAABonus, iAirUnitCount, bNavalCanAttack ? "yes" : "no");
					LogTacticalMessage(strLogString);
				}
			}
			
			// COUNTER-BLOCKADE: Prioritize naval units that are blockading our cities
			// Breaking blockades is critical - it restores city healing and trade income
			if (pTargetUnit->getDomainType() == DOMAIN_SEA && pPlot->isBlockaded(m_pPlayer->GetID()))
			{
				const int iMaxBlockadeCityDistance = 3 + range(GD_INT_GET(NAVAL_PLOT_BLOCKADE_RANGE), 0, 3);

				// Check if this unit can actively blockade one of our cities' workable sea plots.
				bool bBlockadingOurCity = false;
				int iCityLoop = 0;
				for (CvCity* pCity = m_pPlayer->firstCity(&iCityLoop); pCity != NULL && !bBlockadingOurCity; pCity = m_pPlayer->nextCity(&iCityLoop))
				{
					if (plotDistance(*pPlot, *pCity->plot()) > iMaxBlockadeCityDistance)
						continue;
					if (!pCity->GetCityCitizens()->AnyPlotBlockaded())
						continue;

					for (int iI = 0; iI < pCity->GetNumWorkablePlots(); iI++)
					{
						CvPlot* pBlockedPlot = pCity->GetCityCitizens()->GetCityPlotFromIndex(iI);
						if (!pBlockedPlot || !pBlockedPlot->isEffectiveOwner(pCity) || !pBlockedPlot->isWater())
							continue;
						if (!pBlockedPlot->isBlockaded(m_pPlayer->GetID()))
							continue;
						if (!pTargetUnit->isNativeDomain(pPlot) || !pTargetUnit->canEndTurnAtPlot(pBlockedPlot))
							continue;

						bBlockadingOurCity = true;

						// Higher priority if city is damaged (needs healing)
						if (pCity->getDamage() > 0)
							it->SetAuxIntData(it->GetAuxIntData() + 15);

						// Even higher if city is under siege
						if (pCity->isUnderSiege())
							it->SetAuxIntData(it->GetAuxIntData() + 20);

						break;
					}
				}
				
				if (bBlockadingOurCity)
				{
					// Major priority boost for units actively blockading our cities
					it->SetAuxIntData(it->GetAuxIntData() + 25);
					
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Counter-blockade target: %s at (%d,%d), boosted priority",
							pTargetUnit->getName().GetCString(), pPlot->getX(), pPlot->getY());
						LogTacticalMessage(strLogString);
					}
				}
			}
			
			// COMBINED ARMS BOMBARDMENT: Prioritize units defending enemy cities under siege
			// Naval ranged AND air units should prioritize clearing garrison/defenders when ground forces are assaulting
			if (pTargetUnit->getDomainType() == DOMAIN_LAND || pTargetUnit->IsGarrisoned())
			{
				// Check if target is in or adjacent to an enemy city
				CvCity* pEnemyCity = pPlot->getPlotCity();
				if (!pEnemyCity)
				{
					// Check adjacent plots for enemy city
					for (int iDir = 0; iDir < NUM_DIRECTION_TYPES && !pEnemyCity; iDir++)
					{
						CvPlot* pAdj = plotDirection(pPlot->getX(), pPlot->getY(), (DirectionTypes)iDir);
						if (pAdj && pAdj->isCity() && pAdj->getPlotCity()->getTeam() != m_pPlayer->getTeam())
							pEnemyCity = pAdj->getPlotCity();
					}
				}
				
				if (pEnemyCity)
				{
					// Check for combined arms siege - count all friendly forces around the city
					bool bSiegeOngoing = false;
					int iOurLandUnits = 0;
					int iOurNavalUnits = 0;
					bool bWeHaveNavalRanged = false;
					bool bWeHaveAirUnits = false;
					
					for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
					{
						CvPlot* pAdj = plotDirection(pEnemyCity->getX(), pEnemyCity->getY(), (DirectionTypes)iDir);
						if (pAdj)
						{
							// Count our land units adjacent to enemy city
							if (!pAdj->isWater())
							{
								CvUnit* pOurUnit = pAdj->getBestDefender(m_pPlayer->GetID());
								if (pOurUnit && pOurUnit->getDomainType() == DOMAIN_LAND)
								{
									bSiegeOngoing = true;
									iOurLandUnits++;
								}
							}
							
							// Check if we have naval units nearby
							if (pAdj->isWater())
							{
								CvUnit* pOurUnit = pAdj->getBestDefender(m_pPlayer->GetID());
								if (pOurUnit && pOurUnit->getDomainType() == DOMAIN_SEA)
								{
									bSiegeOngoing = true;
									iOurNavalUnits++;
									if (pOurUnit->IsCanAttackRanged())
										bWeHaveNavalRanged = true;
								}
							}
						}
					}
					
					// Check if we have air units that can strike this city
					if (bHaveAirUnits)
					{
						for (list<int>::iterator airIt = m_CurrentTurnUnits.begin(); airIt != m_CurrentTurnUnits.end(); ++airIt)
						{
							CvUnit* pAirUnit = m_pPlayer->getUnit(*airIt);
							if (pAirUnit && pAirUnit->getDomainType() == DOMAIN_AIR && pAirUnit->IsCanAttackRanged())
							{
								if (pAirUnit->canRangeStrikeAt(pEnemyCity->getX(), pEnemyCity->getY()))
								{
									bWeHaveAirUnits = true;
									break;
								}
							}
						}
					}
					
					// If siege is ongoing and we have ranged fire support (naval or air), prioritize this target
					if (bSiegeOngoing && (bWeHaveNavalRanged || bWeHaveAirUnits))
					{
						int iCombinedArmsBonus = 15;
						int iTotalAttackers = iOurLandUnits + iOurNavalUnits;
						
						// More attackers = more value in clearing defenders
						if (iTotalAttackers >= 4)
							iCombinedArmsBonus += 15;
						else if (iTotalAttackers >= 3)
							iCombinedArmsBonus += 12;
						else if (iTotalAttackers >= 2)
							iCombinedArmsBonus += 6;
						
						// Multi-domain assault bonus
						int iDomains = 0;
						if (iOurLandUnits > 0) iDomains++;
						if (iOurNavalUnits > 0) iDomains++;
						if (bWeHaveAirUnits) iDomains++;
						if (iDomains >= 3)
							iCombinedArmsBonus += 15; // Full combined arms assault!
						else if (iDomains >= 2)
							iCombinedArmsBonus += 8;
						
						// Garrison is priority - it adds city defense and counterattack
						if (pTargetUnit->IsGarrisoned())
							iCombinedArmsBonus += 15;
						
						// City already damaged = assault in progress, high priority
						if (pEnemyCity->getDamage() > 0)
							iCombinedArmsBonus += 10;
						
						it->SetAuxIntData(it->GetAuxIntData() + iCombinedArmsBonus);
						
						if (GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("Combined arms target: %s at (%d,%d), defending %s, priority +%d (land:%d naval:%d air:%s domains:%d)",
								pTargetUnit->getName().GetCString(), pPlot->getX(), pPlot->getY(),
								pEnemyCity->getNameNoSpace().c_str(), iCombinedArmsBonus, iOurLandUnits, iOurNavalUnits,
								bWeHaveAirUnits ? "yes" : "no", iDomains);
							LogTacticalMessage(strLogString);
						}
					}
				}
			}
			
			// === AIR SOFTENING COORDINATION FOR GROUND ATTACKS ===
			// Ground forces should prefer attacking targets that air units can soften first
			// This creates efficient combined arms: air weakens, ground finishes
			if (bHaveAirUnits && pTargetUnit)
			{
				int iAirSofteningBonus = 0;
				int iEstimatedAirDamage = 0;
				int iAirUnitsCanStrike = 0;
				
				// Count air units that can strike this target and estimate damage
				for (list<int>::iterator airIt = m_CurrentTurnUnits.begin(); airIt != m_CurrentTurnUnits.end(); ++airIt)
				{
					CvUnit* pAirUnit = m_pPlayer->getUnit(*airIt);
					if (!pAirUnit || pAirUnit->getDomainType() != DOMAIN_AIR || !pAirUnit->IsCanAttackRanged())
						continue;
					
					if (pAirUnit->canRangeStrikeAt(pPlot->getX(), pPlot->getY()))
					{
						iAirUnitsCanStrike++;
						
						// Estimate air damage (rough calculation)
						int iUnusedRef = 0;
						int iDamage = pAirUnit->GetAirCombatDamage(pTargetUnit, NULL, 0, iUnusedRef, false);
						iEstimatedAirDamage += iDamage;
					}
				}
				
				if (iAirUnitsCanStrike > 0)
				{
					int iTargetHP = pTargetUnit->GetCurrHitPoints();
					int iTargetMaxHP = pTargetUnit->GetMaxHitPoints();
					
					// Case 1: Air can kill outright - high priority (let air handle it, ground moves on)
					if (iEstimatedAirDamage >= iTargetHP)
					{
						iAirSofteningBonus += 15; // Good target for air strike
					}
					// Case 2: Air can seriously weaken (>50% damage) - excellent for ground follow-up
					else if (iEstimatedAirDamage >= iTargetHP / 2)
					{
						iAirSofteningBonus += 20; // Ideal combined arms target
					}
					// Case 3: Air can contribute significant damage (25-50%)
					else if (iEstimatedAirDamage >= iTargetHP / 4)
					{
						iAirSofteningBonus += 10; // Worthwhile softening
					}
					
					// Bonus for multiple air units - more reliable softening
					if (iAirUnitsCanStrike >= 3)
						iAirSofteningBonus += 8;
					else if (iAirUnitsCanStrike >= 2)
						iAirSofteningBonus += 4;
					
					// Target is already wounded - coordinate to finish it off
					int iHPPercent = (iTargetHP * 100) / iTargetMaxHP;
					if (iHPPercent <= 50)
					{
						// Wounded unit that air can further weaken - excellent target
						iAirSofteningBonus += 12;
						
						// Even better if air can finish it
						if (iEstimatedAirDamage >= iTargetHP)
							iAirSofteningBonus += 8;
					}
					else if (iHPPercent <= 75)
					{
						// Moderately damaged - air softening is valuable
						iAirSofteningBonus += 6;
					}
					
					// High-value targets worth softening
					UnitAITypes eTargetAI = pTargetUnit->AI_getUnitAIType();
					if (eTargetAI == UNITAI_CITY_BOMBARD || eTargetAI == UNITAI_RANGED)
					{
						// Siege and ranged are dangerous - softening reduces threat
						iAirSofteningBonus += 8;
					}
					else if (pTargetUnit->GetBaseCombatStrength() >= 40)
					{
						// Strong units benefit most from softening
						iAirSofteningBonus += 5;
					}
					
					it->SetAuxIntData(it->GetAuxIntData() + iAirSofteningBonus);
					
					if (GC.getLogging() && GC.getAILogging() && iAirSofteningBonus >= 15)
					{
						CvString strLogString;
						strLogString.Format("Air softening target: %s at (%d,%d), HP %d/%d, air damage ~%d, air units %d, priority +%d",
							pTargetUnit->getName().GetCString(), pPlot->getX(), pPlot->getY(),
							iTargetHP, iTargetMaxHP, iEstimatedAirDamage, iAirUnitsCanStrike, iAirSofteningBonus);
						LogTacticalMessage(strLogString);
					}
				}
			}
		}
	}
}

void CvTacticalAI::SortTargetListAndDropUselessTargets()
{
	// Important: Sort all targets by aux data (if used for that target type)
	std::stable_sort(m_AllTargets.begin(), m_AllTargets.end());

	vector<CvTacticalTarget> reducedTargetList;

	//now in the sorted list we can suppress adjacent non-maximum targets
	int iSuppressionRange = 2;
	for (vector<CvTacticalTarget>::const_iterator it = m_AllTargets.begin(); it != m_AllTargets.end(); ++it)
	{
		bool bBetterTargetAdjacent = false;

		//do this only for enemy units in the same domain
		if (it->GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT )
		{
			for (vector<CvTacticalTarget>::const_iterator it2 = reducedTargetList.begin(); it2 != reducedTargetList.end(); ++it2)
			{
				//land zones have id > 0, water zones id < 0. opposite signs means domain mismatch
				//check signs without multiplication to avoid overflow with large zone IDs
				int iZone1 = it->GetDominanceZone();
				int iZone2 = it2->GetDominanceZone();
				if ((iZone1 > 0 && iZone2 < 0) || (iZone1 < 0 && iZone2 > 0))
					continue;

				//if close to one of our cities, make sure we're not dropping it
				CvPlot* pPlot = GC.getMap().plot(it->GetTargetX(), it->GetTargetY());
				if (m_pPlayer->GetCityDistanceInPlots(pPlot) < 4)
					continue;

				if (it2->GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT)
				{
					if (it2->GetAuxIntData() >= it->GetAuxIntData() && plotDistance(it2->GetTargetX(), it2->GetTargetY(), it->GetTargetX(), it->GetTargetY()) <= iSuppressionRange)
					{
						bBetterTargetAdjacent = true;
						break;
					}
				}
			}
		}

		if (!bBetterTargetAdjacent)
			reducedTargetList.push_back(*it);
	}

	m_AllTargets = reducedTargetList;
}

void CvTacticalAI::ClearCurrentMoveUnits(AITacticalMove eNewMove)
{
	m_CurrentAirSweepUnits.clear();
	m_CurrentMoveCities.clear();
	m_CurrentMoveUnits.clear();
	m_CurrentMoveUnits.setCurrentTacticalMove(eNewMove);
}

/// Sift through the target list and find just those that apply to the dominance zone we are currently looking at
int CvTacticalAI::ExtractTargetsForZone(CvTacticalDominanceZone* pZone /* Pass in NULL for all zones */)
{
	int iMaxRadius = GetTacticalAnalysisMap()->GetMaxZoneRadius();

	m_ZoneTargets.clear();
	for(vector<CvTacticalTarget>::iterator it = m_AllTargets.begin(); it != m_AllTargets.end(); ++it)
	{
		//domain check
		if (pZone)
		{
			DomainTypes eDomain = pZone->IsWater() ? DOMAIN_SEA : DOMAIN_LAND;
			if (!it->IsTargetValidInThisDomain(eDomain))
				continue;
		}

		//zone match
		if(pZone == NULL || it->GetDominanceZone() == pZone->GetZoneID())
		{
			m_ZoneTargets.push_back(&(*it));
			continue;
		}

		//zone boundaries are arbitrary sometimes so include neighboring tiles as well for smaller zones
		if (pZone && plotDistance(pZone->GetCenterX(), pZone->GetCenterY(), it->GetTargetX(), it->GetTargetY()) <= iMaxRadius)
		{
			m_ZoneTargets.push_back(&(*it));
		}
	}

	return (int)m_ZoneTargets.size();
}

/// Find the first target of a requested type in current dominance zone (call after ExtractTargetsForZone())
CvTacticalTarget* CvTacticalAI::GetFirstZoneTarget(AITacticalTargetType eType, eAggressionLevel threshold)
{
	m_eCurrentTargetType = eType;
	m_iCurrentTargetIndex = 0;

	while(m_iCurrentTargetIndex < (int)m_ZoneTargets.size())
	{
		//doesn't make sense to attack multiple times without raising the agg level
		if (m_ZoneTargets[m_iCurrentTargetIndex]->GetLastAggLevel() < threshold)
		{
			if (m_eCurrentTargetType == AI_TACTICAL_TARGET_NONE || m_ZoneTargets[m_iCurrentTargetIndex]->GetTargetType() == m_eCurrentTargetType)
			{
				return m_ZoneTargets[m_iCurrentTargetIndex];
			}
		}
		m_iCurrentTargetIndex++;
	}

	return NULL;
}

/// Find the next target of a requested type in current dominance zone (call after GetFirstZoneTarget())
CvTacticalTarget* CvTacticalAI::GetNextZoneTarget(eAggressionLevel threshold)
{
	m_iCurrentTargetIndex++;

	while(m_iCurrentTargetIndex < (int)m_ZoneTargets.size())
	{
		//doesn't make sense to attack multiple times without raising the agg level
		if (m_ZoneTargets[m_iCurrentTargetIndex]->GetLastAggLevel() < threshold)
		{
			if (m_eCurrentTargetType == AI_TACTICAL_TARGET_NONE || m_ZoneTargets[m_iCurrentTargetIndex]->GetTargetType() == m_eCurrentTargetType)
			{
				return m_ZoneTargets[m_iCurrentTargetIndex];
			}
		}
		m_iCurrentTargetIndex++;
	}

	return NULL;
}

// ROUTINES TO EXECUTE A MISSION

/// Capture the gold from a barbarian camp
void CvTacticalAI::ExecuteBarbarianCampMove(CvPlot* pTargetPlot)
{
	//ignore visibility here so the AI doesn't go naively after revealed but invisible camps
	//and then a unit is stuck there without being able to attack
	if (pTargetPlot->isEnemyUnit(m_pPlayer->GetID(), true, false))
	{
		int nGoodAttackers = 0;
		vector<CvUnit*> vUnits;
		for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
			if (!pUnit)
				continue;

			//need at least one good attacker
			if (TacticalAIHelpers::IsAttackNetPositive(pUnit, pTargetPlot, 0))
				nGoodAttackers++;

			vUnits.push_back(pUnit);

			//don't use too many units
			if (nGoodAttackers > 2)
				break;
		}

		//just get into position, we will attack next turn when in place
		if (nGoodAttackers>1)
			PositionUnitsAroundTarget(vUnits, pTargetPlot);
	}
	else
	{
		for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
		{
			CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
			if (!pUnit)
				continue;

			//try to get into position
			//some of the camps the player has revealed may since have been cleared ... but we need to check
			//if the camp has been cleared there might be a neutral unit in the plot and our pathfinding could fail without the approximate flag!
			ExecuteMoveToPlot(pUnit, pTargetPlot, false, CvUnit::MOVEFLAG_APPROX_TARGET_RING1|CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER);

			if (pUnit->canMove())
			{
				//capture the camp if it still exists - if there is an enemy then we'll attack later
				if (pTargetPlot->GetNumCombatUnits()==0)
					ExecuteMoveToPlot(pUnit, pTargetPlot, false);

				//can use this unit for other moves, reset the tactmove to avoid spamming the log
				pUnit->setTacticalMove(AI_TACTICAL_MOVE_NONE);
			}
			else
				UnitProcessed(pUnit->GetID());

			if (pUnit->plot() == pTargetPlot)
				break;
		}
	}
}

/// Pillage an undefended improvement
bool CvTacticalAI::ExecutePillage(CvPlot* pTargetPlot)
{
	for (size_t i = 0; i < m_CurrentMoveUnits.size(); i++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
		if (pUnit && pUnit->canMoveInto(*pTargetPlot, CvUnit::MOVEFLAG_DESTINATION))
		{
			if (pUnit->shouldPillage(pTargetPlot))
			{
				ExecuteMoveToPlot(pUnit, pTargetPlot, false, CvUnit::MOVEFLAG_ABORT_IF_NEW_ENEMY_REVEALED);

				//now that the neighbor plots are revealed, maybe it's better to retreat?
				CvPlot* pSafePlot = NULL;
				if (TacticalAIHelpers::GetOtherPlayerImprovementDamage(pUnit->plot(), m_pPlayer->GetID(), true) == 0)
				{
					if (!pUnit->shouldPillage(pUnit->plot()))
						pSafePlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit, true).first;
				}

				if (pSafePlot)
				{
					//better go somewhere else
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pSafePlot->getX(), pSafePlot->getY());
					if (!pUnit->canMove())
						UnitProcessed(pUnit->GetID());
					//no use trying this target again
					return false;
				}
				else
				{
					//proceed
					pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
					if (!pUnit->canMove())
						UnitProcessed(pUnit->GetID());
					//done
					return true;
				}
			}
		}
	}

	return false;
}

/// Pillage an undefended improvement

/// Check if Morocco should plunder a trade route at this plot (respects diplomatic relationships)
bool CvTacticalAI::ShouldPlunderTradeRouteAtPlot(CvPlot* pPlot)
{
	if (!pPlot || !m_pPlayer->GetPlayerTraits()->IsCanPlunderWithoutWar())
		return true;  // Not Morocco, allow all plundering

	// Find the trade unit at this plot
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	std::vector<int> aiTradeUnitsAtPlot = GET_PLAYER(m_pPlayer->GetID()).GetTrade()->GetOpposingTradeUnitsAtPlot(pPlot, true);

	if (aiTradeUnitsAtPlot.empty())
	{
		return true;  // No trade unit, allow
	}

	// Check the first (or any) trade unit at this plot
	PlayerTypes eTradeUnitOwner = pTrade->GetOwnerFromID(aiTradeUnitsAtPlot[0]);
	if (eTradeUnitOwner == NO_PLAYER)
		return true;  // Invalid owner, allow

	TeamTypes eMoroccoTeam = m_pPlayer->getTeam();
	TeamTypes eOwnerTeam = GET_PLAYER(eTradeUnitOwner).getTeam();

	// Allow plundering enemies and neutrals
	if (GET_TEAM(eMoroccoTeam).isAtWar(eOwnerTeam))
		return true;  // At war, always allow

	// Check if it's an allied trade route (defensive pact)
	if (GET_TEAM(eMoroccoTeam).IsHasDefensivePact(eOwnerTeam))
	{
		// Don't plunder allies unless desperate
		return false;
	}

	// Check if it's a vassal trade route
	if (GET_TEAM(eMoroccoTeam).IsVassal(eOwnerTeam) || GET_TEAM(eOwnerTeam).IsVassal(eMoroccoTeam))
	{
		// Don't plunder vassals unless desperate
		return false;
	}

	// Neutral or rival - allow
	return true;
}

/// Plunder a specific trade unit
void CvTacticalAI::ExecutePlunderTradeUnit(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		if(pUnit->canMoveInto(*pTargetPlot, CvUnit::MOVEFLAG_DESTINATION ))
		{
			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTargetPlot->getX(), pTargetPlot->getY());
			if (pUnit->at(pTargetPlot->getX(), pTargetPlot->getY()))
			{
				pUnit->PushMission(CvTypes::getMISSION_PLUNDER_TRADE_ROUTE());
				//only end the turn if we can't move anymore
				if (!pUnit->canMove())
					UnitProcessed(pUnit->GetID());
			}
		}
		else if (MoveToEmptySpaceNearTarget(pUnit, pTargetPlot, NO_DOMAIN, 23))
		{
			TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit);
			//don't run away
			UnitProcessed(pUnit->GetID());
		}

	}
}

/// Paradrop in to pillage an undefended improvement
void CvTacticalAI::ExecuteParadropPillage(CvPlot* pTargetPlot)
{
	// Move first one to target
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if(pUnit)
	{
		pUnit->PushMission(CvTypes::getMISSION_PARADROP(), pTargetPlot->getX(), pTargetPlot->getY());
		pUnit->PushMission(CvTypes::getMISSION_PILLAGE());

		// Delete this unit from those we have to move
		if (!pUnit->canMove())
			UnitProcessed(pUnit->GetID());
	}
}

/// Paradrop in to capture an undefended civilian (worker, settler, etc.)
bool CvTacticalAI::ExecuteParadropCivilian(CvPlot* pTargetPlot)
{
	if (m_CurrentMoveUnits.size() == 0)
		return false;

	// Use the best paratrooper from the list
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if (!pUnit)
		return false;

	// Paradrop lands the unit on the plot, capturing any civilian there
	pUnit->PushMission(CvTypes::getMISSION_PARADROP(), pTargetPlot->getX(), pTargetPlot->getY());

	// Delete this unit from those we have to move
	if (!pUnit->canMove())
		UnitProcessed(pUnit->GetID());

	return true;
}

/// Paradrop in to capture a city at 0 HP with no garrison
bool CvTacticalAI::ExecuteParadropCityCapture(CvCity* pCity)
{
	if (!pCity || m_CurrentMoveUnits.size() == 0)
		return false;

	CvPlot* pCityPlot = pCity->plot();
	if (!pCityPlot)
		return false;

	// Use the best paratrooper from the list
	CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[0].GetID());
	if (!pUnit)
		return false;

	// Paradrop onto the city center captures it (since city is at 0 HP with no garrison)
	pUnit->PushMission(CvTypes::getMISSION_PARADROP(), pCityPlot->getX(), pCityPlot->getY());

	// Delete this unit from those we have to move
	if (!pUnit->canMove())
		UnitProcessed(pUnit->GetID());

	return true;
}

void CvTacticalAI::ExecuteAirAttack(CvPlot* pTargetPlot)
{
	if (!pTargetPlot)
		return;

	// Do air attacks, ignore all other units
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());

		if (pUnit && pUnit->getDomainType()==DOMAIN_AIR) //this includes planes and missiles. no nukes.
		{
			int iCount = 0; //failsafe
			while (pUnit->canMove() && pUnit->GetCurrHitPoints() > 30 && iCount < pUnit->getNumAttacks())
			{
				CvPlot* pBestTarget = FindAirTargetNearTarget(pUnit, pTargetPlot);
				if (pBestTarget != NULL)
				{
					//it's a ranged attack but it uses the move mission ... air units are strange
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestTarget->getX(), pBestTarget->getY());

					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("%s ATTACK: %s %d attacks target X: %d, Y: %d", pUnit->isSuicide() ? "MISSILE":"AIR" , pUnit->getName().c_str(), pUnit->GetID(), pBestTarget->getX(), pBestTarget->getY());
						LogTacticalMessage(strMsg);
					}
				}
				iCount++;
			}

			if (pUnit->getNumAttacks() - pUnit->getNumAttacksMadeThisTurn() == 0)
				UnitProcessed(m_CurrentMoveUnits[iI].GetID());
		}
	}
}

/// Queues up attacks on enemy units on or adjacent to army's desired center
/// Now with ground combat coordination: prioritizes targets based on what ground forces need
CvPlot* CvTacticalAI::FindAirTargetNearTarget(CvUnit* pUnit, CvPlot* pApproximateTargetPlot)
{
	int iRange = pUnit->GetRange();
	int iBestValue = -INT_MAX;
	CvPlot* pBestPlot = NULL;

	// Estimate total ground damage available (from m_CurrentMoveUnits which was filled before air attack)
	// This helps us coordinate air strikes with what ground forces can do
	int iTotalGroundDamage = 0;
	int iGroundMeleeCount = 0;
	int iGroundRangedCount = 0;
	for (unsigned int iGround = 0; iGround < m_CurrentMoveUnits.size(); iGround++)
	{
		CvUnit* pGroundUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iGround].GetID());
		if (!pGroundUnit || pGroundUnit->getDomainType() == DOMAIN_AIR)
			continue;
		
		int iExpectedDamage = m_CurrentMoveUnits[iGround].GetExpectedTargetDamage();
		if (iExpectedDamage > 0)
			iTotalGroundDamage += iExpectedDamage;
		
		if (pGroundUnit->IsCanAttackRanged())
			iGroundRangedCount++;
		else
			iGroundMeleeCount++;
	}

	// Loop through all appropriate targets to see if any is of concern
	for (unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		// Is the target of an appropriate type?
		if (m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT ||
			m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_ENEMY_CITY)
		{
			//make sure it is close to our actual target plot
			if (pApproximateTargetPlot)
			{
				int iTargetDistance = plotDistance(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY(), pApproximateTargetPlot->getX(), pApproximateTargetPlot->getY());
				if (iTargetDistance > 3)
					continue;
			}

			int iDistance = plotDistance(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY(), pUnit->getX(), pUnit->getY());
			if (iDistance <= iRange)
			{
				CvPlot* pTestPlot = GC.getMap().plot(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY());
				if (pTestPlot == NULL)
					continue;

				//don't beat a dead horse
				CvCity *pCity = pTestPlot->getPlotCity();
				if (pCity && pCity->getDamage() > pCity->GetMaxHitPoints() - 10)
					continue;

				CvUnit* pDefender = pUnit->rangeStrikeTarget(*pTestPlot, true);
				if (!pDefender && !pCity)
					continue;

				int iValue = 0;
				int iUnusedReferenceVariable = 0;
				int iAirDamage = 0;
				if (pUnit->AI_getUnitAIType() == UNITAI_MISSILE_AIR)
				{
					if (!pDefender)
					{
						// air missiles don't do damage to ungarrisoned cities
						continue;
					}

					//ignore the city when attacking!
					iAirDamage = pUnit->GetAirCombatDamage(pDefender, NULL, 0, iUnusedReferenceVariable, false);
					iValue = iAirDamage;
					//bonus for a kill
					if (pDefender->GetCurrHitPoints() <= iAirDamage)
						iValue += 21;
					//bonus for hitting units in cities, can only do that with missiles
					if (pDefender->plot()->isCity())
						iValue += 17;
					
					// MISSILE COST-BENEFIT: One-time use requires worthwhile targets
					// Don't waste missiles on low-value targets
					{
						int iTargetValue = 0;
						int iDefenderStrength = pDefender->GetBaseCombatStrength();
						int iMissileStrength = pUnit->GetBaseRangedCombatStrength();
						
						// Calculate target value based on unit type and situation
						UnitAITypes eDefenderAI = pDefender->AI_getUnitAIType();
						
						// HIGH PRIORITY: Enemy AA units - missiles can't be intercepted!
						// Killing AA clears the way for our bombers
						bool bTargetIsAA = (pDefender->GetAirInterceptRange() > 0 || pDefender->canIntercept());
						if (bTargetIsAA)
						{
							// Base bonus for AA
							iTargetValue += 90;
							
							// Extra bonus if we have bombers that would benefit
							int iOurBombers = m_pPlayer->GetNumUnitsWithUnitAI(UNITAI_ATTACK_AIR, true);
							int iOurFighters = m_pPlayer->GetNumUnitsWithUnitAI(UNITAI_DEFENSE_AIR, true);
							if (iOurBombers + iOurFighters >= 2)
							{
								iTargetValue += 40; // We have air units that benefit from AA removal
							}
							
							// Even more bonus for strong AA
							if (pDefender->interceptionProbability() >= 50)
							{
								iTargetValue += 30; // High intercept chance - priority kill
							}
						}
						// High-value targets: siege, ranged, carriers, generals
						else if (eDefenderAI == UNITAI_CITY_BOMBARD)
						{
							iTargetValue += 80; // Siege units are critical to eliminate
						}
						else if (eDefenderAI == UNITAI_RANGED)
						{
							iTargetValue += 50; // Ranged units deal damage without risk
						}
						else if (eDefenderAI == UNITAI_CARRIER_SEA)
						{
							iTargetValue += 100; // Carriers are extremely high value
						}
						else if (eDefenderAI == UNITAI_GENERAL || eDefenderAI == UNITAI_ADMIRAL)
						{
							iTargetValue += 120; // Generals/Admirals provide huge bonuses
						}
						else if (pDefender->IsCanAttackRanged())
						{
							iTargetValue += 40; // Other ranged
						}
						else
						{
							iTargetValue += 20; // Regular melee - lower value for missile
						}
						
						// Bonus for hitting high-strength units
						if (iDefenderStrength > iMissileStrength)
						{
							iTargetValue += (iDefenderStrength - iMissileStrength) / 2;
						}
						
						// Major bonus for kills - missile is "worth it" if it gets a kill
						if (pDefender->GetCurrHitPoints() <= iAirDamage)
						{
							iTargetValue += 50; // Kill bonus
							
							// Extra bonus for killing expensive units
							if (iDefenderStrength >= 50)
								iTargetValue += 30;
						}
						
						// Bonus for targets in cities (missiles' unique capability)
						if (pDefender->plot()->isCity())
						{
							iTargetValue += 40; // Only missiles can hit these!
							
							CvCity* pTargetCity = pDefender->plot()->getPlotCity();
							if (pTargetCity && pTargetCity->isInDangerOfFalling())
							{
								// City about to fall - clearing garrison helps capture
								iTargetValue += 60;
							}
						}
						
						// Strategic timing: missiles are most valuable during active assaults
						if (pApproximateTargetPlot && plotDistance(*pTestPlot, *pApproximateTargetPlot) <= 3)
						{
							iTargetValue += 30; // Target is near our main objective
						}
						
						// COORDINATION WITH GROUND/NAVAL ATTACKS
						// Missiles should support friendly units that are engaging this target
						{
							CvPlot* pTargetPlot = pDefender->plot();
							int iFriendlyMeleeAdjacent = pTargetPlot->GetNumFriendlyUnitsAdjacent(m_pPlayer->getTeam(), NO_DOMAIN, false);
							
							// Friendly melee units adjacent to target - they're attacking!
							if (iFriendlyMeleeAdjacent > 0)
							{
								iTargetValue += 50; // Help our troops finish the job
								
								// Extra bonus if multiple units engaging
								if (iFriendlyMeleeAdjacent >= 2)
									iTargetValue += 25;
								
								// Even more if target is wounded - coordinate for the kill
								int iDefenderHPPct = (pDefender->GetCurrHitPoints() * 100) / pDefender->GetMaxHitPoints();
								if (iDefenderHPPct <= 50)
								{
									iTargetValue += 40; // Wounded target with friendlies engaging - finish it!
								}
							}
							
							// Check if our ground forces are in range to follow up
							int iFriendlyUnitsInRange = 0;
							for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
							{
								CvPlot* pAdjacentPlot = plotDirection(pTargetPlot->getX(), pTargetPlot->getY(), (DirectionTypes)iDir);
								if (pAdjacentPlot)
								{
									// Check 2-ring for ranged that can follow up
									for (int iRing = 0; iRing < RING2_PLOTS; iRing++)
									{
										CvPlot* pRingPlot = iterateRingPlots(pAdjacentPlot, iRing);
										if (pRingPlot)
										{
											CvUnit* pFriendlyUnit = pRingPlot->getBestDefender(m_pPlayer->GetID());
											if (pFriendlyUnit && pFriendlyUnit->IsCanAttackRanged() && pFriendlyUnit->canRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY()))
											{
												iFriendlyUnitsInRange++;
											}
										}
									}
								}
							}
							
							// Ranged units can follow up our missile strike
							if (iFriendlyUnitsInRange >= 2)
							{
								iTargetValue += 35; // Good coordination opportunity
							}
							else if (iFriendlyUnitsInRange >= 1)
							{
								iTargetValue += 15;
							}
							
							// SOFTENING FOR ASSAULT: If target is blocking advance toward objective
							if (pApproximateTargetPlot)
							{
								// Is the enemy between us and our objective?
								int iTargetToObjective = plotDistance(*pTargetPlot, *pApproximateTargetPlot);
								if (iTargetToObjective <= 2)
								{
									iTargetValue += 30; // Target is blocking our path to objective
									
									// Check if city assault is imminent
									if (pApproximateTargetPlot->isCity())
									{
										CvCity* pTargetCity = pApproximateTargetPlot->getPlotCity();
										if (pTargetCity && pTargetCity->getDamage() >= pTargetCity->GetMaxHitPoints() / 2)
										{
											// City is wounded - we're close to capturing
											iTargetValue += 50; // Clear defenders for the final push!
										}
									}
								}
							}
						}
						
						// Apply value threshold: don't fire if target isn't worth the missile
						int iMinValueThreshold = 50; // Baseline for "worth firing"
						
						// Lower threshold if we're in a critical battle
						if (pApproximateTargetPlot && pApproximateTargetPlot->isCity())
						{
							CvCity* pAssaultCity = pApproximateTargetPlot->getPlotCity();
							if (pAssaultCity && m_pPlayer->IsAtWarWith(pAssaultCity->getOwner()))
							{
								iMinValueThreshold = 30; // More willing to use missiles during city assault
							}
						}
						
						if (iTargetValue < iMinValueThreshold)
						{
							// Target not worth the missile - heavy penalty
							iValue /= 3;
						}
						else
						{
							// Target is worthwhile - scale bonus by value
							iValue += iTargetValue / 2;
						}
					}
				}
				else
				{
					int iDamage = pUnit->GetAirCombatDamage(pDefender, pCity, 0, iUnusedReferenceVariable, false);
					// if the original target is a unit and we're considering attacking a city, evaluate only the damage done to the garrison
					if (pApproximateTargetPlot && !pApproximateTargetPlot->isCity() && pCity)
					{
						// Garrison absorbs part of the damage
						iDamage = (pDefender && MOD_CORE_GARRISON_DAMAGE_ABSORPTION) ? (iDamage * 2 * pDefender->GetMaxHitPoints()) / (pCity->GetMaxHitPoints() + 2 * pDefender->GetMaxHitPoints()) : 0;
					}

					//use distance as tiebreaker
					iAirDamage = iDamage;
					iValue = iAirDamage - iDistance * 3;

					if (pCity != NULL)
					{
						iValue -= pCity->GetAirStrikeDefenseDamage(pUnit, false);
					}
					else
						iValue -= pDefender->GetAirStrikeDefenseDamage(pUnit, false);

					// Check interceptor threat - this is called AFTER air sweeps, so check current status
					// GetInterceptorCount returns how many interceptors can still intercept us
					int iInterceptorCount = pTestPlot->GetInterceptorCount(pUnit->getOwner(), pUnit, false, true);
					if (iInterceptorCount > 0)
					{
						// Interceptors still active - penalize based on how many and our health
						int iUnitHPct = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
						
						if (iInterceptorCount >= 2)
						{
							// Multiple interceptors - very dangerous, heavy penalty
							iValue /= 3;
						}
						else if (iUnitHPct < 70)
						{
							// Single interceptor but we're damaged - risky
							iValue /= 2;
						}
						else
						{
							// Single interceptor and we're healthy - moderate risk
							iValue = iValue * 2 / 3;
						}
					}
					// No interceptors (sweeps cleared them or none existed) - full value!
				}

				// LAND-BASED AA CHECK: Mobile SAM, AA Gun, etc. can intercept without being air units
				// These are more dangerous than fighters because ground forces must clear them
				int iLandAACount = 0;
				int iLandAAMaxDamage = 0;
				int iInterceptRange = pUnit->GetAirInterceptRange(); // Use our range as reference
				
				for (int iDX = -iInterceptRange; iDX <= iInterceptRange; iDX++)
				{
					for (int iDY = -iInterceptRange; iDY <= iInterceptRange; iDY++)
					{
						CvPlot* pLoopPlot = plotXYWithRangeCheck(pTestPlot->getX(), pTestPlot->getY(), iDX, iDY, iInterceptRange);
						if (!pLoopPlot)
							continue;
						
						for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
						{
							CvUnit* pAAUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
							if (!pAAUnit || pAAUnit->getOwner() == pUnit->getOwner())
								continue;
							if (!GET_TEAM(pUnit->getTeam()).isAtWar(pAAUnit->getTeam()))
								continue;
							
							// Check if this is a land-based AA unit (not an air interceptor)
							if (pAAUnit->getDomainType() != DOMAIN_AIR && pAAUnit->canIntercept())
							{
								// Verify it can reach this plot
								if (plotDistance(*pLoopPlot, *pTestPlot) <= pAAUnit->GetAirInterceptRange())
								{
									iLandAACount++;
									// Calculate potential damage
									int iAADamage = pAAUnit->GetInterceptionDamage(pUnit, false, pTestPlot);
									if (iAADamage > iLandAAMaxDamage)
										iLandAAMaxDamage = iAADamage;
								}
							}
						}
					}
				}
				
				// Penalize targets protected by land-based AA
				if (iLandAACount > 0)
				{
					int iUnitHP = pUnit->GetCurrHitPoints();
					
					// High threat: AA could kill us
					if (iLandAAMaxDamage >= iUnitHP)
					{
						iValue /= 4; // Very dangerous - almost skip unless critical target
					}
					// Moderate threat: multiple AA or significant damage
					else if (iLandAACount >= 2 || iLandAAMaxDamage > iUnitHP / 2)
					{
						iValue /= 2; // Risky - needs SEAD support
					}
					// Low threat: single AA with manageable damage
					else
					{
						iValue = iValue * 2 / 3; // Some risk but acceptable
					}
				}

				// AIR-GROUND COORDINATION: Adjust value based on ground combat needs
				// Goal: Air should soften targets that ground forces will engage
				
				int iDefenderHP = pDefender->GetCurrHitPoints();
				bool bIsOnPrimaryTarget = (pTestPlot == pApproximateTargetPlot);
				bool bIsAdjacentToTarget = (pApproximateTargetPlot && plotDistance(*pTestPlot, *pApproximateTargetPlot) == 1);
				
				// Priority 1: Soften the primary target city - ground forces will definitely engage this
				if (pCity && bIsOnPrimaryTarget)
				{
					// City is the main target - air softening is highly valuable
					int iCityHP = pCity->GetMaxHitPoints() - pCity->getDamage();
					
					// Bonus for attacking city that needs softening for capture
					// If city HP > ground damage, air is critical for the assault
					if (iCityHP > iTotalGroundDamage)
					{
						int iNeededDamage = iCityHP - iTotalGroundDamage;
						// Bigger bonus the more damage we need
						iValue += min(iAirDamage, iNeededDamage) / 2;
					}
					
					// If city is close to capturable and we have melee, bonus for finishing softening
					if (iGroundMeleeCount > 0 && iCityHP <= iTotalGroundDamage + iAirDamage)
					{
						iValue += 25; // Help enable capture this turn
					}
				}
				// Priority 2: Units adjacent to primary target - these block/threaten ground assault
				else if (bIsAdjacentToTarget)
				{
					// Enemy units adjacent to target city/position threaten our attackers
					iValue += 15;
					
					// Extra bonus if this is a ranged/siege unit threatening our melee
					if (pDefender->IsCanAttackRanged())
						iValue += 10;
					
					// If we can kill it, even better - clears the approach
					if (iDefenderHP <= iAirDamage)
						iValue += 20;
				}
				// Priority 3: On the primary target (defending unit in city or on target plot)
				else if (bIsOnPrimaryTarget && pDefender)
				{
					// Garrison or defender on target - air can soften it for ground assault
					// But be careful about overkill
					if (iDefenderHP > iTotalGroundDamage)
					{
						// Ground can't kill it alone - air is needed
						iValue += 20;
					}
					else if (iDefenderHP > iTotalGroundDamage / 2)
					{
						// Ground can probably kill it but air helps reduce our losses
						iValue += 10;
					}
					// If ground can easily handle it, slight penalty to save air for other targets
					else if (iGroundRangedCount > 0)
					{
						iValue -= 5;
					}
				}
				
				// Avoid overkill: penalize targeting units that are already near death
				// if ground forces can finish them easily
				if (!pCity && iDefenderHP <= 20 && iTotalGroundDamage >= iDefenderHP * 2)
				{
					// Ground forces can easily kill this wounded unit - save air for tougher targets
					iValue -= 15;
				}
				
				// Bonus for high-threat targets that will damage our melee attackers
				if (!pCity && pDefender->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
				{
					// Enemy siege is devastating - prioritize killing it before it fires
					iValue += 25;
				}
				
				// === DIRECT GROUND ENGAGEMENT BONUS ===
				// Strongly prioritize targets that have friendly ground units actively engaging
				// This ensures air softens enemies our troops are fighting RIGHT NOW
				if (!pCity)
				{
					int iFriendlyMeleeAdjacent = pTestPlot->GetNumFriendlyUnitsAdjacent(m_pPlayer->getTeam(), DOMAIN_LAND, false, NULL);
					
					if (iFriendlyMeleeAdjacent > 0)
					{
						// Our melee units are adjacent - they're attacking this turn!
						iValue += 30; // Strong bonus for coordinated strike
						
						// More units engaging = higher priority for air support
						if (iFriendlyMeleeAdjacent >= 2)
							iValue += 15;
						
						// If our units can't kill it alone, air is critical
						if (iDefenderHP > iTotalGroundDamage)
							iValue += 20;
						
						// Bonus for helping wounded friendlies - they need fire support
						for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
						{
							CvPlot* pAdj = plotDirection(pTestPlot->getX(), pTestPlot->getY(), (DirectionTypes)iDir);
							if (pAdj)
							{
								CvUnit* pFriendly = pAdj->getBestDefender(m_pPlayer->GetID());
								if (pFriendly && pFriendly->getDomainType() == DOMAIN_LAND && pFriendly->IsCombatUnit())
								{
									int iFriendlyHP = (pFriendly->GetCurrHitPoints() * 100) / pFriendly->GetMaxHitPoints();
									if (iFriendlyHP <= 50)
									{
										// Wounded friendly engaging - urgent air support needed
										iValue += 15;
										break;
									}
								}
							}
						}
					}
					
					// Also check for friendly ranged units in range - they benefit from softening too
					int iFriendlyRangedInRange = 0;
					for (int iRing = 1; iRing <= 2; iRing++)
					{
						for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
						{
							CvPlot* pRingPlot = iterateRingPlots(pTestPlot, i);
							if (pRingPlot)
							{
								CvUnit* pFriendly = pRingPlot->getBestDefender(m_pPlayer->GetID());
								if (pFriendly && pFriendly->IsCanAttackRanged() && pFriendly->getDomainType() != DOMAIN_AIR)
								{
									if (pFriendly->canRangeStrikeAt(pTestPlot->getX(), pTestPlot->getY()))
										iFriendlyRangedInRange++;
								}
							}
						}
					}
					
					if (iFriendlyRangedInRange >= 2)
					{
						// Multiple ranged units targeting this - air softening helps them all
						iValue += 10;
					}
				}

				if (iValue > iBestValue)
				{
					iBestValue = iValue;
					pBestPlot = pTestPlot;
				}
			}			
		}
	}

	return pBestPlot;
}

void CvTacticalAI::ExecuteAirSweep(CvPlot* pTargetPlot)
{
	// Fix: use || not && - can't call methods on NULL pointer
	if (!pTargetPlot || pTargetPlot->GetInterceptorCount(m_pPlayer->GetID(), NULL, false, true) == 0)
		return;

	// Sort sweep units by strength - send strongest fighters first to maximize interceptor kills
	std::stable_sort(m_CurrentAirSweepUnits.begin(), m_CurrentAirSweepUnits.end());

	// Track interceptors remaining - stop sweeping once they're cleared
	int iInterceptorsRemaining = pTargetPlot->GetInterceptorCount(m_pPlayer->GetID(), NULL, false, true);

	// Start by sending possible air sweeps
	for (unsigned int iI = 0; iI < m_CurrentAirSweepUnits.size() && iInterceptorsRemaining > 0; iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentAirSweepUnits[iI].GetID());

		if (pUnit && pUnit->canMove())
		{
			if (pUnit->canAirSweep())
			{
				// Check unit health - don't send damaged fighters on risky sweeps
				int iHPct = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
				if (iHPct < 50 && iInterceptorsRemaining > 1)
				{
					// Damaged fighter vs multiple interceptors - skip, save for later
					continue;
				}

				pUnit->PushMission(CvTypes::getMISSION_AIR_SWEEP(), pTargetPlot->getX(), pTargetPlot->getY());
				if (pUnit->isOutOfAttacks())
					UnitProcessed(m_CurrentAirSweepUnits[iI].GetID());

				// Update interceptor count after sweep (interceptor may have been killed or used up)
				iInterceptorsRemaining = pTargetPlot->GetInterceptorCount(m_pPlayer->GetID(), NULL, false, true);

				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Air sweep with %s %d at X: %d, Y: %d - interceptors remaining: %d", 
						pUnit->getName().c_str(), pUnit->GetID(), pTargetPlot->getX(), pTargetPlot->getY(), iInterceptorsRemaining);
					LogTacticalMessage(strMsg);
				}
			}
		}
	}
}

bool CvTacticalAI::ExecuteSpotterMove(const vector<CvUnit*>& vUnits, CvPlot* pTargetPlot)
{
	if (pTargetPlot->isVisible(m_pPlayer->getTeam()))
		return true; //nothing to do

	//else find a suitable unit
	vector<CvUnit*> vCandidates;
	for (size_t i = 0; i < vUnits.size(); i++)
	{
		CvUnit* pUnit = vUnits[i];

		// we want fast units or tanks
		// (unitai defense includes ranged units ... don't use it here)
		switch (pUnit->AI_getUnitAIType())
		{
		case UNITAI_FAST_ATTACK:
		case UNITAI_SKIRMISHER:
		case UNITAI_ATTACK_SEA:
		case UNITAI_SUBMARINE:
		case UNITAI_ATTACK:
		case UNITAI_COUNTER:
			vCandidates.push_back(pUnit);
			break;
		default:
			break; // Not a candidate.
		}
	}

	vector<OptionWithScore<pair<CvUnit*, CvPlot*>>> vOptions;

	for (size_t i = 0; i < vCandidates.size(); i++)
	{
		CvUnit* pUnit = vCandidates[i];
		int iFlags = CvUnit::MOVEFLAG_NO_EMBARK;

		//move into ring 2 unless we are already there and still can't see the target
		iFlags |= plotDistance(*pTargetPlot, *pUnit->plot()) > 2 ? CvUnit::MOVEFLAG_APPROX_TARGET_RING2 : CvUnit::MOVEFLAG_APPROX_TARGET_RING1;

		if (pUnit->GeneratePath(pTargetPlot, iFlags, 2))
		{
			//try to see if we have a plot we can reach this turn and see the target
			const CvPathNodeArray& path = pUnit->GetLastPath();
			for (size_t i = 0; i < path.size(); i++)
			{
				if (path[i].m_iMoves==0) //want some movement left to retreat if required
					break;

				CvPlot* pPathPlot = GC.getMap().plotUnchecked(path[i].m_iX, path[i].m_iY);
				if (pPathPlot->canSeePlot(pTargetPlot, pUnit->getTeam(), pUnit->visibilityRange(), NO_DIRECTION))
				{
					//or should we use danger as the sorting criterion?
					vOptions.push_back(OptionWithScore<pair<CvUnit*, CvPlot*>>(make_pair(pUnit,pPathPlot),path[i].m_iMoves));
				}
			}
		}
	}

	if (!vOptions.empty())
	{
		std::stable_sort(vOptions.begin(), vOptions.end());
		ExecuteMoveToPlot(vOptions.front().option.first, vOptions.front().option.second, false, CvUnit::MOVEFLAG_NO_EMBARK);
		return true;
	}

	//last resort. use an air sweep to an adjacent plot for recon
	CvPlot** aPlotsToCheck = GC.getMap().getNeighborsUnchecked(pTargetPlot);
	for (unsigned int iI = 0; iI < m_CurrentAirSweepUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentAirSweepUnits[iI].GetID());
		for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
		{
			CvPlot* pAdjacentPlot = aPlotsToCheck[iI];
			if (pAdjacentPlot != NULL && pAdjacentPlot->isVisible(m_pPlayer->getTeam()))
			{
				if (pUnit && pUnit->canAirSweepAt(pAdjacentPlot->getX(), pAdjacentPlot->getY()))
				{
					pUnit->PushMission(CvTypes::getMISSION_AIR_SWEEP(), pAdjacentPlot->getX(), pAdjacentPlot->getY());
					CUSTOMLOG("using air sweep for recon!")
					if (pUnit->isOutOfAttacks())
						UnitProcessed(m_CurrentAirSweepUnits[iI].GetID());
					return true;
				}
			}
		}
	}

	return false;
}

bool CvTacticalAI::ExecuteAttackWithCities(CvUnit* pDefender)
{
	// Start by applying damage from city bombards
	for (unsigned int iI = 0; iI < m_CurrentMoveCities.size(); iI++)
	{
		CvCity* pCity = m_pPlayer->getCity(m_CurrentMoveCities[iI].GetID());
		if (!pCity)
			continue;

		if (pCity->canRangeStrikeAt(pDefender->getX(), pDefender->getY()) && !pCity->isMadeAttack())
		{
			pCity->doTask(TASK_RANGED_ATTACK, pDefender->getX(), pDefender->getY(), false);
			if (pDefender->GetCurrHitPoints() < 1)
				return true;
		}
	}

	//not killed
	return false;
}

//evaluate many possible unit assignments around the target plot and choose the best one
//will not necessarily attack only the target plot when other targets are present!
bool CvTacticalAI::ExecuteAttackWithUnits(CvPlot* pTargetPlot, eAggressionLevel eAggLvl)
{
	vector<CvUnit*> vUnits;
	for (size_t i=0; i<m_CurrentMoveUnits.size(); i++)
		if (m_CurrentMoveUnits[i].GetAttackStrength()>=0) //sometimes we mark units as unnecessary
			vUnits.push_back( m_pPlayer->getUnit( m_CurrentMoveUnits[i].GetID() ) );

	//try to improve visibility
	if (!ExecuteSpotterMove(vUnits,pTargetPlot))
		return false;

	// Issue 4.2: If multiple units can coordinate on this target, increase aggression level
	if (FindCoordinatedAttackOpportunity(pTargetPlot, vUnits))
	{
		if (eAggLvl == AL_LOW)
			eAggLvl = AL_MEDIUM;
		else if (eAggLvl == AL_MEDIUM)
			eAggLvl = AL_HIGH;
	}

	//first handle air units (including missiles)
	ExecuteAirSweep(pTargetPlot);
	ExecuteAirAttack(pTargetPlot);

	//did the air attack already kill the enemy?
	if (pTargetPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true, true) == NULL && !pTargetPlot->isCity())
		return true;

#if defined(MOD_CORE_DEBUGGING)
	if (MOD_CORE_DEBUGGING)
		LogTacticalMessage(CvString::format("trying attack on %d:%d, agg level %d", pTargetPlot->getX(), pTargetPlot->getY(), eAggLvl));
#endif

	return TacticalAIHelpers::FindAndExecuteBestUnitAssignments(m_pPlayer->GetID(), vUnits, pTargetPlot, eAggLvl);
}

//target can be friendly, neutral or hostile
bool CvTacticalAI::PositionUnitsAroundTarget(const vector<CvUnit*>& vUnits, CvPlot* pTarget)
{
	//try to improve visibility. however, if the target is too far away this may fail ... in that case we chance it
	ExecuteSpotterMove(vUnits, pTarget);

	if (MOD_CORE_DEBUGGING)
		LogTacticalMessage(CvString::format("seeking defensive positioning around %d:%d", pTarget->getX(), pTarget->getY()));

	//first round: in case there are enemies around, do a combat simulation
	vector<CvUnit*> vSimUnits = vUnits; //make a copy we can modify!
	bool bTactSimSuccess = TacticalAIHelpers::FindAndExecuteBestUnitAssignments(m_pPlayer->GetID(), vSimUnits, pTarget, AL_LOW);

	//sometimes tactsim cannot use all units, eg if they are too far out
	vector<CvUnit*> farout;
	bool bHaveNavalEscort = false;
	for (vector<CvUnit*>::const_iterator it = vUnits.begin(); it != vUnits.end(); ++it)
	{
		CvUnit* pUnit = *it;

		//also include units we already moved ...
		if (pUnit->IsCombatUnit() && pUnit->getDomainType() == DOMAIN_SEA)
			bHaveNavalEscort = true;

		if (pUnit->TurnProcessed())
			continue;
		
		// This plot distance function call is valid since the update function was called in FindAndExecuteBestUnitAssignments
		if (bTactSimSuccess && TacticalAIHelpers::GetPlotDistanceToTarget(pUnit->plot()->GetPlotIndex(), pUnit->getDomainType()) <= TACTICAL_COMBAT_MAX_TARGET_DISTANCE)
			continue; //do not end the turn ... we may want to shuffle them around later

		farout.push_back(pUnit);
	}

	//we want to move the civilians last so they have a better chance of getting cover
	struct PrSortCombatFirst
	{
		bool operator()(const CvUnit* lhs, const CvUnit* rhs) const 
			{ return (lhs->IsCivilianUnit() ? 2 : lhs->AI_getUnitAIType()==UNITAI_CITY_BOMBARD ? 1 : 0) < (rhs->IsCivilianUnit() ? 2 : rhs->AI_getUnitAIType() == UNITAI_CITY_BOMBARD ? 1 : 0); }
	};
	std::stable_sort(farout.begin(), farout.end(), PrSortCombatFirst());

	//second round: move in as long as there is no danger and we're still far away
	for (vector<CvUnit*>::const_iterator it = farout.begin(); it != farout.end(); ++it)
	{
		//lots of flags ...
		CvUnit* pUnit = *it;
		int	iFlags = CvUnit::MOVEFLAG_NO_STOPNODES | CvUnit::MOVEFLAG_APPROX_TARGET_RING2;
		if (pUnit->isNativeDomain(pTarget)) //don't embark if we don't have to
			iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN;
		if (pUnit->IsCivilianUnit())
			iFlags |= (CvUnit::MOVEFLAG_DONT_STACK_WITH_NEUTRAL | CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER);
		if (!bHaveNavalEscort && pUnit->getDomainType()==DOMAIN_LAND)
			iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;

		//since we know the unit was far out originally, this is guaranteed to be actual movement
		if (!pUnit->GeneratePath(pTarget, iFlags, GetRecruitRange()))
			continue;

		//we are not here to fight or flee, let other moves take over
		int iDanger = pUnit->GetDanger(pUnit->GetPathEndFirstTurnPlot());
		int iDangerLimit = (pUnit->IsCanAttack() && pUnit->AI_getUnitAIType()!=UNITAI_CITY_BOMBARD) ? pUnit->GetCurrHitPoints() / 2 : 0;
		//generals and siege should not even be in fog danger
		if (iDanger > iDangerLimit)
			continue;

		//embark only when it's safe
		CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->GetPathEndFirstTurnPlot());
		if (pZone && pZone->GetOverallDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY && !pUnit->isEmbarked())
			iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;

		if (ExecuteMoveToPlot(pUnit, pTarget, false, iFlags) != INT_MAX)
			UnitProcessed(pUnit->GetID());
	}

	//third round: if the unit is in an army (no tactical moves) and did not move yet, move it to safety now
	for (vector<CvUnit*>::const_iterator it = vUnits.begin(); it != vUnits.end(); ++it)
	{
		CvUnit* pUnit = *it;
		//don't move in further if we're already close
		if (pUnit->TurnProcessed() || pUnit->getArmyID() == -1)
			continue;

		//only flee if we're in danger, and not too far ideally
		if (pUnit->GetDanger() > pUnit->ActualHealRate(pUnit->plot()) || (!pUnit->IsCombatUnit() && pUnit->plot()->getNumDefenders(pUnit->getOwner()) == 0))
		{
			//units are not typically hurt but this is convenient function to move out of danger
			//MoveToSafestPlot() may cause units to run too far
			CvPlot* pPlot = TacticalAIHelpers::FindClosestSafePlotForHealing(pUnit, false).first;
			//second chance for emergencies
			if (!pPlot)
				pPlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit, false, false).first;
			if (pPlot)
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pPlot->getX(), pPlot->getY(), 0, false, false, MISSIONAI_TACTMOVE);
		}

		if (pUnit->canMove())
			pUnit->PushMission(CvTypes::getMISSION_SKIP());
		pUnit->SetTurnProcessed(true);
	}

	return bTactSimSuccess;
}

void CvTacticalAI::ExecuteLandingOperation(CvPlot* pTargetPlot)
{
	if (!pTargetPlot)
		return;

	CvCity* pCapturedCity = (pTargetPlot->getOwner() == m_pPlayer->GetID() && pTargetPlot->isCity()) ? pTargetPlot->getPlotCity() : NULL;
	CvUnit* pExistingGarrison = pCapturedCity ? pCapturedCity->GetGarrisonedUnit() : NULL;
	CvUnit* pImmediateDefender = pCapturedCity ? pTargetPlot->getBestDefender(m_pPlayer->GetID()) : NULL;
	const bool bHasLandGarrison = (pExistingGarrison && pExistingGarrison->getDomainType() == DOMAIN_LAND);
	const bool bHasRangedLandGarrison = (bHasLandGarrison && pExistingGarrison->IsCanAttackRanged());
	const bool bHasImmediateLandDefender = (pImmediateDefender && pImmediateDefender->getDomainType() == DOMAIN_LAND);

	struct SAssignment
	{
		SAssignment( CvUnit* unit, CvPlot* plot, int score, bool isAttack ) : pUnit(unit), pPlot(plot), iScore(score), bAttack(isAttack) {}
		CvUnit* pUnit;
		CvPlot* pPlot;
		int iScore;
		bool bAttack;
		bool operator<(const SAssignment& rhs) const { return iScore>rhs.iScore; }
	};

	struct PrPlotMatch
	{
		PrPlotMatch(CvPlot* refPlot) : pRefPlot(refPlot) {}
		CvPlot* pRefPlot;
		bool operator()(const SAssignment& other) { return pRefPlot==other.pPlot; } 
	};

	struct PrUnitMatch
	{
		PrUnitMatch(CvUnit* refUnit) : pRefUnit(refUnit) {}
		CvUnit* pRefUnit;
		bool operator()(const SAssignment& other) { return pRefUnit==other.pUnit; } 
	};

	vector<SAssignment> choices;
	for (size_t i=0; i<m_CurrentMoveUnits.size(); i++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[i].GetID());
		if (!pUnit)
			continue;

		//first check our immediate neighborhood (ie the tiles we can reach within one turn)
		ReachablePlots eligiblePlots = pUnit->GetAllPlotsInReachThisTurn(true, true, false);
		for (ReachablePlots::const_iterator tile=eligiblePlots.begin(); tile!=eligiblePlots.end(); ++tile)
		{
			CvPlot* pEvalPlot = GC.getMap().plotByIndexUnchecked(tile->iPlotIndex);
			ASSERT(pEvalPlot != NULL, "plotByIndexUnchecked returned null - invalid plot index");
			if (!pEvalPlot->isCoastalLand())
				continue;

			const uint32 plotFlags = pEvalPlot->GetPlotCacheFlags();
			
			int iBonus = plotDistance(*pEvalPlot,*pTargetPlot) * (-10);
			if (pCapturedCity)
			{
				if (pEvalPlot == pTargetPlot)
				{
					iBonus += 40;
					if (pUnit->CanGarrison())
					{
						iBonus += 40;
						if (pUnit->IsCanAttackRanged())
							iBonus += 70;
						else
							iBonus += 15;
					}

					if (bHasRangedLandGarrison)
						iBonus -= 90;
					else if (bHasLandGarrison || bHasImmediateLandDefender)
						iBonus -= 40;
				}
				else if (plotDistance(*pEvalPlot, *pTargetPlot) == 1 && pUnit->IsCanAttackRanged())
				{
					iBonus += 25;
				}
			}
			if (pUnit->IsCanAttackRanged())
			{
				if (pEvalPlot->getArea()!=pTargetPlot->getArea() && plotDistance(*pEvalPlot,*pTargetPlot)>pUnit->GetRange())
					continue;

				if (plotFlags & CvPlot::PLOT_CACHE_HILLS)
					iBonus += 20;
			}
			else
			{
				if (pEvalPlot->getArea()!=pTargetPlot->getArea())
					continue;
			}

			bool bAttack = pEvalPlot->isEnemyCity(*pUnit);
			CvUnit* pDefender = pEvalPlot->getBestDefender(NO_PLAYER);
			if (pDefender)
			{
				if ( m_pPlayer->IsAtWarWith(pDefender->getOwner()) )
					bAttack = true;
				else
					continue; //must be a neutral unit or one of ours
			}

			if (bAttack && pUnit->IsCanAttackWithMove())
			{
				//check if attack makes sense
				if (TacticalAIHelpers::IsAttackNetPositive(pUnit,pEvalPlot,0))
				{
					choices.push_back( SAssignment(pUnit,pEvalPlot,pUnit->GetMaxHitPoints()+1,true) );
				}
			}
			else if (!bAttack)
			{
				//check danger
				int iScore = pUnit->GetMaxHitPoints() - pUnit->GetDanger(pEvalPlot) + iBonus;
				if (iScore>0)
					choices.push_back( SAssignment(pUnit,pEvalPlot,iScore,false) );
			}
		}
	}

	//prefer non-isolated plots
	for (vector<SAssignment>::iterator it=choices.begin(); it!=choices.end(); ++it)
	{
		for (vector<SAssignment>::iterator it2=choices.begin(); it2!=choices.end(); ++it2)
		{
			if (it2!=it && it2->pPlot->isAdjacent(it->pPlot))
				it2->iScore += 10;
		}

		if (it->pPlot->IsFriendlyUnitAdjacent(m_pPlayer->getTeam(),true))
			it->iScore += 10;
	}

	//ok let's go
	std::stable_sort(choices.begin(),choices.end());
	while (!choices.empty())
	{
		SAssignment next = choices.front();

		vector<SAssignment>::iterator last;
		last = remove_if( choices.begin(), choices.end(), PrPlotMatch(next.pPlot) ); choices.erase(last,choices.end());
		last = remove_if( choices.begin(), choices.end(), PrUnitMatch(next.pUnit) ); choices.erase(last,choices.end());

		next.pUnit->PushMission( CvTypes::getMISSION_MOVE_TO(), next.pPlot->getX(), next.pPlot->getY() );
		if (!next.pUnit->canMove()) //not all units end their turn after disembark - they can still be used for other moves!
			UnitProcessed(next.pUnit->GetID());
	}

	//note that it's possible some units were not moved because of conflicts
}

/// Execute moving units to a better location
void CvTacticalAI::ExecuteRepositionMoves()
{
	//don't be too predictable
	int shuffledIndex[RING4_PLOTS - RING1_PLOTS];
	for (int i = RING1_PLOTS; i < RING4_PLOTS; i++)
		shuffledIndex[i-RING1_PLOTS] = i;

	int iNumPlots = RING4_PLOTS - RING1_PLOTS;
	for (int i = 0; i < iNumPlots - 1; i++)
	{
		int iSwapIndex = GC.getGame().randRangeExclusive(0, iNumPlots - i, CvSeeder(m_CurrentMoveUnits.size()).mix(i));
		std::swap<int>(shuffledIndex[i], shuffledIndex[iSwapIndex]);
	}

	for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if (!pUnit)
			continue;

		//any cities we can reinforce?
		CvPlot* pTarget = FindNearbyTarget(pUnit, 12, false);
		if (!pTarget)
			continue;

		//already close ...
		if (plotDistance(*pTarget, *pUnit->plot()) < 5 && TacticalAIHelpers::IsGoodPlotForStaging(m_pPlayer, pUnit->plot(), pUnit->getDomainType()))
		{
			pUnit->PushMission(CvTypes::getMISSION_SKIP());
			UnitProcessed(m_CurrentMoveUnits[iI].GetID());
			continue;
		}

		//find a good spot
		for (int i = 0; i<iNumPlots; i++)
		{
			CvPlot* pTestPlot = iterateRingPlots(pTarget, shuffledIndex[i]);
			if (!pTestPlot)
				continue;

			if (pUnit->IsCanAttackRanged())
				if (pTestPlot->IsAdjacentOwnedByTeamOtherThan(m_pPlayer->getTeam()))
					continue;

			//staging is not fighting ...
			if (pUnit->GetDanger(pTestPlot) > pUnit->GetCurrHitPoints()/5)
				continue;

			if (TacticalAIHelpers::IsGoodPlotForStaging(m_pPlayer, pTestPlot, pUnit->getDomainType()))
			{
				if(GC.getLogging() && GC.getAILogging() && m_pPlayer->isMajorCiv())
				{
					CvString strTemp = pUnit->getUnitInfo().GetDescription();
					CvString strLogString;
					strLogString.Format("%s moving to reinforce city at, X: %d, Y: %d, Current X: %d, Current Y: %d", 
						strTemp.GetCString(), pTestPlot->getX(), pTestPlot->getY(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString);
				}

				ExecuteMoveToPlot(pUnit, pTestPlot);
				UnitProcessed(m_CurrentMoveUnits[iI].GetID());
				break;
			}
		}
	}
}

/// Moves units to the hex with the lowest danger
void CvTacticalAI::ExecuteMovesToSafestPlot(CvUnit* pUnit)
{
	if (!pUnit)
		return;
	CvPlot* pUnitPlot = pUnit->plot();

	//see if we can do damage before retreating (skip only if imminent AND unit can't move after attacking)
	if (pUnit->canMoveAfterAttacking() && (pUnit->getMoves()>GD_INT_GET(MOVE_DENOMINATOR) || pUnit->IsFreeAttackMoves()) && pUnit->canRangeStrike())
		TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit,true);

	//so easy
	pair<CvPlot*, int> pBestPlotMove = TacticalAIHelpers::FindSafestPlotInReach(pUnit, true, true);
	if (pBestPlotMove.first)
	{
		//check if we need to bump somebody else
		CvUnit* pBumpUnit = pUnit->GetPotentialUnitToPushOut(*pBestPlotMove.first);
		if (pBumpUnit)
		{
			if (pUnit->PushBlockingUnitOutOfPlot(*pBestPlotMove.first))
			{
				UnitProcessed(pUnit->GetID());
				return;
			}
		}

		int iMovesRemaining = pBestPlotMove.second;

		//pillage before retreat, if we have movement points to spare
		if ((pUnit->hasFreePillageMove() || iMovesRemaining > GD_INT_GET(MOVE_DENOMINATOR)) && pUnit->shouldPillage(pUnitPlot))
		{
			pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
			if (!pUnit->hasFreePillageMove())
				iMovesRemaining -= GD_INT_GET(MOVE_DENOMINATOR);
		}

		//typical citadel case
		if (pUnitPlot == pBestPlotMove.first)
		{
			// When imminent, only shoot if we won't need to move (unit stays put anyway)
			if (pUnit->GetCurrHitPoints() > pUnit->GetMaxHitPoints() * 3 / 5)
				TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit);

			//make sure the unit stays put!
			pUnit->PushMission(CvTypes::getMISSION_SKIP());
		}
		else
		{
			//try to do some damage if we have movement points to spare
			if ((iMovesRemaining > GD_INT_GET(MOVE_DENOMINATOR) || pUnit->IsFreeAttackMoves()) && pUnit->canRangeStrike() && pUnit->canMoveAfterAttacking())
				TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, false, iMovesRemaining);

			// Move to the lowest danger value found
			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestPlotMove.first->getX(), pBestPlotMove.first->getY(), 0, false, false, MISSIONAI_TACTMOVE);

			//pillage after retreat, if we have movement points to spare
			if (pUnit->shouldPillage(pUnitPlot, false, true) && (pUnit->getMoves() > GD_INT_GET(MOVE_DENOMINATOR) || pUnit->IsFreeAttackMoves() || !pUnit->canRangeStrike()))
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());

			//see if we can do damage after retreating (safe: we already moved)
			if (pUnit->canMove() && pUnit->canRangeStrike())
				TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, true);

			//pillage after retreat, if we have movement points to spare
			if (pUnit->shouldPillage(pUnit->plot(), false, true))
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
		}

		UnitProcessed(pUnit->GetID());
	}
	else if (pUnit->GetDanger() < min(pUnit->GetCurrHitPoints() + pUnit->ActualHealRate(pUnitPlot), pUnit->GetMaxHitPoints()))
	{
		//do nothing and hope for the best
		pUnit->PushMission(CvTypes::getMISSION_SKIP());
		UnitProcessed(pUnit->GetID());
	}
	else //no good plot found
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			CvString strTemp;
			strTemp = GC.getUnitInfo(pUnit->getUnitType())->GetDescription();
			strLogString.Format("Failed to find destination moving %s to safety from, X: %d, Y: %d", strTemp.GetCString(), pUnit->getX(), pUnit->getY());
			LogTacticalMessage(strLogString);
		}

		//try to go home
		if(pUnitPlot->getOwner() != pUnit->getOwner())
		{
			CvCity* pClosestCity = m_pPlayer->GetClosestCityByPathLength(pUnitPlot);
			if (m_pPlayer->isMinorCiv())
				pClosestCity = m_pPlayer->getCapitalCity();

			CvPlot* pMovePlot = pClosestCity ? pClosestCity->plot() : NULL;
			if(pMovePlot != NULL)
				MoveToEmptySpaceNearTarget(pUnit,pMovePlot,DOMAIN_LAND,42,true);
			else
				pUnit->PushMission(CvTypes::getMISSION_SKIP());

			UnitProcessed(pUnit->GetID());
		}
	}
}

/// Heal chosen units
void CvTacticalAI::ExecuteHeals(bool bFirstPass)
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if (!pUnit)
			continue;

		pair<CvPlot*, int> pBetterPlotMove = make_pair(static_cast<CvPlot*>(NULL), 0);

		//need to split from army?
		if (pUnit->getArmyID() != -1)
		{
			CvArmyAI* pArmy = m_pPlayer->getArmyAI(pUnit->getArmyID());
			if (pArmy)
			{
				//Don't do this for civilan operations!
				CvAIOperation* AIOperation = m_pPlayer->getAIOperation(pArmy->GetOperationID());
				if (AIOperation && AIOperation->IsCivilianOperation())
					continue;

				if (pArmy->GetArmyAIState() != ARMYAISTATE_WAITING_FOR_UNITS_TO_REINFORCE)
					pArmy->RemoveUnit(pUnit->GetID(), false);
			}
		}

		//find a suitable spot for healing
		if (pUnit->getDomainType() == DOMAIN_LAND)
		{
			if (pUnit->GetDamageAoEFortified() > 0 && pUnit->canFortify(pUnit->plot()) &&
				pUnit->GetDanger() < pUnit->GetCurrHitPoints() + pUnit->ActualHealRate(pUnit->plot()) &&
				pUnit->plot()->GetNumEnemyUnitsAdjacent(pUnit->getTeam(), pUnit->getDomainType()) > 1)
			{
				//units with area damage if fortified should fortify as much as possible if near enemies
				pUnit->PushMission(CvTypes::getMISSION_FORTIFY());
				UnitProcessed(pUnit->GetID());
				continue;
			}

			//try opportunistic attacks if there is only one nearby unit
			if (pUnit->GetDanger() > 0 && !pUnit->isEmbarked())
			{
				std::vector<CvUnit*> vAttackers = m_pPlayer->GetPossibleAttackers(*pUnit->plot(),m_pPlayer->getTeam());
				if (vAttackers.size() == 1 && TacticalAIHelpers::KillLoneEnemyIfPossible(pUnit, vAttackers[0]))
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Healing unit %s (%d) counterattacked pursuer at X: %d, Y: %d",
							pUnit->getName().GetCString(), pUnit->GetID(), vAttackers[0]->getX(), vAttackers[0]->getY());
						LogTacticalMessage(strLogString);
					}
				}
			}

			pBetterPlotMove = TacticalAIHelpers::FindClosestSafePlotForHealing(pUnit);
		}
		else if (pUnit->getDomainType()==DOMAIN_SEA)
		{
			if (pUnit->GetDanger()>0 || pUnit->ActualHealRate(pUnit->plot()) == 0)
			{
				std::vector<CvUnit*> vAttackers = m_pPlayer->GetPossibleAttackers(*pUnit->plot(),m_pPlayer->getTeam());
				//try to turn the tables on him
				if (vAttackers.size() == 1 && TacticalAIHelpers::KillLoneEnemyIfPossible(pUnit, vAttackers[0]))
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Healing unit %s (%d) counterattacked pursuer at X: %d, Y: %d",
							pUnit->getName().GetCString(), pUnit->GetID(), vAttackers[0]->getX(), vAttackers[0]->getY());
						LogTacticalMessage(strLogString);
					}
				}
				else
				{
					//why not pillage some tiles?
					if (pUnit->shouldPillage(pUnit->plot()))
					{
						pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
						if (GC.getLogging() && GC.getAILogging())
						{
							CvString strMsg;
							strMsg.Format("Heal: pillage with %s before move, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
							LogTacticalMessage(strMsg);
						}
					}
				}

				pBetterPlotMove = TacticalAIHelpers::FindClosestSafePlotForHealing(pUnit);
			}
		}

		//now finally do something
		if (pBetterPlotMove.first)
		{
			if (pBetterPlotMove.first != pUnit->plot())
			{
				//ranged attack before fleeing for fast units
				if (pUnit->canMoveAfterAttacking() && (pBetterPlotMove.second > GD_INT_GET(MOVE_DENOMINATOR) || pUnit->IsFreeAttackMoves()) && pUnit->canRangeStrike())
					TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, false, pBetterPlotMove.second);

				CvUnit* pPushUnit = pUnit->GetPotentialUnitToPushOut(*pBetterPlotMove.first);
				if (pPushUnit)
					pUnit->PushBlockingUnitOutOfPlot(*pBetterPlotMove.first);
				else //plot should be free
					ExecuteMoveToPlot(pUnit, pBetterPlotMove.first);
			}
			else
				//this is required to flush the previous mission!
				pUnit->PushMission(CvTypes::getMISSION_SKIP());

			UnitProcessed(pUnit->GetID());
		}
		//no safe plot to heal ...
		else if (!bFirstPass && pUnit->getDomainType() != DOMAIN_AIR && pUnit->GetDanger() > /*10*/ GD_INT_GET(NEUTRAL_HEAL_RATE))
		{
			//why not pillage more tiles?
			if (pUnit->shouldPillage(pUnit->plot()))
			{
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
			
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Heal: pillage with %s after move, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strMsg);
				}
			}

			//at least try to flee if we're not needed
			if (!pUnit->IsCoveringFriendlyCivilian())
			{
				pBetterPlotMove = TacticalAIHelpers::FindSafestPlotInReach(pUnit, true);
				if (pBetterPlotMove.first && pBetterPlotMove.first != pUnit->plot())
					ExecuteMoveToPlot(pUnit, pBetterPlotMove.first);
			}

			if (!pUnit->canMove())
				UnitProcessed(pUnit->GetID());
		}
	}
}

/// Move barbarian to faraway targets 
void CvTacticalAI::ExecuteBarbarianRoaming()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit && pUnit->isBarbarian()) //combat and captured civilians both
		{
			// LAND MOVES
			if(pUnit->getDomainType() == DOMAIN_LAND)
			{
				CvPlot* pPlot = pUnit->plot();
				if(pPlot && (pPlot->getImprovementType() == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT) || pPlot->isCity()))
				{
					pUnit->PushMission(CvTypes::getMISSION_SKIP());
					UnitProcessed(pUnit->GetID());
					//do it this way to avoid a warning
					pUnit->setTacticalMove(AI_TACTICAL_MOVE_NONE);
					pUnit->setTacticalMove(AI_TACTICAL_BARBARIAN_CAMP);
					continue;
				}

				//where to?
				CvPlot* pBestPlot = FindBestBarbarianLandTarget(pUnit);
				if(!pBestPlot)
					continue;
					
				//civilian to capture?
				bool bTargetIsCombat = pBestPlot->isEnemyUnit(BARBARIAN_PLAYER, true, true);
				bool bTargetIsCivilian = pBestPlot->isEnemyUnit(BARBARIAN_PLAYER, false, true);
				bool bTargetIsImprovement = pBestPlot->getImprovementType() != NO_IMPROVEMENT && !pBestPlot->IsImprovementPillaged();

				//just move in if we can
				if ((bTargetIsCivilian || bTargetIsImprovement) && !bTargetIsCombat)
				{
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pBestPlot->getX(), pBestPlot->getY());
					if (pUnit->canMove() && pUnit->at(pBestPlot->getX(), pBestPlot->getY()))
						pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
				}
				//move towards the target but don't hang around there
				//in fact, since we apparently did not attack this turn, we should continue roaming
				else if (bTargetIsCombat && plotDistance(*pBestPlot, *pUnit->plot()) > 1)
				{
					MoveToEmptySpaceNearTarget(pUnit, pBestPlot, DOMAIN_LAND, 12);
				}

				//hit and run, if you can
				if (!pUnit->canMove())
					UnitProcessed(m_CurrentMoveUnits[iI].GetID());
			}
			// NAVAL MOVES
			else
			{
				CvPlot* pBestPlot = FindBestBarbarianSeaTarget(pUnit);
				if(!pBestPlot)
					continue;

				//no naval pillaging, it's just too annoying
				//same logic as above, if we're already at the target we don't end the turn but move to safety
				bool bTargetIsCombat = pBestPlot->isEnemyUnit(BARBARIAN_PLAYER, true, true);
				if (!bTargetIsCombat || plotDistance(*pBestPlot, *pUnit->plot()) > 1)
				{
					if (MoveToEmptySpaceNearTarget(pUnit, pBestPlot, DOMAIN_SEA, 12))
					{
						TacticalAIHelpers::PerformOpportunityAttack(pUnit, true);
						UnitProcessed(m_CurrentMoveUnits[iI].GetID());
					}
				}
			}
		}
	}
}

/// Move unit to a specific tile, return turns remaining
int CvTacticalAI::ExecuteMoveToPlot(CvUnit* pUnit, CvPlot* pTarget, bool bSetProcessed, int iFlags)
{
	int iResult = INT_MAX; //impossible

	if(!pUnit || !pTarget)
		return iResult;

	//for inspection in GUI
	pUnit->SetMissionAI(MISSIONAI_TACTMOVE, pTarget, NULL);

	// Unit already at target plot?
	if(pTarget == pUnit->plot() && pUnit->canEndTurnAtPlot(pTarget))
	{
		iResult = 0;

		TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit);
		pUnit->PushMission(CvTypes::getMISSION_SKIP());
	}
	else if (pUnit->canMoveInto(*pTarget, CvUnit::MOVEFLAG_DESTINATION) || (iFlags&CvUnit::MOVEFLAG_APPROX_TARGET_RING1) || (iFlags&CvUnit::MOVEFLAG_APPROX_TARGET_RING2))
	{
		int iTurns = INT_MAX;
		if (pUnit->GeneratePath(pTarget,iFlags,INT_MAX,&iTurns))
		{
			//pillage if it makes sense and we have movement points to spare
			if (pUnit->shouldPillage(pUnit->plot(), true, true) && (pUnit->hasFreePillageMove() || pUnit->GetMovementPointsAtCachedTarget()>=GD_INT_GET(MOVE_DENOMINATOR)))
			{
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
				
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strMsg;
					strMsg.Format("Move To Plot: pillage with %s, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strMsg);
				}
			}

			pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), iFlags, false, false, MISSIONAI_TACTMOVE, pTarget);
			iResult = iTurns - 1;

			bool bAlreadyThere = false;
			if (iFlags&CvUnit::MOVEFLAG_APPROX_TARGET_RING2)
				bAlreadyThere = (plotDistance(*pUnit->plot(),*pTarget)<3);
			else if  (iFlags&CvUnit::MOVEFLAG_APPROX_TARGET_RING1)
				bAlreadyThere = (plotDistance(*pUnit->plot(),*pTarget)<2);
			else
				bAlreadyThere = pUnit->at(pTarget->getX(), pTarget->getY());

			//typically because of MOVEFLAG_ABORT_IN_DANGER and newly revealed enemies ...
			if (bSetProcessed && !bAlreadyThere && pUnit->canMove() && pUnit->getArmyID()==-1)
			{
				//try to go to a better place if we're sure the unit will not be moved again this turn
				pTarget = TacticalAIHelpers::FindSafestPlotInReach(pUnit, true).first;
				if (pTarget)
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), 0 /*no approximate flags*/, false, false, MISSIONAI_TACTMOVE, pTarget);
			}
		}
		//maybe units are blocking our way? 
		else if (pUnit->GeneratePath(pTarget,iFlags|CvUnit::MOVEFLAG_IGNORE_STACKING_SELF,INT_MAX,&iTurns))
		{
			//already close? try to push the other unit out
			if (iTurns == 0)
			{
				CvUnit* pPushUnit = pUnit->GetPotentialUnitToPushOut(*pTarget);
				if (pPushUnit && pUnit->PushBlockingUnitOutOfPlot(*pTarget))
					iResult = 0;
			}
			else
			{
				//try to find a good plot in the direction of the target and hope the block clears
				CvPlot* pWorkaround = pUnit->GetLastValidDestinationPlotInCachedPath();
				if (pWorkaround)
				{
					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), iFlags, false, false, MISSIONAI_TACTMOVE, pTarget);
					if (bSetProcessed || !pUnit->canMove())
						UnitProcessed(pUnit->GetID());
					iResult = iTurns-1;
				}
			}
		}

		if(iResult==0 && pUnit->canMove())
			TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit);
	}

	if (bSetProcessed)
		UnitProcessed(pUnit->GetID());

	return iResult;
}

/// Find an adjacent hex to move a blocking unit to
bool CvTacticalAI::ExecuteMoveOfBlockingUnit(CvUnit* pBlockingUnit, CvPlot* pPreferredDirection)
{
	if(!pBlockingUnit->canMove())
	{
		return false;
	}

	CvPlot* pOldPlot = pBlockingUnit->plot();

	std::vector<SPlotWithScore> vCandidates;

	for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		CvPlot* pPlot = plotDirection(pBlockingUnit->getX(), pBlockingUnit->getY(), ((DirectionTypes)iI));
		if(pPlot != NULL)
		{
			if (pPreferredDirection)
				vCandidates.push_back( SPlotWithScore(pPlot,plotDistance(pPreferredDirection->getX(),pPreferredDirection->getY(),pPlot->getX(),pPlot->getY())) );
			else
				vCandidates.push_back( SPlotWithScore(pPlot,0) );
		}
	}

	std::stable_sort(vCandidates.begin(),vCandidates.end());

	for (std::vector<SPlotWithScore>::const_iterator it=vCandidates.begin(); it!=vCandidates.end(); ++it)
	{
		CvPlot* pPlot = it->pPlot;

		// Don't embark for one of these moves
		if (!pOldPlot->isWater() && pPlot->isWater() && pBlockingUnit->getDomainType() == DOMAIN_LAND)
		{
			continue;
		}

		// Has to be somewhere we can move and be empty of other units/enemy cities
		if(!pPlot->getVisibleEnemyDefender(m_pPlayer->GetID()) && !pPlot->isEnemyCity(*pBlockingUnit) && pBlockingUnit->GeneratePath(pPlot))
		{
			ExecuteMoveToPlot(pBlockingUnit, pPlot);
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strTemp;
				CvString strLogString;
				strTemp = pBlockingUnit->getUnitInfo().GetDescription();
				strLogString.Format("Moving blocking %s out of way, Leaving X: %d, Y: %d, Now At X: %d, Y: %d", strTemp.GetCString(), pOldPlot->getX(), pOldPlot->getY(), pBlockingUnit->getX(), pBlockingUnit->getY());
				LogTacticalMessage(strLogString);
			}
			return true;
		}
	}
	return false;
}

/// Move unit to protect a specific tile
void CvTacticalAI::ExecuteNavalBlockadeMove(CvPlot* pTarget)
{
	if (!pTarget)
		return;

	for (unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if (pUnit && pUnit->canUseForTacticalAI())
		{
			//see if we can harrass the enemy first
			if (pUnit->shouldPillage(pUnit->plot()))
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());

			//safety check
			if (pUnit->GetDanger(pTarget) <= pUnit->GetCurrHitPoints())
			{
				//make sure we have a valid path
				if (!pUnit->GeneratePath(pTarget))
					continue;

				if (pUnit->GetDanger(pUnit->GetPathEndFirstTurnPlot()) <= pUnit->GetCurrHitPoints())
				{
					if (GC.getLogging() && GC.getAILogging())
					{
						CvString strMsg;
						strMsg.Format("Naval blockade at %d:%d with %s at %d:%d", pTarget->getX(), pTarget->getY(), pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strMsg);
					}

					pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), CvUnit::MOVEFLAG_APPROX_TARGET_RING1);

					//see if we can harrass the enemy now
					TacticalAIHelpers::PerformOpportunityAttack(pUnit, true);
					if (pUnit->shouldPillage(pUnit->plot(), false, true))
						pUnit->PushMission(CvTypes::getMISSION_PILLAGE());

					UnitProcessed(pUnit->GetID());

					//one is enough?
					break;
				}
			}
		}
	}
}

/// Set up fighters to intercept enemy air units
void CvTacticalAI::ExecuteAirPatrolMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			if(pUnit->canAirPatrol(NULL))
			{
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Starting air patrol at, X: %d, Y: %d with %s %d", pUnit->getX(), pUnit->getY(), pUnit->getName().c_str(), pUnit->GetID());
					LogTacticalMessage(strLogString);
				}

				pUnit->PushMission(CvTypes::getMISSION_AIRPATROL());
				UnitProcessed(m_CurrentMoveUnits[iI].GetID());
			}
		}
	}
}

/// Set up fighters to air sweep to suppress enemy air units/AA
void CvTacticalAI::ExecuteAirSweepMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			if(pUnit->canAirSweep())
			{
				CvPlot *pTarget = m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit);
				if (pTarget)
				{
					pUnit->PushMission(CvTypes::getMISSION_AIR_SWEEP(), pTarget->getX(), pTarget->getY());
					pUnit->finishMoves();
					UnitProcessed(m_CurrentMoveUnits[iI].GetID());
				}
			}
		}
	}
}

/// Bombard enemy units from plots they can't reach (return true if some attack made)
bool CvTacticalAI::ExecuteDestroyEnemyUnits(CvTacticalTarget& kTarget, eAggressionLevel aggLvl)
{
	//mark the target no matter if the attack succeeds
	kTarget.SetLastAggLevel(aggLvl);

	CvPlot* pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());
	//target may be invisible because we remember it from the previous turn ...
	CvUnit* pDefender = pTargetPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, true, true);
	if (pDefender && !pDefender->isDelayedDeath())
	{
		// Might be able to hit/kill with a city
		bool bCityCanAttack = FindCitiesWithinStrikingDistance(pTargetPlot);
		if (bCityCanAttack && ExecuteAttackWithCities(pDefender))
			return true;

		// Now the real deal
		if (FindUnitsWithinStrikingDistance(pTargetPlot) && ComputeTotalExpectedDamage(kTarget) > 0)
			return ExecuteAttackWithUnits(pTargetPlot, aggLvl);
	}

	return false;
}

/// Move units out of current dominance zone
void CvTacticalAI::ExecuteWithdrawMoves()
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(!pUnit)
			continue;
		
		// Allow withdraw to neighboring tactical zone which seems safe
		CvTacticalDominanceZone* pZone = GetTacticalAnalysisMap()->GetZoneByPlot(pUnit->plot());
		if (!pZone)
			continue;

		//todo: if we withdraw one unit, make sure we withdraw any neighboring units as well .. don't want to leave anyone behind!
		int iBestScore = 0;
		CvPlot* pTargetPlot = NULL;
		for (std::vector<int>::const_iterator it = pZone->GetNeighboringZones().begin(); it != pZone->GetNeighboringZones().end(); ++it)
		{
			CvTacticalDominanceZone* pNextZone = GetTacticalAnalysisMap()->GetZoneByID(*it);
			if (pNextZone && pNextZone->GetZoneCity() && pNextZone->IsWater() == (pUnit->getDomainType() == DOMAIN_SEA))
			{
				CvPlot* pTestPlot = pNextZone->GetZoneCity()->plot();
				if(!pTestPlot)
					continue;

				int iScore = pNextZone->getHospitalityScore();
				int iTurns = pUnit->TurnsToReachTarget(pTestPlot, CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER, 12);
				if (iTurns == INT_MAX)
					continue;
				if (!pUnit->canMoveInto(*pTestPlot))
					continue;

				iScore = (iScore * 100) / (iTurns + 1);
				if (iScore > iBestScore)
				{
					pTargetPlot = pTestPlot;
					iBestScore = iScore;
				}
			}
		}

		if (!pTargetPlot)
		{
			//Just go towards nearest city and try to avoid expensive distance map updates for minor players ...
			CvCity* pNearestCity = m_pPlayer->isMinorCiv() ? m_pPlayer->getCapitalCity() : m_pPlayer->GetClosestCityByPathLength(pUnit->plot());

			//Note this might for naval units since pathlength is cross-domain, might give impossible target!
			//But considering we have another fallback below this should be ok
			if (pNearestCity && pUnit->canMoveInto(*pNearestCity->plot()))
				pTargetPlot = pNearestCity->plot();
		}

		if (pTargetPlot && MoveToEmptySpaceNearTarget(pUnit, pTargetPlot, pUnit->getDomainType(), 12, true))
		{
			TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, false);
			UnitProcessed(m_CurrentMoveUnits[iI].GetID());

			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("%s %d withdrew from (%d,%d) towards (%d,%d)", 
					pUnit->getName().GetCString(),pUnit->GetID(),pUnit->getX(),pUnit->getY(),pTargetPlot->getX(),pTargetPlot->getY());
				LogTacticalMessage(strLogString);
			}
		}
		else
		{
			if (pUnit->shouldPillage(pUnit->plot(), true))
				pUnit->PushMission(CvTypes::getMISSION_PILLAGE());

			//now move all units which didn't find a path to a city
			ExecuteMovesToSafestPlot(pUnit);
		}
	}

}

/// Move naval units on top of embarked units in danger
void CvTacticalAI::ExecuteEscortEmbarkedMoves(std::vector<CvUnit*> vTargets)
{
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pUnit = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());
		if(pUnit)
		{
			CvUnit* pBestTarget = NULL;
			int iHighestDanger = -1;
			int iBestMoveFlag = 0;

			// Loop through all my embarked units that are: alone and within range
			for (size_t i=0; i<vTargets.size(); ++i)
			{
				CvUnit* pTarget = vTargets[i];
				int iMoveFlag = pUnit->CanStackUnitAtPlot(pTarget->plot()) ? CvUnit::MOVEFLAG_IGNORE_DANGER : CvUnit::MOVEFLAG_APPROX_TARGET_RING1;
				
				// Can this unit get to the embarked unit in two moves?
				int iTurns = pUnit->TurnsToReachTarget(pTarget->plot(),iMoveFlag,1);
				if (iTurns <= 1)
				{
					//note: civilian in danger have INT_MAX
					int iDanger = pTarget->GetDanger();
					if (iDanger > iHighestDanger)
					{
						iHighestDanger = iDanger;
						pBestTarget = pTarget;
						iBestMoveFlag = iMoveFlag;
					}
				}
			}

			if (pBestTarget)
			{
				if (ExecuteMoveToPlot(pUnit, pBestTarget->plot(), false, iBestMoveFlag) == INT_MAX)
					continue;

				UnitProcessed(m_CurrentMoveUnits[iI].GetID());

				//If we can shoot while doing this, do it!
				if (TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit))
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("%s escort opportunity range attack, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						LogTacticalMessage(strLogString);
					}
				}

				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("%s escorted embarked unit at, Current X: %d, Current Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
					LogTacticalMessage(strLogString);
				}
			}
		}
	}
}

// Get best plot of the array of possible plots, based on plot danger.
CvPlot* CvTacticalAI::GetBestRepositionPlot(CvUnit* pUnit, CvPlot* plotTarget, int iAcceptableDanger)
{
	//safety: barbarians don't leave camp
	if (pUnit->isBarbarian() && pUnit->plot()->getImprovementType() == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
		return NULL;

	//don't pull units out of cities for repositioning
	if (pUnit->IsGarrisoned() && pUnit->getDomainType() != DOMAIN_SEA && pUnit->plot()->getPlotCity()->NeedsGarrison())
		return NULL;

	ReachablePlots reachablePlots = pUnit->GetAllPlotsInReachThisTurn(true, true, false);
	if (reachablePlots.empty())
		return NULL;

	CvCity* pTargetCity = plotTarget->getPlotCity();
	CvUnit* pTargetUnit = NULL;
	if (!pTargetCity)
		pTargetUnit = plotTarget->getBestDefender(NO_PLAYER, m_pPlayer->GetID());

	//done with the preparation, now start for real
	std::vector<SPlotWithTwoScoresL2> vStats;
	int iHighestAttack = 0;
	int iLowestDanger = INT_MAX;
	bool bIsRanged = pUnit->IsCanAttackRanged();

	for (ReachablePlots::iterator moveTile=reachablePlots.begin(); moveTile!=reachablePlots.end(); ++moveTile)
	{
		CvPlot* pMoveTile = GC.getMap().plotByIndexUnchecked(moveTile->iPlotIndex);

		//already occupied?
		if (!pUnit->canMoveInto(*pMoveTile,CvUnit::MOVEFLAG_DESTINATION ))
			continue;

		bool bBetterPass = false;
		if (bIsRanged)
		{
			//don't fly too close to the sun ...
			if ( pUnit->GetRange()>1 && plotDistance(*pMoveTile,*plotTarget)<2 )
				bBetterPass = true;
		}

		int iCurrentDanger = pUnit->GetDanger(pMoveTile);

		int iCurrentAttack = 0; //these methods take into account embarkation so we don't have to check for it
		if (bIsRanged && pUnit->canEverRangeStrikeAt(plotTarget->getX(),plotTarget->getY(),pMoveTile,false))
			iCurrentAttack = pUnit->GetMaxRangedCombatStrength(pTargetUnit, pTargetCity, true, pMoveTile, plotTarget);
		else if (!bIsRanged && (pUnit->GetNumEnemyUnitsAdjacent()>0 || pMoveTile->IsFriendlyUnitAdjacent(pUnit->getTeam(),true)) )
			iCurrentAttack = pUnit->GetMaxAttackStrength(pMoveTile, plotTarget, pTargetUnit);

		if (bBetterPass)
			iCurrentAttack /= 2;

		if (iCurrentDanger<=iAcceptableDanger && iCurrentAttack>0)
		{
			vStats.push_back( SPlotWithTwoScoresL2(pMoveTile,iCurrentAttack,iCurrentDanger) );

			iHighestAttack = max( iHighestAttack, iCurrentAttack );
			iLowestDanger = min( iLowestDanger, iCurrentDanger );
		}
	}

	//we want to find the best combination of attack potential and danger
	float fBestScore = 0;
	CvPlot* pBestRepositionPlot = NULL;
	for (std::vector<SPlotWithTwoScoresL2>::const_iterator it=vStats.begin(); it!=vStats.end(); ++it)
	{
		//be conservative: danger counts twice as much as attack strength
		float fScore = it->score1 / float(iHighestAttack) + 2 * float(iLowestDanger) / it->score2;

		if (fScore > fBestScore)
		{
			pBestRepositionPlot = it->pPlot;
			fBestScore = fScore;
		}
	}

	return pBestRepositionPlot;
}

//AMS: Fills m_CurrentAirSweepUnits with all units able to sweep at target plot.
void CvTacticalAI::FindAirUnitsToAirSweep(CvPlot* pTarget)
{
	// Always use one if available in case we need it for recon
	int interceptionsOnPlot = max(1, pTarget->GetInterceptorCount(m_pPlayer->GetID(), NULL, false, true));

	// Loop through all units available to tactical AI this turn
	m_CurrentAirSweepUnits.clear();
	for (list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end() && interceptionsOnPlot > 0; ++it)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if (pLoopUnit && pLoopUnit->canUseForTacticalAI())
		{
			// Is an air unit.
			if (pLoopUnit->getDomainType() == DOMAIN_AIR && pLoopUnit->canMove())
			{
				// Is able to sweep at target
				if (pLoopUnit->canAirSweepAt(pTarget->getX(), pTarget->getY()))
				{
					int iAttackStrength = pLoopUnit->GetMaxRangedCombatStrength(pTarget->GetBestInterceptor(pLoopUnit->getOwner(),pLoopUnit,false,true),NULL,true,NULL,pTarget);
					// Mod to air sweep strength
					iAttackStrength *= (100 + pLoopUnit->GetAirSweepCombatModifier());
					iAttackStrength /= 100;
					CvTacticalUnit unit(pLoopUnit->GetID());
					unit.SetAttackStrength(iAttackStrength);
					unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
					m_CurrentAirSweepUnits.push_back(unit);

					interceptionsOnPlot--;
				}
			}
		}
	}

	std::stable_sort(m_CurrentAirSweepUnits.begin(), m_CurrentAirSweepUnits.end());
}

CvUnit* CvTacticalAI::FindUnitForThisMove(AITacticalMove eMove, CvPlot* pTarget, int iNumTurnsAway /* = -1 if any distance okay */)
{
	static UnitCombatTypes eReconType = (UnitCombatTypes)GC.getInfoTypeForString("UNITCOMBAT_RECON", true);

	m_CurrentMoveUnits.clear();
	std::vector<OptionWithScore<CvUnit*>> possibleUnits;

	// Loop through all units available to tactical AI this turn
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit && pLoopUnit->getDomainType() != DOMAIN_AIR && pLoopUnit->IsCombatUnit() && !pLoopUnit->TurnProcessed())
		{
			// Mod option: only recon units can claim ruins
			// Leaving this code here because A) this option can be turned off (is by default in Community Patch Only), and
			// B) even though most explorers aren't available to tactical AI, some secondary explorer units with the Reconnaissance promotion, like Conquistadors, can make use of it
			// Economic AI still places a high value on goody huts for AI explorers, so they'll still be prioritized; see EconomicAIHelpers::ScoreExplorePlot()
			if (MOD_BALANCE_RECON_ONLY_ANCIENT_RUINS && eMove == AI_TACTICAL_GOODY && pTarget->isRevealedGoody(m_pPlayer->getTeam()))
			{
				if (pLoopUnit->getUnitCombatType() != eReconType && !pLoopUnit->IsGainsXPFromScouting())
					continue;
			}

			//note that garrisons are not recruited into m_CurrentTurnUnits in the first place
			if(!pLoopUnit->canMove() || !pLoopUnit->IsCanAttack() || !pLoopUnit->canMoveInto(*pTarget,CvUnit::MOVEFLAG_DESTINATION))
				continue;

			// Units in armies are controlled by operational AI
			if(pLoopUnit->getArmyID() != -1)
				continue;

			if (pLoopUnit->IsCoveringFriendlyCivilian())
				continue;

			//performance optimization ... careful because zero is a valid turn value
			if(iNumTurnsAway>1 && plotDistance(*pLoopUnit->plot(),*pTarget)>5*iNumTurnsAway)
				continue;

			int iExtraScore = 0;
			if(eMove == AI_TACTICAL_GARRISON)
			{
				// Do not pull units out of important citadels
				CvPlot* pUnitPlot = pLoopUnit->plot();
				if (TacticalAIHelpers::IsPlayerCitadel(pUnitPlot, m_pPlayer->GetID()) && pUnitPlot->IsBorderLand(m_pPlayer->GetID()) && pLoopUnit->getDomainType() == DOMAIN_LAND)
					continue;

				CvCity* pCity = pTarget->getPlotCity();
			if (!pCity)
				continue;

			bool bCityInDanger = pCity->isInDangerOfFalling() || pCity->getDamage() >= pCity->GetMaxHitPoints() / 2;

			// PROACTIVE: Higher priority for high-threat cities without garrisons
			if (!pCity->HasGarrison())
			{
				int iThreat = pCity->getThreatValue();
				if (iThreat >= 75)
					iExtraScore += 40; // Very high threat - urgent garrison needed
				else if (iThreat >= 50)
					iExtraScore += 25; // High threat - prioritize garrison
				else if (iThreat >= 25)
					iExtraScore += 10; // Moderate threat - slight bonus
			}

				// Strategic geography: use terrain and chokepoint data to adjust garrison type preference.
				// Cities at chokepoints with narrow corridors benefit from high-strength melee (blocking);
				// cities with open terrain or high terrain defense score benefit more from ranged.
				const CvStrategicGeographyMap* pGeoMap = m_pPlayer->GetMilitaryAI()->GetStrategicGeographyMap();
				const StrategicCityAnalysis* pAnalysis = pGeoMap ? pGeoMap->GetCityAnalysis(pCity->GetID()) : NULL;
				bool bChokepointCity = pAnalysis && pAnalysis->bIsChokepointCity;
				bool bNarrowApproach = pAnalysis && pAnalysis->iApproachCorridors <= 2;
				int iTerrainDefense = pAnalysis ? pAnalysis->iTerrainDefenseScore : 0;
				bool bIsCapital = pCity->isCapital();

				// Want to put ranged units in cities to give them a ranged attack
				// Siege units can also attack but are better used for offense
				// Melee units can't attack from inside - only their combat strength helps city defense
				// CRITICAL: Ranged/siege garrisons can attack enemy siege without taking damage,
				// melee garrisons cannot effectively counter siege - strongly prefer ranged!
				switch (pLoopUnit->AI_getUnitAIType())
				{
				case UNITAI_RANGED:
					// Best garrison type - can attack freely without taking damage
					if (pLoopUnit->GetRange() > 1)
					{
						iExtraScore += 80 + pCity->getGarrisonRangedAttackModifier();
						// Terrain bonus: high terrain defense means ranged can exploit cover better
						iExtraScore += iTerrainDefense / 8;
						// Capital bonus: extra incentive to put ranged in capital
						if (bIsCapital)
							iExtraScore += 30;
					}
					else
						iExtraScore += 50; // range 1 ranged still much better than melee
					break;
				case UNITAI_DEFENSE_AIR:
					iExtraScore += 60; // can intercept and attack
					break;
				case UNITAI_CITY_BOMBARD:
					// Siege units can attack from cities - better than melee but wanted for offense
					// Still useful as garrison since they CAN attack enemy siege safely
					iExtraScore += 20;
					break;
				case UNITAI_DEFENSE:
				case UNITAI_ATTACK:
				case UNITAI_FAST_ATTACK:
				case UNITAI_COUNTER:
					// All melee units can't attack from inside cities
					// They provide defense bonus but cannot counter enemy siege without taking damage
					// EXCEPTION: at chokepoint cities with narrow approaches, a high-strength melee
					// garrison is more valuable because it blocks the corridor and can counterattack
					// adjacent melee units that try to take the city.
					if (bChokepointCity && bNarrowApproach && !bIsCapital)
					{
						// Chokepoint city: melee is acceptable — strength matters for blocking
						iExtraScore += pLoopUnit->GetBaseCombatStrength() / 3 - 20;
					}
					else if (bCityInDanger)
						iExtraScore += pLoopUnit->GetBaseCombatStrength() / 5 - 40; // slight bonus for strength, but still prefer ranged
					else
						iExtraScore -= 60; // strongly discourage melee garrison when ranged available
					
					// Capital should never settle for melee when ranged exists — extra penalty
					if (bIsCapital && !bCityInDanger)
						iExtraScore -= 20;
					break;
				default:
					//nothing
					break;
				}

				// Don't use recon units as garrisons
				if (pLoopUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE)
					iExtraScore -= 50;

				// Score candidate by effective city-strength contribution relative to city strength without garrison.
				int iCityStrengthNoGarrison = pCity->getStrengthValue();

				CvUnit* pCurrentGarrison = pCity->GetGarrisonedUnit();
				if (pCurrentGarrison)
					iCityStrengthNoGarrison -= (max(pCurrentGarrison->GetBaseCombatStrength(), pCurrentGarrison->GetBaseRangedCombatStrength()) * 10000) /
						max(1, pCurrentGarrison->getDomainType() == DOMAIN_LAND ? GD_INT_GET(CITY_STRENGTH_LAND_UNIT_DIVISOR) : GD_INT_GET(CITY_STRENGTH_NAVAL_UNIT_DIVISOR));

				iCityStrengthNoGarrison = max(1, iCityStrengthNoGarrison);

				const int iCandidateRawStrength = max(pLoopUnit->GetBaseCombatStrength(), pLoopUnit->GetBaseRangedCombatStrength());
				const int iCandidateDivisor = (pLoopUnit->getDomainType() == DOMAIN_LAND) ? GD_INT_GET(CITY_STRENGTH_LAND_UNIT_DIVISOR) : GD_INT_GET(CITY_STRENGTH_NAVAL_UNIT_DIVISOR);
				const int iCandidateContributionTimes100 = (iCandidateRawStrength * 10000) / max(1, iCandidateDivisor);

				iExtraScore += (120 * iCandidateContributionTimes100) / iCityStrengthNoGarrison;

				// Naval garrisons cannot attack, so they're much worse
				if (pLoopUnit->getDomainType() == DOMAIN_SEA && MOD_CORE_NO_NAVAL_RANGED_ATTACKS_FROM_CANALS && !pLoopUnit->isNativeDomain(pTarget))
					iExtraScore -= 50;

				// Don't put units with a defense boosted from promotions in cities, these boosts are ignored
				// Exception: when city is in danger, defense bonus helps prevent capture
				if (!bCityInDanger)
				iExtraScore -= pLoopUnit->getDefenseModifier();
			}
			else if (eMove == AI_TACTICAL_GUARD)
			{
				// Heal first, guards might be attacked
				if (pLoopUnit->shouldHeal(false))
					continue;

				// Don't embark!
				if (pLoopUnit->getDomainType() != pTarget->getDomain())
					continue;

				// No siege units as plot defenders
				if (pLoopUnit->AI_getUnitAIType()==UNITAI_CITY_BOMBARD)
					continue;

				// Ranged units are ok only in citadels
				if (!TacticalAIHelpers::IsPlayerCitadel(pTarget, m_pPlayer->GetID()) && pLoopUnit->getDomainType() == DOMAIN_LAND)
				{
					if (pLoopUnit->IsCanAttackRanged())
						continue;
					if (pLoopUnit->getExtraVisibilityRange() > 0)
						iExtraScore += 23;
				}

				//these can do in a pinch
				if (pLoopUnit->noDefensiveBonus() || !pLoopUnit->canFortify(pTarget))
					iExtraScore -= 21;

				// Units with defensive promotions are especially valuable
				if(pLoopUnit->getDefenseModifier() > 0 || pLoopUnit->getExtraRangedDefenseModifier() > 0)
					iExtraScore += 31;
				
				// WITHDRAWAL PENALTY: Units with withdrawal chance are unreliable guards
				// They may retreat when attacked, exposing whatever they're guarding
				int iWithdrawalChance = pLoopUnit->withdrawalProbability();
				if (iWithdrawalChance > 0)
				{
					// Significant penalty - guards need to hold their ground
					iExtraScore -= iWithdrawalChance / 2; // -25 to -37 for typical withdrawal
				}
			}
			else if(eMove == AI_TACTICAL_GOODY)
			{
				// Fast movers are top priority
				if (pLoopUnit->getUnitInfo().GetUnitAIType(UNITAI_FAST_ATTACK) || pLoopUnit->getUnitInfo().GetUnitAIType(UNITAI_SKIRMISHER))
					iExtraScore += 31;
			}

			//otherwise collect and sort
			int iTurns = pLoopUnit->TurnsToReachTarget(pTarget, CvUnit::MOVEFLAG_SAFE_EMBARK_ONLY, (iNumTurnsAway == -1 ? MAX_INT : iNumTurnsAway));
			if(iTurns != MAX_INT)
			{
				//tricky to make a good score avoiding ties ...
				int iScore = 1000 + iExtraScore - 20 * iTurns - plotDistance(*pTarget,*pLoopUnit->plot());
				possibleUnits.push_back( OptionWithScore<CvUnit*>(pLoopUnit, iScore));
			}
		}
	}

	if (possibleUnits.empty())
		return NULL;
	else
	{
		std::stable_sort(possibleUnits.begin(), possibleUnits.end());
		CheckDebugTrigger(possibleUnits.front().option->GetID());
		return possibleUnits.front().option;
	}
}

/// Fills m_CurrentMoveUnits with all units within X turns of a target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindUnitsWithinStrikingDistance(CvPlot* pTarget)
{
	m_CurrentMoveUnits.clear();

	bool rtnValue = false;
	bool bIsCityTarget = pTarget->isCity();
	bool bAirUnitsAdded = false;
	CvCity* pTargetCity = bIsCityTarget ? pTarget->getPlotCity() : NULL;
	bool bIslandTarget = false;
	bool bNavalDominatedTarget = false;
	int iLandApproaches = 0;
	int iWaterApproaches = 0;
	bool bPreferNavalCapture = IsNavyLedCoastalAssaultTarget(pTargetCity, bIslandTarget, bNavalDominatedTarget, iLandApproaches, iWaterApproaches);
	bool bLandAssaultCooldownActive = bPreferNavalCapture && IsCoastalAssaultLandCooldownActive(pTargetCity);

	// Detect ready dedicated bombers so we only draft fighters for strikes when needed
	bool bBomberAvailable = false;
	for (list<int>::iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end() && !bBomberAvailable; ++it)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if (!pLoopUnit)
			continue;
		if (pLoopUnit->getDomainType() != DOMAIN_AIR)
			continue;
		if (pLoopUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_DEFENSE_AIR)
			continue; // fighters counted separately
		if (!pLoopUnit->canUseForTacticalAI() || !pLoopUnit->canMove())
			continue;
		if (pLoopUnit->IsCanAttackRanged() && pLoopUnit->canRangeStrikeAt(pTarget->getX(), pTarget->getY()))
			bBomberAvailable = true;
	}

	const int iInterceptorsOnTarget = pTarget->GetInterceptorCount(m_pPlayer->GetID(), NULL, false, true);
	CvUnit* pDefender = pTarget->getBestDefender(NO_PLAYER, m_pPlayer->GetID(), NULL, false, true);

	//todo: check if defender can be damaged at all or if an attacker would die?
	// Loop through all units available to tactical AI this turn
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(!pLoopUnit || !pLoopUnit->canUseForTacticalAI())
			continue;

		// Don't pull barbarian units out of camps to attack.
		if(pLoopUnit->isBarbarian() && (pLoopUnit->plot()->getImprovementType() == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT)))
			continue;

		// Some units can't enter cities
		if (pLoopUnit->isNoCapture() && bIsCityTarget)
			continue;

		if (bIsCityTarget && pTargetCity && ShouldSkipLandUnitForCoastalCapture(pLoopUnit, pTargetCity, bPreferNavalCapture, bLandAssaultCooldownActive))
		{
			if (GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Skipping %s for coastal assault on %s: %s%s%s",
					pLoopUnit->getName().GetCString(), pTargetCity->getNameNoSpace().c_str(),
					bLandAssaultCooldownActive ? "coastal land assault cooldown active" : "unsafe cross-water land participation",
					bIslandTarget ? " [ISLAND CITY]" : "",
					bNavalDominatedTarget ? " [NAVAL-DOMINATED]" : "");
				LogTacticalMessage(strLogString);
			}
			continue;
		}

		// Don't bother with pathfinding if we're very far away
		if (plotDistance(*pLoopUnit->plot(), *pTarget) > pLoopUnit->baseMoves(false) * 4 && !pLoopUnit->getUnitInfo().IsCanChangePort())
			continue;

		// Allow fighters to strike only when it is safe or when no bombers are available
		if (pLoopUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_DEFENSE_AIR)
		{
			// Keep fighters healthy so they can still intercept if needed
			int iHPct = (pLoopUnit->GetCurrHitPoints() * 100) / pLoopUnit->GetMaxHitPoints();
			bool bLowHealth = (iHPct < 65);
			bool bHighInterceptRisk = (iInterceptorsOnTarget > 0 && iHPct < 90);

			bool bAllowFighterAttack = !bBomberAvailable; // primary condition: no bombers ready
			if (!bAllowFighterAttack && !bHighInterceptRisk && !bLowHealth)
			{
				// Secondary: allow if healthy and intercept risk is low for this target
				bAllowFighterAttack = true;
			}

			if (!bAllowFighterAttack)
				continue;
		}

		if (pLoopUnit->IsCoveringFriendlyCivilian())
			continue;

		//note that garrisoned units are *not* recruited into m_CurrentTurnUnits!
		//also note that units in citadels are recruited, but the combat sim knows about their importance

		bool bCanReach = false;
		if ( pLoopUnit->IsCanAttackRanged() )
		{
			//can attack without moving ... for aircraft and other long-range units
			if (pLoopUnit->canRangeStrikeAt(pTarget->getX(), pTarget->getY()))
				bCanReach = true;
			else if (pLoopUnit->canMove() && pLoopUnit->getDomainType()!=DOMAIN_AIR)
			{
				//note that we also take units which can reach an attack plot but can only attack next turn. that's ok.
				ReachablePlots reachablePlots = TacticalAIHelpers::GetAllPlotsInReachThisTurn(pLoopUnit, pLoopUnit->plot(),
					CvUnit::MOVEFLAG_IGNORE_STACKING_SELF | CvUnit::MOVEFLAG_NO_EMBARK, 0);

				//start from the outside
				for (int i=pLoopUnit->GetRange(); i>0 && !bCanReach; i--)
				{
					std::vector<CvPlot*> vPlots = TacticalAIHelpers::GetPlotsForRangedAttack(pTarget,pLoopUnit,i,false);

					for (std::vector<CvPlot*>::const_iterator it=vPlots.begin(); it!=vPlots.end() && !bCanReach; ++it)
						bCanReach = (reachablePlots.find( (*it)->GetPlotIndex() ) != reachablePlots.end());
				}
			}
		}
		else //melee. enough if we can get adjacent to the target
		{
			int iFlags = CvUnit::MOVEFLAG_APPROX_TARGET_RING1 | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF;
			bCanReach = (pLoopUnit->TurnsToReachTarget(pTarget, iFlags, 0) <= 0);
		}

		//include units which are very close even if they cannot do anything right now
		//but the combat sim should have control over them to move them out of the way
		if (!bCanReach && plotDistance(*pLoopUnit->plot(),*pTarget)>2)
			continue;

		if(pLoopUnit->IsCanAttackRanged())
		{
			// Will we do a significant amount of damage
			int iTargetHitpoints = pDefender ? pDefender->GetCurrHitPoints() : 0;
			if(IsExpectedToDamageWithRangedAttack(pLoopUnit, pTarget, MIN(iTargetHitpoints/20, 3)))
			{
				//first-line ranged and air - ranged units should attack BEFORE melee to soften targets
				CvTacticalUnit unit(pLoopUnit->GetID());
				int iAttackStrength = 0;
				if (bIsCityTarget)
					iAttackStrength = pLoopUnit->GetMaxRangedCombatStrength(NULL, pTarget->getPlotCity(), true, NULL, NULL, true, true);
				else
					iAttackStrength = pLoopUnit->GetMaxRangedCombatStrength(pDefender, NULL, true, NULL, NULL, true, true);

				// Ranged-before-melee coordination: boost ranged priority when attacking cities
				// This ensures ranged units soften the city before melee units commit to the assault
				// Melee units take damage from city counterattacks and garrison, so ranged should weaken first
				if (bIsCityTarget)
				{
					// Give ranged units a significant priority boost vs cities
					// This effectively doubles their priority in the sort order
					iAttackStrength = iAttackStrength * 3 / 2;
					
					// Combined arms bombardment coordination: naval and air units get extra priority
					// when bombarding cities that have land/naval siege ongoing
					// This ensures coordinated fire support from sea and air
					if (pLoopUnit->getDomainType() == DOMAIN_SEA || pLoopUnit->getDomainType() == DOMAIN_AIR)
					{
						CvCity* pCity = pTarget->getPlotCity();
						if (pCity)
						{
							// Check for combined arms siege - count adjacent friendly forces by domain
							bool bSiegeOngoing = false;
							int iLandAttackers = 0;
							int iNavalAttackers = 0;
							
							for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
							{
								CvPlot* pAdj = plotDirection(pCity->getX(), pCity->getY(), (DirectionTypes)iDir);
								if (pAdj)
								{
									// Count our units adjacent to the city
									CvUnit* pAdjUnit = pAdj->getBestDefender(m_pPlayer->GetID());
									if (pAdjUnit)
									{
										if (pAdjUnit->getDomainType() == DOMAIN_LAND)
										{
											bSiegeOngoing = true;
											iLandAttackers++;
										}
										else if (pAdjUnit->getDomainType() == DOMAIN_SEA)
										{
											bSiegeOngoing = true;
											iNavalAttackers++;
										}
									}
								}
							}
							
							if (bSiegeOngoing)
							{
								// Combined arms bombardment coordination bonus
								// More attackers = more important for fire support
								int iBombardBonus = 15;
								int iTotalAttackers = iLandAttackers + iNavalAttackers;
								
								if (iTotalAttackers >= 4)
									iBombardBonus += 20;
								else if (iTotalAttackers >= 3)
									iBombardBonus += 15;
								else if (iTotalAttackers >= 2)
									iBombardBonus += 8;
								
								// Multi-domain assault bonus: combined land+naval is more effective
								if (iLandAttackers > 0 && iNavalAttackers > 0)
									iBombardBonus += 10;
								
								// Extra bonus if city is damaged (siege already in progress)
								if (pCity->getDamage() > 0)
									iBombardBonus += 10;
								
								// Bonus for cities under active assault (low HP)
								int iCityHPPercent = ((pCity->GetMaxHitPoints() - pCity->getDamage()) * 100) / pCity->GetMaxHitPoints();
								if (iCityHPPercent <= 50)
									iBombardBonus += 15;
								else if (iCityHPPercent <= 75)
									iBombardBonus += 5;
								
								// Air units get slight extra bonus - they can hit any city, very flexible
								if (pLoopUnit->getDomainType() == DOMAIN_AIR)
									iBombardBonus += 5;
								
								iAttackStrength += iAttackStrength * iBombardBonus / 100;
							}
						}
					}
				}
				
				unit.SetAttackStrength(iAttackStrength);
				unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
				m_CurrentMoveUnits.push_back(unit);
				rtnValue = true;

				if (pLoopUnit->getDomainType()==DOMAIN_AIR)
					bAirUnitsAdded = true;
			}
		}
		else //melee
		{
			int iAttackStrength = pLoopUnit->GetMaxAttackStrength(NULL, pTarget, bIsCityTarget ? NULL : pDefender, true, true);

			// Domain prioritization for city capture
			// Different domains have different advantages when capturing cities
			if (bIsCityTarget && pTarget->getPlotCity())
			{
				CvCity* pCity = pTarget->getPlotCity();
				bool bCoastalCity = pCity->isCoastal();
				int iCityHPPercent = ((pCity->GetMaxHitPoints() - pCity->getDamage()) * 100) / pCity->GetMaxHitPoints();
				
				bool bIslandCity = false;
				int iLandApproaches = 0;
				int iWaterApproaches = 0;
				GetCoastalApproachCounts(pCity, iLandApproaches, iWaterApproaches);
				bIslandCity = (iLandApproaches == 0);
				
				if (pLoopUnit->getDomainType() == DOMAIN_SEA)
				{
					// Naval melee capture bonuses
					if (bIslandCity)
					{
						// Island city REQUIRES naval capture - major priority boost
						iAttackStrength = iAttackStrength * 3 / 2;
					}
					else if (bCoastalCity && iWaterApproaches > iLandApproaches)
					{
						// Naval-dominated coastal city - naval has advantage
						iAttackStrength = iAttackStrength * 5 / 4;
					}
					
					// When city is low HP, naval melee should be prioritized for capture
					// because garrison bonus often doesn't apply to naval attackers
					if (iCityHPPercent <= 25)
					{
						iAttackStrength = iAttackStrength * 5 / 4; // Extra boost for capture attempt
					}
				}
				else if (pLoopUnit->getDomainType() == DOMAIN_LAND)
				{
					// Check if unit has amphibious promotion (no penalty for water->land attacks)
					bool bIsAmphibious = pLoopUnit->isAmphibious();
					
					// Check if unit is currently on water (would need amphibious attack)
					bool bOnWater = pLoopUnit->plot()->isWater();
					
					// Land melee capture considerations
					if (bIslandCity)
					{
						// Island city has no land approaches
						if (bIsAmphibious)
						{
							// Amphibious unit can attack from water without penalty!
							// Good alternative to naval melee
							iAttackStrength = iAttackStrength * 5 / 4;
							
							// At critical HP, amphibious land unit is excellent for capture
							if (iCityHPPercent <= 25)
								iAttackStrength = iAttackStrength * 5 / 4;
						}
						else if (iCityHPPercent <= 15)
						{
							// Non-amphibious but city is nearly dead
							// Even with -50% penalty, might be worth capturing
							// Don't heavily penalize - combat sim will handle actual damage
							iAttackStrength = iAttackStrength * 3 / 4;
						}
						else
						{
							// Non-amphibious attacking island city at decent HP - bad idea
							iAttackStrength = iAttackStrength / 4;
						}
					}
					else if (bCoastalCity && iWaterApproaches > iLandApproaches * 2)
					{
						// Very naval-heavy coastal city
						if (bIsAmphibious && bOnWater)
						{
							// Amphibious unit on water can attack without penalty
							// This is actually good - land unit without normal land approach
							iAttackStrength = iAttackStrength * 11 / 10;
						}
						else if (!bOnWater)
						{
							// Land unit on land attacking - limited approaches
							iAttackStrength = iAttackStrength * 9 / 10;
						}
						else
						{
							// Non-amphibious on water - penalty applies
							// But if city HP very low, still consider it
							if (iCityHPPercent <= 20)
								iAttackStrength = iAttackStrength * 4 / 5; // Moderate penalty
							else
								iAttackStrength = iAttackStrength / 2; // Heavy penalty
						}
					}
					else
					{
						// Land-accessible city - land melee is standard choice
						if (bOnWater && !bIsAmphibious)
						{
							// Land unit on water attacking land-accessible city
							// Prefer attacking from land unless city is nearly dead
							if (iCityHPPercent <= 20)
								iAttackStrength = iAttackStrength * 3 / 4; // Worth the risk
							else
								iAttackStrength = iAttackStrength / 2; // Use land approaches instead
						}
						else
						{
							// Normal land attack or amphibious from water
							// Small boost when city has a garrison (land can tank garrison damage)
							if (pCity->HasGarrison())
								iAttackStrength = iAttackStrength * 11 / 10;
						}
					}
				}
			}

			CvTacticalUnit unit(pLoopUnit->GetID());
			unit.SetAttackStrength(iAttackStrength);
			unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
			m_CurrentMoveUnits.push_back(unit);
			rtnValue = true;
		}
	}

	// As we have air units on the attack targets we should also check possible air sweeps
	if (bAirUnitsAdded)
		FindAirUnitsToAirSweep(pTarget);
	else
		m_CurrentAirSweepUnits.clear();

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return rtnValue;
}

/// Fills m_CurrentMoveCities with all cities within bombard range of a target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindCitiesWithinStrikingDistance(CvPlot* pTargetPlot)
{
	m_CurrentMoveCities.clear();

	// Loop through all of our cities
	int iLoop;
	for(CvCity* pLoopCity = m_pPlayer->firstCity(&iLoop); pLoopCity != NULL; pLoopCity = m_pPlayer->nextCity(&iLoop))
	{
		int iAttackStrength = 0;
		if (pLoopCity->canRangeStrikeAt(pTargetPlot->getX(), pTargetPlot->getY()) && !pLoopCity->isMadeAttack())
			iAttackStrength += pLoopCity->getStrengthValue(true);

		if (iAttackStrength>0)
		{
			CvTacticalCity city;
			city.SetID(pLoopCity->GetID());
			city.SetExpectedTargetDamage(iAttackStrength);
			m_CurrentMoveCities.push_back(city);
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveCities.begin(), m_CurrentMoveCities.end());
	return !m_CurrentMoveCities.empty();
}


bool CvTacticalAI::FindEmbarkedUnitsAroundTarget(CvPlot* pTarget, int iMaxDistance)
{
	m_CurrentMoveUnits.clear();

	if (!pTarget)
		return false;

	// Loop through all units available to tactical AI this turn
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit && pLoopUnit->canUseForTacticalAI() && pLoopUnit->IsCombatUnit() && pLoopUnit->isEmbarked() && plotDistance(*pLoopUnit->plot(),*pTarget)<=iMaxDistance )
		{
			CvTacticalUnit unit(pLoopUnit->GetID());
			unit.SetAttackStrength(pLoopUnit->GetBaseCombatStrength());
			unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
			m_CurrentMoveUnits.push_back(unit);
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return m_CurrentMoveUnits.size()>0;
}


/// Fills m_CurrentMoveUnits with all paratrooper units (available to jump) to the target (returns TRUE if 1 or more found)
bool CvTacticalAI::FindParatroopersWithinStrikingDistance(CvPlot* pTarget, bool bCheckDanger)
{
	m_CurrentMoveUnits.clear();

	// Loop through all units available to tactical AI this turn
	for(list<int>::const_iterator it = m_CurrentTurnUnits.begin(); it != m_CurrentTurnUnits.end(); it++)
	{
		CvUnit* pLoopUnit = m_pPlayer->getUnit(*it);
		if(pLoopUnit && pLoopUnit->canUseForTacticalAI() && 
			pLoopUnit->canParadropAt(pLoopUnit->plot(), pTarget->getX(), pTarget->getY()) &&
			(!bCheckDanger || pLoopUnit->GetDanger(pTarget) < pLoopUnit->GetCurrHitPoints()))
		{
			CvTacticalUnit unit(pLoopUnit->GetID());
			unit.SetAttackStrength(pLoopUnit->GetBaseCombatStrength());
			unit.SetHealthPercent(pLoopUnit->GetCurrHitPoints(), pLoopUnit->GetMaxHitPoints());
			m_CurrentMoveUnits.push_back(unit);
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return m_CurrentMoveUnits.size()>0;
}

//find units for pillaging, plundering, blockading, etc
bool CvTacticalAI::FindUnitsForHarassing(CvPlot* pTarget, int iNumTurnsAway, int iMinHitpoints, int iMaxHitpoints, DomainTypes eDomain, bool bMustHaveMovesLeft, bool bAllowEmbarkation, int iMaxNumUnits, bool bPlunderTradeRoute)
{
	m_CurrentMoveUnits.clear();
	//need to convert turns to max path length here, zero turns away is also valid!
	SPathFinderUserData data(m_pPlayer->GetID(), PT_ARMY_MIXED, NO_PLAYER, (iNumTurnsAway+1)*(eDomain==DOMAIN_LAND ? 3 : 5));
	ReachablePlots relevantPlots = GC.GetStepFinder().GetPlotsInReach(pTarget, data);

	//plots are ordered by turns to reach!
	for (ReachablePlots::const_iterator it = relevantPlots.begin(); it != relevantPlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		CvUnit* pLoopUnit = pPlot->getBestDefender(m_pPlayer->GetID());
		if (pLoopUnit)
		{
			if (pLoopUnit->isDelayedDeath())
				continue;

			if (!pLoopUnit->canMove() || pLoopUnit->TurnProcessed())
				continue;

			// plundering a trade route on the current plot doesn't cost us anything, so we do it whenever possible and the following exclusions do not apply
			if (!bPlunderTradeRoute || pPlot != pTarget)
			{
				if (!pLoopUnit->canUseForTacticalAI())
					continue;

				//these units are too fragile for the moves we have in mind
				if (pLoopUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD || pLoopUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
					continue;

				if (pLoopUnit->IsCoveringFriendlyCivilian())
					continue;

				if (iMinHitpoints > 0 && pLoopUnit->GetCurrHitPoints() < iMinHitpoints)
					continue;

				if (iMaxHitpoints > 0 && pLoopUnit->GetCurrHitPoints() > iMaxHitpoints)
					continue;

				if (pLoopUnit->GetDanger(pTarget) > pLoopUnit->GetCurrHitPoints())
					continue;

				//don't use garrisons if there is an enemy around. the garrison may still attack when we do garrison moves!
				if (pLoopUnit->IsGarrisoned() && pLoopUnit->GetGarrisonedCity()->NeedsGarrison() && pLoopUnit->getDomainType() != DOMAIN_SEA)
					continue;
			}

			if (pLoopUnit->isBarbarian() && pLoopUnit->plot()->getImprovementType() == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
				continue;

			if (eDomain != NO_DOMAIN && pLoopUnit->getDomainType() != eDomain)
				continue;

			//this should be a low-risk thing so don't get our units killed
			int iFlags = CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER;
			if (bAllowEmbarkation)
				iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;
			if (pTarget->isEnemyUnit(m_pPlayer->GetID(), true, true) && !pLoopUnit->IsCanAttackWithMove())
				iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1 | CvUnit::MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN;
			if (bMustHaveMovesLeft)
				iFlags |= CvUnit::MOVEFLAG_TURN_END_IS_NEXT_TURN;

			int iTurnsCalculated = pLoopUnit->TurnsToReachTarget(pTarget, iFlags, iNumTurnsAway);
			if (iTurnsCalculated <= iNumTurnsAway)
			{
				CvTacticalUnit unit(pLoopUnit->GetID());
				int iPlunderBonus = 0;
				if (bPlunderTradeRoute)
				{
					if (pLoopUnit->isHighSeaRaiderUnit())
					{
						iPlunderBonus += 500;
					}
					for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
					{
						iPlunderBonus += pLoopUnit->getYieldFromTRPlunder((YieldTypes)iI);
					}
				}
				
				// === PILLAGING UNIT SUITABILITY BONUS ===
				// Some units are especially good at pillaging due to their mobility and promotions
				int iPillageSuitability = 0;
				
				// 1. RECON UNITS: Scouts/Explorers are excellent pillagers
				// They can get deep behind enemy lines and have pillage-focused promotion tree
				bool bIsRecon = (pLoopUnit->AI_getUnitAIType() == UNITAI_EXPLORE || 
								 pLoopUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE);
				if (bIsRecon)
				{
					iPillageSuitability += 30; // Base recon bonus
					
					// Recon units with combat capability are even better pillagers
					if (pLoopUnit->GetBaseCombatStrength() > 0)
						iPillageSuitability += 20;
				}
				
				// 2. ZOC-IGNORING UNITS: Can slip past enemy lines
				// This is a key promotion for pillaging behind enemy fortifications
				if (pLoopUnit->IsIgnoreZOC())
				{
					iPillageSuitability += 50; // Major bonus - can bypass defenders
				}
				
				// 3. FAST UNITS: Can reach targets quickly and escape
				int iBaseMoves = pLoopUnit->baseMoves(false);
				if (iBaseMoves >= 4)
				{
					iPillageSuitability += (iBaseMoves - 3) * 10; // +10 for 4 moves, +20 for 5, etc.
				}
				
				// 4. MOVE-AFTER-ATTACK: Can pillage and retreat
				if (pLoopUnit->canMoveAfterAttacking())
				{
					iPillageSuitability += 25; // Can escape after pillaging
				}
				
				// 5. LOW-VALUE UNITS: Expendable units are better for risky pillaging
				// Don't send your expensive tanks to pillage - use scouts instead
				if (pLoopUnit->GetBaseCombatStrength() <= 20 && pLoopUnit->GetBaseCombatStrength() > 0)
				{
					iPillageSuitability += 15; // Expendable combat unit
				}
				
				// 6. CAVALRY/FAST ATTACK: Good at hit-and-run
				if (pLoopUnit->AI_getUnitAIType() == UNITAI_FAST_ATTACK)
				{
					iPillageSuitability += 20;
				}
				
				unit.SetAttackStrength(1 + iNumTurnsAway - iTurnsCalculated + iPlunderBonus + iPillageSuitability);
				unit.SetHealthPercent(1, 1);
				m_CurrentMoveUnits.push_back(unit);
				//pathfinding is expensive, don't return more than needed
				if (m_CurrentMoveUnits.size() >= (size_t)iMaxNumUnits)
					break;
			}
		}
	}

	// Now sort them in the order we'd like them to attack
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end());

	return m_CurrentMoveUnits.size()>0;
}

// search radius for units depending on map size and game turn
int CvTacticalAI::GetRecruitRange() const
{
	int iResult = m_iRecruitRange;
	//add some for duration
	iResult += (4*GC.getGame().getGameTurn()) / max(400, GC.getGame().getMaxTurns());
	//add some for map size
	if (GC.getMap().getWorldSize() == WORLDSIZE_LARGE)
		iResult += 1;
	if (GC.getMap().getWorldSize() == WORLDSIZE_HUGE)
		iResult += 2;

	return iResult;
}

/// Estimates the damage we can apply to a target
int CvTacticalAI::ComputeTotalExpectedDamage(const CvTacticalTarget& kTarget)
{
	CvPlot* pTargetPlot = GC.getMap().plot(kTarget.GetTargetX(), kTarget.GetTargetY());

	int rtnValue = 0;
	int iMeleeCount = 0;
	int iTotalGarrisonDamage = 0;

	CvUnit* pCurrentGarrison = kTarget.GetTargetType() == AI_TACTICAL_TARGET_ENEMY_CITY ? pTargetPlot->getPlotCity()->GetGarrisonedUnit() : NULL;
	int iCurrentGarrisonHealth = pCurrentGarrison ? pCurrentGarrison->GetCurrHitPoints() : 0;
	PlayerTypes eOwner = pTargetPlot->getOwner();

	// Loop through all units who can reach the target
	for(unsigned int iI = 0; iI < m_CurrentMoveUnits.size(); iI++)
	{
		CvUnit* pAttacker = m_pPlayer->getUnit(m_CurrentMoveUnits[iI].GetID());

		// Is target a unit?
		switch(kTarget.GetTargetType())
		{
		case AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT:
		{
			CvUnit* pDefender = pTargetPlot->getVisibleEnemyDefender(m_pPlayer->GetID());
			if (pDefender)
			{
				int iSelfDamage = 0;
				//attacker plot will likely change but this is just an estimation anyway
				int iDamage = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pDefender, pAttacker, pTargetPlot, pAttacker->plot(), iSelfDamage, true, 0, 0, true);
				if (iDamage > iSelfDamage/5 && pAttacker->GetCurrHitPoints() > iSelfDamage/2) //exclude only the most extreme suicides, we will sort out the details during combat sim
				{
					m_CurrentMoveUnits[iI].SetExpectedTargetDamage(iDamage);
					m_CurrentMoveUnits[iI].SetExpectedSelfDamage(iSelfDamage);

					//if we have a lot of melee units, don't assume they all can be executed at once
					if (!pAttacker->IsCanAttackRanged() && !pAttacker->canMoveAfterAttacking())
					{
						if (iMeleeCount<3)
							rtnValue += iDamage;
						iMeleeCount++;
					}
					else
						rtnValue += iDamage;
				}
			}
		}
		break;

		case AI_TACTICAL_TARGET_ENEMY_CITY:
		{
			CvCity* pCity = pTargetPlot->getPlotCity();
			if(pCity != NULL)
			{
				int iSelfDamage = 0;
				int iGarrisonDamage = 0;
				bool bNeedsRecalculation = pCurrentGarrison != pCity->GetGarrisonedUnit();
				//attacker plot will likely change but this is just an estimation anyway
				int iDamage = TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pCity, pAttacker, pAttacker->plot(), iSelfDamage, iGarrisonDamage, true, 0, rtnValue, iTotalGarrisonDamage, false, true, pCurrentGarrison);
				iTotalGarrisonDamage += iGarrisonDamage;

				if (pCurrentGarrison)
				{
					iCurrentGarrisonHealth -= iGarrisonDamage;
					if (iCurrentGarrisonHealth <= 0)
					{
						pCurrentGarrison = pTargetPlot->getBestDefender(eOwner, m_pPlayer->GetID(), NULL, true, true, false, false, pCurrentGarrison);
						iCurrentGarrisonHealth = pCurrentGarrison ? pCurrentGarrison->GetCurrHitPoints() : 0;
						iTotalGarrisonDamage = 0;
					}
				}
				if (iDamage + iGarrisonDamage > iSelfDamage || ((iDamage + iGarrisonDamage) * 2 > iSelfDamage && pAttacker->GetCurrHitPoints() - iSelfDamage > pAttacker->GetMaxHitPoints() / 2)) //exclude suicidal melee attacks
				{
					// If we did the previous calculation with the wrong garrisoned unit, we need to recompute the value assuming a garrison is present to ensure fair sorting
					// TODO we should probably sort m_CurrentMoveUnits so the ranged units and strongest units are processed first, weak units can then be used to finish off the city or attack when the garrison is dead
					int iNormalizedDamage = bNeedsRecalculation ? TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pCity, pAttacker, pAttacker->plot(), iSelfDamage, iGarrisonDamage, true) : iDamage;

					m_CurrentMoveUnits[iI].SetExpectedTargetDamage(iNormalizedDamage);
					m_CurrentMoveUnits[iI].SetExpectedSelfDamage(iSelfDamage);
					rtnValue += iDamage;
				}
			}
		}
		break;
		default:
		UNREACHABLE(); // Other target types cannot be damaged.
		}
	}

	//sort by expected damage to target!
	std::stable_sort(m_CurrentMoveUnits.begin(), m_CurrentMoveUnits.end(), TacticalAIHelpers::SortByExpectedTargetDamageDescending);

	return rtnValue;
}

/// Estimates the bombard damage we can apply to a target
int CvTacticalAI::ComputeTotalExpectedCityBombardDamage(CvUnit* pTarget)
{
	int iExpectedDamage = 0;

	// Now loop through all the cities that can bombard it
	for(unsigned int iI = 0; iI < m_CurrentMoveCities.size(); iI++)
	{
		CvCity* pAttackingCity = m_pPlayer->getCity(m_CurrentMoveCities[iI].GetID());
		
		iExpectedDamage += pAttackingCity->rangeCombatDamage(pTarget);

		if (pAttackingCity->HasGarrison())
		{
			CvUnit* pGarrison = pAttackingCity->GetGarrisonedUnit();
			if (pGarrison->canRangeStrikeAt(pTarget->getX(), pTarget->getY()))
			{
				int iUnusedReferenceVariable = 0;
				int iUnitDamage = pGarrison->GetRangeCombatDamage(pTarget, NULL, 0, iUnusedReferenceVariable, false, 0, 0, NULL, NULL, true, true);
				//assume same damage for multiple attacks ...
				iExpectedDamage += iUnitDamage * pGarrison->getNumAttacks();
			}
		}

	}
	
	return iExpectedDamage;
}

bool CvTacticalAI::IsExpectedToDamageWithRangedAttack(CvUnit* pAttacker, CvPlot* pTargetPlot, int iMinDamage)
{
	int iExpectedDamage = 0;

	int iGarrisonDamage = 0;
	if(pTargetPlot->isCity())
	{
		CvCity* pCity = pTargetPlot->getPlotCity();
		int iGarrisonMaxHP = 0;
		if (pCity->HasGarrison() && !pCity->GetGarrisonedUnit()->isDelayedDeath())
		{
			iGarrisonMaxHP = pCity->GetGarrisonedUnit()->GetMaxHitPoints();
		}
		iExpectedDamage = pAttacker->GetRangeCombatDamage(NULL, pCity, iGarrisonMaxHP, iGarrisonDamage, /*bIncludeRand*/ false, 0, 0, NULL, NULL, true, true);
	}
	else
	{
		CvUnit* pDefender = pTargetPlot->getBestDefender(NO_PLAYER, m_pPlayer->GetID());
		if(pDefender)
		{
			iExpectedDamage = pAttacker->GetRangeCombatDamage(pDefender, NULL, 0, iGarrisonDamage, false, 0, 0, NULL, NULL, true, true);
		}
	}

	return iExpectedDamage >= iMinDamage;
}

/// Move up close to our target avoiding our own units if possible
bool CvTacticalAI::MoveToEmptySpaceNearTarget(CvUnit* pUnit, CvPlot* pTarget, DomainTypes eDomain, int iMaxTurns, bool bMustBeSafePath)
{
	if (!pUnit || !pTarget || pUnit->atPlot(*pTarget))
		return false;

	int iFlags = 0;
	//can we move there directly? if not try to move to an adjacent plot
	if (!pUnit->canMoveInto(*pTarget, CvUnit::MOVEFLAG_DESTINATION))
		iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;
	if (eDomain==pTarget->getDomain())
		iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_NATIVE_DOMAIN;
	if (bMustBeSafePath)
		iFlags |= CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER;

	int iTurns = pUnit->TurnsToReachTarget(pTarget,iFlags,iMaxTurns);

	//if not possible, try again with more leeway
	if (iTurns==INT_MAX)
	{
		if (iFlags & CvUnit::MOVEFLAG_APPROX_TARGET_RING1)
		{
			iFlags &= ~CvUnit::MOVEFLAG_APPROX_TARGET_RING1;
			iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING2;
		}
		else
			iFlags |= CvUnit::MOVEFLAG_APPROX_TARGET_RING1;

		iTurns = pUnit->TurnsToReachTarget(pTarget,iFlags,iMaxTurns);
	}

	if (iTurns <= iMaxTurns)
	{
		//for inspection in GUI
		pUnit->SetMissionAI(MISSIONAI_TACTMOVE,pTarget,NULL);

		//may not actually move if there is no safe path
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pTarget->getX(), pTarget->getY(), iFlags);

		//don't call finish moves, otherwise we won't heal!
		return true;
	}

	return false;
}

/// Find a multi-turn target for a land barbarian to wander towards
CvPlot* CvTacticalAI::FindBestBarbarianLandTarget(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = NULL;
	int iMaxTurns = m_iLandBarbarianRange;
	
	// combat units look at all offensive targets within x turns
	if (pUnit->IsCanDefend())
	{
		pBestMovePlot = FindNearbyTarget(pUnit, iMaxTurns, true);

		// alternatively explore
		if (pBestMovePlot == NULL)
			pBestMovePlot = FindBarbarianExploreTarget(pUnit);
	}

	// by default go back to camp or so
	if (pBestMovePlot == NULL)
	{
		if (!pUnit->IsCanDefend())
			iMaxTurns = 23;

		pBestMovePlot = FindNearbyTarget(pUnit, iMaxTurns, false);
	}

	return pBestMovePlot;
}

/// Find a multi-turn target for a sea barbarian to wander towards
CvPlot* CvTacticalAI::FindBestBarbarianSeaTarget(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = NULL;
	int iBestValue = MAX_INT;

	SPathFinderUserData data(pUnit, 0, m_iSeaBarbarianRange);
	ReachablePlots movePlots = GC.GetPathFinder().GetPlotsInReach(pUnit->plot(), data);

	// Loop through all unit targets to find the closest
	for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT); pTarget != NULL; pTarget = GetNextZoneTarget())
	{
		CvPlot* pPlot = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());

		ReachablePlots::const_iterator itPlot = movePlots.find(pPlot->GetPlotIndex());
		if (itPlot != movePlots.end() && itPlot->iPathLength < iBestValue)
		{
			iBestValue = itPlot->iPathLength;
			pBestMovePlot = pPlot;
		}
	}

	// No units to pick on, so sail to a tile adjacent to the second closest barbarian camp
	if(pBestMovePlot == NULL)
	{
		CvPlot* pNearestCamp = NULL;
		int iBestCampDistance = MAX_INT;

		// Start by finding the very nearest camp
		for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP); pTarget!=NULL; pTarget = GetNextZoneTarget())
		{
			CvPlot* pCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
			if (pCamp->isAdjacentToShallowWater())
			{
				int iDistance = plotDistance(pUnit->getX(), pUnit->getY(), pTarget->GetTargetX(), pTarget->GetTargetY());
				if (iDistance < iBestCampDistance)
				{
					pNearestCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
					iBestCampDistance = iDistance;
				}
			}
		}

		// Try to sail to the second closest camp - this should result in patrolling behavior
		for (CvTacticalTarget* pTarget = GetFirstZoneTarget(AI_TACTICAL_TARGET_BARBARIAN_CAMP); pTarget!=NULL; pTarget = GetNextZoneTarget())
		{
			CvPlot* pCamp = GC.getMap().plot(pTarget->GetTargetX(), pTarget->GetTargetY());
			if(pCamp != pNearestCamp && pCamp->isAdjacentToShallowWater())
			{
				for (ReachablePlots::const_iterator it = movePlots.begin(); it != movePlots.end(); ++it)
				{
					CvPlot* pTestPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
					if (pTestPlot->isAdjacent(pCamp))
					{
						int iValue = it->iPathLength;
						if (iValue < iBestValue)
						{
							iBestValue = iValue;
							pBestMovePlot = pTestPlot;
						}
					}
				}
			}
		}
	}

	// No obvious target ... next try
	if (pBestMovePlot == NULL)
		pBestMovePlot = FindBarbarianExploreTarget(pUnit);

	return pBestMovePlot;
}

/// Scan nearby tiles for the best choice, borrowing code from the explore AI
CvPlot* CvTacticalAI::FindBarbarianExploreTarget(CvUnit* pUnit)
{
	CvPlot* pBestMovePlot = 0;
	int iBestValue = 0;

	ReachablePlots reachablePlots = pUnit->GetAllPlotsInReachThisTurn(true, true, false);
	for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
	{
		CvPlot* pConsiderPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

		if (pUnit->atPlot(*pConsiderPlot))
			continue;

		//ignore cities
		if (!pConsiderPlot->isRevealed(pUnit->getTeam()) || pConsiderPlot->isCity())
			continue;

		//even barbarians consider danger sometimes
		if (pUnit->GetDanger(pConsiderPlot) > pUnit->GetCurrHitPoints())
			continue;

		int iValue = 0;
		for (int i = 0; i < RING1_PLOTS; i++)
		{
			CvPlot* pNeighbor = iterateRingPlots(pConsiderPlot, i);
			if (!pNeighbor)
				continue;

			if (!pNeighbor->isRevealed(pUnit->getTeam()))
				iValue += 3;
			else if (!pNeighbor->isVisible(pUnit->getTeam()) && pNeighbor->isOwned())
				iValue += 2;
		}

		// disembark if possible
		if (pUnit->isEmbarked() && pUnit->isNativeDomain(pConsiderPlot))
			iValue += 100;

		if (iValue > iBestValue)
		{
			pBestMovePlot = pConsiderPlot;
			iBestValue = iValue;
		}
	}

	return pBestMovePlot;
}

/// Do we want to move this air unit to a new base?
bool CvTacticalAI::ShouldRebase(CvUnit* pUnit) const
{
	if (!pUnit || pUnit->getDomainType()!=DOMAIN_AIR)
		return false;

	CvPlot* pUnitPlot = pUnit->plot();
	if (!pUnitPlot)
		return false;

	// Is this unit in a base in danger?
	if (pUnitPlot->isCity())
	{
		if (pUnitPlot->getPlotCity()->isInDangerOfFalling(true))
			return true;

		if (pUnit->shouldHeal(true) && m_pPlayer->GetPlotDanger(pUnitPlot->getPlotCity())>0)
			return true;
	}
	else
	{
		CvUnit *pCarrier = pUnit->getTransportUnit();
		if (pCarrier && pCarrier->isProjectedToDieNextTurn())
			return true;

		if (pUnit->shouldHeal(true) && pCarrier->GetDanger(pUnitPlot)>0)
			return true;

		if (IsAirUnitCommittedToActiveCarrierGroup(pUnit))
			return false;
	}

	bool bIsNeeded = false;
	if (!m_pPlayer->GetPlayersAtWarWith().empty())
	{
		switch (pUnit->getUnitInfo().GetDefaultUnitAIType())
		{
		case UNITAI_DEFENSE_AIR:
			// Is this a fighter that doesn't have any useful missions nearby
			{
				int iNumNearbyEnemyAirUnits = m_pPlayer->GetMilitaryAI()->GetNumEnemyAirUnitsInRange(pUnitPlot, pUnit->GetRange(), true /*bCountFighters*/, true /*bCountBombers*/);
				if (iNumNearbyEnemyAirUnits > 0  || m_pPlayer->GetMilitaryAI()->GetBestAirSweepTarget(pUnit))
				{
					bIsNeeded = true;
				}
			}
			break;
		case UNITAI_ATTACK_AIR:
		case UNITAI_ICBM:
		case UNITAI_MISSILE_AIR:
			//Is this a bomber or a missile that lacks useful target?
			{
				//check for targets in tactical map
				for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
				{
					// If it's a nuke, we only want city targets
					if (pUnit->canNuke())
					{
						if (m_AllTargets[iI].GetTargetType() != AI_TACTICAL_TARGET_ENEMY_CITY)
							continue;
						//if the city is already weak, no point in nuking it
						CvPlot* pTargetPlot = GC.getMap().plot(m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY());
						if (pTargetPlot->getPlotCity()->isInDangerOfFalling())
							continue;
					}

					// Is the target of an appropriate type?
					if(m_AllTargets[iI].GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT)
					{
						// Is this target near enough?
						if(plotDistance(pUnit->getX(), pUnit->getY(), m_AllTargets[iI].GetTargetX(), m_AllTargets[iI].GetTargetY()) <= pUnit->GetRange())
						{
							bIsNeeded = true;
							break;
						}
					}
				}
			}
			break;
		default:
			UNREACHABLE(); // Unit type cannot rebase.
		}

	}

	return !bIsNeeded;
}

// Find a faraway target for a unit to wander towards
// Can be either a specific type or any offensive type
// Returns the closest matching target that is reachable for the unit
CvPlot* CvTacticalAI::FindNearbyTarget(CvUnit* pUnit, int iMaxTurns, bool bOffensive)
{
	if (pUnit == NULL)
		return NULL;

	vector<OptionWithScore<CvPlot*>> candidates;

	// Loop through all appropriate targets to find the closest
	for(unsigned int iI = 0; iI < m_AllTargets.size(); iI++)
	{
		CvTacticalTarget target = m_AllTargets[iI];

		// Is the target of an appropriate type?
		bool bTypeMatch = false;
		if(bOffensive)
		{
			if (target.GetTargetType() == AI_TACTICAL_TARGET_ENEMY_COMBAT_UNIT ||
				target.GetTargetType() == AI_TACTICAL_TARGET_ENEMY_CITY)
			{
				bTypeMatch = !TacticalAIHelpers::IsSuicideMeleeAttack(pUnit, GC.getMap().plotUnchecked(target.GetTargetX(), target.GetTargetY()));
			}

			if (target.GetTargetType() == AI_TACTICAL_TARGET_IMPROVEMENT ||
				target.GetTargetType() == AI_TACTICAL_TARGET_IMPROVEMENT_RESOURCE ||
				(target.GetTargetType() == AI_TACTICAL_TARGET_TRADE_UNIT_LAND && pUnit->getDomainType()==DOMAIN_LAND) ||
				(target.GetTargetType() == AI_TACTICAL_TARGET_TRADE_UNIT_SEA && pUnit->getDomainType()==DOMAIN_SEA) ||
 				target.GetTargetType() == AI_TACTICAL_TARGET_HIGH_PRIORITY_CIVILIAN ||
				target.GetTargetType() == AI_TACTICAL_TARGET_LOW_PRIORITY_CIVILIAN )
			{
				bTypeMatch = true;
			}
		}
		else //defensive targets
		{
			if (target.GetTargetType() == AI_TACTICAL_TARGET_FRIENDLY_CITY ||
				(pUnit->isBarbarian() && target.GetTargetType() == AI_TACTICAL_TARGET_BARBARIAN_CAMP))
			{
				bTypeMatch = true;
			}
		}

		// Is this unit near enough?
		if (bTypeMatch)
		{
			CvPlot* pPlot = GC.getMap().plot(target.GetTargetX(), target.GetTargetY());
			if (!pPlot)
				continue;

			if (plotDistance(target.GetTargetX(), target.GetTargetY(),pUnit->getX(),pUnit->getY()) > iMaxTurns*3)
				continue;

			//can't do anything if we would need to embark
			if (pPlot->needsEmbarkation(pUnit))
				continue;

			//Ranged naval unit? Let's get a water plot (naval melee can enter cities, don't care for others)
			if (!pPlot->isWater() && pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_SEA)
			{
				pPlot = MilitaryAIHelpers::GetCoastalWaterNearPlot(pPlot);
				if (!pPlot)
					continue;
			}
	
			//shortcut, may happen often (do this after the domain checks so don't accidentally get stuck in the wrong domain)
			if (pUnit->plot() == pPlot)
				return pPlot;

			candidates.push_back(OptionWithScore<CvPlot*>(pPlot, plotDistance(*pPlot, *pUnit->plot())));
		}
	}

	//second round. default sort order is descending
	std::stable_sort(candidates.begin(), candidates.end());
	std::reverse(candidates.begin(), candidates.end());

	for (size_t i=0; i<candidates.size(); i++)
	{
		CvPlot* pPlot = candidates[i].option;
		if ( pUnit->TurnsToReachTarget(pPlot,CvUnit::MOVEFLAG_APPROX_TARGET_RING1|CvUnit::MOVEFLAG_IGNORE_STACKING_SELF|CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER,iMaxTurns) < INT_MAX )
			return pPlot;
	}

	return NULL;
}

/// Remove a unit that we've allocated from list of units to move this turn
void CvTacticalAI::UnitProcessed(int iID)
{
	m_CurrentTurnUnits.remove(iID);

	CvUnit* pUnit = m_pPlayer->getUnit(iID);
	if (!pUnit)
		return;

	if (iID==gCurrentUnitToTrack)
	{
		CvPlayer& owner = GET_PLAYER(pUnit->getOwner());
		OutputDebugString( CvString::format("turn %03d: used %s %s %d for tactical move %s. hitpoints %d, pos (%d,%d), danger %d\n", 
			GC.getGame().getGameTurn(), owner.getCivilizationAdjective(), pUnit->getName().c_str(), gCurrentUnitToTrack,
			tacticalMoveNames[m_CurrentMoveUnits.getCurrentTacticalMove()], 
			pUnit->GetCurrHitPoints(), pUnit->getX(), pUnit->getY(), pUnit->GetDanger() ) );
	}

	pUnit->setTacticalMove(m_CurrentMoveUnits.getCurrentTacticalMove());
	pUnit->SetTurnProcessed(true);

	//try and upgrade units even if tactical AI is using them
	if (pUnit->CanUpgradeRightNow(false) && !pUnit->IsHurt())
	{
		// Don't upgrade if we will go over supply
		if (m_pPlayer->GetNumUnitsToSupply() < m_pPlayer->GetNumUnitsSupplied() || !pUnit->isNoSupply())
		{
			CvUnit* pNewUnit = pUnit->DoUpgrade();
			if (pNewUnit)
				pNewUnit->SetTurnProcessed(true);
		}
	}
}

/// Is this civilian target of high priority?
bool CvTacticalAI::IsHighPriorityCivilianTarget(CvTacticalTarget* pTarget)
{
	//barbarians don't care
	if (m_pPlayer->isBarbarian())
		return true;

	CvUnit* pUnit = pTarget->GetUnitPtr();
	if (pUnit && pUnit->IsCivilianUnit())
	{
		if (pUnit->IsCombatSupportUnit())
			return true;

		if (pUnit->AI_getUnitAIType() == UNITAI_SETTLE)
			return true;
	}

	return false;
}

FILogFile* CvTacticalAI::GetLogFile()
{
	return LOGFILEMGR.GetLog(GetLogFileName(m_pPlayer->getCivilizationShortDescription()), FILogFile::kDontTimeStamp | FILogFile::kDontFlushOnWrite);
}

/// Log current status of the operation
void CvTacticalAI::LogTacticalMessage(const CvString& strMsg)
{
	if(GC.getLogging() && GC.getAILogging())
	{
		CvString strOutBuf;
		CvString strBaseString;

		CvString strPlayerName(m_pPlayer->getCivilizationShortDescription());
		strPlayerName.Replace(' ', '_'); //no spaces!

		// Get the leading info for this line
		strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
		strBaseString += strPlayerName + ", ";

		strOutBuf = strBaseString + strMsg;

		FILogFile* pLog = GetLogFile();
		if (pLog)
			pLog->Msg(strOutBuf);
	}
}

/// Build log filename
CvString CvTacticalAI::GetLogFileName(const CvString& playerName) const
{
	CvString strLogName;

	// Open the log file
	if(GC.getPlayerAndCityAILogSplit())
	{
		strLogName = "PlayerTacticalAILog_" + playerName + ".csv";
	}
	else
	{
		strLogName = "PlayerTacticalAILog.csv";
	}

	return strLogName;
}

// HELPER FUNCTIONS
bool TacticalAIHelpers::SortByExpectedTargetDamageDescending(const CvTacticalUnit& obj1, const CvTacticalUnit& obj2)
{
	return obj1.GetExpectedTargetDamage()*2-obj1.GetExpectedSelfDamage() > obj2.GetExpectedTargetDamage()*2-obj2.GetExpectedSelfDamage();
}

ReachablePlots TacticalAIHelpers::GetAllPlotsInReachThisTurn(const CvUnit* pUnit, const CvPlot* pStartPlot, int iFlags, int iMinMovesLeft, int iStartMoves, const PlotIndexContainer& plotsToIgnoreForZOC)
{
	if (!pStartPlot)
		return ReachablePlots();

	if (!plotsToIgnoreForZOC.empty())
		iFlags |= CvUnit::MOVEFLAG_SELECTIVE_ZOC;

	SPathFinderUserData data(pUnit, iFlags, 1);
	data.iMinMovesLeft = iMinMovesLeft;
	if (iStartMoves>-1) //overwrite this only if we have a sane value
		data.iStartMoves = iStartMoves;
	data.plotsToIgnoreForZOC = plotsToIgnoreForZOC;

	return GC.GetPathFinder().GetPlotsInReach(pStartPlot->getX(), pStartPlot->getY(), data);
}

vector<int> TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(const CvUnit* pUnit, const CvPlot* pBasePlot, bool bOnlyWithEnemy, bool bIgnoreVisibility)
{
	vector<int> resultSet;

	if (!pUnit || !pBasePlot)
		return resultSet;

	int iRange = min(5,max(1,pUnit->GetRange()));
	for(int i=1; i<RING_PLOTS[iRange]; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pBasePlot,i);
		if (!pLoopPlot)
			continue;

		if (!bOnlyWithEnemy || pLoopPlot->isEnemyCity(*pUnit) || pLoopPlot->isEnemyUnit(pUnit->getOwner(),true,!bIgnoreVisibility))
			if (pUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY(), pBasePlot, bIgnoreVisibility))
				resultSet.push_back(pLoopPlot->GetPlotIndex());
	}

	return resultSet;
}

std::set<int> TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(const CvUnit* pUnit, ReachablePlots& basePlots, bool bOnlyWithEnemy, bool bIgnoreVisibility)
{
	std::set<int> resultSet;

	if (!pUnit || !pUnit->IsCanAttackRanged())
		return resultSet;

	int iRange = min(5,max(1,pUnit->GetRange()));
	for (ReachablePlots::const_iterator base=basePlots.begin(); base!=basePlots.end(); ++base)
	{
		CvPlot* pBasePlot = GC.getMap().plotByIndexUnchecked( base->iPlotIndex );

		int iPlotMoves = base->iMovesLeft;
		if (iPlotMoves<=0)
			continue;

		//can't shoot if embarked
		if (!pUnit->isNativeDomain(pBasePlot))
			continue;

		//we have enough moves for an attack ...
		for(int i=1; i<RING_PLOTS[iRange]; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pBasePlot,i);

			//if the plot is already know to be attackable, don't check again
			//the reverse is not true: from another base plot the attack might work!
			if (!pLoopPlot || resultSet.find(pLoopPlot->GetPlotIndex())!=resultSet.end())
				continue;

			if (!bOnlyWithEnemy || pLoopPlot->isEnemyCity(*pUnit) || pLoopPlot->isEnemyUnit(pUnit->getOwner(),true,!bIgnoreVisibility))
				if (pUnit->canEverRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY(), pBasePlot, bIgnoreVisibility))
					resultSet.insert(pLoopPlot->GetPlotIndex());
		}
	}

	return resultSet;
}

void TacticalAIHelpers::UpdatePlotDistanceToTarget(PlayerTypes ePlayer, CvPlot* pTargetPlot)
{
	gDistanceToTargetPlots.clear();

	SPathFinderUserData data(ePlayer, PT_LAND_UNIT_SIMPLE);
	if (!GET_PLAYER(ePlayer).CanEmbark())
		data.iFlags |= CvUnit::MOVEFLAG_NO_EMBARK;

	ReachablePlots eReachablePlots = GC.GetStepFinder().GetPlotsInReach(pTargetPlot, data);
	gDistanceToTargetPlots[DOMAIN_LAND] = eReachablePlots;

	data.ePath = PT_NAVAL_UNIT_SIMPLE;
	if (!GET_TEAM(GET_PLAYER(ePlayer).getTeam()).CanBuildOceanCrossingUnit())
		data.iFlags |= CvUnit::MOVEFLAG_NO_OCEAN;

	eReachablePlots = GC.GetStepFinder().GetPlotsInReach(pTargetPlot, data);
	gDistanceToTargetPlots[DOMAIN_SEA] = eReachablePlots;
	gTargetPlot = pTargetPlot;
}

int TacticalAIHelpers::GetPlotDistanceToTarget(int iPlotIndex, DomainTypes eDomain)
{
	if (gDistanceToTargetPlots.empty())
		return plotDistance(gTargetPlot->GetPlotIndex(), iPlotIndex);

	if (eDomain == DOMAIN_HOVER)
		eDomain = DOMAIN_LAND;

	ReachablePlots::const_iterator it = gDistanceToTargetPlots[eDomain].find(iPlotIndex);
	if (it == gDistanceToTargetPlots[eDomain].end())
		return INT_MAX;

	return it->iPathLength;
}

bool TacticalAIHelpers::IsAttackNetPositive(CvUnit* pUnit, const CvPlot* pTargetPlot, int iSelfDamage)
{
	if (!pUnit || !pTargetPlot)
		return false;

	//target can be city or a unit
	CvCity* pTargetCity = pTargetPlot->getPlotCity();
	//no visibility check, when we call this we already know there is an enemy unit ...
	CvUnit* pTargetUnit = pTargetPlot->getBestDefender( NO_PLAYER, pUnit->getOwner(), pUnit, false, true);

	int iDamageDealt = 0;
	int iGarrisonDamage = 0;
	int iDamageReceived = 1;
	if (pTargetCity)
	{
		//+2 to make sure it's positive if city has zero hitpoints left
		iDamageDealt = GetSimulatedDamageFromAttackOnCity(pTargetCity, pUnit, pUnit->plot(), iDamageReceived, iGarrisonDamage, false, iSelfDamage) + 2;
		return (iDamageDealt + iGarrisonDamage > iDamageReceived || iDamageDealt == pTargetCity->GetMaxHitPoints()-pTargetCity->getDamage());
	}
	else if (pTargetUnit)
	{
		iDamageDealt = GetSimulatedDamageFromAttackOnUnit(pTargetUnit, pUnit, pTargetUnit->plot(), pUnit->plot(), iDamageReceived, false, iSelfDamage);
		return (iDamageDealt > iDamageReceived || iDamageDealt == pTargetUnit->GetCurrHitPoints());
	}

	return false;
}

//see if there is a possible target around the unit
bool TacticalAIHelpers::PerformOpportunityAttack(CvUnit* pUnit, bool bAllowMovement)
{
	if (!pUnit || !pUnit->IsCanAttack() || !pUnit->canMove() || pUnit->isDelayedDeath())
		return false;

	//for ranged we have a readymade method
	if (pUnit->IsCanAttackRanged())
		return TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, bAllowMovement);

	//where can we go
	CvPlot* pOrigin = pUnit->plot();
	vector<CvPlot*> testPlots;
	if (bAllowMovement)
	{
		ReachablePlots reachablePlots = pUnit->GetAllPlotsInReachThisTurn(true, true, false);
		for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
		{
			CvPlot *pTestPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
			testPlots.push_back(pTestPlot);
		}
	}
	else
	{
		//simply check all adjacent plots. if the unit is a garrison or the attack is not a kill, it will not advance after attacking
		for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
		{
			CvPlot* pTestPlot = iterateRingPlots(pOrigin, i);
			if (pTestPlot && pUnit->canMoveInto(*pTestPlot,CvUnit::MOVEFLAG_ATTACK))
				testPlots.push_back(pTestPlot);
		}
	}

	//what can we do?
	vector<SPlotWithScore> meleeTargets;
	int iScoreThreshold = 0;
	for (size_t i = 0; i < testPlots.size(); i++)
	{
		CvPlot* pTestPlot = testPlots[i];
		if (!pTestPlot || pTestPlot->isCity())
			continue;

		//attack an enemy?
		if (pTestPlot->isEnemyUnit(pUnit->getOwner(), true, true) && !pUnit->isOutOfAttacks())
		{
			CvUnit* pEnemy = pTestPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), pUnit, true);
			PRECONDITION(pEnemy, "isEnemyUnit is true, but getBestDefender didn't return a unit");
			int iDamageReceived = 0;
			int iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pEnemy, pUnit, pTestPlot, pOrigin, iDamageReceived);
			int iPostCombatHp = pUnit->GetCurrHitPoints() - iDamageReceived;
			bool bWouldKill = (iDamageDealt >= pEnemy->GetCurrHitPoints());

			//no suicide
			if (iDamageReceived >= pUnit->GetCurrHitPoints())
				continue;

			//there might be stacking issues so do another pathfinder check
			if (pUnit->TurnsToReachTarget(pTestPlot, CvUnit::MOVEFLAG_ATTACK | CvUnit::MOVEFLAG_NO_EMBARK, 1) > 0)
				continue;

			// Opportunity attacks are a fast heuristic; add one post-attack danger check so
			// naval melee units do not lunge into obvious coastal kill zones they cannot survive.
			UnitIdContainer killedEnemies;
			const CvPlot* pPlotAfterAttack = pOrigin;
			if (bWouldKill)
			{
				killedEnemies.push_back(pEnemy->GetID());
				pPlotAfterAttack = pTestPlot;
				iPostCombatHp += pUnit->getHPHealedIfDefeatEnemy();
				iPostCombatHp = min(iPostCombatHp, pUnit->GetMaxHitPoints());
			}

			if (pUnit->GetDanger(pPlotAfterAttack, killedEnemies, iDamageReceived) >= iPostCombatHp)
				continue;

			if (iDamageDealt >= pEnemy->GetCurrHitPoints())
			{
				if (iDamageReceived < pUnit->GetCurrHitPoints())
					iDamageDealt += 30; //bonus for a kill, but no suicide

				int iHealFromKill = pUnit->getHPHealedIfDefeatEnemy();
				if (iHealFromKill > pUnit->getDamage() + iDamageReceived)
					iHealFromKill = pUnit->getDamage() + iDamageReceived;
				iDamageReceived -= iHealFromKill;

				if (!bAllowMovement && !pUnit->plot()->isFortification(pUnit->getTeam()))
					continue;
			}

			//we have just a single attacker, avoid enemy clusters
			if (iDamageReceived * 4 >= (pUnit->GetMaxHitPoints() - (pUnit->getDamage() + iDamageReceived)) * 3 && pTestPlot->GetNumEnemyUnitsAdjacent(pUnit->getTeam(), NO_DOMAIN) > 0)
				continue;

			//if the attacker is (almost) unhurt, we can be a bit more aggressive and assume we'll heal up next turn
			int iHealRate = pUnit->ActualHealRate(pUnit->plot());
			if (pUnit->getDamage() < iHealRate / 2 && iDamageReceived < pUnit->GetMaxHitPoints() / 2)
			{
				if (iHealRate > pUnit->getDamage() + iDamageReceived)
					iHealRate = pUnit->getDamage() + iDamageReceived;
				iDamageReceived -= iHealRate;
			}

			if (iDamageReceived <= 0)
			{
				iDamageDealt += iDamageReceived + 1;
				iDamageReceived = 0;
			}

			int iScore = (1000 * iDamageDealt) / (iDamageReceived + 10);
			meleeTargets.push_back(SPlotWithScore(pTestPlot, iScore));

			//increase the threshold for each new enemy we find
			iScoreThreshold += 1000;
		}
		//maybe capture a civilian?
		else if (pTestPlot->isEnemyUnit(pUnit->getOwner(), false, true))
		{
			bool bIsSafe = pUnit->GetDanger(pTestPlot) == 0 && pUnit->GetDanger() == 0;
			bool bCanReturn = pUnit->TurnsToReachTarget(pTestPlot, CvUnit::MOVEFLAG_ATTACK, 1) == 0;
			if (bIsSafe || bCanReturn)
				meleeTargets.push_back(SPlotWithScore(pTestPlot, 10 - plotDistance(*pTestPlot, *pUnit->plot())));
		}
	}

	//nothing to do?
	if (meleeTargets.empty())
		return false;

	std::stable_sort(meleeTargets.begin(), meleeTargets.end());

	//we will never do attacks with negative scores!
	if (meleeTargets.back().score < iScoreThreshold)
		return false;

	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg;
		strMsg.Format("Performing melee opportunity attack on (%d:%d) with %s at (%d:%d)",
			meleeTargets.front().pPlot->getX(), meleeTargets.front().pPlot->getY(), pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
		GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strMsg);
	}

	pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), meleeTargets.back().pPlot->getX(), meleeTargets.back().pPlot->getY());
	if (pUnit->canMove() && !pUnit->atPlot(*pOrigin)) //try to move back to the original plot (if we advanced)
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pOrigin->getX(), pOrigin->getY());

	return true;
}

//see if we can hit anything from our current plot - with or without moving
bool TacticalAIHelpers::PerformRangedOpportunityAttack(CvUnit* pUnit, bool bAllowMovement, int bSaveMovement)
{
	if (!pUnit || !pUnit->IsCanAttackRanged() || !pUnit->canMove() || pUnit->isDelayedDeath())
		return false;

	CvPlot* pBasePlot = pUnit->plot();
	bool bIsAirUnit = pUnit->getDomainType() == DOMAIN_AIR;
	if (bIsAirUnit || (pUnit->IsGarrisoned() && pUnit->getDomainType() == DOMAIN_LAND && pUnit->plot()->getPlotCity()->NeedsGarrison()))
		bAllowMovement = false;

	if (bAllowMovement || pUnit->canMoveAfterAttacking())
	{
		//no loop needed, there is only one unit anyway
		set<int> dummy;
		gTargetPlot = pUnit->plot();
		vector<STacticalAssignment> vAssignments = TacticalAIHelpers::FindBestUnitAssignments(vector<CvUnit*>(1, pUnit), pUnit->plot(), AL_LOW, dummy, false, !bAllowMovement, bSaveMovement);
		if (vAssignments.empty())
			return false;

		if (GC.getLogging() && GC.getAILogging())
		{
			CvString strMsg;
			strMsg.Format("Performing ranged opportunity attack with %s at (%d:%d)", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
			GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strMsg);
		}

		return TacticalAIHelpers::ExecuteUnitAssignments(pUnit->getOwner(), vAssignments);
	}
	else
	{
		int iMaxDamage = 0;
		CvPlot* pBestTarget = NULL;

		int iRange = max(1,min(5,pUnit->GetRange()));
		CvCity* pDefenseCity = pUnit->IsGarrisoned() ? pUnit->plot()->getPlotCity() : NULL;
		bool bHasPriorityRangedThreat = false;
		if (pDefenseCity)
		{
			for (int i = RING0_PLOTS; i < RING_PLOTS[iRange]; i++)
			{
				CvPlot* pScanPlot = iterateRingPlots(pBasePlot, i);
				if (!pScanPlot || pScanPlot->isCity())
					continue;

				if (!pUnit->canRangeStrikeAt(pScanPlot->getX(), pScanPlot->getY()))
					continue;

				CvUnit* pScanUnit = pScanPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), pUnit, true /*testWar*/);
				if (!pScanUnit || pScanUnit->isDelayedDeath())
					continue;

				UnitAITypes eScanAI = pScanUnit->AI_getUnitAIType();
				if (eScanAI != UNITAI_CITY_BOMBARD && !pScanUnit->IsCanAttackRanged())
					continue;

				int iTargetRing = plotDistance(*pScanPlot, *pDefenseCity->plot());
				bool bCanReachCity = (iTargetRing <= pScanUnit->GetRange() + pScanUnit->baseMoves(false));

				if (bCanReachCity)
				{
					bHasPriorityRangedThreat = true;
					break;
				}
			}
		}
		for (int i=RING0_PLOTS; i<RING_PLOTS[iRange]; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(pBasePlot, i);
			if (!pLoopPlot || pLoopPlot->isCity())
				continue;

			if (!pUnit->canRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY()))
				continue;

				//don't blindly attack the first one we find, check how much damage we can do
			CvUnit* pOtherUnit = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), pUnit, true /*testWar*/);
			if (pOtherUnit && !pOtherUnit->isDelayedDeath())
			{
				int  iUnusedReferenceVariable = 0;
				int iDamage = bIsAirUnit ? pUnit->GetAirCombatDamage(pOtherUnit, NULL, 0, iUnusedReferenceVariable, false) :
											pUnit->GetRangeCombatDamage(pOtherUnit, NULL, 0, iUnusedReferenceVariable, false) +  pUnit->GetRangeCombatSplashDamage(pOtherUnit->plot()) + (pUnit->hasMoved() ? 0 : pUnit->GetTileDamageIfNotMoved());

				//kill bonus
				if (iDamage >= pOtherUnit->GetCurrHitPoints())
					iDamage += 30;

				// Ranged garrisons should prioritize targets intelligently
				if (pUnit->IsGarrisoned())
				{
					CvCity* pCity = pUnit->plot()->getPlotCity();
					
					// COMBINED ARMS DEFENSE COORDINATION:
					// Check what the city is likely to target and avoid overlapping
					// City attack happens before garrison ranged, so coordinate
					CvUnit* pCityTarget = NULL;
					if (pCity && pCity->canRangeStrike() && !pCity->isMadeAttack())
					{
						pCityTarget = pCity->getBestRangedStrikeTarget();
					}
					
					// Count other friendly defenders for coordinated fire assessment
					int iFriendlyNavalRanged = 0;
					for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
					{
						CvPlot* pAdj = plotDirection(pCity->getX(), pCity->getY(), (DirectionTypes)iDir);
						if (pAdj && pAdj->isWater())
						{
							CvUnit* pNaval = pAdj->getBestDefender(pUnit->getOwner());
							if (pNaval && pNaval->getDomainType() == DOMAIN_SEA && pNaval->IsCanAttackRanged())
								iFriendlyNavalRanged++;
						}
					}
					
					// If city is targeting this unit, consider spreading fire to other threats
					// Unless this target can be killed with combined fire
					if (pCityTarget == pOtherUnit)
					{
						int iCityDamage = pCity->rangeCombatDamage(pOtherUnit, false, NULL);
						int iCombinedDamage = iDamage + iCityDamage;
						
						if (iCombinedDamage >= pOtherUnit->GetCurrHitPoints())
						{
							// Combined fire CAN kill - coordinate for the kill!
							iDamage += 60;
						}
						else
						{
							// Can't kill with combined fire - spread damage to other targets
							// unless there are no other valid targets
							iDamage -= 20;
						}
					}
					else if (pCityTarget != NULL)
					{
						// City is targeting someone else - bonus for attacking different unit
						// This spreads damage across multiple threats
						iDamage += 15;
					}
					
					// THREAT TO CITY ASSESSMENT - same hierarchy as city targeting:
					// Siege > Land Ranged > Naval Ranged > Adjacent Wounded Melee > Other
					bool bIsNavalTarget = (pOtherUnit->getDomainType() == DOMAIN_SEA);
					int iTargetRing = pCity ? plotDistance(*pLoopPlot, *pCity->plot()) : 0;
					UnitAITypes eTargetAI = pOtherUnit->AI_getUnitAIType();
					int iCityHPPct = 100;
					if (pCity)
						iCityHPPct = ((pCity->GetMaxHitPoints() - pCity->getDamage()) * 100) / pCity->GetMaxHitPoints();

					// Check if target can attack the city
					bool bTargetCanReachCity = false;
					bool bMovedThisTurn = (pOtherUnit->getLastMoveTurn() == GC.getGame().getGameTurn());
					if (pOtherUnit->IsCanAttackRanged() || eTargetAI == UNITAI_CITY_BOMBARD)
						bTargetCanReachCity = (iTargetRing <= pOtherUnit->GetRange() + pOtherUnit->baseMoves(false));
					else
						bTargetCanReachCity = (iTargetRing <= 1 + pOtherUnit->baseMoves(false));

					// Priority 1: Enemy siege units - biggest threat to city walls
					if (eTargetAI == UNITAI_CITY_BOMBARD)
					{
						iDamage += 40;
						if (bTargetCanReachCity)
							iDamage += 20;
					}
					// Priority 2: Enemy ranged units - differentiate land vs naval
					else if (pOtherUnit->IsCanAttackRanged())
					{
						if (bIsNavalTarget)
						{
							// Naval ranged: less threat to city than land ranged
							iDamage += 15;
							if (bTargetCanReachCity)
								iDamage += 5;
						}
						else
						{
							// Land ranged: significant threat to city
							iDamage += 25;
							if (bTargetCanReachCity)
								iDamage += 10;
						}
					}
					// Priority 3: Adjacent wounded melee - capture threat!
					else if (pCity && iTargetRing == 1 && 
							 pOtherUnit->GetCurrHitPoints() < pOtherUnit->GetMaxHitPoints() / 2)
					{
						if (bIsNavalTarget)
						{
							// Naval melee: only urgent at low city HP
							iDamage += (iCityHPPct <= 50) ? 35 : 15;
						}
						else
						{
							// Land melee adjacent wounded - urgent capture threat
							iDamage += 35;
						}
					}
					// Regular melee further away - lowest priority (no bonus)

					// Disengage penalty: don't target non-threatening units when city is under siege
					if (!pOtherUnit->isEmbarked() && !bTargetCanReachCity &&
						iDamage < pOtherUnit->GetCurrHitPoints())
					{
						iDamage -= 30;
						if (pOtherUnit->GetCurrHitPoints() < pOtherUnit->GetMaxHitPoints() / 2)
							iDamage -= 20;
					}

					// RETREAT DETECTION via Extended Memory System (Phase 3)
					// City-aware directional intent: dot product of movement vector vs
					// unit→city vector to distinguish approach from withdrawal.
					// Falls back to lightweight heuristic if sighting data is unavailable.
					bool bLikelyRetreating = false;
					bool bConfirmedAttacking = false;
					if (pCity)
					{
						const CvUnitSightingManager& sightMgr = GET_PLAYER(pUnit->getOwner()).GetUnitSightingManager();
						const UnitSighting* pSighting = sightMgr.GetSighting(
							pOtherUnit->getOwner(), pOtherUnit->GetID());
						if (pSighting && !pSighting->IsExpired(GC.getGame().getGameTurn()))
						{
							UnitPredictedIntent eIntent = sightMgr.InferUnitIntentNearCity(
								pSighting, pCity->getX(), pCity->getY(), GC.getGame().getGameTurn(), bMovedThisTurn);
							bLikelyRetreating = (eIntent == UNIT_INTENT_RETREAT);
							bConfirmedAttacking = (eIntent == UNIT_INTENT_ATTACK_CITY);
						}
						else
						{
							// Fallback: lightweight heuristic (wounded + moved this turn)
							bLikelyRetreating = (bMovedThisTurn &&
								pOtherUnit->GetCurrHitPoints() <= pOtherUnit->GetMaxHitPoints() / 2);
						}
					}
					else
					{
						bLikelyRetreating = (bMovedThisTurn &&
							pOtherUnit->GetCurrHitPoints() <= pOtherUnit->GetMaxHitPoints() / 2);
					}
					if (bLikelyRetreating && iCityHPPct <= 50 && eTargetAI != UNITAI_CITY_BOMBARD)
					{
						int iRetreatPenalty = 60;
						if (!bTargetCanReachCity)
							iRetreatPenalty += 20;
						iDamage -= iRetreatPenalty;
					}
					else if (bConfirmedAttacking && bTargetCanReachCity && iCityHPPct <= 50)
					{
						// Sighting manager confirms this unit is attacking — small priority boost
						iDamage += 15;
					}
					
					// Embarked units are extremely vulnerable targets - prioritize them!
					// Similar logic to city ranged strike in getBestRangedStrikeTarget()
					if (pOtherUnit->isEmbarked())
					{
						// Embarked units have very low combat strength - easy kills
						iDamage += 50; // Base bonus for targeting embarked
						
						// Kill bonus - embarked are often one-shot
						if (iDamage >= pOtherUnit->GetCurrHitPoints())
							iDamage += 60;
						
						// Adjacent embarked is a landing threat
						if (pCity && plotDistance(*pLoopPlot, *pCity->plot()) == 1)
							iDamage += 40;
						
						// Embarked siege/ranged are high value (kill before they land and bombard)
						if (pOtherUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD || pOtherUnit->IsCanAttackRanged())
							iDamage += 30;
						
						// Suppress embarked bonuses for retreating units when city HP is low
						// Consistent with city bombardment logic in getBestRangedStrikeTarget()
						if (bLikelyRetreating && iCityHPPct <= 50 &&
							pOtherUnit->AI_getUnitAIType() != UNITAI_CITY_BOMBARD)
						{
							iDamage -= 80;
							if (iDamage < 0) iDamage = 0;
						}
					}
					
					// Escort protection check - if a naval unit is protecting embarked units,
					// bonus for killing the escort to expose the transports
					if (pLoopPlot->isWater() && !pOtherUnit->isEmbarked() && pOtherUnit->IsCombatUnit())
					{
						int iEmbarkedOnPlot = 0;
						for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
						{
							CvUnit* pStackUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
							if (pStackUnit && pStackUnit != pOtherUnit && pStackUnit->isEmbarked() &&
								GET_TEAM(pUnit->getTeam()).isAtWar(pStackUnit->getTeam()))
							{
								iEmbarkedOnPlot++;
							}
						}
						
						if (iEmbarkedOnPlot > 0)
						{
							// This naval unit is escorting embarked units - killing it exposes them!
							// +40 base + 15 per embarked unit protected
							iDamage += 40 + (iEmbarkedOnPlot * 15);
							
							// Extra bonus if we can kill the escort
							if (iDamage >= pOtherUnit->GetCurrHitPoints())
								iDamage += 30;
						}
					}

					// If ranged/siege threats to the city are present, avoid shooting melee screens
					// unless an adjacent melee can actually one-shot capture the city now.
					if (bHasPriorityRangedThreat && !pOtherUnit->IsCanAttackRanged() &&
						pOtherUnit->AI_getUnitAIType() != UNITAI_CITY_BOMBARD)
					{
						bool bImmediateCaptureThreat = false;
						if (pDefenseCity && plotDistance(*pLoopPlot, *pDefenseCity->plot()) == 1 &&
							pOtherUnit->IsCanAttackWithMove() && pOtherUnit->canMoveOrAttackInto(*pDefenseCity->plot()))
						{
							int iDefenseCityHP = pDefenseCity->GetMaxHitPoints() - pDefenseCity->getDamage();
							int iAttackerDamage = 0;
							int iProjectedCityDamage = TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(
								pDefenseCity, pOtherUnit, pLoopPlot, iAttackerDamage, true, 0, true);
							bImmediateCaptureThreat = (iProjectedCityDamage >= iDefenseCityHP);
						}
						if (!bImmediateCaptureThreat)
							iDamage -= 120;
					}
				}

				if (iDamage > iMaxDamage)
				{
					pBestTarget = pLoopPlot;
					iMaxDamage = iDamage;
				}
			}
		}

		if (!pBestTarget)
			return false;

		if (GC.getLogging() && GC.getAILogging())
		{
			CvString strMsg;
			strMsg.Format("Performing stationary ranged opportunity attack with %s at (%d:%d) on (%d:%d)",
				pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY(), pBestTarget->getX(), pBestTarget->getY());
			GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strMsg);
		}

		pUnit->PushMission(bIsAirUnit ? CvTypes::getMISSION_MOVE_TO() : CvTypes::getMISSION_RANGE_ATTACK(), pBestTarget->getX(), pBestTarget->getY());
		return true;
	}
}

pair<CvPlot*, int> TacticalAIHelpers::FindSafestPlotInReach(const CvUnit* pUnit, bool bAllowEmbark, bool bConsiderPush)
{
	//use rebase moves for aircraft!
	if (!pUnit || pUnit->getDomainType() == DOMAIN_AIR)
		return make_pair(static_cast<CvPlot*>(NULL), 0);

	vector<OptionWithScore<pair<CvPlot*, int>>> aCityList;
	vector<OptionWithScore<pair<CvPlot*, int>>> aZeroDangerList;
	vector<OptionWithScore<pair<CvPlot*, int>>> aCoverList;
	vector<OptionWithScore<pair<CvPlot*, int>>> aDangerList;
	vector<OptionWithScore<pair<CvPlot*, int>>> aEmbarkList; //LAST RESORT: water plots for land units

	//special behavior for cities and citadels - don't run even if there is a "safe" plot somewhere else
	CvPlot* pCurrentPlot = pUnit->plot();
	bool bIsInCityOrCitadelNow = (pCurrentPlot->isFriendlyCity(*pUnit) && !pCurrentPlot->getPlotCity()->isInDangerOfFalling()) || 
								(pUnit->IsCombatUnit() && TacticalAIHelpers::IsPlayerCitadel(pCurrentPlot, pUnit->getOwner()) && pUnit->getDomainType() == DOMAIN_LAND);
	if (bIsInCityOrCitadelNow && !pUnit->isProjectedToDieNextTurn() && pUnit->canEndTurnAtPlot(pCurrentPlot))
		if (pUnit->AI_getUnitAIType()!=UNITAI_CITY_BOMBARD || pUnit->GetDanger(pCurrentPlot)<pUnit->GetCurrHitPoints())
			return make_pair(pCurrentPlot, pUnit->getMoves());

	//for current plot
	int iCurrentHealRate = pUnit->ActualHealRate(pUnit->plot());
	int iCurrentDanger = pUnit->GetDanger();

	//don't run if we are needed
	if (pUnit->IsCoveringFriendlyCivilian() && pUnit->GetDanger(pCurrentPlot)<pUnit->GetCurrHitPoints()*2)
		return make_pair(pCurrentPlot, pUnit->getMoves());

	//compute once outside the loop - used for retreat direction check
	CvPlayer& kPlayer = GET_PLAYER(pUnit->getOwner());
	bool bImminentAttack = kPlayer.GetTacticalAI() ? kPlayer.GetTacticalAI()->IsImminentAttackCached() : false;
	int iCurrentCityDistance = kPlayer.GetCityDistancePathLength(pCurrentPlot);

	//Use SAFE_EMBARK_ONLY to filter dangerous embark plots during pathfinding where possible.
	int iEmbarkFlags = bAllowEmbark ? CvUnit::MOVEFLAG_SAFE_EMBARK_ONLY : CvUnit::MOVEFLAG_NO_EMBARK;
	ReachablePlots eligiblePlots = TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pUnit->plot(), iEmbarkFlags);
	for (ReachablePlots::iterator it = eligiblePlots.begin(); it != eligiblePlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

		// don't attack though
		if (pPlot->getNumVisibleEnemyDefenders(pUnit) > 0 || pPlot->isEnemyCity(*pUnit))
			continue;

		int iFlags = CvUnit::MOVEFLAG_DESTINATION;
		// allow capturing civilians!
		if (pPlot->isVisibleEnemyUnit(pUnit))
			iFlags |= CvUnit::MOVEFLAG_ATTACK;

		//if we cannot move in because one of our own units is blocking us
		if (!pUnit->canMoveInto(*pPlot, iFlags) && pUnit->canMoveInto(*pPlot, iFlags | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF))
		{
			if (!bConsiderPush || !pUnit->CanPushOutUnitHere(*pPlot))
				continue;
		}

		//   prefer being in a city with the lowest danger value
		//   prefer being in a plot with no danger value
		//   prefer being under a unit with the lowest danger value
		//   prefer being in your own territory with the lowest danger value
		//   prefer the lowest danger value

		// plot danger is a bit unreliable, so we need extra checks
		int iDanger = pUnit->GetDanger(pPlot);
		int iCityDistance = kPlayer.GetCityDistancePathLength(pPlot);
		if (iCityDistance == INT_MAX)
			iCityDistance = 0; // No cities (e.g. barbarians) - distance irrelevant, use 0 to avoid overflow

		bool bIsZeroDanger = (iDanger <= 0);
		bool bIsInTerritory = (pPlot->getTeam() == kPlayer.getTeam());

		bool bWrongDomain = pPlot->needsEmbarkation(pUnit);
		bool bWouldEmbark = bWrongDomain && !pUnit->isEmbarked();
		
		//RETREAT DIRECTION CHECK: Prefer plots that move us TOWARD friendly cities, not away
		//A retreating unit should not move further from safety (frontline toward enemy)
		bool bMovingTowardEnemy = (iCityDistance > iCurrentCityDistance);

		//CRITICAL FIX for domain transition bug:
		//The GetDanger() system calculates danger from enemy units that can attack a plot THIS turn.
		//However, it evaluates damage from the DEFENDER's current domain perspective.
		//For a land unit considering a water plot, GetDanger() uses the land unit's current (land) perspective,
		//which may show zero danger because naval units "can't attack land units on land."
		//But if the unit embarks, it becomes vulnerable to naval attacks - this is NOT modeled by GetDanger().
		//
		//The proper fix would be to make GetDanger() simulate domain transitions, but that's architecturally
		//complex and risky. Instead, we explicitly scan for nearby naval threats when considering embarkation.
		//This catches ships that could attack an embarked unit even if GetDanger() doesn't see them as threats.
		if (bWouldEmbark)
		{
			int iNavalThreats = 0;
			
			//Determine scan range based on game era (fallback for unseen naval units)
			//Naval unit movement by era: Ancient=3-4, Classical=4, Medieval=4-5, Renaissance=5, Industrial+=6
			//Plus ranged attack range (typically 2) = max effective threat range
			int iEra = GC.getGame().getCurrentEra();
			int iBaseNavalScanRange;
			if (iEra <= 1) // Ancient/Classical
				iBaseNavalScanRange = 5;  // trireme (4 move) + melee
			else if (iEra <= 3) // Medieval/Renaissance  
				iBaseNavalScanRange = 7;  // caravel/frigate (5 move) + ranged (2)
			else // Industrial+
				iBaseNavalScanRange = 8;  // ironclad/destroyer (6 move) + ranged (2)
			
			//Use the larger of RING5_PLOTS to scan a wide area, we'll filter by actual threat range
			for (int iI = 0; iI < RING5_PLOTS; iI++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(pPlot, iI);
				if (!pLoopPlot)
					continue;
				
				int iDist = plotDistance(*pPlot, *pLoopPlot);
				
				//Skip plots beyond our maximum scan range
				if (iDist > iBaseNavalScanRange)
					continue;
				
				//Check all units on this plot
				for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
				{
					CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
					if (!pLoopUnit)
						continue;
					
					//Is this an enemy naval unit that can attack?
					if (pLoopUnit->getDomainType() == DOMAIN_SEA && 
						pLoopUnit->isEnemy(pUnit->getTeam()) &&
						pLoopUnit->IsCanAttack())
					{
						//Calculate this specific unit's threat range based on its actual stats
						//maxMoves() includes all promotions (Navigation, etc.)
						int iUnitMoves = pLoopUnit->maxMoves() / GD_INT_GET(MOVE_DENOMINATOR);
						int iThreatRange = iUnitMoves;
						
						//Add ranged attack range if applicable
						if (pLoopUnit->IsCanAttackRanged())
							iThreatRange += pLoopUnit->GetRange();
						
						//Is this unit close enough to threaten us?
						if (iDist <= iThreatRange)
						{
							iNavalThreats++;
							
							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("FindSafestPlotInReach: %s detected naval threat %s at (%d,%d), dist=%d, threatRange=%d (moves=%d, range=%d)",
									pUnit->getName().GetCString(), pLoopUnit->getName().GetCString(),
									pLoopPlot->getX(), pLoopPlot->getY(), iDist, iThreatRange, iUnitMoves,
									pLoopUnit->IsCanAttackRanged() ? pLoopUnit->GetRange() : 0);
								GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
							}
						}
					}
				}
			}
			
			//Also check remembered sightings for naval units that recently moved into fog.
			//Use the sighting manager's last-seen coordinates rather than the unit's live plot,
			//otherwise we'd be leaking hidden information into the retreat logic.
			const CvUnitSightingManager& sightMgr = GET_PLAYER(pUnit->getOwner()).GetUnitSightingManager();
			const UnitSet& vanishedUnits = GET_PLAYER(pUnit->getOwner()).GetVanishedUnits();
			int iCurrentTurn = GC.getGame().getGameTurn();
			for (UnitSet::const_iterator it = vanishedUnits.begin(); it != vanishedUnits.end(); ++it)
			{
				const UnitSighting* pSighting = sightMgr.GetSighting(it->first, it->second);
				if (!pSighting || pSighting->IsExpired(iCurrentTurn) || pSighting->IsConfirmed(iCurrentTurn))
					continue;
				
				//Is this a remembered enemy naval unit that can attack?
				if (((pSighting->flags & SIGHTING_FLAG_NAVAL) != 0) && GET_PLAYER((PlayerTypes)pSighting->owner).isAlive())
				{
					UnitTypes eRememberedType = (UnitTypes)pSighting->unitType;
					CvUnitEntry* pkRememberedUnitInfo = GC.getUnitInfo(eRememberedType);
					if (!pkRememberedUnitInfo)
						continue;

					//Calculate distance from the unit's last seen position to the embark plot.
					int iDist = plotDistance(pPlot->getX(), pPlot->getY(), (int)pSighting->x, (int)pSighting->y);
					
					//Calculate threat range from remembered capabilities only.
					int iUnitMoves = (int)pSighting->movementPoints;
					int iThreatRange = iUnitMoves;
					
					if ((pSighting->flags & SIGHTING_FLAG_RANGED) != 0)
						iThreatRange += pkRememberedUnitInfo->GetRange();
					
					//Is this remembered unit close enough to potentially threaten us?
					//Use a slightly larger range since the unit may have moved toward us
					if (iDist <= iThreatRange + 2)
					{
						iNavalThreats++;
						
						if (GC.getLogging() && GC.getAILogging())
						{
							CvString strLogString;
							strLogString.Format("FindSafestPlotInReach: %s detected REMEMBERED naval threat unitType=%d (last seen at %d,%d), dist=%d, threatRange=%d",
								pUnit->getName().GetCString(), (int)eRememberedType,
								(int)pSighting->x, (int)pSighting->y, iDist, iThreatRange);
							GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
						}
					}
				}
			}
			
			if (iNavalThreats > 0)
			{
				bIsZeroDanger = false;
				//Set a significant danger value - embarking with naval threats is extremely dangerous
				if (iDanger < iNavalThreats * 100)
					iDanger = iNavalThreats * 100;
					
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("FindSafestPlotInReach: %s detected %d total naval threats near embark plot (%d,%d), setting danger=%d",
						pUnit->getName().GetCString(), iNavalThreats, pPlot->getX(), pPlot->getY(), iDanger);
					GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
				}
			}
		}

		// citadels have low danger but not zero. so we need to make sure we're not abandoning them too easily
		bool bIsInCityOrCitadel = (pPlot->isFriendlyCity(*pUnit) && !pPlot->getPlotCity()->isInDangerOfFalling()) ||
			(pUnit->IsCombatUnit() && TacticalAIHelpers::IsPlayerCitadel(pPlot, pUnit->getOwner()) && pUnit->getDomainType() == DOMAIN_LAND);

		// civilians and already-embarked units want cover, but fresh embark moves must stay in the embark list
		// so they do not bypass the "embark only as a last resort" ranking.
		bool bIsInCover = false;
		if (!bWouldEmbark && (pUnit->IsCivilianUnit() || !pUnit->isNativeDomain(pPlot)))
		{
			CvUnit* pDefender = pPlot->getBestDefender(pUnit->getOwner());
			if (pDefender && pDefender != pUnit && !pDefender->isProjectedToDieNextTurn() && pDefender->GetDanger()<pDefender->GetCurrHitPoints())
			{
				bIsInCover = true;
				//otherwise we will get only INT_MAX for civilians
				iDanger = pDefender->GetDanger(pPlot);
			}
		}

		//avoid overflow further down and useful handling for civilians
		if (iDanger == INT_MAX)
			iDanger = 10000;

		//map 144 to 144, everything above is not so important
		int iScore = (iDanger > 144) ? 12 * sqrti(iDanger) : iDanger;

		if (pPlot != pUnit->plot() && !pUnit->hasMoved())
		{
			//we can't heal after moving and lose fortification bonus, so the current plot gets a bonus (respectively all others a penalty)
			if (pUnit->canFortify(pUnit->plot()))
				iScore += 3;

			// We can outheal the danger, should stay and heal
			if (iCurrentHealRate > 0 && pUnit->getDamage() >= iCurrentHealRate && iCurrentHealRate > iCurrentDanger && !pUnit->isAlwaysHeal())
				iScore += max(0, iCurrentHealRate * 2);
		}

		//safer at home ... but not if we need to embark b/c we can't fight back then
		if (!bIsInTerritory || bWouldEmbark)
			iScore += bImminentAttack ? 18 : 12;

		//try to hide - if there are few enemy units, this might be a tiebreaker
		if (pPlot->IsKnownVisibleToEnemy(pUnit->getOwner()))
			iScore += bImminentAttack ? (iScore / 3) : (iScore / 4);

		//avoid enemy territory
		if (pPlot->IsAdjacentOwnedByEnemy(pUnit->getTeam()))
			iScore += iScore / 20;

		//avoid enemy units (for civilians danger may be infinite in a lot lof places)
		if (pPlot->IsEnemyUnitAdjacent(pUnit->getTeam()) || pPlot->IsEnemyCityAdjacent(pUnit->getTeam(), NULL))
			iScore += iScore / 10;

		//naval units should avoid enemy coast, never know what's hiding there
		if (pUnit->getDomainType() == DOMAIN_SEA)
			iScore += pPlot->countMatchingAdjacentPlots(DOMAIN_LAND, NO_PLAYER, pUnit->getOwner(), NO_PLAYER) * 7;

		if (!bIsInCityOrCitadel)
		{
			//try to go where our friends are
			int iFriendlyUnitsAdjacent = pPlot->GetNumFriendlyUnitsAdjacent(pUnit->getTeam(), NO_DOMAIN, true, pUnit);
			//don't go where our foes are - use NO_DOMAIN when embarking so we count naval threats too!
			DomainTypes eThreatDomain = bWouldEmbark ? NO_DOMAIN : pUnit->getDomainType();
			int iEnemyUnitsAdjacent = pPlot->GetNumEnemyUnitsAdjacent(pUnit->getTeam(), eThreatDomain);
			int iAdjacencyWeight = bImminentAttack ? 20 : 13;
			iScore += (iEnemyUnitsAdjacent - iFriendlyUnitsAdjacent) * iAdjacencyWeight;

			//use city distance as tiebreaker
			if (pUnit->getDomainType() != DOMAIN_SEA)
				iScore = iScore * 10 + iCityDistance;
			else
				// Naval units should try to go home to heal, even if it's considered dangerous
				iScore = iScore * 3 + iCityDistance;
		}

		//RETREAT DIRECTION: heavily penalize moving TOWARD enemy (further from our cities)
		//A retreating damaged unit should move toward safety, not toward the frontline
		if (bMovingTowardEnemy)
			iScore += bImminentAttack ? 160 : 100;

		//Phase 3: Prefer retreating to chokepoint tiles — creates a defensive bottleneck
		//where the enemy can only attack through a narrow corridor.
		//Only relevant for land combat units (civilians and naval units don't benefit from chokepoints).
		if (pUnit->IsCombatUnit() && pUnit->getDomainType() == DOMAIN_LAND && !bWouldEmbark)
		{
			if (pPlot->IsChokePoint())
				iScore -= 20; // significant preference for chokepoint retreat positions
		}

		//THIRD-PARTY SAFE HAVEN for LAND plots (applies to iScore for danger/zero lists)
		//Retreating into neutral territory where enemy can't follow is tactically smart
		bool bIsThirdPartySafeHaven = false;
		if (!bWouldEmbark && !bIsInTerritory)
		{
			PlayerTypes ePlotOwner = pPlot->getOwner();
			if (ePlotOwner != NO_PLAYER)
			{
				TeamTypes eOurTeam = pUnit->getTeam();
				TeamTypes ePlotTeam = pPlot->getTeam();
				
				if (ePlotTeam != NO_TEAM && !GET_TEAM(eOurTeam).isAtWar(ePlotTeam))
				{
					bool bWeHaveOpenBorders = GET_TEAM(ePlotTeam).IsAllowsOpenBordersToTeam(eOurTeam);
					if (bWeHaveOpenBorders)
					{
						//Check if enemies can't follow
						for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
						{
							PlayerTypes eEnemy = (PlayerTypes)iPlayerLoop;
							if (!GET_PLAYER(eEnemy).isAlive())
								continue;
							if (!GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(eEnemy).getTeam()))
								continue;
							
							TeamTypes eEnemyTeam = GET_PLAYER(eEnemy).getTeam();
							if (!GET_TEAM(ePlotTeam).IsAllowsOpenBordersToTeam(eEnemyTeam))
							{
								bIsThirdPartySafeHaven = true;
								iScore -= 50; //bonus for enemy-blocked territory
								
								//Extra if third party is at war with enemy
								if (GET_TEAM(ePlotTeam).isAtWar(eEnemyTeam))
									iScore -= 30;
								break;
							}
						}
					}
				}
			}
		}

		if(bIsInCityOrCitadel)
		{
			aCityList.push_back( OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft),iScore) );
		}
		else if(bIsInCover) //mostly relevant for civilians
		{
			aCoverList.push_back( OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft),iScore) );
		}
		else if(bWouldEmbark && bAllowEmbark)
		{
			//EMBARKATION IS LAST RESORT for land units - put in separate list
			//Only consider water plots if NO safe land plots are available
			//Score based on: distance to own cities, naval threats, escort availability, third-party territory
			int iEmbarkScore = iCityDistance * 10; //prefer closer to our cities
			if (bMovingTowardEnemy)
				iEmbarkScore += 500; //heavily penalize moving toward enemy frontline
			if (!bIsZeroDanger)
				iEmbarkScore += iDanger; //factor in detected naval danger
			//prefer plots in our territory (we can see threats better)
			if (!bIsInTerritory)
				iEmbarkScore += 200;
			
			//Check for friendly naval escort on this water plot
			//A friendly naval unit provides some protection, but it's still risky if they could be killed
			CvUnit* pNavalEscort = pPlot->getBestDefender(pUnit->getOwner());
			if (pNavalEscort && pNavalEscort->getDomainType() == DOMAIN_SEA && pNavalEscort->IsCombatUnit())
			{
				//We have a naval escort! Much safer, but evaluate their survival chance
				int iEscortDanger = pNavalEscort->GetDanger(pPlot);
				int iEscortHP = pNavalEscort->GetCurrHitPoints();
				
				if (iEscortDanger >= iEscortHP)
				{
					//Escort is likely to die - still risky but better than nothing
					iEmbarkScore -= 100; //small bonus for having doomed escort
				}
				else if (iEscortDanger >= iEscortHP / 2)
				{
					//Escort is in significant danger
					iEmbarkScore -= 200; //moderate bonus
				}
				else
				{
					//Escort is relatively safe - good protection!
					iEmbarkScore -= 400; //significant bonus for safe escort
				}
				
				if (GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("FindSafestPlotInReach: %s found naval escort %s at (%d,%d), escort danger=%d/%d HP, embarkScore=%d",
						pUnit->getName().GetCString(), pNavalEscort->getName().GetCString(),
						pPlot->getX(), pPlot->getY(), iEscortDanger, iEscortHP, iEmbarkScore);
					GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
				}
			}
			
			//THIRD-PARTY TERRITORY SAFE HAVEN CHECK
			//If this plot or adjacent plots are in neutral territory where:
			//  - We have open borders (can enter)
			//  - Our enemy does NOT have open borders (can't pursue with melee)
			//Then this is a potential safe haven - enemy can only attack with ranged
			PlayerTypes ePlotOwner = pPlot->getOwner();
			if (ePlotOwner != NO_PLAYER && ePlotOwner != pUnit->getOwner())
			{
				TeamTypes eOurTeam = pUnit->getTeam();
				TeamTypes ePlotTeam = pPlot->getTeam();
				
				//Check if this is a third party (not our enemy)
				if (ePlotTeam != NO_TEAM && !GET_TEAM(eOurTeam).isAtWar(ePlotTeam))
				{
					//We're not at war with the plot owner - check open borders
					bool bWeHaveOpenBorders = GET_TEAM(ePlotTeam).IsAllowsOpenBordersToTeam(eOurTeam);
					
					if (bWeHaveOpenBorders)
					{
						//Check if any of our current enemies lack open borders with this third party
						bool bEnemyBlocked = false;
						for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
						{
							PlayerTypes eEnemy = (PlayerTypes)iPlayerLoop;
							if (!GET_PLAYER(eEnemy).isAlive())
								continue;
							if (!GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(eEnemy).getTeam()))
								continue;
								
							//This is an enemy - do they have open borders with the third party?
							TeamTypes eEnemyTeam = GET_PLAYER(eEnemy).getTeam();
							bool bEnemyHasOpenBorders = GET_TEAM(ePlotTeam).IsAllowsOpenBordersToTeam(eEnemyTeam);
							
							if (!bEnemyHasOpenBorders)
							{
								bEnemyBlocked = true;
								break;
							}
						}
						
						if (bEnemyBlocked)
						{
							//This third-party territory blocks enemy pursuit! 
							//Enemy can only attack with ranged units from outside
							iEmbarkScore -= 300; //significant bonus for safe haven
							
							//Extra bonus if the third party is at war with our enemy
							for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
							{
								PlayerTypes eEnemy = (PlayerTypes)iPlayerLoop;
								if (!GET_PLAYER(eEnemy).isAlive())
									continue;
								if (!GET_TEAM(eOurTeam).isAtWar(GET_PLAYER(eEnemy).getTeam()))
									continue;
									
								if (GET_TEAM(ePlotTeam).isAtWar(GET_PLAYER(eEnemy).getTeam()))
								{
									//Third party is hostile to our enemy - even safer!
									iEmbarkScore -= 200;
									break;
								}
							}
							
							if (GC.getLogging() && GC.getAILogging())
							{
								CvString strLogString;
								strLogString.Format("FindSafestPlotInReach: %s found third-party safe haven at (%d,%d), owner=%d, embarkScore=%d",
									pUnit->getName().GetCString(), pPlot->getX(), pPlot->getY(), (int)ePlotOwner, iEmbarkScore);
								GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
							}
						}
					}
				}
			}
			
			aEmbarkList.push_back( OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft), iEmbarkScore) );
		}
		else if(bIsZeroDanger)
		{
			//if danger is zero, look at distance to closest owned city instead
			//but also factor in domain penalty and retreat direction penalty
			int iZeroScore = bIsInTerritory ? iCityDistance : iCityDistance * 2;
			if (bMovingTowardEnemy)
				iZeroScore += bImminentAttack ? 160 : 100;
			aZeroDangerList.push_back( OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft), iZeroScore) );
		}
		else if(!bWouldEmbark) //land plots with some danger
		{
			aDangerList.push_back( OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft),iScore) );
		}
	}

	//high scores are bad, we sort descending
	std::stable_sort(aCityList.begin(), aCityList.end());
	std::stable_sort(aCoverList.begin(), aCoverList.end());
	std::stable_sort(aZeroDangerList.begin(), aZeroDangerList.end());
	std::stable_sort(aDangerList.begin(), aDangerList.end());
	std::stable_sort(aEmbarkList.begin(), aEmbarkList.end());

	pair<CvPlot*, int> kSelectedMove = make_pair(static_cast<CvPlot*>(NULL), 0);
	CvPlot* pSelectedPlot = NULL;
	const char* szListName = "none";

	// Now that we've gathered up our lists of destinations, pick the most promising one
	// PRIORITY ORDER: city > cover > zeroDanger > danger > embark (last resort!)
	if (aCityList.size()>0)
	{
		kSelectedMove = aCityList.back().option;
		pSelectedPlot = kSelectedMove.first;
		szListName = "city";
	}
	else if (aCoverList.size() > 0)
	{
		kSelectedMove = aCoverList.back().option;
		pSelectedPlot = kSelectedMove.first;
		szListName = "cover";
		CvUnit* pDefender = pSelectedPlot->getBestDefender(pUnit->getOwner());
		if (pDefender && pDefender != pUnit)
		{
			//taking cover only works if the defender will not move away!
			//since we move civilians only after the combat units have moved it should be safe to pin the defender here (AI players only!)
			if (!pDefender->TurnProcessed() && !pDefender->isHuman(ISHUMAN_AI_UNITS))
			{
				TacticalAIHelpers::PerformRangedOpportunityAttack(pDefender, false);
				pDefender->PushMission(CvTypes::getMISSION_SKIP());
				pDefender->SetTurnProcessed(true);
			}
		}
	}
	else if (aZeroDangerList.size()>0)
	{
		kSelectedMove = aZeroDangerList.back().option;
		pSelectedPlot = kSelectedMove.first;
		szListName = "zeroDanger";
	}
	else if (aDangerList.size()>0)
	{
		kSelectedMove = aDangerList.back().option;
		pSelectedPlot = kSelectedMove.first;
		szListName = "danger";
	}
	else if (aEmbarkList.size()>0)
	{
		//EMBARKATION IS ABSOLUTE LAST RESORT - only if no land options exist at all
		//Pick the safest water plot (closest to our cities, fewest naval threats)
		kSelectedMove = aEmbarkList.back().option;
		pSelectedPlot = kSelectedMove.first;
		szListName = "embark(lastResort)";
		
		if (GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("FindSafestPlotInReach: %s (%d) has NO land options, forced to consider embarkation. Embark candidates=%d",
				pUnit->getName().GetCString(), pUnit->GetID(), (int)aEmbarkList.size());
			GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
		}
	}

	//Log selection for debugging embark issues
	if (GC.getLogging() && GC.getAILogging() && pSelectedPlot && pUnit->getDomainType() == DOMAIN_LAND && pSelectedPlot->isWater())
	{
		CvString strLogString;
		strLogString.Format("FindSafestPlotInReach: %s (%d) at (%d,%d) selected WATER plot (%d,%d) from %s list. Lists: city=%d cover=%d zero=%d danger=%d embark=%d",
			pUnit->getName().GetCString(), pUnit->GetID(), 
			pUnit->getX(), pUnit->getY(),
			pSelectedPlot->getX(), pSelectedPlot->getY(), szListName,
			(int)aCityList.size(), (int)aCoverList.size(), (int)aZeroDangerList.size(), (int)aDangerList.size(), (int)aEmbarkList.size());
		GET_PLAYER(pUnit->getOwner()).GetTacticalAI()->LogTacticalMessage(strLogString);
	}

	return kSelectedMove;
}

void CTacticalUnitArray::push_back(const CvTacticalUnit& unit)
{
	CheckDebugTrigger(unit.GetID());
	m_vec.push_back(unit);
}

bool TacticalAIHelpers::IsGoodPlotForStaging(CvPlayer* pPlayer, CvPlot* pCandidate, DomainTypes eDomain)
{
	if (!pPlayer || !pCandidate)
		return false;

	if (pCandidate->getBestDefender(pPlayer->GetID())!=NULL)
		return false;

	if (eDomain != NO_DOMAIN && pCandidate->getDomain() != eDomain)
		return false;

	int iCityDistance = pPlayer->GetCityDistancePathLength(pCandidate);
	if (iCityDistance>5)
		return false;

	if (pCandidate->getRouteType()!=NO_ROUTE)
		return false;

	int iFriendlyCombatUnitsAdjacent = 0;
	int iPassableNeighbors = 0;
	for (int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		CvPlot* pNeighbor = plotDirection(pCandidate->getX(), pCandidate->getY(), ((DirectionTypes)iI));
		if (!pNeighbor)
			continue;

		//don't want to provoke other players, stay away from their units
		if (pNeighbor->isNeutralUnit(pPlayer->GetID(),true,true,true) || pNeighbor->isEnemyUnit(pPlayer->GetID(),true,true,true))
			return false;

		//stay away from their borders as well
		if (pNeighbor->isOwned() && pNeighbor->getTeam() != pPlayer->getTeam())
			if (GET_PLAYER(pNeighbor->getOwner()).isMajorCiv())
				return false;

		if (pNeighbor->getBestDefender(pPlayer->GetID()) != NULL)
			iFriendlyCombatUnitsAdjacent++;

		if (eDomain == NO_DOMAIN || eDomain == pNeighbor->getDomain())
			if (!pNeighbor->isImpassable(pPlayer->getTeam()))
				iPassableNeighbors++;
	}

	//don't build a wall of units
	if (iFriendlyCombatUnitsAdjacent>3)
		return false;

	//don't move into dead ends
	if (iPassableNeighbors<3)
		return false;

	//we don't know the unit, so use a rough estimation ...
	if (pPlayer->GetPlotDanger(*pCandidate, false) > /*10*/ GD_INT_GET(NEUTRAL_HEAL_RATE))
		return false;

	return true;
}

bool TacticalAIHelpers::IsCloseToContestedBorder(CvPlayer* pPlayer, CvPlot* pPlot)
{
	bool bResult = false;

	for (int i = RING1_PLOTS; i < RING2_PLOTS; i++)
	{
		CvPlot* pTestPlot = iterateRingPlots(pPlot, i);
		if (pTestPlot && pPlayer->IsAtWarWith(pTestPlot->getOwner()) && pTestPlot->getDomain() == DOMAIN_LAND)
		{
			CvTacticalDominanceZone* pZone = pPlayer->GetTacticalAI()->GetTacticalAnalysisMap()->GetZoneByPlot(pTestPlot);
			if (pZone && pZone->GetOverallDominanceFlag() == TACTICAL_DOMINANCE_FRIENDLY)
				continue;

			bResult = true;
			break;
		}
	}

	return bResult;
}

pair<CvPlot*, int> TacticalAIHelpers::FindClosestSafePlotForHealing(CvUnit* pUnit, bool bConservative)
{
	if (!pUnit)
		return make_pair(static_cast<CvPlot*>(NULL), 0);

	//first see if the current plot is good
	int iCurrentHealRate = pUnit->ActualHealRate(pUnit->plot());
	if (pUnit->GetDanger() == 0 && iCurrentHealRate > 5 && !pUnit->isAlwaysHeal())
		return make_pair(pUnit->plot(), pUnit->getMoves());

	//check if we can outheal the damage
	if (iCurrentHealRate > 5 && iCurrentHealRate > pUnit->GetDanger() && !pUnit->isAlwaysHeal())
		return make_pair(pUnit->plot(), pUnit->getMoves());

	//doesn't get much safer than in a city
	//also garrisons should not run away!
	if (pUnit->plot()->isCity() && pUnit->getDomainType() == DOMAIN_LAND)
		return make_pair(pUnit->plot(), pUnit->getMoves());

	std::vector<OptionWithScore<pair<CvPlot*, int>>> vCandidates;
	ReachablePlots eligiblePlots = pUnit->GetAllPlotsInReachThisTurn(); //embarkation allowed for now, we sort it out below
	for (ReachablePlots::const_iterator it = eligiblePlots.begin(); it != eligiblePlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

		if (pPlot->isEnemyUnit(pUnit->getOwner(), true, true))
			continue;

		bool bPillage = (it->iMovesLeft > 0) && pUnit->shouldPillage(pPlot, false);
		//don't check movement, don't need to heal right now
		if (pUnit->getDomainType() == DOMAIN_LAND)
		{
			if (pUnit->ActualHealRate(pPlot, false) == 0)
				continue;

			//don't mess with (ranged) garrisons if enemies are around
			CvCity* pCity = pPlot->getPlotCity();
			if (pCity && pCity->HasGarrison() && pCity->isUnderSiege() && pCity->GetGarrisonedUnit()->IsCanAttackRanged() && pCity->GetGarrisonedUnit()->getDomainType() == DOMAIN_LAND)
				continue;
		}
		else
		{
			//naval units usually must pillage to heal ...
			if (!bPillage && pUnit->ActualHealRate(pPlot, false) == 0)
				continue;
		}

		//can we stay there?
		if (!pUnit->canMoveInto(*pPlot, CvUnit::MOVEFLAG_DESTINATION))
		{
			if (pUnit->canMoveInto(*pPlot, CvUnit::MOVEFLAG_DESTINATION | CvUnit::MOVEFLAG_IGNORE_STACKING_SELF))
			{
				//todo: we should maybe choose the target plot based on whether we can make a good swap?
				if (!pUnit->CanPushOutUnitHere(*pPlot))
					continue;
			}
			else
				continue;
		}

		int iDanger = min(pUnit->GetDanger(pPlot),10000); //handle INT_MAX for civilians
		int iHealRate = pUnit->ActualHealRate(pPlot, false);

		// a heal now is about as good as two heals next turn
		if (pPlot == pUnit->plot() && !pUnit->hasMoved() && !pUnit->isAlwaysHeal())
			iHealRate *= 2;

		//sometimes we want to ignore pillage health, it's a one-time effect and may lead into dead ends
		if (bPillage && !bConservative)
		{
			if (!pUnit->hasFreePillageMove())
				iHealRate = max(iHealRate, /*25*/ GD_INT_GET(PILLAGE_HEAL_AMOUNT));
			else
				iHealRate += /*25*/ GD_INT_GET(PILLAGE_HEAL_AMOUNT);
		}

		//make up a score function
		//don't try to go to heal in a plot where we will slowly die
		int iScore = iHealRate - iDanger;
		//is this safe enough?
		if (iScore > 0)
		{
			//tiebreaker
			iScore -= plotDistance(pPlot->getX(), pPlot->getY(), pUnit->getX(), pUnit->getY()) * 2 - GET_PLAYER(pUnit->getOwner()).GetCityDistancePathLength(pPlot);
			vCandidates.push_back(OptionWithScore<pair<CvPlot*, int>>(make_pair(pPlot, it->iMovesLeft), iScore));
		}
	}

	if (!vCandidates.empty())
	{
		std::stable_sort(vCandidates.begin(), vCandidates.end());
		//how to tell whether it's safe enough?
		return vCandidates.back().option;
	}

	return make_pair(static_cast<CvPlot*>(NULL), 0);
}

std::vector<CvPlot*> TacticalAIHelpers::GetPlotsForRangedAttack(const CvPlot* pTarget, const CvUnit* pUnit, int iRange, bool bCheckOccupied)
{
	std::vector<CvPlot*> vPlots;

	if (!pTarget || !pUnit)
		return vPlots;

	// Aircraft and special promotions make us ignore LOS
	bool bIgnoreLOS = pUnit->IsRangeAttackIgnoreLOS() || pUnit->getDomainType()==DOMAIN_AIR;
	// Can only bombard in domain? (used for Subs' torpedo attack)
	bool bOnlyInDomain = pUnit->getUnitInfo().IsRangeAttackOnlyInDomain();

	const vector<CvPlot*>& vCandidates = GC.getMap().GetPlotsAtRangeX(pTarget, iRange, false, !bIgnoreLOS);

	//filter and take only the half closer to origin
	CvPlot* pRefPlot = pUnit->plot();
	if(pRefPlot == NULL)
		return vPlots;

	int iRefDist = plotDistance(*pRefPlot,*pTarget);
	std::vector<SPlotWithScore> vIntermediate;
	for (size_t i=0; i<vCandidates.size(); i++)
	{
		if(vCandidates[i] == NULL)
			continue;

		int iDistance = plotDistance(*pRefPlot,*(vCandidates[i]));
		if (iDistance>iRefDist && iRefDist>iRange)
			continue;

		if (!vCandidates[i]->isRevealed(pUnit->getTeam()))
			continue;

		//this concerns not only embarked units but also ships in harbor!
		if (!pUnit->isNativeDomain(vCandidates[i]))
			continue;

		if (bCheckOccupied && vCandidates[i]!=pRefPlot && vCandidates[i]->getBestDefender(NO_PLAYER))
			continue;

		if (bOnlyInDomain)
		{
			//subs can only attack within their (water) area or adjacent cities (VP only)
			if (pRefPlot->getLandmass() != vCandidates[i]->getLandmass())
			{
				if (!MOD_BALANCE_VP)
					continue;

				if (!vCandidates[i]->isCity())
					continue;

				if (!vCandidates[i]->getPlotCity()->HasAccessToLandmassOrOcean(pRefPlot->getLandmass()))
					continue;
			}
		}

		vIntermediate.push_back( SPlotWithScore(vCandidates[i],iDistance) );
	}

	//sort by increasing distance
	std::stable_sort(vIntermediate.begin(), vIntermediate.end());

	for (size_t i=0; i<vIntermediate.size(); i++)
		vPlots.push_back(vIntermediate[i].pPlot);

	return vPlots;
}

//helper function for city threat calculation
int TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(const CvCity* pCity, const CvUnit* pAttacker, const CvPlot* pAttackerPlot, int& iAttackerDamage,
	int& iGarrisonDamage, bool bIgnoreUnitAdjacencyBoni, int iExtraSelfDamage, int iExtraCityDamage, int iExtraGarrisonDamage, bool bQuickAndDirty, bool bOverrideGarrison, const CvUnit* pGarrisonOverride)
{
	if (!pAttacker || !pCity || pAttacker->isDelayedDeath() || pAttacker->IsDead())
		return 0;
		
	int iDamage = 0;
	const CvUnit* pGarrison = bOverrideGarrison ? pGarrisonOverride : pCity->GetGarrisonedUnit();
	int iGarrisonMaxHP = (pGarrison != NULL && pGarrison->GetMaxHitPoints() > pGarrison->getDamage() + iExtraGarrisonDamage) ? pGarrison->GetMaxHitPoints() : 0;
	if (pAttacker->IsCanAttackRanged())
	{
		if (pAttacker->getDomainType() == DOMAIN_AIR)
			iDamage = pAttacker->GetAirCombatDamage(NULL, pCity, iGarrisonMaxHP, iGarrisonDamage, false, iExtraSelfDamage, iExtraCityDamage);
		else
			iDamage = pAttacker->GetRangeCombatDamage(NULL, pCity, iGarrisonMaxHP, iGarrisonDamage, false, iExtraSelfDamage, iExtraCityDamage, NULL, pAttackerPlot, bIgnoreUnitAdjacencyBoni, bQuickAndDirty);

		iAttackerDamage = 0; //what about interceptions?
	}
	else
	{
		//just assume the unit can attack from its current location - modifiers might be different, but thats acceptable
		iDamage = pAttacker->getMeleeCombatDamageCity(
			pAttacker->GetMaxAttackStrength(pAttackerPlot, pCity->plot(), NULL, bIgnoreUnitAdjacencyBoni, bQuickAndDirty),
			pCity, //not affected by assumed extra damage
			iAttackerDamage, iGarrisonMaxHP, iGarrisonDamage, false, iExtraSelfDamage, iExtraCityDamage, pGarrisonOverride);
	}

	return iDamage;
}

//helper function for unit threat calculation
int TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(const CvUnit* pDefender, const CvUnit* pAttacker, 
				const CvPlot* pDefenderPlot, const CvPlot* pAttackerPlot, int& iAttackerDamage, 
				bool bIgnoreUnitAdjacencyBoni, int iExtraSelfDamage, int iExtraDefenderDamage, bool bQuickAndDirty)
{
	if (!pAttacker || !pDefender || pDefender->isDelayedDeath() || pDefender->IsDead() || pAttacker->isDelayedDeath() || pAttacker->IsDead())
		return 0;
		
	int iDamage = 0;
	int iUnusedReferenceVariable = 0;
	if (pAttacker->IsCanAttackRanged())
	{
		if (pAttacker->getDomainType() == DOMAIN_AIR)
		{
			// ignore interception for quick and dirty mode ...
			CvUnit* pInterceptor = bQuickAndDirty ? NULL : pDefenderPlot->GetBestInterceptor(pAttacker->getOwner(), pAttacker, false, true);
			// assume interception is successful - do this before the actual attack
			iAttackerDamage = pInterceptor ? pInterceptor->GetInterceptionDamage(pAttacker, false, pDefenderPlot) : 0;

			if (pAttacker->GetCurrHitPoints() - iAttackerDamage > 0)
			{
				iDamage += pAttacker->GetAirCombatDamage(pDefender, NULL, 0, iUnusedReferenceVariable, false, iExtraSelfDamage, iExtraDefenderDamage, pDefenderPlot, pAttackerPlot, bQuickAndDirty);
				iAttackerDamage += pDefender->GetAirStrikeDefenseDamage(pAttacker, false, pDefenderPlot);
			}
		}
		else
		{
			// Naval units can't fire from cities
			const CvPlot* pFromPlot = pAttackerPlot ? pAttackerPlot : pAttacker->plot();
			if (pAttacker->getDomainType() == DOMAIN_SEA && pFromPlot->isCity())
				return 0;

			iDamage += pAttacker->GetRangeCombatDamage(pDefender, NULL, 0, iUnusedReferenceVariable, false, iExtraSelfDamage, iExtraDefenderDamage,
							pDefenderPlot, pAttackerPlot, bIgnoreUnitAdjacencyBoni, bQuickAndDirty);
			iDamage += (pAttacker->hasMoved() || pAttacker->plot() != pAttackerPlot) ? 0 : pAttacker->GetTileDamageIfNotMoved();
			iAttackerDamage = 0;
		}
	}
	else
	{
		//for melee attack check whether the attacker can actually go where the defender is
		//the defender might only be there hypothetically - so an empty plot is a valid target 
		if (pDefenderPlot && !pAttacker->canMoveOrAttackInto(*pDefenderPlot))
			return 0;

		if (pAttacker->isRangedSupportFire())
			iDamage += pAttacker->GetRangeCombatDamage(pDefender, NULL, 0, iUnusedReferenceVariable, false, iExtraSelfDamage, iExtraDefenderDamage,
							pDefenderPlot, pAttackerPlot, bIgnoreUnitAdjacencyBoni, bQuickAndDirty);

		// no melee attack if the RangedSupportFire has killed the defender
		if (pDefender->GetCurrHitPoints() > iDamage + iExtraDefenderDamage)
		{
			int iAttackerStrength = pAttacker->GetMaxAttackStrength(pAttackerPlot, pDefenderPlot, pDefender, bIgnoreUnitAdjacencyBoni, bQuickAndDirty, iExtraSelfDamage, iExtraDefenderDamage + iDamage);
			//do not override defender flanking/general bonus (it is known during combat simulation)
			int iDefenderStrength = pDefender->GetMaxDefenseStrength(pDefenderPlot, pAttacker, pAttackerPlot, false, bQuickAndDirty, iExtraDefenderDamage + iDamage);

			//just assume the unit can attack from its current location - modifiers might be different, but thats acceptable
			iDamage += pAttacker->getMeleeCombatDamage(
				iAttackerStrength,
				iDefenderStrength,
				iAttackerDamage,
				false, pDefender,
				iExtraSelfDamage,
				iExtraDefenderDamage + iDamage);

			iDamage += (pAttacker->hasMoved() || pAttacker->plot() != pAttackerPlot) ? 0 : pAttacker->GetTileDamageIfNotMoved();
		}
	}

	return iDamage;
}

bool TacticalAIHelpers::KillLoneEnemyIfPossible(CvUnit* pOurUnit, CvUnit* pEnemyUnit)
{
	if (!pOurUnit || !pEnemyUnit || pEnemyUnit->isDelayedDeath())
		return false;

	//aircraft are different
	if (pOurUnit->getDomainType()==DOMAIN_AIR || pEnemyUnit->getDomainType()==DOMAIN_AIR)
		return false;

	//see how the attack would go
	int iDamageDealt = 0;
	int iDamageReceived = 0;
	iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pEnemyUnit, pOurUnit, pEnemyUnit->plot(), pOurUnit->plot(), iDamageReceived);

	//is it worth it? (take into account some randomness ...)
	if ( iDamageDealt-3 > pEnemyUnit->GetCurrHitPoints() && pOurUnit->GetCurrHitPoints()-iDamageReceived > 23 )
	{
		if (pOurUnit->IsCanAttackRanged())
		{
			//can we attack directly
			if (pOurUnit->canRangeStrikeAt(pEnemyUnit->getX(),pEnemyUnit->getY()))
			{
				pOurUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(),pEnemyUnit->getX(),pEnemyUnit->getY());
				return true;
			}
			else if (pOurUnit->canRangeStrike())
			{
				//need to move and shoot
				bool bIgnoreLOS = pOurUnit->IsRangeAttackIgnoreLOS();
				const vector<CvPlot*>& vAttackPlots = GC.getMap().GetPlotsAtRangeX(pEnemyUnit->plot(), pOurUnit->GetRange(), false, !bIgnoreLOS);
				for (std::vector<CvPlot*>::const_iterator it = vAttackPlots.begin(); it != vAttackPlots.end(); ++it)
				{
					if (pOurUnit->TurnsToReachTarget(*it, CvUnit::MOVEFLAG_TURN_END_IS_NEXT_TURN, 1) == 0 && pOurUnit->canEverRangeStrikeAt(pEnemyUnit->getX(), pEnemyUnit->getY(), *it, false))
					{
						pOurUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), (*it)->getX(), (*it)->getY(), CvUnit::MOVEFLAG_IGNORE_DANGER);
						//sometimes the unit takes an unexpected path
						if (pOurUnit->atPlot(**it))
							pOurUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pEnemyUnit->getX(), pEnemyUnit->getY());
						else
							OutputDebugString("pathfinding issue ...\n");
						return true;
					}
				}
			}
		}
		else //melee
		{
			if (pOurUnit->TurnsToReachTarget(pEnemyUnit->plot(),0,1)==0)
			{
				pOurUnit->PushMission(CvTypes::getMISSION_MOVE_TO(),pEnemyUnit->getX(),pEnemyUnit->getY());
				return true;
			}
		}
	}

	return false;
}

bool TacticalAIHelpers::IsSuicideMeleeAttack(const CvUnit * pAttacker, CvPlot * pTarget)
{
	if (!pAttacker || !pTarget)
		return false;

	// Special handling for air attacks - they can't do melee but check if suicide via interception
	if (pAttacker->getDomainType() == DOMAIN_AIR)
	{
		if (!pAttacker->IsCanAttackRanged())
			return false; // Not an air strike

		// Air units are suicide if they'd likely die to interception
		if (pTarget->isCity())
		{
			int iInterceptionDamage = 0;
			CvCity* pCity = pTarget->getPlotCity();
			if (pCity && pCity->getOwner() != pAttacker->getOwner())
			{
				iInterceptionDamage = pCity->GetAirStrikeDefenseDamage(pAttacker, false);
				if (iInterceptionDamage >= pAttacker->GetCurrHitPoints())
					return true; // Would be killed by interception
			}
		}
		else
		{
			CvUnit* pDefender = pTarget->getBestDefender(pAttacker->getOwner());
			if (pDefender && pDefender->canIntercept())
			{
				int iInterceptionDamage = pDefender->GetAirStrikeDefenseDamage(pAttacker, false);
				if (iInterceptionDamage >= pAttacker->GetCurrHitPoints())
					return true; // Would be killed by interception
			}
		}
		return false; // Air unit can safely attack
	}

	if (pAttacker->IsCanAttackRanged())
		return false;

	int iDamageReceived = 0;
	int iGarrisonDamage = 0;

	//if we're not adjacent we don't know the plot the attacker will use in the end
	CvPlot* pAttackerPlot = NULL;
	if (pAttacker->plot()->isAdjacent(pTarget))
		pAttackerPlot = pAttacker->plot();

	//unit attack or city attack?
	if (pTarget->isCity())
	{
		TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pTarget->getPlotCity(), pAttacker, pAttackerPlot, iDamageReceived, iGarrisonDamage);
	}
	else
	{
		CvUnit* pDefender = pTarget->getBestDefender(NO_PLAYER, pAttacker->getOwner(), pAttacker);
		if (pDefender)
			TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pDefender, pAttacker, pTarget, pAttackerPlot, iDamageReceived);
		else
			return false;
	}

	//add some margin for randomness
	return (iDamageReceived+3 >= pAttacker->GetCurrHitPoints());
}

bool TacticalAIHelpers::CanKillTarget(const CvUnit* pAttacker, CvPlot* pTarget)
{
	if (!pAttacker || !pTarget)
		return false;

	CvCity* pTargetCity = pTarget->getPlotCity();
	if (pTargetCity)
	{
		if (!pAttacker->isEnemy(pTargetCity->getTeam()))
			return false;

		//shortcut
		if (pTargetCity->isInDangerOfFalling(true))
			return true;

		//see how an attack would go just in case
		int iDamageDealt = 0;
		int iDamageReceived = 0;
		int iGarrisonDamage = 0;
		iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pTargetCity, pAttacker, pAttacker->plot(), iDamageReceived, iGarrisonDamage, true, 0, true);

		int iNumMelee = pTarget->GetNumFriendlyUnitsAdjacent(pAttacker->getTeam(), NO_DOMAIN, false, pAttacker);
		if (iNumMelee > 0)
		{
			// assume each melee unit does 1/4 of our damage
			iDamageDealt += iNumMelee * iDamageDealt / 4;
		}

		//ranged units can't capture themselves so we check for an adjacent melee unit
		if (pAttacker->IsCanAttackRanged())
		{
			if (iNumMelee == 0)
				return false;
		}
		else
		{
			//some melee units cannot capture either
			if (pAttacker->isNoCapture() && iNumMelee == 0)
				return false;
		}

		return iDamageDealt + pTargetCity->getDamage() >= pTargetCity->GetMaxHitPoints();
	}

	CvUnit* pDefender = pTarget->getVisibleEnemyDefender(pAttacker->getOwner());
	if (pDefender)
	{
		//see how the attack would go
		int iDamageDealt = 0;
		int iDamageReceived = 0;
		iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pDefender, pAttacker, pDefender->plot(), pAttacker->plot(), iDamageReceived, true, 0, true);
		//no suicide attacks ...
		return iDamageDealt >= pDefender->GetCurrHitPoints() && (iDamageReceived == 0 || iDamageReceived < pAttacker->GetCurrHitPoints()-3);
	}

	return false;
}

vector<pair<CvPlot*, bool>> TacticalAIHelpers::GetTargetsInRange(const CvUnit * pUnit, bool bMustBeAbleToKill, bool bIncludeCivilians)
{
	vector<pair<CvPlot*, bool>> result;
	if (!pUnit)
		return result;

	ReachablePlots reachablePlots = pUnit->GetAllPlotsInReachThisTurn(true, true, false);
	for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

		//only melee attacks here - ranged attacks are checked below
		bool bMilitaryTarget = (pPlot->isEnemyCity(*pUnit) && !pUnit->isNoCapture()) || pPlot->isEnemyUnit(pUnit->getOwner(), true, true);
		if (bMilitaryTarget && !pUnit->IsCanAttackRanged())
		{
			bool bCanKill = CanKillTarget(pUnit,pPlot);
			if (bMustBeAbleToKill && !bCanKill)
				continue;

			result.push_back( make_pair(pPlot,bCanKill) );
		}
		else if (bIncludeCivilians && pPlot->isEnemyUnit(pUnit->getOwner(), false, true))
			//we know the civilian is unescorted!
			result.push_back(make_pair(pPlot, true));
		else if (pPlot->getImprovementType() == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
			//unoccupied barb camp?
			result.push_back(make_pair(pPlot, true));
	}

	if (pUnit->IsCanAttackRanged())
	{
		//for ranged every tile we can enter with movement left is a base for attack
		std::set<int> attackableTiles = TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(pUnit,reachablePlots,true,false);
		for (std::set<int>::const_iterator attackTile=attackableTiles.begin(); attackTile!=attackableTiles.end(); ++attackTile)
		{
			CvPlot* pAttackTile = GC.getMap().plotByIndexUnchecked(*attackTile);
			bool bCanKill = CanKillTarget(pUnit,pAttackTile);

			if (bMustBeAbleToKill && !bCanKill)
				continue;

			result.push_back(make_pair(pAttackTile, bCanKill));
		}
	}

	return result;
}

pair<int, int> TacticalAIHelpers::EstimateLocalUnitPower(const ReachablePlots& plotsToCheck, TeamTypes eTeamA, TeamTypes eTeamB, bool bMustBeVisibleToBoth)
{
	if (plotsToCheck.empty())
		return make_pair(0, 0);

	int iTeamAPower = 0;
	int iTeamBPower = 0;

	for (ReachablePlots::const_iterator it = plotsToCheck.begin(); it != plotsToCheck.end(); ++it)
	{
		CvPlot* pLoopPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		ASSERT(pLoopPlot != NULL, "plotByIndexUnchecked returned null - invalid plot index");

		if (bMustBeVisibleToBoth && !(pLoopPlot->isVisible(eTeamA) && pLoopPlot->isVisible(eTeamB)))
			continue;

		// If there are Units here, loop through them
		if (pLoopPlot->getNumUnits() > 0)
		{
			IDInfo* pUnitNode = pLoopPlot->headUnitNode();
			while (pUnitNode != NULL)
			{
				CvUnit* pLoopUnit = ::GetPlayerUnit(*pUnitNode);
				pUnitNode = pLoopPlot->nextUnitNode(pUnitNode);

				// Is a combat unit
				if (pLoopUnit && (pLoopUnit->IsCombatUnit() || pLoopUnit->getDomainType() == DOMAIN_AIR))
				{
					int iScale = pLoopUnit->isNativeDomain(pLoopPlot) ? 1 : 2;

					if (pLoopUnit->getTeam() == eTeamA)
						iTeamAPower += pLoopUnit->GetPower() / iScale;
					if (pLoopUnit->getTeam() == eTeamB)
						iTeamBPower += pLoopUnit->GetPower() / iScale;
				}
			}
		}
	}

	return pair<int, int>(iTeamAPower,iTeamBPower);
}

//could we see additional plot when the unit moves to the test plot?
int TacticalAIHelpers::CountAdditionallyVisiblePlots(CvUnit * pUnit, CvPlot * pTestPlot)
{
	if (!pUnit || !pTestPlot)
		return 0;

	int iCount = 0;
	for (int iRange = 2; iRange <= pUnit->visibilityRange(); iRange++)
	{
		const vector<CvPlot*>& vPlots = GC.getMap().GetPlotsAtRangeX(pTestPlot, iRange, true, true);
		for (size_t i = 0; i < vPlots.size(); i++)
			if (vPlots[i] && !vPlots[i]->isVisible(pUnit->getTeam())) //we already know that would have line of sight
				iCount++;
	}
	
	return iCount;
}

//convenience
bool isCombatUnit(eUnitMovementStrategy eMoveType) { return eMoveType == MS_FIRSTLINE || eMoveType == MS_SECONDLINE || eMoveType == MS_THIRDLINE; }
bool isEmbarkedUnit(eUnitMovementStrategy eMoveType) { return eMoveType == MS_EMBARKED; }
bool isSupportUnit(eUnitMovementStrategy eMoveType) { return eMoveType == MS_SUPPORT; }

//Unbelievable bad logic but taken like this from CvUnitCombat
bool AttackEndsTurn(const CvUnit* pUnit, int iNumAttacksLeft)
{
	return !pUnit->canMoveAfterAttacking() && !pUnit->isRangedSupportFire() && iNumAttacksLeft<2;
}

static int NumAttacksForUnit(int iMovesLeft, int iMaxAttacks, bool bFreeAttackMoves)
{
	return bFreeAttackMoves ? max(0, iMaxAttacks) : max(0, min( (iMovesLeft+GD_INT_GET(MOVE_DENOMINATOR)-1)/GD_INT_GET(MOVE_DENOMINATOR), iMaxAttacks ));
}

CvTacticalPlot::eTactPlotDomain DomainForUnit(const CvUnit* pUnit)
{
	if (!pUnit)
		return CvTacticalPlot::TD_BOTH;

	switch (pUnit->getDomainType())
	{
	case DOMAIN_LAND:
		return CvTacticalPlot::TD_LAND;
	case DOMAIN_SEA:
		return CvTacticalPlot::TD_SEA;
	default:
		return CvTacticalPlot::TD_BOTH;
	}
}

void CDangerCache::clear()
{
	// Memory optimization: periodically release capacity to prevent unbounded growth
	// In long games (32-bit), these caches can accumulate significant memory
	if (GC.getGame().getGameTurn() % 10 == 0 || dangerStats.capacity() > dangerStats.size() * 2)
	{
		// Force capacity release using swap idiom
		vector<pair<int, vector<SDefendStats>>>().swap(dangerStats);
	}
	else
	{
		dangerStats.clear();
	}
}

void CDangerCache::storeDanger(int iDefenderId, int iDefenderPlot, int iPrevDamage, const SUnitIDValueContainer& unitDamageDealt, int iDanger)
{
	DefendKey key;
	key.iDefenderId = iDefenderId;
	key.iPlotId = iDefenderPlot;
	key.iPrevDamage = iPrevDamage;
	key.iDamageHash = unitDamageDealt.GetHash();

	dangerStats[key] = iDanger;
}

bool CDangerCache::findDanger(int iDefenderId, int iDefenderPlot, int iPrevDamage, const SUnitIDValueContainer& unitDamageDealt, int& iDanger) const
{
	DefendKey key;
	key.iDefenderId = iDefenderId;
	key.iPlotId = iDefenderPlot;
	key.iPrevDamage = iPrevDamage;
	key.iDamageHash = unitDamageDealt.GetHash();

	std::tr1::unordered_map<DefendKey, int, DefendKeyHash>::const_iterator it =
		dangerStats.find(key);

	if (it != dangerStats.end())
	{
		iDanger = it->second;
		gDangerCacheHit++;
		return true;
	}

	iDanger = 0;
	gDangerCacheMiss++;
	return false;
}

void CAttackCache::clear()
{
	// Memory optimization: periodically release capacity to prevent unbounded growth
	// In long games (32-bit), these caches can accumulate significant memory
	if (GC.getGame().getGameTurn() % 10 == 0 || attackStats.capacity() > attackStats.size() * 2)
	{
		// Force capacity release using swap idiom
		vector<pair<int, vector<SAttackStats>>>().swap(attackStats);
	}
	else
	{
		attackStats.clear();
	}
}

void CAttackCache::storeAttack(int iAttackerId, int iAttackerPlot, int iDefenderId, int iGarrisonId, int iPrevSelfDamage, int iPrevUnitDamage, int iPrevCityDamage, int iUnitDamageDealt, int iCityDamageDealt, int iDamageTaken)
{
	AttackKey key;
	key.iAttackerId = iAttackerId;
	key.iAttackerPlot = iAttackerPlot;
	key.iDefenderId = iDefenderId;
	key.iGarrisonId = iGarrisonId;
	key.iPrevSelfDamage = iPrevSelfDamage;
	key.iPrevUnitDamage = iPrevUnitDamage;
	key.iPrevCityDamage = iPrevCityDamage;

	std::tr1::unordered_map<AttackKey, vector<int>, AttackKeyHash>::iterator it =
		attackStats.find(key);

	if (it != attackStats.end())
	{
		it->second[0] = iUnitDamageDealt;
		it->second[1] = iCityDamageDealt;
		it->second[2] = iDamageTaken;
	}
	else
	{
		vector<int> newValue(3);
		newValue[0] = iUnitDamageDealt;
		newValue[1] = iCityDamageDealt;
		newValue[2] = iDamageTaken;
		attackStats[key] = newValue;
	}
}

bool CAttackCache::findAttack(int iAttackerId, int iAttackerPlot, int iDefenderId, int iGarrisonId, int iPrevSelfDamage, int iPrevUnitDamage, int iPrevCityDamage, int& iUnitDamageDealt, int& iCityDamageDealt, int& iDamageTaken) const
{
	AttackKey key;
	key.iAttackerId = iAttackerId;
	key.iAttackerPlot = iAttackerPlot;
	key.iDefenderId = iDefenderId;
	key.iGarrisonId = iGarrisonId;
	key.iPrevSelfDamage = iPrevSelfDamage;
	key.iPrevUnitDamage = iPrevUnitDamage;
	key.iPrevCityDamage = iPrevCityDamage;

	std::tr1::unordered_map<AttackKey, vector<int>, AttackKeyHash>::const_iterator it =
		attackStats.find(key);

	if (it != attackStats.end())
	{
		iUnitDamageDealt = it->second[0];
		iCityDamageDealt = it->second[1];
		iDamageTaken = it->second[2];
		gAttackCacheMiss++;
		return true;
	}

	iUnitDamageDealt = 0;
	iCityDamageDealt = 0;
	iDamageTaken = 0;
	gAttackCacheMiss++;
	return false;
}

const ReachablePlots& CvBasePosition::getReachablePlotsForUnit(const SUnitStats& unit) const
{
	static ReachablePlots emptyResult;

	SPathFinderStartPos key(unit, freedPlots.read());
	TCachedMovePlots::const_iterator result = gReachablePlotsLookup.find(key);
	if (result != gReachablePlotsLookup.end())
		return result->second;

	return emptyResult;
}

//do we have to stop now
bool CvBasePosition::isExhausted() const
{
	return availableUnits.read().empty();
}

bool CvBasePosition::unitHasAssignmentOfType(int iUnitID, eUnitAssignmentType assignmentType) const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (size_t i = nFirstInterestingAssignment; i < assignedMoves_r.size(); i++)
	{
		const STacticalAssignment& a = assignedMoves_r[i];
		if (a.iUnitID == iUnitID && a.eAssignmentType == assignmentType)
			return true;
	}

	return false;
}

bool CvBasePosition::plotHasAssignmentOfType(int iToPlotIndex, eUnitAssignmentType assignmentType) const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (size_t i = nFirstInterestingAssignment; i < assignedMoves_r.size(); i++)
	{
		const STacticalAssignment& a = assignedMoves_r[i];
		if (a.iToPlotIndex == iToPlotIndex && a.eAssignmentType == assignmentType)
			return true;
	}

	return false;
}

bool CvBasePosition::lastAssignmentIsAfterRestart(int iUnitID) const
{
	bool bHaveRestart = false;
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (size_t i = nFirstInterestingAssignment; i < assignedMoves_r.size(); i++)
	{
		const STacticalAssignment& a = assignedMoves_r[i];
		if (a.eAssignmentType == A_RESTART)
		{
			bHaveRestart = true;
			continue;
		}

		if (bHaveRestart && a.iUnitID == iUnitID && a.eAssignmentType != A_FINISH)
			return true;
	}

	return false;
}

const SUnitStats* CvBasePosition::getAvailableUnitStats(int iUnitID) const
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	for (vector<SUnitStats>::const_iterator it = availableUnits_r.begin(); it != availableUnits_r.end(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	return NULL;
}

const SUnitStats* CvBasePosition::GetUnitStats(int iUnitID) const
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	for (vector<SUnitStats>::const_iterator it = availableUnits_r.begin(); it != availableUnits_r.end(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	const vector<SUnitStats>& notQuiteFinishedUnits_r = notQuiteFinishedUnits.read();
	for (vector<SUnitStats>::const_iterator it = notQuiteFinishedUnits_r.begin(); it != notQuiteFinishedUnits_r.end(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	const vector<SUnitStats>& finishedUnits_r = finishedUnits.read();
	for (vector<SUnitStats>::const_iterator it = finishedUnits_r.begin(); it != finishedUnits_r.end(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	return NULL;
}

const STacticalAssignment* CvBasePosition::getInitialAssignment(int iUnitID) const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (vector<STacticalAssignment>::const_iterator it = assignedMoves_r.begin(); it != assignedMoves_r.end(); ++it)
		if (it->iUnitID == iUnitID && it->eAssignmentType == A_INITIAL)
			return &(*it);

	return NULL;
}

STacticalAssignment* CvBasePosition::getInitialAssignmentMutable(int iUnitID)
{
	vector<STacticalAssignment>& assignedMoves_w = assignedMoves.write();
	for (vector<STacticalAssignment>::iterator it = assignedMoves_w.begin(); it != assignedMoves_w.end(); ++it)
		if (it->iUnitID == iUnitID && it->eAssignmentType == A_INITIAL)
			return &(*it);

	return NULL;
}

const STacticalAssignment* CvBasePosition::getLatestMoveAssignment(int iUnitID) const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (size_t i = assignedMoves_r.size() - 1; i >= nFirstInterestingAssignment; i--)
		if (assignedMoves_r[i].iUnitID == iUnitID)
		{
			eUnitAssignmentType eAssignment = assignedMoves_r[i].eAssignmentType;
			if (eAssignment == A_MOVE || eAssignment == A_MOVE_SWAP || eAssignment == A_CAPTURE || eAssignment == A_MOVE_FORCED
				|| eAssignment == A_MOVE_SWAP_REVERSE || eAssignment == A_MOVE_DOUBLE || eAssignment == A_MELEEKILL)
				return &(assignedMoves_r[i]);
		}

	return NULL;
}

const STacticalAssignment* CvBasePosition::getLatestAssignment(int iUnitID) const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	for (vector<STacticalAssignment>::const_reverse_iterator it = assignedMoves_r.rbegin(); it != assignedMoves_r.rend(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	return NULL;
}

STacticalAssignment* CvBasePosition::getLatestAssignmentMutable(int iUnitID)
{
	vector<STacticalAssignment>& assignedMoves_w = assignedMoves.write();
	for (vector<STacticalAssignment>::reverse_iterator it = assignedMoves_w.rbegin(); it != assignedMoves_w.rend(); ++it)
		if (it->iUnitID == iUnitID)
			return &(*it);

	return NULL;
}

//try to detect simple permutations in the unit assigments which result in equivalent results
static bool positionIsEquivalent(const CvBasePosition* ref, const CvBasePosition* other)
{
	//self comparison is false by definition!
	if (ref == other)
		return false;

	//"ref" is the new position we are evaluating. the "other" may already have finish moves tacked on, so size may be larger!
	if (ref->GetNumAssignments() > other->GetNumAssignments())
		return false;

	//now check the scores (ignoring any extra moves in other)
	int iRefScore = 0;
	int iOtherScore = 0;
	for (size_t i = ref->getFirstInterestingAssignment(); i < ref->GetNumAssignments(); i++)
	{
		iRefScore += ref->GetAssignment(i).Score();
		iOtherScore += other->GetAssignment(i).Score();
	}
	if (iRefScore != iOtherScore)
		return false;

	//now check for simple (A.B -> B.A) and less simple (A.B.C -> C.A.B | B.C.A), (A.B.C.D -> D.A.B.C | C.D.A.B | B.C.D.A ) permutations
	size_t A = INT_MAX;
	size_t B = INT_MAX;
	size_t C = INT_MAX;
	size_t D = INT_MAX;
	bool mismatch = false;
	//the "other" may have more moves assigned but they should all be of type FINISH ...
	//for performance do the iteration in reverse; we expect the differences at the end
	for (size_t i = ref->GetNumAssignments() - 1; i >= ref->getFirstInterestingAssignment(); i--)
	{
		//ignore matching elements
		if (ref->GetAssignment(i) == other->GetAssignment(i))
			continue;

		//remember where the differences occurred
		if (A == INT_MAX)
			A = i;
		else if (B == INT_MAX)
			B = i;
		else if (C == INT_MAX)
			C = i;
		else if (D == INT_MAX)
			D = i;

		//if we found two differences, check if the elements are flipped
		if (A != INT_MAX && B != INT_MAX)
		{
			//simple flip?
			if (C == INT_MAX)
			{
				if (ref->GetAssignment(A) == other->GetAssignment(B) && ref->GetAssignment(B) == other->GetAssignment(A))
				{
					//go on checking
					A = INT_MAX;
					B = INT_MAX;
				}
				else
				{
					//check for a three-element permutation before giving up
				}
			}
			else //C != INT_MAX
			{
				if (D == INT_MAX)
				{
					bool CAB = ref->GetAssignment(A) == other->GetAssignment(C) &&
						ref->GetAssignment(B) == other->GetAssignment(A) &&
						ref->GetAssignment(C) == other->GetAssignment(B);
					bool BCA = ref->GetAssignment(A) == other->GetAssignment(B) &&
						ref->GetAssignment(B) == other->GetAssignment(C) &&
						ref->GetAssignment(C) == other->GetAssignment(A);

					if (CAB || BCA)
					{
						//go on checking
						A = INT_MAX;
						B = INT_MAX;
						C = INT_MAX;
					}
					else
					{
						//check for a four-element permutation before giving up
					}
				}
				else //D != INT_MAX
				{
					bool DABC = ref->GetAssignment(A) == other->GetAssignment(D) &&
						ref->GetAssignment(B) == other->GetAssignment(A) &&
						ref->GetAssignment(C) == other->GetAssignment(B) &&
						ref->GetAssignment(D) == other->GetAssignment(C);
					bool CDAB = ref->GetAssignment(A) == other->GetAssignment(C) &&
						ref->GetAssignment(B) == other->GetAssignment(D) &&
						ref->GetAssignment(C) == other->GetAssignment(A) &&
						ref->GetAssignment(D) == other->GetAssignment(B);
					bool BCDA = ref->GetAssignment(A) == other->GetAssignment(B) &&
						ref->GetAssignment(B) == other->GetAssignment(C) &&
						ref->GetAssignment(C) == other->GetAssignment(D) &&
						ref->GetAssignment(D) == other->GetAssignment(A);

					if (DABC || CDAB || BCDA)
					{
						//go on checking
						A = INT_MAX;
						B = INT_MAX;
						C = INT_MAX;
						D = INT_MAX;
					}
					else
					{
						//real difference or more complex permutations
						mismatch = true;
						break;
					}
				}
			}
		}
	}

	//gotcha - there might be an "unfinished" mismatch!
	if (A != INT_MAX)
		mismatch = true;

	if (mismatch)
	{
		giDifferentPos++;
		return false;
	}
	else
	{
		giEquivalentPos++;
		return true;
	}
}

void CvBasePosition::setFirstInterestingAssignment(size_t i)
{
	nFirstInterestingAssignment = (unsigned char)i;
}

size_t CvBasePosition::getFirstInterestingAssignment() const
{
	return (size_t)nFirstInterestingAssignment;
}

void CvBasePosition::UpdateScore(const STacticalAssignment& assignment)
{
	UpdateScore(assignment.iUnitID, assignment.GetPlotScore(), assignment.GetOldPlotScore(), assignment.GetDamageDelta(), assignment.GetBonusScore());
}

void CvBasePosition::UpdateScore(int iUnitId, int iPlotScore, int iOldPlotScore, int iDamageDelta_, int iBonusScore_)
{
	iScoreOverParent += iBonusScore_ + iPlotScore - iOldPlotScore + iDamageDelta_;
	iDamageDelta += iDamageDelta_;
	iBonusScore += iBonusScore_;

	const map<int, short>& plotScores_r = plotScores.read();

	//update total score and check for old plot score
	//total score is (iDamageDelta + iBonusScore) * 10 + sum(plotScores)
	//score over parent is (iBonusScore + iDamageDelta + iPlotScore - sPreviousPlotScore)
	iTotalScore = (iDamageDelta + iBonusScore) * 10;
	bool bFoundScore = false;
	for (map<int, short>::const_iterator it = plotScores_r.begin(); it != plotScores_r.end(); ++it)
	{
		int iLoopUnitID = it->first;
		short sPreviousPlotScore = it->second;

		iTotalScore += sPreviousPlotScore;
		if (iLoopUnitID == iUnitId)
		{
			bFoundScore = true;
			iScoreOverParent -= sPreviousPlotScore;
			if (iPlotScore != sPreviousPlotScore)
			{
				plotScores.write()[iUnitId] = iPlotScore;
			}
		}
	}
	//the unit didn't have a previous plot score, so we set it here
	if (!bFoundScore && iPlotScore != 0)
		plotScores.write()[iUnitId] = iPlotScore;
}

static int GetUnitDangerForPlot(const CvUnit* pUnit, const CvPlot* pPlot, int iSelfDamage, const CvTacticalPosition& assumedPosition)
{
	int iDanger = 0;

	//first try the cache
	if (!gTactPosStorage.getDangerCache().findDanger(pUnit->GetID(), pPlot->GetPlotIndex(), iSelfDamage, assumedPosition.GetUnitDamageDealt(), iDanger))
	{
		iDanger = pUnit->GetDanger(pPlot, assumedPosition.GetUnitDamageDealt(), iSelfDamage);
		//can happen with garrisons, catch this case as it messes up the math
		if (iDanger == INT_MAX)
			iDanger = 10 * pUnit->GetMaxHitPoints();

		gTactPosStorage.getDangerCache().storeDanger(pUnit->GetID(), pPlot->GetPlotIndex(), iSelfDamage, assumedPosition.GetUnitDamageDealt(), iDanger);
	}

	return iDanger;
}

// what is the rough state looking like after this assignment
static void GetNextPosition(const CvTacticalPosition& previousPosition, const STacticalAssignment* assignment, CvTacticalPosition& newPosition)
{
	newPosition.initFromParent(previousPosition);

	for (SUnitIDValueContainer::const_iterator it2 = assignment->unitDamage.begin(); it2 != assignment->unitDamage.end(); ++it2)
	{
		const SUnitIDValueContainer::value_type& p = *it2;
		newPosition.ChangeUnitDamage(p.first, p.second);
	}
	if (assignment->iDamagedCityId != -1)
		newPosition.ChangeCityDamage(assignment->iDamagedCityId, assignment->iCityDamage);
}

// what does the unit look like after this assignment
static SUnitStats GetNextUnit(const SUnitStats& previousUnit, const STacticalAssignment* assignment)
{
	SUnitStats newUnit = previousUnit;

	newUnit.iSelfDamage += assignment->iSelfDamage;
	newUnit.iMovesLeft = assignment->iRemainingMoves;

	return newUnit;
}

//note that the score returned from this function is not multiplied by 10 yet
bool ScoreAttackDamage(const CvTacticalPlot* tactPlot, const CvUnit* pUnit, const CvTacticalPlot* assumedPlot, const CvTacticalPosition& assumedPosition, CAttackCache& cache, STacticalAssignment* result, int iSelfDamage)
{
	eAggressionLevel eAggLvl = assumedPosition.getAggressionLevel();
	float fAggBias = assumedPosition.getAggressionBias();

	int iCityDamageDealt = 0;
	SUnitIDValueContainer unitDamageDealt;
	int iDamageReceived = 0; //always zero for ranged attack
	int iBonusScore = 0; //splash damage and other bonuses
	bool bRanged = pUnit->IsCanAttackRanged();

	//the damage calculation doesn't know about hypothetical flanking units, so we ignore it here and add it ourselves 
	int iPrevCityDamage = 0;
	SUnitIDValueContainer prevUnitDamage;
	int iPrevCityHitPoints = 0;
	SUnitIDValueContainer prevUnitHitPoints;

	const CvPlot* pUnitPlot = assumedPlot->getPlot();
	const CvPlot* pTestPlot = tactPlot->getPlot();

	CvCity* pEnemyCity = NULL;
	CvUnit* pEnemyUnit = NULL;

	bool bScoreReduction = false;

	//AL_LOW, AL_MEDIUM, AL_HIGH, AL_BRAVEHEART
	//braveheart allows attacks for which you need luck to survive
	int hpLimit[5] = {70,40,20,-5};

	if (tactPlot->isEnemyCity()) //a plot can be both a city and a unit - in that case we would attack the city
	{
		pEnemyCity = pTestPlot->getPlotCity();
		if (!pEnemyCity)
		{
			result->SetImpossible();
			return false;
		}

		iPrevCityDamage = assumedPosition.GetCityDamage(pEnemyCity->GetID());
		result->iDamagedCityId = pEnemyCity->GetID();
		int iPrevUnitDamage = 0;
		int iPrevUnitHitPoints = 0;

		pEnemyUnit = tactPlot->getEnemyUnit();
		if (pEnemyUnit)
		{
			iPrevUnitDamage = assumedPosition.GetUnitDamage(pEnemyUnit->GetID());
			prevUnitDamage.SetValue(pEnemyUnit->GetID(), iPrevUnitDamage);
			iPrevUnitHitPoints = pEnemyUnit->GetMaxHitPoints() - pEnemyUnit->getDamage() - iPrevUnitDamage;
			prevUnitHitPoints.SetValue(pEnemyUnit->GetID(), iPrevUnitHitPoints);
		}

		int iGarrisonDamage = 0;

		//first try the cache
		if (!cache.findAttack(pUnit->GetID(),pUnitPlot->GetPlotIndex(), pEnemyCity->GetID(), pEnemyUnit ? pEnemyUnit->GetID() : -1, iSelfDamage, iPrevUnitDamage, iPrevCityDamage, iGarrisonDamage, iCityDamageDealt, iDamageReceived))
		{
			iCityDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pEnemyCity, pUnit, pUnitPlot, iDamageReceived, iGarrisonDamage, true, iSelfDamage, iPrevUnitDamage, iPrevUnitDamage, true, true, pEnemyUnit);
			cache.storeAttack(pUnit->GetID(),pUnitPlot->GetPlotIndex(), pEnemyCity->GetID(), pEnemyUnit ? pEnemyUnit->GetID() : -1, iSelfDamage, iPrevUnitDamage, iPrevCityDamage, iGarrisonDamage, iCityDamageDealt, iDamageReceived);
		}

		iBonusScore += pUnit->GetRangeCombatSplashDamage(pTestPlot) + (pUnit->GetCityAttackPlunderModifier() / 50);
		iPrevCityHitPoints = pEnemyCity->GetMaxHitPoints() - pEnemyCity->getDamage() - iPrevCityDamage;


		bool bBlockaded = pEnemyCity->IsBlockadedWaterAndLand() || tactPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_BOTH, -1) == pTestPlot->countPassableNeighbors(NO_DOMAIN);

		//but we don't want our melee units to die so take into account self damage and counterattacks
		if (!bRanged)
		{
			if (iCityDamageDealt >= iPrevCityHitPoints - 1)
			{
				// If we take the city, the unit also dies
				if (iPrevUnitHitPoints > 0)
				{
					iGarrisonDamage = iPrevUnitHitPoints;
				}
			}
			else
			{
				//if we have multiple units encircling the city, try to take into account their attacks as well
				//easiest way is to consider damage from last turn. so ideally ranged units start attacking and melee joins in later
				float fRemainingTurnsOnCity = iPrevCityHitPoints / (max(iCityDamageDealt * fAggBias, pEnemyCity->getDamageTakenLastTurn() * 1.0f) + 1);
				if (iPrevUnitHitPoints > 0)
					// killing the garrisoned unit is also valuable
					fRemainingTurnsOnCity = min(fRemainingTurnsOnCity, iPrevUnitHitPoints / (max(iGarrisonDamage + fAggBias, pEnemyUnit->GetDamageTakenLastTurn() * 1.0f) + 1));

				//if the city cannot heal, be even more aggressive
				if (bBlockaded)
					fRemainingTurnsOnCity = max(0.f, fRemainingTurnsOnCity - 1);

				//consider that we have other units around which can soak damage
				int iCounterattackDamage = pEnemyCity->canRangeStrike() ? pEnemyCity->rangeCombatDamage(pUnit, false, pUnitPlot, true) : 0;
				float fScaledCounterattackDamage = iCounterattackDamage / fAggBias;
				float fRemainingTurnsOnAttacker = pUnit->GetCurrHitPoints() / (iDamageReceived + fScaledCounterattackDamage + 1);

				//no attack if it's too early yet
				bool bGoodFirstAttack = pUnit->getDamage() < 13 && iCityDamageDealt + iGarrisonDamage > iDamageReceived && iDamageReceived < 23;
				if (fRemainingTurnsOnAttacker < fRemainingTurnsOnCity && !bGoodFirstAttack)
				{
					result->SetImpossible();
					return false;
				}
			}
		}

		//city blockaded? not 100% accurate, but anyway TODO actual figure out if it's blockaded
		if (bBlockaded)
		{
			iCityDamageDealt *= max(100 + /*0 in CP, 20 in VP*/ GD_INT_GET(BLOCKADED_CITY_ATTACK_MODIFIER), 0);
			iCityDamageDealt /= 100;
			iGarrisonDamage *= max(100 + /*0 in CP, 20 in VP*/ GD_INT_GET(BLOCKADED_CITY_ATTACK_MODIFIER), 0);
			iGarrisonDamage /= 100;
		}

		if (pEnemyUnit)
			unitDamageDealt.ChangeValue(pEnemyUnit->GetID(), iGarrisonDamage);
	}
	else if (tactPlot->isEnemyCombatUnit())
	{
		pEnemyUnit = tactPlot->getEnemyUnit();
		if (!pEnemyUnit)
		{
			result->SetImpossible();
			return false;
		}

		int iPrevUnitDamage = assumedPosition.GetUnitDamage(pEnemyUnit->GetID());
		int iPrevUnitHitPoints = pEnemyUnit->GetCurrHitPoints() - iPrevUnitDamage;

		prevUnitDamage.SetValue(pEnemyUnit->GetID(), iPrevUnitDamage);
		prevUnitHitPoints.SetValue(pEnemyUnit->GetID(), iPrevUnitHitPoints);

		int iUnitDamageDealt = 0;

		//first use the cache
		int iCityDamageDealt;
		if (!cache.findAttack(pUnit->GetID(), pUnitPlot->GetPlotIndex(), pEnemyUnit->GetID(), -1, iSelfDamage, iPrevUnitDamage, 0, iUnitDamageDealt, iCityDamageDealt, iDamageReceived))
		{
			//use the quick and dirty method ... and don't check for general bonus etc (their position isn't official yet - we handle that below)
			iUnitDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pEnemyUnit, pUnit, pTestPlot, pUnitPlot, iDamageReceived, true, iSelfDamage, iPrevUnitDamage, true);
			cache.storeAttack(pUnit->GetID(), pUnitPlot->GetPlotIndex(), pEnemyUnit->GetID(), -1, iSelfDamage, iPrevUnitDamage, 0, iUnitDamageDealt, iCityDamageDealt, iDamageReceived);
		}

		iBonusScore += pUnit->EstimatePlagueDamage(pEnemyUnit);

		//problem is flanking bonus affects combat strength, not damage, so the effect is nonlinear. anyway just assume 10% per adjacent unit
		if (!bRanged) //only for melee
		{
			//it works both ways!
			//note that this can go quite wrong if we're facing multiple enemy players!
			int iDelta = tactPlot->getNumAdjacentFriendlies(DomainForUnit(pUnit), assumedPlot->getPlotIndex()) - assumedPlot->getNumAdjacentEnemies(DomainForUnit(pUnit));
			iUnitDamageDealt += (iDelta * iUnitDamageDealt) / 10;
			iDamageReceived -= (iDelta * iDamageReceived) / 10;
		}

		//repeat attacks may give extra bonus
		if (iPrevUnitDamage > 0)
		{
			int iBonus = pUnit->getMultiAttackBonus() + GET_PLAYER(pUnit->getOwner()).GetPlayerTraits()->GetMultipleAttackBonus();
			if (iBonus > 0) //the bonus affects attack strength, so the effect is hard to predict ...
				iUnitDamageDealt += (iBonus * (iUnitDamageDealt + iPrevUnitDamage)) / 100;
		}

		//don't be as aggressive when attacking embarked units
		if (!pEnemyUnit->IsCanAttack())
			fAggBias /= 2;

		//don't be distracted by attacks on barbarians or melee units in other domain when there are real enemies around
		if ((pEnemyUnit->getOwner() == BARBARIAN_PLAYER || (!pEnemyUnit->IsCanAttackRanged() && pEnemyUnit->getDomainType() != pUnit->getDomainType())) && !tactPlot->isEnemyCity())
			bScoreReduction = true;

		unitDamageDealt.ChangeValue(pEnemyUnit->GetID(), iUnitDamageDealt);
	}

	// Splash damage is always applied
	int iSplashDamage = pUnit->getSplashDamage();
	if (iSplashDamage > 0)
	{
		CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pTestPlot);
		for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
		{
			CvPlot* pAdjacentPlot = aNeighbors[i];
			if (!pAdjacentPlot)
				continue;

			const CvTacticalPlot* adjacentTactPlot = assumedPosition.getTactPlot(pAdjacentPlot->GetPlotIndex());
			if (!adjacentTactPlot)
				continue;

			CvUnit* pAdjacentUnit = adjacentTactPlot->getEnemyUnit();
			if (!pAdjacentUnit)
				continue;

			if (pAdjacentPlot->isFortification(pAdjacentUnit->getTeam()))
				continue;

			prevUnitHitPoints.SetValue(pAdjacentUnit->GetID(), pAdjacentUnit->GetCurrHitPoints() - assumedPosition.GetUnitDamage(pAdjacentUnit->GetID()));
			unitDamageDealt.ChangeValue(pAdjacentUnit->GetID(), iSplashDamage);
		}

		// Defensive unit clearing priority: bonus for attacking units adjacent to enemy cities
		// This helps clear the path for city assault and removes counterattack threats
		CvCity* pAdjacentEnemyCity = pTestPlot->GetAdjacentCity();
		if (pAdjacentEnemyCity && GET_PLAYER(assumedPosition.getPlayer()).IsAtWarWith(pAdjacentEnemyCity->getOwner()))
		{
			// Check if the enemy unit actually poses a threat or blocks our attack
			// Naval melee units can't threaten land attackers, so skip them unless:
			// 1. Our attacker is also naval (same domain combat)
			// 2. The enemy unit has ranged attacks (can hit land units)
			// 3. It's a garrison (always relevant)
			bool bRelevantThreat = true;
			if (pEnemy->getDomainType() == DOMAIN_SEA && !pEnemy->IsCanAttackRanged() && !pEnemy->IsGarrisoned())
			{
				// Naval melee unit - only relevant if attacker is also naval
				if (pUnit->getDomainType() != DOMAIN_SEA)
					bRelevantThreat = false;
			}
			
			if (bRelevantThreat)
			{
				// Base bonus for attacking a unit adjacent to an enemy city
				iExtraScore += 15;
				
				// Extra bonus if this is a kill - clearing defenders is very valuable
				if (iDamageDealt >= iPrevHitPoints - 10)
					iExtraScore += 25;
				
				// Extra bonus if the enemy unit can ranged attack (threatens our siege)
				if (pEnemy->IsCanAttackRanged())
					iExtraScore += 10;
				
				// Extra bonus if this kill would open a blockade position
				// Check if killing this unit would allow us to blockade the city from this plot
				if (iDamageDealt >= iPrevHitPoints - 10 && !pAdjacentEnemyCity->IsBlockaded(NO_DOMAIN))
					iExtraScore += 20;
				
				// Extra bonus if the city is already damaged - we're in the final assault phase
				if (pAdjacentEnemyCity->getDamage() > 0)
					iExtraScore += 10;
				
				// Strong bonus if this is a garrison unit - removing the garrison is critical
				if (pEnemy->IsGarrisoned())
					iExtraScore += 30;
			}
		}
	}

	//fake general bonus
	if (assumedPosition.HasSupport(pUnit->getDomainType()))
	{
		if (pEnemyUnit)
			unitDamageDealt.ChangeValue(pEnemyUnit->GetID(), unitDamageDealt.GetValue(pEnemyUnit->GetID()) / 10);
		iCityDamageDealt += iCityDamageDealt / 10;
		iDamageReceived -= iDamageReceived / 10;
	}

	//fake siege tower bonus
	if (assumedPosition.HasCitySupport() && tactPlot->isEnemyCity())
	{
		if (pEnemyUnit)
			unitDamageDealt.ChangeValue(pEnemyUnit->GetID(), unitDamageDealt.GetValue(pEnemyUnit->GetID()) / 5);
		iCityDamageDealt += iCityDamageDealt / 5;
		iDamageReceived -= iDamageReceived / 5;
	}

	int iTotalUnitDamageDealt = 0;
	SUnitIDValueContainer actualDamageDealt;
	int iTotalActualUnitDamageDealt = 0;
	bool bCityKill = false;
	bool bUnitKill = false;

	//bonus for a kill
	//don't be too precise because of randomness in damage calculation added later
	//if it doesn't work out we can try again
	//better than assuming it's not a kill and having a melee unit end up in a bad place
	if (iPrevCityHitPoints > 0 && iCityDamageDealt >= iPrevCityHitPoints)
	{
		bScoreReduction = false;

		iBonusScore += pUnit->GetExtraXPOnKill();

		//only melee can capture/kill a city!
		if (!bRanged && pUnit->IsCanAttackWithMove())
			bCityKill = true;
		else
		{
			if (iPrevCityHitPoints > 1)
				iBonusScore += min(iCityDamageDealt - iPrevCityHitPoints + 1, 3);
			iCityDamageDealt = min(iCityDamageDealt, iPrevCityHitPoints - 1);
		}
	}
	for (SUnitIDValueContainer::const_iterator it = unitDamageDealt.begin(); it != unitDamageDealt.end(); ++it)
	{
		int iPrevUnitHitPoints = prevUnitHitPoints.GetValue((*it).first);
		int iUnitDamageDealt = (*it).second;

		if (iUnitDamageDealt > iPrevUnitHitPoints)
			iBonusScore += min(iUnitDamageDealt - iPrevUnitHitPoints, 10);

		actualDamageDealt.SetValue((*it).first, min(iPrevUnitHitPoints, iUnitDamageDealt));

		iTotalUnitDamageDealt += iUnitDamageDealt;
		iTotalActualUnitDamageDealt += min(iPrevUnitHitPoints, iUnitDamageDealt);

		if (iPrevUnitHitPoints > 0 && iUnitDamageDealt >= iPrevUnitHitPoints)
		{
			if (pEnemyUnit && pEnemyUnit->GetID() == (*it).first)
				bUnitKill = true;

			iBonusScore += pUnit->GetExtraXPOnKill();

			bScoreReduction = false;

			if (pUnit->getHPHealedIfDefeatEnemy() > 0)
				iDamageReceived = max(iDamageReceived - pUnit->getHPHealedIfDefeatEnemy(), -pUnit->getDamage()); //may turn negative, but can't heal more than current damage

			// pillage fortification on kill?
			if (pUnit->IsPillageFortificationsOnKill())
			{
				if (pTestPlot->getImprovementType() != NO_IMPROVEMENT && !pTestPlot->IsImprovementPillaged())
				{
					CvImprovementEntry* pkImprovementInfo = GC.getImprovementInfo(pTestPlot->getImprovementType());
					if (pkImprovementInfo->IsNoFollowUp() && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pTestPlot->getOwner()))
					{
						iBonusScore += 15;
						// Citadel
						if (pkImprovementInfo->GetNearbyEnemyDamage() > 0 || pkImprovementInfo->GetDefenseModifier() >= 50)
						{
							iBonusScore += 15;
						}
					}
				}
			}

			// Only consider yield bonus for direct kills (not kills via splash damage)
			bool bYieldBonus = false;
			if (pEnemyUnit && pEnemyUnit->GetID() == (*it).first && pEnemyUnit->IsCombatUnit())
			{
				bYieldBonus = pUnit->GetGoldenAgeValueFromKills() > 0;

				if (!bYieldBonus)
				{
					UnitTypes eUnitType = pUnit->getUnitType();
					CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnitType);

					for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
					{
						YieldTypes eYield = (YieldTypes)iI;

						bYieldBonus = (pEnemyUnit->isBarbarian() && (pUnit->getYieldFromBarbarianKills(eYield) || pkUnitInfo->GetYieldFromBarbarianKills(eYield)))
							|| pUnit->getYieldFromKills(eYield) || pkUnitInfo->GetYieldFromKills(eYield);

						if (bYieldBonus)
							break;
					}
				}

				if (bYieldBonus)
					iBonusScore += 20;
			}
		}
	}

	if (bCityKill)
		result->eAssignmentType = A_MELEEKILL;
	else if (bUnitKill)
	{
		if (bRanged)
			result->eAssignmentType = A_RANGEKILL;
		else if (pUnitPlot->isFortification(pUnit->getTeam()) || iPrevCityHitPoints > 0)
			result->eAssignmentType = A_MELEEKILL_NO_ADVANCE;
		else
			result->eAssignmentType = A_MELEEKILL;
	}
	else
		result->eAssignmentType = bRanged ? A_RANGEATTACK : A_MELEEATTACK;

	result->iSelfDamage = iDamageReceived;
	result->unitDamage = unitDamageDealt;
	result->iCityDamage = iCityDamageDealt;

	//for melee units we check if the damage received is worth it ...
	if (iDamageReceived > 0 && gSafePlotCount[pUnit->GetID()] > 0)
	{
		float fAggFactor = /*100*/ GD_INT_GET(COMBAT_AI_OFFENSE_DAMAGEWEIGHT) / 100.f;
		switch (eAggLvl)
		{
		case AL_LOW:
			//want to do more damage than we take
			fAggFactor *= 0.7f;
			break;
		case AL_MEDIUM:
			//accept slightly more damage than we deal
			fAggFactor *= 1.1f;
			break;
		case AL_HIGH:
			//we check whether we can survive a counterattack in ScoreCombatUnitTurnEnd
			fAggFactor *= 2.3f;
			break;
		case AL_BRAVEHEART:
			//basically suicide
			fAggFactor *= 4.2f;
			break;
		default:
			result->SetImpossible();
			return false;
		}

		//need to consider danger as well
		//if there are many enemies around we need our melee units as shields, don't waste hp on attacks
		const CvPlot* pPlotAfterAttack = result->eAssignmentType == A_MELEEKILL ? pTestPlot : pUnitPlot;

		CvTacticalPosition pNewPosition;
		GetNextPosition(assumedPosition, result, pNewPosition);
		int iDanger = GetUnitDangerForPlot(pUnit, pPlotAfterAttack, iSelfDamage + iDamageReceived, pNewPosition);

		bool bBelowHpLimitAfterMeleeAttack = iDanger > pUnit->GetMaxHitPoints();
		//should consider self-damage from previous attacks here ... blitz
		if (iDamageReceived > 0 && pUnit->GetCurrHitPoints() - iDamageReceived < hpLimit[eAggLvl])
			bBelowHpLimitAfterMeleeAttack = true;

		//bias depends on the ratio of friendly to enemy units
		int iScaledDamage = int((iTotalUnitDamageDealt + iCityDamageDealt) * fAggBias * fAggFactor + 0.5f);
		int iDamageDelta = iScaledDamage - iDamageReceived;
		bool bVoluntaryCancel = (bBelowHpLimitAfterMeleeAttack && iDamageDelta < 0 && result->eAssignmentType != A_MELEEKILL && result->eAssignmentType != A_MELEEKILL_NO_ADVANCE);
		bool bSuicideCancel = (pUnit->GetCurrHitPoints() - iDamageReceived < 3) && eAggLvl != AL_BRAVEHEART; //in the real event there will be some randomness!
		if (bVoluntaryCancel || bSuicideCancel)
		{
			result->SetImpossible();
			return false;
		}
	}

	//finally the almighty score
	//add previous damage again and again to make concentrated fire attractive
	//todo: consider pEnemy->getUnitInfo().GetProductionCost() and pEnemy->GetBaseCombatStrength()
	//todo: normalize damage done by max hp to balance between city attacks and unit attacks?

	int iActualCityDamageDealt = min(iCityDamageDealt, iPrevCityHitPoints + (pUnit->IsCanAttackRanged() ? 2 : 10));

	int iActualDamageTaken = min(iDamageReceived, pUnit->GetCurrHitPoints() - iSelfDamage);
	if (iActualDamageTaken < 0)
		iActualDamageTaken = max(iActualDamageTaken, -(pUnit->getDamage() + iSelfDamage));


	// Focus on low HP units/cities
	int iTotalActualDamageDealt = iActualCityDamageDealt + iTotalActualUnitDamageDealt;
	if (iTotalActualDamageDealt > 0)
	{
		int iPrevUnitHitPoints = pEnemyUnit ? prevUnitHitPoints.GetValue(pEnemyUnit->GetID()) : 0;
		int iHpAfterAttack = iPrevUnitHitPoints > 0 ? iPrevUnitHitPoints - actualDamageDealt.GetValue(pEnemyUnit->GetID()) : -1;
		if (iHpAfterAttack == -1 || iHpAfterAttack > iPrevCityHitPoints - iActualCityDamageDealt)
			iHpAfterAttack = iPrevCityHitPoints - iActualCityDamageDealt;
		iBonusScore += min(max(30 - iHpAfterAttack, 0), iTotalActualDamageDealt);
	}

	if (bScoreReduction)
		iBonusScore -= 10;

	if (bCityKill)
		iBonusScore += 100;
	else if (bUnitKill)
		iBonusScore += 15;

	result->SetScore(0, iBonusScore, iActualCityDamageDealt + iTotalActualUnitDamageDealt - iActualDamageTaken);

	return bCityKill || bUnitKill;
}

bool TacticalAIHelpers::IsPlayerCitadel(const CvPlot* pPlot, PlayerTypes ePlayer)
{
	if (!pPlot || ePlayer==NO_PLAYER || pPlot->getOwner() != ePlayer)
		return false;

	// Citadel here?
	ImprovementTypes eImprovement = pPlot->getImprovementType();
	if (eImprovement != NO_IMPROVEMENT && !pPlot->IsImprovementPillaged())
	{
		CvImprovementEntry* pInfo = GC.getImprovementInfo(eImprovement);
		if (pInfo->GetNearbyEnemyDamage() <= /*10 in CP, 5 in VP*/ GD_INT_GET(ENEMY_HEAL_RATE))
			return false;

		if (pInfo->GetDefenseModifier() < 50)
			return false;

		return true;
	}
	
	return false;
}

int TacticalAIHelpers::GetOtherPlayerImprovementDamage(const CvPlot* pPlot, PlayerTypes ePlayer, bool bCheckWar)
{
	if (!pPlot || ePlayer==NO_PLAYER || !pPlot->isOwned())
		return 0;

	// Citadel here?
	ImprovementTypes eImprovement = pPlot->getImprovementType();
	if (eImprovement != NO_IMPROVEMENT && !pPlot->IsImprovementPillaged())
	{
		CvImprovementEntry* pInfo = GC.getImprovementInfo(eImprovement);
		if (pInfo->GetNearbyEnemyDamage() == 0)
			return 0;

		if (!bCheckWar || GET_PLAYER(ePlayer).IsAtWarWith(pPlot->getOwner()))
			return pInfo->GetNearbyEnemyDamage();
	}
	
	return 0;
}

int TacticalAIHelpers::SentryScore(const CvPlot * pPlot, PlayerTypes ePlayer)
{
	TeamTypes eTeam = GET_PLAYER(ePlayer).getTeam();
	int iScore = pPlot->defenseModifier(eTeam, false, false);

	//basically check for plots with good visibility
	const vector<CvPlot*>& possibleEnemyPlots = GC.getMap().GetPlotsAtRangeX(pPlot, 2, true, true);
	for (size_t i = 0; i < possibleEnemyPlots.size(); i++)
	{
		//there may be a sentinel null pointer
		if (possibleEnemyPlots[i] == NULL)
			continue;
		//really we should consider whether the enemy can move there, not our team ...
		if (!possibleEnemyPlots[i]->isValidMovePlot(ePlayer, false))
			continue;

		iScore += 23; //less than a good defense bonus ...
	}

	return iScore;
}

static int HealUnitsInPlot(SUnitIDValueContainer& unitHealing, int& iDamageDelta, const CvTacticalPlot* tactPlot, int iHealAmount, const CvTacticalPosition& assumedPosition)
{
	if (!tactPlot)
		return 0;

	const vector<STacticalUnit>& units = tactPlot->getUnitsAtPlot();
	for (vector<STacticalUnit>::const_iterator it = units.begin(); it != units.end(); ++it)
	{
		CvUnit* pUnit = GET_PLAYER(assumedPosition.getPlayer()).getUnit(it->iUnitID);
		if (!pUnit)
			continue;

		const SUnitStats* unit = assumedPosition.GetUnitStats(it->iUnitID);

		int iActualHealAmount = min(iHealAmount, pUnit->GetCurrHitPoints() - (unit ? unit->iSelfDamage : 0));
		unitHealing.ChangeValue(pUnit->GetID(), iActualHealAmount);
		iDamageDelta += iActualHealAmount;
	}

	return iDamageDelta;
}

static int HealAdjacentUnits(SUnitIDValueContainer& unitHealing, int& iDamageDelta, const CvPlot* pPlot, int iDamageAmount, const CvTacticalPosition& assumedPosition)
{
	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	int iHealing = 0;
	for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		CvPlot* pAdjacentPlot = aNeighbors[i];
		if (!pAdjacentPlot)
			continue;

		const CvTacticalPlot* adjacentTactPlot = assumedPosition.getTactPlot(pAdjacentPlot->GetPlotIndex());
		if (!adjacentTactPlot)
			continue;

		iHealing += HealUnitsInPlot(unitHealing, iDamageDelta, adjacentTactPlot, iDamageAmount, assumedPosition);
	}
	return iHealing;
}

static int DamageUnitInPlot(SUnitIDValueContainer& unitDamage, int& iDamageDelta, const CvPlot* pPlot, const CvTacticalPlot* tactPlot, int iDamageAmount, const CvTacticalPosition& assumedPosition)
{
	CvUnit* pUnit = tactPlot->getEnemyUnit();
	if (!pUnit)
		return 0;

	if (pPlot->isFortification(pUnit->getTeam()))
		return 0;

	int iActualDamageAmount = min(iDamageAmount, pUnit->GetCurrHitPoints() - assumedPosition.GetUnitDamage(pUnit->GetID()));
	unitDamage.ChangeValue(pUnit->GetID(), iActualDamageAmount);
	iDamageDelta += iActualDamageAmount;
	return iDamageDelta;
}

static int DamageAdjacentUnits(SUnitIDValueContainer& unitDamage, int& iDamageDelta, const CvPlot* pPlot, int iDamageAmount, const CvTacticalPosition& assumedPosition)
{
	int iDamage = 0;
	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		const CvPlot* pAdjacentPlot = aNeighbors[i];
		if (!pAdjacentPlot)
			continue;

		const CvTacticalPlot* adjacentTactPlot = assumedPosition.getTactPlot(pAdjacentPlot->GetPlotIndex());
		if (!adjacentTactPlot)
			continue;

		iDamage += DamageUnitInPlot(unitDamage, iDamageDelta, pAdjacentPlot, adjacentTactPlot, iDamageAmount, assumedPosition);
	}
	return iDamage;
}

static int GetPrevPlotScore(int iUnitID, const CvBasePosition& position)
{
	const STacticalAssignment* prevAssignment = position.getLatestAssignment(iUnitID);
	return prevAssignment ? prevAssignment->GetPlotScore() : 0;
}

static STacticalAssignment* ScorePlotForPillageMove(const SUnitStats& unit, const CvTacticalPlot* testPlot, int iAssumedMovesLeft, const CvTacticalPosition& assumedPosition)
{
	//default action is do nothing and invalid score (not -INT_MAX, to prevent overflows!)
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex,testPlot->getPlotIndex(), unit.iUnitID, iAssumedMovesLeft, unit.eMoveStrategy, A_PILLAGE, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	//the plot we're checking right now
	const CvPlot* pTestPlot = testPlot->getPlot();
	const CvUnit* pUnit = unit.pUnit;
	const uint32 plotFlags = pTestPlot->GetPlotCacheFlags();

	int iDamageDelta = 0;
	int iSelfDamage = 0;
	int iBonusScore = 0;

	if (!pUnit->canPillage(pTestPlot) || assumedPosition.plotHasAssignmentOfType(testPlot->getPlotIndex(), A_PILLAGE))
		return result;

	if (pUnit->canPillage(pTestPlot) && !assumedPosition.plotHasAssignmentOfType(testPlot->getPlotIndex(), A_PILLAGE))
	{
		if (!unit.pUnit->hasFreePillageMove())
			result->iRemainingMoves -= min((int)result->iRemainingMoves, GD_INT_GET(MOVE_DENOMINATOR));

		int iImprovementDamage = TacticalAIHelpers::GetOtherPlayerImprovementDamage(pTestPlot, assumedPosition.getPlayer(), true);
		if (iImprovementDamage > 0)
			iBonusScore += iImprovementDamage;
		if (iImprovementDamage < 15 && pTestPlot->getResourceType(pUnit->getTeam()) != NO_RESOURCE && GC.getResourceInfo(pTestPlot->getResourceType(pUnit->getTeam()))->getResourceUsage() == RESOURCEUSAGE_STRATEGIC)
			iBonusScore += 15;
		else if (iImprovementDamage < 10 && pTestPlot->getOwner() != NO_PLAYER && pTestPlot->getRouteType() != NO_ROUTE && (plotFlags & (CvPlot::PLOT_CACHE_HILLS | CvPlot::PLOT_CACHE_DESERT | CvPlot::PLOT_CACHE_MARSH | CvPlot::PLOT_CACHE_FOREST | CvPlot::PLOT_CACHE_RIVER)))
		{
			// route in rough terrain?
			iBonusScore += 10;
		}
		
		if (pUnit->getPillageHealAmount(pTestPlot) > 0)
		{
			int iHealAmount = pUnit->getPillageHealAmount(pTestPlot);
			if (iHealAmount > pUnit->getDamage() + unit.iSelfDamage)
				iHealAmount = pUnit->getDamage() + unit.iSelfDamage;

			if (iHealAmount > 0)
			{
				iSelfDamage -= iHealAmount;
				iDamageDelta += iHealAmount;
			}
		}

		if (pTestPlot->getImprovementType() != NO_IMPROVEMENT && !pTestPlot->IsImprovementPillaged())
		{
			iBonusScore += pUnit->GetXPFromPillaging();

			int iAOEHeal = pUnit->getAOEHealOnPillage() > 0;
			if (iAOEHeal > 0)
				HealAdjacentUnits(result->unitHealing, iDamageDelta, pTestPlot, iAOEHeal, assumedPosition);

			int iAOEDamage = pUnit->getAOEDamageOnPillage() > 0;
			if (iAOEDamage > 0)
				DamageAdjacentUnits(result->unitDamage, iDamageDelta, pTestPlot, iAOEDamage, assumedPosition);
		}
	}

	result->iSelfDamage = iSelfDamage;
	result->SetScore(0, iBonusScore, iDamageDelta);
	
	return result;
}


bool isKillAssignment(eUnitAssignmentType eAssignmentType)
{
	return eAssignmentType == A_MELEEKILL ||
		eAssignmentType == A_MELEEKILL_NO_ADVANCE ||
		eAssignmentType == A_RANGEKILL;
}

static bool IsSubmarineUnit(const CvUnit* pUnit)
{
	if (!pUnit)
		return false;

	if (pUnit->AI_getUnitAIType() == UNITAI_SUBMARINE)
		return true;

	static const UnitCombatTypes eSubCombat = (UnitCombatTypes)GC.getInfoTypeForString("UNITCOMBAT_SUBMARINE");
	return (pUnit->getUnitCombatType() == eSubCombat);
}

// Check if a unit can detect submarines (destroyers, ASW aircraft, etc.)
static bool IsAntiSubmarineUnit(const CvUnit* pUnit)
{
	if (!pUnit)
		return false;

	static const InvisibleTypes eSubInvisible = (InvisibleTypes)GC.getInfoTypeForString("INVISIBLE_SUBMARINE");
	if (eSubInvisible == NO_INVISIBLE)
		return false;

	return (pUnit->getSeeInvisibleType() == eSubInvisible);
}

static int CountEnemySubDetectUnitsAroundPlot(const CvPlot* pPlot, const CvUnit* pUnit, int iRange)
{
	if (!pPlot || !pUnit)
		return 0;

	static const InvisibleTypes eSubInvisible = (InvisibleTypes)GC.getInfoTypeForString("INVISIBLE_SUBMARINE");
	if (eSubInvisible == NO_INVISIBLE)
		return 0;

	TeamTypes eOurTeam = pUnit->getTeam();
	int iCount = 0;

	for (int i = RING0_PLOTS; i < RING_PLOTS[iRange]; i++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(pPlot, i);
		if (!pLoopPlot)
			continue;

		if (!pLoopPlot->isVisible(eOurTeam))
			continue;

		CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), pUnit, true);
		if (!pEnemy)
			continue;

		if (!pEnemy->isEnemy(eOurTeam, pLoopPlot))
			continue;

		if (pEnemy->getSeeInvisibleType() == eSubInvisible)
			iCount++;
	}

	return iCount;
}

int ScoreCombatUnitTurnEnd(const CvUnit* pUnit, eUnitAssignmentType eLastAssignment, const CvTacticalPlot* testPlot, int iDanger,
							CvTacticalPlot::eTactPlotDomain eRelevantDomain, int iSelfDamage,
							const CvTacticalPosition& assumedPosition, eUnitMoveEvalMode evalMode, bool bRelaxedCheck, bool bOnlyCheckImpossible)
{
	int iResult = 0;
	const CvPlot* pTestPlot = testPlot->getPlot();

	//the danger value reflects any defensive terrain bonuses
	//but unfortunately danger is not very useful here
	// * ZOC is unclear during simulation
	// * freshly revealed enemy units are not considered
	int iNumAdjFriendlies = (evalMode==EM_FINAL) ? testPlot->getNumAdjacentFriendliesEndTurn(eRelevantDomain) : testPlot->getNumAdjacentFriendlies(eRelevantDomain, -1);
	if (bRelaxedCheck) //assume we have friends which might catch up to us later
		iNumAdjFriendlies++;
	bool bIsFrontlineCitadelOrCity = (TacticalAIHelpers::IsPlayerCitadel(pTestPlot, assumedPosition.getPlayer()) || pTestPlot->isCity()) && pUnit->getDomainType() == DOMAIN_LAND && testPlot->getEnemyDistance() < 3;

	//don't do it if it's a death trap (unless there is no other choice ...)
	int iNumAdjEnemies = testPlot->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH);
	if (iNumAdjEnemies > 3 || (iNumAdjEnemies == 3 && assumedPosition.getAggressionBias() < 1))
		if (!bIsFrontlineCitadelOrCity)
			return INT_MAX;

	int iMaxHitPoints = pUnit->GetMaxHitPoints();
	int iCurrHitPoints = iMaxHitPoints - pUnit->getDamage() - iSelfDamage;

	//unseen enemies might be hiding behind the edge, so assume danger there
	if (testPlot->isEdgePlot())
	{
		//siege units (with limited visibility) should not move there unless covered (the -1 is important)
		if (pUnit->visibilityRange() < 2 && testPlot->getNumAdjacentFriendlies(DomainForUnit(pUnit), -1) < 2)
			return INT_MAX;
		
		iDanger = max(iMaxHitPoints / 2, iDanger);
	}
	if (!MOD_COMBATAI_TWO_PASS_DANGER && !bOnlyCheckImpossible)
	{
		if (iDanger > 0 && (iDanger > iMaxHitPoints || !testPlot->isEdgePlot()) && testPlot->hasCoverFromOtherUnits(assumedPosition))
			//check for cover and assume this would help us
			iDanger /= 2;
	}

	if (iDanger > 0)
	{
		//avoid extreme danger, except in citadels
		if (!bIsFrontlineCitadelOrCity && assumedPosition.getAggressionLevel() != AL_BRAVEHEART)
		{
			//extra careful with ranged / siege units / carriers
			if (pUnit->AI_getUnitAIType() == UNITAI_RANGED || pUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD || pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
			{
				if (iDanger > iCurrHitPoints)
					return INT_MAX;
			}

			//the minimum amount of hitpoint we want a standalone unit to have for the expected counterattacks
			bool isForKill = (eLastAssignment == A_MELEEKILL || eLastAssignment == A_MELEEKILL_NO_ADVANCE || eLastAssignment == A_RANGEKILL);
			bool couldFlee = (gSafePlotCount[pUnit->GetID()] > 0);
			int iMagicNumber = (isForKill || !couldFlee)  ? 23 : 42;

			//if this is the only enemy ...
			if (assumedPosition.getNumEnemies() == 1 && isForKill)
				iMagicNumber = 12;

			//if there is nothing we would cover or that covers us or we are low on health, don't do it
			if (iCurrHitPoints * iCurrHitPoints < iMagicNumber * iDanger)
				return INT_MAX;
		}

		if (!bOnlyCheckImpossible)
		{
			//make it relative to current hitpoints
			int iScaledDanger = (iDanger * /*100*/ GD_INT_GET(COMBAT_AI_OFFENSE_DANGERWEIGHT)) / max(1, iCurrHitPoints);

			//danger values can get very high if there are many enemy units around, so try to normalize this a bit
			//this maps 225 to 225, higher values get flattened
			if (iScaledDanger > 225)
				iScaledDanger = 15 * sqrti(iScaledDanger);

			//try to be more careful with highly promoted units
			iScaledDanger += (pUnit->getExperienceTimes100() - GET_PLAYER(assumedPosition.getPlayer()).GetAvgUnitExp100()) / 200;

			//no reason to run into the enemy alone
			if (iNumAdjFriendlies == 0)
				iScaledDanger *= 2;

			//penalty for high danger plots (should this be personality dependent?)
			iResult -= iScaledDanger;
		}
	}

	// Wounded unit damage rotation - heavily wounded units should avoid front line
	// This encourages healthier units to take hits while wounded units heal
	int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / max(1, pUnit->GetMaxHitPoints());
	if (iHealthPercent < 35 && testPlot->getEnemyDistance(eRelevantDomain) <= 1)
	{
		// Heavily wounded on front line is bad - they might die and be lost
		// Exception: citadels and cities are safer
		if (!bIsFrontlineCitadelOrCity)
			iResult -= 15;
	}
	else if (iHealthPercent > 80 && testPlot->getEnemyDistance(eRelevantDomain) <= 1 && !pUnit->IsCanAttackRanged())
	{
		// Healthy melee units should take the front line
		iResult += 5;
	}

	if (bOnlyCheckImpossible)
		return 1;

	//try to stay together, in pairs at least
	if (iNumAdjFriendlies > 0)
	{
		//very vulnerable units
		if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA || pUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
			iResult += iNumAdjFriendlies * 11;
		else
			iResult += 7 + iNumAdjFriendlies;
	}
	//when in doubt, stay under air cover
	if (testPlot->hasAirCover())
		iResult+=3;
	//when in doubt, hide from the enemy
	if (!pTestPlot->IsKnownVisibleToEnemy(pUnit->getOwner()))
		iResult++;

	// DESTROYER ASW POSITIONING: Anti-submarine units should position to detect and screen
	// This encourages destroyers to patrol ahead of valuable ships and near detected subs
	if (IsAntiSubmarineUnit(pUnit) && pUnit->getDomainType() == DOMAIN_SEA)
	{
		// Check for nearby friendly high-value ships that need ASW screening
		bool bNearCarrier = false;
		bool bNearCapitalShip = false;
		int iASWScreenBonus = 0;
		
		for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
			if (!pLoopPlot)
				continue;
			
			for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
			{
				CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
				if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner())
					continue;
				
				if (pLoopUnit->getDomainType() != DOMAIN_SEA)
					continue;
				
				// Carrier screening is highest priority
				if (pLoopUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
				{
					bNearCarrier = true;
					iASWScreenBonus += 8;
				}
				// Other capital ships also benefit from ASW screening
				else if (pLoopUnit->GetBaseCombatStrength() > pUnit->GetBaseCombatStrength() && 
						 !IsAntiSubmarineUnit(pLoopUnit))
				{
					bNearCapitalShip = true;
					iASWScreenBonus += 3;
				}
			}
		}
		
		// Bonus for screening high-value assets
		if (bNearCarrier)
			iResult += min(iASWScreenBonus, 15);
		else if (bNearCapitalShip)
			iResult += min(iASWScreenBonus, 8);
		
		// Bonus for positioning ahead of the fleet (toward enemies)
		// ASW ships should be between enemies and valuable ships
		if ((bNearCarrier || bNearCapitalShip) && testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA) < 3)
		{
			iResult += 5; // Forward screening position
		}
	}

	// NAVAL FLEET CONCENTRATION POSITIONING: Naval units should prefer to end turns
	// in positions where they can support other ships attacking the same targets
	if (pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit() && !bIsSubmarine)
	{
		int iConcentrationBonus = 0;
		int iAdjacentNavalFriendlies = 0;
		int iNearbyNavalUnitsInRange = 0;
		
		// Count nearby friendly naval combat units
		for (int i = RING0_PLOTS; i < RING_PLOTS[3]; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
			if (!pLoopPlot)
				continue;
			
			for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
			{
				CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
				if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner() || pLoopUnit == pUnit)
					continue;
				if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
					continue;
				
				int iDist = plotDistance(*testPlot.getPlot(), *pLoopPlot);
				if (iDist <= 1)
				{
					iAdjacentNavalFriendlies++;
				}
				
				// Count ships that share attack range with us (for ranged units)
				if (pUnit->IsCanAttackRanged() && pLoopUnit->IsCanAttackRanged())
				{
					// Check if from this position we could engage similar targets
					int iUnitRange = pUnit->GetRange();
					int iLoopRange = pLoopUnit->GetRange();
					// If both can reach similar distance, they can focus fire
					if (iDist <= iUnitRange + iLoopRange)
					{
						iNearbyNavalUnitsInRange++;
					}
				}
			}
		}
		
		// Bonus for naval formation - not too spread out, not too bunched
		if (iAdjacentNavalFriendlies >= 1 && iAdjacentNavalFriendlies <= 3)
		{
			iConcentrationBonus += 8; // Good formation density
		}
		else if (iAdjacentNavalFriendlies > 3)
		{
			iConcentrationBonus += 4; // A bit crowded but still supporting
		}
		
		// Ranged ships get bonus for being in coordinated firing positions
		if (pUnit->IsCanAttackRanged() && iNearbyNavalUnitsInRange >= 1)
		{
			iConcentrationBonus += min(iNearbyNavalUnitsInRange * 3, 12); // Up to +12 for fire coordination
		}
		
		// Position near enemy targets where fleet can focus fire
		if (testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA) <= 2 && iAdjacentNavalFriendlies >= 2)
		{
			iConcentrationBonus += 10; // Good attack position with support
		}
		
		iResult += iConcentrationBonus;
	}

	// CARRIER POSITIONING: Carriers need special positioning logic
	// They must balance safety (very valuable with cargo) vs. staying in range for air strikes
	if (pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
	{
		int iCarrierBonus = 0;
		bool bHasCargo = pUnit->hasCargo();
		int iCargoCount = pUnit->getCargo();
		
		// SAFETY FIRST: Carriers with aircraft are extremely valuable and must be protected
		if (bHasCargo)
		{
			// Heavy penalty for being too close to enemies when loaded
			int iEnemyDist = testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA);
			if (iEnemyDist <= 1)
			{
				iCarrierBonus -= 50 + (iCargoCount * 15); // Very bad - carrier at risk with cargo
			}
			else if (iEnemyDist == 2)
			{
				iCarrierBonus -= 15 + (iCargoCount * 5); // Risky position
			}
			else if (iEnemyDist >= 3 && iEnemyDist <= 5)
			{
				iCarrierBonus += 10; // Good safe distance
			}
			
			// Extra bonus for escort coverage when loaded
			int iEscortCount = 0;
			for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
				if (!pLoopPlot)
					continue;
				
				for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
				{
					CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
					if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner())
						continue;
					if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
						continue;
					if (pLoopUnit == pUnit)
						continue;
					
					int iDist = plotDistance(*testPlot.getPlot(), *pLoopPlot);
					if (iDist <= 1)
					{
						iEscortCount++;
						// ASW escorts are particularly valuable for carriers
						if (IsAntiSubmarineUnit(pLoopUnit))
							iCarrierBonus += 5;
					}
				}
			}
			
			// Loaded carriers need escorts
			if (iEscortCount >= 2)
				iCarrierBonus += 15;
			else if (iEscortCount == 1)
				iCarrierBonus += 5;
			else if (iEscortCount == 0 && iEnemyDist <= 3)
				iCarrierBonus -= 20; // No escort near enemies - very risky!
		}
		
		// AIR RANGE OPTIMIZATION: Position to maximize air unit effectiveness
		// Check if this position keeps carrier within strike range of targets
		if (bHasCargo)
		{
			int iMaxAirRange = 0;
			int iMinAirRange = 99;
			int iAttackAirCount = 0;
			
			// Get actual range of loaded air units by iterating cargo
			// Note: We use the carrier's current plot since cargo moves with carrier
			const CvPlot* pCarrierPlot = pUnit->plot();
			if (pCarrierPlot)
			{
				const IDInfo* pUnitNode = pCarrierPlot->headUnitNode();
				while (pUnitNode != NULL)
				{
					const CvUnit* pCargoUnit = ::GetPlayerUnit(*pUnitNode);
					pUnitNode = pCarrierPlot->nextUnitNode(pUnitNode);
					
					if (pCargoUnit && pCargoUnit->getTransportUnit() == pUnit)
					{
						// This is one of our loaded aircraft
						int iCargoRange = pCargoUnit->GetRange();
						
						// Only count attack aircraft (bombers, missiles) for strike range
						// Fighters have shorter range and are for defense
						UnitAITypes eCargoAI = pCargoUnit->AI_getUnitAIType();
						if (eCargoAI == UNITAI_ATTACK_AIR || eCargoAI == UNITAI_MISSILE_AIR || eCargoAI == UNITAI_ICBM)
						{
							iAttackAirCount++;
							if (iCargoRange > iMaxAirRange)
								iMaxAirRange = iCargoRange;
							if (iCargoRange < iMinAirRange)
								iMinAirRange = iCargoRange;
						}
						else if (eCargoAI == UNITAI_DEFENSE_AIR)
						{
							// Fighters - track for interception range but don't use for strike positioning
							// Fighters still benefit from being near enemies for interception
						}
					}
				}
			}
			
			// If no attack aircraft, use a default range (might have only fighters)
			if (iMaxAirRange == 0)
				iMaxAirRange = 6; // Default fighter range for interception positioning
			
			// Check if there are enemy targets within air range from this position
			const CvPlot* pTargetPlot = assumedPosition.getTarget();
			if (pTargetPlot && iAttackAirCount > 0)
			{
				int iDistToTarget = plotDistance(*testPlot.getPlot(), *pTargetPlot);
				
				// Optimal: within air range but not too close (safe position)
				// Use the minimum range of our attack aircraft to ensure all can strike
				int iEffectiveRange = (iMinAirRange < 99) ? iMinAirRange : iMaxAirRange;
				
				if (iDistToTarget <= iEffectiveRange && iDistToTarget >= 3)
				{
					iCarrierBonus += 25; // Perfect positioning - safe but in range
					
					// Extra bonus if all attack aircraft can reach
					if (iDistToTarget <= iMinAirRange)
						iCarrierBonus += 10; // All aircraft can strike
				}
				else if (iDistToTarget <= iMaxAirRange)
				{
					iCarrierBonus += 15; // At least some aircraft in range
				}
				else if (iDistToTarget <= iMaxAirRange + 2)
				{
					iCarrierBonus += 5; // Close to range, might reach next turn
				}
			}
			
			// Also check for any enemy units within air range
			int iTargetsInAirRange = 0;
			int iMaxRingToCheck = min(iMaxAirRange, 10); // Cap at 10 for performance
			for (int iRing = 3; iRing <= iMaxRingToCheck; iRing++)
			{
				int iRingStart = GetRingPlotCountSafe(iRing - 1);
				int iRingEnd = GetRingPlotCountSafe(iRing);
				for (int i = iRingStart; i < iRingEnd; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
					if (pLoopPlot && pLoopPlot->isVisibleOtherUnit(pUnit->getOwner()))
					{
						CvUnit* pEnemyUnit = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemyUnit)
							iTargetsInAirRange++;
					}
					if (pLoopPlot && pLoopPlot->isCity())
					{
						CvCity* pCity = pLoopPlot->getPlotCity();
						if (pCity && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pCity->getOwner()))
							iTargetsInAirRange += 2; // Cities are valuable targets
					}
				}
			}
			
			// Bonus for having targets in optimal air range (scaled by attack aircraft count)
			if (iTargetsInAirRange > 0 && iAttackAirCount > 0)
			{
				int iTargetBonus = min(iTargetsInAirRange * 3, 15);
				iTargetBonus += min(iAttackAirCount * 3, 12); // More bombers = more important to be in range
				iCarrierBonus += min(iTargetBonus, 25);
			}
		}
		
		// FLEET INTEGRATION: Carriers should stay with the battle group but behind the line
		{
			int iCapitalShipsAhead = 0; // Ships between carrier and enemy
			int iCapitalShipsBehind = 0; // Ships behind carrier (carrier is too far forward)
			
			for (int i = RING0_PLOTS; i < RING_PLOTS[3]; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
				if (!pLoopPlot)
					continue;
				
				for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
				{
					CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
					if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner() || pLoopUnit == pUnit)
						continue;
					if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
						continue;
					if (pLoopUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
						continue; // Don't count other carriers
					
					// Check if this ship is between us and enemies
					int iFriendlyEnemyDist = 0;
					for (int j = RING0_PLOTS; j < RING_PLOTS[3]; j++)
					{
						CvPlot* pEnemyCheck = iterateRingPlots(pLoopPlot, j);
						if (pEnemyCheck && pEnemyCheck->isVisibleOtherUnit(pUnit->getOwner()))
						{
							iFriendlyEnemyDist = plotDistance(*pLoopPlot, *pEnemyCheck);
							break;
						}
					}
					
					int iCarrierEnemyDist = testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA);
					if (iFriendlyEnemyDist > 0 && iFriendlyEnemyDist < iCarrierEnemyDist)
					{
						iCapitalShipsAhead++; // This ship is screening us
					}
					else if (iFriendlyEnemyDist > 0 && iFriendlyEnemyDist > iCarrierEnemyDist)
					{
						iCapitalShipsBehind++; // This ship is behind us - we're too far forward!
					}
				}
			}
			
			// Bonus for having ships ahead (screening the carrier)
			if (iCapitalShipsAhead >= 2)
			{
				iCarrierBonus += 15; // Good screening
			}
			else if (iCapitalShipsAhead == 1)
			{
				iCarrierBonus += 8;
			}
			
			// Penalty for being ahead of the fleet (carrier is exposed)
			if (iCapitalShipsBehind > 0 && iCapitalShipsAhead == 0)
			{
				iCarrierBonus -= 15; // We're ahead of our escorts - dangerous!
			}
			else if (iCapitalShipsBehind > iCapitalShipsAhead)
			{
				iCarrierBonus -= 8; // More ships behind than ahead - we're too far forward
			}
		}
		
		iResult += iCarrierBonus;
	}

	// === NAVAL ESCORT FOR EMBARKED UNITS ===
	// Naval combat units should position to protect friendly embarked units
	// This is critical for amphibious operations - transports need warship escort
	if (pUnit->getDomainType() == DOMAIN_SEA && pUnit->IsCombatUnit())
	{
		int iEscortBonus = 0;
		int iEmbarkedUnitsNearby = 0;
		int iEmbarkedUnitsAdjacent = 0;
		
		// Check for friendly embarked units that need protection
		for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(testPlot.getPlot(), i);
			if (!pLoopPlot || !pLoopPlot->isWater())
				continue;
			
			for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
			{
				CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
				if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner())
					continue;
				
				// Found a friendly embarked unit
				if (pLoopUnit->isEmbarked())
				{
					iEmbarkedUnitsNearby++;
					int iDist = plotDistance(*testPlot.getPlot(), *pLoopPlot);
					if (iDist <= 1)
						iEmbarkedUnitsAdjacent++;
					
					// Higher value targets get more escort priority
					// Great Generals, Settlers, Workers are very valuable
					if (pLoopUnit->IsGreatGeneral() || pLoopUnit->IsGreatAdmiral())
						iEscortBonus += 6;
					else if (pLoopUnit->AI_getUnitAIType() == UNITAI_SETTLE)
						iEscortBonus += 5;
					else if (pLoopUnit->IsCombatUnit())
						iEscortBonus += 3; // Combat units waiting to land
					else
						iEscortBonus += 2; // Workers, etc.
				}
			}
		}
		
		if (iEmbarkedUnitsNearby > 0)
		{
			// Base bonus for being near embarked units
			iResult += min(iEscortBonus, 20);
			
			// Extra bonus for being adjacent (direct protection)
			if (iEmbarkedUnitsAdjacent > 0)
				iResult += iEmbarkedUnitsAdjacent * 3;
			
			// Naval ranged units should position to provide fire support
			if (pUnit->IsCanAttackRanged())
			{
				// Check if this position allows firing at enemies threatening the embarked units
				int iEnemyDist = testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA);
				if (iEnemyDist <= pUnit->GetRange() && iEnemyDist >= 2)
				{
					iResult += 5; // Good fire support position
				}
			}
			else
			{
				// Melee naval should be between enemies and embarked units
				int iEnemyDist = testPlot.getEnemyDistance(CvTacticalPlot::TD_SEA);
				if (iEnemyDist <= 2)
				{
					iResult += 4; // Screening position
				}
			}
		}
	}

	//try to occupy enemy citadels!
	int iImprovementDamage = TacticalAIHelpers::GetOtherPlayerImprovementDamage(pTestPlot, assumedPosition.getPlayer(), true);
	if (iImprovementDamage > 0)
		iResult += iImprovementDamage / 3;

	//also occupy our own citadels
	if (bIsFrontlineCitadelOrCity)
	{
		if (pUnit->GetRange() > 1 || testPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_LAND, -1)==0 || testPlot->getNumAdjacentEnemies(CvTacticalPlot::TD_LAND)>0)
			iResult += TACTICAL_COMBAT_CITADEL_BONUS;
		else
			iResult += TACTICAL_COMBAT_CITADEL_BONUS/2;
	}

	//try not to be a sitting duck (faster than isNativeDomain but not entirely accurate)
	if (pUnit->getDomainType() != pTestPlot->getDomain())
		iResult-=3;

	//sometimes danger is zero, but maybe we're wrong, so look at plot defense too
	iResult += pTestPlot->defenseModifier(pUnit->getTeam(),false,false) / 5;

	//todo: take into account mobility at the proposed plot
	//todo: take into account ZOC when ending the turn

	return iResult;
}

static STacticalAssignment* ScorePlotForCombatUnitMove(const SUnitStats& unit, const CvTacticalPlot* testPlot, const CvTacticalPosition& assumedPosition, eUnitMoveEvalMode evalMode) 
{
	//default action is do nothing and invalid score (not -INT_MAX, to prevent overflows!)
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex,testPlot->getPlotIndex(), unit.iUnitID, unit.iMovesLeft, unit.eMoveStrategy, A_MOVE, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	//the plot we're checking right now
	const CvPlot* pTestPlot = testPlot->getPlot();
	const CvUnit* pUnit = unit.pUnit;

	//different contributions
	int iDangerScore = 0;
	int iPlotScore = 0;
	int iBonusScore = 0;
	int iDamageDelta = 0;
	int iSelfDamage = 0;

	int iCurrentHealth = pUnit->GetCurrHitPoints() - unit.iSelfDamage;

	bool bMoving = unit.iPlotIndex != pTestPlot->GetPlotIndex();

	if (!bMoving)
		result->eAssignmentType = A_FINISH_TEMP;

	//ranged attacks are cross-domain
	CvTacticalPlot::eTactPlotDomain eRelevantDomain = pUnit->IsCanAttackRanged() ? CvTacticalPlot::TD_BOTH : pTestPlot->isWater() ? CvTacticalPlot::TD_SEA : CvTacticalPlot::TD_LAND;

	// Skirmishers should go to the front line when they can attack
	eUnitMovementStrategy eMoveStrategy = unit.eMoveStrategy;
	bool bSkirmisher = unit.iAttacksLeft > 0 && pUnit->GetRange() == 1 && (unit.iMovesLeft > GD_INT_GET(MOVE_DENOMINATOR) || (!pUnit->canMoveAfterAttacking() && unit.iMovesLeft > 0));
	if (bSkirmisher)
		eMoveStrategy = MS_FIRSTLINE;

	//zero to TACTICAL_COMBAT_MAX_TARGET_DISTANCE
	static const int iPlotScoreForEnemyDistanceLandAttack[5][5] = {
		{ -1,-1,-1,-1,-1 }, //none (should not occur)
		{ 12, 12, 6, 1, -1 }, //firstline (note that it's ok to evaluate the score in an enemy plot for a firstline unit -> meleekill) 
		{ -1, 8, 12, 2, -1 }, //secondline
		{  1, 1, 8, 12, -1 }, //thirdline (ranged and damaged melee units)
		{ -1, 1, 8,  8, -1 }, //support (can also happen for damaged melee units)
	};
	static const int iPlotScoreForEnemyDistanceSeaAttack[5][5] = {
		{ -1,-1,-1,-1,-1 }, //none (should not occur)
		{ 12, 8, 6, 1, -1 }, //firstline (note that it's ok to evaluate the score in an enemy plot for a firstline unit -> meleekill) 
		{ -1, 8, 8, 6, -1 }, //secondline
		{  1, 6, 8, 8, -1 }, //thirdline (ranged and damaged melee units)
		{ -1, 1, 8,  8, -1 }, //support (can also happen for damaged melee units)
	};
	static const int iPlotScoreForTargetDistanceEscort[5][5] = {
		{ -1,-1,-1,-1,-1 }, //none (should not occur)
		{  6, 6, 5, 3, 1 }, //firstline can go anywhere but outside when in doubt
		{  8, 8, 3, 2, 1 }, //secondline prefers "inside"
		{  10, 10, 2, 1, 1 }, //thirdline prefers inside even more
		{  8, 8, 2, 1, 1 }, //support (can also happen for damaged melee units)
	};

	//lookup desirability by unit strategy / enemy distance
	//even for intermediate plots, so as not to bias against them
	if (assumedPosition.haveEnemies())
	{
		int iEnemyDistance = !pUnit->IsCanAttackRanged() || pUnit->IsRangeAttackIgnoreLOS() || pUnit->GetRange() < 2
			? testPlot->getEnemyDistance(eRelevantDomain)
			: testPlot->getRangedAttackEnemyDistance(eRelevantDomain);

		iPlotScore = pUnit->getDomainType() != DOMAIN_SEA
			? iPlotScoreForEnemyDistanceLandAttack[eMoveStrategy][iEnemyDistance]
			: iPlotScoreForEnemyDistanceSeaAttack[eMoveStrategy][iEnemyDistance];

		if (pTestPlot->isFriendlyCity(*pUnit))
			iPlotScore = 12;
		// wounded units and scouts can go wherever as long as they don't die (danger checked later)
		else if (iCurrentHealth < gMinHpForTactsim || pUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE)
			iPlotScore = 12;

		// move skirmishers in to fire
		if (bSkirmisher && iEnemyDistance == 1)
			iPlotScore += 3;

		// if we are not in a good spot, try moving
		if (iPlotScore <= 6 && pTestPlot == pUnit->plot() && pUnit->getDamage() + unit.iSelfDamage < 10)
			iPlotScore -= 2;

		// Move melee units in to capture cities
		if (!pUnit->IsCanAttackRanged() && testPlot->getEnemyDistance() == 1)
		{
			CvCity* pAdjacentCity = pTestPlot->GetAdjacentCity();
			if (pAdjacentCity && pAdjacentCity->GetMaxHitPoints() - pAdjacentCity->getDamage() <= 5 && GET_PLAYER(pAdjacentCity->getOwner()).IsAtWarWith(pUnit->getOwner()))
				iPlotScore += 5;
		}

		//if we made a kill, assume we are in a good plot
		//this is very important because enemy distance changes and we might end up with -1
		if (isKillAssignment(unit.eLastAssignment))
			iPlotScore = max(6, iPlotScore);
	}
	else
	{
		//we have no enemies around prepare for the unexpected, melee units screening far from the target, other units closer
		int iTargetDistance = min(plotDistance(*assumedPosition.getTarget(), *pTestPlot), (int)TACTICAL_COMBAT_MAX_TARGET_DISTANCE);

		iPlotScore += iPlotScoreForTargetDistanceEscort[eMoveStrategy][iTargetDistance];
	}

	// === COMBAT BONUS IMPROVEMENT AWARENESS (Shoshone Encampment, etc.) ===
	if (pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		int iNearbyImprovementBonus = pUnit->GetNearbyImprovementModifier(pTestPlot);
		if (iNearbyImprovementBonus > 0)
		{
			iPlotScore += iNearbyImprovementBonus / 3;
			if (assumedPosition.haveEnemies() && testPlot->getEnemyDistance(eRelevantDomain) <= 3)
				iPlotScore += iNearbyImprovementBonus / 5;
		}
	}

	if (bMoving && testPlot->isEnemyCivilian()) //unescorted civilian
	{
		if (unit.iMovesLeft > 0 || testPlot->getNumAdjacentEnemies(CvTacticalPlot::TD_LAND) == 0)
		{
			CvUnit* pCivilian;
			for (int iI = 0; iI < pTestPlot->getNumUnits(); iI++)
			{
				pCivilian = pTestPlot->getUnitByIndex(iI);
				if (pCivilian && pCivilian->IsCivilianUnit() && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pCivilian->getOwner()))
				{
					//workers are not so important ...
					iBonusScore += (pCivilian->AI_getUnitAIType() == UNITAI_WORKER) ? 20 : 150;
					result->eAssignmentType = A_CAPTURE; //important so that the next assignment can be a move again
				}
			}
		}
	}

	// empty barbarian camp
	if (bMoving && pTestPlot->getRevealedImprovementType(pUnit->getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
	{
		if (!testPlot->isEnemyCombatUnit())
		{
			if (assumedPosition.isEarlyFinish(true))
				iPlotScore = 12;
			iBonusScore += 100;
		}
	}

	//give a bonus for potential fortifying/healing
	if (result->eAssignmentType == A_FINISH_TEMP)
	{
		if (unit.iMovesLeft == unit.iMaxMoves)
		{
			if (pUnit->getDamage() + unit.iSelfDamage > 0 && !pUnit->IsCannotHeal(/*bConsiderResourceShortage*/ true) && !pUnit->isEmbarked())
			{
				int iHealRate = pUnit->ActualHealRate(pTestPlot, false);

				if (iHealRate > 0)
				{
					if (iHealRate > pUnit->getDamage() + unit.iSelfDamage)
						iHealRate = pUnit->getDamage() + unit.iSelfDamage;

					iDamageDelta += iHealRate;
					iSelfDamage -= iHealRate;

					result->eAssignmentType = A_HEAL;
				}
			}
			if (pUnit->IsEverFortifyable() && !pUnit->isEmbarked())
			{
				// This happens at the beginning of our next turn, so we can't assume enemy units will stick around, and we won't deal any damage this turn
				if (pUnit->GetDamageAoEFortified() > 0)
					iBonusScore += testPlot->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH) * pUnit->GetDamageAoEFortified() / 3;

				if (unit.eMoveStrategy == MS_FIRSTLINE)
					iPlotScore += 1;
			}
		}
		else
		{
			if (pUnit->getDamage() + unit.iSelfDamage > 0 && !pUnit->IsCannotHeal(/*bConsiderResourceShortage*/ true) && !pUnit->isEmbarked())
			{
				if (pUnit->isAlwaysHeal())
				{
					int iHealRate = pUnit->ActualHealRate(pTestPlot, false);
					if (iHealRate > pUnit->getDamage() + unit.iSelfDamage)
						iHealRate = pUnit->getDamage() + unit.iSelfDamage;

					iSelfDamage -= iHealRate;
				}
				else if (pUnit->GetFlatHealRate() > 0)
				{
					int iHealRate = pUnit->GetFlatHealRate();
					if (iHealRate > pUnit->getDamage() + unit.iSelfDamage)
						iHealRate = pUnit->getDamage() + unit.iSelfDamage;

					iSelfDamage -= iHealRate;
				}
			}
		}

		int iCitadelDamage = testPlot->GetAdjacentImprovementDamage();
		if (iCitadelDamage > 0)
		{
			int iDamageTaken = min(iCurrentHealth, iCitadelDamage);

			iDamageDelta -= iDamageTaken;
			iSelfDamage += iDamageTaken;
		}

		// unit giving extra strength to an adjacent city?
		if (pUnit->GetAdjacentCityDefenseMod() > 0 && pTestPlot->IsAdjacentCity(pUnit->getTeam()))
			iPlotScore++;
	}

	// aoe damage on move
	if (bMoving && pUnit->getAoEDamageOnMove() > 0)
		DamageAdjacentUnits(result->unitDamage, iDamageDelta, pTestPlot, pUnit->getAoEDamageOnMove(), assumedPosition);

	// assume difference in danger from AoE damage on move is negligible (it's definitely not worth the CPU time)
	int iDanger = GetUnitDangerForPlot(pUnit, pTestPlot, unit.iSelfDamage + iSelfDamage, assumedPosition);

	//many considerations are only relevant if we end the turn here (critical for skirmishers which can move after attacking ...)
	//we only consider this when explicitly ending the turn!
	if (evalMode != EM_INTERMEDIATE)
	{
		result->eAssignmentType = evalMode == EM_FINAL ? A_FINISH : A_INITIAL;

		iDangerScore = ScoreCombatUnitTurnEnd(pUnit, unit.eLastAssignment, testPlot, iDanger,
			eRelevantDomain, unit.iSelfDamage + iSelfDamage, assumedPosition, evalMode, false, false);

		if (iDangerScore == INT_MAX)
		{
			if (gSafePlotCount[unit.iUnitID] > 0)
				return result; //don't do it

			//unit has no safe plots and must end turn somewhere (EM_FINAL)
			//clamp the sentinel to worst-possible arithmetic-safe value
			iDangerScore = SHRT_MIN;
		}
	}
	else
	{
		iDangerScore = -iDanger / 2;

		//scale it up a bit to reduce truncation error during division
		int iOverkillPercent = (100*iDanger) / max(1, iCurrentHealth);
		iDangerScore -= iOverkillPercent / 20;

		//there is a tendency for fast units to move too far ahead without a chance to withdraw later
		//so try to catch this case right here, cannot wait until the end of the sim when it's too late
		//if (iOverkillPercent > 300 && unit.iMovesLeft < 3 * GD_INT_GET(MOVE_DENOMINATOR) && unit.iMovesLeft > 3 * GD_INT_GET(MOVE_DENOMINATOR))
		//	return result; //don't do it

		//give a bonus for occupying a citadel even if it's just intermediate for now
		//but we want our units to take turns soaking damage, so we have to incentivise moving in.
		//bonus should be larger than 60 to override the difference between firstline/secondline base score.
		bool bIsFrontlineCitadelOrCity = (TacticalAIHelpers::IsPlayerCitadel(pTestPlot, assumedPosition.getPlayer()) || pTestPlot->isCity()) && pUnit->getDomainType() == DOMAIN_LAND && testPlot->getEnemyDistance() < 3;
		if (bIsFrontlineCitadelOrCity)
		{
			if (pUnit->GetRange()>1 || testPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_LAND, unit.iPlotIndex) == 0 || testPlot->getNumAdjacentEnemies(CvTacticalPlot::TD_LAND) > 0)
	 			iDangerScore += TACTICAL_COMBAT_CITADEL_BONUS;
			else
				iDangerScore += TACTICAL_COMBAT_CITADEL_BONUS/2;
		}
		else if (TacticalAIHelpers::GetOtherPlayerImprovementDamage(pTestPlot, assumedPosition.getPlayer(), true) >= 25)
		{
			if (unit.iMovesLeft > 0) //can pillage this turn
				iDangerScore += TACTICAL_COMBAT_CITADEL_BONUS*2;
			else
				iDangerScore += TACTICAL_COMBAT_CITADEL_BONUS;
		}
	}

	// Check if we are moving into an improvement that restores our moves
	if (bMoving && pTestPlot->getOwner() == pUnit->getOwner() && testPlot->getPlotIndex() != unit.iPlotIndex && pTestPlot->IsRestoreMoves())
	{
		if (evalMode == EM_INTERMEDIATE)
			iBonusScore += unit.iMaxMoves - unit.iMovesLeft;
		result->iRemainingMoves = pUnit->baseMoves(false);
	}

	// === DEFENSIVE POSITIONING IMPROVEMENTS ===
	// When defending friendly territory, use terrain and positioning smartly
	{
		const CvPlot* pDefTarget = assumedPosition.getTarget();
		bool bDefendingFriendlyCity = pDefTarget && pDefTarget->isCity() && pDefTarget->isFriendlyCity(*pUnit);

		if (bDefendingFriendlyCity && pTestPlot->IsFriendlyTerritory(pUnit->getOwner()))
		{
			CvCity* pFriendlyCity = pDefTarget->getPlotCity();
			int iDistToCity = plotDistance(*pTestPlot, *pDefTarget);

			// 1. Defensive terrain bonus - prefer hills, forests for defensive bonuses
			if (!pUnit->IsCanAttackRanged() || pUnit->GetRange() == 1) // melee and skirmishers benefit most
			{
				int iDefMod = pTestPlot->defenseModifier(pUnit->getTeam(), false, false);
				if (iDefMod > 0)
					iPlotScore += iDefMod / 10; // +2 for 20% defense, +5 for 50% defense
			}

			// 2. Chokepoint bonus for melee units - block enemy advance routes
			if (!pUnit->IsCanAttackRanged() && pTestPlot->IsChokePoint())
				iPlotScore += 8;

			// 3. Wounded unit rotation - heavily wounded units should fall back
			int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
			if (iHealthPercent < 40)
			{
				int iEnemyDist = testPlot->getEnemyDistance(eRelevantDomain);
				if (iEnemyDist >= 2)
					iPlotScore += 4;
				else if (iEnemyDist <= 1 && !pTestPlot->isCity())
					iPlotScore -= 6;
			}

			// 3b. WOUNDED UNITS SEEKING MEDIC SUPPORT
			if (pUnit->getDamage() > 0)
			{
				int iDamagePercent = (pUnit->getDamage() * 100) / pUnit->GetMaxHitPoints();
				int iBestMedicHeal = 0;
				int iMedicsNearby = 0;

				const vector<STacticalUnit>& unitsHere = testPlot->getUnitsAtPlot();
				for (size_t i = 0; i < unitsHere.size(); i++)
				{
					CvUnit* pStackedUnit = GET_PLAYER(assumedPosition.getPlayer()).getUnit(unitsHere[i].iUnitID);
					if (pStackedUnit && pStackedUnit != pUnit && pStackedUnit->getSameTileHeal() > 0)
					{
						iBestMedicHeal = max(iBestMedicHeal, pStackedUnit->getSameTileHeal());
						iMedicsNearby++;
					}
				}

				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (pAdj)
					{
						CvUnit* pAdjUnit = pAdj->getBestDefender(pUnit->getOwner());
						if (pAdjUnit && pAdjUnit->getAdjacentTileHeal() > 0)
						{
							iBestMedicHeal = max(iBestMedicHeal, pAdjUnit->getAdjacentTileHeal());
							iMedicsNearby++;
						}
					}
				}

				if (iMedicsNearby > 0)
				{
					int iMedicBonus = 3;
					if (iDamagePercent >= 60)
						iMedicBonus += 6;
					else if (iDamagePercent >= 40)
						iMedicBonus += 4;
					else if (iDamagePercent >= 20)
						iMedicBonus += 2;
					iMedicBonus += iBestMedicHeal / 5;
					if (iMedicsNearby >= 2)
						iMedicBonus += 2;
					iPlotScore += min(iMedicBonus, 15);
				}
			}

			// 4. City proximity bonus when city is threatened
			if (pFriendlyCity && (pFriendlyCity->isUnderSiege() || pFriendlyCity->isInDangerOfFalling()))
			{
				if (iDistToCity <= 2)
					iPlotScore += 6;
				else if (iDistToCity <= 3)
					iPlotScore += 3;
				else if (iDistToCity > 4)
					iPlotScore -= 4;
			}

			// 5. Melee screening for ranged - melee should position between ranged and enemies
			if (!pUnit->IsCanAttackRanged())
			{
				int iAdjacentFriendlyRanged = 0;
				int iAdjacentFriendlySiege = 0;
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (pAdj)
					{
						CvUnit* pAdjUnit = pAdj->getBestDefender(pUnit->getOwner());
						if (pAdjUnit && pAdjUnit->IsCanAttackRanged() && pAdjUnit->GetRange() > 1)
						{
							iAdjacentFriendlyRanged++;
							if (pAdjUnit->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
								iAdjacentFriendlySiege++;
						}
					}
				}
				if (iAdjacentFriendlyRanged > 0 && testPlot->getEnemyDistance(eRelevantDomain) <= 2)
				{
					iPlotScore += iAdjacentFriendlyRanged * 3;

					// WITHDRAWAL PENALTY FOR SCREENING: Units with withdrawal should NOT screen fragile units
					int iWithdrawalChance = pUnit->withdrawalProbability();
					if (iWithdrawalChance > 0)
					{
						int iWithdrawPenalty = (iWithdrawalChance / 10) * iAdjacentFriendlyRanged;
						if (iAdjacentFriendlySiege > 0)
							iWithdrawPenalty += (iWithdrawalChance / 10) * iAdjacentFriendlySiege;
						iPlotScore -= iWithdrawPenalty;
					}
				}
			}

			// === COUNTER-ENEMY TERRAIN BONUSES ===
			// Avoid fighting enemies in their preferred terrain (e.g. Iroquois Woodsman in forests)
			{
				FeatureTypes eFeature = pTestPlot->getFeatureType();
				bool bHasFeature = (eFeature != NO_FEATURE);
				bool bIsRoughGroundCT = pTestPlot->isRoughGround();

				if (bHasFeature || bIsRoughGroundCT)
				{
					int iEnemyTerrainBonus = 0;
					int iEnemiesWithBonus = 0;

					for (int i = RING0_PLOTS; i < RING2_PLOTS; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (pLoopPlot)
						{
							CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
							if (pEnemy && pEnemy->IsCombatUnit() && !pEnemy->IsCanAttackRanged())
							{
								int iEnemyRoughBonus = pEnemy->roughAttackModifier();
								int iEnemyRoughFromBonus = pEnemy->getExtraRoughFromPercent();
								int iEnemyFeatureBonus = bHasFeature ? pEnemy->featureAttackModifier(eFeature) : 0;

								int iTotalEnemyBonus = 0;
								if (bIsRoughGroundCT)
								{
									iTotalEnemyBonus += iEnemyRoughBonus;
									iTotalEnemyBonus += iEnemyRoughFromBonus;
								}
								iTotalEnemyBonus += iEnemyFeatureBonus;

								if (iTotalEnemyBonus > 0)
								{
									iEnemyTerrainBonus = max(iEnemyTerrainBonus, iTotalEnemyBonus);
									iEnemiesWithBonus++;
								}
							}
						}
					}

					if (iEnemyTerrainBonus > 0)
					{
						iPlotScore -= iEnemyTerrainBonus / 3;
						if (iEnemiesWithBonus >= 2)
							iPlotScore -= iEnemyTerrainBonus / 5;
					}
				}
			}
		}
	}

	// === TERRAIN AWARENESS FOR MOUNTED/FAST UNITS ===
	// Units with open terrain bonuses should prefer open ground for defense
	// Units with rough terrain bonuses should prefer rough ground
	// This applies to cavalry, knights, elephants, camels and units with relevant promotions
	if (!pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		bool bIsOpenGround = pTestPlot->isOpenGround();
		bool bIsRoughGround = pTestPlot->isRoughGround();
		
		// Check defensive terrain bonuses - important for where to end turn
		int iOpenDefBonus = pUnit->openDefenseModifier();
		int iRoughDefBonus = pUnit->roughDefenseModifier();
		
		if (iOpenDefBonus != 0 || iRoughDefBonus != 0)
		{
			// Unit has terrain-specific defense bonuses
			if (bIsOpenGround && iOpenDefBonus > 0)
			{
				// Bonus for defending on open terrain when unit has open defense bonus
				iPlotScore += iOpenDefBonus / 5; // +2 to +6 typically
			}
			else if (bIsRoughGround && iRoughDefBonus > 0)
			{
				// Bonus for defending on rough terrain when unit has rough defense bonus
				iPlotScore += iRoughDefBonus / 5;
			}
			else if (bIsRoughGround && iOpenDefBonus > 0)
			{
				// Penalty for cavalry-type unit ending turn on rough terrain
				// They lose their open terrain bonus and have no defensive bonus
				iPlotScore -= iOpenDefBonus / 8; // mild penalty
			}
			else if (bIsOpenGround && iRoughDefBonus > 0)
			{
				// Penalty for rough-terrain unit (e.g., Jaguar) ending on open ground
				iPlotScore -= iRoughDefBonus / 8;
			}
		}
		
		// Also consider noDefensiveBonus flag (mounted units without special promotions)
		// These units should avoid rough terrain when possible as they get no defense bonus there
		if (pUnit->noDefensiveBonus())
		{
			int iTerrainDefMod = pTestPlot->defenseModifier(pUnit->getTeam(), false, false);
			if (iTerrainDefMod > 0 && bIsRoughGround)
			{
				// Unit with noDefensiveBonus is on terrain that would give defense bonus
				// but they can't use it - mild penalty for not utilizing terrain
				iPlotScore -= 2;
			}
			else if (bIsOpenGround)
			{
				// Open ground is neutral for noDefensiveBonus units
				// At least they're not wasting potential defense bonuses
				iPlotScore += 1;
			}
		}
		
		// === FEATURE-SPECIFIC TERRAIN PROMOTIONS (Woodsman, etc.) ===
		// Units with promotions like Woodsman get bonuses in specific features (forest/jungle)
		// They should prefer positioning in these features for defense and to enable attacks
		FeatureTypes eFeature = pTestPlot->getFeatureType();
		if (eFeature != NO_FEATURE)
		{
			// Check if unit has defense bonus in this feature
			int iFeatureDefBonus = pUnit->featureDefenseModifier(eFeature);
			if (iFeatureDefBonus > 0)
			{
				// Unit has specific defense bonus in this feature (e.g., Woodsman in forest)
				// This is different from the generic rough terrain bonus
				iPlotScore += iFeatureDefBonus / 4; // +5-12 typically
				
				// Extra bonus when enemies are nearby (we'll be attacked here)
				if (testPlot.getEnemyDistance(eRelevantDomain) <= 2)
					iPlotScore += iFeatureDefBonus / 6;
			}
			
			// Check if unit has attack bonus from this feature (RoughFromMod)
			// Units with this bonus fight better when attacking FROM this feature
			int iFeatureAttackFromBonus = pUnit->getExtraRoughFromPercent();
			if (iFeatureAttackFromBonus > 0 && bIsRoughGround)
			{
				// Unit has attack bonus when attacking FROM rough terrain (Woodsman)
				// Position in forests/jungle to enable this bonus on attacks
				if (testPlot.getEnemyDistance(eRelevantDomain) <= 2)
				{
					iPlotScore += iFeatureAttackFromBonus / 3; // +3-10 for positioning to attack
				}
			}
		}
		
		// === FLANKING POSITIONING FOR MELEE UNITS ===
		// Units with high FlankAttackModifier should position to enable flanking attacks
		int iFlankMod = pUnit->GetFlankAttackModifier();
		if (iFlankMod >= 15 && testPlot.getEnemyDistance(eRelevantDomain) <= 2)
		{
			// Count enemies this plot is adjacent to (potential flanking targets)
			int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
			
			if (iAdjacentEnemies > 0)
			{
				// We're adjacent to enemies - good flanking position if friendlies are also nearby
				int iAdjacentFriendlies = testPlot.getNumAdjacentFriendlies(eRelevantDomain, unit.iPlotIndex);
				
				// Bonus for being in position to flank with friendlies
				if (iAdjacentFriendlies > 0)
				{
					// Good flanking position - we can support friendlies or be supported
					iPlotScore += 2 + (iFlankMod / 15); // +3 to +4 for high flank modifier
				}
				
				// Extra bonus if multiple enemies are adjacent (cavalry charge opportunity)
				if (iAdjacentEnemies >= 2 && iFlankMod >= 25)
				{
					iPlotScore += 2; // Cavalry likes targets of opportunity
				}
			}
		}
		
		// === SPOTTER/OBSERVER FOR INDIRECT FIRE UNITS (Artillery, Battleships, etc.) ===
		// Fast and recon units should position to provide sight for indirect fire units
		// Indirect fire units (IsRangeAttackIgnoreLOS) can attack over obstacles if they have sight
		// This gives valuable tactical advantage - artillery can fire over mountains, forests, etc.
		// 
		// Key units that benefit: Artillery, Rocket Artillery, Battleship, Missile Cruiser
		// Good spotters: Scouts, Cavalry, Helicopters, Destroyers, Fighters doing recon
		if (assumedPosition.haveEnemies())
		{
			int iUnitRange = pUnit->GetRange();
			int iUnitSight = pUnit->visibilityRange();
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			
			// Check if this unit could be a good spotter (fast or recon type)
			bool bIsGoodSpotter = false;
			bool bIsIndirectFireUnit = pUnit->IsRangeAttackIgnoreLOS();
			
			// Good spotters: fast units, recon, aircraft doing sweeps
			if (!bIsIndirectFireUnit)
			{
				switch (pUnit->AI_getUnitAIType())
				{
				case UNITAI_EXPLORE:
				case UNITAI_FAST_ATTACK:
				case UNITAI_SKIRMISHER:
				case UNITAI_ATTACK_SEA:
				case UNITAI_SUBMARINE: // Subs can spot for surface ships
					bIsGoodSpotter = true;
					break;
				default:
					// Fast units with good sight are also decent spotters
					if (pUnit->baseMoves(false) >= 4 || iUnitSight >= 3)
						bIsGoodSpotter = true;
					break;
				}
			}
			
			if (bIsGoodSpotter)
			{
				// Count friendly indirect fire units that could benefit from our spotting
				int iFriendlyIndirectFireUnits = 0;
				int iFriendlyIndirectFireRange = 0;
				CvPlot* pBestIndirectFirePlot = NULL;
				
				// Search for friendly indirect fire units within reasonable range
				for (int iRing = 1; iRing <= 5; iRing++)
				{
					for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (!pLoopPlot)
							continue;
						
						CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pFriendly && pFriendly->IsCanAttackRanged() && pFriendly->IsRangeAttackIgnoreLOS())
						{
							iFriendlyIndirectFireUnits++;
							if (pFriendly->GetRange() > iFriendlyIndirectFireRange)
							{
								iFriendlyIndirectFireRange = pFriendly->GetRange();
								pBestIndirectFirePlot = pLoopPlot;
							}
						}
					}
				}
				
				// If we have indirect fire allies, prioritize positions that provide sight to enemies
				if (iFriendlyIndirectFireUnits > 0)
				{
					// Count enemies we can see from this position that indirect fire could target
					int iEnemiesWeCanSpot = 0;
					int iEnemiesBehindObstacles = 0;
					
					// Check enemies within our sight range
					for (int iRing = 1; iRing <= min(iUnitSight, 3); iRing++)
					{
						for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
						{
							CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
							if (!pLoopPlot)
								continue;
							
							// Can we see this plot from our test position?
							if (!pTestPlot->canSeePlot(pLoopPlot, pUnit->getTeam(), iUnitSight, NO_DIRECTION))
								continue;
							
							CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
							if (pEnemy && pEnemy->IsCombatUnit())
							{
								iEnemiesWeCanSpot++;
								
								// Check if this enemy is behind an obstacle from the indirect fire unit
								// (i.e., would the indirect fire unit need spotting to hit this target?)
								if (pBestIndirectFirePlot)
								{
									int iDistFromArtillery = plotDistance(*pBestIndirectFirePlot, *pLoopPlot);
									if (iDistFromArtillery <= iFriendlyIndirectFireRange)
									{
										// Enemy is in range of our artillery
										// Check if there's an obstacle between artillery and target
										if (!pBestIndirectFirePlot->canSeePlot(pLoopPlot, pUnit->getTeam(), iFriendlyIndirectFireRange, NO_DIRECTION))
										{
											// Artillery CANNOT see this target - we would be providing critical spotting!
											iEnemiesBehindObstacles++;
										}
									}
								}
							}
						}
					}
					
					// Bonus for spotting enemies that artillery can hit
					if (iEnemiesWeCanSpot > 0)
					{
						iPlotScore += iEnemiesWeCanSpot * 2;
						
						// BIG bonus for spotting enemies that are behind obstacles!
						// This is the key value of spotters - enabling indirect fire over mountains/forests
						if (iEnemiesBehindObstacles > 0)
						{
							iPlotScore += iEnemiesBehindObstacles * 8; // Major bonus
							iPlotScore += iFriendlyIndirectFireUnits * 3; // More bonus per indirect fire unit
						}
					}
					
					// Spotters should stay safe enough to maintain sight
					// Mild penalty for getting too close to enemies (might die)
					if (iEnemyDist == 1 && testPlot.getNumAdjacentEnemies(eRelevantDomain) >= 2)
					{
						iPlotScore -= 4; // Don't get surrounded while spotting
					}
				}
			}
			
			// === INDIRECT FIRE UNIT POSITIONING ===
			// Artillery and battleships should consider spotter availability
			if (bIsIndirectFireUnit)
			{
				// Check for friendly spotters that can extend our effective range
				int iFriendlySpotters = 0;
				int iBestSpotterSight = 0;
				
				for (int iRing = 1; iRing <= min(iUnitRange + 2, 5); iRing++)
				{
					for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (!pLoopPlot)
							continue;
						
						CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pFriendly && pFriendly != pUnit && !pFriendly->IsRangeAttackIgnoreLOS())
						{
							// Check if this unit could spot for us
							int iFriendlySight = pFriendly->visibilityRange();
							if (iFriendlySight >= 2)
							{
								iFriendlySpotters++;
								if (iFriendlySight > iBestSpotterSight)
									iBestSpotterSight = iFriendlySight;
							}
						}
					}
				}
				
				// Bonus for having spotters nearby
				if (iFriendlySpotters > 0)
				{
					iPlotScore += 3; // Base bonus for having spotter support
					iPlotScore += min(iFriendlySpotters, 3) * 2; // +2 per spotter, max +6
					
					// Extra bonus if spotters can see further than we can
					if (iBestSpotterSight > iUnitSight)
					{
						iPlotScore += 4; // Good coordination - spotters extend our reach
					}
				}
				
				// Artillery with indirect fire can position behind obstacles
				// Check if this plot is protected by terrain from enemy sight
				if (pTestPlot->isHills() || pTestPlot->isMountain() || 
					pTestPlot->getFeatureType() == FEATURE_FOREST || 
					pTestPlot->getFeatureType() == FEATURE_JUNGLE)
				{
					// Protected position for indirect fire
					if (iEnemyDist <= iUnitRange && iEnemyDist >= 2)
					{
						iPlotScore += 5; // Good defilade position - can fire but harder to counter
					}
				}
			}
		}
		
		// === MODERN ARMOR (TANK) POSITIONING ===
		// Tanks (high-strength fast melee) use different tactics than cavalry:
		// - Lead assaults as spearhead (not flanking)
		// - Need infantry support for combined arms
		// - Position for breakthrough attacks on defensive lines
		// - Can absorb damage, so less concerned about escape routes
		int iOurStrength = pUnit->GetBaseCombatStrength();
		bool bIsModernArmor = (pUnit->baseMoves(false) >= 4 && iOurStrength >= 60);
		
		if (bIsModernArmor && assumedPosition.haveEnemies())
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			int iAdjacentFriendlies = testPlot.getNumAdjacentFriendlies(eRelevantDomain, unit.iPlotIndex);
			
			// 1. SPEARHEAD POSITIONING: Tanks should lead the assault
			// Position at the front of friendly formations
			if (iEnemyDist <= 2)
			{
				// Count friendly non-tank melee nearby (infantry support)
				int iFriendlyInfantry = 0;
				for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pFriendly && !pFriendly->IsCanAttackRanged() &&
							pFriendly->baseMoves(false) <= 3 &&
							pFriendly->GetBaseCombatStrength() < iOurStrength)
						{
							iFriendlyInfantry++;
						}
					}
				}
				
				// Bonus for having infantry support (combined arms)
				if (iFriendlyInfantry >= 2)
				{
					iPlotScore += 10; // Excellent combined arms position
				}
				else if (iFriendlyInfantry == 1)
				{
					iPlotScore += 5;
				}
				
				// Tanks are happier on the front line than cavalry
				// They can absorb hits and break through
				if (iEnemyDist == 1)
				{
					// Adjacent to enemy - assault position
					// Good for tanks, unlike cavalry
					iPlotScore += 4;
					
					// Extra bonus for having support
					if (iAdjacentFriendlies >= 2)
						iPlotScore += 4;
				}
			}
			
			// 2. BREAKTHROUGH CORRIDOR: Position toward enemy cities
			const CvPlot* pTarget = assumedPosition.getTarget();
			if (pTarget && pTarget->isCity())
			{
				CvCity* pTargetCity = pTarget->getPlotCity();
				if (pTargetCity && GET_PLAYER(assumedPosition.getPlayer()).IsAtWarWith(pTargetCity->getOwner()))
				{
					int iDistToCity = plotDistance(*pTestPlot, *pTarget);
					
					// Tanks should push toward the objective
					if (iDistToCity <= 3)
					{
						iPlotScore += 8; // Close to objective
					}
					else if (iDistToCity <= 5)
					{
						iPlotScore += 4;
					}
				}
			}
			
			// 3. AVOID TERRAIN THAT NEGATES ARMOR ADVANTAGE
			// Tanks dislike rough terrain even more than cavalry
			// But for different reasons: reduces mobility, not defense
			if (pTestPlot->isRoughGround())
			{
				// Tanks struggle in rough terrain
				iPlotScore -= 3;
				
				// Worse if near enemies (can't maneuver)
				if (iEnemyDist <= 2)
					iPlotScore -= 2;
			}
			else if (pTestPlot->isOpenGround())
			{
				// Open terrain - tank country
				iPlotScore += 4;
				
				// Extra bonus for open terrain near enemies (can exploit mobility)
				if (iEnemyDist <= 2)
					iPlotScore += 2;
			}
			
			// 4. MASS CONCENTRATION: Tanks work best in groups
			// Unlike cavalry which spreads out for flanking
			if (iAdjacentFriendlies >= 2)
			{
				// Good formation - mutual support
				iPlotScore += 4;
			}
			else if (iAdjacentFriendlies == 0 && iEnemyDist <= 2)
			{
				// Isolated tank near enemies - vulnerable
				iPlotScore -= 4;
			}
		}
		
		// === SLOW INFANTRY POSITIONING (Spearmen, Swordsmen, Pikemen, Longswordsmen, etc.) ===
		// Slow melee infantry (2 moves) use fundamentally different tactics than fast cavalry:
		// - Form defensive lines and hold positions
		// - Use terrain defense bonuses heavily (unlike cavalry)
		// - Anti-cavalry units (spearmen/pikemen) should intercept mounted threats
		// - Mainline infantry (swordsmen) are versatile front-line fighters
		// Detection: Non-ranged, 2 moves or less, not a tank
		bool bIsSlowInfantry = (!pUnit->IsCanAttackRanged() && pUnit->baseMoves(false) <= 2 && !bIsModernArmor);
		
		if (bIsSlowInfantry && assumedPosition.haveEnemies())
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
			int iAdjacentFriendlies = testPlot.getNumAdjacentFriendlies(eRelevantDomain, unit.iPlotIndex);
			
			// Check for anti-cavalry bonus (spearmen/pikemen have bonus vs UNITCOMBAT_MOUNTED)
			static UnitCombatTypes eMountedCombat = (UnitCombatTypes)GC.getInfoTypeForString("UNITCOMBAT_MOUNTED");
			int iAntiCavalryBonus = 0;
			if (eMountedCombat != NO_UNITCOMBAT)
			{
				iAntiCavalryBonus = pUnit->unitCombatModifier(eMountedCombat);
			}
			
			bool bIsAntiCavalry = (iAntiCavalryBonus >= 25); // Spearman typically has +50% vs mounted
			
			// 1. ANTI-CAVALRY INTERCEPTION POSITIONING
			// Spearmen/pikemen should position to intercept enemy cavalry
			if (bIsAntiCavalry)
			{
				// Check for enemy cavalry nearby that we can intercept
				int iEnemyCavalryNearby = 0;
				int iClosestCavalryDist = INT_MAX;
				
				for (int iRing = 1; iRing <= 3; iRing++)
				{
					for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (pLoopPlot)
						{
							CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
							if (pEnemy && pEnemy->IsCombatUnit() && !pEnemy->IsCanAttackRanged())
							{
								// Check if this is a mounted unit we counter
								UnitCombatTypes eEnemyCombat = pEnemy->getUnitCombatType();
								if (eEnemyCombat == eMountedCombat)
								{
									iEnemyCavalryNearby++;
									int iDistToCav = plotDistance(*pTestPlot, *pLoopPlot);
									if (iDistToCav < iClosestCavalryDist)
										iClosestCavalryDist = iDistToCav;
								}
							}
						}
					}
				}
				
				// Bonus for positioning to intercept cavalry
				if (iEnemyCavalryNearby > 0)
				{
					// Strong bonus for being in interception range of cavalry
					iPlotScore += 10;
					
					// Extra bonus if we're close enough to be attacked (we want that!)
					if (iClosestCavalryDist <= 2)
					{
						iPlotScore += 8; // Bait position - cavalry may attack us
					}
					
					// Bonus per cavalry we can threaten
					iPlotScore += iEnemyCavalryNearby * 3;
				}
				
				// Anti-cavalry units should protect ranged units from cavalry charges
				// Position between friendly ranged and enemy cavalry
				if (iEnemyCavalryNearby > 0)
				{
					int iAdjacentFriendlyRanged = 0;
					for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
					{
						CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
						if (pAdj)
						{
							CvUnit* pFriendly = pAdj->getBestDefender(pUnit->getOwner());
							if (pFriendly && pFriendly->IsCanAttackRanged())
							{
								iAdjacentFriendlyRanged++;
							}
						}
					}
					
					// Strong bonus for screening ranged from cavalry
					if (iAdjacentFriendlyRanged > 0)
					{
						iPlotScore += iAdjacentFriendlyRanged * 5;
					}
				}
			}
			
			// 2. TERRAIN DEFENSE: Slow infantry LOVE defensive terrain
			// Unlike cavalry, infantry can fully utilize terrain bonuses
			int iTerrainDefMod = pTestPlot->defenseModifier(pUnit->getTeam(), false, false);
			if (iTerrainDefMod > 0)
			{
				// Strong bonus for defensive terrain (hills, forests)
				iPlotScore += iTerrainDefMod / 4; // +5 for 20%, +12 for 50%
				
				// Extra bonus when enemies are close (we'll be attacked here)
				if (iEnemyDist <= 2)
					iPlotScore += iTerrainDefMod / 6;
			}
			
			// Penalty for open ground (no terrain defense)
			if (pTestPlot->isOpenGround() && iEnemyDist <= 2)
			{
				iPlotScore -= 4; // Exposed without terrain advantage
			}
			
			// 3. FORMATION FIGHTING: Infantry is strongest in groups
			// Mutual support and combined strength
			if (iAdjacentFriendlies >= 2)
			{
				// Good formation - infantry phalanx/shield wall
				iPlotScore += 6;
				
				// Extra bonus for being part of a larger formation
				if (iAdjacentFriendlies >= 3)
					iPlotScore += 3;
			}
			else if (iAdjacentFriendlies == 0 && iEnemyDist <= 2)
			{
				// Isolated infantry is vulnerable
				iPlotScore -= 6;
			}
			
			// 4. HOLD THE LINE: Infantry should maintain front-line positions
			// They don't have the mobility to reposition quickly
			if (iEnemyDist == 1 && iAdjacentEnemies > 0)
			{
				// On the front line - this is where infantry belongs
				// But only if we have support
				if (iAdjacentFriendlies >= 1)
				{
					iPlotScore += 4; // Good defensive position with support
				}
				else
				{
					iPlotScore -= 4; // Exposed on front line alone
				}
			}
			else if (iEnemyDist == 2)
			{
				// One tile back - reserve position
				// Good if there are friendlies on the front line we can support
				if (iAdjacentEnemies == 0 && iAdjacentFriendlies >= 1)
				{
					iPlotScore += 2; // Reserve/support position
				}
			}
			
			// 5. CHOKEPOINT CONTROL: Infantry excels at holding chokepoints
			if (pTestPlot->IsChokePoint() && iEnemyDist <= 3)
			{
				iPlotScore += 12; // Chokepoint is ideal for infantry
				
				// Even better with terrain defense
				if (iTerrainDefMod > 0)
					iPlotScore += 4;
			}
			
			// 6. FORTIFICATION VALUE: Infantry benefits greatly from fortifying
			if (pUnit->canFortify(pTestPlot) && iEnemyDist >= 2 && iTerrainDefMod > 0)
			{
				// Position where we can fortify on good terrain
				iPlotScore += 3;
			}
		}
	}

	// === FAST RANGED UNIT POSITIONING (Skirmishers, Mounted Archers) ===
	// Fast ranged units have unique tactical needs compared to melee cavalry:
	// - Maintain kiting distance (stay at max range from melee threats)
	// - Prefer positions with multiple escape routes (avoid getting cornered)
	// - Maximize targets in range while minimizing exposure
	// - Avoid positions where enemy melee can close distance next turn
	if (pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		int iUnitRange = pUnit->GetRange();
		int iBaseMoves = pUnit->baseMoves(false);
		bool bCanMoveAfterAttack = pUnit->canMoveAfterAttacking();
		bool bIsSkirmisher = (pUnit->AI_getUnitAIType() == UNITAI_SKIRMISHER ||
							  pUnit->getUnitInfo().GetUnitAIType(UNITAI_SKIRMISHER));
		bool bIsFastRanged = (iBaseMoves >= 3);
		
		// Only apply these bonuses to fast ranged units (not slow siege or archers)
		if (bIsFastRanged || bIsSkirmisher)
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
			
			// 1. KITING DISTANCE: Prefer positions at optimal range from enemies
			// Fast ranged wants to be at range 2 (can shoot, enemy can't close gap easily)
			if (iUnitRange >= 2)
			{
				if (iEnemyDist == 2)
				{
					// Perfect kiting distance - in range to attack but enemy needs 2 moves to reach
					iPlotScore += 8;
					
					// Extra bonus for skirmishers who specialize in this
					if (bIsSkirmisher)
						iPlotScore += 4;
				}
				else if (iEnemyDist == 1 && iAdjacentEnemies > 0)
				{
					// Too close! Enemy melee can attack us next turn
					// Heavy penalty unless we can move after attacking
					if (!bCanMoveAfterAttack)
						iPlotScore -= 12; // Very dangerous for static ranged
					else
						iPlotScore -= 4; // Risky but we can escape after shooting
				}
				else if (iEnemyDist == iUnitRange && iEnemyDist > 2)
				{
					// At max range - good standoff position for long-range units
					iPlotScore += 5;
				}
			}
			
			// 2. ESCAPE ROUTE AWARENESS: Fast ranged need multiple retreat paths
			// Count passable adjacent plots that are further from enemies
			if (bCanMoveAfterAttack || bIsFastRanged)
			{
				int iEscapeRoutes = 0;
				int iBlockedSides = 0;
				
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (!pAdj)
						continue;
					
					// Check if this adjacent plot is a valid escape route
					if (pUnit->canMoveInto(*pAdj, CvUnit::MOVEFLAG_DESTINATION))
					{
						// Check if it's further from enemies (safe direction)
						int iAdjEnemyDist = assumedPosition.getTactPlot(pAdj->GetPlotIndex()).getEnemyDistance(eRelevantDomain);
						if (iAdjEnemyDist > iEnemyDist)
						{
							iEscapeRoutes++;
						}
						else if (iAdjEnemyDist < iEnemyDist)
						{
							// This direction leads toward enemies
							iBlockedSides++;
						}
					}
					else if (pAdj->isImpassable(pUnit->getTeam()) || pAdj->isWater())
					{
						iBlockedSides++;
					}
				}
				
				// Bonus for having multiple escape routes
				if (iEscapeRoutes >= 3)
				{
					iPlotScore += 6; // Many escape options - excellent skirmisher position
				}
				else if (iEscapeRoutes >= 2)
				{
					iPlotScore += 3;
				}
				else if (iEscapeRoutes == 0 && iEnemyDist <= 2)
				{
					// No escape routes and close to enemies - very bad for mobile ranged
					iPlotScore -= 8;
				}
				
				// Penalty for being cornered (blocked by terrain on multiple sides)
				if (iBlockedSides >= 4 && iEnemyDist <= 2)
				{
					iPlotScore -= 6; // Getting pinned is death for skirmishers
				}
			}
			
			// 3. FIRING ARC OPTIMIZATION: Prefer positions with targets in range
			// Fast ranged should position where they can hit enemies while staying mobile
			{
				int iTargetsInRange = 0;
				
				// Check how many enemies we can hit from this position
				for (int iRing = 1; iRing <= iUnitRange; iRing++)
				{
					for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (pLoopPlot)
						{
							CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
							if (pEnemy && !pEnemy->IsCivilianUnit())
							{
								iTargetsInRange++;
								
								// Bonus for having high-value targets in range (siege, ranged)
								if (pEnemy->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
									iTargetsInRange++; // Siege counts double
								else if (pEnemy->IsCanAttackRanged())
									iTargetsInRange++; // Enemy ranged counts extra
							}
						}
					}
				}
				
				// Bonus for having multiple targets in range
				if (iTargetsInRange >= 2)
				{
					iPlotScore += min(iTargetsInRange * 2, 8); // Up to +8 for many targets
				}
				
				// Penalty if we have no targets in range (wasted turn for skirmisher)
				if (iTargetsInRange == 0 && assumedPosition.haveEnemies())
				{
					iPlotScore -= 4;
				}
			}
			
			// 4. AVOID MELEE THREATS: Fast ranged should stay away from fast enemy melee
			// Chariot archers should fear enemy cavalry, etc.
			if (iAdjacentEnemies > 0)
			{
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (!pAdj)
						continue;
					
					CvUnit* pAdjEnemy = pAdj->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
					if (pAdjEnemy && !pAdjEnemy->IsCanAttackRanged())
					{
						// Adjacent enemy melee - check if they're fast enough to be a threat
						int iEnemyMoves = pAdjEnemy->baseMoves(false);
						
						if (iEnemyMoves >= iBaseMoves)
						{
							// Enemy is as fast or faster - big danger!
							iPlotScore -= 8;
							
							// Extra penalty if we can't move after attacking
							if (!bCanMoveAfterAttack)
								iPlotScore -= 4;
						}
						else if (iEnemyMoves >= 3)
						{
							// Fast enemy melee nearby - moderate danger
							iPlotScore -= 4;
						}
					}
				}
			}
			
			// 5. OPEN TERRAIN PREFERENCE: Fast ranged prefer open terrain for mobility
			// Rough terrain slows down escape and pursuit
			if (pTestPlot->isOpenGround())
			{
				// Open ground - easier to maneuver and escape
				iPlotScore += 3;
				
				// Extra bonus for very fast units (4+ moves)
				if (iBaseMoves >= 4)
					iPlotScore += 2;
			}
			else if (pTestPlot->isRoughGround())
			{
				// Rough terrain - harder to kite effectively
				if (iEnemyDist <= 2)
				{
					iPlotScore -= 2; // Mild penalty when near enemies
				}
			}
			
			// 6. STAY MOBILE: Skirmishers should avoid positions that end their movement
			// Fortifying or garrisoning negates their mobility advantage
			if (bIsSkirmisher && (pTestPlot->isCity() || TacticalAIHelpers::IsPlayerCitadel(pTestPlot, pUnit->getOwner())))
			{
				// Skirmishers in cities/citadels waste their mobility
				// (Unless wounded and need to heal)
				int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
				if (iHealthPercent > 60)
				{
					iPlotScore -= 5; // Healthy skirmisher shouldn't garrison
				}
			}
		}
		// === SLOW RANGED UNIT POSITIONING (Archers, Crossbowmen, etc.) ===
		// Non-mobile ranged units (2 moves, range 2) need different tactics:
		// - Cannot kite effectively - must rely on melee screen
		// - Need defensive terrain since they can't escape
		// - Should stay behind melee line, not on it
		// - Must avoid getting adjacent to enemy melee at all costs
		else if (!bIsFastRanged && !bIsSkirmisher && iUnitRange >= 2)
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
			int iAdjacentFriendlyMelee = 0;
			
			// Count friendly melee units that can screen for us
			for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
			{
				CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
				if (pAdj)
				{
					CvUnit* pFriendly = pAdj->getBestDefender(pUnit->getOwner());
					if (pFriendly && !pFriendly->IsCanAttackRanged() && pFriendly->IsCombatUnit())
					{
						iAdjacentFriendlyMelee++;
					}
				}
			}
			
			// 1. STAY BEHIND THE LINE: Slow ranged must maintain distance
			if (iEnemyDist == 1 && iAdjacentEnemies > 0)
			{
				// CRITICAL: Adjacent to enemy melee - disaster for slow ranged
				// They cannot escape and will be destroyed
				iPlotScore -= 20;
				
				// Slightly less bad if we have melee support
				if (iAdjacentFriendlyMelee >= 2)
					iPlotScore += 5;
			}
			else if (iEnemyDist == 2)
			{
				// Perfect firing position - one tile behind the front
				iPlotScore += 10;
				
				// Even better if we have melee screen between us and enemies
				if (iAdjacentFriendlyMelee >= 1)
					iPlotScore += 4;
			}
			else if (iEnemyDist == 3 && iUnitRange == 2)
			{
				// One tile too far back - can still fire but should close up
				iPlotScore += 2;
			}
			
			// 2. DEFENSIVE TERRAIN: Slow ranged benefit greatly from terrain bonuses
			// They can't run, so they need to survive being attacked
			int iTerrainDefMod = pTestPlot->defenseModifier(pUnit->getTeam(), false, false);
			if (iTerrainDefMod > 0)
			{
				// Strong bonus for defensive terrain
				iPlotScore += iTerrainDefMod / 4; // Up to +6 for 25% terrain
				
				// Extra valuable when enemies are close
				if (iEnemyDist <= 2)
					iPlotScore += iTerrainDefMod / 8;
			}
			
			// 3. AVOID OPEN GROUND: Unlike cavalry, slow ranged are vulnerable in the open
			if (pTestPlot->isOpenGround() && iEnemyDist <= 3)
			{
				// Open terrain is dangerous for units that can't retreat
				iPlotScore -= 3;
			}
			
			// 4. STAY NEAR FRIENDLIES: Slow ranged need protection
			int iAdjacentFriendlies = testPlot.getNumAdjacentFriendlies(eRelevantDomain, unit.iPlotIndex);
			if (iAdjacentFriendlies >= 2)
			{
				iPlotScore += 4; // Good mutual support
			}
			else if (iAdjacentFriendlies == 0 && iEnemyDist <= 2)
			{
				iPlotScore -= 6; // Isolated and vulnerable
			}
			
			// 5. FORTIFICATION VALUE: Slow ranged benefit from fortifying
			// Unlike fast units, they should dig in when not attacking
			if (pUnit->canFortify(pTestPlot) && iEnemyDist >= 2)
			{
				iPlotScore += 2; // Position where we can fortify safely
			}
		}
	}

	// === MEDIC UNIT POSITIONING ===
	// Units with AdjacentTileHeal (medic promotion) should position to maximize healing support
	// This is critical for pre-gunpowder armies where ranged units often have medic
	if (pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		int iMedicHeal = pUnit->getAdjacentTileHeal();
		
		// Only apply if this unit has medic ability (heals adjacent friendlies)
		if (iMedicHeal > 0)
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			
			// Count wounded friendly units nearby that would benefit from our medic aura
			int iWoundedFriendliesNearby = 0;
			int iTotalHealingNeeded = 0;
			
			for (int i = RING0_PLOTS; i < RING1_PLOTS; i++) // Adjacent tiles only (medic range)
			{
				CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
				if (pAdj)
				{
					CvUnit* pFriendly = pAdj->getBestDefender(pUnit->getOwner());
					if (pFriendly && pFriendly != pUnit && pFriendly->IsCombatUnit())
					{
						int iDamage = pFriendly->getDamage();
						if (iDamage > 0)
						{
							iWoundedFriendliesNearby++;
							iTotalHealingNeeded += iDamage;
						}
					}
				}
			}
			
			// Bonus for positioning to heal wounded friendlies
			if (iWoundedFriendliesNearby > 0)
			{
				// Base bonus per wounded unit we can heal
				int iMedicBonus = iWoundedFriendliesNearby * 4;
				
				// Scale with how much healing is needed (up to cap)
				iMedicBonus += min(iTotalHealingNeeded / 20, 8);
				
				// Cap bonus
				iPlotScore += min(iMedicBonus, 20);
			}
			
			// Medics should stay safe to keep providing healing
			// Mild penalty for being on the front line
			if (iEnemyDist == 1)
			{
				iPlotScore -= 4; // Medic shouldn't be in direct combat
			}
			else if (iEnemyDist == 2 && iWoundedFriendliesNearby > 0)
			{
				// Good medic position - close enough to heal, not on front line
				iPlotScore += 3;
			}
		}
	}

	// === GREAT GENERAL AURA COORDINATION (Combat Units) ===
	// Combat units benefit from Great General aura (+15% combat by default)
	// Units should prefer positions within General's aura range to get this bonus
	// This coordination helps units cluster around Generals for mutual benefit
	// Key: hasSupportBonus() returns true if a General/Admiral is adjacent
	// But Generals have range 2 (GREAT_GENERAL_RANGE), so we need extended check
	if (pUnit->IsCombatUnit() && evalMode != EM_INTERMEDIATE && !pUnit->IsIgnoreGreatGeneralBenefit())
	{
		int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
		bool bHasGeneralBonus = testPlot.hasSupportBonus(unit.iPlotIndex);
		
		// Only care about General aura when enemies are nearby (combat expected)
		if (iEnemyDist <= 3 && assumedPosition.haveEnemies())
		{
			// Check if we're within range of a friendly Great General/Admiral
			// hasSupportBonus only checks adjacent (range 1), but GG range is 2
			// Count generals within extended range for better coordination
			int iGeneralsInRange = 0;
			int iGeneralRange = /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
			
			// Check for Generals within aura range
			for (int iRing = 0; iRing <= iGeneralRange; iRing++)
			{
				int iStart = (iRing == 0) ? 0 : RING_PLOTS[iRing-1];
				int iEnd = RING_PLOTS[min(iRing, 5)];
				for (int i = iStart; i < iEnd; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pLoopUnit = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pLoopUnit && pLoopUnit != pUnit)
						{
							// Check if this is a Great General (land) or Great Admiral (sea)
							bool bIsRelevantGeneral = false;
							if (pUnit->getDomainType() == DOMAIN_LAND && pLoopUnit->IsGreatGeneral())
								bIsRelevantGeneral = true;
							else if (pUnit->getDomainType() == DOMAIN_SEA && pLoopUnit->IsGreatAdmiral())
								bIsRelevantGeneral = true;
							
							if (bIsRelevantGeneral)
							{
								iGeneralsInRange++;
								// Extended range generals may have AuraRangeChange
								int iThisGeneralRange = iGeneralRange + pLoopUnit->GetAuraRangeChange();
								int iActualDist = plotDistance(*pTestPlot, *pLoopPlot);
								
								// Are we actually within this General's aura?
								if (iActualDist <= iThisGeneralRange)
								{
									// Good! We'll get the combat bonus from this General
									// Bonus scales with proximity (closer is better for safety)
									int iProximityBonus = (iThisGeneralRange + 1 - iActualDist) * 3;
									iPlotScore += iProximityBonus;
								}
							}
						}
					}
				}
			}
			
			// If we found a General nearby, bonus for positioning to benefit from aura
			if (iGeneralsInRange > 0)
			{
				// Base bonus for being within General aura
				iPlotScore += 5;
				
				// Melee units benefit more from General aura (in direct combat)
				if (!pUnit->IsCanAttackRanged())
				{
					iPlotScore += 4; // Melee gets more from combat bonus
				}
				
				// Extra bonus when on front line (where combat bonus matters most)
				if (iEnemyDist <= 2)
				{
					iPlotScore += 3;
				}
			}
			else if (!bHasGeneralBonus && iGeneralsInRange == 0)
			{
				// No General support available
				// Check if there ARE friendly Generals we could move toward
				// This encourages units to converge on General-supported positions
				bool bFriendlyGeneralExists = false;
				
				// Quick check via area effect units list (more efficient)
				const std::vector<std::pair<int,int>>& possibleUnits = GET_PLAYER(pUnit->getOwner()).GetAreaEffectPositiveUnits();
				for (size_t i = 0; i < possibleUnits.size(); i++)
				{
					CvPlot* pUnitPlot = GC.getMap().plotByIndexUnchecked(possibleUnits[i].second);
					if (plotDistance(*pUnitPlot, *pTestPlot) <= 6) // Within reasonable move distance
					{
						CvUnit* pPossibleGeneral = GET_PLAYER(pUnit->getOwner()).getUnit(possibleUnits[i].first);
						if (pPossibleGeneral && !pPossibleGeneral->isDelayedDeath())
						{
							if ((pUnit->getDomainType() == DOMAIN_LAND && pPossibleGeneral->IsGreatGeneral()) ||
								(pUnit->getDomainType() == DOMAIN_SEA && pPossibleGeneral->IsGreatAdmiral()))
							{
								bFriendlyGeneralExists = true;
								
								// Mild bonus for moving toward the General's direction
								int iDistToGeneral = plotDistance(*pTestPlot, *pUnitPlot);
								int iDistFromCurrent = plotDistance(*pUnit->plot(), *pUnitPlot);
								if (iDistToGeneral < iDistFromCurrent)
								{
									iPlotScore += 2; // Moving toward General
								}
								break;
							}
						}
					}
				}
			}
		}
	}

	// === NAVAL FIRE SUPPORT FOR LAND UNITS (Amphibious Coordination) ===
	// Land units attacking coastal positions should coordinate with naval ranged units
	// Battleships, Frigates, and other naval ranged can provide devastating fire support
	// This is critical for successful amphibious operations and coastal city assaults
	if (pUnit->getDomainType() == DOMAIN_LAND && pUnit->IsCombatUnit() && evalMode != EM_INTERMEDIATE)
	{
		// Check if this plot is coastal (adjacent to water)
		bool bIsCoastalPlot = false;
		int iAdjacentWaterTiles = 0;
		
		for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
		{
			CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
			if (pAdj && pAdj->isWater())
			{
				bIsCoastalPlot = true;
				iAdjacentWaterTiles++;
			}
		}
		
		// Only apply if this is a coastal position
		if (bIsCoastalPlot && assumedPosition.haveEnemies())
		{
			int iNavalFireSupportBonus = 0;
			int iNavalRangedNearby = 0;
			int iTotalNavalRangedStrength = 0;
			
			// Check for friendly naval ranged units that can provide fire support
			for (int iRing = 1; iRing <= 3; iRing++)
			{
				for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (!pLoopPlot || !pLoopPlot->isWater())
						continue;
					
					for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
					{
						CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
						if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner())
							continue;
						
						// Found a friendly naval unit
						if (pLoopUnit->getDomainType() == DOMAIN_SEA && 
							pLoopUnit->IsCanAttackRanged() && 
							pLoopUnit->IsCombatUnit())
						{
							// Check if this naval unit can actually fire at enemies near our position
							int iNavalRange = pLoopUnit->GetRange();
							int iDistToUs = plotDistance(*pTestPlot, *pLoopPlot);
							
							if (iDistToUs <= iNavalRange)
							{
								iNavalRangedNearby++;
								iTotalNavalRangedStrength += pLoopUnit->GetBaseRangedCombatStrength();
								
								// Battleships and similar heavy naval ranged are particularly valuable
								if (pLoopUnit->GetBaseRangedCombatStrength() >= 50)
									iNavalFireSupportBonus += 5;
								else
									iNavalFireSupportBonus += 3;
							}
						}
					}
				}
			}
			
			if (iNavalRangedNearby > 0)
			{
				// Base bonus for having naval fire support
				int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
				
				// Strong bonus when engaging enemies with naval support
				if (iEnemyDist <= 2)
				{
					iPlotScore += iNavalFireSupportBonus;
					
					// Extra bonus for melee units (they need fire support most)
					if (!pUnit->IsCanAttackRanged())
						iPlotScore += min(iNavalRangedNearby * 3, 12);
				}
				
				// Coastal city assault bonus
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (pAdj && pAdj->isCity() && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pAdj->getOwner()))
					{
						// Adjacent to enemy coastal city with naval support - excellent!
						iPlotScore += min(iTotalNavalRangedStrength / 10, 15);
						break;
					}
				}
			}
		}
	}

	// === HELICOPTER GUNSHIP POSITIONING ===
	// Helicopters are extremely mobile but need to:
	// - Stay away from AA coverage zones
	// - Position for quick surgical strikes
	// - Use their ability to reach any terrain
	// - Avoid clustering (vulnerable to area AA)
	if (pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_LAND && 
		pUnit->IsHoveringUnit() && evalMode != EM_INTERMEDIATE)
	{
		int iUnitRange = pUnit->GetRange();
		int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
		
		// 1. AA AVOIDANCE ZONES: Helicopters must avoid areas covered by AA
		// This is life-or-death for helicopters
		int iAAUnitsNearby = 0;
		int iClosestAADist = INT_MAX;
		
		for (int i = RING0_PLOTS; i < RING_PLOTS[4]; i++) // Check 4-tile radius
		{
			CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
			if (!pLoopPlot)
				continue;
			
			CvUnit* pPotentialAA = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
			if (pPotentialAA && pPotentialAA->canIntercept() && pPotentialAA->getDomainType() != DOMAIN_AIR)
			{
				iAAUnitsNearby++;
				int iDistToAA = plotDistance(*pTestPlot, *pLoopPlot);
				if (iDistToAA < iClosestAADist)
					iClosestAADist = iDistToAA;
			}
		}
		
		// Heavy penalty for being in AA coverage
		if (iAAUnitsNearby > 0)
		{
			// Get interception range (usually 2-3)
			int iAARange = 2; // Default interception range
			
			if (iClosestAADist <= iAARange)
			{
				// In interception range - very dangerous
				iPlotScore -= 20;
				
				// Even worse with multiple AA (crossfire)
				if (iAAUnitsNearby >= 2)
					iPlotScore -= iAAUnitsNearby * 10;
			}
			else if (iClosestAADist <= iAARange + 1)
			{
				// Just outside AA range - still risky
				iPlotScore -= 8;
			}
		}
		else
		{
			// No AA nearby - helicopter can operate freely
			iPlotScore += 8;
		}
		
		// 2. STRIKE POSITIONING: Position to hit targets without entering AA zones
		// Helicopters should position at their max range from valuable targets
		{
			int iTargetsInRange = 0;
			int iHighValueTargetsInRange = 0;
			int iArmorTargetsInRange = 0;
			
			for (int iRing = 1; iRing <= iUnitRange; iRing++)
			{
				for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemy && !pEnemy->IsCivilianUnit())
						{
							iTargetsInRange++;
							
							// Check for anti-armor bonus (tank hunting)
							UnitCombatTypes eEnemyCombatType = pEnemy->getUnitCombatType();
							if (eEnemyCombatType != NO_UNITCOMBAT)
							{
								int iAntiArmorBonus = pUnit->unitCombatModifier(eEnemyCombatType);
								if (iAntiArmorBonus >= 25)
								{
									// We have a significant bonus against this unit type
									iArmorTargetsInRange++;
									
									// Modern tanks are especially valuable targets
									if (pEnemy->GetBaseCombatStrength() >= 60)
										iArmorTargetsInRange++;
								}
							}
							
							// Other high-value targets for helicopter
							if (pEnemy->AI_getUnitAIType() == UNITAI_CITY_BOMBARD)
								iHighValueTargetsInRange += 2; // Siege
							else if (pEnemy->canIntercept())
								iHighValueTargetsInRange += 3; // Enemy AA (remove threats)
							else if (pEnemy->GetCurrHitPoints() < pEnemy->GetMaxHitPoints() / 2)
								iHighValueTargetsInRange++; // Wounded
						}
					}
				}
			}
			
			// Bonus for having targets in range
			if (iTargetsInRange > 0)
				iPlotScore += min(iTargetsInRange * 2, 6);
			
			// Extra bonus for high-value targets
			if (iHighValueTargetsInRange > 0)
				iPlotScore += min(iHighValueTargetsInRange * 3, 12);
			
			// TANK HUNTING BONUS: Strong bonus for positions with armor targets in range
			// This is a primary role for helicopter gunships
			if (iArmorTargetsInRange > 0)
			{
				iPlotScore += iArmorTargetsInRange * 5; // +5 per armor target
				iPlotScore += 8; // Base bonus for having tank-hunting opportunity
			}
		}
		
		// 3. DON'T CLUSTER: Helicopters should spread out to avoid area attacks
		int iAdjacentFriendlyHelis = 0;
		for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
		{
			CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
			if (pAdj)
			{
				CvUnit* pFriendly = pAdj->getBestDefender(pUnit->getOwner());
				if (pFriendly && pFriendly->IsHoveringUnit() && pFriendly->IsCanAttackRanged())
					iAdjacentFriendlyHelis++;
			}
		}
		
		if (iAdjacentFriendlyHelis >= 2)
		{
			// Clustering helicopters - vulnerable to area attacks
			iPlotScore -= 6;
		}
		
		// 4. TERRAIN FLEXIBILITY: Helicopters can use any terrain
		// They should use this to reach positions other units can't
		// No terrain penalties for helicopters!
		// But prefer positions that let them retreat over friendly territory
		if (pTestPlot->IsFriendlyTerritory(pUnit->getOwner()))
		{
			iPlotScore += 3; // Easier to retreat/resupply
		}
		else if (pTestPlot->isWater() && pUnit->IsHoveringUnit())
		{
			// Hovering over water - unique helicopter advantage
			// Good if it gives firing angles others can't get
			if (iEnemyDist <= iUnitRange && iAAUnitsNearby == 0)
				iPlotScore += 5; // Exploit water-hover for attack angles
		}
		
		// 5. MOBILITY PRESERVATION: Don't garrison helicopters
		if (pTestPlot->isCity())
		{
			int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
			if (iHealthPercent > 50)
			{
				iPlotScore -= 8; // Helicopters should stay mobile
			}
		}
	}

	// === SUPPRESSION/SUPPORT UNIT POSITIONING (Bazookas, Machine Guns, etc.) ===
	// Units with negative NearbyEnemyCombatMod debuff enemies within range.
	// These "suppression" units should position to maximize their aura coverage.
	// Bazookas (COVERING_FIRE_2) have -15% debuff on enemies within 2 tiles.
	// Machine Guns with similar promotions provide area denial.
	// These units should:
	// - Position where the aura covers maximum enemies
	// - Stay near friendly melee to support attacks (debuffed enemies easier to kill)
	// - Position at moderate range (need protection from melee but want to be effective)
	if (pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		int iSuppressionMod = pUnit->getNearbyEnemyCombatMod(); // Negative = debuff enemies
		
		// Only apply to units with suppression auras (negative modifier)
		if (iSuppressionMod < 0)
		{
			int iSuppressionRange = pUnit->getNearbyEnemyCombatRange(); // Usually 2 tiles
			if (iSuppressionRange <= 0)
				iSuppressionRange = 2; // Default assumption
			
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			
			// 1. AURA COVERAGE: Count enemies that would be debuffed from this position
			int iEnemiesInAura = 0;
			int iHighValueInAura = 0; // Melee units are more affected by combat debuffs
			
			for (int iRing = 1; iRing <= iSuppressionRange; iRing++)
			{
				for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemy && pEnemy->IsCombatUnit())
						{
							iEnemiesInAura++;
							
							// Melee units are high-value targets for suppression
							// Combat debuffs affect their attack/defense directly
							if (!pEnemy->IsCanAttackRanged())
							{
								iHighValueInAura++;
								
								// Modern armor (tanks) are even more valuable to suppress
								if (pEnemy->GetBaseCombatStrength() >= 60)
									iHighValueInAura++;
							}
						}
					}
				}
			}
			
			// Bonus for covering multiple enemies with suppression aura
			if (iEnemiesInAura > 0)
			{
				// Base bonus per enemy in aura
				int iAuraBonus = iEnemiesInAura * 4;
				
				// Extra bonus for suppressing melee/tanks (more affected by combat mod)
				iAuraBonus += iHighValueInAura * 3;
				
				// Cap the bonus to prevent runaway values
				iPlotScore += min(iAuraBonus, 25);
			}
			
			// 2. SUPPORT POSITIONING: Stay near friendly melee to support their attacks
			// Enemies debuffed by our aura are easier for friendlies to kill
			int iFriendlyMeleeNearby = 0;
			for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
				if (pLoopPlot)
				{
					CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
					if (pFriendly && !pFriendly->IsCanAttackRanged() && pFriendly->IsCombatUnit())
					{
						iFriendlyMeleeNearby++;
					}
				}
			}
			
			// Bonus for being near friendlies who benefit from our suppression
			if (iFriendlyMeleeNearby > 0 && iEnemiesInAura > 0)
			{
				iPlotScore += min(iFriendlyMeleeNearby * 3, 9); // +3 per friendly, max +9
			}
			
			// 3. SAFE DISTANCE: Suppression units shouldn't be on front line
			// They're typically fragile and need protection
			if (iEnemyDist == 1)
			{
				// Adjacent to enemies - too exposed for support unit
				int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
				if (iAdjacentEnemies > 0)
				{
					iPlotScore -= 8 + (iAdjacentEnemies * 2);
				}
			}
			else if (iEnemyDist == 2)
			{
				// Ideal range for suppression aura (if range is 2)
				if (iSuppressionRange >= 2)
				{
					iPlotScore += 6; // Good position - in aura range but not adjacent
				}
			}
			
			// 4. DEFENSIVE TERRAIN: Suppression units benefit from cover
			// They need to survive to maintain their aura
			int iTerrainDefMod = pTestPlot->defenseModifier(pUnit->getTeam(), false, false);
			if (iTerrainDefMod > 0)
			{
				iPlotScore += iTerrainDefMod / 10; // Small bonus for defensive terrain
			}
			
			// 5. COMBINED ARMS WITH ANTI-TANK: If this unit also has anti-armor bonus,
			// it should position where it can both suppress AND engage armor
			UnitCombatTypes eArmorCombat = (UnitCombatTypes)GC.getInfoTypeForString("UNITCOMBAT_ARMOR");
			if (eArmorCombat != NO_UNITCOMBAT)
			{
				int iAntiArmorBonus = pUnit->unitCombatModifier(eArmorCombat);
				if (iAntiArmorBonus >= 25)
				{
					// This unit has significant anti-armor bonus (like Bazooka with +50%)
					// Check for armor targets in firing range
					int iUnitRange = pUnit->GetRange();
					int iArmorInRange = 0;
					
					for (int iRing = 1; iRing <= iUnitRange; iRing++)
					{
						for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
						{
							CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
							if (pLoopPlot)
							{
								CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
								if (pEnemy && pEnemy->getUnitCombatType() == eArmorCombat)
								{
									iArmorInRange++;
								}
							}
						}
					}
					
					// Strong bonus for positions with armor targets in range
					if (iArmorInRange > 0)
					{
						iPlotScore += 10 + (iArmorInRange * 5); // +15 for one tank, +20 for two
					}
				}
			}
		}
	}

	// === LIGHT TANK / ARMORED CAR POSITIONING ===
	// Light tanks are reconnaissance and screening units that should:
	// - Stay ahead of main force (scouting)
	// - Avoid direct confrontation with heavy armor
	// - Use speed to exploit gaps and flank
	// - Support heavy tanks with fire
	// Detection: Fast ranged (4+ moves), moderate strength (30-60), not hovering
	if (pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_LAND &&
		pUnit->baseMoves(false) >= 4 && !pUnit->IsHoveringUnit() && evalMode != EM_INTERMEDIATE)
	{
		int iOurStrength = pUnit->GetBaseCombatStrength();
		bool bIsLightTank = (iOurStrength >= 30 && iOurStrength < 60);
		
		if (bIsLightTank && assumedPosition.haveEnemies())
		{
			int iUnitRange = pUnit->GetRange();
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			int iAdjacentEnemies = testPlot.getNumAdjacentEnemies(eRelevantDomain);
			
			// 1. SCREENING POSITION: Stay ahead of main force but not too exposed
			// Light tanks should be at range 2-3 from enemies (can fire, hard to catch)
			if (iEnemyDist == 2 || iEnemyDist == iUnitRange)
			{
				// Good screening distance
				iPlotScore += 8;
			}
			else if (iEnemyDist == 1 && iAdjacentEnemies > 0)
			{
				// Too close for a light tank
				iPlotScore -= 10;
			}
			
			// 2. FLANK POSITIONING: Light tanks exploit gaps
			// Check for positions on the flanks of enemy formations
			{
				int iEnemiesInFront = 0;
				int iEnemiesOnSide = 0;
				
				const CvPlot* pTarget = assumedPosition.getTarget();
				if (pTarget)
				{
					DirectionTypes eDirToTarget = directionXY(pTestPlot, pTarget);
					
					for (int i = RING0_PLOTS; i < RING_PLOTS[3]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (!pLoopPlot)
							continue;
						
						CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemy && pEnemy->IsCombatUnit())
						{
							DirectionTypes eDirToEnemy = directionXY(pTestPlot, pLoopPlot);
							
							// Is enemy in our attack direction or on the side?
							int iAngleDiff = abs((int)eDirToTarget - (int)eDirToEnemy);
							if (iAngleDiff > 3) iAngleDiff = 6 - iAngleDiff;
							
							if (iAngleDiff <= 1)
								iEnemiesInFront++;
							else
								iEnemiesOnSide++;
						}
					}
				}
				
				// Bonus for flanking positions (enemies on side, not in front)
				if (iEnemiesOnSide > 0 && iEnemiesInFront == 0)
				{
					iPlotScore += 8; // Good flanking position
				}
				else if (iEnemiesInFront >= 2)
				{
					iPlotScore -= 5; // Facing strong enemy concentration
				}
			}
			
			// 3. HEAVY TANK SUPPORT: Position to support friendly heavy armor
			{
				bool bNearFriendlyHeavyArmor = false;
				for (int i = RING0_PLOTS; i < RING_PLOTS[3]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pFriendly && !pFriendly->IsCanAttackRanged() &&
							pFriendly->baseMoves(false) >= 4 &&
							pFriendly->GetBaseCombatStrength() >= 60)
						{
							bNearFriendlyHeavyArmor = true;
							break;
						}
					}
				}
				
				// Bonus for being near heavy tanks (combined arms support)
				if (bNearFriendlyHeavyArmor)
				{
					iPlotScore += 6;
					
					// Extra bonus if in position to support their attack
					if (iEnemyDist <= iUnitRange)
						iPlotScore += 4;
				}
			}
			
			// 4. AVOID HEAVY ENEMY ARMOR: Don't get caught by tanks
			{
				bool bHeavyEnemyNearby = false;
				for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemy && !pEnemy->IsCanAttackRanged() &&
							pEnemy->GetBaseCombatStrength() >= 60)
						{
							bHeavyEnemyNearby = true;
							break;
						}
					}
				}
				
				if (bHeavyEnemyNearby)
				{
					// Enemy heavy armor nearby - dangerous for light tank
					iPlotScore -= 10;
				}
			}
			
			// 5. OPEN TERRAIN: Light tanks need mobility
			if (pTestPlot->isOpenGround())
			{
				iPlotScore += 4;
			}
			else if (pTestPlot->isRoughGround())
			{
				iPlotScore -= 3;
			}
			
			// 6. ESCAPE ROUTES: Light tanks need to retreat if caught
			{
				int iEscapeRoutes = 0;
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (pAdj && pUnit->canMoveInto(*pAdj, CvUnit::MOVEFLAG_DESTINATION))
					{
						int iAdjEnemyDist = assumedPosition.getTactPlot(pAdj->GetPlotIndex()).getEnemyDistance(eRelevantDomain);
						if (iAdjEnemyDist > iEnemyDist)
							iEscapeRoutes++;
					}
				}
				
				if (iEscapeRoutes >= 2)
				{
					iPlotScore += 4;
				}
				else if (iEscapeRoutes == 0 && iEnemyDist <= 2)
				{
					iPlotScore -= 6; // Cornered light tank
				}
			}
		}
	}

	// === MOUNTAIN-CAPABLE UNIT POSITIONING (Inca, Recon with Altitude Training, Helicopters) ===
	// Units that can move into mountains have unique tactical opportunities:
	// - Mountains provide excellent defense (+25% in VP)
	// - Mountains are impassable to most enemies (flanking immunity)
	// - Mountains provide superior visibility (sight bonus)
	// - Units can retreat to mountains for safety
	// Detection: Check if unit can enter mountains (promotion or trait)
	if (pUnit->getDomainType() == DOMAIN_LAND && evalMode != EM_INTERMEDIATE)
	{
		// Check if this unit can cross mountains
		bool bCanCrossMountains = pUnit->canCrossMountains() || GET_PLAYER(assumedPosition.getPlayer()).CanCrossMountain();
		
		if (bCanCrossMountains)
		{
			int iEnemyDist = testPlot.getEnemyDistance(eRelevantDomain);
			
			// Check if this plot is a mountain
			if (pTestPlot->isMountain())
			{
				// 1. DEFENSIVE MOUNTAIN POSITION: Mountains are excellent defensive terrain
				// The defense bonus is already in defenseModifier, but we add awareness for the
				// tactical value of being unreachable by most enemies
				
				// Count how many enemy units CANNOT reach us on the mountain
				int iEnemiesWhoCannotReach = 0;
				int iEnemiesWhoCanReach = 0;
				
				for (int iRing = 1; iRing <= 3; iRing++)
				{
					for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (!pLoopPlot)
							continue;
						
						CvUnit* pEnemy = pLoopPlot->getBestDefender(NO_PLAYER, pUnit->getOwner(), NULL, true);
						if (pEnemy && pEnemy->IsCombatUnit())
						{
							// Check if enemy can cross mountains
							bool bEnemyCanCrossMountains = pEnemy->canCrossMountains() || 
								GET_PLAYER(pEnemy->getOwner()).CanCrossMountain() ||
								pEnemy->IsHoveringUnit();
							
							if (bEnemyCanCrossMountains)
								iEnemiesWhoCanReach++;
							else
								iEnemiesWhoCannotReach++;
						}
					}
				}
				
				// Strong bonus if enemies cannot reach us on the mountain
				if (iEnemiesWhoCannotReach > 0 && iEnemiesWhoCanReach == 0)
				{
					// Completely safe from nearby melee enemies!
					iPlotScore += 15;
					
					// Extra bonus per enemy who can't reach us (we're denying them options)
					iPlotScore += iEnemiesWhoCannotReach * 3;
				}
				else if (iEnemiesWhoCannotReach > iEnemiesWhoCanReach)
				{
					// Mostly safe - only some enemies can follow
					iPlotScore += 8 + (iEnemiesWhoCannotReach - iEnemiesWhoCanReach) * 2;
				}
				
				// 2. RANGED UNITS ON MOUNTAINS: Excellent firing platform
				if (pUnit->IsCanAttackRanged())
				{
					// Mountains provide LOS advantage and protection
					iPlotScore += 8;
					
					// Extra bonus if in range to attack enemies who can't reach us
					int iUnitRange = pUnit->GetRange();
					if (iEnemyDist <= iUnitRange && iEnemiesWhoCannotReach > 0)
					{
						iPlotScore += 6; // Can attack enemies who can't retaliate
					}
				}
				
				// 3. WOUNDED UNITS: Mountains are great for wounded units to heal safely
				int iHealthPercent = (pUnit->GetCurrHitPoints() * 100) / pUnit->GetMaxHitPoints();
				if (iHealthPercent < 50 && iEnemiesWhoCanReach == 0)
				{
					iPlotScore += 10; // Safe healing position
				}
				
				// 4. BLOCKING MOUNTAIN PASS: If this mountain is between enemies and our city
				// it can serve as an impassable barrier (for enemies who can't cross)
				const CvPlot* pTarget = assumedPosition.getTarget();
				if (pTarget && pTarget->isCity() && pTarget->isFriendlyCity(*pUnit))
				{
					// Check if we're between the target city and enemies
					int iDistToCity = plotDistance(*pTestPlot, *pTarget);
					if (iDistToCity <= 4 && iEnemyDist <= 3 && iDistToCity < iEnemyDist)
					{
						// We're between enemies and our city, on a mountain
						// This is a blocking position
						iPlotScore += 6;
					}
				}
			}
			else
			{
				// 5. MOUNTAIN ESCAPE ROUTES: Check for nearby mountains as escape options
				// Units that can cross mountains have unique retreat paths
				int iMountainEscapeRoutes = 0;
				
				for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
				{
					CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
					if (pAdj && pAdj->isMountain())
					{
						// This adjacent mountain is a potential escape route
						// (only for us, not for most enemies)
						iMountainEscapeRoutes++;
					}
				}
				
				// Bonus for having mountain escape options
				if (iMountainEscapeRoutes > 0 && iEnemyDist <= 2)
				{
					// Near enemies but have mountain escape routes
					iPlotScore += iMountainEscapeRoutes * 2;
				}
				
				// 6. MOUNTAIN FLANKING: Use mountains to get behind enemy lines
				// Mountains that enemies can't cross can serve as flanking corridors
				if (assumedPosition.haveEnemies() && iEnemyDist <= 4)
				{
					// Check if there's a mountain route that gets us behind enemies
					for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
						if (pLoopPlot && pLoopPlot->isMountain())
						{
							// Check if this mountain leads toward enemy flank/rear
							int iMtnEnemyDist = assumedPosition.getTactPlot(pLoopPlot->GetPlotIndex()).getEnemyDistance(eRelevantDomain);
							if (iMtnEnemyDist <= iEnemyDist)
							{
								// Mountain closer to enemies - potential flanking route
								// Small bonus for being near a flanking path
								iPlotScore += 2;
								break;
							}
						}
					}
				}
			}
		}
	}

	//final score
	//danger values (typically negative!) are mostly useful as tiebreaker
	result->iSelfDamage = iSelfDamage;

	//small bias for staying close to our cities, to have a way to retreat if necessary
	int iCityDistanceScore = 10 - GET_PLAYER(assumedPosition.getPlayer()).GetCityDistanceInPlots(pTestPlot);
	int iExtra = max(iCityDistanceScore, 0);

	//often there are multiple identical units which could move into a plot (eg in naval battles)
	//in that case we want to prefer the one which has more movement points left to make the movement animation look better
	iExtra += result->iRemainingMoves / GD_INT_GET(MOVE_DENOMINATOR);

	result->SetScore(iPlotScore * 10 + iDangerScore + iExtra, iBonusScore, iDamageDelta);

	return result;
}

//stacking with combat units is allowed here!
static STacticalAssignment* ScorePlotForNonFightingUnitMove(const SUnitStats& unit, const CvTacticalPlot* testPlot, const CvTacticalPosition& assumedPosition, eUnitMoveEvalMode evalMode)
{
	//default action is do nothing and invalid score (not -INT_MAX, to prevent overflows!)
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex,testPlot->getPlotIndex(), unit.iUnitID, unit.iMovesLeft, unit.eMoveStrategy, A_MOVE, GetPrevPlotScore(unit.iUnitID, assumedPosition));
	int iScore = 0;
		
	//the plot we're checking right now
	const CvPlot* pTestPlot = testPlot->getPlot();
	const CvUnit* pUnit = unit.pUnit;

	//cannot deal with enemies here, only friendly/empty plots
	if (testPlot->isEnemy())
		return result;

	if (evalMode == EM_FINAL && unit.eLastAssignment == A_USE_POWER)
	{
		result->SetScore(0, 0, 0);
		return result;
	}

	//check distance to target if gathering (not attacking)
	const CvTacticalPlot* targetPlot = assumedPosition.getTactPlot( assumedPosition.getTarget()->GetPlotIndex() );
	if (targetPlot && !targetPlot->isEnemy())
	{
		//can be treacherous with impassable terrain in between but everything else is much more complex
		int iPlotDistance = plotDistance(*assumedPosition.getTarget(),*pTestPlot);
		iScore += 3 - iPlotDistance;
	}

	//generals and admirals
	if (unit.eMoveStrategy == MS_SUPPORT)
	{
		//check distance to enemy in any case
		switch (testPlot->getEnemyDistance())
		{
		case 0:
			return result; //don't ever go there, wouldn't work anyway
			break;
		case 1:
			iScore = pTestPlot->isCity() ? 13 : 2; //dangerous to end the turn, avoid
			break;
		case 2:
			iScore = 23; //good for defense support, good for attack support, but risky
			break;
		case 3:
			iScore = 17; //good for defense support, not so good for attack support
			break;
		default:
			iScore = 5; //usual case for gathering moves, otherwise not really interesting
			break;
		}

		bool bHaveSimCover = false;
		const vector<STacticalUnit>& units = testPlot->getUnitsAtPlot();
		for (size_t i = 0; i < units.size(); i++)
		{
			if (isCombatUnit(units[i].eMoveType))
			{
				//we have to make sure our protector will stay ...
				CvUnit* pDefender = GET_PLAYER(assumedPosition.getPlayer()).getUnit(units[i].iUnitID);
				if (!assumedPosition.lastAssignmentIsAfterRestart(units[i].iUnitID) && !pDefender->shouldHeal(false))
				{
					bHaveSimCover = true;
					break;
				}
			}
		}

		bool bHaveRealCover = false;
		if (!bHaveSimCover)
		{
			//this check is stronger than pure IsCombatUnitEndTurn
			CvUnit* pBestDefender = pTestPlot->getBestDefender(assumedPosition.getPlayer());
			bool bDefenderIsGood = pBestDefender && pBestDefender->TurnProcessed() && !pBestDefender->isProjectedToDieNextTurn() && pBestDefender->GetDanger() < pBestDefender->GetCurrHitPoints();
			if (pTestPlot->isFriendlyCity(*pUnit) || bDefenderIsGood)
				bHaveRealCover = true;
		}

		if (evalMode == EM_INTERMEDIATE)
		{
			//don't do it if we don't have enough movement to move to a safe plot later
			if ((unit.iMovesLeft<=GD_INT_GET(MOVE_DENOMINATOR) && !pUnit->IsFreeAttackMoves()) && !bHaveSimCover && !bHaveRealCover)
				return result;
		}
		else
		{
			if (testPlot->isNicePlotForCitadel() && pUnit->IsGreatGeneral() && unit.iMovesLeft > 0)
			{
				result->eAssignmentType = A_USE_POWER;
				result->iRemainingMoves = 0;
				iScore += 100;
			}
			//we want one of our own combat units covering us (either sim or non sim). cities are also considered safe
			else if (!pTestPlot->isFriendlyCity(*pUnit) && !bHaveSimCover && !bHaveRealCover) 
				return result;

			//surrounding cover is also good
			int iFriends = (evalMode==EM_FINAL) ? testPlot->getNumAdjacentFriendliesEndTurn(CvTacticalPlot::TD_BOTH) : testPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_BOTH, -1);
			iScore += iFriends;

			//when in doubt prefer the high ground - looks cooler
			const uint32 plotFlags = pTestPlot->GetPlotCacheFlags();
			if (plotFlags & (CvPlot::PLOT_CACHE_HILLS | CvPlot::PLOT_CACHE_MOUNTAIN))
				iScore++;

			//try not to be a sitting duck (faster than isNativeDomain but not entirely accurate)
			if (pUnit->getDomainType() != pTestPlot->getDomain())
				iScore -= 3;
		}

		// === GREAT GENERAL AURA POSITIONING ===
		// Generals should position to maximize combat bonus to nearby units
		// Their aura range is GREAT_GENERAL_RANGE (default 2) + any AuraRangeChange
		// Melee units benefit most from combat bonuses (direct combat)
		// Ranged/siege benefit less but still gain from the bonus
		int iGeneralAuraRange = /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE) + pUnit->GetAuraRangeChange();
		
		// Count units within aura range, weighted by type
		int iMeleeUnitsInAura = 0;
		int iRangedUnitsInAura = 0;
		int iUnitsNearFront = 0; // Units close to enemies that will actually fight
		
		for (int iRing = 0; iRing <= iGeneralAuraRange; iRing++)
		{
			int iStart = (iRing == 0) ? 0 : RING_PLOTS[iRing-1];
			int iEnd = RING_PLOTS[min(iRing, 5)];
			for (int i = iStart; i < iEnd; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
				if (pLoopPlot)
				{
					CvUnit* pLoopUnit = pLoopPlot->getBestDefender(pUnit->getOwner());
					if (pLoopUnit && pLoopUnit != pUnit && pLoopUnit->IsCombatUnit())
					{
						// Don't count units that ignore Great General benefit
						if (pLoopUnit->IsIgnoreGreatGeneralBenefit())
							continue;
						
						// Check domain match (Generals for land, Admirals for sea)
						if ((pUnit->IsGreatGeneral() && pLoopUnit->getDomainType() != DOMAIN_LAND) ||
							(pUnit->IsGreatAdmiral() && pLoopUnit->getDomainType() != DOMAIN_SEA))
							continue;
						
						if (pLoopUnit->IsCanAttackRanged())
						{
							iRangedUnitsInAura++;
						}
						else
						{
							iMeleeUnitsInAura++;
						}
						
						// Check if this unit is near the front (will actually benefit from combat bonus)
						// Get the enemy distance for this unit's plot
						const CvTacticalPlot& unitTactPlot = assumedPosition.getTactPlot(pLoopPlot->GetPlotIndex());
						if (unitTactPlot.isValid() && unitTactPlot.getEnemyDistance() <= 3)
						{
							iUnitsNearFront++;
						}
					}
				}
			}
		}
		
		// Bonus for covering units with aura (melee counts more)
		// Melee units are in direct combat and gain full benefit from +15% combat
		iScore += iMeleeUnitsInAura * 3;
		// Ranged still benefits but less directly
		iScore += iRangedUnitsInAura * 1;
		
		// Extra bonus for covering units that are actually near the front
		// Combat bonuses matter most when units are engaged with enemies
		iScore += iUnitsNearFront * 2;
		
		// Bonus for stacking with a strong melee unit (direct protection + full bonus)
		if (testPlot.hasFriendlyCombatUnit())
		{
			const vector<STacticalUnit>& unitsHere = testPlot.getUnitsAtPlot();
			for (size_t i = 0; i < unitsHere.size(); i++)
			{
				if (isCombatUnit(unitsHere[i].eMoveType))
				{
					CvUnit* pStackedUnit = GET_PLAYER(assumedPosition.getPlayer()).getUnit(unitsHere[i].iUnitID);
					if (pStackedUnit && !pStackedUnit->IsCanAttackRanged())
					{
						// Stacked with melee - good for protection and gives them stacked bonus
						iScore += 3;
						
						// Extra bonus if melee is damaged (General gives morale/healing support)
						if (pStackedUnit->getDamage() > 0)
							iScore += 2;
					}
				}
			}
		}

		//points for supported units (count only the first ring for performance ...)
		int iFriends = testPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_BOTH, -1);
		if (testPlot->hasFriendlyCombatUnit())
			iFriends++;

		//supported units count double plus one extra if we have covered
		iScore += iFriends*2;
		if (testPlot->hasFriendlyCombatUnit())
			iScore++;

		//avoid overlap. this works only because we ignore our own aura when calling this function!
		if (testPlot->hasSupportBonus(unit.iPlotIndex) && testPlot->getPlotIndex()!=unit.iPlotIndex)
			iScore /= 2;
	}

	//plain embarked units
	if (unit.eMoveStrategy == MS_EMBARKED)
	{
		//check distance to enemy in any case
		switch (testPlot->getEnemyDistance())
		{
		case 0:
			return result; //don't ever go there, wouldn't work anyway
			break;
		case 1:
			iScore = 2; //dangerous, only in emergencies
			break;
		default:
			iScore = 23; //embarked units don't care about getting close to enemies
			break;
		}

		if (evalMode!=EM_INTERMEDIATE)
		{
			//catch the case with infinite danger
			int iDanger = min(1000, pUnit->GetDanger(pTestPlot, assumedPosition.GetUnitDamageDealt(), 0));

			//embarked units have limited vision so be careful
			if (testPlot->isEdgePlot())
				iDanger += 50;

			//we want friends around us
			int iFriends = (evalMode==EM_FINAL) ? testPlot->getNumAdjacentFriendliesEndTurn(CvTacticalPlot::TD_BOTH) : testPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_BOTH, -1);
			iScore -= iDanger / (iFriends + 1);

			// === EMBARKED UNIT SEEKING NAVAL ESCORT ===
			int iNavalEscortCount = 0;
			int iNavalCombatStrengthNearby = 0;
			bool bHasRangedEscort = false;

			for (int i = RING0_PLOTS; i < RING2_PLOTS; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(pTestPlot, i);
				if (!pLoopPlot || !pLoopPlot->isWater())
					continue;

				for (int iUnitLoop = 0; iUnitLoop < pLoopPlot->getNumUnits(); iUnitLoop++)
				{
					CvUnit* pLoopUnit = pLoopPlot->getUnitByIndex(iUnitLoop);
					if (!pLoopUnit || pLoopUnit->getOwner() != pUnit->getOwner())
						continue;

					if (pLoopUnit->getDomainType() == DOMAIN_SEA && pLoopUnit->IsCombatUnit())
					{
						iNavalEscortCount++;
						iNavalCombatStrengthNearby += pLoopUnit->GetBaseCombatStrength();
						if (pLoopUnit->IsCanAttackRanged())
							bHasRangedEscort = true;
					}
				}
			}

			if (iNavalEscortCount > 0)
			{
				iScore += iNavalEscortCount * 4;
				if (bHasRangedEscort)
					iScore += 5;

				int iEnemyDist = testPlot->getEnemyDistance(CvTacticalPlot::TD_SEA);
				if (iEnemyDist <= 3 && iNavalCombatStrengthNearby > 0)
					iScore += min(iNavalCombatStrengthNearby / 10, 10);
			}
			else if (testPlot->getEnemyDistance(CvTacticalPlot::TD_SEA) <= 3)
			{
				iScore -= 15;
			}

			bool bAdjacentToFriendlyCoast = false;
			bool bAdjacentToNeutralCoast = false;
			for (int i = RING0_PLOTS; i < RING1_PLOTS; i++)
			{
				CvPlot* pAdj = iterateRingPlots(pTestPlot, i);
				if (pAdj && pAdj->isCoastalLand())
				{
					if (pAdj->IsFriendlyTerritory(pUnit->getOwner()))
						bAdjacentToFriendlyCoast = true;
					else if (!pAdj->isOwned() || !GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pAdj->getOwner()))
						bAdjacentToNeutralCoast = true;
				}
			}
			if (bAdjacentToFriendlyCoast)
				iScore += 8;
			else if (bAdjacentToNeutralCoast)
				iScore += 4;
		}
	}

	//often there are multiple identical units which could move into a plot
	//in that case we want to prefer the one which has more movement points left
	int iExtra = result->iRemainingMoves / GD_INT_GET(MOVE_DENOMINATOR);

	//scale to be in range with actual fighting units
	result->SetScore(iScore * 10 + iExtra, 0, 0);

	return result;
}

static STacticalAssignment* ScorePlotForRangedAttack(const SUnitStats& unit, const CvTacticalPlot* assumedUnitPlot, const CvTacticalPlot* enemyPlot, const CvTacticalPosition& assumedPosition)
{
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex, enemyPlot->getPlotIndex(), unit.iUnitID, unit.iMovesLeft, unit.eMoveStrategy, A_RANGEATTACK, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	int iBonusScore = 0;

	//received damage is zero here but still use the correct unit number ratio so as not to distort scores
	ScoreAttackDamage(enemyPlot, unit.pUnit, assumedUnitPlot, assumedPosition, gTactPosStorage.getAttackCache(), result, unit.iSelfDamage);
	if (!result->IsAcceptable())
		return result;

	//what happens next?
	if (AttackEndsTurn(unit.pUnit, unit.iAttacksLeft))
	{
		result->iRemainingMoves = 0;
	}
	else
	{
		if (!unit.pUnit->IsFreeAttackMoves())
			result->iRemainingMoves -= min((int)result->iRemainingMoves, GD_INT_GET(MOVE_DENOMINATOR));

		//a bonus the further we can disengage after attacking
		iBonusScore += (result->iRemainingMoves * 2) / GD_INT_GET(MOVE_DENOMINATOR);

		// === RANGED HIT-AND-RUN (Skirmishers, Mounted Archers) ===
		if (unit.pUnit->canMoveAfterAttacking() && result->iRemainingMoves > 0)
		{
			bool bHasSafePlot = (gSafePlotCount[unit.iUnitID] > 0);
			int iRetreatMoves = result->iRemainingMoves / GD_INT_GET(MOVE_DENOMINATOR);

			if (bHasSafePlot)
			{
				iBonusScore += 10;
				if (iRetreatMoves >= 2)
					iBonusScore += iRetreatMoves * 2;
				if (unit.pUnit->baseMoves(false) >= 4 && iRetreatMoves >= 2)
					iBonusScore += 8;
			}

			int iCurrentDanger = unit.pUnit->GetDanger(assumedUnitPlot->getPlot());
			if (iCurrentDanger > 0 && bHasSafePlot && iRetreatMoves >= 1)
				iBonusScore += 5;
		}
	}

	//a slight boost for attacking the "real" target
	if ( enemyPlot->getPlotIndex()==assumedPosition.getTarget()->GetPlotIndex() )
		iBonusScore += 2;

	result->AddScore(0, iBonusScore, 0);

	// === SKIRMISHER/MOUNTED ARCHER TARGET PRIORITIZATION ===
	// Fast ranged units (chariot archers, horse archers, Keshiks, Camel Archers) should prioritize:
	// 1. Siege units - can kite and destroy them safely
	// 2. Slow melee units - can shoot and retreat before they close
	// 3. Wounded units - finish them off with ranged fire
	// 4. Isolated targets - no supporting fire to worry about
	if (!enemyPlot.isEnemyCity() && unit.pUnit->getDomainType() == DOMAIN_LAND && unit.pUnit->IsCanAttackRanged())
	{
		CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
		if (pEnemyUnit)
		{
			// Identify skirmisher-type units: ranged with high mobility or move-after-attack
			bool bIsSkirmisher = (unit.pUnit->AI_getUnitAIType() == UNITAI_SKIRMISHER ||
								  unit.pUnit->getUnitInfo().GetUnitAIType(UNITAI_SKIRMISHER));
			bool bIsFastRanged = (unit.pUnit->baseMoves(false) >= 3);
			bool bCanKite = unit.pUnit->canMoveAfterAttacking();
			
			UnitAITypes eEnemyAI = pEnemyUnit->AI_getUnitAIType();
			
			// Target prioritization for skirmishers and mounted archers
			if (bIsSkirmisher || (bIsFastRanged && bCanKite))
			{
				// PRIORITY 1: Siege units - skirmishers can safely harass slow siege
				if (eEnemyAI == UNITAI_CITY_BOMBARD)
				{
					newAssignment.iBonusScore += 20; // High priority - siege can't catch us
					
					// Bonus for killing siege - removes major threat
					if (bIsKill)
						newAssignment.iBonusScore += 15;
					
					// Extra bonus if we can move after attack (perfect kiting)
					if (bCanKite && newAssignment.iRemainingMoves > 0)
						newAssignment.iBonusScore += 10;
				}
				// PRIORITY 2: Slow melee units (infantry, pikemen)
				// Skirmishers excel at kiting slow melee units
				else if (!pEnemyUnit->IsCanAttackRanged())
				{
					int iEnemyMoves = pEnemyUnit->baseMoves(false);
					
					// Slow enemy (2 moves or less) - easy to kite
					if (iEnemyMoves <= 2)
					{
						newAssignment.iBonusScore += 12;
						
						// Perfect target if we have more mobility
						if (unit.pUnit->baseMoves(false) > iEnemyMoves + 1)
							newAssignment.iBonusScore += 5;
					}
					// Fast melee (cavalry, knights) - dangerous, be cautious
					else if (iEnemyMoves >= 4)
					{
						// Enemy cavalry can catch us - only attack if safe
						if (!bCanKite || newAssignment.iRemainingMoves == 0)
							newAssignment.iBonusScore -= 8; // Risky without escape
					}
				}
				// Counter-skirmisher warfare - enemy ranged units
				else if (pEnemyUnit->IsCanAttackRanged())
				{
					// Against other ranged, kills are valuable
					if (bIsKill)
						newAssignment.iBonusScore += 10;
					
					// Caution against long-range ranged (crossbows, gatling guns)
					if (pEnemyUnit->GetRange() >= unit.pUnit->GetRange())
					{
						// Enemy has equal or better range - careful
						if (!bCanKite)
							newAssignment.iBonusScore -= 5;
					}
				}
				
				// PRIORITY 3: Wounded enemies - finish them with ranged fire
				int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
				int iEnemyMaxHP = pEnemyUnit->GetMaxHitPoints();
				int iEnemyHPPercent = (iEnemyHP * 100) / iEnemyMaxHP;
				
				if (iEnemyHPPercent <= 50)
				{
					// Wounded enemy - opportunistic kill
					newAssignment.iBonusScore += (100 - iEnemyHPPercent) / 8; // Up to +6 for nearly dead
					
					// Big bonus for finishing wounded targets
					if (bIsKill)
						newAssignment.iBonusScore += 12;
				}
				
				// PRIORITY 4: Isolated enemies - safe to harass
				int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
				if (iEnemySupport == 0)
				{
					newAssignment.iBonusScore += 8; // No friends to help them
				}
				else if (iEnemySupport >= 2)
				{
					// Multiple enemies nearby - careful of being caught
					if (!bCanKite || !gSafePlotCount[unit.iUnitID])
						newAssignment.iBonusScore -= 5;
				}
				
				// Workers and settlers - easy targets for mounted raiders
				if (pEnemyUnit->IsCivilianUnit())
				{
					newAssignment.iBonusScore += 15;
					
					// Settlers are extremely valuable
					if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SETTLE)
						newAssignment.iBonusScore += 20;
				}
			}
		}
	}

	// === RANGED FEATURE-SPECIFIC ATTACK BONUSES (Woodsman, Jungle Fighter, etc.) ===
	// Ranged units with feature-specific attack bonuses should prioritize targets in those features
	// Note: For ranged, the bonus applies to the TARGET'S terrain, not the attacker's position
	if (!enemyPlot.isEnemyCity() && unit.pUnit->getDomainType() == DOMAIN_LAND && unit.pUnit->IsCanAttackRanged())
	{
		const CvPlot* pTargetPlot = enemyPlot.getPlot();
		if (pTargetPlot)
		{
			FeatureTypes eTargetFeature = pTargetPlot->getFeatureType();
			if (eTargetFeature != NO_FEATURE)
			{
				// Check if we have attack bonus against targets IN this feature
				int iFeatureAttackBonus = unit.pUnit->featureAttackModifier(eTargetFeature);
				if (iFeatureAttackBonus > 0)
				{
					// Prefer shooting at enemies in terrain we have bonus against
					newAssignment.iBonusScore += iFeatureAttackBonus / 3; // +8-15 typically
					
					// Extra for kills
					if (bIsKill)
						newAssignment.iBonusScore += iFeatureAttackBonus / 5;
				}
			}
		}
	}

	// === ANTI-ARMOR TARGET PRIORITIZATION (Bazookas, AT Guns, etc.) ===
	// Units with significant combat modifiers against specific unit types should prioritize those targets.
	// Bazookas have PROMOTION_ANTI_TANK (+50% vs UNITCOMBAT_ARMOR).
	// AT Guns and similar units have bonuses against tanks/armored vehicles.
	// These units should hunt armor whenever possible - it's their primary role.
	if (!enemyPlot.isEnemyCity() && unit.pUnit->getDomainType() == DOMAIN_LAND && 
		unit.pUnit->IsCanAttackRanged() && !unit.pUnit->IsHoveringUnit()) // Non-helicopter ranged
	{
		CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
		if (pEnemyUnit && pEnemyUnit->IsCombatUnit())
		{
			UnitCombatTypes eEnemyCombatType = pEnemyUnit->getUnitCombatType();
			
			if (eEnemyCombatType != NO_UNITCOMBAT)
			{
				// Check for anti-type bonus (e.g., Bazooka's +50% vs Armor)
				int iAntiTypeBonus = unit.pUnit->unitCombatModifier(eEnemyCombatType);
				
				// Only apply if we have a significant bonus (25%+)
				if (iAntiTypeBonus >= 25)
				{
					// This is our primary target type - prioritize it!
					newAssignment.iBonusScore += 25;
					
					// Scale bonus with our advantage magnitude
					newAssignment.iBonusScore += iAntiTypeBonus / 5; // +10 at 50%, +20 at 100%
					
					// Extra bonus for kills - removing the threat we're designed to counter
					if (bIsKill)
						newAssignment.iBonusScore += 20;
					
					// Modern tanks are high-value targets (more threatening)
					if (pEnemyUnit->GetBaseCombatStrength() >= 60)
						newAssignment.iBonusScore += 10;
					
					// Slightly less valuable if enemy is heavily wounded (will die anyway)
					int iEnemyHPPercent = (pEnemyUnit->GetCurrHitPoints() * 100) / pEnemyUnit->GetMaxHitPoints();
					if (iEnemyHPPercent <= 25 && !bIsKill)
					{
						// Nearly dead - save our anti-armor for healthy targets if possible
						newAssignment.iBonusScore -= 10;
					}
				}
			}
			
			// PENALTY: Anti-armor units are less effective against non-armor targets
			// Bazookas have -25% vs fortified units and cities (from COVERING_FIRE promotion)
			// Check if we have a PENALTY against this target type
			if (eEnemyCombatType != NO_UNITCOMBAT)
			{
				int iPenaltyMod = unit.pUnit->unitCombatModifier(eEnemyCombatType);
				if (iPenaltyMod < 0)
				{
					// We have a combat penalty against this type
					// Discourage attacking (save ammo for better targets)
					newAssignment.iBonusScore += iPenaltyMod / 2; // e.g., -12 for -25% penalty
				}
			}
			
			// Also check fortification penalty (Bazookas are bad vs fortified)
			if (pEnemyUnit->IsFortified())
			{
				int iFortifiedPenalty = unit.pUnit->attackFortifiedModifier();
				if (iFortifiedPenalty < 0)
				{
					// Penalty for attacking fortified enemies
					newAssignment.iBonusScore += iFortifiedPenalty / 3; // e.g., -8 for -25% penalty
				}
			}
		}
	}

	// === HELICOPTER GUNSHIP TACTICS ===
	// Helicopters are extremely mobile but vulnerable to anti-air. They excel at:
	// - Surgical strikes on isolated targets
	// - Picking off wounded units
	// - Destroying siege/artillery before it can fire
	// - Avoiding AA coverage zones at all costs
	if (unit.pUnit->getDomainType() == DOMAIN_LAND && unit.pUnit->IsCanAttackRanged() && 
		unit.pUnit->IsHoveringUnit())
	{
		CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
		const CvPlot* pTargetPlot = enemyPlot.getPlot();
		
		// 1. AA AVOIDANCE: Check for anti-air threats near target
		// This is CRITICAL - helicopters take massive damage from AA
		int iAAThreatsNearTarget = 0;
		int iMaxAADamage = 0;
		
		for (int i = RING0_PLOTS; i < RING_PLOTS[3]; i++) // Check 3-tile radius
		{
			CvPlot* pLoopPlot = iterateRingPlots(pTargetPlot, i);
			if (!pLoopPlot)
				continue;
			
			CvUnit* pPotentialAA = pLoopPlot->getBestDefender(NO_PLAYER, unit.pUnit->getOwner(), NULL, true);
			if (pPotentialAA && pPotentialAA->canIntercept() && pPotentialAA->getDomainType() != DOMAIN_AIR)
			{
				iAAThreatsNearTarget++;
				int iAADamage = pPotentialAA->GetInterceptionDamage(unit.pUnit, false, pTargetPlot);
				if (iAADamage > iMaxAADamage)
					iMaxAADamage = iAADamage;
			}
		}
		
		// Heavy penalty for attacking into AA coverage
		if (iAAThreatsNearTarget > 0)
		{
			// Estimate interception damage as fraction of our HP
			int iOurHP = unit.pUnit->GetCurrHitPoints();
			int iDamagePercent = (iMaxAADamage * 100) / max(1, iOurHP);
			
			// Scale penalty by damage risk
			if (iDamagePercent >= 50)
			{
				// Would take serious damage - heavily discourage unless it's a kill
				if (!bIsKill)
					newAssignment.iBonusScore -= 60;
				else
					newAssignment.iBonusScore -= 30; // Still risky but kill might be worth it
			}
			else if (iDamagePercent >= 25)
			{
				newAssignment.iBonusScore -= 25;
			}
			else
			{
				newAssignment.iBonusScore -= 10; // Mild risk
			}
			
			// Extra penalty for multiple AA (crossfire)
			if (iAAThreatsNearTarget >= 2)
				newAssignment.iBonusScore -= iAAThreatsNearTarget * 15;
		}
		else
		{
			// No AA coverage - helicopter can operate freely!
			newAssignment.iBonusScore += 15; // Safe operating zone
		}
		
		// 2. TARGET PRIORITY: Helicopters excel at specific targets
		if (pEnemyUnit && !enemyPlot.isEnemyCity())
		{
			UnitAITypes eEnemyAI = pEnemyUnit->AI_getUnitAIType();
			UnitCombatTypes eEnemyCombatType = pEnemyUnit->getUnitCombatType();
			
			// PRIORITY A: TANK HUNTING - Helicopters have anti-armor bonus
			// Check if we have a combat modifier bonus against this unit type
			int iAntiArmorBonus = 0;
			if (eEnemyCombatType != NO_UNITCOMBAT)
			{
				iAntiArmorBonus = unit.pUnit->unitCombatModifier(eEnemyCombatType);
			}
			
			if (iAntiArmorBonus >= 25) // Significant bonus (typically 50%+ vs armor)
			{
				// This is a prime target - exploit our anti-armor advantage
				newAssignment.iBonusScore += 30;
				
				// Scale bonus with our advantage
				newAssignment.iBonusScore += iAntiArmorBonus / 5; // +10 at 50%, +20 at 100%
				
				// Extra bonus for killing armor (removes threat, exploits bonus fully)
				if (bIsKill)
					newAssignment.iBonusScore += 25;
				
				// Even more valuable if it's a modern tank (high strength)
				if (pEnemyUnit->GetBaseCombatStrength() >= 60)
					newAssignment.iBonusScore += 15;
			}
			// PRIORITY B: Siege/Artillery - destroy before they bombard our cities
			else if (eEnemyAI == UNITAI_CITY_BOMBARD)
			{
				newAssignment.iBonusScore += 25;
				
				// Extra bonus for killing siege
				if (bIsKill)
					newAssignment.iBonusScore += 20;
			}
			// PRIORITY C: Enemy AA units - eliminate threats to our air operations
			else if (pEnemyUnit->canIntercept())
			{
				// Very high value target - removing AA opens airspace
				newAssignment.iBonusScore += 30;
				
				if (bIsKill)
					newAssignment.iBonusScore += 25;
			}
			// PRIORITY D: Wounded enemies - surgical finishing
			else
			{
				int iEnemyHPPercent = (pEnemyUnit->GetCurrHitPoints() * 100) / pEnemyUnit->GetMaxHitPoints();
				if (iEnemyHPPercent <= 40)
				{
					// Heavily wounded - perfect helicopter target
					newAssignment.iBonusScore += 15;
					
					if (bIsKill)
						newAssignment.iBonusScore += 10;
				}
			}
			
			// PRIORITY E: Isolated targets - helicopter can reach where others can't
			int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
			if (iEnemySupport == 0)
			{
				// Isolated unit - helicopter's reach advantage
				newAssignment.iBonusScore += 12;
			}
			
			// PRIORITY E: Supply line disruption - workers, settlers behind enemy lines
			if (pEnemyUnit->IsCivilianUnit())
			{
				newAssignment.iBonusScore += 20;
				
				// Settlers are prime targets
				if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SETTLE)
					newAssignment.iBonusScore += 15;
			}
		}
		
		// 3. AVOID FORTIFIED/ENTRENCHED TARGETS: Helicopters waste their mobility on these
		// Better to let ground forces handle dug-in enemies
		if (pEnemyUnit && pEnemyUnit->IsCombatUnit() && !pEnemyUnit->IsCanAttackRanged())
		{
			if (pEnemyUnit->IsFortified() || pEnemyUnit->fortifyModifier() > 0)
			{
				// Fortified enemy - less valuable target for helicopter
				newAssignment.iBonusScore -= 8;
			}
		}
	}
	
	// === LIGHT TANK / ARMORED CAR TACTICS ===
	// Light tanks: Fast ranged units with moderate armor. Different from helicopters:
	// - Bound by terrain (no hovering)
	// - Not vulnerable to AA
	// - Good for reconnaissance, screening, exploiting gaps
	// - Can engage enemy armor but prefer softer targets
	// Detection: Fast ranged (4+ moves), moderate strength (30-60), land unit, not hovering
	if (unit.pUnit->getDomainType() == DOMAIN_LAND && unit.pUnit->IsCanAttackRanged() &&
		unit.pUnit->baseMoves(false) >= 4 && !unit.pUnit->IsHoveringUnit())
	{
		int iOurStrength = unit.pUnit->GetBaseCombatStrength();
		bool bIsLightTank = (iOurStrength >= 30 && iOurStrength < 60);
		
		if (bIsLightTank && !enemyPlot.isEnemyCity())
		{
			CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
			if (pEnemyUnit)
			{
				int iEnemyStrength = pEnemyUnit->GetBaseCombatStrength();
				UnitAITypes eEnemyAI = pEnemyUnit->AI_getUnitAIType();
				
				// 1. RECONNAISSANCE ROLE: Light tanks should spot and report, not brawl
				// Prefer softer targets over heavy armor
				if (iEnemyStrength > iOurStrength + 20)
				{
					// Enemy is much stronger (heavy tank or fortified unit)
					// Light tank should harass, not engage directly
					if (!bIsKill)
						newAssignment.iBonusScore -= 15; // Don't pick fights we can't win
				}
				else if (iEnemyStrength < iOurStrength - 10)
				{
					// Weaker enemy - good target for light tank
					newAssignment.iBonusScore += 10;
				}
				
				// 2. SCREENING ROLE: Engage enemy recon and light units
				// Counter enemy light units and scouts
				if (pEnemyUnit->baseMoves(false) >= 3 && !pEnemyUnit->IsCanAttackRanged())
				{
					// Enemy fast melee (cavalry/light armor) - good target for light tank
					newAssignment.iBonusScore += 12;
					
					if (bIsKill)
						newAssignment.iBonusScore += 8;
				}
				
				// 3. EXPLOIT GAPS: Light tanks can quickly exploit weak points
				// Prefer targets on the flanks of enemy formations
				int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
				if (iEnemySupport == 0)
				{
					// Isolated/flanked enemy
					newAssignment.iBonusScore += 10;
				}
				else if (iEnemySupport == 1)
				{
					// Lightly supported - still viable
					newAssignment.iBonusScore += 5;
				}
				else if (iEnemySupport >= 3)
				{
					// Heavily defended - not ideal for light tank
					newAssignment.iBonusScore -= 8;
				}
				
				// 4. FIRE SUPPORT: Prioritize helping our heavier units
				// Check if friendly heavy armor is engaged with this target
				vector<SUnitStats> allUnits = assumedPosition.getAvailableUnits();
				allUnits.insert(allUnits.end(), assumedPosition.getFinishedUnits().begin(), assumedPosition.getFinishedUnits().end());
				
				bool bFriendlyArmorEngaged = false;
				for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
				{
					const CvUnit* pFriendly = it->pUnit;
					if (!pFriendly || pFriendly == unit.pUnit)
						continue;
					
					// Check for heavy armor (strength >= 60, fast melee)
					if (pFriendly->GetBaseCombatStrength() >= 60 && 
						pFriendly->baseMoves(false) >= 4 && 
						!pFriendly->IsCanAttackRanged())
					{
						int iDistToEnemy = plotDistance(it->iPlotIndex, enemyPlot.getPlotIndex());
						if (iDistToEnemy <= 2)
						{
							bFriendlyArmorEngaged = true;
							break;
						}
					}
				}
				
				if (bFriendlyArmorEngaged)
				{
					// Support our heavy tanks
					newAssignment.iBonusScore += 15;
					
					// Extra bonus if this softens target for heavy armor kill
					if (!bIsKill && newAssignment.iDamage > pEnemyUnit->GetCurrHitPoints() / 3)
						newAssignment.iBonusScore += 8;
				}
				
				// 5. SOFT TARGETS: Light tanks excel against infantry, siege, supply
				if (eEnemyAI == UNITAI_CITY_BOMBARD)
				{
					newAssignment.iBonusScore += 15; // Artillery is vulnerable
				}
				else if (pEnemyUnit->IsCivilianUnit())
				{
					newAssignment.iBonusScore += 12; // Raid supply lines
				}
			}
		}
	}

	// NAVAL RANGED SOFTENING: Naval ranged units should prioritize targets that naval melee can capture/finish
	// This creates proper combined arms: frigates/battleships soften, then destroyers/privateers capture
	if (unit.pUnit->getDomainType() == DOMAIN_SEA && unit.pUnit->IsCanAttackRanged())
	{
		bool bHaveNavalMelee = false;
		bool bNavalMeleeCanReach = false;
		int iNavalMeleeDamage = 0;
		int iNavalBonus = 0;

		vector<SUnitStats> allUnits = assumedPosition.getAvailableUnits();
		const vector<SUnitStats>& finishedUnits = assumedPosition.getFinishedUnits();
		allUnits.insert(allUnits.end(), finishedUnits.begin(), finishedUnits.end());

		for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
		{
			const CvUnit* pLoopUnit = it->pUnit;
			if (!pLoopUnit || pLoopUnit->IsCanAttackRanged())
				continue;

			if (pLoopUnit->getDomainType() == DOMAIN_SEA && pLoopUnit->IsCanAttackWithMove())
			{
				bHaveNavalMelee = true;
				int iMeleeDistance = plotDistance(it->iPlotIndex, enemyPlot->getPlotIndex());
				if (iMeleeDistance <= 2)
				{
					bNavalMeleeCanReach = true;
					if (enemyPlot->isEnemyCity())
					{
						CvCity* pCity = enemyPlot->getPlot()->getPlotCity();
						if (pCity)
							iNavalMeleeDamage += pLoopUnit->GetMaxAttackStrength(NULL, NULL, NULL) / 10;
					}
					else if (enemyPlot->getEnemyUnit())
					{
						iNavalMeleeDamage += pLoopUnit->GetMaxAttackStrength(NULL, NULL, enemyPlot->getEnemyUnit()) / 10;
					}
				}
			}
		}

		if (bHaveNavalMelee && bNavalMeleeCanReach)
		{
			if (enemyPlot->isEnemyCity())
			{
				CvCity* pTargetCity = enemyPlot->getPlot()->getPlotCity();
				if (pTargetCity && pTargetCity->isCoastal())
				{
					int iCityHP = pTargetCity->GetMaxHitPoints() - pTargetCity->getDamage();
					int iHPAfterAttack = iCityHP - result->iCityDamage;
					if (iHPAfterAttack > 0 && iHPAfterAttack <= iNavalMeleeDamage)
						iNavalBonus += 50;
					else if (iHPAfterAttack > 0 && iHPAfterAttack < iCityHP / 2)
						iNavalBonus += 25;
					else
						iNavalBonus += 10;
				}
			}
			else if (enemyPlot->getEnemyUnit() && enemyPlot->getEnemyUnit()->getDomainType() == DOMAIN_SEA)
			{
				CvUnit* pEnemy = enemyPlot->getEnemyUnit();
				int iEnemyHP = pEnemy->GetCurrHitPoints();
				int iDmgToEnemy = result->unitDamage.GetValue(pEnemy->GetID());
				int iHPAfterAttack = iEnemyHP - iDmgToEnemy;
				if (iHPAfterAttack > 0 && iHPAfterAttack <= iNavalMeleeDamage)
				{
					iNavalBonus += 30;
					if (pEnemy->AI_getUnitAIType() == UNITAI_CARRIER_SEA ||
						pEnemy->GetBaseCombatStrength() > unit.pUnit->GetBaseCombatStrength())
					{
						iNavalBonus += 20;
					}
				}
				else if (iDmgToEnemy > iEnemyHP / 3)
					iNavalBonus += 15;
			}
		}

		// Submarine first-strike: any invisible naval unit benefits when undetected
		if (unit.pUnit->getInvisibleType() != NO_INVISIBLE && bHaveNavalMelee)
		{
			if (enemyPlot->getEnemyUnit() && !enemyPlot->getPlot()->IsKnownVisibleToEnemy(unit.pUnit->getOwner()))
				iNavalBonus += 15;
		}

		// NAVAL FLEET CONCENTRATION: Prioritize targets that multiple friendly ships can engage
		{
			int iFriendlyShipsCanEngage = 0;
			int iFriendlyRangedDamage = 0;
			int iFriendlyMeleeDamage = 0;

			for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
			{
				const CvUnit* pLoopUnit = it->pUnit;
				if (!pLoopUnit || pLoopUnit == unit.pUnit)
					continue;
				if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
					continue;

				int iDistToTarget = plotDistance(it->iPlotIndex, enemyPlot->getPlotIndex());

				if (pLoopUnit->IsCanAttackRanged())
				{
					if (iDistToTarget <= pLoopUnit->GetRange())
					{
						iFriendlyShipsCanEngage++;
						iFriendlyRangedDamage += pLoopUnit->GetMaxRangedCombatStrength(NULL, NULL, true, NULL) / 10;
					}
				}
				else if (iDistToTarget <= 2)
				{
					iFriendlyShipsCanEngage++;
					if (enemyPlot->getEnemyUnit())
						iFriendlyMeleeDamage += pLoopUnit->GetMaxAttackStrength(NULL, NULL, enemyPlot->getEnemyUnit()) / 10;
					else
						iFriendlyMeleeDamage += pLoopUnit->GetMaxAttackStrength(NULL, NULL, NULL) / 10;
				}
			}

			if (iFriendlyShipsCanEngage >= 1)
			{
				iNavalBonus += 15;
				iNavalBonus += min(iFriendlyShipsCanEngage * 8, 30);

				int iThisDamage = enemyPlot->isEnemyCity() ? result->iCityDamage : (enemyPlot->getEnemyUnit() ? result->unitDamage.GetValue(enemyPlot->getEnemyUnit()->GetID()) : 0);
				int iTotalFleetDamage = iThisDamage + iFriendlyRangedDamage + iFriendlyMeleeDamage;
				int iTargetHP = 0;
				if (enemyPlot->isEnemyCity())
				{
					CvCity* pCity = enemyPlot->getPlot()->getPlotCity();
					if (pCity)
						iTargetHP = pCity->GetMaxHitPoints() - pCity->getDamage();
				}
				else if (enemyPlot->getEnemyUnit())
				{
					iTargetHP = enemyPlot->getEnemyUnit()->GetCurrHitPoints();
				}

				if (iTargetHP > 0)
				{
					if (iTotalFleetDamage >= iTargetHP)
						iNavalBonus += 40;
					else if (iTotalFleetDamage >= iTargetHP * 2 / 3)
						iNavalBonus += 20;
				}

				if (enemyPlot->getEnemyUnit())
				{
					CvUnit* pEnemy = enemyPlot->getEnemyUnit();
					if (pEnemy->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
						iNavalBonus += 30;
					else if (pEnemy->GetBaseCombatStrength() > unit.pUnit->GetBaseCombatStrength())
						iNavalBonus += 15;
				}
			}
		}

		// CARRIER ATTACK CAUTION: Carriers should rarely attack - let other ships fight
		if (unit.pUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
		{
			bool bCarrierHasCargo = unit.pUnit->hasCargo();
			int iCarrierDanger = unit.pUnit->GetDanger(assumedUnitPlot->getPlot());
			int iCarrierHP = unit.pUnit->GetCurrHitPoints() - result->iSelfDamage;
			bool bIsSafeAttack = (iCarrierDanger < iCarrierHP / 3);

			if (!bIsSafeAttack)
			{
				int iPenalty = 30;
				if (bCarrierHasCargo)
					iPenalty += unit.pUnit->getCargo() * 15;
				iNavalBonus -= iPenalty;
			}

			// Defer to other ships when they could attack the same target
			bool bOtherShipsCanAttack = false;
			for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
			{
				const CvUnit* pLoopUnit = it->pUnit;
				if (!pLoopUnit || pLoopUnit == unit.pUnit)
					continue;
				if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
					continue;
				if (pLoopUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
					continue;

				int iDistToTarget = plotDistance(it->iPlotIndex, enemyPlot->getPlotIndex());
				if (pLoopUnit->IsCanAttackRanged() && iDistToTarget <= pLoopUnit->GetRange())
				{
					bOtherShipsCanAttack = true;
					break;
				}
				else if (!pLoopUnit->IsCanAttackRanged() && iDistToTarget <= 2)
				{
					bOtherShipsCanAttack = true;
					break;
				}
			}
			if (bOtherShipsCanAttack)
				iNavalBonus -= 20;
		}

		if (iNavalBonus != 0)
			result->AddScore(0, iNavalBonus, 0);
	}

	// === ENEMY COMBAT BONUS IMPROVEMENT AWARENESS (Shoshone Encampment, Fort, Citadel, etc.) ===
	if (!enemyPlot->isEnemyCity())
	{
		CvUnit* pEnemyUnit = enemyPlot->getEnemyUnit();
		if (pEnemyUnit && pEnemyUnit->IsCombatUnit())
		{
			const CvPlot* pTargetPlot = enemyPlot->getPlot();
			int iDefBonusPenalty = 0;

			int iEnemyImprovementBonus = pEnemyUnit->GetNearbyImprovementModifier(pTargetPlot);
			if (iEnemyImprovementBonus > 0)
				iDefBonusPenalty -= iEnemyImprovementBonus / 2;

			if (pTargetPlot)
			{
				ImprovementTypes eImprovement = pTargetPlot->getImprovementType();
				if (eImprovement != NO_IMPROVEMENT)
				{
					CvImprovementEntry* pkImprovement = GC.getImprovementInfo(eImprovement);
					if (pkImprovement)
					{
						int iFortDefense = pkImprovement->GetDefenseModifier();
						if (iFortDefense >= 25)
							iDefBonusPenalty -= iFortDefense / 5;
					}
				}
			}

			if (iDefBonusPenalty != 0)
				result->AddScore(0, iDefBonusPenalty, 0);
		}
	}

	return result;
}

static STacticalAssignment* ScorePlotForMeleeAttack(const SUnitStats& unit, const CvTacticalPlot* assumedUnitPlot, const CvTacticalPlot* enemyPlot, int iAssumedMovesLeft, const CvTacticalPosition& assumedPosition)
{
	//default action is invalid
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex, enemyPlot->getPlotIndex(), unit.iUnitID, 0, unit.eMoveStrategy, A_MELEEATTACK, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	int iBonusScore = 0;

	//the plot we're checking right now
	const CvPlot* pEnemyPlot = enemyPlot->getPlot();

	//sanity check
	if (!enemyPlot || !assumedUnitPlot)
		return result;

	//this is only for melee attacks - ranged attacks are handled separately
	const CvUnit* pUnit = unit.pUnit;
	if (!enemyPlot->isEnemy() || pUnit->IsCanAttackRanged())
		return result;

	//how often can we attack this turn (depending on moves left on the unit)
	int iMaxAttacks = NumAttacksForUnit(unit.iMovesLeft, unit.iAttacksLeft, pUnit->IsFreeAttackMoves());

	if (iMaxAttacks == 0)
		return result;

	//check how much damage we could do
	ScoreAttackDamage(enemyPlot, pUnit, assumedUnitPlot, assumedPosition, gTactPosStorage.getAttackCache(), result, unit.iSelfDamage);
	if (!result->IsAcceptable())
		return result;

	//what happens next? capturing a city always ends the turn
	if (AttackEndsTurn(pUnit, iMaxAttacks) ||
		CvUnitMovement::IsSlowedByZOC(pUnit,assumedUnitPlot->getPlot(),pEnemyPlot,assumedPosition.getFreedPlots()) ||
		(enemyPlot->isEnemyCity() && result->eAssignmentType == A_MELEEKILL))
		//end turn cost will be checked in time, no need to panic yet
		result->iRemainingMoves = 0;
	else
	{
		int iMoveCost = unit.iMovesLeft - iAssumedMovesLeft;
		int iAttackCost = pUnit->IsFreeAttackMoves() ? 0 : max(iMoveCost, GD_INT_GET(MOVE_DENOMINATOR));
		result->iRemainingMoves = max(0,unit.iMovesLeft - iAttackCost);
	}

	// === HIT-AND-RUN TACTICS ===
	// Units that can move after attacking should leverage this for safer attacks
	// This is especially valuable for fast units with high moves remaining
	bool bCanMoveAfterAttack = pUnit->canMoveAfterAttacking() && result.iRemainingMoves > 0;
	bool bHasSafePlot = (gSafePlotCount[unit.iUnitID] > 0);
	
	if (bCanMoveAfterAttack)
	{
		// Calculate retreat potential based on remaining moves
		int iRetreatMoves = result.iRemainingMoves / GD_INT_GET(MOVE_DENOMINATOR);
		
		// Bonus for having escape route - scaled by remaining movement
		if (bHasSafePlot)
		{
			// Base hit-and-run bonus - unit can attack and escape
			result.iBonusScore += 8;
			
			// Extra bonus per movement point available for retreat
			result.iBonusScore += iRetreatMoves * 3;
			
			// Fast units (3+ moves after attack) are excellent at hit-and-run
			if (iRetreatMoves >= 3)
			{
				result.iBonusScore += 10; // cavalry charge & retreat
				
				// If unit has high base moves, it's a dedicated fast unit
				if (pUnit->baseMoves(false) >= 4)
					result.iBonusScore += 5; // cavalry/lancer/mounted archer bonus
			}
		}
		else
		{
			// Can move but nowhere safe to go - risky hit-and-run
			// Still slightly better than being stuck, but not by much
			result.iBonusScore += 2;
		}
		
		// Hit-and-run units prefer wounded targets they can finish quickly
		// Attack, kill, retreat before enemy can respond
		if (bIsKill && bHasSafePlot)
		{
			result.iBonusScore += 15; // Safe kill - ideal hit-and-run
			
			// Extra bonus for killing ranged/siege units that threaten our lines
			CvUnit* pEnemy = enemyPlot.getEnemyUnit();
			if (pEnemy && pEnemy->IsCanAttackRanged())
			{
				result.iBonusScore += 10; // Silencing enemy ranged is valuable
			}
		}
		
		// Penalize attacks that leave us in danger with no escape
		if (!bHasSafePlot && !bIsKill)
		{
			int iDanger = pUnit->GetDanger(assumedUnitPlot.getPlot(), assumedPosition.getKilledEnemies(), result.iSelfDamage);
			if (iDanger > pUnit->GetCurrHitPoints() / 2)
			{
				// High danger attack with no escape - discourage unless it's a kill
				result.iBonusScore -= 10;
			}
		}
	}
	else if (!pUnit->canMoveAfterAttacking() && pUnit->baseMoves(false) >= 3)
	{
		// Fast unit without hit-and-run capability (no promotion yet)
		// Slight penalty for risky attacks where retreat would be valuable
		int iAdjacentEnemies = enemyPlot.getNumAdjacentEnemies(DomainForUnit(pUnit));
		if (iAdjacentEnemies >= 2 && !bIsKill)
		{
			result.iBonusScore -= 3; // Would benefit from hit-and-run but can't do it
		}
	}

	// === CITY ATTACK MODIFIERS ===
	// Different unit types have different effectiveness against cities
	// Tanks: No city attack penalty (breakthrough weapons)
	// Cavalry/Mounted: City attack penalty (historically ineffective vs fortifications)
	if (enemyPlot.isEnemyCity() && pUnit->getDomainType() == DOMAIN_LAND && !pUnit->IsCanAttackRanged())
	{
		bool bIsFastMelee = (pUnit->baseMoves(false) >= 3);
		int iOurStrength = pUnit->GetBaseCombatStrength();
		bool bIsModernArmor = (bIsFastMelee && iOurStrength >= 60);
		
		// Check for city attack modifiers on the unit
		int iCityAttackMod = pUnit->cityAttackModifier();
		
		if (bIsModernArmor)
		{
			// TANKS: Designed for breakthrough - good at city assault
			// No penalty, and bonus for capturing cities
			result.iBonusScore += 15; // Tanks are effective city attackers
			
			// Extra bonus if this would capture the city
			if (bIsKill)
			{
				result.iBonusScore += 25; // Tank breakthrough captures city!
			}
			
			// Tanks with infantry support are even better at city assault
			int iFriendlyInfantryNearby = 0;
			for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
			{
				CvPlot* pLoopPlot = iterateRingPlots(pEnemyPlot, i);
				if (pLoopPlot)
				{
					CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
					if (pFriendly && !pFriendly->IsCanAttackRanged() && 
						pFriendly->AI_getUnitAIType() != UNITAI_FAST_ATTACK &&
						pFriendly->baseMoves(false) <= 3)
					{
						iFriendlyInfantryNearby++;
					}
				}
			}
			
			if (iFriendlyInfantryNearby >= 2)
			{
				result.iBonusScore += 12; // Combined arms city assault
			}
			else if (iFriendlyInfantryNearby == 1)
			{
				result.iBonusScore += 6;
			}
			
			// Apply any city attack modifier bonus the unit has
			if (iCityAttackMod > 0)
			{
				result.iBonusScore += iCityAttackMod / 5; // +4 for 20%, +10 for 50%
			}
		}
		else if (bIsFastMelee && iCityAttackMod < 0)
		{
			// CAVALRY/MOUNTED: Has city attack penalty
			// These units are historically bad at assaulting fortifications
			// Apply penalty based on their city attack modifier
			result.iBonusScore += iCityAttackMod / 3; // e.g., -10 for -33% modifier
			
			// Even worse if this isn't a capture (just chip damage)
			if (!bIsKill)
			{
				result.iBonusScore -= 8; // Cavalry shouldn't be assaulting cities
			}
			
			// Cavalry should prefer other targets when available
			// Small additional penalty to encourage finding unit targets
			result.iBonusScore -= 5;
		}
		else if (bIsFastMelee && iCityAttackMod == 0)
		{
			// Fast melee without explicit penalty (e.g., upgraded cavalry, unique units)
			// Neutral - no bonus or penalty
			// But still prefer infantry for city assault
			if (!bIsKill)
			{
				result.iBonusScore -= 3; // Mild preference for infantry assault
			}
		}
	}

	//don't break formation if there are many enemies around
	if (result->eAssignmentType == A_MELEEKILL && !enemyPlot->isEnemyCity() && enemyPlot->getNumAdjacentEnemies(DomainForUnit(pUnit)) > 3
		&& result->iRemainingMoves == 0)
	{
		result->SetImpossible();
		return result;
	}

	//a slight boost for attacking the "real" target or a citadel
	int iImprovementDamage = TacticalAIHelpers::GetOtherPlayerImprovementDamage(pEnemyPlot, assumedPosition.getPlayer(), true);
	if (pEnemyPlot == assumedPosition.getTarget())
		iBonusScore += 3;
	else if (iImprovementDamage > 0)
		iBonusScore += iImprovementDamage / 10;

	//combo bonus
	if (result->eAssignmentType == A_MELEEKILL && enemyPlot->isEnemyCivilian())
	{
		CvUnit* pCivilian;
		for (int iI = 0; iI < pEnemyPlot->getNumUnits(); iI++)
		{
			pCivilian = pEnemyPlot->getUnitByIndex(iI);
			if (pCivilian && pCivilian->IsCivilianUnit() && GET_PLAYER(pUnit->getOwner()).IsAtWarWith(pCivilian->getOwner()))
			{
				//workers are not so important ...
				iBonusScore += (pCivilian->AI_getUnitAIType() == UNITAI_WORKER) ? 20 : 150;
			}
		}
	}

	// barbarian camp
	if (result->eAssignmentType == A_MELEEKILL && pEnemyPlot->getRevealedImprovementType(pUnit->getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
		iBonusScore += 100;

	result->AddScore(0, iBonusScore, 0);

	// === TERRAIN ATTACK AWARENESS ===
	// Units with open/rough terrain attack bonuses should prefer targets on matching terrain
	// This is especially important for cavalry, knights, and units with terrain promotions
	if (pUnit->getDomainType() == DOMAIN_LAND && !enemyPlot.isEnemyCity())
	{
		bool bEnemyOnOpen = pEnemyPlot->isOpenGround();
		bool bEnemyOnRough = pEnemyPlot->isRoughGround();
		
		int iOpenAttackBonus = pUnit->openAttackModifier();
		int iRoughAttackBonus = pUnit->roughAttackModifier();
		
		if (iOpenAttackBonus != 0 || iRoughAttackBonus != 0)
		{
			// Unit has terrain-specific attack bonuses from promotions or unit type
			if (bEnemyOnOpen && iOpenAttackBonus > 0)
			{
				// Prefer attacking enemies on open terrain when we have open attack bonus
				// This bonus is already factored into damage, but we want to prioritize such attacks
				result.iBonusScore += iOpenAttackBonus / 4; // +5 to +7 typically for cavalry
			}
			else if (bEnemyOnRough && iRoughAttackBonus > 0)
			{
				// Prefer attacking enemies on rough terrain when we have rough attack bonus
				result.iBonusScore += iRoughAttackBonus / 4;
			}
			else if (bEnemyOnRough && iOpenAttackBonus > 0 && iRoughAttackBonus <= 0)
			{
				// Cavalry attacking into rough terrain - no bonus applies
				// Slight penalty to encourage choosing better targets if available
				result.iBonusScore -= iOpenAttackBonus / 8; // mild penalty
			}
			else if (bEnemyOnOpen && iRoughAttackBonus > 0 && iOpenAttackBonus <= 0)
			{
				// Unit with rough bonus attacking on open - not ideal
				result.iBonusScore -= iRoughAttackBonus / 8;
			}
		}
		
		// === FEATURE-SPECIFIC ATTACK BONUSES (Woodsman, Jungle Fighter, etc.) ===
		// Units with feature-specific attack bonuses should prioritize targets on those features
		FeatureTypes eEnemyFeature = pEnemyPlot->getFeatureType();
		if (eEnemyFeature != NO_FEATURE)
		{
			// Check if unit has attack bonus against enemies IN this feature
			int iFeatureAttackBonus = pUnit->featureAttackModifier(eEnemyFeature);
			if (iFeatureAttackBonus > 0)
			{
				// Strong preference for targets we have feature bonus against
				result.iBonusScore += iFeatureAttackBonus / 3; // +8-15 typically
				
				// Extra bonus for kills - our feature advantage makes kills more likely
				if (bIsKill)
					result.iBonusScore += iFeatureAttackBonus / 5;
			}
		}
		
		// Check if we have attack bonus when attacking FROM our current terrain (RoughFromMod/Woodsman)
		const CvPlot* pOurPlot = assumedUnitPlot.getPlot();
		if (pOurPlot)
		{
			FeatureTypes eOurFeature = pOurPlot->getFeatureType();
			if (eOurFeature != NO_FEATURE && pOurPlot->isRoughGround())
			{
				int iRoughFromBonus = pUnit->getExtraRoughFromPercent();
				if (iRoughFromBonus > 0)
				{
					// We're attacking FROM rough terrain with Woodsman-type bonus
					// This bonus makes our attacks stronger
					result.iBonusScore += iRoughFromBonus / 3; // +3-10 typically
					
					// Extra for kills since we're attacking at advantage
					if (bIsKill)
						result.iBonusScore += iRoughFromBonus / 5;
				}
			}
		}
	}

	// === FLANKING BONUS AWARENESS ===
	// Units with high FlankAttackModifier (cavalry, lancers) should prioritize flanking attacks
	// Also consider existing friendly adjacencies to maximize flanking damage
	if (!enemyPlot.isEnemyCity())
	{
		int iFlankMod = pUnit->GetFlankAttackModifier();
		CvTacticalPlot::eTactPlotDomain eUnitDomain = pUnit->getDomainType() == DOMAIN_SEA ? CvTacticalPlot::TD_SEA : CvTacticalPlot::TD_LAND;
		
		// Count friendly units adjacent to the enemy (not including us since we're the attacker)
		int iAdjacentFriendlies = enemyPlot.getNumAdjacentFriendlies(eUnitDomain, unit.iPlotIndex);
		
		// Count enemy units adjacent to target (enemy support)
		int iAdjacentEnemies = enemyPlot.getNumAdjacentEnemies(eUnitDomain);
		
		// Net flanking advantage: more friendlies than enemies = flanking bonus
		int iFlankingAdvantage = iAdjacentFriendlies - iAdjacentEnemies;
		
		if (iFlankingAdvantage > 0)
		{
			// We have flanking advantage - bonus scales with unit's FlankAttackModifier
			// Base flanking is 10% per adjacent friend (BONUS_PER_ADJACENT_FRIEND)
			// FlankAttackModifier adds to this, so cavalry with +25% gets 35% per flanker
			
			int iBaseFlankBonus = iFlankingAdvantage * 3; // +3 per net flanker for all melee
			int iFlankModBonus = (iFlankMod * iFlankingAdvantage) / 10; // Extra for high flank modifier
			
			result.iBonusScore += iBaseFlankBonus + iFlankModBonus;
			
			// Extra bonus for cavalry-type units with high flank modifier
			// These units should strongly prefer flanking attacks
			if (iFlankMod >= 25)
			{
				result.iBonusScore += 5; // cavalry/lancer bonus for any flanking situation
				
				// Big bonus for multiple flankers - cavalry excels at concentrated charges
				if (iAdjacentFriendlies >= 2)
					result.iBonusScore += iFlankMod / 5; // +5 to +10 for multiple flankers
			}
		}
		else if (iFlankingAdvantage < 0 && iFlankMod >= 25)
		{
			// Cavalry attacking without flanking support against supported enemy
			// This is not cavalry's strong suit - mild penalty
			result.iBonusScore -= 5;
		}
		
		// Units with high FlankAttackModifier should avoid attacking isolated enemies
		// when there are flanking opportunities elsewhere
		// (This is a soft preference - isolated enemies are still valid targets)
		if (iAdjacentFriendlies == 0 && iFlankMod >= 25)
		{
			// No flanking support - cavalry prefers to wait for better positioning
			// But don't penalize too much since kills are still valuable
			if (!bIsKill)
				result.iBonusScore -= 3;
		}
	}

	// === CAVALRY TARGET PRIORITIZATION ===
	// Fast melee units (cavalry, lancers, hussars) should prioritize specific target types:
	// 1. Siege units - devastating to armies, fragile when caught
	// 2. Ranged units - can be killed before they fire, especially with hit-and-run
	// 3. Isolated enemies - safer to attack without support
	// 4. Low-defense targets - cavalry excels at destroying weak enemies
	if (!enemyPlot.isEnemyCity() && pUnit->getDomainType() == DOMAIN_LAND)
	{
		CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
		if (pEnemyUnit)
		{
			bool bIsFastMelee = (pUnit->baseMoves(false) >= 3 && !pUnit->IsCanAttackRanged());
			bool bIsCavalry = (pUnit->AI_getUnitAIType() == UNITAI_FAST_ATTACK || 
							   pUnit->getUnitInfo().GetUnitAIType(UNITAI_FAST_ATTACK));
			UnitAITypes eEnemyAI = pEnemyUnit->AI_getUnitAIType();
			
			// Distinguish modern armor (tanks) from ancient/medieval cavalry
			// Tanks have high base combat strength (60+) and typically 5+ moves
			// They use different tactics: breakthrough vs hit-and-run
			int iOurStrength = pUnit->GetBaseCombatStrength();
			bool bIsModernArmor = (bIsFastMelee && iOurStrength >= 60);
			
			// === MODERN ARMOR (TANKS) TACTICS ===
			// Tanks excel at: breakthrough attacks, combined arms, absorbing damage
			// Tanks struggle with: urban combat, anti-tank weapons, fighting alone
			if (bIsModernArmor)
			{
				int iEnemyStrength = pEnemyUnit->GetBaseCombatStrength();
				
				// 1. BREAKTHROUGH PRIORITY: Tanks should break through defensive lines
				// Target units blocking the path to objectives (cities)
				CvCity* pNearbyEnemyCity = pEnemyPlot->GetAdjacentCity();
				if (!pNearbyEnemyCity)
				{
					// Check if enemy is near any enemy city (within 3 tiles)
					for (int iRing = 1; iRing <= 3 && !pNearbyEnemyCity; iRing++)
					{
						for (int i = RING_PLOTS[iRing-1]; i < RING_PLOTS[min(iRing, 5)]; i++)
						{
							CvPlot* pLoopPlot = iterateRingPlots(pEnemyPlot, i);
							if (pLoopPlot && pLoopPlot->isCity())
							{
								CvCity* pCity = pLoopPlot->getPlotCity();
								if (pCity && GET_PLAYER(assumedPosition.getPlayer()).IsAtWarWith(pCity->getOwner()))
								{
									pNearbyEnemyCity = pCity;
									break;
								}
							}
						}
					}
				}
				
				if (pNearbyEnemyCity)
				{
					// Enemy is defending near a city - breakthrough value
					result.iBonusScore += 20;
					
					// Extra bonus for killing defenders blocking city assault
					if (bIsKill)
						result.iBonusScore += 15;
					
					// Adjacent to city = highest priority breach point
					if (pNearbyEnemyCity->plot()->isAdjacent(pEnemyPlot))
						result.iBonusScore += 10;
				}
				
				// 2. COMBINED ARMS: Tanks benefit from infantry support
				// Check for friendly infantry nearby (protects from AT threats)
				int iFriendlyInfantryNearby = 0;
				CvTacticalPlot::eTactPlotDomain eUnitDomain = CvTacticalPlot::TD_LAND;
				int iAdjacentFriendlies = enemyPlot.getNumAdjacentFriendlies(eUnitDomain, unit.iPlotIndex);
				
				// Estimate infantry support (non-fast-attack melee friendlies)
				for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
				{
					CvPlot* pLoopPlot = iterateRingPlots(pEnemyPlot, i);
					if (pLoopPlot)
					{
						CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
						if (pFriendly && !pFriendly->IsCanAttackRanged() && 
							pFriendly->AI_getUnitAIType() != UNITAI_FAST_ATTACK &&
							pFriendly->baseMoves(false) <= 3)
						{
							iFriendlyInfantryNearby++;
						}
					}
				}
				
				// Bonus for attacking with infantry support (combined arms)
				if (iFriendlyInfantryNearby >= 2)
				{
					result.iBonusScore += 15; // Well-supported tank assault
				}
				else if (iFriendlyInfantryNearby == 1)
				{
					result.iBonusScore += 8;
				}
				else if (iAdjacentFriendlies == 0)
				{
					// Unsupported tank - vulnerable to AT ambush
					// Mild penalty unless it's a sure kill
					if (!bIsKill)
						result.iBonusScore -= 5;
				}
				
				// 3. ARMOR ADVANTAGE: Tanks can engage other heavy units
				// Unlike cavalry, tanks don't need to avoid armored enemies
				if (!pEnemyUnit->IsCanAttackRanged() && !pEnemyUnit->IsCivilianUnit())
				{
					// Can we overpower this enemy with our armor advantage?
					if (iOurStrength > iEnemyStrength * 1.2)
					{
						// We outclass them - press the attack
						result.iBonusScore += 10;
					}
					else if (iEnemyStrength > iOurStrength * 1.2)
					{
						// They're stronger - might be super-heavy or have AT bonus
						// Caution unless we have support
						if (iFriendlyInfantryNearby == 0 && !bIsKill)
							result.iBonusScore -= 8;
					}
				}
				
				// 4. PRIORITY TARGETS: Modern context priorities
				// Anti-tank units (AT guns, tank destroyers) are dangerous
				// Siege is still valuable but tanks are better at direct assault
				if (eEnemyAI == UNITAI_CITY_BOMBARD)
				{
					// Siege units - less critical for tanks (they assault cities directly)
					// but still valuable to eliminate
					result.iBonusScore += 12;
					if (bIsKill)
						result.iBonusScore += 8;
				}
				else if (pEnemyUnit->IsCanAttackRanged())
				{
					// Ranged units - tanks can absorb their fire, but still good to silence
					result.iBonusScore += 8;
					if (bIsKill)
						result.iBonusScore += 10;
					
					// Artillery is especially valuable to destroy
					if (pEnemyUnit->GetRange() >= 3)
						result.iBonusScore += 5;
				}
				
				// 5. WOUNDED TARGETS: Exploitation
				int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
				int iEnemyMaxHP = pEnemyUnit->GetMaxHitPoints();
				int iEnemyHPPercent = (iEnemyHP * 100) / iEnemyMaxHP;
				
				if (iEnemyHPPercent <= 50)
				{
					// Exploit weakness - tanks are good at finishing
					result.iBonusScore += (100 - iEnemyHPPercent) / 6;
					if (bIsKill)
						result.iBonusScore += 10;
				}
				
				// 6. SPEARHEAD ROLE: Tanks lead the assault
				// Bonus for being first to engage (breaking the line)
				if (iAdjacentFriendlies >= 1 && enemyPlot.getNumAdjacentEnemies(eUnitDomain) >= 2)
				{
					// Multiple enemies - tank is breaking through
					result.iBonusScore += 8;
				}
				
				// Civilians - tanks can capture but it's not their specialty
				if (pEnemyUnit->IsCivilianUnit())
				{
					result.iBonusScore += 10; // Lower than cavalry - not their focus
				}
			}
			// === TRADITIONAL CAVALRY TACTICS (non-tank fast melee) ===
			// Original cavalry logic for knights, lancers, hussars, etc.
			else if (bIsFastMelee || bIsCavalry)
			{
				// PRIORITY 1: Siege units - cavalry's historical counter
				// Siege units are slow, fragile, and devastating - perfect cavalry targets
				if (eEnemyAI == UNITAI_CITY_BOMBARD)
				{
					result.iBonusScore += 25; // High priority - cripples enemy sieges
					
					// Extra bonus for killing siege - removes threat permanently
					if (bIsKill)
						result.iBonusScore += 20;
					
					// Cavalry with hit-and-run can safely raid siege lines
					if (pUnit->canMoveAfterAttacking() && result.iRemainingMoves > 0)
						result.iBonusScore += 10; // Can hit and escape
				}
				// PRIORITY 2: Ranged units - can't fight back effectively in melee
				else if (pEnemyUnit->IsCanAttackRanged())
				{
					// Ranged units are vulnerable to cavalry charges
					result.iBonusScore += 15;
					
					// Extra bonus for killing - silences their firepower
					if (bIsKill)
						result.iBonusScore += 15;
					
					// Archers and crossbowmen are especially vulnerable
					if (eEnemyAI == UNITAI_RANGED)
						result.iBonusScore += 5;
				}
				// PRIORITY 3: Isolated enemies - no supporting fire
				// Cavalry excels at picking off lone units
				int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
				if (iEnemySupport == 0)
				{
					result.iBonusScore += 12; // Isolated target - safe approach
					
					// Hit-and-run units can safely engage isolated targets
					if (pUnit->canMoveAfterAttacking())
						result.iBonusScore += 5;
				}
				else if (iEnemySupport >= 3)
				{
					// Well-supported enemy - cavalry should be cautious
					// Unless it's a high-value target worth the risk
					if (eEnemyAI != UNITAI_CITY_BOMBARD && !bIsKill)
						result.iBonusScore -= 8;
				}
				
				// PRIORITY 4: Low-defense targets (wounded or weak)
				// Cavalry can rapidly exploit weaknesses in enemy lines
				int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
				int iEnemyMaxHP = pEnemyUnit->GetMaxHitPoints();
				int iEnemyHPPercent = (iEnemyHP * 100) / iEnemyMaxHP;
				
				if (iEnemyHPPercent <= 50)
				{
					// Wounded enemy - easy kill potential
					result.iBonusScore += (100 - iEnemyHPPercent) / 5; // Up to +10 for nearly dead
					
					// Cavalry should finish off wounded targets
					if (bIsKill)
						result.iBonusScore += 8;
				}
				
				// PRIORITY 5: Workers and settlers - raiders' delight
				if (pEnemyUnit->IsCivilianUnit())
				{
					// Cavalry raids on workers/settlers are devastating economically
					result.iBonusScore += 20;
					
					// Settlers are extremely valuable targets
					if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SETTLE)
						result.iBonusScore += 30;
				}
				
				// Penalty for attacking heavily armored units without advantage
				// Cavalry historically struggled against prepared heavy infantry
				if (!pEnemyUnit->IsCanAttackRanged() && !pEnemyUnit->IsCivilianUnit())
				{
					int iEnemyDefense = pEnemyUnit->getDefenseModifier();
					if (iEnemyDefense >= 25 && !bIsKill && iEnemySupport > 0)
					{
						// Heavily armored, supported melee unit - not ideal cavalry target
						result.iBonusScore -= 10;
					}
					
					// Fortified infantry is tough for cavalry
					if (pEnemyUnit->IsFortified() && !bIsKill)
					{
						result.iBonusScore -= pEnemyUnit->fortifyModifier() / 5; // -2 to -5
					}
				}
				
				// === ENEMY COMBAT BONUS IMPROVEMENT AWARENESS (Shoshone Encampment, etc.) ===
				// Check if the enemy gets a combat bonus from nearby improvements (like Shoshone encampments)
				// This makes the enemy significantly tougher to attack - prefer other targets
				int iEnemyImprovementBonus = pEnemyUnit->GetNearbyImprovementModifier(pEnemyPlot);
				if (iEnemyImprovementBonus > 0 && !bIsKill)
				{
					// Enemy has defensive bonus from nearby improvement (+20% for Shoshone)
					// Penalize attacking them - prefer targets without this bonus
					result.iBonusScore -= iEnemyImprovementBonus / 2; // -10 for 20% bonus
					
					// Extra penalty if we're cavalry - we're fast, we can find better targets
					if (bIsCavalry)
						result.iBonusScore -= iEnemyImprovementBonus / 4; // additional -5
				}
			}
			
			// === RECON UNIT TACTICS (scouts, explorers) ===
			// Recon units are weaker than cavalry but still fast (3 moves).
			// They should:
			// 1. Prioritize survival over kills
			// 2. Finish off wounded enemies (safe kills)
			// 3. Target siege/ranged units (fragile, high value)
			// 4. Avoid strong/fortified enemies
			// 5. Capture undefended workers/settlers
			bool bIsRecon = (pUnit->AI_getUnitAIType() == UNITAI_EXPLORE || 
							 pUnit->getUnitInfo().GetDefaultUnitAIType() == UNITAI_EXPLORE);
			
			if (bIsRecon && !bIsCavalry) // Don't double-apply if already counted as cavalry
			{
				int iEnemyStrength = pEnemyUnit->GetBaseCombatStrength();
				int iOurStrength = pUnit->GetBaseCombatStrength();
				int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
				int iEnemyMaxHP = pEnemyUnit->GetMaxHitPoints();
				int iEnemyHPPercent = (iEnemyHP * 100) / max(iEnemyMaxHP, 1);
				
				// 1. SAFE KILLS: Recon excels at finishing wounded enemies
				// This is their primary combat role - cleanup duty
				if (bIsKill)
				{
					// Big bonus for kills - this is what recon should do
					result.iBonusScore += 15;
					
					// Extra bonus for killing badly wounded targets (almost guaranteed success)
					if (iEnemyHPPercent <= 25)
						result.iBonusScore += 10;
					else if (iEnemyHPPercent <= 50)
						result.iBonusScore += 5;
				}
				else
				{
					// Non-kill attacks are risky for weak recon units
					// Penalty scales with how much stronger the enemy is
					if (iEnemyStrength > iOurStrength * 1.2f)
						result.iBonusScore -= 10;
				}
				
				// 2. HIGH-VALUE SOFT TARGETS: Siege and ranged units
				// These are valuable and fragile - good recon targets
				if (eEnemyAI == UNITAI_CITY_BOMBARD)
				{
					result.iBonusScore += 15; // Siege is high priority but less than cavalry
					if (bIsKill)
						result.iBonusScore += 10; // Killing siege is great
				}
				else if (pEnemyUnit->IsCanAttackRanged())
				{
					result.iBonusScore += 10; // Ranged units can't fight back well in melee
					if (bIsKill)
						result.iBonusScore += 8;
				}
				
				// 3. CIVILIANS: Easy captures
				if (pEnemyUnit->IsCivilianUnit())
				{
					result.iBonusScore += 15; // Workers/settlers are easy pickings
					if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SETTLE)
						result.iBonusScore += 20; // Settlers are extremely valuable
				}
				
				// 4. SURVIVAL PRIORITY: Avoid dangerous situations
				// Recon units are too valuable for exploration to throw away
				int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
				
				// Isolated targets are safe
				if (iEnemySupport == 0)
				{
					result.iBonusScore += 8;
				}
				else if (iEnemySupport >= 2 && !bIsKill)
				{
					// Multiple enemies nearby and we can't get a kill - very risky
					result.iBonusScore -= 15;
				}
				
				// Avoid fortified strong enemies
				if (pEnemyUnit->IsFortified() && !bIsKill)
				{
					result.iBonusScore -= 12;
				}
				
				// Penalize attacking units much stronger than us
				if (!pEnemyUnit->IsCivilianUnit() && iEnemyStrength >= iOurStrength * 1.5f && !bIsKill)
				{
					result.iBonusScore -= 15; // Don't pick fights you can't win
				}
				
				// 5. SURVIVALISM BONUS: Recon with self-healing can be more aggressive
				// If unit can heal in enemy/neutral territory, it can sustain longer raids
				int iEnemyHeal = pUnit->getExtraEnemyHeal();
				int iNeutralHeal = pUnit->getExtraNeutralHeal();
				if (iEnemyHeal > 0 || iNeutralHeal > 0)
				{
					// Can heal outside friendly territory - more sustainable aggression
					int iHealBonus = max(iEnemyHeal, iNeutralHeal);
					
					// Bonus for attacking when we can self-heal
					if (bIsKill)
						result.iBonusScore += iHealBonus / 3; // +3-6 for typical survivalism
					
					// Reduce penalty for risky attacks if we can heal
					// Units with self-heal can afford to take some damage
					if (result.iSelfDamage > 0 && result.iSelfDamage < pUnit->GetCurrHitPoints() / 2)
					{
						// Self-healing mitigates damage taken
						result.iBonusScore += min(iHealBonus / 2, result.iSelfDamage / 5);
					}
				}
				
				// 6. WITHDRAWAL BONUS: Recon with withdrawal chance can take more risks
				// Units that can withdraw from melee effectively take less damage on average
				int iWithdrawalChance = pUnit->withdrawalProbability();
				if (iWithdrawalChance > 0)
				{
					// Withdrawal is powerful - it completely negates damage when it triggers
					// Scale bonus based on withdrawal chance (typically 50-75% for recon)
					int iWithdrawBonus = iWithdrawalChance / 10; // +5-7 for typical withdrawal
					
					// Withdrawal only helps against melee attackers, not ranged
					// Since recon is attacking, the bonus applies to counterattacks they might face
					result.iBonusScore += iWithdrawBonus;
					
					// Extra bonus when taking risks - withdrawal makes self-damage less certain
					if (result.iSelfDamage > 0 && !bIsKill)
					{
						// Effective damage is reduced by withdrawal chance
						// e.g., 50% withdrawal = expected 50% of normal damage
						int iEffectiveDamageReduction = (result.iSelfDamage * iWithdrawalChance) / 200;
						result.iBonusScore += iEffectiveDamageReduction;
					}
					
					// Withdrawal makes isolated attacks safer since you can escape
					if (iEnemySupport == 0)
						result.iBonusScore += 3; // Extra safe when can withdraw from lone enemy
				}
			}
			
			// === SLOW INFANTRY MELEE TACTICS (Spearmen, Swordsmen, Pikemen, etc.) ===
			// Slow melee units (2 moves) use fundamentally different melee tactics than cavalry:
			// - Anti-cavalry units (spearmen/pikemen) should prioritize mounted targets
			// - Mainline infantry (swordsmen) should target weakened enemies for kills
			// - All slow infantry prefers supported attacks over isolated charges
			bool bIsSlowInfantry = (!pUnit->IsCanAttackRanged() && pUnit->baseMoves(false) <= 2);
			
			if (bIsSlowInfantry && !bIsFastMelee && !bIsCavalry && !bIsRecon)
			{
				int iEnemySupport = enemyPlot.getNumAdjacentEnemies(CvTacticalPlot::TD_LAND);
				int iOurSupport = assumedUnitPlot.getNumAdjacentFriendlies(CvTacticalPlot::TD_LAND, unit.iPlotIndex);
				int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
				int iEnemyMaxHP = pEnemyUnit->GetMaxHitPoints();
				int iEnemyHPPercent = (iEnemyHP * 100) / max(iEnemyMaxHP, 1);
				
				// Check for anti-cavalry bonus (spearmen/pikemen)
				static UnitCombatTypes eMountedCombat = (UnitCombatTypes)GC.getInfoTypeForString("UNITCOMBAT_MOUNTED");
				int iAntiCavalryBonus = 0;
				bool bEnemyIsCavalry = false;
				
				if (eMountedCombat != NO_UNITCOMBAT)
				{
					iAntiCavalryBonus = pUnit->unitCombatModifier(eMountedCombat);
					bEnemyIsCavalry = (pEnemyUnit->getUnitCombatType() == eMountedCombat);
				}
				
				bool bIsAntiCavalry = (iAntiCavalryBonus >= 25);
				
				// 1. ANTI-CAVALRY MELEE: Spearmen should prioritize mounted targets
				if (bIsAntiCavalry && bEnemyIsCavalry)
				{
					// This is our specialty - give big bonus for engaging cavalry
					result.iBonusScore += 35;
					
					// Extra bonus based on our anti-cavalry strength
					result.iBonusScore += iAntiCavalryBonus / 5; // +10 for +50% bonus
					
					// Killing cavalry is excellent
					if (bIsKill)
					{
						result.iBonusScore += 25;
					}
					
					// Bonus for protecting ranged units from cavalry
					// Check if there are friendly ranged units nearby that cavalry could threaten
					int iNearbyFriendlyRanged = 0;
					const CvPlot* pAttackerPlot = assumedUnitPlot.getPlot();
					if (pAttackerPlot)
					{
						for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
						{
							CvPlot* pLoopPlot = iterateRingPlots(pAttackerPlot, i);
							if (pLoopPlot)
							{
								CvUnit* pFriendly = pLoopPlot->getBestDefender(pUnit->getOwner());
								if (pFriendly && pFriendly->IsCanAttackRanged())
								{
									iNearbyFriendlyRanged++;
								}
							}
						}
					}
					
					// Big bonus for protecting ranged from cavalry
					if (iNearbyFriendlyRanged > 0)
					{
						result.iBonusScore += iNearbyFriendlyRanged * 8;
					}
				}
				// Anti-cavalry units attacking non-cavalry targets
				else if (bIsAntiCavalry && !bEnemyIsCavalry)
				{
					// Mild penalty - save spearmen for cavalry interception
					// Unless it's a kill opportunity
					if (!bIsKill && iEnemySupport > 0)
					{
						result.iBonusScore -= 8;
					}
				}
				
				// 2. MAINLINE INFANTRY (non-anti-cavalry): Target priority
				// Swordsmen and similar units are versatile front-line fighters
				if (!bIsAntiCavalry)
				{
					// Priority: Siege units - slow infantry can catch them
					if (eEnemyAI == UNITAI_CITY_BOMBARD)
					{
						result.iBonusScore += 20;
						if (bIsKill)
							result.iBonusScore += 15;
					}
					// Priority: Weakened enemies - infantry excels at finishing
					else if (iEnemyHPPercent <= 50)
					{
						result.iBonusScore += (100 - iEnemyHPPercent) / 4; // Up to +12
						if (bIsKill)
							result.iBonusScore += 10;
					}
					// Priority: Ranged units - can't fight back in melee
					else if (pEnemyUnit->IsCanAttackRanged())
					{
						result.iBonusScore += 10;
						if (bIsKill)
							result.iBonusScore += 8;
					}
				}
				
				// 3. FORMATION FIGHTING: Infantry needs mutual support
				// Unlike cavalry, infantry charges without support are dangerous
				if (iOurSupport >= 2)
				{
					// Well-supported attack - this is how infantry fights best
					result.iBonusScore += 10;
				}
				else if (iOurSupport == 1)
				{
					result.iBonusScore += 4;
				}
				else if (iOurSupport == 0 && iEnemySupport > 0)
				{
					// Unsupported attack into supported enemy - very risky for infantry
					if (!bIsKill)
						result.iBonusScore -= 15;
					else
						result.iBonusScore -= 5; // Still risky even for kill
				}
				
				// 4. DEFENSIVE COMBAT BONUS: Infantry uses terrain
				// If we're on good defensive terrain, we can afford more aggressive attacks
				const CvPlot* pOurPlot = assumedUnitPlot.getPlot();
				if (pOurPlot)
				{
					int iOurTerrainDef = pOurPlot->defenseModifier(pUnit->getTeam(), false, false);
					if (iOurTerrainDef > 0)
					{
						// Attacking from good terrain is safer
						result.iBonusScore += iOurTerrainDef / 8;
					}
				}
				
				// Penalty for attacking fortified enemies (infantry struggles)
				if (pEnemyUnit->IsFortified() && !bIsKill)
				{
					int iFortifyMod = pEnemyUnit->fortifyModifier();
					result.iBonusScore -= iFortifyMod / 4; // -5 to -12
				}
				
				// 5. CIVILIANS: Infantry can capture them, but not a specialty
				if (pEnemyUnit->IsCivilianUnit())
				{
					result.iBonusScore += 10;
					if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SETTLE)
						result.iBonusScore += 15;
				}
			}
		}
	}

	// Defensive unit clearing priority for melee attacks
	// If killing this unit would open a path to an enemy city, give bonus
	if (bIsKill && !enemyPlot.isEnemyCity())
	{
		CvCity* pAdjacentEnemyCity = pEnemyPlot->GetAdjacentCity();
		if (pAdjacentEnemyCity && GET_PLAYER(assumedPosition.getPlayer()).IsAtWarWith(pAdjacentEnemyCity->getOwner()))
		{
			// Check domain relevance - naval melee doesn't block land assault
			CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
			bool bRelevantDefender = true;
			if (pEnemyUnit && pEnemyUnit->getDomainType() == DOMAIN_SEA && !pEnemyUnit->IsCanAttackRanged())
			{
				// Naval melee unit - only relevant bonus if our attacker is also naval
				if (pUnit->getDomainType() != DOMAIN_SEA)
					bRelevantDefender = false;
			}
			
			if (bRelevantDefender)
			{
				// Killing a defender near a city is valuable - opens assault paths
				result.iBonusScore += 50;
				
				// Extra bonus if the city is the primary target
				if (assumedPosition.getTarget() == pAdjacentEnemyCity->plot())
					result.iBonusScore += 30;
			}
		}
	}

	// NAVAL MELEE COORDINATION: Naval melee should prefer targets softened by naval ranged
	// This complements the ranged softening logic for proper combined arms
	if (pUnit->getDomainType() == DOMAIN_SEA && !pUnit->IsCanAttackRanged())
	{
		// Check if we have naval ranged units that could have softened this target
		bool bHaveNavalRanged = false;
		int iNavalRangedDamage = 0;
		
		vector<SUnitStats> allUnits = assumedPosition.getAvailableUnits();
		allUnits.insert(allUnits.end(), assumedPosition.getFinishedUnits().begin(), assumedPosition.getFinishedUnits().end());
		
		for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
		{
			const CvUnit* pLoopUnit = it->pUnit;
			if (!pLoopUnit || !pLoopUnit->IsCanAttackRanged())
				continue;
			
			// Found a naval ranged unit (or submarine)
			if (pLoopUnit->getDomainType() == DOMAIN_SEA)
			{
				bHaveNavalRanged = true;
				
				// Check if ranged unit can hit this target
				int iRangedDistance = plotDistance(it->iPlotIndex, enemyPlot.getPlotIndex());
				if (iRangedDistance <= pLoopUnit->GetRange())
				{
					// Estimate damage the ranged could do
					iNavalRangedDamage += pLoopUnit->GetMaxRangedCombatStrength(NULL, NULL, true, NULL) / 10;
				}
			}
		}
		
		if (bHaveNavalRanged)
		{
			// Attacking a coastal city - check if ranged can help soften
			if (enemyPlot.isEnemyCity())
			{
				CvCity* pTargetCity = enemyPlot.getPlot()->getPlotCity();
				if (pTargetCity && pTargetCity->isCoastal())
				{
					// Big bonus if city is already softened and we can capture
					if (result.eAssignmentType == A_MELEEKILL)
					{
						result.iBonusScore += 75; // Capture the softened city!
						
						// Extra bonus for island cities (naval-only capture)
						int iLandApproaches = 0;
						for (int iDir = 0; iDir < NUM_DIRECTION_TYPES; iDir++)
						{
							CvPlot* pAdj = plotDirection(pEnemyPlot->getX(), pEnemyPlot->getY(), (DirectionTypes)iDir);
							if (pAdj && !pAdj->isWater() && !pAdj->isImpassable(pUnit->getTeam()))
								iLandApproaches++;
						}
						if (iLandApproaches == 0)
							result.iBonusScore += 50; // Only we can capture this!
					}
					// Moderate bonus if city is damaged (being softened)
					else if (pTargetCity->getDamage() > 0)
					{
						// More bonus the more damaged the city is
						int iDamagePercent = (100 * pTargetCity->getDamage()) / pTargetCity->GetMaxHitPoints();
						result.iBonusScore += iDamagePercent / 4; // Up to +25 at 100% damage
					}
				}
			}
			// Attacking an enemy naval unit
			else if (enemyPlot.getEnemyUnit() && enemyPlot.getEnemyUnit()->getDomainType() == DOMAIN_SEA)
			{
				CvUnit* pEnemy = enemyPlot.getEnemyUnit();
				int iEnemyMaxHP = pEnemy->GetMaxHitPoints();
				int iEnemyHP = pEnemy->GetCurrHitPoints();
				
				// Bonus if enemy is already damaged (ranged softened it)
				if (iEnemyHP < iEnemyMaxHP)
				{
					int iDamagePercent = (100 * (iEnemyMaxHP - iEnemyHP)) / iEnemyMaxHP;
					
					// Prefer finishing off damaged enemies
					if (result.eAssignmentType == A_MELEEKILL || result.eAssignmentType == A_MELEEKILL_NO_ADVANCE)
					{
						result.iBonusScore += 25 + iDamagePercent / 2; // Up to +75 for killing heavily damaged
					}
					else
					{
						result.iBonusScore += iDamagePercent / 5; // Up to +20 for attacking damaged
					}
				}
				
				// Extra bonus for high-value targets (carriers, capital ships)
				if (bIsKill && (pEnemy->AI_getUnitAIType() == UNITAI_CARRIER_SEA || 
					pEnemy->GetBaseCombatStrength() > pUnit->GetBaseCombatStrength()))
				{
					result.iBonusScore += 30;
				}
			}
		}
		
		// DESTROYER SUB-HUNTING COORDINATION
		// Anti-submarine warfare units (destroyers) should prioritize hunting submarines
		// This creates proper hunter-killer tactics where ASW units seek out and destroy subs
		CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
		if (pEnemyUnit && IsAntiSubmarineUnit(pUnit) && IsSubmarineUnit(pEnemyUnit))
		{
			// Major bonus for ASW units attacking submarines - this is their primary role
			result.iBonusScore += 60;
			
			// Extra bonus if we can kill the sub
			if (bIsKill)
			{
				result.iBonusScore += 50; // Eliminating subs is critical for fleet protection
			}
			
			// Bonus based on sub threat level
			// High-value targets: nuclear subs, attack subs with missiles
			if (pEnemyUnit->AI_getUnitAIType() == UNITAI_SUBMARINE)
			{
				// Check if sub can carry missiles/nukes (very dangerous)
				if (pEnemyUnit->cargoSpace() > 0)
				{
					result.iBonusScore += 40; // Nuclear/missile sub - highest priority
				}
				else
				{
					result.iBonusScore += 20; // Regular attack sub
				}
			}
			
			// Check if we have friendly units that need protection from this sub
			bool bSubThreatensFriendlies = false;
			int iThreatenedValue = 0;
			
			for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
			{
				const CvUnit* pLoopUnit = it->pUnit;
				if (!pLoopUnit || pLoopUnit->getDomainType() != DOMAIN_SEA)
					continue;
				
				// Check if this friendly unit is in danger from the submarine
				int iDistToSub = plotDistance(it->iPlotIndex, enemyPlot.getPlotIndex());
				if (iDistToSub <= pEnemyUnit->GetRange() + 1) // Sub could attack next turn
				{
					bSubThreatensFriendlies = true;
					
					// Carriers are particularly vulnerable and valuable
					if (pLoopUnit->AI_getUnitAIType() == UNITAI_CARRIER_SEA)
					{
						iThreatenedValue += 100;
					}
					// Other capital ships
					else if (pLoopUnit->GetBaseCombatStrength() > pUnit->GetBaseCombatStrength())
					{
						iThreatenedValue += 40;
					}
					// Other naval units
					else
					{
						iThreatenedValue += 15;
					}
				}
			}
			
			// Big bonus for protecting threatened friendlies
			if (bSubThreatensFriendlies)
			{
				result.iBonusScore += min(iThreatenedValue, 150); // Cap at +150
			}
			
			// Coordination bonus: if we have multiple ASW units, coordinate attack
			int iASWCount = 0;
			for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
			{
				const CvUnit* pLoopUnit = it->pUnit;
				if (pLoopUnit && IsAntiSubmarineUnit(pLoopUnit))
				{
					int iDistToTarget = plotDistance(it->iPlotIndex, enemyPlot.getPlotIndex());
					if (iDistToTarget <= 3) // Can engage or support
						iASWCount++;
				}
			}
			
			// More ASW units nearby = coordinated hunt bonus
			if (iASWCount >= 2)
			{
				result.iBonusScore += 20; // Pack hunting bonus
			}
		}
		
		// NAVAL FLEET CONCENTRATION: Naval melee should prioritize targets being attacked by fleet
		// This creates focus fire coordination where ranged softens and melee finishes
		if (!IsSubmarineUnit(pUnit)) // Subs have their own coordination logic
		{
			int iFriendlyRangedEngaging = 0;
			int iTotalRangedDamage = 0;
			int iOtherMeleeEngaging = 0;
			
			for (vector<SUnitStats>::const_iterator it = allUnits.begin(); it != allUnits.end(); ++it)
			{
				const CvUnit* pLoopUnit = it->pUnit;
				if (!pLoopUnit || pLoopUnit == pUnit)
					continue;
				if (pLoopUnit->getDomainType() != DOMAIN_SEA || !pLoopUnit->IsCombatUnit())
					continue;
				
				int iDistToTarget = plotDistance(it->iPlotIndex, enemyPlot.getPlotIndex());
				
				// Count ranged ships that can engage
				if (pLoopUnit->IsCanAttackRanged())
				{
					if (iDistToTarget <= pLoopUnit->GetRange())
					{
						iFriendlyRangedEngaging++;
						iTotalRangedDamage += pLoopUnit->GetMaxRangedCombatStrength(NULL, NULL, true, NULL) / 10;
					}
				}
				// Count other melee that can engage
				else if (iDistToTarget <= 2)
				{
					iOtherMeleeEngaging++;
				}
			}
			
			// Bonus for attacking targets that ranged fleet is engaging
			if (iFriendlyRangedEngaging >= 1)
			{
				result.iBonusScore += 20; // Base coordination bonus
				
				// Extra bonus per ranged ship (up to +40 more)
				result.iBonusScore += min(iFriendlyRangedEngaging * 12, 40);
				
				// Check if ranged fire has already softened the target
				CvUnit* pEnemyUnit = enemyPlot.getEnemyUnit();
				if (pEnemyUnit)
				{
					int iEnemyHP = pEnemyUnit->GetCurrHitPoints();
					int iMaxHP = pEnemyUnit->GetMaxHitPoints();
					
					// Big bonus if target is already damaged (ranged fire worked)
					if (iEnemyHP < iMaxHP * 2 / 3)
					{
						result.iBonusScore += 35; // Target is softened, move in for kill
					}
					else if (iEnemyHP < iMaxHP)
					{
						result.iBonusScore += 15; // Target taking damage
					}
					
					// Bonus if our melee damage plus ranged can kill
					if (iTotalRangedDamage + result.iDamage >= iEnemyHP)
					{
						result.iBonusScore += 50; // Combined fleet can kill
					}
				}
			}
			
			// Bonus for attacking with multiple melee (coordinated assault)
			if (iOtherMeleeEngaging >= 1)
			{
				result.iBonusScore += 15; // Coordinated melee attack
				result.iBonusScore += min(iOtherMeleeEngaging * 8, 24); // Up to +24 more
			}
		}
	}

	return result;
}

static STacticalAssignment* ScorePlotForAdmiralHeal(const SUnitStats& unit, const CvTacticalPlot* assumedUnitPlot, int iAssumedMovesLeft, const CvTacticalPosition& assumedPosition)
{
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex, assumedUnitPlot->getPlotIndex(), unit.iUnitID, 0, unit.eMoveStrategy, A_USE_POWER, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	if (iAssumedMovesLeft == 0)
		return result;

	int iDamageDelta = 0;

	int iNearbyFriendly = assumedUnitPlot->getNumAdjacentFriendlies(CvTacticalPlot::TD_SEA, -1) + (assumedUnitPlot->hasFriendlyCombatUnit() ? 1 : 0);
	if (iNearbyFriendly >= 3)
	{
		vector<SUnitStats> units = assumedPosition.getAvailableUnits();
		units.insert(units.end(), assumedPosition.getFinishedUnits().begin(), assumedPosition.getFinishedUnits().end());
		for (vector<SUnitStats>::const_iterator it = units.begin(); it != units.end(); ++it)
		{
			const CvUnit* pLoopUnit = it->pUnit;
			if (!pLoopUnit->IsCombatUnit() || (pLoopUnit->getDomainType() != DOMAIN_SEA && it->eMoveStrategy != MS_EMBARKED) || pLoopUnit->IsCannotHeal(true))
				continue;

			if (plotDistance(it->iPlotIndex, assumedUnitPlot->getPlotIndex()) > 1)
				continue;

			const CvTacticalPlot* adjacentTactPlot = assumedPosition.getTactPlot(pLoopUnit->plot()->GetPlotIndex());
			if (!adjacentTactPlot)
				continue;

			HealUnitsInPlot(result->unitHealing, iDamageDelta, adjacentTactPlot, 1000, assumedPosition);
		}
	}

	result->SetScore(0, 0, iDamageDelta);

	return result;
}

CvTacticalPlot::CvTacticalPlot(const CvPlot* plot, PlayerTypes ePlayer, const vector<const CvUnit*>& allOurUnits) :
	pPlot(NULL) //important, invalid by default
{
	if (!plot || ePlayer == NO_PLAYER)
		return;

	//minor players ignore barb camps and the units inside
	CvPlayerAI& kPlayer = GET_PLAYER(ePlayer);
	if (plot->getRevealedImprovementType(kPlayer.getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT) && kPlayer.isMinorCiv() && !kPlayer.isBarbarian())
		return;

	//the most important thing
	pPlot = plot;

	//constant
	bfBlockedByNonSimCombatUnit = 0;
	bHasAirCover = pPlot->HasAirCover(ePlayer);
	bIsVisibleToEnemy = pPlot->IsKnownVisibleToEnemy(kPlayer.GetID());

	//updated once at the beginning
	nVisiblePlotsNearEnemyRange2 = 0;
	nVisiblePlotsNearEnemyRange3 = 0;
	//maybe only plant citadels if we're not winning anyhow or if we have many generals? kPlayer.GetDiplomacyAI()->GetStateAllWars()
	bMightWantCitadel = kPlayer.IsNicePlotForCitadel(pPlot);

	//updated if necessary
	bEdgeOfTheKnownWorldUnknown = true;
	bEnemyCivilianPresent = false;
	bEdgeOfTheKnownWorld = false;
	nAdjacentEnemyImprovementDamage = 0;
	pFirstEnemyCombatUnit = NULL;
	pSecondEnemyCombatUnit = NULL;

	//set only once
	bFriendlyDefenderEndTurn = false;
	bSimUnitBlocking[TD_BOTH] = false;
	bSimUnitBlocking[TD_LAND] = false;
	bSimUnitBlocking[TD_SEA] = false;

	//set initial state, update after every move
	aiFriendlyCombatUnitsAdjacent[TD_BOTH] = 0;
	aiFriendlyCombatUnitsAdjacent[TD_LAND] = 0;
	aiFriendlyCombatUnitsAdjacent[TD_SEA] = 0;
	aiFriendlyCombatUnitsAdjacentEndTurn[TD_BOTH] = 0;
	aiFriendlyCombatUnitsAdjacentEndTurn[TD_LAND] = 0;
	aiFriendlyCombatUnitsAdjacentEndTurn[TD_SEA] = 0;

	aiEnemyDistance[TD_BOTH] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiEnemyDistance[TD_LAND] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiEnemyDistance[TD_SEA] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiEnemyCombatUnitsAdjacent[TD_BOTH] = 0;
	aiEnemyCombatUnitsAdjacent[TD_LAND] = 0;
	aiEnemyCombatUnitsAdjacent[TD_SEA] = 0;
	aiRangedAttackEnemyDistance[TD_BOTH] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiRangedAttackEnemyDistance[TD_LAND] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiRangedAttackEnemyDistance[TD_SEA] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;

	//enemy distance alone is not enough
	//there may be multiple enemy combat units but we only care about the best defender
	//also ignore the official visibility, we only add tactical plots if we can see them in the sim
	//note that AI can see all submarines at this stage (might be ignored as tactical target though)
	pFirstEnemyCombatUnit = pPlot->getBestDefender(NO_PLAYER,ePlayer,NULL,true,true);
	pSecondEnemyCombatUnit = pPlot->getBestDefender(NO_PLAYER, ePlayer, NULL, true, true, false, false, pFirstEnemyCombatUnit);

	//concerning embarkation. this is complex because it allows combat units to stack, violating the 1UPT rule.
	//note that there are other exceptions as well, eg a fort can hold a naval unit and a land unit.
	//therefore we check for "native domain". we consider non-native domain units in the simulation but only allow moves into the native domain.
	//this means that 1UPT is still valid for all our simulated moves and we can ignore embarked defenders etc.

	//so here comes tricky logic to figure out whether we can use this plot
	for (int i = 0; i < pPlot->getNumUnits(); i++)
	{
		CvUnit* pPlotUnit = pPlot->getUnitByIndex(i);

		//ignore zombies
		if (pPlotUnit->isDelayedDeath())
			continue;

		//enemies
		if (GET_PLAYER(ePlayer).IsAtWarWith(pPlotUnit->getOwner()))
		{
			//combat units (embarked or not)
			if (pPlotUnit->IsCanDefend())
			{
				//enemy distance for other plots will be set afterwards in refreshVolatilePlotProperties
				//but we need to set the zeros here!
				aiEnemyDistance[TD_BOTH] = 0;
				if (pPlotUnit->getDomainType() == DOMAIN_LAND)
					aiEnemyDistance[TD_LAND] = 0;
				else if (pPlotUnit->getDomainType() == DOMAIN_SEA)
					aiEnemyDistance[TD_SEA] = 0;
			}
			else
				//civilian
				bEnemyCivilianPresent = true;
		}
		//neutral units
		else if (ePlayer != pPlotUnit->getOwner())
		{
			//check if we can use the plot for combat units
			if (pPlotUnit->IsCanDefend())
			{
				if (pPlotUnit->getDomainType() == DOMAIN_LAND)
					bfBlockedByNonSimCombatUnit |= 1;
				else if (pPlotUnit->getDomainType() == DOMAIN_SEA)
					bfBlockedByNonSimCombatUnit |= 2;
			}

			//rules for cities are complex so just don't try it
			if (pPlot->isCity())
				bfBlockedByNonSimCombatUnit |= 3;
		}
		//owned units not included in sim
		else if (std::find(allOurUnits.begin(), allOurUnits.end(), pPlotUnit) == allOurUnits.end())
		{
			if (pPlotUnit->IsCanDefend())
			{
				//mark as friendly
				bfBlockedByNonSimCombatUnit |= 4;

				if (pPlotUnit->getDomainType() == DOMAIN_LAND)
					bfBlockedByNonSimCombatUnit |= 1;
				else if (pPlotUnit->getDomainType() == DOMAIN_SEA)
					bfBlockedByNonSimCombatUnit |= 2;

				if (pPlotUnit->TurnProcessed())
				{
					//we need to update the adjacent friendly unit count for adjacent plots
					//but they are not guaranteed to exist at this point so we have to defer this
					bFriendlyDefenderEndTurn = true;
				}

				//rules for cities are complex so just don't try it
				//note that owned cities without a garrison are fine to use for tactsim
				if (pPlot->isCity())
					bfBlockedByNonSimCombatUnit |= 3;
			}
		}
	}

	//important, not every enemy city has a garrison!
	if (pPlot->isCity() && GET_PLAYER(ePlayer).IsAtWarWith(pPlot->getOwner()))
	{
		aiEnemyDistance[TD_BOTH] = 0;
		aiEnemyDistance[TD_LAND] = 0;
		aiEnemyDistance[TD_SEA] = 0;
	}
}

void CvTacticalPlot::resetVolatileProperties()
{
	bEdgeOfTheKnownWorld = false;
	nAdjacentEnemyImprovementDamage = 0;
	//set the distance to maximum but only if it's not a plot with an enemy
	if (aiEnemyDistance[TD_BOTH] != 0)
		aiEnemyDistance[TD_BOTH] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	if (aiEnemyDistance[TD_LAND] != 0)
		aiEnemyDistance[TD_LAND] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	if (aiEnemyDistance[TD_SEA] != 0)
		aiEnemyDistance[TD_SEA] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiEnemyCombatUnitsAdjacent[TD_BOTH] = 0;
	aiEnemyCombatUnitsAdjacent[TD_LAND] = 0;
	aiEnemyCombatUnitsAdjacent[TD_SEA] = 0;
	aiRangedAttackEnemyDistance[TD_BOTH] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiRangedAttackEnemyDistance[TD_LAND] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
	aiRangedAttackEnemyDistance[TD_SEA] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
}

bool CvTacticalPlot::isBlockedByNonSimUnit(eTactPlotDomain eDomain, bool bMustBeFriendly) const
{
	if (bMustBeFriendly && (bfBlockedByNonSimCombatUnit & 4) == 0)
		return false;

	if (eDomain == TD_BOTH)
		return (bfBlockedByNonSimCombatUnit & 3) != 0;
	else if (eDomain == TD_LAND)
		return (bfBlockedByNonSimCombatUnit & 1) != 0;
	else if (eDomain == TD_SEA)
		return (bfBlockedByNonSimCombatUnit & 2) != 0;

	return false;
}

bool CvTacticalPlot::hasFriendlyCombatUnit() const
{
	for (size_t i = 0; i < vUnitsHere.size(); i++)
		if (isCombatUnit(vUnitsHere[i].eMoveType))
			return true;
	
	return false;
}

bool CvTacticalPlot::hasFriendlyEmbarkedUnit() const
{
	for (size_t i = 0; i < vUnitsHere.size(); i++)
		if (isEmbarkedUnit(vUnitsHere[i].eMoveType))
			return true;

	return false;
}

void CvTacticalPlot::setCombatUnitEndTurn(CvTacticalPosition& currentPosition, eTactPlotDomain unitDomain, bool bOverride)
{
	//can't do anything before initialization is done
	if (!pPlot)
		return;
	
	//don't do this twice unless override
	if (bFriendlyDefenderEndTurn && !bOverride)
		return;

	bFriendlyDefenderEndTurn = true;

	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		CvPlot* pNeighbor = aNeighbors[i];
		if (pNeighbor)
		{
			CvTacticalPlot* tactPlot = currentPosition.getTactPlotMutable(pNeighbor->GetPlotIndex());
			if (tactPlot)
			{
				tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[unitDomain]++;
				if (unitDomain != TD_BOTH)
					tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[TD_BOTH]++;

				if (tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[TD_LAND] > 6 || tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[TD_SEA] > 6)
					OutputDebugString("implausible amount of neighbors");
			}
		}
	}
}

bool CvTacticalPlot::hasCoverFromOtherUnits(const CvTacticalPosition& currentPosition) const
{
	//performance optimization
	if (getEnemyDistance() < 2)
		return false;
	if (getEnemyDistance() > 3)
		return true;

	eTactPlotDomain eDomain = getPlot()->isWater() ? TD_SEA : TD_LAND;
	int iMyEnemyDist = getEnemyDistance(eDomain);

	const vector<CvTacticalPlot>& plots = currentPosition.getTactPlots();
	for (int iI = 0; iI < (int)plots.size(); iI++)
	{
		if (plots[iI].getEnemyDistance(eDomain) < iMyEnemyDist && !plots[iI].isCombatEndTurn())
			if (plots[iI].getPlot()->isAdjacent(getPlot()))
				return false;
	}

	return true;
}

void CvTacticalPlot::changeNeighboringUnitCount(CvTacticalPosition& currentPosition, eUnitMovementStrategy moveType, eTactPlotDomain unitDomain, int iChange) const
{
	//we don't count embarked units
	if (!pPlot || isEmbarkedUnit(moveType))
		return;

	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		CvPlot* pNeighbor = aNeighbors[i];
		if (pNeighbor)
		{
			CvTacticalPlot* tactPlot = currentPosition.getTactPlotMutable(pNeighbor->GetPlotIndex());
			if (tactPlot)
			{
				if (isCombatUnit(moveType)) //embarked is already handled
				{
					tactPlot->aiFriendlyCombatUnitsAdjacent[unitDomain] += iChange;
					if (unitDomain != TD_BOTH)
						tactPlot->aiFriendlyCombatUnitsAdjacent[TD_BOTH] += iChange;

					if (tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[TD_LAND] > 6 || tactPlot->aiFriendlyCombatUnitsAdjacentEndTurn[TD_SEA] > 6)
						OutputDebugString("implausible amount of neighbors");
				}
			}
		}
	}
}

void CvTacticalPlot::friendlyUnitMovingIn(CvTacticalPosition& currentPosition, const STacticalAssignment& assignment)
{
	//no more enemies here
	removeEnemyUnitIfPresent();
	removeEnemyUnitIfPresent(); // there may be several units in the plot
	bEnemyCivilianPresent = false;

	CvUnit* pUnit = GET_PLAYER(currentPosition.getPlayer()).getUnit(assignment.iUnitID);
	CvTacticalPlot::eTactPlotDomain unitDomain = DomainForUnit(pUnit);

	vUnitsHere.push_back( STacticalUnit(assignment.iUnitID,assignment.eMoveType));
	changeNeighboringUnitCount(currentPosition, assignment.eMoveType, unitDomain, +1);
}

void CvTacticalPlot::friendlyUnitMovingOut(CvTacticalPosition& currentPosition, const STacticalAssignment& assignment)
{
	for (vector<STacticalUnit>::iterator it = vUnitsHere.begin(); it != vUnitsHere.end(); it++)
	{
		if (assignment.iUnitID == it->iUnitID)
		{
			CvUnit* pUnit = GET_PLAYER(currentPosition.getPlayer()).getUnit(assignment.iUnitID);
			CvTacticalPlot::eTactPlotDomain unitDomain = DomainForUnit(pUnit);

			vUnitsHere.erase(it);
			changeNeighboringUnitCount(currentPosition, assignment.eMoveType, unitDomain, -1);
			return;
		}
	}

	OutputDebugString("invalid move\n");
}

int CvTacticalPlot::getNumAdjacentFriendlies(eTactPlotDomain eDomain, int iIgnoreUnitPlot) const 
{
	if (iIgnoreUnitPlot >= 0)
	{
		CvPlot* pIgnorePlot = GC.getMap().plotByIndexUnchecked(iIgnoreUnitPlot);
		if (pIgnorePlot->isAdjacent(pPlot))
			return aiFriendlyCombatUnitsAdjacent[eDomain] - 1;
	}

	return aiFriendlyCombatUnitsAdjacent[eDomain];
}

int CvTacticalPlot::getNumAdjacentFriendliesEndTurn(eTactPlotDomain eDomain) const 
{ 
	return aiFriendlyCombatUnitsAdjacentEndTurn[eDomain]; 
}

bool CvTacticalPlot::removeEnemyUnitIfPresent()
{
	bool bReturn = false;
	if (pFirstEnemyCombatUnit)
		pFirstEnemyCombatUnit = NULL;
	else
		pSecondEnemyCombatUnit = NULL;

	if (aiEnemyDistance[TD_BOTH] == 0)
	{
		if (!pSecondEnemyCombatUnit)
			aiEnemyDistance[TD_BOTH] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
		bReturn = true;
	}
	if (aiEnemyDistance[TD_LAND]==0)
	{
		if (!pSecondEnemyCombatUnit)
			aiEnemyDistance[TD_LAND] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
		bReturn = true;
	}
	if (aiEnemyDistance[TD_SEA]==0)
	{
		if (!pSecondEnemyCombatUnit)
			aiEnemyDistance[TD_SEA] = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
		bReturn = true;
	}

	//need to call refreshVolatilePlotProperties if return value is true
	return bReturn;
}

unsigned char CvTacticalPlot::getEnemyDistance(eTactPlotDomain eDomain) const
{
	return aiEnemyDistance[eDomain];
}

void CvTacticalPlot::setEnemyDistance(eTactPlotDomain eDomain, int iDistance)
{
	aiEnemyDistance[eDomain] = static_cast<unsigned char>(iDistance);
}

unsigned char CvTacticalPlot::getRangedAttackEnemyDistance(eTactPlotDomain eDomain) const
{
	return aiRangedAttackEnemyDistance[eDomain];
}

void CvTacticalPlot::setRangedAttackEnemyDistance(eTactPlotDomain eDomain, int iDistance)
{
	aiRangedAttackEnemyDistance[eDomain] = static_cast<unsigned char>(iDistance);
}

bool CvTacticalPlot::checkEdgePlotsForSurprises(const CvTacticalPosition& currentPosition, vector<int>& landEnemies, vector<int>& seaEnemies)
{
	//we only ever add plots, so if it's not a new plot and not an edge plot, nothing to do
	if (!bEdgeOfTheKnownWorld && !bEdgeOfTheKnownWorldUnknown)
		return false;

	//performance optimization ... only look at outward neighbors!
	CvPlot* pTarget = currentPosition.getTarget();
	int iRefDistance = plotDistance(pPlot->getX(), pPlot->getY(), pTarget->getX(), pTarget->getY());

	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
	{
		CvPlot* pNeighbor = aNeighbors[i];
		if (!pNeighbor)
			continue;

		int iNeighborDistance = plotDistance(pNeighbor->getX(), pNeighbor->getY(), pTarget->getX(), pTarget->getY());
		if (iNeighborDistance < iRefDistance)
			continue;

		//minor players ignore barb camps and the units inside
		CvPlayer& kPlayer = GET_PLAYER(currentPosition.getPlayer());
		if (pNeighbor->getRevealedImprovementType(kPlayer.getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT) && kPlayer.isMinorCiv() && !kPlayer.isBarbarian())
			continue;

		const CvTacticalPlot* tactPlot = currentPosition.getTactPlot(pNeighbor->GetPlotIndex());
		//if the tactical plot is invalid, it's out of range or invisible on the main map.
		//these are the ones we need to check here
		if (!tactPlot)
		{
			//don't ignore enemy cities, we know they exist even if invisible
			if (pNeighbor->isCity() && GET_PLAYER(currentPosition.getPlayer()).IsAtWarWith(pNeighbor->getOwner()))
			{
				//poor man's deduplication
				if (landEnemies.empty() || landEnemies.back()!=pNeighbor->GetPlotIndex())
					landEnemies.push_back(pNeighbor->GetPlotIndex());
			}
			else if (pNeighbor->isVisible(GET_PLAYER(currentPosition.getPlayer()).getTeam()))
			{
				CvUnit* pEnemy = pNeighbor->getBestDefender(NO_PLAYER, currentPosition.getPlayer(), NULL, true);
				if (pEnemy)
				{
					if (pEnemy->getDomainType() == DOMAIN_LAND)
					{
						//poor man's deduplication
						if (landEnemies.empty() || landEnemies.back()!=pNeighbor->GetPlotIndex())
							landEnemies.push_back(pNeighbor->GetPlotIndex());
					}
					else if (pEnemy->getDomainType() == DOMAIN_SEA)
					{
						//poor man's deduplication
						if (seaEnemies.empty() || seaEnemies.back()!=pNeighbor->GetPlotIndex())
							seaEnemies.push_back(pNeighbor->GetPlotIndex());
					}
				}
			}
			else
				//if the neighbor is invisible but passable be careful as well, enemies might be hiding there
				bEdgeOfTheKnownWorld |= !pNeighbor->isImpassable();

			int iImprovementDamage = TacticalAIHelpers::GetOtherPlayerImprovementDamage(pNeighbor, currentPosition.getPlayer(), true);
			if (iImprovementDamage > 0)
				nAdjacentEnemyImprovementDamage = max((int)nAdjacentEnemyImprovementDamage, iImprovementDamage);
		}

	}

	bEdgeOfTheKnownWorldUnknown = false;
	return bEdgeOfTheKnownWorld;
}

const vector<int>& CvTacticalPosition::getRangeAttackPlotsForUnit(const SUnitStats& unit) const
{
	static vector<int> emptyResult;

	TCachedRangeAttackPlots::const_iterator result = gRangeAttackPlotsLookup.find( make_pair(unit.iUnitID,unit.iPlotIndex) );
	if (result != gRangeAttackPlotsLookup.end())
		return result->second;

	return emptyResult;
}

bool IsCombatUnit(const SUnitStats& unit)
{
	switch (unit.eMoveStrategy)
	{
	case MS_FIRSTLINE:
	case MS_SECONDLINE:
	case MS_THIRDLINE:
		return true;
		break;
	default:
		return false;
	}
}

static STacticalAssignment* ScorePlotForMove(const SUnitStats& unit, const CvTacticalPlot* testPlot, const CvTacticalPosition& assumedPosition, eUnitMoveEvalMode evalMode)
{
	if (IsCombatUnit(unit))
		return ScorePlotForCombatUnitMove(unit, testPlot, assumedPosition, evalMode);
	else
		return ScorePlotForNonFightingUnitMove(unit, testPlot, assumedPosition, evalMode);
}

void CvTacticalPosition::getPreferredAssignmentsForUnit(const SUnitStats& unit, int nMaxCount) const
{
	//the challenge is that often a move can be good or bad depending on what our *other* units end up doing. so there are two strategies:
	//a) return as many moves as possible and check validity at the end.
	//b) return only "safe" moves in the sense that we dare to actually do them.
	//problem eg are generals, we want to move them close to the action but there might not be cover there or the cover might move away.
	//anyway experience shows if we choose an attractive but invalid move at the beginning of the sim this can poison everything
	//because so many positions can be generated from that we never get to examine at alternative beginnings in depth.
	//so we go with option B, even if it means we need to skip some daring moves.
	//on the other hand, the daring moves may still be possible later in the sim when they are not so daring anymore.
	//todo: try "fixup moves", meaning if a possible move is invalid, see if it can be made valid by moving another unit.
	gPossibleMoves.clear();
	gPossibleRangedAttacks.clear();

	const CvTacticalPlot* assumedUnitPlot = getTactPlot(unit.iPlotIndex);
	CvUnit* pUnit = GET_PLAYER(getPlayer()).getUnit(unit.iUnitID);
	if (!pUnit || !assumedUnitPlot)
		return;

	CvTacticalPosition tempPosition;
	int iOldPlotDistanceToTarget = bTargetDistanceRelevant ? TacticalAIHelpers::GetPlotDistanceToTarget(unit.iPlotIndex, pUnit->getDomainType()) : 0;
	if (iOldPlotDistanceToTarget < TACTICAL_COMBAT_MAX_TARGET_DISTANCE)
		iOldPlotDistanceToTarget = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;

	//check moves and melee attacks first
	const ReachablePlots& reachablePlots = getReachablePlotsForUnit(unit);
	for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
	{
		//the plot we're checking right now
		const CvTacticalPlot* testPlot = getTactPlot(it->iPlotIndex);
		if (!testPlot)
			continue;

		//if there is an enemy in the plot, we want to attack
		if (testPlot->isEnemy())
		{
			//ranged attacks are handled separately below
			if (pUnit->IsCanAttackRanged() || !IsCombatUnit(unit))
				continue;

			//for a melee attack we need to move to a defined adjacent plot first
			if (!assumedUnitPlot->getPlot()->isAdjacent(testPlot->getPlot()))
				continue;

			//does the attack make sense
			STacticalAssignment* attack = ScorePlotForMeleeAttack(unit,assumedUnitPlot,testPlot,it->iMovesLeft,*this);
			if (!attack->IsAcceptable())
				continue;

			GetNextPosition(*this, attack, tempPosition);
			SUnitStats tempUnit = GetNextUnit(unit, attack);

			//where would we end the turn? ie are we advancing or not
			const CvTacticalPlot* newPlot = (attack->eAssignmentType == A_MELEEKILL) ? testPlot : assumedUnitPlot;
			if (attack->iRemainingMoves == 0 && !tempPosition.canProbablyEndTurnAfterAssignment(tempUnit, newPlot, attack->eAssignmentType))
				continue;

			gAssignmentStorage.consumeOne();

			//also consider which plot we end up in
			STacticalAssignment* moveToPlot = ScorePlotForMove(tempUnit, newPlot, tempPosition, EM_INTERMEDIATE);
			attack->AddScore(moveToPlot);

			gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(attack, attack->Score()));
			//done with this plot
			continue;
		}

		//already there?
		if (unit.iPlotIndex == it->iPlotIndex)
		{
			//try pillaging as an intermediate step
			STacticalAssignment* pillaging = ScorePlotForPillageMove(unit, testPlot, it->iMovesLeft, *this);
			if (pillaging->Score() > 0 && pillaging->IsAcceptable())
			{
				GetNextPosition(*this, pillaging, tempPosition);
				SUnitStats tempUnit = GetNextUnit(unit, pillaging);
				
				if (pillaging->iRemainingMoves > 0 || tempPosition.canProbablyEndTurnAfterAssignment(tempUnit, testPlot, pillaging->eAssignmentType))
				{
					gAssignmentStorage.consumeOne();
					STacticalAssignment* stayAfterPillage = ScorePlotForMove(tempUnit, testPlot, tempPosition, EM_INTERMEDIATE);

					pillaging->AddScore(stayAfterPillage);
					//continue with the same plot, maybe in the final analysis we will skip the pillage because we need the movement points
					gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(pillaging, pillaging->Score()));
				}
			}

			//try ranged attacks
			if (pUnit->IsCanAttackRanged() && unit.iAttacksLeft > 0 && unit.iMovesLeft > 0)
			{
				const vector<int>& rangeAttackPlots = getRangeAttackPlotsForUnit(unit);
				for (vector<int>::const_iterator it = rangeAttackPlots.begin(); it != rangeAttackPlots.end(); ++it)
				{
					//the plot we're checking right now
					const CvTacticalPlot* enemyPlot = getTactPlot(*it);

					//note: all valid plots are visible by definition
					if (enemyPlot && enemyPlot->isEnemy())
					{
						STacticalAssignment* rangedAttack = ScorePlotForRangedAttack(unit, assumedUnitPlot, enemyPlot, *this);
						if (rangedAttack->Score() <= 0 || !rangedAttack->IsAcceptable())
							continue;

						GetNextPosition(*this, rangedAttack, tempPosition);
						SUnitStats tempUnit = GetNextUnit(unit, rangedAttack);

						//this is a catch-22: we really want to allow dangerous attacks because we might be able to kill the enemy unit making it safe.
						//experience shows this leads to a lot of "impossible" positions which have to be discarded later, killing performance.
						//but canProbablyEndTurnAfterThisAssignment() will consider near-kills for danger estimation!
						if (rangedAttack->iRemainingMoves == 0 && !tempPosition.canProbablyEndTurnAfterAssignment(tempUnit, testPlot, rangedAttack->eAssignmentType))
							continue;

						//discourage attacks on cities with non-siege units if the real target is something else
						if (enemyPlot->isEnemyCity() && getTarget() != enemyPlot->getPlot() && pUnit->GetRange() > 1 && pUnit->AI_getUnitAIType() != UNITAI_CITY_BOMBARD)
						{
							rangedAttack->SetScore(0, rangedAttack->GetBonusScore() / 2, rangedAttack->GetDamageDelta() / 2);
						}

						gAssignmentStorage.consumeOne();
						STacticalAssignment* stayAfterAttack = ScorePlotForMove(tempUnit, testPlot, tempPosition, EM_INTERMEDIATE);

						rangedAttack->AddScore(stayAfterAttack);
						gPossibleRangedAttacks.push_back(OptionWithScore<STacticalAssignment*>(rangedAttack, rangedAttack->Score()));
					}
				}
			}

			// Check if we should perform a admiral heal bomb
			if (pUnit->IsGreatAdmiral() && pUnit->canRepairFleet(testPlot->getPlot()))
			{
				STacticalAssignment* repairFleet = ScorePlotForAdmiralHeal(unit, testPlot, it->iMovesLeft, *this);
				if (repairFleet->Score() >= 2000)
				{
					gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(repairFleet, repairFleet->Score()));
					gAssignmentStorage.consumeOne();
				}
			}

			if (gPossibleRangedAttacks.empty())
			{
				//what is the score for simply staying put and not attacking anybody?
				//use EM_INTERMEDIATE so we're less strict concerning danger, enemies might be killed in the course of the sim
				STacticalAssignment* moveToPlot = ScorePlotForMove(unit, testPlot, *this, EM_INTERMEDIATE);

				GetNextPosition(*this, moveToPlot, tempPosition);
				SUnitStats tempUnit = GetNextUnit(unit, moveToPlot);

				//also try just staying in place
				//doesn't matter if we have movement left in this case
				if (tempPosition.canProbablyEndTurnAfterAssignment(tempUnit, testPlot, moveToPlot->eAssignmentType))
				{
					gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(moveToPlot, moveToPlot->Score()));
					gAssignmentStorage.consumeOne();
				}
			}
		}
		else //moving to a different plot
		{
			if (unit.eLastAssignment == A_MOVE)
				continue;

			//make sure we have a chance to execute this move ... so skip it if there is an unmoveable block
			if (IsCombatUnit(unit) && testPlot->IsSimUnitBlocking(DomainForUnit(pUnit)))
				continue;

			bool bPreviousWasMove = unit.eLastAssignment == A_MOVE_SWAP || unit.eLastAssignment == A_MOVE_SWAP_REVERSE;

			// Only combat units may ever do two moves in a row
			if (bPreviousWasMove && !IsCombatUnit(unit))
				continue;

			int iMoveTowardsTargetScore = 0;

			if (bTargetDistanceRelevant)
			{
				// Try to move towards the target
				int iNewPlotDistanceToTarget = TacticalAIHelpers::GetPlotDistanceToTarget(it->iPlotIndex, pUnit->getDomainType());
				if (iNewPlotDistanceToTarget < TACTICAL_COMBAT_MAX_TARGET_DISTANCE)
					iNewPlotDistanceToTarget = TACTICAL_COMBAT_MAX_TARGET_DISTANCE;
				iMoveTowardsTargetScore = (iOldPlotDistanceToTarget - iNewPlotDistanceToTarget) * 40;
				if (iNewPlotDistanceToTarget > iOldPlotDistanceToTarget)
					continue;
			}
			SUnitStats tempUnit = unit;
			tempUnit.iMovesLeft = it->iMovesLeft;

			STacticalAssignment* moveToPlot = ScorePlotForMove(tempUnit, testPlot, *this, EM_INTERMEDIATE);
			tempUnit = GetNextUnit(tempUnit, moveToPlot);

			//if the last assignment was a move, we should only do another move if another unit wants to swap us out
			//note: if a move increases tile visibility we change it to MOVE_FORCED so we can move again!
			if (bPreviousWasMove)
				moveToPlot->eAssignmentType = A_MOVE_DOUBLE;

			bool bRanged = pUnit->IsCanAttackRanged();

			//may not be able to end the turn ... 
			bool bMoveInForSkirmish = bRanged && unit.iAttacksLeft > 0 && moveToPlot->iRemainingMoves > (!pUnit->IsFreeAttackMoves() ? GC.getMOVE_DENOMINATOR() : 0) && pUnit->canMoveAfterAttacking();
			bool bMoveInForFrontlineUnit = !bRanged && moveToPlot->iRemainingMoves > 0 && unit.iAttacksLeft > 0 && testPlot->getEnemyDistance() == 1; // need to move here in order to check possible attacks
			bool bCanStay = false;

			if (!bMoveInForSkirmish && !bMoveInForFrontlineUnit)
				bCanStay = canProbablyEndTurnAfterAssignment(tempUnit, testPlot, moveToPlot->eAssignmentType);

			if (bMoveInForSkirmish || bMoveInForFrontlineUnit || bCanStay)
			{
				moveToPlot->AddScore(iMoveTowardsTargetScore, 0, 0);

				gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(moveToPlot, moveToPlot->Score()));
				gAssignmentStorage.consumeOne();
			}
		}
	}

	//it can happen that there are a lot of targets to attack - typically this means we should retreat!
	//so make sure we consider at least one MOVE and not only RANGE_ATTACK assignments
	//but keep at least one ranged attack!
	if (gPossibleRangedAttacks.size() >= (size_t)nMaxCount && nMaxCount>1)
	{
		std::stable_sort(gPossibleRangedAttacks.begin(), gPossibleRangedAttacks.end());
		gPossibleRangedAttacks.erase(gPossibleRangedAttacks.begin() + nMaxCount - 1, gPossibleRangedAttacks.end());
	}

	//only now add the ranged attacks
	gPossibleMoves.insert(gPossibleMoves.end(), gPossibleRangedAttacks.begin(), gPossibleRangedAttacks.end());

	//need to return in sorted order. note that we don't filter out bad (negative moves) they just are unlikely to get picked
	std::stable_sort(gPossibleMoves.begin(),gPossibleMoves.end());

	//don't return more than requested unless there is a tie
	if (gPossibleMoves.size() > (size_t)nMaxCount)
	{
		while (gPossibleMoves[nMaxCount].score == gPossibleMoves[nMaxCount - 1].score && (size_t)nMaxCount < gPossibleMoves.size())
			nMaxCount++;

		gPossibleMoves.erase(gPossibleMoves.begin() + nMaxCount, gPossibleMoves.end());
	}

	//always add a 'do-nothing' move; this does not guarantee that it will get picked but it allows move-attack-flee for damaged units
	//order does not matter, the parent function will sort again ... BLOCKED should be strictly worse than FINISH_TEMP
	//would be nice to have a way to make sure the "important" blocks (no other options) are picked over unimportant blocks (many other options)
	//but we should not give a bonus score for wasting movement points to paint ourselves into a corner!
	//also cannot check for the existamce of good moves here because we don't know yet if we can actually execute the good-looking moves
	//could also sort the moves by a secondary criterion, but best solution seems to be the "bad unit' mechanism to restart the sim
	STacticalAssignment* blocked = gAssignmentStorage.peekNext();
	blocked->init(unit.iPlotIndex, unit.iPlotIndex, unit.iUnitID, unit.iMovesLeft, unit.eMoveStrategy, A_BLOCKED, GetPrevPlotScore(unit.iUnitID, *this));
	blocked->SetScore(-10, 0, 0);
	gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(blocked, blocked->Score()));
	gAssignmentStorage.consumeOne();
}

//if we have many units we won't look at all of them (for performance reasons)
//obviously needs plot types to be defined
void CvTacticalPosition::dropSuperfluousUnits(int iMaxUnitsToKeep)
{
	//if we have weak units without a clear purpose we drop them
	int iNumAvailableUnits = GetNumAvailableUnits();
	if (iMaxUnitsToKeep > iNumAvailableUnits)
		iMaxUnitsToKeep = iNumAvailableUnits;

	//temporarily raise aggression level to make sure we consider all possible melee attacks (even those which only make sense after other attacks)
	eAggressionLevel actualLevel = getAggressionLevel();

	//very important before calling getPreferredAssignmentsForUnit()
	updateMovePlotsIfRequired();

	vector<SUnitStats>& availableUnits_w = availableUnits.write();

	//get the best move for each unit
	for (size_t i = 0; i < availableUnits_w.size() && iMaxUnitsToKeep>0; i++)
	{
		//this always returns at least one move
		getPreferredAssignmentsForUnit(availableUnits_w[i], 1);
		availableUnits_w[i].iImportanceScore = gPossibleMoves.front().score;
		gAssignmentStorage.reset(false);
	}

	//reset aggression to the original value
	eAggression = actualLevel;

	std::stable_sort(availableUnits_w.begin(), availableUnits_w.end());

	//simply consider those extra units as blocked.
	//since addAssignment will modify availableUnits, we copy the relevant units first
	vector<SUnitStats> unitsToDrop(availableUnits_w.begin()+iMaxUnitsToKeep, availableUnits_w.end() );
	if (!unitsToDrop.empty())
	{
		vector<STacticalAssignment>& assignedMoves_w = assignedMoves.write();
		for (vector<SUnitStats>::const_iterator itUnit = unitsToDrop.begin(); itUnit != unitsToDrop.end(); ++itUnit)
		{
			vector<SUnitStats>::iterator toDrop = find_if(availableUnits_w.begin(), availableUnits_w.end(), PrMatchingUnit(itUnit->iUnitID));
			availableUnits_w.erase(toDrop);
			STacticalAssignment blocked;
			blocked.init(itUnit->iPlotIndex, itUnit->iPlotIndex, itUnit->iUnitID, itUnit->iMovesLeft, itUnit->eMoveStrategy, A_BLOCKED, GetPrevPlotScore(itUnit->iUnitID, *this));
			blocked.SetScore(0, 0, 0);
			assignedMoves_w.push_back(blocked);
		}
	}
}

void CvTacticalPosition::addInitialAssignments()
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	//first pass. problem is scores might be off because officially we don't know where our units are yet
	for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
	{
		const CvTacticalPlot* tactPlot = getTactPlot(itUnit->iPlotIndex);
		if (tactPlot) //failsafe
		{
			SUnitStats tmp = *itUnit;
			tmp.iMovesLeft = 0;
			//we pretend the unit has zero moves, this means we do not score any possible attacks
			//this is important for symmetry with canStayInPlot()
			STacticalAssignment eInitialAssignmentNoMoves = *ScorePlotForMove(tmp, tactPlot, *this, EM_INITIAL);
			eInitialAssignmentNoMoves.iRemainingMoves = itUnit->iMovesLeft;
			eInitialAssignmentNoMoves.eAssignmentType = A_INITIAL;
			addAssignment(eInitialAssignmentNoMoves);
		}
	}

	//second pass. fix up the scores with correct number of adjacent units
	for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
	{
		const CvTacticalPlot* tactPlot = getTactPlot(itUnit->iPlotIndex);
		if (tactPlot) //failsafe
		{
			SUnitStats tmp = *itUnit;
			tmp.iMovesLeft = 0;
			STacticalAssignment eInitialAssignmentNoMoves = *ScorePlotForMove(tmp, tactPlot, *this, EM_INITIAL);
			STacticalAssignment* pInitial = getInitialAssignmentMutable(itUnit->iUnitID);
			if (pInitial)
			{
				pInitial->SetScore(&eInitialAssignmentNoMoves);
			}
		}
	}
}

//finding a particular unit
struct PrIsMoveForUnit
{
	int iUnitID;
	PrIsMoveForUnit(int iID) : iUnitID(iID) {}
	bool operator()(const STacticalAssignment& other) { return iUnitID == other.iUnitID; }
};

bool CvTacticalPosition::makeNextAssignments(int iMaxBranches, int iMaxChoicesPerUnit, CvTactPosStorage& storage,
	vector<CvTacticalPosition*>& openPositionsHeap, vector<CvTacticalPosition*>& completedPositions, const PrPositionSortHeapGeneration& heapSort,
	const vector<const CvUnit*>& ourUnits)
{
	/*
	abstract:
	get preferred plots for all combat units
	choose M best overall moves (combine as far as possible)
	create child positions
		assign moves
		update affected tact plots
		update unit reachable plots
	*/

	//very important, lazy update
	updateMovePlotsIfRequired();

	gMovesToAdd.clear();
	gOverAllChoices.clear();
	gAssignmentStorage.reset(false);
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();

	for (size_t i=0; i< availableUnits_r.size(); i++)
	{
		getPreferredAssignmentsForUnit(availableUnits_r[i], iMaxChoicesPerUnit);
		gOverAllChoices.insert( gOverAllChoices.end(), gPossibleMoves.begin(), gPossibleMoves.end() );
	}

	//important that moves are ordered by quality instead of unit id
	std::stable_sort(gOverAllChoices.begin(), gOverAllChoices.end());

	//sometimes we want to do combo moves, ie swaps should have the combined score of two moves
	//be generous when checking the candidates, so 5x the number of children we want in the end
	const size_t iMaxCandidates = max(iMaxBranches * 5u, gOverAllChoices.size() / 2);
	const size_t nChoices = gOverAllChoices.size();

	for (size_t iI = 0; iI < min(nChoices, iMaxCandidates); iI++)
	{
		const STacticalAssignment& assignment = *gOverAllChoices[iI].option;

		if (assignment.eAssignmentType == A_MOVE_DOUBLE)
			continue;

		if (assignment.eAssignmentType != A_MOVE)
		{
			gMovesToAdd.push_back(SComboMove());
			SComboMove& move = gMovesToAdd.back();
			move.addMove(assignment);

			// Bundle a following block/finish move to reduce search depth
			if (heapSort.bDepthFirst && iI + 1 < nChoices)
			{
				eUnitAssignmentType nextType = gOverAllChoices[iI + 1].option->eAssignmentType;
				if (nextType == A_FINISH_TEMP || nextType == A_BLOCKED || nextType == A_HEAL)
					if (move.addMove(*gOverAllChoices[iI + 1].option))
						iI++;
			}
			continue;
		}

		DomainTypes eDomain = GET_PLAYER(ePlayer).getUnit(assignment.iUnitID)->getDomainType();
		if (eDomain != DOMAIN_SEA)
			eDomain = DOMAIN_LAND;

		// A_MOVE: target plot may still be occupied
		int blocks = countBlockingUnitsAtPlot(assignment.iToPlotIndex, assignment.eMoveType, eDomain);

		if (blocks == 0)
		{ 
			gMovesToAdd.push_back(SComboMove()); gMovesToAdd.back().addMove(assignment);
			continue;
		}
		if (blocks > 1)
		{
			continue;
		}

		// blocks == 1: find best non-blocked move for the blocking unit
		int iBlockID = getFirstBlockingUnitIDAtPlot(assignment.iToPlotIndex, assignment.eMoveType, eDomain);

		for (size_t iJ = 0; iJ < nChoices; iJ++)
		{
			const STacticalAssignment& other = *gOverAllChoices[iJ].option;

			if (other.iUnitID != iBlockID)
				continue;
			if (other.eAssignmentType != A_MOVE && other.eAssignmentType != A_MOVE_DOUBLE)
				continue;

			// Swap: blocker wants to move into our current plot
			if ((iI < iJ || other.eAssignmentType == A_MOVE_DOUBLE) && assignment.iFromPlotIndex == other.iToPlotIndex)
			{
				const STacticalAssignment* pLA = getLatestAssignment(other.iUnitID);
				if (pLA->eAssignmentType == A_MOVE_SWAP)
					continue; // don't swap out a unit that was just swapped in

				const CvUnit* pOtherUnit = GET_PLAYER(ePlayer).getUnit(other.iUnitID);
				if (!pOtherUnit->ReadyToSwap())
					continue;

				gMovesToAdd.push_back(SComboMove());
				SComboMove& move = gMovesToAdd.back();
				move.addMove(assignment);
				move.a().eAssignmentType = A_MOVE_SWAP;
				move.addMove(other);
				move.b().eAssignmentType = A_MOVE_SWAP_REVERSE;
				break;
			}

			// Chain: blocker can vacate to a free plot
			if (!isMoveBlockedByOtherUnit(other, eDomain))
			{
				gMovesToAdd.push_back(SComboMove());
				SComboMove& move = gMovesToAdd.back();
				move.addMove(other);
				move.a().eAssignmentType = A_MOVE;
				move.addMove(assignment);
				break;
			}
		}
	}

	//note that this is a stable sort, meaning in case of ties the original order is maintained
	//since we have a fixed cutoff, the simulation result depends on the order the moves were created in, ie the order of units
	std::stable_sort(gMovesToAdd.begin(), gMovesToAdd.end(), SComboMove::PrEvalOrder());

	for (size_t i = 0; i < gMovesToAdd.size(); i++)
	{
		//we need memory for the new child but we'll commit it only later after the uniqueness check
		CvTacticalPosition* pNewChild = storage.peekNext();
		if (!pNewChild)
			break;

		//important, hook it up to the parent so we can access the history
		addChild(pNewChild);
		gCheckedPositions++;

		const STacticalAssignment& aRef = gMovesToAdd[i].getA();

		AddAssignmentResult a = pNewChild->addAssignment(aRef);

		AddAssignmentResult b = RESULT_NOOP;
		if (gMovesToAdd[i].hasB())
			b = pNewChild->addAssignment(gMovesToAdd[i].getB());

		//cannot add a RESTART in the middle of a combo move for consistency, so add afterwards
		if (a == RESULT_ADDED_W_VIS_CHANGE || b == RESULT_ADDED_W_VIS_CHANGE)
		{
			STacticalAssignment restart;
			restart.init(-1, -1, aRef.iUnitID, 0, aRef.eMoveType, A_RESTART, GetPrevPlotScore(aRef.iUnitID, *this));
			restart.SetScore(0, 0, 0);
			pNewChild->assignedMoves.write().push_back(restart);
		}

		//try to detect duplicates ...
		bool isConsistent = (a != RESULT_NOT_ADDED && b != RESULT_NOT_ADDED);
		if (isConsistent && pNewChild->isUnique(TACTSIM_UNIQUENESS_CHECK_GENERATIONS))
		{
			//do we need to keep working on this one?
			if (pNewChild->isEarlyFinish() || pNewChild->isExhausted())
			{
				int iProblemUnit = -1;
				bool bSuccess = true;

				// This returns true as soon as it's found a valid move
				if (bHasGeneral || bHasAdmiral || bHasSiegetower)
					bSuccess = TacticalAIHelpers::AddSupportMoves(*pNewChild, ourUnits, true);

				if (bSuccess && pNewChild->addFinishMovesIfAcceptable(pNewChild->isEarlyFinish(), iProblemUnit))
				{
					//good case, we're done
					completedPositions.push_back(pNewChild);
					giValidEndPos++;
					storage.consumeOne();
				}
				else
				{
					//position is illegal, do not remember it so we can re-use the memory
					giInvalidEndPos++;
					removeChild(pNewChild);

					//remember the id of the bad unit so if necessary we can try again without that unit
					//there might be double-counting or omissions of bad units but we just need to produce a plausible candidate
					if (iProblemUnit != -1)
						gBadUnitsCount[iProblemUnit]++;
				}
			}
			else
			{
				//also good case, keep this for the next round
				openPositionsHeap.push_back(pNewChild);
				push_heap(openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
				storage.consumeOne();
			}
		}
		else
			removeChild(pNewChild);

		if (childPositions.size() >= (size_t)iMaxBranches)
			break;
	}

	//can happen we have no children if all were considered redundant or invalid
	//note that we also considered blocked moves for all children, but those also may turn out to be invalid if the unit doesn't have enough moves to flee 
	return !childPositions.empty();
}

//lazy update of move plots
void CvTacticalPosition::updateMovePlotsIfRequired()
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	if (movePlotUpdateFlagA==-1 && movePlotUpdateFlagB==-1)
	{
		for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
			updateMoveAndAttackPlotsForUnit(*itUnit);
	}
	else
	{
		for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
			if (itUnit->iUnitID == movePlotUpdateFlagA || itUnit->iUnitID == movePlotUpdateFlagB)
				updateMoveAndAttackPlotsForUnit(*itUnit);
	}

	movePlotUpdateFlagA = 0;
	movePlotUpdateFlagB = 0;
}

bool CvTacticalPosition::isMoveBlockedByOtherUnit(const STacticalAssignment& move, DomainTypes eDomain) const
{
	//only movement can be blocked
	if (move.eAssignmentType != A_MOVE && move.eAssignmentType != A_MOVE_DOUBLE)
		return false;

	return countBlockingUnitsAtPlot(move.iToPlotIndex, move.eMoveType, eDomain) != 0;
}

int CvTacticalPosition::countBlockingUnitsAtPlot(int iPlotIndex, eUnitMovementStrategy moveType, DomainTypes eDomain) const
{
	int result = 0;

	const CvTacticalPlot* tactPlot = getTactPlot(iPlotIndex);
	if (!tactPlot)
		return result;

	const vector<STacticalUnit>& units = tactPlot->getUnitsAtPlot();
	const CvPlayer& kPlayer = GET_PLAYER(ePlayer);

	for (size_t i = 0; i < units.size(); i++)
	{
		DomainTypes eOtherDomain = kPlayer.getUnit(units[i].iUnitID)->getDomainType();
		if (eOtherDomain != DOMAIN_SEA)
			eOtherDomain = DOMAIN_LAND;

		if (eOtherDomain != eDomain)
			continue;

		if (isCombatUnit(moveType) && isCombatUnit(units[i].eMoveType))
			result++;
		if (isEmbarkedUnit(moveType) && isEmbarkedUnit(units[i].eMoveType))
			result++;
	}

	return result;
}

int CvTacticalPosition::getFirstBlockingUnitIDAtPlot(int iPlotIndex, eUnitMovementStrategy moveType, DomainTypes eDomain) const
{
	int result = -1;

	const CvTacticalPlot* tactPlot = getTactPlot(iPlotIndex);
	if (!tactPlot)
		return result;

	const vector<STacticalUnit>& units = tactPlot->getUnitsAtPlot();
	const CvPlayer& kPlayer = GET_PLAYER(ePlayer);

	for (size_t i = 0; i < units.size(); i++)
	{
		DomainTypes eOtherDomain = kPlayer.getUnit(units[i].iUnitID)->getDomainType();
		if (eOtherDomain != DOMAIN_SEA)
			eOtherDomain = DOMAIN_LAND;

		if (eOtherDomain != eDomain)
			continue;

		if (isCombatUnit(moveType) && isCombatUnit(units[i].eMoveType))
			return units[i].iUnitID;
		if (isEmbarkedUnit(moveType) && isEmbarkedUnit(units[i].eMoveType))
			return units[i].iUnitID;
	}

	return result;
}

//can we stop now?
bool CvTacticalPosition::isEarlyFinish(bool bExtraKill) const
{ 
	//simple - all enemies are gone (check for non-zero to make sure we aren't trivially complete)
	return nOriginalEnemies > 0 && nOriginalEnemies == nKilledEnemies + (bExtraKill ? 1 : 0);
}

//see if all the plots where our units would end their turn are acceptable
//this is a deferred check because in the beginning it's not clear how many enemy units we can eliminate
bool CvTacticalPosition::addFinishMovesIfAcceptable(bool bEarlyFinish, int& iBadUnitID)
{ 
	//we allow some "bad" units depending on context
	int iUnitLossThreshold = gDefaultUnitLossThreshold + nKilledEnemies;
	int iPotentialUnitLossCounter = 0;

	const vector<SUnitStats>& notQuiteFinishedUnits_r = notQuiteFinishedUnits.read();
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();

	//only units which have exhausted their moves are in this array! if the sim was aborted, somebody else will hopefully pick up the pieces
	for (size_t i=0; i < notQuiteFinishedUnits_r.size(); i++)
	{
		const SUnitStats& unit = notQuiteFinishedUnits_r[i];
		const STacticalAssignment* pInitial = getInitialAssignment(unit.iUnitID);
		if (!pInitial)
			return false; //something wrong

		//if the unit is blocked but has movement left and can flee, let's assume that is ok
		if (unit.eLastAssignment == A_BLOCKED && unit.iMovesLeft>0)
			continue;

		//make sure we don't leave a unit in an impossible position
		const CvTacticalPlot* tactPlot = getTactPlot(unit.iPlotIndex);
		SUnitStats tmp = unit;
		tmp.iMovesLeft = 0;
		STacticalAssignment* nextAssignment = ScorePlotForMove(unit, tactPlot, *this, EM_FINAL);

		if (bReturnToStartPositions && unit.pUnit->plot()->GetPlotIndex() != tactPlot->getPlotIndex())
			return false;

		if (unit.iMovesLeft < nSaveMovement)
			return false;

		bool bAccept = nextAssignment->IsAcceptable();
		if (!bAccept)
		{
			//second chance
			//allow putting the unit in danger if we can afford it and it's not one of our high-xp champions
			if (unit.pUnit->getExperienceTimes100() < gMedianUnitXP && iPotentialUnitLossCounter < iUnitLossThreshold)
			{
				iPotentialUnitLossCounter++;
				bAccept = true;
			}
		}

		if (bAccept)
		{
			//if the score is acceptable, end their turn. unless the unit is blocked, then we may use them for other tasks
			if (unit.eLastAssignment != A_BLOCKED)
			{
				nextAssignment->iRemainingMoves = unit.iMovesLeft;
				nextAssignment->eAssignmentType = A_FINISH;
				assignedMoves.write().push_back(*nextAssignment);
			}
		}
		else
		{
			iBadUnitID = unit.iUnitID;
			return false;
		}
	}

	//try to enforce some sort of sparsity, we should use only the minimum amount of units. 
	//so give a bonus for unmoved units. especially important in earlyFinish situations with many units.
	for (size_t i = 0; i < availableUnits_r.size(); i++)
	{
		const SUnitStats& unit = availableUnits_r[i];
		if (unit.eLastAssignment == A_INITIAL)
			plotScores.write()[unit.iUnitID] += isEarlyFinish() ? 123 : 45;
	}

	//scores look good and target was killed, we're done
	//second chance: did we kill anything (offensive), heal a unit, or at least improve the arrangement of our units (defensive)?
	//order is important here, need to add finish moves first!
	if (bEarlyFinish || isKillOrImprovedPosition())
	{
		vector<SUnitStats>& finishedUnits_w = finishedUnits.write();
		finishedUnits_w.insert(finishedUnits_w.end(), notQuiteFinishedUnits_r.begin(), notQuiteFinishedUnits_r.end());
		notQuiteFinishedUnits.write().clear();
		iBadUnitID = -1;
		return true;
	}

	//do not clear notQuiteFinishedUnits for better debugging
	return false;
}

//although we try to pick "positive" moves only sometimes there are only bad choices
bool CvTacticalPosition::isKillOrImprovedPosition() const
{
	const vector<STacticalAssignment>& assignedMoves_r = assignedMoves.read();
	//if we made a kill, that is always good
	//on the other hand it messes up the enemy distance, so we cannot really compare before/after in that case!
	//note that regular attacks are checked below!
	//also a restart means we discovered new enemies so that is also okay
	for (size_t i = nFirstInterestingAssignment; i < assignedMoves_r.size(); i++)
		if (isKillAssignment(assignedMoves_r[i].eAssignmentType) || assignedMoves_r[i].eAssignmentType==A_RESTART)
			return true;

	//find the original position to look up the unit move strategies
	const CvTacticalPosition* root = this;
	while (root->getParent())
		root = root->getParent();

	//if we did not make an attack, see if our units moved in the right way at least
	//note that this comparison only works because we know we did not kill an enemy, which would change enemyDistance!
	int iPositive = 0; //enemy distance change
	int iNegative = 0; //enemy distance change
	int iBefore = 0; //target distance change
	int iAfter = 0; //target distance change
	int iAttacksNoMove = 0;
	int iHealingUnits = 0;
	for (size_t i = nFirstInterestingAssignment; i < assignedMoves_r.size(); i++)
	{
		const STacticalAssignment& move = assignedMoves_r[i];
		if (move.eAssignmentType == A_FINISH)
		{
			//compare final to initial and see whether we came closer to the ideal distance
			const STacticalAssignment* initial = getInitialAssignment(move.iUnitID);
			const SUnitStats* unit = root->getAvailableUnitStats(move.iUnitID);

			// support units are not part of combat simulation
			if (!unit)
				continue;

			CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(unit->iUnitID);
			const CvTacticalPlot* initialPlot = root->getTactPlot(initial->iFromPlotIndex);
			const CvTacticalPlot* finalPlot = getTactPlot(move.iFromPlotIndex);

			//only relevant in degenerate cases without enemies
			int iDistBefore = TacticalAIHelpers::GetPlotDistanceToTarget(initial->iFromPlotIndex, pUnit->getDomainType());
			int iDistAfter = TacticalAIHelpers::GetPlotDistanceToTarget(move.iFromPlotIndex, pUnit->getDomainType());
			// Only count if both are reachable - can't meaningfully compare if either is INT_MAX
			if (iDistBefore != INT_MAX && iDistAfter != INT_MAX)
			{
				iBefore += iDistBefore;
				iAfter += iDistAfter;
			}

			//which domain to use here? for simplicity assume firstline is melee and in-domain, everything else cross-domain
			CvTacticalPlot::eTactPlotDomain eRelevantDomain = unit->eMoveStrategy == MS_FIRSTLINE ? (finalPlot->getPlot()->isWater() ? CvTacticalPlot::TD_SEA : CvTacticalPlot::TD_LAND) : CvTacticalPlot::TD_BOTH;
			int iInitialDistance = initialPlot->getEnemyDistance(eRelevantDomain);
			int iFinalDistance = finalPlot->getEnemyDistance(eRelevantDomain);

			//occupying a citadel is always fine
			bool bIsStayingInFrontlineCitadel = (initialPlot->getPlotIndex() == finalPlot->getPlotIndex()) &&
				finalPlot->getEnemyDistance() < 3 && TacticalAIHelpers::IsPlayerCitadel(finalPlot->getPlot(), getPlayer()) && pUnit->getDomainType() == DOMAIN_LAND;

			switch (unit->eMoveStrategy)
			{
			case MS_NONE:
				UNREACHABLE(); // Units are always supposed to be assigned a strategy.
			case MS_FIRSTLINE:
				if (bIsStayingInFrontlineCitadel)
					iPositive++;
				else if (iInitialDistance != 1 && iFinalDistance == 1)
					iPositive++;
				else if (iInitialDistance == 1 && iFinalDistance != 1)
					iNegative++;
				break;
			case MS_SECONDLINE:
				if (bIsStayingInFrontlineCitadel)
					iPositive++;
				else if (iInitialDistance != 2 && iFinalDistance == 2)
					iPositive++;
				else if (iInitialDistance == 2 && iFinalDistance != 2)
					iNegative++;
				break;
			case MS_THIRDLINE:
				//thirdline is a bit different, ignore citadels
				if (iInitialDistance == 1 && iFinalDistance > 1)
					iPositive++;
				if (iInitialDistance > 1 && iFinalDistance == 1)
					iNegative++;
				break;
			case MS_SUPPORT:
			case MS_EMBARKED:
				//ignore for now
				break;
			}
		}
		else if (move.eAssignmentType == A_RANGEATTACK || move.eAssignmentType == A_MELEEATTACK)
			//we already checked for kills!
			iAttacksNoMove++;
		else if (move.eAssignmentType == A_HEAL)
			iHealingUnits++;
		else if (move.eAssignmentType == A_USE_POWER)
			return true;

		//note that RESTARTS are ignored here ... just hope that we still find good moves after the restart
	}

	bool bMovingTowardsTarget = (bTargetDistanceRelevant && iAfter < iBefore);
	if (haveEnemies())
	{
		//staying in place and bombarding is fine!
		//staying in place and healing is also progress TODO: should we check whether we are healing more than we are taking damage?
		//note that retreating a damaged unit counts as positive because it should be MS_THIRDLINE
		return (iNegative < iPositive) || (iNegative == iPositive && (bMovingTowardsTarget || iAttacksNoMove > 0 || iHealingUnits > 1 || (iHealingUnits == root->GetNumAvailableUnits())));
	}
	else
	{
		//did we get closer to the target plot?
		return bMovingTowardsTarget || iHealingUnits > 0;
	}
}

//this influences how daring we'll be
void CvTacticalPosition::countEnemiesAndCheckVisibility()
{
	if (parentPosition != NULL)
		CUSTOMLOG("should not happen");

	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	PlotIndexContainer& enemyPlots_w = enemyPlots.write();
	vector<CvTacticalPlot>& tactPlots_w = tactPlots.write();

	//will add non-sim friendly units later
	nOurOriginalUnits = availableUnits_r.size();
	nOriginalEnemies = 0;
	enemyPlots_w.clear();

	for (size_t i = 0; i < tactPlots_w.size(); i++)
	{
		bool bEnemyBarbarianCamp = !GET_PLAYER(ePlayer).isBarbarian() ? tactPlots_w[i].getPlot()->getRevealedImprovementType(GET_PLAYER(ePlayer).getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT) : false;
		if (tactPlots_w[i].isEnemy() || bEnemyBarbarianCamp)
		{
			// Cities and barbarian camps count as one enemy each
			if (tactPlots_w[i].isEnemyCity())
				nOriginalEnemies++;
			else if (bEnemyBarbarianCamp)
				nOriginalEnemies++;

			for (int iI = 0; iI < tactPlots_w[i].getPlot()->getNumUnits(); iI++)
			{
				CvUnit* pEnemyUnit = tactPlots_w[i].getPlot()->getUnitByIndex(iI);
				if (pEnemyUnit->IsCombatUnit())
					nOriginalEnemies++;
			}
			enemyPlots_w.push_back(tactPlots_w[i].getPlotIndex());
		}

		//also count our non-sim units which are close to the front
		if (tactPlots_w[i].isBlockedByNonSimUnit(CvTacticalPlot::TD_BOTH, true) && tactPlots_w[i].getEnemyDistance() < 3)
			nOurOriginalUnits++;

		//ignore range 1, we can always see those plots so they are boring
		//ignore range 4+, this is too far out and we don't have those plots cached
		const vector<CvPlot*>& vSeeToPlots2 = GC.getMap().GetPlotsAtRangeX(tactPlots_w[i].getPlot(), 2, true, true);
		const vector<CvPlot*>& vSeeToPlots3 = GC.getMap().GetPlotsAtRangeX(tactPlots_w[i].getPlot(), 3, true, true);
		int iVisiblityScore2 = 0;
		int iVisiblityScore3 = 0;

		//give a bonus to plots with high outward visibility, an enemy might come into range next turn!
		//however, we are only interested in the direction towards the enemy ... look at enemy distance as a proxy 
		for (size_t j = 0; j < vSeeToPlots2.size(); j++)
		{
			//null is used as a sentinel value
			if (vSeeToPlots2[j] == NULL)
				continue;

			const CvTacticalPlot* testPlot = getTactPlot(vSeeToPlots2[j]->GetPlotIndex());
			if (testPlot && testPlot->getEnemyDistance() <= 3)
				iVisiblityScore2++;
		}
		for (size_t j = 0; j < vSeeToPlots3.size(); j++)
		{
			//null is used as a sentinel value
			if (vSeeToPlots3[j] == NULL)
				continue;

			const CvTacticalPlot* testPlot = getTactPlot(vSeeToPlots3[j]->GetPlotIndex());
			if (testPlot && testPlot->getEnemyDistance() <= 3)
				iVisiblityScore3++;
		}

		tactPlots_w[i].setNumVisiblePlotsRange2(iVisiblityScore2);
		tactPlots_w[i].setNumVisiblePlotsRange3(iVisiblityScore3);
	}

	//need this to be sorted for binary search
	std::stable_sort(enemyPlots_w.begin(), enemyPlots_w.end());
}

void CvTacticalPosition::refreshVolatilePlotProperties(bool bInitial)
{
	gLandEnemies.clear();
	gSeaEnemies.clear();
	gCitadels.clear();
	gCities.clear();

	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	vector<CvTacticalPlot>& tactPlots_w = tactPlots.write();

	for (vector<CvTacticalPlot>::iterator it = tactPlots_w.begin(); it != tactPlots_w.end(); ++it)
	{
		//need to check whether this is a city or actual units
		if (it->isEnemyCity())
			gCities.push_back(it->getPlotIndex());
		//ignore garrisons for enemy distance consideration
		else if (it->isEnemyCombatUnit())
		{
			if (it->getEnemyDistance(CvTacticalPlot::TD_LAND) == 0)
				gLandEnemies.push_back(it->getPlotIndex());
			if (it->getEnemyDistance(CvTacticalPlot::TD_SEA) == 0)
				gSeaEnemies.push_back(it->getPlotIndex());
		}

		it->resetVolatileProperties();

		//include plots with enemies if they are just outside of the simulation range
		//we won't attack them but we won't ignore them either
		it->checkEdgePlotsForSurprises(*this,gLandEnemies,gSeaEnemies);

		int iImprovementDamage = TacticalAIHelpers::GetOtherPlayerImprovementDamage(it->getPlot(), getPlayer(), true);
		if (iImprovementDamage > 0 && !plotHasAssignmentOfType(it->getPlotIndex(), A_PILLAGE))
			gCitadels.push_back(make_pair(it->getPlotIndex(), iImprovementDamage));
	}

	// Some teams have bonus sight range in certain circumstances
	TeamTypes eTeam = !availableUnits_r.empty() ? availableUnits_r[0].pUnit->getTeam() : NO_TEAM;

	//distance transform
	for (vector<CvTacticalPlot>::iterator it = tactPlots_w.begin(); it != tactPlots_w.end(); ++it)
	{
		const CvPlot* pSourcePlot = it->getPlot();
		const CvPlot* pTargetPlot;
		//pt1
		for (size_t i = 0; i < gLandEnemies.size(); i++)
		{
			pTargetPlot = GC.getMap().plotByIndexUnchecked(gLandEnemies[i]);
			int iDistance = plotDistance(*pSourcePlot, *pTargetPlot);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_BOTH))
				it->setEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_LAND))
				it->setEnemyDistance(CvTacticalPlot::TD_LAND, iDistance);
			if (iDistance == 1)
			{
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_BOTH, it->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH) + 1);
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_LAND, it->getNumAdjacentEnemies(CvTacticalPlot::TD_LAND) + 1);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_LAND))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_LAND, iDistance);
			}
		}

		//pt2
		for (size_t i = 0; i < gSeaEnemies.size(); i++)
		{
			pTargetPlot = GC.getMap().plotByIndexUnchecked(gSeaEnemies[i]);
			int iDistance = plotDistance(*pSourcePlot, *pTargetPlot);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_BOTH))
				it->setEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_SEA))
				it->setEnemyDistance(CvTacticalPlot::TD_SEA, iDistance);
			if (iDistance == 1)
			{
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_BOTH, it->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH) + 1);
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_SEA, it->getNumAdjacentEnemies(CvTacticalPlot::TD_SEA) + 1);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_SEA))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_SEA, iDistance);
			}
		}

		//pt3
		for (size_t i = 0; i < gCities.size(); i++)
		{
			pTargetPlot = GC.getMap().plotByIndexUnchecked(gCities[i]);
			int iDistance = plotDistance(*pSourcePlot, *pTargetPlot);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_BOTH))
				it->setEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_LAND))
				it->setEnemyDistance(CvTacticalPlot::TD_LAND, iDistance);
			if (iDistance < it->getEnemyDistance(CvTacticalPlot::TD_SEA))
				it->setEnemyDistance(CvTacticalPlot::TD_SEA, iDistance);
			if (iDistance == 1)
			{
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_BOTH, it->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH) + 1);
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_LAND, it->getNumAdjacentEnemies(CvTacticalPlot::TD_LAND) + 1);
				it->setNumAdjacentEnemies(CvTacticalPlot::TD_SEA, it->getNumAdjacentEnemies(CvTacticalPlot::TD_SEA) + 1);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_BOTH, iDistance);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_LAND))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_LAND, iDistance);
			}
			if (iDistance < it->getRangedAttackEnemyDistance(CvTacticalPlot::TD_SEA))
			{
				if (pSourcePlot->canSeePlot(pTargetPlot, eTeam, iDistance, NO_DIRECTION))
					it->setRangedAttackEnemyDistance(CvTacticalPlot::TD_SEA, iDistance);
			}
		}
	}

	//citadels
	for (size_t i=0; i<gCitadels.size(); i++)
	{
		//iterate neighbors
		CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(GC.getMap().plotByIndexUnchecked(gCitadels[i].first));
		for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
		{
			CvPlot* pNeighbor = aNeighbors[i];
			if (!pNeighbor)
				continue;

			CvTacticalPlot* tactPlot = getTactPlotMutable(pNeighbor->GetPlotIndex());
			if (tactPlot)
				tactPlot->SetAdjacentImprovementDamage(max(tactPlot->GetAdjacentImprovementDamage(), gCitadels[i].second));
		}
	}

	//deferred update of adjacent unit count for non-sim units
	if (bInitial)
	{
		for (vector<CvTacticalPlot>::iterator it = tactPlots_w.begin(); it != tactPlots_w.end(); ++it)
		{
			//even if we're not sure whether the unit is going to stay, we can get temporary benefits
			if (it->isBlockedByNonSimUnit(CvTacticalPlot::TD_LAND))
			{
				it->changeNeighboringUnitCount(*this, MS_FIRSTLINE, CvTacticalPlot::TD_LAND, +1);
				if (it->isCombatEndTurn())
					it->setCombatUnitEndTurn(*this, CvTacticalPlot::TD_LAND, true);
			}
			//don't count cities twice
			if (it->isBlockedByNonSimUnit(CvTacticalPlot::TD_SEA) && !it->getPlot()->isCity())
			{
				it->changeNeighboringUnitCount(*this, MS_FIRSTLINE, CvTacticalPlot::TD_SEA, +1);
				if (it->isCombatEndTurn())
					it->setCombatUnitEndTurn(*this, CvTacticalPlot::TD_SEA, true);
			}
		}
	}
}

//need a default constructor for stl containers ...
CvTacticalPosition::CvTacticalPosition()
{
	initFromScratch(NO_PLAYER, AL_LOW, NULL, false, false, false);
}

void CvTacticalPosition::initFromScratch(PlayerTypes player, eAggressionLevel eAggLvl, CvPlot* pTarget, bool bTargetDistanceRelevant_, bool bReturnToStartPositions_, int iSaveMovement_)
{
	ePlayer = player;
	pTargetPlot = pTarget;
	bTargetDistanceRelevant = bTargetDistanceRelevant_;
	bReturnToStartPositions = bReturnToStartPositions_;
	bHasGeneral = false;
	bHasAdmiral = false;
	bHasSiegetower = false;
	nSaveMovement = iSaveMovement_;
	eAggression = eAggLvl;
	nOurOriginalUnits = 0;
	nOriginalEnemies = 0;
	nKilledEnemies = 0;
	nFirstInterestingAssignment = 0;
	iBonusScore = 0;
	iDamageDelta = 0;
	iTotalScore = 0;
	iScoreOverParent = 0; 
	parentPosition = NULL;
	iGeneration = 0;
	iID = 1; //zero doesn't work here
	movePlotUpdateFlagA = 0;
	movePlotUpdateFlagB = 0;

	childPositions.clear();
	tactPlotLookup.clear();
	tactPlots.clear();
	availableUnits.clear();
	notQuiteFinishedUnits.clear();
	finishedUnits.clear();
	assignedMoves.clear();
	enemyPlots.clear();
	freedPlots.clear();
	unitDamageDealt.clear();
	plotScores.clear();
}

void CvTacticalPosition::initFromParent(const CvTacticalPosition& parent)
{
	ePlayer = parent.ePlayer;
	pTargetPlot = parent.pTargetPlot;
	bTargetDistanceRelevant = parent.bTargetDistanceRelevant;
	bReturnToStartPositions = parent.bReturnToStartPositions;
	bHasGeneral = parent.bHasGeneral;
	bHasAdmiral = parent.bHasAdmiral;
	bHasSiegetower = parent.bHasSiegetower;
	nSaveMovement = parent.nSaveMovement;
	eAggression = parent.eAggression;
	nOurOriginalUnits = parent.nOurOriginalUnits;
	nOriginalEnemies = parent.nOriginalEnemies;
	nKilledEnemies = parent.nKilledEnemies;
	nFirstInterestingAssignment = parent.nFirstInterestingAssignment;
	iBonusScore = parent.iBonusScore;
	iDamageDelta = parent.iDamageDelta;
	iTotalScore = parent.iTotalScore;
	iScoreOverParent = 0;
	parentPosition = &parent;
	movePlotUpdateFlagA = parent.movePlotUpdateFlagA;
	movePlotUpdateFlagB = parent.movePlotUpdateFlagB;
	iGeneration = parent.iGeneration + 1;

	//clever scheme to encode the tree structure into IDs
	//works only if the tree is not too wide or too deep
	if (parent.getID() < ULLONG_MAX / 10 - 10)
		iID = parent.getID() * 10 + parent.childPositions.size();
	else
		iID = ULLONG_MAX;

	//childPositions stays empty!
	childPositions.clear();

	//copied from parent, modified when addAssignment is called
	tactPlotLookup.inheritFrom(parent.tactPlotLookup.read());
	tactPlots.inheritFrom(parent.tactPlots.read());
	assignedMoves.inheritFrom(parent.assignedMoves.read());
	availableUnits.inheritFrom(parent.availableUnits.read());
	notQuiteFinishedUnits.inheritFrom(parent.notQuiteFinishedUnits.read());
	finishedUnits.inheritFrom(parent.finishedUnits.read());
	freedPlots.inheritFrom(parent.freedPlots.read());
	enemyPlots.inheritFrom(parent.enemyPlots.read());
	unitDamageDealt.inheritFrom(parent.unitDamageDealt.read());
	plotScores.inheritFrom(parent.plotScores.read());
}

bool CvTacticalPosition::haveEnemies() const
{
	return nOriginalEnemies > 0;
}

bool CvTacticalPosition::removeChild(CvTacticalPosition* pChild)
{
	//just unlink the child - do not delete it, the memory is allocated statically
	vector<CvTacticalPosition*>::iterator it = find(childPositions.begin(), childPositions.end(), pChild);
	if (it!=childPositions.end())
		childPositions.erase(it);

	return false;
}

size_t CvTacticalPosition::addChild(CvTacticalPosition* pChild)
{
	if (pChild)
	{
		childPositions.push_back(pChild); //this order is better for generating an ID for the child
		pChild->initFromParent(*this);
	}
	return childPositions.size();
}

void CvTacticalPosition::getPlotsWithChangedVisibility(const STacticalAssignment& assignment, vector<int>& madeVisible) const
{
	madeVisible.clear();

	CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(assignment.iUnitID);
	CvPlot* pNewPlot = GC.getMap().plotByIndexUnchecked(assignment.iToPlotIndex);

	for (int i=1; i<RING3_PLOTS; i++)
	{
		CvPlot* pTestPlot = iterateRingPlots(pNewPlot,i); //iterate around the new plot, not the old plot
		if (!pTestPlot)
			continue;

		//todo: check for distance to target? TACTICAL_COMBAT_MAX_TARGET_DISTANCE

		if (pTestPlot->getVisibilityCount(pUnit->getTeam())==0)
		{
			if (pNewPlot->canSeePlot(pTestPlot, pUnit->getTeam(), pUnit->visibilityRange(), pUnit->getFacingDirection(true)))
				madeVisible.push_back(pTestPlot->GetPlotIndex());
		}
	}
}

void CvTacticalPosition::updateMoveAndAttackPlotsForUnit(SUnitStats unit)
{
	CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(unit.iUnitID);
	CvPlot* pStartPlot = GC.getMap().plotByIndexUnchecked(unit.iPlotIndex);
	const PlotIndexContainer& freedPlots_r = freedPlots.read();

	TCachedMovePlots::const_iterator itP = gReachablePlotsLookup.find(SPathFinderStartPos(unit, freedPlots_r));
	if (itP != gReachablePlotsLookup.end())
	{
		gMovePlotsCacheHit++;
	}
	else
	{
		gMovePlotsCacheMiss++;

		//note: we allow (intermediate) embarkation here but filter out the non-native plots later (useful for denmark and lategame)
		int iMoveFlags = CvUnit::MOVEFLAG_IGNORE_STACKING_SELF | CvUnit::MOVEFLAG_IGNORE_DANGER;
		ReachablePlots reachablePlots = TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pStartPlot, iMoveFlags, 0, unit.iMovesLeft, freedPlots_r);

		//need to know this if we're doing defensive positioning
		bool bTargetIsEnemy = pTargetPlot->isEnemyUnit(ePlayer, true, true) || pTargetPlot->isEnemyCity(*pUnit);
		CvPlayer& kPlayer = GET_PLAYER(ePlayer);

		//try to save some memory here
		ReachablePlots reachablePlotsPruned;
		for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
		{
			CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

			//this is a performance fix / logic simplification
			//all open positions share just one instance of gSafePlotCount
			//we only update the count once at the start of the sim
			//if there is no safe plot then, there will never be one!
			if (!parentPosition)
			{
				bool bIsSafe = GET_PLAYER(ePlayer).GetPlotDanger(*pPlot, pUnit, GetUnitDamageDealt(), 0) < pUnit->GetCurrHitPoints();
				if (bIsSafe && pUnit->canEndTurnAtPlot(pPlot))
					gSafePlotCount[unit.iUnitID]++;
			}

			//note that if the unit is far away, it won't have any good plots and will be considered blocked
			//this is just a rough check, we check the existance of a corresponding tact plot below
			//+2 is due to the "maneuver space" around each enemy
			if (TacticalAIHelpers::GetPlotDistanceToTarget(it->iPlotIndex, pUnit->getDomainType()) > TACTICAL_COMBAT_MAX_TARGET_DISTANCE+2)
				continue;

			if (unit.eMoveStrategy == MS_EMBARKED)
			{
				//only allow disembarking if it takes us closer to the target
				if (TacticalAIHelpers::GetPlotDistanceToTarget(it->iPlotIndex, pUnit->getDomainType()) >= TacticalAIHelpers::GetPlotDistanceToTarget(pUnit->plot()->GetPlotIndex(), pUnit->getDomainType()))
					continue;
			}
			else
			{
				if (haveEnemies())
				{
					//ignore all plots where we cannot fight. allow ships to capture/garrison cities though!
					if (!pUnit->isNativeDomain(pPlot) && !pPlot->isCoastalCityOrPassableImprovement(pUnit->getOwner(),false,false))
						continue;
				}
				else
				{
					//we don't want to fight so embarkation is ok if we're careful
					if (!pUnit->isNativeDomain(pPlot))
					{
						//for embarked units, every attacker is bad news
						if (bTargetIsEnemy || !kPlayer.GetPossibleAttackers(*pPlot, NO_TEAM).empty())
							continue;

						CvTacticalDominanceZone* pZone = GET_PLAYER(ePlayer).GetTacticalAI()->GetTacticalAnalysisMap()->GetZoneByPlot(pPlot);
						if (pZone && pZone->GetOverallDominanceFlag() != TACTICAL_DOMINANCE_FRIENDLY)
							continue;
					}
				}
			}

			//last (expensive) check, need to have a tact plot for each reachable plot
			const CvTacticalPlot* plot = getTactPlot(it->iPlotIndex);
			if (plot && !plot->isBlockedByNonSimUnit(DomainForUnit(pUnit)))
				reachablePlotsPruned.insertNoIndex(*it);
		}

		reachablePlotsPruned.createIndex();
		gReachablePlotsLookup[SPathFinderStartPos(unit, freedPlots_r)] = reachablePlotsPruned;
	}

	//simply ignore visibility here, later there's a check if there is a valid tactical plot for the targets
	TCachedRangeAttackPlots::const_iterator itA = gRangeAttackPlotsLookup.find(make_pair(unit.iUnitID, unit.iPlotIndex));
	if (itA != gRangeAttackPlotsLookup.end())
		gAttackPlotsCacheHit++;
	else
	{
		gAttackPlotsCacheMiss++;
		vector<int> rangeAttackPlots = TacticalAIHelpers::GetPlotsUnderRangedAttackFrom(pUnit, pStartPlot, true, true);
		gRangeAttackPlotsLookup[make_pair(unit.iUnitID, unit.iPlotIndex)] = rangeAttackPlots;
	}
}

bool CvTacticalPosition::isAttackablePlot(int iPlotIndex) const
{
	const PlotIndexContainer& enemyPlots_r = enemyPlots.read();
	return std::binary_search(enemyPlots_r.begin(), enemyPlots_r.end(), iPlotIndex );
}

pair<int,int> CvTacticalPosition::doVisibilityUpdate(const STacticalAssignment& newAssignment)
{
	int nNewEnemies = 0;

	//may need to add some new tactical plots - ideally we should reconsider all queued assignments afterwards
	//the next round of assignments will take into account the new plots in any case
	getPlotsWithChangedVisibility(newAssignment, gNewlyVisiblePlots);
	for (size_t i=0; i<gNewlyVisiblePlots.size(); i++)
	{
		//since it was invisible before, we know there are no friendly units around
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(gNewlyVisiblePlots[i]);
		if (pPlot) //also create plots for neutral units - otherwise edgeOfTheKnownWorld is not correct
		{
			//can pass empty set of units - the plot was invisible before so we know there is none of our units there
			if (addTacticalPlot(pPlot, vector<const CvUnit*>()))
			{
				//we revealed a new enemy ... need to execute moves up to here, do a danger plot update and reconsider
				if (pPlot->isEnemyUnit(ePlayer, true, false))
					nNewEnemies++;

#if defined(MOD_CORE_DEBUGGING)
				if (MOD_CORE_DEBUGGING)
				{
					//make sure that the adjacent unit count is correct
					//normally it should because adjacent plots are visible from the beginning but ...
					CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(GC.getMap().plotByIndexUnchecked(pPlot->GetPlotIndex()));
					for (int i = 0; i < NUM_DIRECTION_TYPES; i++)
					{
						CvPlot* pNeighbor = aNeighbors[i];
						if (!pNeighbor)
							continue;

						const CvTacticalPlot* neighborPlot = getTactPlot(pNeighbor->GetPlotIndex());
						if (neighborPlot)
						{
							const vector<STacticalUnit>& units = neighborPlot->getUnitsAtPlot();
							for (size_t j = 0; j < units.size(); j++)
								ASSERT(!isCombatUnit(units[j].eMoveType));
						}
					}
				}
#endif
			}
		}
	}

	if (nNewEnemies>0)
		refreshVolatilePlotProperties();

	return make_pair( (int)gNewlyVisiblePlots.size(), nNewEnemies);
}

CvTacticalPosition::AddAssignmentResult CvTacticalPosition::addAssignment(const STacticalAssignment& newAssignment)
{
	//if we killed an enemy ZOC will change
	bool bRecomputeAllMoves = false;
	//newly visible plots, newly visible enemies
	std::pair<int, int> visibilityResult(0, 0);
	vector<SUnitStats>& availableUnits_w = availableUnits.write();
	
	vector<SUnitStats>::iterator itUnit = find_if(availableUnits_w.begin(), availableUnits_w.end(), PrMatchingUnit(newAssignment.iUnitID));

	if (itUnit == availableUnits_w.end() || itUnit->iPlotIndex != newAssignment.iFromPlotIndex)
		return RESULT_NOT_ADDED;

	//tactical plots are only touched for "real" moves. blocked units may be on invalid plots.
	//a unit may also start out on an invalid plot (eg. too far away)
	if (newAssignment.eAssignmentType != A_BLOCKED && itUnit->eLastAssignment != A_INITIAL)
	{
		if (!getTactPlotMutable(newAssignment.iToPlotIndex))
			return RESULT_NOT_ADDED;
		if (!getTactPlotMutable(newAssignment.iFromPlotIndex))
			return RESULT_NOT_ADDED;
	}

	//i know what you did last summer!
	itUnit->eLastAssignment = newAssignment.eAssignmentType;

	//store the assignment
	assignedMoves.write().push_back(newAssignment);

	//now deal with the consequencess
	bool bAffectsScore = true;
	bool bEndOfSim = false;
	switch (newAssignment.eAssignmentType)
	{
	case A_INITIAL:
		getTactPlotMutable(newAssignment.iToPlotIndex)->friendlyUnitMovingIn(*this, newAssignment);
		bAffectsScore = false;
		break;
	case A_MOVE_FORCED:
	case A_MOVE_SWAP_REVERSE:
	case A_MOVE:
	case A_MOVE_SWAP:
	case A_CAPTURE:
	{
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		itUnit->iPlotIndex = newAssignment.iToPlotIndex;

		//do the visibility update first so all newly neighboring plots become part of the sim
		visibilityResult = doVisibilityUpdate(newAssignment);

		//now do the accounting for the neighbor plots
		getTactPlotMutable(newAssignment.iFromPlotIndex)->friendlyUnitMovingOut(*this, newAssignment);
		getTactPlotMutable(newAssignment.iToPlotIndex)->friendlyUnitMovingIn(*this, newAssignment);

		//in case this was a move which revealed new enemies, pretend it was a forced move so we can move again
		if (visibilityResult.second > 0 && newAssignment.eAssignmentType == A_MOVE)
			itUnit->eLastAssignment = A_MOVE_FORCED;

		if (!GET_PLAYER(ePlayer).isBarbarian())
		{
			CvPlot* pFuturePlot = GC.getMap().plotByIndexUnchecked(newAssignment.iToPlotIndex);
			if (pFuturePlot->getRevealedImprovementType(GET_PLAYER(ePlayer).getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
			{
				//barbarian camps are counted as a separate enemy
				nKilledEnemies++;
			}
		}
		//aoe damage on move
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitDamage.begin(); it != newAssignment.unitDamage.end(); ++it)
			ChangeUnitDamage((*it).first, (*it).second);
		if (newAssignment.iDamagedCityId != -1)
			ChangeCityDamage(newAssignment.iDamagedCityId, newAssignment.iCityDamage);
		break;
	}
	case A_RANGEATTACK:
	case A_MELEEATTACK:
	{
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		itUnit->iAttacksLeft--;
		itUnit->iSelfDamage += newAssignment.iSelfDamage;
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitDamage.begin(); it != newAssignment.unitDamage.end(); ++it)
			ChangeUnitDamage((*it).first, (*it).second);
		if (newAssignment.iDamagedCityId != -1)
			ChangeCityDamage(newAssignment.iDamagedCityId, newAssignment.iCityDamage);
		break;
	}
	case A_RANGEKILL:
	{
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		itUnit->iAttacksLeft--;
		getTactPlotMutable(newAssignment.iToPlotIndex)->removeEnemyUnitIfPresent();
		refreshVolatilePlotProperties();
		bRecomputeAllMoves = true; //ZOC changed
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitDamage.begin(); it != newAssignment.unitDamage.end(); ++it)
			ChangeUnitDamage((*it).first, (*it).second);
		if (newAssignment.iDamagedCityId != -1)
			ChangeCityDamage(newAssignment.iDamagedCityId, newAssignment.iCityDamage);
		freedPlots.write().push_back(newAssignment.iToPlotIndex);
		nKilledEnemies++;
		if (!getTactPlot(newAssignment.iToPlotIndex)->getEnemyUnit())
		{
			PlotIndexContainer& enemyPlots_w = enemyPlots.write();
			enemyPlots_w.erase(std::remove(enemyPlots_w.begin(), enemyPlots_w.end(), newAssignment.iToPlotIndex), enemyPlots_w.end());
		}
		break;
	}
	case A_MELEEKILL_NO_ADVANCE:
	case A_MELEEKILL:
	{
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		itUnit->iAttacksLeft--;
		itUnit->iSelfDamage += newAssignment.iSelfDamage;

		if (newAssignment.eAssignmentType == A_MELEEKILL)
		{
			itUnit->iPlotIndex = newAssignment.iToPlotIndex; //this is because we're advancing

			//do the visibility update first so all newly neighboring plots become part of the sim
			//do this before the distance update
			visibilityResult = doVisibilityUpdate(newAssignment);
			
			//now the accounting
			getTactPlotMutable(newAssignment.iFromPlotIndex)->friendlyUnitMovingOut(*this, newAssignment); //this is because we're advancing
			getTactPlotMutable(newAssignment.iToPlotIndex)->friendlyUnitMovingIn(*this, newAssignment); //this implicitly removes the enemyUnit flag
		}
		else //NO_ADVANCE
			getTactPlotMutable(newAssignment.iToPlotIndex)->removeEnemyUnitIfPresent();

		//includes splash damage
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitDamage.begin(); it != newAssignment.unitDamage.end(); ++it)
			ChangeUnitDamage((*it).first, (*it).second);
		if (newAssignment.iDamagedCityId != -1)
			ChangeCityDamage(newAssignment.iDamagedCityId, newAssignment.iCityDamage);

		CvPlot* pFutureExEnemyPlot = GC.getMap().plotByIndexUnchecked(newAssignment.iToPlotIndex);
		if (pFutureExEnemyPlot->isCity())
		{
			nKilledEnemies++;
		}
		else if (pFutureExEnemyPlot->getRevealedImprovementType(GET_PLAYER(ePlayer).getTeam()) == GD_INT_GET(BARBARIAN_CAMP_IMPROVEMENT))
		{
			nKilledEnemies++;
		}
		for (int iI = 0; iI < pFutureExEnemyPlot->getNumUnits(); iI++)
		{
			CvUnit* pEnemyUnit = pFutureExEnemyPlot->getUnitByIndex(iI);
			if (pEnemyUnit->IsCombatUnit())
				nKilledEnemies++;
		}

		if (getTactPlot(newAssignment.iToPlotIndex)->getEnemyUnit() == NULL)
		{
			freedPlots.write().push_back(newAssignment.iToPlotIndex);
			if (!getTactPlot(newAssignment.iToPlotIndex)->getEnemyUnit())
			{
				PlotIndexContainer& enemyPlots_w = enemyPlots.write();
				enemyPlots_w.erase(std::remove(enemyPlots_w.begin(), enemyPlots_w.end(), newAssignment.iToPlotIndex), enemyPlots_w.end());
			}
		}

		//important that we do the visibility update first!
		refreshVolatilePlotProperties();
		bRecomputeAllMoves = true; //ZOC changed
		break;
	}
	case A_PILLAGE:
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		if (TacticalAIHelpers::GetOtherPlayerImprovementDamage( GC.getMap().plotByIndexUnchecked(newAssignment.iToPlotIndex), getPlayer(), true) > 0)
			refreshVolatilePlotProperties();
		//aoe damage on pillage
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitDamage.begin(); it != newAssignment.unitDamage.end(); ++it)
			ChangeUnitDamage((*it).first, (*it).second);
		//aoe heal on pillage
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitHealing.begin(); it != newAssignment.unitHealing.end(); ++it)
			HealFriendlyUnit((*it).first, (*it).second);
		break;
	case A_USE_POWER:
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		bEndOfSim = true;
		//admiral heal
		for (SUnitIDValueContainer::const_iterator it = newAssignment.unitHealing.begin(); it != newAssignment.unitHealing.end(); ++it)
			HealFriendlyUnit((*it).first, (*it).second);
		break;
	case A_FINISH:
		OutputDebugString("this should not happen\n");
	case A_HEAL:
	case A_FINISH_TEMP:
		bEndOfSim = true;
		break;
	case A_BLOCKED:
		bAffectsScore = false;
		bEndOfSim = true;
		break;
	default:
		UNREACHABLE();
	}

	//we update the moveplots lazily because it takes a while and we don't know yet if we will ever follow up on this position
	if (bRecomputeAllMoves)
	{
		movePlotUpdateFlagA = -1;
		movePlotUpdateFlagB = -1;
	}
	else if (itUnit->iMovesLeft>0 && !bEndOfSim)
	{
		//make sure we don't regress to a "lower" level
		if (movePlotUpdateFlagA==0) 
			movePlotUpdateFlagA = itUnit->iUnitID; //need to update only this one
		else if (movePlotUpdateFlagB==0)
			movePlotUpdateFlagB = itUnit->iUnitID; //need to update this one as well
		else
		{
			//need to update more than 2 units, simply do all
			movePlotUpdateFlagA = -1;
			movePlotUpdateFlagB = -1;
		}
	}

	//forced moves don't even affect the score
	if (bAffectsScore)
	{
		//when in doubt, increasing our visibility is good
		//but only for our first line units ... need to be careful with the others
		STacticalAssignment modifiedAssignment = newAssignment;
		if (newAssignment.iRemainingMoves > 0)
		{
			modifiedAssignment.AddScore(0, visibilityResult.first, 0);
		}

		UpdateScore(modifiedAssignment);

	}

	//are we done or can we do further moves with this unit?
	if (itUnit->iMovesLeft == 0 || bEndOfSim)
	{
		//blocked units might still move away
		if (IsCombatUnit(*itUnit))
		{
			CvTacticalPlot::eTactPlotDomain tactDomain = DomainForUnit(itUnit->pUnit);
			CvTacticalPlot* tactPlot = getTactPlotMutable(itUnit->iPlotIndex);
			tactPlot->SetSimUnitBlocking(tactDomain);
			//if the unit is blocked, we won't assume anything about it ending its turn here, but it is blocking our other moves
			if (newAssignment.eAssignmentType != A_BLOCKED)
			{
				CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(itUnit->iUnitID);
				// Need to check these cases, otherwise the unit can run away!
				if (itUnit->iMovesLeft == 0 || pUnit->isBarbarian() || !pUnit->shouldHeal(false))
					tactPlot->setCombatUnitEndTurn(*this, tactDomain);
			}
		}
		notQuiteFinishedUnits.write().push_back(*itUnit);
		availableUnits_w.erase(itUnit);
	}

	//todo: should we stop the simulation? how to include this in position scoring?
	//don't do restarts if we have a lot of units, the simulation can take very long then
	//also don't do a restart if this was the last unit
	//any new enemies in sight or borders changed?
	bool bCityCapture = (newAssignment.eAssignmentType == A_MELEEKILL && GC.getMap().plotByIndexUnchecked(newAssignment.iToPlotIndex)->isCity());
	bool bRestartRequired = (visibilityResult.second > 0) || bCityCapture;
	if (bRestartRequired && availableUnits_w.size() > 0)
		return RESULT_ADDED_W_VIS_CHANGE;

	return RESULT_ADDED;
}

bool STacticalAssignment::operator==(const STacticalAssignment& rhs) const
{
	return iTotalScore == rhs.iTotalScore &&
		iUnitID == rhs.iUnitID &&
		iFromPlotIndex == rhs.iFromPlotIndex &&
		iToPlotIndex == rhs.iToPlotIndex &&
		iRemainingMoves == rhs.iRemainingMoves &&
		eMoveType == rhs.eMoveType &&
		eAssignmentType == rhs.eAssignmentType;
}

//do not allow one unit to be present multiple times!
bool SComboMove::addMove(const STacticalAssignment& move)
{
	if (!aPtr)
	{
		aPtr = &move;
		aIsLocal = false;
		return true;
	}
	else if (!bPtr && aPtr->iUnitID != move.iUnitID)
	{
		bPtr = &move;
		bIsLocal = false;
		return true;
	}
	else
	{
		return false;
	}
}

//try to detect whether this new position is equivalent to one we already have
static bool tacticalPositionIsEquivalentToAnyChild(const CvTacticalPosition* ref, const CvTacticalPosition* current)
{
	//go depth first
	const vector<CvTacticalPosition*>& children = current->getChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		bool bMatch = tacticalPositionIsEquivalentToAnyChild(ref, children[i]);
		if (bMatch)
			return bMatch;
	}

	return positionIsEquivalent(ref, current);
}

bool CvTacticalPosition::isUnique(int levels) const
{
	//go up x levels
	const CvTacticalPosition* start = this;
	while (start->parentPosition && levels > 0)
	{
		start = start->parentPosition;
		levels--;
	}

	//then recurse downwards to all leaves
	return !tacticalPositionIsEquivalentToAnyChild(this, start);
}

struct TacticalPosition_PairCompareFirst
{
	bool operator() (const std::pair<unsigned short, unsigned char>& l, const std::pair<unsigned short, unsigned char>& r) const { return l.first < r.first; }
};

struct TacticalPosition_EqualRangeComparison
{
	bool operator() (const pair<unsigned short, unsigned char>& a, unsigned short b) const { return a.first < b; }
	bool operator() (unsigned short a, const pair<unsigned short, unsigned char>& b) const { return a < b.first; }
};

CvTacticalPlot* CvTacticalPosition::findTactPlotMutable(int iPlotIndex)
{
	if (iPlotIndex >= 0 && iPlotIndex < USHRT_MAX)
	{
		const TactPlotIndexByPlotIndex& tactPlotLookup_r = tactPlotLookup.read();
		TactPlotIndexByPlotIndex::const_iterator it =
			lower_bound(tactPlotLookup_r.begin(), tactPlotLookup_r.end(),
				(unsigned short)iPlotIndex, TacticalPosition_EqualRangeComparison());
		if (it != tactPlotLookup_r.end() && it->first == (unsigned short)iPlotIndex)
			return &tactPlots.write()[it->second];
	}
	return NULL;
}

const CvTacticalPlot* CvTacticalPosition::findTactPlot(int iPlotIndex) const
{
	if (iPlotIndex >= 0 && iPlotIndex < USHRT_MAX)
	{
		const TactPlotIndexByPlotIndex& tactPlotLookup_r = tactPlotLookup.read();
		TactPlotIndexByPlotIndex::const_iterator it =
			lower_bound(tactPlotLookup_r.begin(), tactPlotLookup_r.end(),
				(unsigned short)iPlotIndex, TacticalPosition_EqualRangeComparison());
		if (it != tactPlotLookup_r.end() && it->first == (unsigned short)iPlotIndex)
			return &tactPlots.read()[it->second];
	}
	return NULL;
}

bool CvTacticalPosition::addTacticalPlot(const CvPlot* pPlot, const vector<const CvUnit*>& allOurUnits)
{
	//don't check the official visibility here, we might want to create a tactplot that only became visible during simulation
	if (!pPlot)
		return false;

	//already added?
	if (getTactPlot(pPlot->GetPlotIndex()))
		return false; 

	//cannot process more than this
	if (tactPlots.read().size() == 255)
		return false;

	CvTacticalPlot newPlot(pPlot, ePlayer, allOurUnits);
	if (newPlot.getPlot() != NULL)
	{
		TactPlotIndexByPlotIndex& tactPlotLookup_w = tactPlotLookup.write();
		vector<CvTacticalPlot>& tactPlots_w = tactPlots.write();
		//cast to smaller types to save memmory ...
		TactPlotIndexByPlotIndex::value_type newEntry( (TactPlotIndexByPlotIndex::value_type::first_type)pPlot->GetPlotIndex(), (TactPlotIndexByPlotIndex::value_type::second_type)tactPlots_w.size());
		tactPlotLookup_w.insert(upper_bound(tactPlotLookup_w.begin(), tactPlotLookup_w.end(), newEntry, TacticalPosition_PairCompareFirst()), newEntry);
		tactPlots_w.push_back(newPlot);

#if defined(MOD_CORE_DEBUGGING)
		if (GC.getLogging() && GC.getAILogging() && MOD_CORE_DEBUGGING && iID==1) //log only initial position
		{
			CvString strMsg;
			strMsg.Format("added sim plot (%d:%d), %s, %s",
				pPlot->getX(), pPlot->getY(),
				newPlot.isEnemy() ? "enemy" : (newPlot.isBlockedByNonSimUnit(CvTacticalPlot::TD_BOTH) ? "blocked" : "available"),
				newPlot.isVisibleToEnemy() ? "enemy_can_see" : "enemy_cannot_see"
			);
			GET_PLAYER(ePlayer).GetTacticalAI()->LogTacticalMessage(strMsg);
		}
#endif

		return true;
	}

	return false;
}

bool CvTacticalPosition::addAvailableUnit(const CvUnit* pUnit)
{
	if (!pUnit || !pUnit->canMove() || !pUnit->canEndTurnAtPlot(pUnit->plot()))
		return false;

	eUnitMovementStrategy eStrategy = MS_NONE;

	//ok this is a bit involved
	//case a) we want to fight (enemies around). units should stay in their native domain so they can fight.
	//case b) we don't want to fight (no enemies). units may embark if the target is not their native domain
	//later in updateMoveAndAttackPlotsForUnits we try and filter the reachable plots according to unit strategy
	//also, only land units can embark and but melee ships can move into certain land plots (cities) so it's tricky

	//if were not looking to fight but about to embark then keep the unit away from enemies
	if (!haveEnemies() && !pUnit->isNativeDomain(pTargetPlot) && pUnit->CanEverEmbark() && pUnit->IsCombatUnit())
	{
		eStrategy = MS_EMBARKED;
	}
	else
	{
		//normal combat units
		switch (pUnit->getUnitInfo().GetDefaultUnitAIType())
		{
			//front line units
		case UNITAI_ATTACK:
		case UNITAI_DEFENSE:
		case UNITAI_COUNTER:
		case UNITAI_PARADROP:
		case UNITAI_ATTACK_SEA:
		case UNITAI_RESERVE_SEA:
		case UNITAI_ESCORT_SEA:
		case UNITAI_FAST_ATTACK:
			//ranged units
		case UNITAI_RANGED:
		case UNITAI_CITY_BOMBARD:
		case UNITAI_ASSAULT_SEA:
		case UNITAI_SKIRMISHER:
		case UNITAI_SUBMARINE:
			if (pUnit->GetRange() > 2 || (MOD_AI_UNIT_PRODUCTION && pUnit->canIntercept())) // MOD_AI_UNIT_PRODUCTION : AA to back
				eStrategy = MS_THIRDLINE;
			else if (pUnit->GetRange() == 2)
				eStrategy = MS_SECONDLINE;
			else
			{
				//the unit AI type is unreliable, so we do this manually
				if (pUnit->IsCanAttackRanged() && pUnit->getDomainType() == DOMAIN_SEA && pUnit->maxMoves() > 3 && pUnit->canMoveAfterAttacking())
					eStrategy = MS_THIRDLINE; //very fast units can stay even further back
				else if (pUnit->IsCanAttackRanged() && pUnit->canMoveAfterAttacking() && pUnit->maxMoves() > 2)
					eStrategy = MS_SECONDLINE; //skirmishers are second line always
				else
					eStrategy = MS_FIRSTLINE; //regular melee and slingers
			}
			break;
		//carriers should stay back
		case UNITAI_CARRIER_SEA:
			eStrategy = MS_THIRDLINE;
			break;

		//explorers are an edge case, the visibility can be useful, so include them
		//they shouldn't fight if the odds are bad
		case UNITAI_EXPLORE:
		case UNITAI_EXPLORE_SEA:
			eStrategy = MS_THIRDLINE;
			break;

		//combat support, stay out of danger
		case UNITAI_GENERAL:
			bHasGeneral = true;
			eStrategy = MS_SUPPORT;
			break;
		case UNITAI_ADMIRAL:
			bHasAdmiral = true;
			eStrategy = MS_SUPPORT;
			break;
		case UNITAI_CITY_SPECIAL:
			bHasSiegetower = true;
			eStrategy = MS_SUPPORT;
			break;

		//air units. ignore here, attack / rebase is handled elsewhere
		case UNITAI_ATTACK_AIR:
		case UNITAI_DEFENSE_AIR:
		case UNITAI_MISSILE_AIR:
		case UNITAI_ICBM:
		default:
			//skip the unit (other civilians as well)
			return false;
		}
	}

	// We want to set the properties above, but not actually add them to the combat sim
	if (pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral() || pUnit->IsSapper())
		return false;

	//careful with damaged units
	if (pUnit->isProjectedToDieNextTurn())
	{
		//do not use for purely defensive moves
		if (!haveEnemies())
			return false;
		//pull back our melee units if they have soaked enough damage, but maybe we can still score a kill!
		if (eStrategy == MS_FIRSTLINE)
			eStrategy = MS_THIRDLINE;
	}

	//we will update the importance later, use 0 for now
	availableUnits.write().push_back(SUnitStats(pUnit, 0, eStrategy));

	//lazy update of move plots later
	movePlotUpdateFlagA = -1; 
	movePlotUpdateFlagB = -1;

#if defined(MOD_CORE_DEBUGGING)
	if (GC.getLogging() && GC.getAILogging() && MOD_CORE_DEBUGGING)
	{
		CvString strMsg;
		strMsg.Format("added sim unit %s, id %d, at (%d:%d), moves %d, damage %d, pathfinder count %d",
			pUnit->getName().c_str(),
			pUnit->GetID(),
			pUnit->getX(),
			pUnit->getY(),
			pUnit->getMoves(),
			pUnit->getDamage(),
			GC.GetPathFinder().GetCurrentGenerationID()
		);
		GET_PLAYER(ePlayer).GetTacticalAI()->LogTacticalMessage(strMsg);
	}
#endif

	return true;
}

static bool IsAttackMove(eUnitAssignmentType eAssignmentType)
{
	return eAssignmentType == A_MELEEATTACK || eAssignmentType == A_MELEEKILL || eAssignmentType == A_MELEEKILL_NO_ADVANCE
		|| eAssignmentType == A_RANGEATTACK || eAssignmentType == A_RANGEKILL;
}

static STacticalAssignment* ScorePlotForSupportMove(const SUnitStats& unit, const CvPlot* pPlot, int iAssumedMovesLeft, const CvSupportPosition& assumedPosition, eUnitMoveEvalMode evalMode, bool bLastPosition)
{
	STacticalAssignment* result = gAssignmentStorage.peekNext();
	result->init(unit.iPlotIndex, pPlot->GetPlotIndex(), unit.iUnitID, iAssumedMovesLeft, unit.eMoveStrategy, A_MOVE, GetPrevPlotScore(unit.iUnitID, assumedPosition));

	int iPlotScore = 0;
	int iBonusScore = 0;
	int iDangerScore = 0;

	const STacticalAssignment& lastTacticalAssignment = assumedPosition.GetTacticalPosition()->getAssignments().back();

	int iLastAttackFromPlotIndex = lastTacticalAssignment.iFromPlotIndex;
	int iLastAttackToPlotIndex = lastTacticalAssignment.iToPlotIndex;

	bool bAttackMove = !bLastPosition || IsAttackMove(lastTacticalAssignment.eAssignmentType);

	CvPlayer& kPlayer = GET_PLAYER(assumedPosition.getPlayer());

	CvUnit* pUnit = kPlayer.getUnit(unit.iUnitID);
	int iEffectRange = pUnit->GetAuraRangeChange() + /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
	CvUnit* pAttackingUnit = kPlayer.getUnit(lastTacticalAssignment.iUnitID);
	DomainTypes eAttackerDomain = pAttackingUnit->getDomainType();

	const CvUnit* pDefender = NULL;
	int iDefenderDamage = 0;
	int iDanger = bLastPosition ? assumedPosition.GetUnitDanger(unit, pPlot, pDefender, iDefenderDamage) : 0;
	bool bIsSafe = true;

	if (pDefender)
	{
		int iProjectedDamage = iDanger + pDefender->getDamage() + iDefenderDamage;
		bIsSafe = (iProjectedDamage * 3 <= pDefender->GetMaxHitPoints() * 2);
	}
	else
	{
		bIsSafe = (iDanger == 0);
	}

	if ((iAssumedMovesLeft == 0 || bLastPosition) && !bIsSafe)
		return result;

	DomainTypes eDomain = pUnit->getDomainType();
	bool bInNativeDomain = (eDomain == DOMAIN_LAND && pPlot->getDomain() == DOMAIN_LAND) || eDomain == DOMAIN_SEA;

	if (evalMode == EM_INTERMEDIATE)
	{
		if (unit.iPlotIndex == pPlot->GetPlotIndex())
		{
			if (bLastPosition)
				result->eAssignmentType = A_FINISH_TEMP;
			else
				result->eAssignmentType = A_WAIT;
		}
		else
		{
			if (unit.eLastAssignment == A_MOVE)
				return result;
		}

		if (bAttackMove)
		{
			if (pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral())
			{
				bool bCorrectDomain = bInNativeDomain &&
					((eDomain != DOMAIN_SEA && eAttackerDomain != DOMAIN_SEA)
					|| (eDomain == DOMAIN_SEA && eAttackerDomain == DOMAIN_SEA));
				// if the attack is already receiving a bonus, do nothing (including if it's us giving the bonus)
				bool bWillGiveBonus = bCorrectDomain && !assumedPosition.HasCombatBonus(iLastAttackFromPlotIndex, eDomain) && iEffectRange >= plotDistance(pPlot->GetPlotIndex(), iLastAttackFromPlotIndex);

				if (bWillGiveBonus)
					iBonusScore += (kPlayer.GetGreatGeneralCombatBonus() + kPlayer.GetPlayerTraits()->GetGreatGeneralExtraBonus() + pUnit->GetAuraEffectChange());
				else if (!bLastPosition)
					return result;
			}
			else if (pUnit->IsSapper())
			{
				int iBonusDelta = 0;
				if (bInNativeDomain && GC.getMap().plotByIndexUnchecked(iLastAttackToPlotIndex)->isCity())
				{
					int iOldCityAttackBonus = assumedPosition.GetCityAttackBonus(iLastAttackToPlotIndex);

					int iPlotDistance = plotDistance(pPlot->GetPlotIndex(), iLastAttackToPlotIndex);

					int iNewCityAttackBonus = 0;
					if (iPlotDistance <= iEffectRange)
						iNewCityAttackBonus = iEffectRange == iPlotDistance ? 1 : 2;

					iBonusDelta = iNewCityAttackBonus - iOldCityAttackBonus;
				}

				if (iBonusDelta > 0)
					iBonusScore += (/*50 in CP, 40 in VP*/ GD_INT_GET(SAPPED_CITY_ATTACK_MODIFIER) * iBonusDelta) / 2;
				else if (!bLastPosition)
					return result;
			}
		}

		// For the final position, do some extra work to figure out if we're well positioned for the end of the turn
		if (bLastPosition)
		{
			const vector<SUnitStats>& finalCombatPositionUnits = assumedPosition.GetFinalCombatPositions();
			const CvPlot* pLoopPlot;
			const CvUnit* pLoopUnit;

			if (bInNativeDomain)
			{
				if (pUnit->IsGreatGeneral() || pUnit->IsGreatAdmiral())
				{
					for (vector<SUnitStats>::const_iterator it = finalCombatPositionUnits.begin(); it != finalCombatPositionUnits.end(); ++it)
					{
						pLoopUnit = kPlayer.getUnit(it->iUnitID);

						if (pUnit->IsGreatGeneral() && (pLoopUnit->getDomainType() == DOMAIN_SEA || pLoopUnit->isEmbarked()))
							continue;

						if (pUnit->IsGreatAdmiral() && pLoopUnit->getDomainType() != DOMAIN_SEA)
							continue;

						pLoopPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
						int iPlotDistance = plotDistance(*pPlot, *pLoopPlot);

						if (iPlotDistance <= iEffectRange)
						{
							iPlotScore += kPlayer.GetGreatGeneralCombatBonus() + kPlayer.GetPlayerTraits()->GetGreatGeneralExtraBonus() + pUnit->GetAuraEffectChange();
							if (pLoopUnit->IsRequiresLeadership())
								iPlotScore += 50;
						}
					}
				}

				int iSameTileHeal = pUnit->getSameTileHeal();
				int iAdjacentTileHeal = pUnit->getAdjacentTileHeal();

				if (iSameTileHeal > 0 || iAdjacentTileHeal > 0)
				{
					for (vector<SUnitStats>::const_iterator it = finalCombatPositionUnits.begin(); it != finalCombatPositionUnits.end(); ++it)
					{
						pLoopUnit = kPlayer.getUnit(it->iUnitID);
						pLoopPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

						int iPlotDistance = plotDistance(*pPlot, *pLoopPlot);
						int iBonusHeal = iPlotDistance == 0 ? iSameTileHeal : (iPlotDistance == 1 ? iAdjacentTileHeal : 0);

						if (iBonusHeal == 0)
							continue;

						int iDamage = pLoopUnit->getDamage() + it->iSelfDamage;
						bool bHealing = it->iMovesLeft == pLoopUnit->maxMoves() || pLoopUnit->isAlwaysHeal();

						if (!bHealing || iDamage <= 5)
							continue;

						TeamTypes ePlotTeam = pLoopPlot->getTeam();
						if (ePlotTeam == kPlayer.getTeam())
							iBonusHeal += pUnit->getExtraFriendlyHeal();
						else if (kPlayer.IsAtWarWith(pLoopPlot->getOwner()))
							iBonusHeal += pUnit->getExtraEnemyHeal();
						else
							iBonusHeal += pUnit->getExtraNeutralHeal();

						// TODO real healing numbers?
						iPlotScore += min(iBonusHeal, iDamage - 5) * 4;
					}
				}
			}

			const CvTacticalPlot* finalTactPlot = assumedPosition.GetFinalTacticalPosition()->getTactPlot(pPlot->GetPlotIndex());

			if (!pPlot->isCity())
			{
				if (!pDefender)
				{
					iDangerScore -= min(max(iDanger, pUnit->GetMaxHitPoints() / 3), pUnit->GetMaxHitPoints());
				}
				else
				{
					int iDefenderDamageTotal = iDanger + iDefenderDamage + pDefender->getDamage();
					if (iDanger == 0)
						iDefenderDamageTotal /= 3;
					iDangerScore -= iDefenderDamageTotal * pUnit->GetMaxHitPoints() / (pDefender->GetMaxHitPoints() * 3);
				}
			}

			if (finalTactPlot)
			{
				switch (finalTactPlot->getEnemyDistance())
				{
				case 0:
					return result; //don't ever go there, wouldn't work anyway
					break;
				case 1:
					iPlotScore += pPlot->isCity() ? 1 : 0; //dangerous to end the turn, avoid
					break;
				case 2:
					iPlotScore += 1; //good for defense support, good for attack support, but risky
					break;
				case 3:
					iPlotScore += 1; //good for defense support, not so good for attack support
					break;
				default:
					break; //usual case for gathering moves, otherwise not really interesting
				}
			}
		}
	}
	else if (evalMode == EM_FINAL)
	{
		if (bIsSafe)
			result->SetScore(0, 0, 0);

		return result;
	}

	//small bias for staying close to our cities, to have a way to retreat if necessary
	int iCityDistanceScore = 10 - GET_PLAYER(assumedPosition.getPlayer()).GetCityDistanceInPlots(pPlot);
	int iExtra = max(iCityDistanceScore, 0);

	// Stay close to the center of the army
	iExtra -= plotDistance(*assumedPosition.GetCenterOfMass(), *pPlot);

	iExtra += result->iRemainingMoves / GD_INT_GET(MOVE_DENOMINATOR);

	result->SetScore(iPlotScore * 10 + iDangerScore + iExtra, iBonusScore, 0);

	return result;
}

bool CvSupportPosition::HasCombatBonus(int iPlotIndex, DomainTypes eDomain) const
{
	const CvUnit* pUnit;

	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	for (vector<SUnitStats>::const_iterator it = availableUnits_r.begin(); it != availableUnits_r.end(); ++it)
	{
		pUnit = it->pUnit;
		bool bProvidesBuff = (pUnit->IsGreatGeneral() && eDomain != DOMAIN_SEA) || (pUnit->IsGreatAdmiral() && eDomain == DOMAIN_SEA);

		if (!bProvidesBuff)
			continue;

		int iEffectRange = pUnit->GetAuraRangeChange() + /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
		if (iEffectRange < plotDistance(iPlotIndex, it->iPlotIndex))
			continue;

		return true;
	}

	const vector<SUnitStats>& notQuiteFinishedUnits_r = notQuiteFinishedUnits.read();
	for (vector<SUnitStats>::const_iterator it = notQuiteFinishedUnits_r.begin(); it != notQuiteFinishedUnits_r.end(); ++it)
	{
		pUnit = it->pUnit;
		bool bProvidesBuff = (pUnit->IsGreatGeneral() && eDomain != DOMAIN_SEA) || (pUnit->IsGreatAdmiral() && eDomain == DOMAIN_SEA);

		if (!bProvidesBuff)
			continue;

		int iEffectRange = pUnit->GetAuraRangeChange() + /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
		if (iEffectRange < plotDistance(iPlotIndex, it->iPlotIndex))
			continue;

		return true;
	}

	return false;
}

int CvSupportPosition::GetCityAttackBonus(int iPlotIndex) const
{
	const CvUnit* pUnit;

	int iMaxBuff = 0;

	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	const vector<SUnitStats>& notQuiteFinishedUnits_r = notQuiteFinishedUnits.read();

	for (vector<SUnitStats>::const_iterator it = availableUnits_r.begin(); it != availableUnits_r.end(); ++it)
	{
		pUnit = it->pUnit;

		if (!pUnit->IsSapper())
			continue;

		int iEffectRange = pUnit->GetAuraRangeChange() + /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
		int iPlotDist = plotDistance(iPlotIndex, it->iPlotIndex);

		if (iEffectRange < iPlotDist)
			continue;

		iMaxBuff = max(iMaxBuff, iEffectRange == iPlotDist ? 1 : 2);
	}

	for (vector<SUnitStats>::const_iterator it = notQuiteFinishedUnits_r.begin(); it != notQuiteFinishedUnits_r.end(); ++it)
	{
		pUnit = it->pUnit;

		if (!pUnit->IsSapper())
			continue;

		int iEffectRange = pUnit->GetAuraRangeChange() + /*2*/ GD_INT_GET(GREAT_GENERAL_RANGE);
		int iPlotDist = plotDistance(iPlotIndex, it->iPlotIndex);

		if (iEffectRange < iPlotDist)
			continue;

		iMaxBuff = max(iMaxBuff, iEffectRange == iPlotDist ? 1 : 2);
	}

	return iMaxBuff;
}

int CvSupportPosition::GetUnitDanger(const SUnitStats& unit, const CvPlot* pPlot, const CvUnit*& pDefender, int& iDefenderDamage) const
{
	if (!pPlot)
		return INT_MAX;

	if (pPlot->isCity())
	{
		return pPlot->getPlotCity()->isInDangerOfFalling() ? INT_MAX : 0;
	}

	CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(unit.iUnitID);
	if (!pUnit)
		return INT_MAX;

	const CvTacticalPlot* pTactPlot = finalTacticalPosition->getTactPlot(pPlot->GetPlotIndex());

	if (pTactPlot && pTactPlot->isCombatEndTurn())
	{
		const vector<STacticalUnit>& unitsAtPlot = pTactPlot->getUnitsAtPlot();
		if (!unitsAtPlot.empty())
		{
			for (vector<STacticalUnit>::const_iterator it = unitsAtPlot.begin(); it != unitsAtPlot.end(); ++it)
			{
				int iCoveringUnitId = it->iUnitID;

				const SUnitStats* pCoveringUnitStats = finalTacticalPosition->GetUnitStats(iCoveringUnitId);
				if (!pCoveringUnitStats || !IsCombatUnit(*pCoveringUnitStats))
					continue;

				pDefender = GET_PLAYER(ePlayer).getUnit(iCoveringUnitId);
				iDefenderDamage = pCoveringUnitStats->iSelfDamage;
			}
		}
	}
	if (!pDefender)
	{
		pDefender = pPlot->getBestDefender(ePlayer);
		if (pDefender && !pDefender->TurnProcessed() && pDefender->getMoves() > 0)
			pDefender = NULL;
	}

	if (pDefender)
		return GetUnitDangerForPlot(pDefender, pPlot, iDefenderDamage, *finalTacticalPosition);
	else
		return GetUnitDangerForPlot(pUnit, pPlot, 0, *finalTacticalPosition);
}

void CvSupportPosition::getPreferredAssignmentsForUnit(const SUnitStats& unit, int nMaxCount, bool bLastPosition) const
{
	gPossibleMoves.clear();

	const CvPlot* pAssumedUnitPlot = GC.getMap().plotByIndexUnchecked(unit.iPlotIndex);
	CvUnit* pUnit = GET_PLAYER(getPlayer()).getUnit(unit.iUnitID);
	if (!pUnit || !pAssumedUnitPlot)
		return;

	int iOldPlotDistanceToTarget = GetFinalTacticalPosition()->IsTargetToDistanceRelevant() ? TacticalAIHelpers::GetPlotDistanceToTarget(unit.iPlotIndex, pUnit->getDomainType()) : 0;

	//check moves and melee attacks first
	const ReachablePlots& reachablePlots = getReachablePlotsForUnit(unit);
	for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
	{
		//the plot we're checking right now
		const CvPlot* pTestPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		if (!pTestPlot)
			continue;

		int iMoveTowardsTargetScore = 0;

		if (GetFinalTacticalPosition()->IsTargetToDistanceRelevant())
		{
			// Try to move towards the target
			int iNewPlotDistanceToTarget = TacticalAIHelpers::GetPlotDistanceToTarget(it->iPlotIndex, pUnit->getDomainType());
			iMoveTowardsTargetScore = iOldPlotDistanceToTarget - iNewPlotDistanceToTarget;
		}

		STacticalAssignment* moveToPlot = ScorePlotForSupportMove(unit, pTestPlot, it->iMovesLeft, *this, EM_INTERMEDIATE, bLastPosition);

		if (moveToPlot->IsAcceptable())
		{
			moveToPlot->AddScore(iMoveTowardsTargetScore, 0, 0);
			gPossibleMoves.push_back(OptionWithScore<STacticalAssignment*>(moveToPlot, moveToPlot->Score()));
			gAssignmentStorage.consumeOne();
		}
	}

	//need to return in sorted order. note that we don't filter out bad (negative moves) they just are unlikely to get picked
	std::stable_sort(gPossibleMoves.begin(), gPossibleMoves.end());

	//don't return more than requested unless there is a tie
	if (gPossibleMoves.size() > (size_t)nMaxCount)
	{
		while (gPossibleMoves[nMaxCount].score == gPossibleMoves[nMaxCount - 1].score && (size_t)nMaxCount < gPossibleMoves.size())
			nMaxCount++;

		gPossibleMoves.erase(gPossibleMoves.begin() + nMaxCount, gPossibleMoves.end());
	}
}

bool CvSupportPosition::addInitialAssignments()
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
	{
		CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(itUnit->iPlotIndex);
		// Check if there's a valid move
		STacticalAssignment eInitialAssignmentNoMoves = *ScorePlotForSupportMove(*itUnit, pPlot, 0, *this, EM_INITIAL, false);
		eInitialAssignmentNoMoves.iRemainingMoves = itUnit->iMovesLeft;
		eInitialAssignmentNoMoves.eAssignmentType = A_INITIAL;
		addAssignment(eInitialAssignmentNoMoves);
	}
	return true;
}

bool CvSupportPosition::makeNextAssignments(int iMaxBranches, int iMaxChoicesPerUnit, CvSupportPosStorage& storage,
	vector<CvSupportPosition*>& openPositionsHeap, vector<CvSupportPosition*>& completedPositions, const PrPositionSortHeapGeneration& heapSort,
	const map<const CvTacticalPosition*, const CvTacticalPosition*> nextAttackPosition)
{
	/*
	abstract:
	get preferred plots for all combat units
	choose M best overall moves (combine as far as possible)
	create child positions
		assign moves
		update affected tact plots
		update unit reachable plots
	*/

	//very important, lazy update
	updateMovePlotsIfRequired();

	// Is this the final tactical position, if so, just try to find the best plot to go to to prepare for the next turn
	map<const CvTacticalPosition*, const CvTacticalPosition*>::const_iterator nextPosIt = nextAttackPosition.find(tacticalPosition);
	bool bLastPosition = nextPosIt->second == NULL;

	gOverAllChoices.clear();
	gAssignmentStorage.reset(false);
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	for (size_t i = 0; i < availableUnits_r.size(); i++)
	{
		getPreferredAssignmentsForUnit(availableUnits_r[i], iMaxChoicesPerUnit, bLastPosition);
		gOverAllChoices.insert(gOverAllChoices.end(), gPossibleMoves.begin(), gPossibleMoves.end());
	}

	//important that moves are ordered by quality instead of unit id
	std::stable_sort(gOverAllChoices.begin(), gOverAllChoices.end());

	for (size_t i = 0; i < gOverAllChoices.size(); i++)
	{
		//we need memory for the new child but we'll commit it only later after the uniqueness check
		CvSupportPosition* pNewChild = storage.peekNext();
		if (!pNewChild)
			break;

		//important, hook it up to the parent so we can access the history
		addChild(pNewChild);
		gCheckedPositions++;

		pNewChild->initFromParent(*this);

		AddAssignmentResult assignmentResult = pNewChild->addAssignment(*gOverAllChoices[i].option);

		if (gOverAllChoices[i].option->eAssignmentType == A_WAIT)
			pNewChild->UpdateTacticalPosition(*nextPosIt->second);

		//cannot add a RESTART in the middle of a combo move for consistency, so add afterwards
		if (assignmentResult == RESULT_ADDED_W_VIS_CHANGE)
		{
			STacticalAssignment restart;
			restart.init(-1, -1, gOverAllChoices[i].option->iUnitID, 0, gOverAllChoices[i].option->eMoveType, A_RESTART, GetPrevPlotScore(gOverAllChoices[i].option->iUnitID, *this));
			restart.SetScore(0, 0, 0);
			pNewChild->assignedMoves.write().push_back(restart);
		}

		//try to detect duplicates ...
		bool isConsistent = assignmentResult != RESULT_NOT_ADDED;
		if (isConsistent && pNewChild->isUnique(TACTSIM_UNIQUENESS_CHECK_GENERATIONS))
		{
			//do we need to keep working on this one?
			if (pNewChild->isExhausted())
			{
				if (pNewChild->addFinishMovesIfAcceptable())
				{
					//good case, we're done
					completedPositions.push_back(pNewChild);
					giValidEndPos++;
					storage.consumeOne();
				}
				else
				{
					//position is illegal, do not remember it so we can re-use the memory
					giInvalidEndPos++;
					removeChild(pNewChild);
				}
			}
			else
			{
				//also good case, keep this for the next round
				openPositionsHeap.push_back(pNewChild);
				push_heap(openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
				storage.consumeOne();
			}
		}
		else
			removeChild(pNewChild);

		if (childPositions.size() >= (size_t)iMaxBranches)
			break;
	}

	//can happen we have no children if all were considered redundant or invalid
	//note that we also considered blocked moves for all children, but those also may turn out to be invalid if the unit doesn't have enough moves to flee 
	return !childPositions.empty();
}

//lazy update of move plots
void CvSupportPosition::updateMovePlotsIfRequired()
{
	const vector<SUnitStats>& availableUnits_r = availableUnits.read();
	if (movePlotUpdateFlagA == -1 && movePlotUpdateFlagB == -1)
	{
		for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
			updateMovePlotsForUnit(*itUnit);
	}
	else
	{
		for (vector<SUnitStats>::const_iterator itUnit = availableUnits_r.begin(); itUnit != availableUnits_r.end(); ++itUnit)
			if (itUnit->iUnitID == movePlotUpdateFlagA || itUnit->iUnitID == movePlotUpdateFlagB)
				updateMovePlotsForUnit(*itUnit);
	}

	movePlotUpdateFlagA = 0;
	movePlotUpdateFlagB = 0;
}

//see if all the plots where our units would end their turn are acceptable
//this is a deferred check because in the beginning it's not clear how many enemy units we can eliminate
bool CvSupportPosition::addFinishMovesIfAcceptable()
{
	const vector<SUnitStats>& notQuiteFinishedUnits_r = notQuiteFinishedUnits.read();
	//only units which have exhausted their moves are in this array! if the sim was aborted, somebody else will hopefully pick up the pieces
	for (size_t i = 0; i < notQuiteFinishedUnits_r.size(); i++)
	{
		const SUnitStats& unit = notQuiteFinishedUnits_r[i];
		const STacticalAssignment* pInitial = getInitialAssignment(unit.iUnitID);
		if (!pInitial)
			return false; //something wrong

		//if the unit is blocked but has movement left and can flee, let's assume that is ok
		if (unit.eLastAssignment == A_BLOCKED && unit.iMovesLeft > 0)
			continue;

		//make sure we don't leave a unit in an impossible position
		const CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(unit.iPlotIndex);
		STacticalAssignment* nextAssignment = ScorePlotForSupportMove(unit, pPlot, 0, *this, EM_FINAL, true);

		if (nextAssignment->IsAcceptable())
		{
			//if the score is acceptable, end their turn. unless the unit is blocked, then we may use them for other tasks
			if (unit.eLastAssignment != A_BLOCKED)
			{
				nextAssignment->iRemainingMoves = unit.iMovesLeft;
				nextAssignment->eAssignmentType = A_FINISH;
				assignedMoves.write().push_back(*nextAssignment);
			}
		}
		else
		{
			return false;
		}
	}

	vector<SUnitStats>& finishedUnits_w = finishedUnits.write();
	finishedUnits_w.insert(finishedUnits_w.end(), notQuiteFinishedUnits_r.begin(), notQuiteFinishedUnits_r.end());
	notQuiteFinishedUnits.write().clear();

	return true;
}

//need a default constructor for stl containers ...
CvSupportPosition::CvSupportPosition()
{
	ePlayer = NO_PLAYER;
	nFirstInterestingAssignment = 0;
	iBonusScore = 0;
	iDamageDelta = 0;
	iScoreOverParent = 0;
	parentPosition = NULL;
	tacticalPosition = NULL;
	finalTacticalPosition = NULL;
	iGeneration = 0;
	iID = 1; //zero doesn't work here
	movePlotUpdateFlagA = 0;
	movePlotUpdateFlagB = 0;

	iLastFromAttackPlotIndex = -1;
	iLastToAttackPlotIndex = -1;

	childPositions.clear();
	assignedMoves.clear();
	availableUnits.clear();
	freedPlots.clear();
	availableUnits.clear();
	notQuiteFinishedUnits.clear();
	finishedUnits.clear();
	finalCombatPositionUnits.clear();
	plotScores.clear();

	pCenterOfMass = NULL;
}

static const CvPlot* CalculateCenterOfMass(const vector<SUnitStats>& positions)
{
	if (positions.empty())
		return NULL;

	int iTotalX = 0;
	int iTotalY = 0;

	int iTotalX2 = 0;
	int iTotalY2 = 0;
	int iWorldWidth = GC.getMap().getGridWidth();
	int iWorldHeight = GC.getMap().getGridHeight();

	//the first unit is our reference ...

	CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(positions.front().iPlotIndex);

	int iRefX = pPlot->getX();
	int iRefY = pPlot->getY();

	for (vector<SUnitStats>::const_iterator it = positions.begin() + 1; it != positions.end(); ++it)
	{
		pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);
		int iDX = pPlot->getX() - iRefX;
		int iDY = pPlot->getY() - iRefY;

		if (GC.getMap().isWrapX())
		{
			if (iDX > +(iWorldWidth / 2))
				iDX -= iWorldWidth;
			if (iDX < -(iWorldWidth / 2))
				iDX += iWorldWidth;
		}
		if (GC.getMap().isWrapY())
		{
			if (iDY > +(iWorldHeight / 2))
				iDY -= iWorldHeight;
			if (iDY < -(iWorldHeight / 2))
				iDY += iWorldHeight;
		}

		iTotalX += iDX;
		iTotalY += iDY;
		iTotalX2 += iDX * iDX;
		iTotalY2 += iDY * iDY;
	}

	//finally, compute average
	float fNUnits = (float)positions.size();
	float fAvgX = (iTotalX / fNUnits) + iRefX;
	float fAvgY = (iTotalY / fNUnits) + iRefY;

	//rounding to nearest integer
	int iAvgX = fAvgX > 0 ? int(fAvgX + 0.5f) : int(fAvgX - 0.5f);
	int iAvgY = fAvgY > 0 ? int(fAvgY + 0.5f) : int(fAvgY - 0.5f);

	//this handles wrapped coordinates
	CvPlot* pCOM = GC.getMap().plot(iAvgX, iAvgY);
	if (!pCOM)
		return NULL;

	return pCOM;
}

void CvSupportPosition::initFromTacticalPosition(const CvTacticalPosition& tactPos, const CvTacticalPosition& finalTactPos, const vector<const CvUnit*>& ourUnits)
{
	ePlayer = tactPos.getPlayer();
	nFirstInterestingAssignment = 0;
	iBonusScore = 0;
	iDamageDelta = 0;
	iScoreOverParent = 0;
	parentPosition = NULL;
	tacticalPosition = &tactPos;
	finalTacticalPosition = &finalTactPos;
	iGeneration = 0;
	iID = 1; //zero doesn't work here
	movePlotUpdateFlagA = -1;
	movePlotUpdateFlagB = -1;
	bHasGeneral = false;
	bHasAdmiral = false;
	bHasSiegetower = false;

	iLastFromAttackPlotIndex = tactPos.getAssignments().back().iFromPlotIndex;
	iLastToAttackPlotIndex = tactPos.getAssignments().back().iToPlotIndex;

	childPositions.clear();
	assignedMoves.clear();
	availableUnits.clear();
	notQuiteFinishedUnits.clear();
	finishedUnits.clear();
	finalCombatPositionUnits.clear();
	plotScores.clear();

	freedPlots.inheritFrom(tactPos.getFreedPlots());

	vector<SUnitStats>& finalCombatPositionUnits_w = finalCombatPositionUnits.write();
	finalCombatPositionUnits_w.clear();

	for (vector<const CvUnit*>::const_iterator it = ourUnits.begin(); it != ourUnits.end(); ++it)
	{
		const CvUnit* pUnit = *it;

		const STacticalAssignment* lastAssignment = finalTacticalPosition->getLatestMoveAssignment(pUnit->GetID());
		if (!AddAvailableUnit(pUnit) && pUnit->IsCombatUnit())
		{
			int iPlotIndex = lastAssignment ? lastAssignment->iToPlotIndex : pUnit->plot()->GetPlotIndex();
			finalCombatPositionUnits_w.push_back(SUnitStats(pUnit, pUnit->GetID(), iPlotIndex, 0, 0, 0, MS_NONE));
		}
	}

	pCenterOfMass = CalculateCenterOfMass(finalCombatPositionUnits_w);
}

void CvSupportPosition::UpdateTacticalPosition(const CvTacticalPosition& tactPos)
{
	tacticalPosition = &tactPos;
	iLastFromAttackPlotIndex = tactPos.getAssignments().back().iFromPlotIndex;
	iLastToAttackPlotIndex = tactPos.getAssignments().back().iToPlotIndex;
}

void CvSupportPosition::initFromParent(const CvSupportPosition& parent)
{
	ePlayer = parent.ePlayer;
	nFirstInterestingAssignment = parent.nFirstInterestingAssignment;
	iBonusScore = parent.iBonusScore;
	iDamageDelta = parent.iDamageDelta;
	iScoreOverParent = 0;
	parentPosition = &parent;
	tacticalPosition = parent.tacticalPosition;
	finalTacticalPosition = parent.finalTacticalPosition;
	movePlotUpdateFlagA = parent.movePlotUpdateFlagA;
	movePlotUpdateFlagB = parent.movePlotUpdateFlagB;
	bHasGeneral = parent.bHasGeneral;
	bHasAdmiral = parent.bHasAdmiral;
	bHasSiegetower = parent.bHasSiegetower;
	iLastFromAttackPlotIndex = parent.iLastFromAttackPlotIndex;
	iLastToAttackPlotIndex = parent.iLastToAttackPlotIndex;
	iGeneration = parent.iGeneration + 1;
	pCenterOfMass = parent.pCenterOfMass;

	//clever scheme to encode the tree structure into IDs
	//works only if the tree is not too wide or too deep
	if (parent.getID() < ULLONG_MAX / 10 - 10)
		iID = parent.getID() * 10 + parent.childPositions.size();
	else
		iID = ULLONG_MAX;

	//childPositions stays empty!
	childPositions.clear();

	//copied from parent, modified when addAssignment is called
	assignedMoves.inheritFrom(parent.assignedMoves.read());
	availableUnits.inheritFrom(parent.availableUnits.read());
	notQuiteFinishedUnits.inheritFrom(parent.notQuiteFinishedUnits.read());
	finishedUnits.inheritFrom(parent.finishedUnits.read());
	freedPlots.inheritFrom(parent.freedPlots.read());
	finalCombatPositionUnits.inheritFrom(parent.finalCombatPositionUnits.read());
	plotScores.inheritFrom(parent.plotScores.read());
}

bool CvSupportPosition::removeChild(CvSupportPosition* pChild)
{
	//just unlink the child - do not delete it, the memory is allocated statically
	vector<CvSupportPosition*>::iterator it = find(childPositions.begin(), childPositions.end(), pChild);
	if (it != childPositions.end())
		childPositions.erase(it);

	return false;
}

size_t CvSupportPosition::addChild(CvSupportPosition* pChild)
{
	if (pChild)
	{
		childPositions.push_back(pChild); //this order is better for generating an ID for the child
		pChild->initFromParent(*this);
	}
	return childPositions.size();
}

void CvSupportPosition::updateMovePlotsForUnit(SUnitStats unit)
{
	CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(unit.iUnitID);
	CvPlot* pStartPlot = GC.getMap().plotByIndexUnchecked(unit.iPlotIndex);

	const PlotIndexContainer& freedPlots_r = freedPlots.read();

	TCachedMovePlots::const_iterator itP = gReachablePlotsLookup.find(SPathFinderStartPos(unit, freedPlots_r));
	if (itP != gReachablePlotsLookup.end())
	{
		gMovePlotsCacheHit++;
	}
	else
	{
		gMovePlotsCacheMiss++;

		//note: we allow (intermediate) embarkation here but filter out the non-native plots later (useful for denmark and lategame)
		int iMoveFlags = CvUnit::MOVEFLAG_IGNORE_STACKING_SELF | CvUnit::MOVEFLAG_IGNORE_DANGER;
		ReachablePlots reachablePlots = TacticalAIHelpers::GetAllPlotsInReachThisTurn(pUnit, pStartPlot, iMoveFlags, 0, unit.iMovesLeft, freedPlots_r);

		//try to save some memory here
		ReachablePlots reachablePlotsPruned;
		for (ReachablePlots::const_iterator it = reachablePlots.begin(); it != reachablePlots.end(); ++it)
		{
			CvPlot* pPlot = GC.getMap().plotByIndexUnchecked(it->iPlotIndex);

			//note that if the unit is far away, it won't have any good plots and will be considered blocked
			//this is just a rough check, we check the existance of a corresponding tact plot below
			//+2 is due to the "maneuver space" around each enemy
			if (TacticalAIHelpers::GetPlotDistanceToTarget(it->iPlotIndex, pUnit->getDomainType()) > TACTICAL_COMBAT_MAX_TARGET_DISTANCE + 2)
				continue;

			//this is a performance fix / logic simplification
			//all open positions share just one instance of gSafePlotCount
			//we only update the count once at the start of the sim
			//if there is no safe plot then, there will never be one!
			if (!parentPosition)
			{
				bool bIsSafe = GET_PLAYER(ePlayer).GetPlotDanger(*pPlot, pUnit, SUnitIDValueContainer(), 0) < pUnit->GetCurrHitPoints();
				if (bIsSafe && pUnit->canEndTurnAtPlot(pPlot))
					gSafePlotCount[unit.iUnitID]++;
			}

			//last (expensive) check, need to have a tact plot for each reachable plot
			reachablePlotsPruned.insertNoIndex(*it);
		}

		reachablePlotsPruned.createIndex();
		gReachablePlotsLookup[SPathFinderStartPos(unit, freedPlots_r)] = reachablePlotsPruned;
	}
}

static bool supportPositionIsEquivalentToAnyChild(const CvSupportPosition* ref, const CvSupportPosition* current)
{
	//go depth first
	const vector<CvSupportPosition*>& children = current->getChildren();
	for (size_t i = 0; i < children.size(); i++)
	{
		bool bMatch = supportPositionIsEquivalentToAnyChild(ref, children[i]);
		if (bMatch)
			return bMatch;
	}

	return positionIsEquivalent(ref, current);
}

bool CvSupportPosition::isUnique(int levels) const
{
	//go up x levels
	const CvSupportPosition* start = this;
	while (start->parentPosition && levels > 0)
	{
		start = start->parentPosition;
		levels--;
	}

	//then recurse downwards to all leaves
	return !supportPositionIsEquivalentToAnyChild(this, start);
}

CvSupportPosition::AddAssignmentResult CvSupportPosition::addAssignment(const STacticalAssignment& newAssignment)
{
	vector<SUnitStats>& availableUnits_w = availableUnits.write();
	vector<SUnitStats>::iterator itUnit = find_if(availableUnits_w.begin(), availableUnits_w.end(), PrMatchingUnit(newAssignment.iUnitID));

	if (itUnit == availableUnits_w.end() || itUnit->iPlotIndex != newAssignment.iFromPlotIndex)
		return RESULT_NOT_ADDED;

	//i know what you did last summer!
	itUnit->eLastAssignment = newAssignment.eAssignmentType;

	//store the assignment
	assignedMoves.write().push_back(newAssignment);

	//now deal with the consequences
	bool bAffectsScore = true;
	bool bEndOfSim = false;
	bool bNoMove = false;
	switch (newAssignment.eAssignmentType)
	{
	case A_INITIAL:
		bAffectsScore = false;
		break;
	case A_MOVE:
	{
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		itUnit->iPlotIndex = newAssignment.iToPlotIndex;
		break;
	}
	case A_USE_POWER:
		itUnit->iMovesLeft = newAssignment.iRemainingMoves;
		bEndOfSim = true;
		break;
	case A_FINISH:
		OutputDebugString("this should not happen\n");
	case A_HEAL:
	case A_FINISH_TEMP:
		bEndOfSim = true;
		break;
	case A_BLOCKED:
		bAffectsScore = false;
		bEndOfSim = true;
		break;
	case A_WAIT:
		bNoMove = true;
		bAffectsScore = false;
		break;
	default:
		UNREACHABLE();
	}

	//we update the moveplots lazily because it takes a while and we don't know yet if we will ever follow up on this position
	if (itUnit->iMovesLeft > 0 && !bEndOfSim && !bNoMove)
	{
		//make sure we don't regress to a "lower" level
		if (movePlotUpdateFlagA == 0)
			movePlotUpdateFlagA = itUnit->iUnitID; //need to update only this one
		else if (movePlotUpdateFlagB == 0)
			movePlotUpdateFlagB = itUnit->iUnitID; //need to update this one as well
		else
		{
			//need to update more than 2 units, simply do all
			movePlotUpdateFlagA = -1;
			movePlotUpdateFlagB = -1;
		}
	}

	//forced moves don't even affect the score
	if (bAffectsScore)
	{
		UpdateScore(newAssignment);
	}

	//are we done or can we do further moves with this unit?
	if (itUnit->iMovesLeft == 0 || bEndOfSim)
	{
		notQuiteFinishedUnits.write().push_back(*itUnit);
		availableUnits.write().erase(itUnit);
	}

	return RESULT_ADDED;
}

bool CvSupportPosition::AddAvailableUnit(const CvUnit* pUnit)
{
	if (!pUnit || !pUnit->canMove() || !pUnit->canEndTurnAtPlot(pUnit->plot()))
		return false;

	if (!pUnit->IsGreatGeneral() && !pUnit->IsGreatAdmiral() && !pUnit->IsSapper())
		return false;

	if (pUnit->IsGreatGeneral())
	{
		if (bHasGeneral)
			return false;
		else
			bHasGeneral = true;
	}

	if (pUnit->IsGreatAdmiral())
	{
		if (bHasAdmiral)
			return false;
		else
			bHasAdmiral = true;
	}

	if (pUnit->IsSapper())
	{
		if (bHasSiegetower)
			return false;
		else
			bHasSiegetower = true;
	}

	availableUnits.write().push_back(SUnitStats(pUnit, 0, MS_SUPPORT));

	//lazy update of move plots later
	movePlotUpdateFlagA = -1;
	movePlotUpdateFlagB = -1;

	return true;
}

int CvTacticalPosition::countChildren() const
{
	int iCount = (int)childPositions.size();
	for (size_t i = 0; i < childPositions.size(); i++)
		iCount += childPositions[i]->countChildren();

	return iCount;
}

float CvTacticalPosition::getAggressionBias() const
{
	//avoid extreme ratios, use the sqrt
	float fUnitNumberRatio = sqrtf(nOurOriginalUnits / float(max(1,(int)nOriginalEnemies)));
	return max( 0.9f, fUnitNumberRatio ); //<1 indicates we're fewer but don't stop attacking because of that
}

//this is intended to filter out the no-chance-in-hell moves
//it's not intended to be the final check, the situation can still change as the sim progresses
bool CvTacticalPosition::canProbablyEndTurnAfterAssignment(const SUnitStats& unit, const CvTacticalPlot* assumedUnitPlot, eUnitAssignmentType eAssignmentType) const
{
	const CvUnit* pUnit = unit.pUnit;
	if (!pUnit || !assumedUnitPlot)
		return false;

	if (pUnit->IsCanDefend())
	{
		//if we have nowhere to flee to, we can just as well stay?
		if (gSafePlotCount[unit.iUnitID] == 0)
			return true;

		int iDanger = GetUnitDangerForPlot(pUnit, assumedUnitPlot->getPlot(), unit.iSelfDamage, *this);

		iDanger /= max(1, (int)getAggressionLevel());

		return ScoreCombatUnitTurnEnd(pUnit, eAssignmentType, assumedUnitPlot, iDanger, CvTacticalPlot::TD_BOTH,
			unit.iSelfDamage, *this, EM_FINAL, availableUnits.read().size() > 1, true) != INT_MAX;
	}
	else
		//civilians need cover. full scoring logic in ScorePlotForNonFightingUnitMove is more complex; here we just need a rule of thumb
		return assumedUnitPlot->isCombatEndTurn();
}

std::ostream& operator<<(ostream& os, const CvPlot& p)
{
    os << "(" << p.getX() << "," << p.getY() << ")";
    return os;
}

ostream& operator << (ostream& out, const STacticalAssignment& arg)
{
	const char* eType = assignmentTypeNames[arg.eAssignmentType];
	//CvPlot* pFromPlot = GC.getMap().plotByIndexUnchecked( arg.iFromPlotIndex );
	//CvPlot* pToPlot = GC.getMap().plotByIndexUnchecked( arg.iToPlotIndex );
	out << arg.iUnitID << " " << eType << " from " << arg.iFromPlotIndex << " to " << arg.iToPlotIndex << " (" << arg.Score() << ")";
	return out;
}

void CvTacticalPosition::dumpChildren(ofstream& out) const
{
	out << "n" << (void*)this << " [ label = \"id " << (void*)this << ": score " << getScoreTotal() << ", " << GetNumAvailableUnits() << " units\" ";
	if (isEarlyFinish() || isExhausted())
		out << " shape=box ";
	out << "];\n";

	size_t nAssignments = assignedMoves.read().size();
	for (size_t i = 0; i < childPositions.size(); i++)
	{
		out << "n" << (void*)this << " -> n" << (void*)childPositions[i] << " [ label = \"";

		size_t nAssignmentsChild = childPositions[i]->getAssignments().size();
		for (size_t j = nAssignments; j < nAssignmentsChild; j++)
		{
			const STacticalAssignment& assignment = childPositions[i]->getAssignments()[j];
			CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(assignment.iUnitID);
			out << pUnit->getName().c_str() << " " << assignment << "\\n";
		}
		out << "\" color=blue ];\n";
	}

	for (size_t i = 0; i < childPositions.size(); i++)
		childPositions[i]->dumpChildren(out);
}

void CvTacticalPosition::dumpPlotStatus(const char* fname) const
{
	ofstream out(fname);
	if (out)
	{
		const vector<CvTacticalPlot>& tactPlots_r = tactPlots.read();
		out << "#x,y,terrain,owner,isEnemy,isFriendly,nAdjEnemy,nAdjFriendly,nAdjFirstline,isEdge,iEnemyDist\n"; 
		for (vector<CvTacticalPlot>::const_iterator it = tactPlots_r.begin(); it != tactPlots_r.end(); ++it)
		{
			CvPlot* pPlot =  GC.getMap().plotByIndexUnchecked( it->getPlotIndex() );
			out << pPlot->getX() << "," << pPlot->getY() << "," << pPlot->getTerrainType() << "," << pPlot->getOwner() << "," << (it->isEnemy() ? 1 : 0) << "," << (it->hasFriendlyCombatUnit() ? 1 : 0) << "," 
				<< it->getNumAdjacentEnemies(CvTacticalPlot::TD_BOTH) << "," << it->getNumAdjacentFriendlies(CvTacticalPlot::TD_BOTH,-1) << "," << it->getNumAdjacentFriendliesEndTurn(CvTacticalPlot::TD_BOTH) << "," 
				<< (it->isEdgePlot() ? 1 : 0) << "," << (int)(it->getEnemyDistance()) << "\n";
		}
	}
	out.close();
}

void CvTacticalPosition::exportToDotFile(const char* fname) const
{
	std::ofstream out;
	out.open(fname);
	if (out)
	{
		out << "digraph tacticalmoves {\n";
		dumpChildren(out);
		out << "}\n";
	}
	out.close();
}

//warning: only keep the reference returned around if you know what you are doing!
//it may get invalidated by additional calls to this function!
CvTacticalPlot* CvTacticalPosition::getTactPlotMutable(int plotindex)
{
	return findTactPlotMutable(plotindex);
}

const CvTacticalPlot* CvTacticalPosition::getTactPlot(int plotindex) const
{
	return findTactPlot(plotindex);
}

int CvTacticalPosition::GetUnitDamage(int iUnitID) const
{
	return unitDamageDealt.read().GetValue(iUnitID);
}

int CvTacticalPosition::GetCityDamage(int iCityID) const
{
	return unitDamageDealt.read().GetValue(-iCityID);
}

void CvTacticalPosition::ChangeUnitDamage(int iUnitID, int iChange)
{
	unitDamageDealt.write().ChangeValue(iUnitID, iChange);
}
void CvTacticalPosition::ChangeCityDamage(int iCityID, int iChange)
{
	unitDamageDealt.write().ChangeValue(-iCityID, iChange);
}

void CvTacticalPosition::HealFriendlyUnit(int iUnitID, int iChange)
{
	vector<SUnitStats>& availableUnits_w = availableUnits.write();
	vector<SUnitStats>::iterator it = find_if(availableUnits_w.begin(), availableUnits_w.end(), PrMatchingUnit(iUnitID));
	if (it != availableUnits_w.end())
		it->iSelfDamage -= iChange;
}

bool TacticalAIHelpers::FindAndExecuteBestUnitAssignments(PlayerTypes ePlayer, vector<CvUnit*>& vUnits, CvPlot* pTarget, eAggressionLevel eAggLvl)
{
	int iCount = 0;
	bool bSuccess = false;
	set<int> unuseableUnits;
	vector<CvUnit*> currentUnits = vUnits;
	TacticalAIHelpers::UpdatePlotDistanceToTarget(ePlayer, pTarget);
	do
	{
		iCount++;

		vector<STacticalAssignment> vAssignments = TacticalAIHelpers::FindBestUnitAssignments(currentUnits, pTarget, eAggLvl, unuseableUnits, true);
		if (vAssignments.empty())
		{
			if (unuseableUnits.size()>0 && currentUnits.size()>unuseableUnits.size())
			{
				//drop the offending units and try again
				vector<CvUnit*> remainingUnits;
				for (vector<CvUnit*>::const_iterator it = currentUnits.begin(); it != currentUnits.end(); ++it)
				{
					if (unuseableUnits.find((*it)->GetID())==unuseableUnits.end())
						remainingUnits.push_back(*it);
				}
				currentUnits = remainingUnits;
			}
			else
				break; //give up
		}
		else
			//restarts might happen when new enemies become visible
			bSuccess = TacticalAIHelpers::ExecuteUnitAssignments(vUnits.front()->getOwner(), vAssignments);
	}
	while (!bSuccess && iCount < 4);

	gDistanceToTargetPlots.clear();

	return bSuccess;
}

//make sure our units come in a defined order (important for reproducability, don't want to sort pointers!)
template<typename T>
struct PrSortPairBySecondAsc
{
	bool operator()(const pair<T, T>& lhs, const pair<T, T>& rhs) const { return lhs.second < rhs.second; }
};

//try to find a combination of unit actions (move, attack etc) which does maximal damage to the enemy while exposing us to minimal risk
vector<STacticalAssignment> TacticalAIHelpers::FindBestUnitAssignments(
	const vector<CvUnit*>& vUnits, CvPlot* pTarget, eAggressionLevel eAggLvl, set<int>& unuseableUnits, bool bTargetDistanceRelevant, bool bReturnToStartPositions, int iSaveMovement)
{
	/*
	abstract:

	----
	create tactical plots
	add all units with reachable plots
 
	create open position
	while (pop open positions)
	 if (make next assignments)
	  add children to open positions
	 else if (completed and legal)
	   add to completed positions

	return best completed position
	----

	units are position according to their attack range (melee has range 1 for this purpose)
	distance to enemy is main criterion, danger is secondary
	ranged attack are always possible; melee attacks are accepted or not depending on aggression level
	final confirmation whether an assignement is acceptable happens only at the end

	if aggression level is zero, we do not plan any attacks. only movement.
		if target is friendly, we try to stay within N plots around it with melee units covering ranged units.
		if target is hostile, we try to come close but no closer than N plots with melee units covering ranged units.
	*/

	vector<STacticalAssignment> result;
	if (vUnits.empty() || vUnits.front()==NULL || pTarget==NULL)
		return result;

	//meta parameters depending on difficulty setting
	int iMaxBranches = range(GC.getGame().getHandicapInfo().getTacticalSimMaxBranches(),2,9); //cannot do more, else our ID scheme doesn't work
	int iMaxChoicesPerUnit = range(GC.getGame().getHandicapInfo().getTacticalSimMaxChoicesPerUnit(),2,9);
	int iMaxCompletedPositions = range(GC.getGame().getHandicapInfo().getTacticalSimMaxCompletedPositions(), 1, 4000);
	gCheckedPositions = 0;

	PlayerTypes ePlayer = vUnits.front()->getOwner();
	TeamTypes ourTeam = GET_PLAYER(ePlayer).getTeam();

	static vector<CvTacticalPosition*> openPositionsHeap;
	static vector<CvTacticalPosition*> completedPositions;

#if defined(VPDEBUG)
	if (GC.getLogging() && GC.getAILogging())
	{
		CvString strMsg = CvString::format("simulating assignments around %d:%d with %d units, agg level %d", pTarget->getX(), pTarget->getY(), vUnits.size(), eAggLvl);
		for (size_t i = 0; i < vUnits.size(); i++)
			strMsg += CvString::format("; %d", vUnits[i]->GetID());

		GET_PLAYER(ePlayer).GetTacticalAI()->LogTacticalMessage(strMsg);

		// Assertions for critical conditions
		ASSERT(iMaxBranches >= 2 && iMaxBranches <= 9 && "Invalid branch count");
		ASSERT(iMaxChoicesPerUnit >= 2 && iMaxChoicesPerUnit <= 9 && "Invalid choices per unit");
	}
#endif

	//clean up from the last run, more if the player changed to limit memory usage
	//Memory optimization: trigger hard reset every 10 turns to prevent STL container capacity bloat
	bool bHardReset = (eLastTactSimPlayer != ePlayer) || (GC.getGame().getGameTurn() % 10 == 0);
	gTactPosStorage.reset(bHardReset);
	gSupportPosStorage.reset(bHardReset);
	gAssignmentStorage.reset(bHardReset);
	eLastTactSimPlayer = ePlayer;

	gReachablePlotsLookup.clear();
	gRangeAttackPlotsLookup.clear();
	gSafePlotCount.clear();
	gBadUnitsCount.clear();
	unuseableUnits.clear();

	//basic leader trait dependence
	int iOffenseFlavor = range(GET_PLAYER(ePlayer).GetGrandStrategyAI()->GetPersonalityAndGrandStrategy((FlavorTypes)GC.getInfoTypeForString("FLAVOR_OFFENSE")), 0, 10);
	gDefaultUnitLossThreshold = (iOffenseFlavor>6 && vUnits.size()>6) ? 1 : 0;
	gMinHpForTactsim = 50 - 2 * iOffenseFlavor;
	
	//set up the initial position
	CvTacticalPosition* initialPosition = gTactPosStorage.peekNext(); gTactPosStorage.consumeOne();
	if (!initialPosition)
		return result;

	initialPosition->initFromScratch(ePlayer, eAggLvl, pTarget, bTargetDistanceRelevant, bReturnToStartPositions, iSaveMovement);

	//first pass: make sure there are no duplicates and other invalid inputs
	vector<const CvUnit*> ourUnits;
	vector<int> unitXP;

	ourUnits.reserve(vUnits.size());
	unitXP.reserve(vUnits.size());

	for (size_t i = 0; i < vUnits.size(); i++)
	{
		CvUnit* pUnit = vUnits[i];

		//do not use a set for enforcing uniqueness - the iteration order would depend on memory address by default
		//unfortunately the simulation result sometimes seems to depend on the order of the units being processed ...
		if (std::find(ourUnits.begin(), ourUnits.end(), pUnit) != ourUnits.end())
			continue;

		//units outside of their native domain are a problem because they violate 1UPT. 
		//we accept them only if they are alone in the plot and only allow movement into the native domain.
		//exception: since we ignore garrisoned units for tactsim, we can still use units (ships) in cities if a (land) garrison is present
		if (pUnit && pUnit->canUseNow())
		{
			if (pUnit->isNativeDomain(pUnit->plot()) || pUnit->plot()->getNumUnits() == 1 || pUnit->plot()->isCity())
			{
				ourUnits.push_back(vUnits[i]);
				unitXP.push_back(vUnits[i]->getExperienceTimes100());
			}
		}
	}

	if (ourUnits.empty())
		return result;

	//remember the median xp so that we can protect our experienced units over the rookies
	std::nth_element(unitXP.begin(), unitXP.begin() + unitXP.size()/2, unitXP.end());
	gMedianUnitXP = (unitXP[unitXP.size()/2]);

	//create the tactical plots around the target (up to distance 5)
	//not equivalent to the union of all reachable plots: we need to consider unreachable enemies as well!
	//some units may have their initial plots outside of this range but that's ok, we'll fix it later
	vector<CvPlot*> enemyPlots;
	for (int i = 0; i < RING_PLOTS[TACTICAL_COMBAT_MAX_TARGET_DISTANCE + 1]; i++)
	{
		CvPlot* pPlot = iterateRingPlots(pTarget, i);
		if (pPlot && pPlot->isVisible(ourTeam))
		{
			initialPosition->addTacticalPlot(pPlot, ourUnits);
			//need this for later
			if (pPlot->isEnemyUnit(initialPosition->getPlayer(), true, false))
				enemyPlots.push_back(pPlot);
		}
	}

	//second pass, ensure we have space to maneuver around our enemies
	//this might reveal new enemies but there has to be a line somewhere ...
	for (size_t j = 0; j < enemyPlots.size(); j++)
	{
		for (int i = RING0_PLOTS; i < RING_PLOTS[2]; i++)
		{
			CvPlot* pPlot = iterateRingPlots(enemyPlots[j], i);
			if (pPlot && pPlot->isVisible(ourTeam))
				initialPosition->addTacticalPlot(pPlot, ourUnits);
		}
	}

	//do this once before we start adding units
	initialPosition->countEnemiesAndCheckVisibility();

	//third pass, now that we know which units will be used, add them to the initial position
	for(vector<const CvUnit*>::const_iterator it=ourUnits.begin(); it!=ourUnits.end(); ++it)
	{
		const CvUnit* pUnit = *it;
		if (initialPosition->addAvailableUnit(pUnit))
		{
			//make sure we know the immediate surroundings of every unit
			for (int j = 0; j < RING1_PLOTS; j++)
			{
				CvPlot* pPlot = iterateRingPlots(pUnit->plot(), j);
				if (pPlot)
					initialPosition->addTacticalPlot(pPlot, ourUnits);
			}
		}
	}

	//find out which plot is frontline, second line etc
	initialPosition->refreshVolatilePlotProperties(true);

	//now associate our units with their initial plots (after we know the plot types)
	initialPosition->addInitialAssignments();

	//number of enemies influences how aggressive we can be
	//note that for defensive positioning we do not require any enemies to be nearby
	initialPosition->countEnemiesAndCheckVisibility();

	//small performance optimization
	initialPosition->setFirstInterestingAssignment(initialPosition->getAssignments().size());

	//around 15 units everything becomes slow so don't use too many
	initialPosition->dropSuperfluousUnits(TACTSIM_MAX_UNITS);

	openPositionsHeap.clear();
	completedPositions.clear();
	size_t iUsedPositions = 0;

	//don't need to call make_heap for a single element
	openPositionsHeap.push_back(initialPosition);

	//initially we go breadth-first, later switch to depth-first
	CvTacticalPosition::PrPositionSortHeapGeneration heapSort(false);

	cvStopWatch timer("tactsim", NULL, 0, true);
	timer.StartPerfTest();
	while (!openPositionsHeap.empty())
	{
		pop_heap( openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
		CvTacticalPosition* current = openPositionsHeap.back();

		//switch our strategy?
		if (!heapSort.bDepthFirst && current->getGeneration() > TACTSIM_BREADTH_FIRST_GENERATIONS)
		{
			heapSort.bDepthFirst = true;
			make_heap(openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
			//pick again after re-sorting
			continue;
		}
		else
			//go on with the selected position, remove it from the heap
			openPositionsHeap.pop_back();

		//just pick the "best" move in depth first mode
		int iMaxBranchesNow = heapSort.bDepthFirst ? 1 : iMaxBranches;
		//allow two moves per unit in case one is invalid
		int iMaxChoicesPerUnitNow = heapSort.bDepthFirst ? 2 : iMaxChoicesPerUnit;

		//here the magic happens!
		current->makeNextAssignments(iMaxBranchesNow, iMaxChoicesPerUnitNow, gTactPosStorage, openPositionsHeap, completedPositions, heapSort, ourUnits);

		int iOldPauseCount = iUsedPositions / 500;
		iUsedPositions += current->getChildren().size();
		int iNewPauseCount = iUsedPositions / 500;

		//at some point we have seen enough good positions to pick one
		if (completedPositions.size() > (size_t)iMaxCompletedPositions)
			break;

		//be a good citizen and let the UI run in between ... stupid design
		if (iOldPauseCount!=iNewPauseCount && gDLL->HasGameCoreLock())
		{
			gDLL->ReleaseGameCoreLock();
			Sleep(1);
			gDLL->GetGameCoreLock();
		}

		//did we run out of resources?
		//this typically happens if there are only invalid positions to be found ...
		if (gTactPosStorage.peekNext() == NULL)
		{
			timer.EndPerfTest();
			int iStartingUnits = initialPosition->GetNumAvailableUnits();

			std::stringstream ss;
			ss << "warning: tactsim abandoned after, " << std::setprecision(3) << timer.GetDeltaInSeconds() << " s, " <<
				completedPositions.size() << " completed, " <<
				openPositionsHeap.size() << " open, " <<
				iUsedPositions << " processed, " <<
				iStartingUnits << " starting units, " <<
				current->GetNumAvailableUnits() << " remaining units";
				CUSTOMLOG(ss.str().c_str());
			break;
		}
	}
	timer.EndPerfTest();
	int durationMs = int(timer.GetDeltaInSeconds() * 1000);

	if (completedPositions.empty())
	{
		//bad but maybe we can recover ... we should have picked BLOCKED moves for impossible units but that doesn't always happen
		//since search breadth is limited and it only turns out at the end whether a chosen move was actually impossible
		//the next best thing is to try again without the problematic units
		for (map<int,int>::const_iterator i = gBadUnitsCount.begin(); i != gBadUnitsCount.end(); ++i)
			unuseableUnits.insert(i->first);
	}
	else
	{
		//good case, pick the best one
		//need the predicate, else we sort the pointers by address!
		std::stable_sort(completedPositions.begin(), completedPositions.end(), CvTacticalPosition::PrPositionSortArrayTotalScore());

		if (completedPositions.front()->HasSupport(DOMAIN_LAND) || completedPositions.front()->HasSupport(DOMAIN_SEA) || completedPositions.front()->HasCitySupport())
			TacticalAIHelpers::AddSupportMoves(*completedPositions.front(), ourUnits);
		result = completedPositions.front()->getAssignments();
	}

	if(GC.getLogging() && GC.getAILogging())
	{
		if (true)
		{
			GET_PLAYER(ePlayer).GetTacticalAI()->LogTacticalMessage(CvString::format("tactsim around (%d:%d) with agg %d finished in %d ms. started with %d units and %d enemies on %d plots. used %d positions, %d completed.",
				pTarget->getX(),pTarget->getY(), eAggLvl, durationMs, initialPosition->GetNumAvailableUnits(), initialPosition->getNumEnemies(), initialPosition->getNumPlots(), iUsedPositions, completedPositions.size()));
		}

		//debug dump
#if defined(MOD_CORE_DEBUGGING)
		if (gCurrentUnitToTrack == -1)
		{
			ofstream out("c:\\temp\\positionscores.csv", std::ios::app);
			if (out)
			{
				for (int i = 0; i < gTactPosStorage.getSize(); i++)
				{
					CvTacticalPosition* pos = gTactPosStorage.first() + i;
					bool isComplete = pos->isEarlyFinish() || pos->isExhausted();
					out << pos->getID() << "," << pos->getScoreLastRound() << "," << pos->getScoreTotal() << "," << (isComplete ? 1:0) << ";";
				}
				out << std::endl;
			}
			out.close();
		}
#endif
	}


#if defined(VPDEBUG)
	// Additional debug info
	char szDebugInfo[256];
	sprintf_s(szDebugInfo, "TacticalAI: Target (%d,%d), Units %lu, Agg %d, Enemies %d, Checked Positions %d, Used %d, Completed %lu, Bad Units %lu, %d ms, Player %d\n",
		pTarget->getX(), pTarget->getY(), (unsigned long)vUnits.size(), eAggLvl, initialPosition->getNumEnemies(), gCheckedPositions, iUsedPositions, (unsigned long)completedPositions.size(), (unsigned long)unuseableUnits.size(), durationMs, ePlayer);
	OutputDebugString(szDebugInfo);
#endif

	return result;
}

bool TacticalAIHelpers::ExecuteUnitAssignments(PlayerTypes ePlayer, const std::vector<STacticalAssignment>& vAssignments)
{
	static const BuildTypes eCitadel = (BuildTypes)GC.getInfoTypeForString("BUILD_CITADEL");
	static const BuildTypes eOrdo = MOD_BALANCE_VP ? (BuildTypes)GC.getInfoTypeForString("BUILD_ORDO") : NO_BUILD;
	static const BuildTypes eIsibaya = MOD_BALANCE_VP ? (BuildTypes)GC.getInfoTypeForString("BUILD_ISIBAYA") : NO_BUILD;

	//take the assigned moves one by one and try to execute them faithfully. 
	//may fail if a melee kill unexpectedly happens or does not happen

	vector<CvUnit*> finishedUnits;

	for (size_t i = 0; i < vAssignments.size(); i++)
	{
		CvUnit* pUnit = GET_PLAYER(ePlayer).getUnit(vAssignments[i].iUnitID);
		//be extra careful with the unit here, if we capture cities and liberate them strange instakills can happen
		//so we need to guess whether the pointer is still valid
		if (!pUnit || pUnit->isDelayedDeath() || pUnit->plot()==NULL)
			continue;

		CvPlot* pFromPlot = GC.getMap().plotByIndexUnchecked(vAssignments[i].iFromPlotIndex);
		CvPlot* pToPlot = GC.getMap().plotByIndexUnchecked(vAssignments[i].iToPlotIndex);

		//abort movement and retry if we find eg an enemy submarine
		int iMoveflags = CvUnit::MOVEFLAG_IGNORE_DANGER | CvUnit::MOVEFLAG_NO_STOPNODES | CvUnit::MOVEFLAG_ABORT_IF_NEW_ENEMY_REVEALED;
		bool bPrecondition = false;
		bool bPostcondition = false;

		CvUnit* pEnemy = NULL;

		switch (vAssignments[i].eAssignmentType)
		{
		case A_INITIAL:
		case A_FINISH_TEMP:
		case A_MOVE_DOUBLE:
		case A_WAIT:
			continue; //skip this!
			break;
		case A_MOVE:
		case A_MOVE_FORCED:
		case A_CAPTURE:
			pUnit->ClearPathCache(); //make sure there's no stale path which coincides with our target
			bPrecondition = pUnit->canMove() && (pUnit->plot() == pFromPlot) && !(pToPlot->isEnemyUnit(ePlayer,true,true) || pToPlot->isEnemyCity(*pUnit)); //no enemy
#ifdef TACTDEBUG
			if (bPrecondition)
			{
				//see if we can indeed reach the target plot this turn ... 
				pUnit->ClearPathCache(); 
				if (!pUnit->GeneratePath(pToPlot, iMoveflags) || pUnit->GetPathEndFirstTurnPlot() != pToPlot)
					OutputDebugString("ouch, pathfinding problem\n");
			}
#endif
			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pToPlot->getX(), pToPlot->getY(), iMoveflags, false, false, MISSIONAI_OPMOVE);

			//movement may indeed fail if we stumble upon an invisible unit!
			bPostcondition = (pUnit->plot() == pToPlot);

			//post-move safety for ranged units: if we couldn't attack and we're in danger, try to pull back
			if (bPostcondition && pUnit->IsCanAttackRanged() && pUnit->canMove())
			{
				int iDanger = pUnit->GetDanger();
				if (iDanger > pUnit->GetCurrHitPoints() / 2)
				{
					bool bAttacked = TacticalAIHelpers::PerformRangedOpportunityAttack(pUnit, true);
					if (!bAttacked)
					{
						CvPlot* pSafePlot = TacticalAIHelpers::FindSafestPlotInReach(pUnit, true, true).first;
						if (pSafePlot && pSafePlot != pUnit->plot() && pUnit->canMoveInto(*pSafePlot, CvUnit::MOVEFLAG_DESTINATION))
						{
							pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pSafePlot->getX(), pSafePlot->getY(), CvUnit::MOVEFLAG_AI_ABORT_IN_DANGER, false, false, MISSIONAI_OPMOVE);
						}
					}
				}
			}

#ifdef TACTDEBUG
			//check this only for moves, eg melee kills can fail this check because the pathfinder assumes attacks end the turn!
			if (vAssignments[i].iRemainingMoves != pUnit->getMoves() && bPostcondition)
				OutputDebugString("ouch, inconsistent movement points\n");
#endif
			break;
		case A_MOVE_SWAP:
			pUnit->ClearPathCache(); //make sure there's no stale path which coincides with our target
			bPrecondition = (pUnit->plot() == pFromPlot) && !(pToPlot->isEnemyUnit(ePlayer,true,true) || pToPlot->isEnemyCity(*pUnit)); //no enemy
			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_SWAP_UNITS(), pToPlot->getX(), pToPlot->getY(), iMoveflags, false, false, MISSIONAI_OPMOVE);
			bPostcondition = (pUnit->plot() == pToPlot); //plot changed
			break;
		case A_MOVE_SWAP_REVERSE:
			//nothing to do, this is just a dummy which always occurs after MOVE_SWAP for bookkeeping
			bPrecondition = (pUnit->plot() == pToPlot);
			bPostcondition = (pUnit->plot() == pToPlot);
			break;
		case A_RANGEATTACK:
		{
			bool bCityBefore = pToPlot->isEnemyCity(*pUnit);
			bool bUnitBefore = pToPlot->isEnemyUnit(ePlayer, true, true);
			bPrecondition = (pUnit->plot() == pFromPlot) && (bCityBefore || bUnitBefore); //enemy present
			pEnemy = pToPlot->getBestDefender(NO_PLAYER, ePlayer, pUnit);
			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pToPlot->getX(), pToPlot->getY());
			bPostcondition = (!bCityBefore || pToPlot->isEnemyCity(*pUnit)) && (!bUnitBefore || (pEnemy && !pEnemy->IsDead())); //enemy should survive
			break;
		}
		case A_RANGEKILL:
			bPrecondition = (pUnit->plot() == pFromPlot) && pToPlot->isEnemyUnit(ePlayer,true,true); //defending unit present. does not apply to cities
			pEnemy = pToPlot->getBestDefender(NO_PLAYER, ePlayer, pUnit);
			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_RANGE_ATTACK(), pToPlot->getX(), pToPlot->getY());
			bPostcondition = pEnemy && pEnemy->IsDead(); //defending unit is gone
			break;
		case A_MELEEATTACK:
			bPrecondition = (pUnit->plot() == pFromPlot) && (pToPlot->isEnemyUnit(ePlayer,true,true) || pToPlot->isEnemyCity(*pUnit)); //enemy present
			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pToPlot->getX(), pToPlot->getY());
			bPostcondition = (pUnit->plot() == pFromPlot) && (pToPlot->isEnemyUnit(ePlayer,true,true) || pToPlot->isEnemyCity(*pUnit)); //enemy still present
			break;
		case A_MELEEKILL:
		case A_MELEEKILL_NO_ADVANCE:
		{
			bPrecondition = (pUnit->plot() == pFromPlot) && (pToPlot->isEnemyUnit(ePlayer, true, true) || pToPlot->isEnemyCity(*pUnit)); //enemy present
			CvCity* pCity = pToPlot->getPlotCity();
			CvUnit* pEnemy = pToPlot->getBestDefender(NO_PLAYER, ePlayer, pUnit);
			bool bCityKill = false;
			bool bUnitKill = false;
			//because of randomness in previous combat results, it may happen that we cannot actually kill the enemy
			if (bPrecondition)
			{
				int iDamageDealt = 0;
				int iDamageReceived = 0;
				int iGarrisonDamageDealt = 0;
				if (pToPlot->isEnemyCity(*pUnit))
				{
					iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnCity(pCity, pUnit, pUnit->plot(), iDamageReceived, iGarrisonDamageDealt);
					if (iDamageDealt >= (pCity->GetMaxHitPoints() - pCity->getDamage()))
						bCityKill = true;
					if (pEnemy && (bCityKill || iGarrisonDamageDealt >= pEnemy->GetCurrHitPoints()))
						bUnitKill = true;
				}
				else
				{
					iDamageDealt = TacticalAIHelpers::GetSimulatedDamageFromAttackOnUnit(pEnemy, pUnit, pEnemy->plot(), pUnit->plot(), iDamageReceived);
					if (iDamageDealt >= pEnemy->GetCurrHitPoints())
						bUnitKill = true;
				}
			}

			if (bPrecondition)
				pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pToPlot->getX(), pToPlot->getY());

			//because of randomness in previous combat results, it may happen that we cannot actually kill the enemy
			if (vAssignments[i].eAssignmentType == A_MELEEKILL)
			{
				bPostcondition = pUnit->plot() == pToPlot; //advanced into enemy plot
			}
			else
			{
				bPostcondition = pUnit->plot() == pFromPlot;
			}
			if (bPostcondition && bUnitKill)
			{
				bPostcondition = pEnemy->IsDead(); //defending unit is dead
			}
			break;
		}
		case A_PILLAGE:
			pUnit->PushMission(CvTypes::getMISSION_PILLAGE());
			bPrecondition = true;
			bPostcondition = true;
			break;
		case A_USE_POWER:
			if (eOrdo != NO_BUILD && pUnit->canBuild(pUnit->plot(), eOrdo))
				pUnit->PushMission(CvTypes::getMISSION_BUILD(), eOrdo);
			else if (eIsibaya != NO_BUILD && pUnit->canBuild(pUnit->plot(), eIsibaya))
				pUnit->PushMission(CvTypes::getMISSION_BUILD(), eIsibaya);
			else if (pUnit->canBuild(pUnit->plot(), eCitadel))
				pUnit->PushMission(CvTypes::getMISSION_BUILD(), eCitadel);
			else if (pUnit->canRepairFleet(pUnit->plot()))
				pUnit->PushMission(CvTypes::getMISSION_REPAIR_FLEET());
			else
				bPrecondition = false;
			break;
		case A_HEAL:
		case A_FINISH:
			pUnit->PushMission(CvTypes::getMISSION_SKIP());
			//this is the difference to a blocked unit, we prevent anyone else from moving it unless we want it to heal
			if (!pUnit->shouldHeal(false) || pUnit->getMoves() == 0 || pUnit->isBarbarian()) //barbarians don't heal
				//important ... this allows civilian units to use this one as cover!
				finishedUnits.push_back(pUnit);
			bPrecondition = true;
			bPostcondition = true;
			break;
		case A_BLOCKED:
			pUnit->PushMission(CvTypes::getMISSION_SKIP());
			//do not mark the unit as processed, it can be reused for other tasks!
			bPrecondition = true;
			bPostcondition = true;
			break;
		case A_RESTART:
			return false; //the previous move revealed a new enemy (which cause a danger update). restart the combat simulation with the remaining units.
			break;
		}

#ifdef TACTDEBUG
		//this can happen sometimes because of randomness or splash damage etc
		if (!bPrecondition || !bPostcondition)
		{
			CvString strLogString;
			const char* unitName = pUnit->getUnitInfo().GetDescription();
			strLogString.Format(
				"Turn %d: tactsim: could not execute %s from (%d,%d) to (%d,%d) with %s (%d) now at (%d,%d) (%s failed)\n",
				GC.getGame().getGameTurn(),
				assignmentTypeNames[vAssignments[i].eAssignmentType],
				pFromPlot ? pFromPlot->getX() : -1,
				pFromPlot ? pFromPlot->getY() : -1,
				pToPlot ? pToPlot->getX() : -1,
				pToPlot ? pToPlot->getY() : -1,
				unitName,
				vAssignments[i].iUnitID,
				(pUnit&& pUnit->plot()) ? pUnit->plot()->getX() : -1,
				(pUnit&& pUnit->plot()) ? pUnit->plot()->getY() : -1,
				!bPrecondition ? "precondition" : "postcondition"
			);
			OutputDebugString(strLogString);
			return false;
		}
#else
		if (!bPrecondition || !bPostcondition)
			return false;
#endif
	}

	// Only do this once we know were successful
	for (vector<CvUnit*>::const_iterator it = finishedUnits.begin(); it != finishedUnits.end(); ++it)
	{
		GET_PLAYER(ePlayer).GetTacticalAI()->UnitProcessed((*it)->GetID());
	}

	return true;
}

bool TacticalAIHelpers::AddSupportMoves(CvTacticalPosition& positionAfterCombatMoves, const vector<const CvUnit*>& ourUnits, bool bEarlyExit)
{
	vector<STacticalAssignment> result;

	gSupportPosStorage.reset(false);

	static vector<CvSupportPosition*> openPositionsHeap;
	static vector<CvSupportPosition*> completedPositions;
	size_t iUsedPositions = 0;

	int iMaxBranches = range(GC.getGame().getHandicapInfo().getTacticalSimMaxBranches(), 2, 9); //cannot do more, else our ID scheme doesn't work
	int iMaxChoicesPerUnit = range(GC.getGame().getHandicapInfo().getTacticalSimMaxChoicesPerUnit(), 2, 9);
	int iMaxCompletedPositions = range(GC.getGame().getHandicapInfo().getTacticalSimMaxCompletedPositions(), 1, 4000);

	vector<const CvTacticalPosition*> allCombatPositions;
	map<const CvTacticalPosition*, const CvTacticalPosition*> nextAttackPosition;
	const CvTacticalPosition* tactPos = &positionAfterCombatMoves;
	while (tactPos != NULL)
	{
		allCombatPositions.push_back(tactPos);
		tactPos = tactPos->getParent();
	}

	std::reverse(allCombatPositions.begin(), allCombatPositions.end());

	// Insert all assignments from the initial position
	const CvTacticalPosition* previousPosition = NULL;

	for (vector<const CvTacticalPosition*>::const_iterator it = allCombatPositions.begin(); it != allCombatPositions.end(); ++it)
	{
		tactPos = *it;

		bool bAttackMove = IsAttackMove(tactPos->getAssignments().back().eAssignmentType);

		if (bAttackMove)
		{
			if (previousPosition)
				nextAttackPosition[previousPosition] = tactPos;

			previousPosition = tactPos;
		}
		if (it == allCombatPositions.end() - 1)
		{
			if (previousPosition && !bAttackMove)
				nextAttackPosition[previousPosition] = tactPos;

			nextAttackPosition[tactPos] = NULL;
		}
	}

	CvSupportPosition* initialPosition = gSupportPosStorage.peekNext(); gSupportPosStorage.consumeOne();
	if (!initialPosition)
	{
		return true;
	}

	// breadth first
	CvTacticalPosition::PrPositionSortHeapGeneration heapSort(false);

	if (bEarlyExit)
	{
		initialPosition->initFromTacticalPosition(positionAfterCombatMoves, positionAfterCombatMoves, ourUnits);
		initialPosition->addInitialAssignments();
		initialPosition->setFirstInterestingAssignment(initialPosition->getAssignments().size());

		initialPosition->updateMovePlotsIfRequired();

		for (size_t i = 0; i < initialPosition->getAvailableUnits().size(); i++)
		{
			initialPosition->getPreferredAssignmentsForUnit(initialPosition->getAvailableUnits()[i], iMaxChoicesPerUnit, true);
			if (gPossibleMoves.empty())
				return false;
		}

		return true;
	}

	initialPosition->initFromTacticalPosition(*nextAttackPosition.begin()->first, positionAfterCombatMoves, ourUnits);
	initialPosition->addInitialAssignments();
	initialPosition->setFirstInterestingAssignment(initialPosition->getAssignments().size());

	openPositionsHeap.clear();
	completedPositions.clear();

	openPositionsHeap.push_back(initialPosition);

	while (!openPositionsHeap.empty())
	{
		pop_heap(openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
		CvSupportPosition* current = openPositionsHeap.back();

		//switch our strategy?
		if (!heapSort.bDepthFirst && current->getGeneration() > TACTSIM_BREADTH_FIRST_GENERATIONS)
		{
			heapSort.bDepthFirst = true;
			make_heap(openPositionsHeap.begin(), openPositionsHeap.end(), heapSort);
			//pick again after re-sorting
			continue;
		}
		else
			//go on with the selected position, remove it from the heap
			openPositionsHeap.pop_back();

		//just pick the "best" move in depth first mode
		int iMaxBranchesNow = heapSort.bDepthFirst ? 1 : iMaxBranches;
		//allow two moves per unit in case one is invalid
		int iMaxChoicesPerUnitNow = heapSort.bDepthFirst ? 2 : iMaxChoicesPerUnit;

		//here the magic happens!
		current->makeNextAssignments(iMaxBranchesNow, iMaxChoicesPerUnitNow, gSupportPosStorage, openPositionsHeap, completedPositions, heapSort, nextAttackPosition);

		int iOldPauseCount = iUsedPositions / 500;
		iUsedPositions += current->getChildren().size();
		int iNewPauseCount = iUsedPositions / 500;

		//at some point we have seen enough good positions to pick one
		if (completedPositions.size() > (size_t)iMaxCompletedPositions)
			break;

		//be a good citizen and let the UI run in between ... stupid design
		if (iOldPauseCount != iNewPauseCount && gDLL->HasGameCoreLock())
		{
			gDLL->ReleaseGameCoreLock();
			Sleep(1);
			gDLL->GetGameCoreLock();
		}
	}

	if (!completedPositions.empty())
	{
		//good case, pick the best one
		//need the predicate, else we sort the pointers by address!
		std::stable_sort(completedPositions.begin(), completedPositions.end(), CvTacticalPosition::PrPositionSortArrayTotalScore());

		vector<STacticalAssignment>::const_iterator combatIt = positionAfterCombatMoves.getAssignments().begin();
		vector<STacticalAssignment>::const_iterator supportIt = completedPositions.front()->getAssignments().begin();

		while (combatIt != positionAfterCombatMoves.getAssignments().end())
		{
			if (IsAttackMove(combatIt->eAssignmentType) || combatIt == positionAfterCombatMoves.getAssignments().end() - 1)
			{
				while (supportIt != completedPositions.front()->getAssignments().end())
				{
					if (supportIt->eAssignmentType == A_WAIT)
					{
						supportIt++;
						break;
					}

					result.push_back(*supportIt);
					positionAfterCombatMoves.UpdateScore(*supportIt);
					++supportIt;
				}
			}

			result.push_back(*combatIt);
			++combatIt;
		}
	}
	else
		// no viable positions found
		return false;

	positionAfterCombatMoves.SetAssignments(result);
	return true;
}

void CvTactPosStorage::reset(bool bHard)
{ 
	//this is normally enough
	iCount = 0; 
	attackCache.clear(); 
	dangerCache.clear();

	//in a hard reset we recreate all stl containers from scratch
	//because their capacity tends to increase over time otherwise
	if (bHard)
	{
		//PrintMemoryInfo("before hard reset");

		for (int i = 0; i < iSize; i++)
			aPositions[i].wipe();

		//for some reason the memory usage is not affected immediately ... but the wiping works
		//PrintMemoryInfo("after hard reset");
	}
}

void CvSupportPosStorage::reset(bool bHard)
{
	//this is normally enough
	iCount = 0;

	//in a hard reset we recreate all stl containers from scratch
	//because their capacity tends to increase over time otherwise
	if (bHard)
	{
		//PrintMemoryInfo("before hard reset");

		for (int i = 0; i < iSize; i++)
			aPositions[i].wipe();

		//for some reason the memory usage is not affected immediately ... but the wiping works
		//PrintMemoryInfo("after hard reset");
	}
}

void CvTactAssignmentStorage::reset(bool bHard)
{
	//this is normally enough
	iCount = 0;

	//in a hard reset we recreate all stl containers from scratch
	//because their capacity tends to increase over time otherwise
	if (bHard)
	{
		//PrintMemoryInfo("before hard reset");

		for (int i = 0; i < iSize; i++)
			aAssignments[i].wipe();

		//for some reason the memory usage is not affected immediately ... but the wiping works
		//PrintMemoryInfo("after hard reset");
	}
}

const char* tacticalMoveNames[] =
{
	"T_NONE",

	"T_UNASSIGNED",
	"T_GUARD",
	"T_GARRISON",
	"T_OPERATION",

	"T_PILLAGE",
	"T_PLUNDER",
	"T_GOODY",

	"T_HEAL",
	"T_SAFETY",
	"T_REPOSITION",
	"T_ESCORT",

	"T_AIRSWEEP",
	"T_AIRPATROL",

	"T_HEDGEHOG",
	"T_COUNTERATTACK",
	"T_WITHDRAW",
	"T_REINFORCE",
	"T_ATTRITION",
	"T_SURGICAL_STRIKE",
	"T_STEAMROLL",
	"T_FLANKATTACK",

	"T_AIRLIFT",
	"T_BLOCKADE",
	"T_CAPTURE",

	"B_CAMP",
	"B_ROAM",
	"B_HUNT",
};

const char* postureNames[] =
{
	"P_NONE",
    "P_WITHDRAW",
    "P_ATTRIT_FROM_RANGE",
    "P_EXPLOIT_FLANKS",
    "P_STEAMROLL",
    "P_SURGICAL_CITY_STRIKE",
    "P_HEDGEHOG",
    "P_COUNTERATTACK",
    "P_SHORE_BOMBARDMENT",
};

const char* assignmentTypeNames[] = 
{
	"INITIAL",
	"MOVE", 
	"MELEEATTACK", 
	"MELEEKILL", 
	"RANGEATTACK", 
	"RANGEKILL", 
	"FINISH",
	"BLOCKED",
	"PILLAGE",
	"CAPTURE",
	"FORCEDMOVE",
	"RESTART",
	"MELEEKILL_NOADVANCE",
	"SWAP",
	"SWAPREVERSE",
	"MOVEDOUBLE",
	"USEPOWER",
	"FINISHTEMP",
	"HEAL",
	"WAIT"
};

