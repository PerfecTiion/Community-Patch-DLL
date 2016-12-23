﻿/*	-------------------------------------------------------------------------------------------------------
	� 1991-2012 Take-Two Interactive Software and its subsidiaries.  Developed by Firaxis Games.  
	Sid Meier's Civilization V, Civ, Civilization, 2K Games, Firaxis Games, Take-Two Interactive Software 
	and their respective logos are all trademarks of Take-Two interactive Software, Inc.  
	All other marks and trademarks are the property of their respective owners.  
	All rights reserved. 
	------------------------------------------------------------------------------------------------------- */

#include "CvGameCoreDLLPCH.h"
#include "CvGlobals.h"
#include "CvCity.h"
#include "CvArea.h"
#include "CvMap.h"
#include "CvPlot.h"
#include "CvTeam.h"
#include "CvGameCoreUtils.h"
#include "CvInternalGameCoreUtils.h"
#include "CvPlayerAI.h"
#include "CvUnit.h"
#include "CvInfos.h"
#include "CvRandom.h"
#include "CvImprovementClasses.h"
#include "CvCitySpecializationAI.h"
#include "CvEconomicAI.h"
#include "CvMilitaryAI.h"
#include "CvNotifications.h"
#include "CvUnitCombat.h"
#include "CvTypes.h"

// interfaces used
#include "CvEnumSerialization.h"
#include "CvDiplomacyAI.h"
#include "CvWonderProductionAI.h"

#include "CvDllCity.h"
#include "CvDllCombatInfo.h"
#include "CvDllPlot.h"
#include "CvDllUnit.h"
#include "CvGameQueries.h"

#include "CvInfosSerializationHelper.h"
#include "cvStopWatch.h"
#include "CvCityManager.h"
#if defined(MOD_BALANCE_CORE)
#include "CvBarbarians.h"
#endif
// include after all other headers
#include "LintFree.h"

OBJECT_VALIDATE_DEFINITION(CvCity)

namespace
{
// debugging
YieldTypes s_lastYieldUsedToUpdateRateFromTerrain;
int        s_changeYieldFromTerreain;
}

//	--------------------------------------------------------------------------------
namespace FSerialization
{

//is it wise to store pointers here?
//anyway vector seems better than set because of the defined ordering
std::vector<CvCity*> citiesToCheck;

void SyncCities()
{
	if(GC.getGame().isNetworkMultiPlayer())
	{
		PlayerTypes authoritativePlayer = GC.getGame().getActivePlayer();

		std::vector<CvCity*>::const_iterator i;
		for(i = citiesToCheck.begin(); i != citiesToCheck.end(); ++i)
		{
			const CvCity* city = *i;

			if(city)
			{
				const CvPlayer& player = GET_PLAYER(city->getOwner());
				if(city->getOwner() == authoritativePlayer || (gDLL->IsHost() && !player.isHuman() && player.isAlive()))
				{
					const FAutoArchive& archive = city->getSyncArchive();
					if(archive.hasDeltas())
					{
						FMemoryStream memoryStream;
						std::vector<std::pair<std::string, std::string> > callStacks;
						archive.saveDelta(memoryStream, callStacks);
						gDLL->sendCitySyncCheck(city->getOwner(), city->GetID(), memoryStream, callStacks);
					}
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
// clears ALL deltas for ALL units
void ClearCityDeltas()
{
	std::vector<CvCity*>::iterator i;
	for(i = citiesToCheck.begin(); i != citiesToCheck.end(); ++i)
	{
		CvCity* city = *i;

		if(city)
		{
			FAutoArchive& archive = city->getSyncArchive();
			archive.clearDelta();
		}
	}
}
}


//	--------------------------------------------------------------------------------
// Public Functions...
CvCity::CvCity() :
	m_syncArchive(*this)
	, m_strNameIAmNotSupposedToBeUsedAnyMoreBecauseThisShouldNotBeCheckedAndWeNeedToPreserveSaveGameCompatibility("CvCity::m_strName", m_syncArchive, "")
	, m_eOwner("CvCity::m_eOwner", m_syncArchive, NO_PLAYER)
	, m_iX("CvCity::m_iX", m_syncArchive)
	, m_iY("CvCity::m_iY", m_syncArchive)
	, m_iID("CvCity::m_iID", m_syncArchive)
	, m_iRallyX("CvCity::m_iRallyX", m_syncArchive)
	, m_iRallyY("CvCity::m_iRallyY", m_syncArchive)
	, m_iGameTurnFounded("CvCity::m_iGameTurnFounded", m_syncArchive)
	, m_iGameTurnAcquired("CvCity::m_iGameTurnAcquired", m_syncArchive)
	, m_iGameTurnLastExpanded("CvCity::m_iGameTurnLastExpanded", m_syncArchive)
#if defined(MOD_BALANCE_CORE)
	, m_iAdditionalFood("CvCity::m_iAdditionalFood", m_syncArchive)
#endif
	, m_iPopulation("CvCity::m_iPopulation", m_syncArchive)
	, m_iHighestPopulation("CvCity::m_iHighestPopulation", m_syncArchive)
	, m_iExtraHitPoints("CvCity::m_iExtraHitPoints", m_syncArchive)
	, m_iNumGreatPeople("CvCity::m_iNumGreatPeople", m_syncArchive)
	, m_iBaseGreatPeopleRate("CvCity::m_iBaseGreatPeopleRate", m_syncArchive)
	, m_iGreatPeopleRateModifier("CvCity::m_iGreatPeopleRateModifier", m_syncArchive)
	, m_iJONSCultureStored("CvCity::m_iJONSCultureStored", m_syncArchive, true)
	, m_iJONSCultureLevel("CvCity::m_iJONSCultureLevel", m_syncArchive)
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	, m_iJONSCulturePerTurnFromBuildings("CvCity::m_iJONSCulturePerTurnFromBuildings", m_syncArchive)
#endif
	, m_iJONSCulturePerTurnFromPolicies("CvCity::m_iJONSCulturePerTurnFromPolicies", m_syncArchive)
	, m_iJONSCulturePerTurnFromSpecialists("CvCity::m_iJONSCulturePerTurnFromSpecialists", m_syncArchive)
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	, m_iJONSCulturePerTurnFromReligion("CvCity::m_iJONSCulturePerTurnFromReligion", m_syncArchive)
#endif
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	, m_iFaithPerTurnFromBuildings("CvCity::m_iFaithPerTurnFromBuildings", m_syncArchive)
#endif
	, m_iFaithPerTurnFromPolicies("CvCity::m_iFaithPerTurnFromPolicies", m_syncArchive)
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	, m_iFaithPerTurnFromReligion("CvCity::m_iFaithPerTurnFromReligion", m_syncArchive)
#endif
	, m_iCultureRateModifier("CvCity::m_iCultureRateModifier", m_syncArchive)
	, m_iNumWorldWonders("CvCity::m_iNumWorldWonders", m_syncArchive)
	, m_iNumTeamWonders("CvCity::m_iNumTeamWonders", m_syncArchive)
	, m_iNumNationalWonders("CvCity::m_iNumNationalWonders", m_syncArchive)
	, m_iWonderProductionModifier("CvCity::m_iWonderProductionModifier", m_syncArchive)
	, m_iCapturePlunderModifier("CvCity::m_iCapturePlunderModifier", m_syncArchive)
	, m_iPlotCultureCostModifier("CvCity::m_iPlotCultureCostModifier", m_syncArchive)
	, m_iPlotBuyCostModifier("CvCity::m_iPlotBuyCostModifier", m_syncArchive)
	, m_iMaintenance("CvCity::m_iMaintenance", m_syncArchive)
	, m_iHealRate("CvCity::m_iHealRate", m_syncArchive)
	, m_iNoOccupiedUnhappinessCount("CvCity::m_iNoOccupiedUnhappinessCount", m_syncArchive)
	, m_iFood("CvCity::m_iFood", m_syncArchive)
	, m_iFoodKept("CvCity::m_iFoodKept", m_syncArchive)
	, m_iMaxFoodKeptPercent("CvCity::m_iMaxFoodKeptPercent", m_syncArchive)
	, m_iOverflowProduction("CvCity::m_iOverflowProduction", m_syncArchive)
	, m_iFeatureProduction("CvCity::m_iFeatureProduction", m_syncArchive)
	, m_iMilitaryProductionModifier("CvCity::m_iMilitaryProductionModifier", m_syncArchive)
	, m_iSpaceProductionModifier("CvCity::m_iSpaceProductionModifier", m_syncArchive)
	, m_iFreeExperience("CvCity::m_iFreeExperience", m_syncArchive)
	, m_iCurrAirlift("CvCity::m_iCurrAirlift", m_syncArchive) // unused
	, m_iMaxAirUnits("CvCity::m_iMaxAirUnits", m_syncArchive)
	, m_iAirModifier("CvCity::m_iAirModifier", m_syncArchive) // unused
	, m_iNukeModifier("CvCity::m_iNukeModifier", m_syncArchive)
	, m_iCultureUpdateTimer("CvCity::m_iCultureUpdateTimer", m_syncArchive)	// unused
	, m_iCitySizeBoost("CvCity::m_iCitySizeBoost", m_syncArchive)
	, m_iSpecialistFreeExperience("CvCity::m_iSpecialistFreeExperience", m_syncArchive)
	, m_iStrengthValue("CvCity::m_iStrengthValue", m_syncArchive, true)
	, m_iDamage("CvCity::m_iDamage", m_syncArchive)
	, m_iThreatValue("CvCity::m_iThreatValue", m_syncArchive, true)
	, m_hGarrison("CvCity::m_hGarrison", m_syncArchive)
	, m_iResourceDemanded("CvCity::m_iResourceDemanded", m_syncArchive)
	, m_iWeLoveTheKingDayCounter("CvCity::m_iWeLoveTheKingDayCounter", m_syncArchive)
	, m_iLastTurnGarrisonAssigned("CvCity::m_iLastTurnGarrisonAssigned", m_syncArchive)
	, m_iThingsProduced("CvCity::m_iThingsProduced", m_syncArchive)
	, m_iDemandResourceCounter("CvCity::m_iDemandResourceCounter", m_syncArchive, true)
	, m_iResistanceTurns("CvCity::m_iResistanceTurns", m_syncArchive)
	, m_iRazingTurns("CvCity::m_iRazingTurns", m_syncArchive)
	, m_iCountExtraLuxuries("CvCity::m_iCountExtraLuxuries", m_syncArchive)
	, m_iCheapestPlotInfluenceDistance("CvCity::m_iCheapestPlotInfluenceDistance", m_syncArchive)
	, m_iEspionageModifier("CvCity::m_iEspionageModifier", m_syncArchive)
	, m_iTradeRouteRecipientBonus("CvCity::m_iTradeRouteRecipientBonus", m_syncArchive)
	, m_iTradeRouteSeaGoldBonus("CvCity::m_iTradeRouteSeaGoldBonus", m_syncArchive)
	, m_iTradeRouteLandGoldBonus("CvCity::m_iTradeRouteLandGoldBonus", m_syncArchive)
	, m_iTradeRouteTargetBonus("CvCity::m_iTradeRouteTargetBonus", m_syncArchive)
	, m_iNumTradeRouteBonus("CvCity::m_iNumTradeRouteBonus", m_syncArchive)
	, m_unitBeingBuiltForOperation()
	, m_bNeverLost("CvCity::m_bNeverLost", m_syncArchive)
	, m_bDrafted("CvCity::m_bDrafted", m_syncArchive)
	, m_bAirliftTargeted("CvCity::m_bAirliftTargeted", m_syncArchive)   // unused
	, m_bProductionAutomated("CvCity::m_bProductionAutomated", m_syncArchive)
	, m_bLayoutDirty("CvCity::m_bLayoutDirty", m_syncArchive)
	, m_bMadeAttack("CvCity::m_bMadeAttack", m_syncArchive)
	, m_bOccupied("CvCity::m_bOccupied", m_syncArchive)
	, m_bPuppet("CvCity::m_bPuppet", m_syncArchive)
	, m_bIgnoreCityForHappiness("CvCity::m_bIgnoreCityForHappiness", m_syncArchive)
	, m_bIndustrialRouteToCapital("CvCity::m_bIndustrialRouteToCapital", m_syncArchive)
	, m_bFeatureSurrounded("CvCity::m_bFeatureSurrounded", m_syncArchive)
	, m_ePreviousOwner("CvCity::m_ePreviousOwner", m_syncArchive)
	, m_eOriginalOwner("CvCity::m_eOriginalOwner", m_syncArchive)
	, m_ePlayersReligion("CvCity::m_ePlayersReligion", m_syncArchive)
	, m_aiSeaPlotYield("CvCity::m_aiSeaPlotYield", m_syncArchive)
	, m_aiRiverPlotYield("CvCity::m_aiRiverPlotYield", m_syncArchive)
	, m_aiLakePlotYield("CvCity::m_aiLakePlotYield", m_syncArchive)
	, m_aiSeaResourceYield("CvCity::m_aiSeaResourceYield", m_syncArchive)
	, m_aiBaseYieldRateFromTerrain("CvCity::m_aiBaseYieldRateFromTerrain", m_syncArchive, true)
	, m_aiBaseYieldRateFromBuildings("CvCity::m_aiBaseYieldRateFromBuildings", m_syncArchive)
	, m_aiBaseYieldRateFromSpecialists("CvCity::m_aiBaseYieldRateFromSpecialists", m_syncArchive)
	, m_aiBaseYieldRateFromMisc("CvCity::m_aiBaseYieldRateFromMisc", m_syncArchive)
	, m_aiBaseYieldRateFromReligion("CvCity::m_aiBaseYieldRateFromReligion", m_syncArchive)
	, m_aiYieldRateModifier("CvCity::m_aiYieldRateModifier", m_syncArchive)
	, m_aiYieldPerPop("CvCity::m_aiYieldPerPop", m_syncArchive)
	, m_aiYieldPerReligion("CvCity::m_aiYieldPerReligion", m_syncArchive)
	, m_aiPowerYieldRateModifier("CvCity::m_aiPowerYieldRateModifier", m_syncArchive)
	, m_aiResourceYieldRateModifier("CvCity::m_aiResourceYieldRateModifier", m_syncArchive)
	, m_aiExtraSpecialistYield("CvCity::m_aiExtraSpecialistYield", m_syncArchive)
	, m_aiProductionToYieldModifier("CvCity::m_aiProductionToYieldModifier", m_syncArchive)
	, m_aiDomainFreeExperience("CvCity::m_aiDomainFreeExperience", m_syncArchive)
	, m_aiDomainProductionModifier("CvCity::m_aiDomainProductionModifier", m_syncArchive)
#if defined(MOD_BALANCE_CORE_EVENTS)
	, m_aiEconomicValue("CvCity::m_aiEconomicValue", m_syncArchive)
	, m_aiEventChoiceDuration("CvCity::m_aiEventChoiceDuration", m_syncArchive)
	, m_aiEventIncrement("CvCity::m_aiEventIncrement", m_syncArchive)
	, m_abEventActive("CvCity::m_abEventActive", m_syncArchive)
	, m_abEventChoiceActive("CvCity::m_abEventChoiceActive", m_syncArchive)
	, m_abEventChoiceFired("CvCity::m_abEventChoiceFired", m_syncArchive)
	, m_abEventFired("CvCity::m_abEventFired", m_syncArchive)
	, m_aiEventCooldown("CvCity::m_aiEventCooldown", m_syncArchive)
	, m_aiEventCityYield("CvCity::m_aiEventCityYield", m_syncArchive)
	, m_iEventHappiness("CvCity::m_iEventHappiness", m_syncArchive)
	, m_iCityEventCooldown("CvCity::m_iCityEventCooldown", m_syncArchive)
	, m_iComboUnhappiness("CvCity::m_iComboUnhappiness", m_syncArchive)
	, m_iPillagedPlots("CvCity::m_iPillagedPlots", m_syncArchive)
	, m_iGrowthFromTourism("CvCity::m_iGrowthFromTourism", m_syncArchive)
	, m_iBuildingClassHappinessFromReligion("CvCity::m_iBuildingClassHappinessFromReligion", m_syncArchive)
#endif
	, m_abEverOwned("CvCity::m_abEverOwned", m_syncArchive)
	, m_abRevealed("CvCity::m_abRevealed", m_syncArchive, true)
	, m_strScriptData("CvCity::m_strScriptData", m_syncArchive)
	, m_paiNoResource("CvCity::m_paiNoResource", m_syncArchive)
	, m_paiFreeResource("CvCity::m_paiFreeResource", m_syncArchive)
	, m_paiNumResourcesLocal("CvCity::m_paiNumResourcesLocal", m_syncArchive)
	, m_paiProjectProduction("CvCity::m_paiProjectProduction", m_syncArchive)
	, m_paiSpecialistProduction("CvCity::m_paiSpecialistProduction", m_syncArchive)
	, m_paiUnitProduction("CvCity::m_paiUnitProduction", m_syncArchive)
	, m_paiUnitProductionTime("CvCity::m_paiUnitProductionTime", m_syncArchive)
	, m_paiSpecialistCount("CvCity::m_paiSpecialistCount", m_syncArchive)
	, m_paiMaxSpecialistCount("CvCity::m_paiMaxSpecialistCount", m_syncArchive)
	, m_paiForceSpecialistCount("CvCity::m_paiForceSpecialistCount", m_syncArchive)
	, m_paiFreeSpecialistCount("CvCity::m_paiFreeSpecialistCount", m_syncArchive)
	, m_paiImprovementFreeSpecialists("CvCity::m_paiImprovementFreeSpecialists", m_syncArchive)
	, m_paiUnitCombatFreeExperience("CvCity::m_paiUnitCombatFreeExperience", m_syncArchive)
	, m_paiUnitCombatProductionModifier("CvCity::m_paiUnitCombatProductionModifier", m_syncArchive)
	, m_paiFreePromotionCount("CvCity::m_paiFreePromotionCount", m_syncArchive)
	, m_iBaseHappinessFromBuildings("CvCity::m_iBaseHappinessFromBuildings", m_syncArchive)
	, m_iUnmoddedHappinessFromBuildings("CvCity::m_iUnmoddedHappinessFromBuildings", m_syncArchive)
	, m_bRouteToCapitalConnectedLastTurn("CvCity::m_bRouteToCapitalConnectedLastTurn", m_syncArchive)
	, m_bRouteToCapitalConnectedThisTurn("CvCity::m_bRouteToCapitalConnectedThisTurn", m_syncArchive)
	, m_strName("CvCity::m_strName", m_syncArchive)
	, m_orderQueue()
	, m_aaiBuildingSpecialistUpgradeProgresses(0)
	, m_ppaiResourceYieldChange(0)
	, m_ppaiFeatureYieldChange(0)
	, m_ppaiTerrainYieldChange(0)
	, m_pCityBuildings(FNEW(CvCityBuildings, c_eCiv5GameplayDLL, 0))
	, m_pCityStrategyAI(FNEW(CvCityStrategyAI, c_eCiv5GameplayDLL, 0))
	, m_pCityCitizens(FNEW(CvCityCitizens, c_eCiv5GameplayDLL, 0))
	, m_pCityReligions(FNEW(CvCityReligions, c_eCiv5GameplayDLL, 0))
	, m_pEmphases(FNEW(CvCityEmphases, c_eCiv5GameplayDLL, 0))
	, m_pCityEspionage(FNEW(CvCityEspionage, c_eCiv5GameplayDLL, 0))
	, m_pCityCulture(FNEW(CvCityCulture, c_eCiv5GameplayDLL, 0))
	, m_bombardCheckTurn(0)
	, m_iPopulationRank("CvCity::m_iPopulationRank", m_syncArchive)
	, m_bPopulationRankValid("CvCity::m_bPopulationRankValid", m_syncArchive)
	, m_aiBaseYieldRank("CvCity::m_aiBaseYieldRank", m_syncArchive)
	, m_abBaseYieldRankValid("CvCity::m_abBaseYieldRankValid", m_syncArchive)
	, m_aiYieldRank("CvCity::m_aiYieldRank", m_syncArchive)
	, m_abYieldRankValid("CvCity::m_abYieldRankValid", m_syncArchive)
	, m_bOwedCultureBuilding("CvCity::m_bOwedCultureBuilding", m_syncArchive)
#if defined(MOD_BUILDINGS_CITY_WORKING)
	, m_iCityWorkingChange("CvCity::m_iCityWorkingChange", m_syncArchive)
#endif
#if defined(MOD_RELIGION_CONVERSION_MODIFIERS)
	, m_iConversionModifier("CvCity::m_iConversionModifier", m_syncArchive)
#endif
#if defined(MOD_DIPLOMACY_CITYSTATES)
	, m_aiBaseYieldRateFromLeague("CvCity::m_aiBaseYieldRateFromLeague", m_syncArchive)
	, m_iTotalScienceyAid("CvCity::m_iTotalScienceyAid", m_syncArchive)
	, m_iTotalArtsyAid("CvCity::m_iTotalArtsyAid", m_syncArchive)
	, m_iTotalGreatWorkAid("CvCity::m_iTotalGreatWorkAid", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
	, m_iChangePovertyUnhappiness("CvCity::m_iChangePovertyUnhappiness", m_syncArchive)
	, m_iChangeDefenseUnhappiness("CvCity::m_iChangeDefenseUnhappiness", m_syncArchive)
	, m_iChangeUnculturedUnhappiness("CvCity::m_iChangeUnculturedUnhappiness", m_syncArchive)
	, m_iChangeIlliteracyUnhappiness("CvCity::m_iChangeIlliteracyUnhappiness", m_syncArchive)
	, m_iChangeMinorityUnhappiness("CvCity::m_iChangeMinorityUnhappiness", m_syncArchive)
#endif
#if defined(MOD_DIPLOMACY_CITYSTATES) || defined(MOD_BALANCE_CORE)
	, m_aiChangeGrowthExtraYield("CvCity::m_aiChangeGrowthExtraYield", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_iTradeRouteSeaDistanceModifier("CvCity::m_iTradeRouteSeaDistanceModifier", m_syncArchive)
	, m_iTradeRouteLandDistanceModifier("CvCity::m_iTradeRouteLandDistanceModifier", m_syncArchive)
	, m_iTradePriorityLand("CvCity::m_iTradePriorityLand", m_syncArchive)
	, m_iTradePrioritySea("CvCity::m_iTradePrioritySea", m_syncArchive)
	, m_iNearbySettlerValue("CvCity::m_iNearbySettlerValue", m_syncArchive)
	, m_iThreatRank("CvCity::m_iThreatRank", m_syncArchive)
	, m_iUnitPurchaseCooldown("CvCity::m_iUnitPurchaseCooldown", m_syncArchive)
	, m_iBuildingPurchaseCooldown("CvCity::m_iBuildingPurchaseCooldown", m_syncArchive)
	, m_iReligiousTradeModifier("CvCity::m_iReligiousTradeModifier", m_syncArchive)
	, m_iFreeBuildingTradeTargetCity("CvCity::m_iFreeBuildingTradeTargetCity", m_syncArchive)
	, m_iBaseTourism("CvCity::m_iBaseTourism", m_syncArchive)
	, m_iBaseTourismBeforeModifiers("CvCity::m_iBaseTourismBeforeModifiers", m_syncArchive)
	, m_aiChangeYieldFromVictory("CvCity::m_aiChangeYieldFromVictory", m_syncArchive)
	, m_aiYieldFromKnownPantheons("CvCity::m_aiYieldFromKnownPantheons", m_syncArchive)
	, m_aiGoldenAgeYieldMod("CvCity::m_aiGoldenAgeYieldMod", m_syncArchive)
	, m_aiYieldFromWLTKD("CvCity::m_aiYieldFromWLTKD", m_syncArchive)
	, m_aiYieldFromConstruction("CvCity::m_aiYieldFromConstruction", m_syncArchive)
	, m_aiYieldFromTech("CvCity::m_aiYieldFromTech", m_syncArchive)
	, m_aiYieldFromUnitProduction("CvCity::m_aiYieldFromUnitProduction", m_syncArchive)
	, m_aiYieldFromBirth("CvCity::m_aiYieldFromBirth", m_syncArchive)
	, m_aiYieldFromBorderGrowth("CvCity::m_aiYieldFromBorderGrowth", m_syncArchive)
	, m_aiYieldFromPolicyUnlock("CvCity::m_aiYieldFromPolicyUnlock", m_syncArchive)
	, m_aiYieldFromPurchase("CvCity::m_aiYieldFromPurchase", m_syncArchive)
	, m_aiYieldFromUnitLevelUp("CvCity::m_aiYieldFromUnitLevelUp", m_syncArchive)
	, m_aiYieldPerAlly("CvCity::m_aiYieldPerAlly", m_syncArchive)
	, m_aiYieldPerFriend("CvCity::m_aiYieldPerFriend", m_syncArchive)
	, m_aiScienceFromYield("CvCity::m_aiScienceFromYield", m_syncArchive)
	, m_aiBuildingScienceFromYield("CvCity::m_aiBuildingScienceFromYield", m_syncArchive)
	, m_aiSpecialistRateModifier("CvCity::m_aiSpecialistRateModifier", m_syncArchive)
	, m_aiNumTimesOwned("CvCity::m_aiNumTimesOwned", m_syncArchive)
	, m_aiStaticCityYield("CvCity::m_aiStaticCityYield", m_syncArchive)
	, m_aiThemingYieldBonus("CvCity::m_aiThemingYieldBonus", m_syncArchive)
	, m_aiBaseYieldRateFromCSAlliance("CvCity::m_aiBaseYieldRateFromCSAlliance", m_syncArchive)
	, m_aiBaseYieldRateFromCSFriendship("CvCity::m_aiBaseYieldRateFromCSFriendship", m_syncArchive)
	, m_aiResourceQuantityPerXFranchises("CvCity::m_aiResourceQuantityPerXFranchises", m_syncArchive)
	, m_aiYieldChangeFromCorporationFranchises("CvCity::m_aiYieldChangeFromCorporationFranchises", m_syncArchive)
	, m_iLandTourismBonus("CvCity::m_iLandTourismBonus", m_syncArchive)
	, m_iSeaTourismBonus("CvCity::m_iSeaTourismBonus", m_syncArchive)
	, m_iAlwaysHeal("CvCity::m_iAlwaysHeal", m_syncArchive)
	, m_bIsBastion("CvCity::m_bIsBastion", m_syncArchive)
	, m_bNoWarmonger("CvCity::m_bNoWarmonger", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_iBlockBuildingDestruction("CvCity::m_iBlockBuildingDestruction", m_syncArchive)
	, m_iBlockWWDestruction("CvCity::m_iBlockWWDestruction", m_syncArchive)
	, m_iBlockUDestruction("CvCity::m_iBlockUDestruction", m_syncArchive)
	, m_iBlockGPDestruction("CvCity::m_iBlockGPDestruction", m_syncArchive)
	, m_iBlockRebellion("CvCity::m_iBlockRebellion", m_syncArchive)
	, m_iBlockUnrest("CvCity::m_iBlockUnrest", m_syncArchive)
	, m_iBlockScience("CvCity::m_iBlockScience", m_syncArchive)
	, m_iBlockGold("CvCity::m_iBlockGold", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE_SPIES)
	, m_iCityRank("CvCity::m_iCityRank", m_syncArchive)
	, m_iTurnsSinceRankAnnouncement("CvCity::m_iTurnsSinceRankAnnouncement", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_abIsBestForWonder("CvCity::m_abIsBestForWonder", m_syncArchive)
	, m_abIsPurchased("CvCity::m_abIsPurchased", m_syncArchive)
	, m_abTraded("CvCity::m_abTraded", m_syncArchive)
	, m_abPaidAdoptionBonus("CvCity::m_abPaidAdoptionBonus", m_syncArchive)
	, m_iExtraBuildingMaintenance("CvCity::m_iExtraBuildingMaintenance", m_syncArchive)
	, m_paiNumTerrainWorked("CvCity::m_paiNumTerrainWorked", m_syncArchive)
	, m_paiNumFeaturelessTerrainWorked("CvCity::m_paiNumFeaturelessTerrainWorked", m_syncArchive)
	, m_paiNumFeatureWorked("CvCity::m_paiNumFeatureWorked", m_syncArchive)
	, m_paiNumResourceWorked("CvCity::m_paiNumResourceWorked", m_syncArchive)
	, m_paiNumImprovementWorked("CvCity::m_paiNumImprovementWorked", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	, m_paiBuildingClassCulture("CvCity::m_paiBuildingClassCulture", m_syncArchive)
	, m_paiHurryModifier("CvCity::m_paiHurryModifier", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_vClosestNeighbors("CvCity::m_vClosestNeighbors", m_syncArchive)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_ppaiImprovementYieldChange(0)
	, m_ppaaiSpecialistExtraYield("CvCity::m_ppaaiSpecialistExtraYield", m_syncArchive)
#endif
#if defined(MOD_API_UNIFIED_YIELDS) && defined(MOD_API_PLOT_YIELDS)
	, m_ppaiPlotYieldChange(0)
	, m_ppaiYieldPerXTerrainFromBuildings(0)
	, m_ppaiYieldPerXTerrainFromReligion(0)
	, m_ppaiYieldPerXTerrain(0)
	, m_ppaiYieldPerXFeature(0)
	, m_ppaiYieldPerXUnimprovedFeature(0)
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	, m_ppaiReligionBuildingYieldRateModifier(0)
	, m_ppaiLocalBuildingClassYield(0)
	, m_ppaiEventBuildingClassYield(0)
	, m_ppaiEventBuildingClassYieldModifier(0)
	, m_ppaiEventImprovementYield(0)
	, m_ppaiEventResourceYield(0)
	, m_ppaiEventTerrainYield(0)
	, m_ppaiEventFeatureYield(0)
	, m_ppaiEventSpecialistYield(0)
#endif
#if defined(MOD_BALANCE_CORE)
	, m_abOwedChosenBuilding("CvCity::m_abOwedChosenBuilding", m_syncArchive)
	, m_abBuildingInvestment("CvCity::m_abBuildingInvestment", m_syncArchive)
	, m_abUnitInvestment("CvCity::m_abUnitInvestment", m_syncArchive)
	, m_abBuildingConstructed("CvCity::m_abBuildingConstructed", m_syncArchive)
	, m_iBorderObstacleCity("CvCity::m_iBorderObstacleCity", m_syncArchive)
	, m_iBorderObstacleWater("CvCity::m_iBorderObstacleWater", m_syncArchive)
	, m_iNumNearbyMountains("CvCity::m_iNumNearbyMountains", m_syncArchive)
	, m_iLocalUnhappinessMod("CvCity::m_iLocalUnhappinessMod", m_syncArchive)
#endif
#if defined(MOD_BUGFIX_FREE_FOOD_BUILDING)
	, m_bOwedFoodBuilding("CvCity::m_bOwedFoodBuilding", m_syncArchive)
#endif
#if defined(MOD_CORE_PER_TURN_DAMAGE)
	, m_iDamageTakenThisTurn("CvCity::m_iDamageTakenThisTurn", m_syncArchive)
	, m_iDamageTakenLastTurn("CvCity::m_iDamageTakenLastTurn", m_syncArchive)
#endif
{
	OBJECT_ALLOCATED
	FSerialization::citiesToCheck.push_back(this);

	reset(0, NO_PLAYER, 0, 0, true);
}

//	--------------------------------------------------------------------------------
CvCity::~CvCity()
{
	CvCityManager::OnCityDestroyed(this);

	//really shouldn't happen that it's not present, but there was a crash here
	std::vector<CvCity*>::iterator it = std::find( FSerialization::citiesToCheck.begin(), FSerialization::citiesToCheck.end(), this);
	if (it!=FSerialization::citiesToCheck.end())
		FSerialization::citiesToCheck.erase(it);

	uninit();

	delete m_pCityBuildings;
	delete m_pCityStrategyAI;
	delete m_pCityCitizens;
	delete m_pCityReligions;
	delete m_pEmphases;
	delete m_pCityEspionage;
	delete m_pCityCulture;

	OBJECT_DESTROYED
}


//	--------------------------------------------------------------------------------
#if defined(MOD_API_EXTENSIONS)
void CvCity::init(int iID, PlayerTypes eOwner, int iX, int iY, bool bBumpUnits, bool bInitialFounding, ReligionTypes eInitialReligion, const char* szName)
#else
void CvCity::init(int iID, PlayerTypes eOwner, int iX, int iY, bool bBumpUnits, bool bInitialFounding)
#endif
{
	VALIDATE_OBJECT
	//CvPlot* pAdjacentPlot;
	CvPlot* pPlot;
	BuildingTypes eLoopBuilding;
	int iI;

	pPlot = GC.getMap().plot(iX, iY);

	//--------------------------------
	// Init saved data
	reset(iID, eOwner, pPlot->getX(), pPlot->getY());

	CvPlayerAI& owningPlayer = GET_PLAYER(getOwner());

	//--------------------------------
	// Init non-saved data

	//--------------------------------
	// Init other game data
	CvString strNewCityName = owningPlayer.getNewCityName();
	setName(strNewCityName.c_str());

#if !defined(NO_ACHIEVEMENTS)
	if(strcmp(strNewCityName.c_str(), "TXT_KEY_CITY_NAME_LLANFAIRPWLLGWYNGYLL") == 0)
	{
		gDLL->UnlockAchievement(ACHIEVEMENT_XP1_34);
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if(bInitialFounding)
	{
		SetNumTimesOwned(eOwner, 1);
	}
	else
	{
		ChangeNumTimesOwned(eOwner, 1);
	}
#endif

	// Plot Ownership
	setEverOwned(getOwner(), true);

	pPlot->setOwner(getOwner(), m_iID, bBumpUnits);
	// Clear the improvement before the city attaches itself to the plot, else the improvement does not
	// remove the resource allocation from the current owner.  This would result in double resource points because
	// the plot has already had setOwner called on it (above), giving the player the resource points.
	pPlot->setImprovementType(NO_IMPROVEMENT);
#if defined(MOD_BUGFIX_MINOR)
#if defined(MOD_EVENTS_TILE_IMPROVEMENTS)
	pPlot->SetImprovementPillaged(false, false);
	pPlot->SetRoutePillaged(false, false);
#else
	pPlot->SetImprovementPillaged(false);
	pPlot->SetRoutePillaged(false);
#endif
#endif
	pPlot->setPlotCity(this);
	pPlot->SetCityPurchaseID(m_iID);

	int iRange = 1;
#if defined(MOD_CONFIG_GAME_IN_XML)
	iRange = GD_INT_GET(CITY_STARTING_RINGS);
#endif
	for(int iDX = -iRange; iDX <= iRange; iDX++)
	{
		for(int iDY = -iRange; iDY <= iRange; iDY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iRange);
			if(pLoopPlot != NULL)
			{
				if(pLoopPlot != NULL)
				{
					if(pLoopPlot->getOwner() == NO_PLAYER)
					{
#if defined(MOD_WWII_TERRITORY)
						if(!pLoopPlot->isWater())
						{
							pLoopPlot->setOwner(getOwner(), NO_PLAYER, bBumpUnits);
						}
#else
						pLoopPlot->setOwner(getOwner(), m_iID, bBumpUnits);
#endif
					}
					if(pLoopPlot->getOwner() == getOwner())
					{
						pLoopPlot->SetCityPurchaseID(m_iID);
					}
				}
			}
		}
	}

	// this is a list of plot that are owned by the player
	owningPlayer.UpdatePlots();

#if defined(MOD_GLOBAL_CITY_FOREST_BONUS)
	static BuildTypes eBuildRemoveForest = (BuildTypes)GC.getInfoTypeForString("BUILD_REMOVE_FOREST");
	static BuildTypes eBuildRemoveJungle = (BuildTypes)GC.getInfoTypeForString("BUILD_REMOVE_JUNGLE");
	bool bClearedForest = false;
	bool bClearedJungle = false;
	FeatureTypes eFeature = pPlot->getFeatureType();
#endif

	//SCRIPT call ' bool citiesDestroyFeatures(iX, iY);'
	if(pPlot->getFeatureType() != NO_FEATURE)
	{
#if defined(MOD_GLOBAL_CITY_FOREST_BONUS)
		// Only for major civs building on a forest
		if(MOD_GLOBAL_CITY_FOREST_BONUS && eBuildRemoveForest != -1 && !owningPlayer.isMinorCiv() && (eFeature == FEATURE_FOREST))
		{
			// Don't do this for the AI capitals - it's just too much of an initial boost!
			TechTypes iRequiredTech = (TechTypes) gCustomMods.getOption("GLOBAL_CITY_FOREST_BONUS_TECH", -1);
			bClearedForest = (iRequiredTech == -1 || GET_TEAM(owningPlayer.getTeam()).GetTeamTechs()->HasTech(iRequiredTech));
		}
		// OR only for major civs building on a jungle
		else if(MOD_GLOBAL_CITY_FOREST_BONUS && (eBuildRemoveJungle != -1) && (!owningPlayer.isMinorCiv()) && (eFeature == FEATURE_JUNGLE))
		{
			TechTypes iRequiredTech = (TechTypes) gCustomMods.getOption("GLOBAL_CITY_FOREST_BONUS_TECH", -1);
			bClearedJungle = (iRequiredTech == -1 || GET_TEAM(owningPlayer.getTeam()).GetTeamTechs()->HasTech(iRequiredTech));
		}
#endif
					
		pPlot->setFeatureType(NO_FEATURE);
	}

	// wipe out dig sites
	ResourceTypes eArtifactResourceType = static_cast<ResourceTypes>(GC.getARTIFACT_RESOURCE());
	ResourceTypes eHiddenArtifactResourceType = static_cast<ResourceTypes>(GC.getHIDDEN_ARTIFACT_RESOURCE());
	if (pPlot->getResourceType() == eArtifactResourceType || pPlot->getResourceType() == eHiddenArtifactResourceType)
	{
		pPlot->setResourceType(NO_RESOURCE, 0);
		pPlot->ClearArchaeologicalRecord();
	}

	setupGraphical();

	pPlot->updateCityRoute();

	//force recalculation of trade routes
	GC.getGame().GetGameTrade()->InvalidateTradePathCache(eOwner);

	for(iI = 0; iI < MAX_TEAMS; iI++)
	{
		if(GET_TEAM((TeamTypes)iI).isAlive())
		{
			if(pPlot->isVisible(((TeamTypes)iI)))
			{
				setRevealed(((TeamTypes)iI), true);
			}
		}
	}

	int iNumBuildingInfos = GC.getNumBuildingInfos();
	for(iI = 0; iI < iNumBuildingInfos; iI++)
	{
		if(owningPlayer.isBuildingFree((BuildingTypes)iI))
		{
			if(isValidBuildingLocation((BuildingTypes)iI))
			{
				m_pCityBuildings->SetNumFreeBuilding(((BuildingTypes)iI), 1);
			}
		}
	}
#if defined(MOD_BALANCE_CORE)
	// Free Buildings
	const CvCivilizationInfo& thisCiv = getCivilizationInfo();
	for(int iBuildingClassLoop = 0; iBuildingClassLoop < GC.getNumBuildingClassInfos(); iBuildingClassLoop++)
	{
		const BuildingClassTypes eBuildingClass = static_cast<BuildingClassTypes>(iBuildingClassLoop);
		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
		if(!pkBuildingClassInfo)
		{
			continue;
		}

		BuildingTypes eBuilding = ((BuildingTypes)(thisCiv.getCivilizationBuildings(eBuildingClass)));

		if(eBuilding != NO_BUILDING)
		{
			if (GET_PLAYER(getOwner()).GetNumCitiesFreeChosenBuilding(eBuildingClass) > 0 || GET_PLAYER(getOwner()).IsFreeChosenBuildingNewCity(eBuildingClass))
			{		
				CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
				if(pkBuildingInfo)
				{
					if(isValidBuildingLocation(eBuilding))
					{
						if(GetCityBuildings()->GetNumRealBuilding(eBuilding) > 0)
						{
							GetCityBuildings()->SetNumRealBuilding(eBuilding, 0);
						}

						GetCityBuildings()->SetNumFreeBuilding(eBuilding, 1);

						if(GetCityBuildings()->GetNumFreeBuilding(eBuilding) > 0)
						{
							GET_PLAYER(getOwner()).ChangeNumCitiesFreeChosenBuilding(eBuildingClass, -1);
						}
						if(getFirstBuildingOrder(eBuilding) == 0)
						{
							clearOrderQueue();
							chooseProduction();
							// Send a notification to the user that what they were building was given to them, and they need to produce something else.
						}
					}
				}
			}
		}
	}
#endif

	GC.getMap().getArea(pPlot->getArea())->changeCitiesPerPlayer(getOwner(), 1);
#if defined(MOD_BALANCE_CORE)
	std::vector<int> areas = pPlot->getAllAdjacentAreas();
	for (std::vector<int>::iterator it=areas.begin(); it!=areas.end(); ++it)
	{
		CvArea* pkArea = GC.getMap().getArea(*it);
		if (pkArea->isWater())
			pkArea->changeCitiesPerPlayer(getOwner(), 1);
	}
#endif

	GET_TEAM(getTeam()).changeNumCities(1);

	GC.getGame().changeNumCities(1);
	// Tell the city manager now as well.
	CvCityManager::OnCityCreated(this);

	int iGameTurn = GC.getGame().getGameTurn();
	setGameTurnFounded(iGameTurn);
	setGameTurnAcquired(iGameTurn);
	setGameTurnLastExpanded(iGameTurn);

	GC.getMap().updateWorkingCity(pPlot,getWorkPlotDistance()*2);
	GetCityCitizens()->DoFoundCity();

	// Default starting population
	changePopulation(GC.getINITIAL_CITY_POPULATION() + GC.getGame().getStartEraInfo().getFreePopulation());
	// Free population from things (e.g. Policies)
	changePopulation(GET_PLAYER(getOwner()).GetNewCityExtraPopulation());

#if defined(MOD_API_EXTENSIONS)
	// We do this here as changePopulation() sends a game event we may have caught to do funky renaming things
	if (szName) {
		setName(szName);
	}
#endif
	// Free food from things (e.g. Policies)
	int iFreeFood = growthThreshold() * GET_PLAYER(getOwner()).GetFreeFoodBox();
	changeFoodTimes100(iFreeFood);
	
	if (bInitialFounding)
	{
		owningPlayer.setFoundedFirstCity(true);
		owningPlayer.ChangeNumCitiesFounded(1);

#if defined(MOD_BALANCE_CORE)
		if(MOD_BALANCE_CORE && isCapital())
		{
			int iNumAllies = GET_PLAYER(getOwner()).GetNumCSAllies();
			int iNumFriends = GET_PLAYER(getOwner()).GetNumCSFriends();
			if(iNumAllies > 0 || iNumFriends > 0)
			{
				//If we get a yield bonus in all cities because of CS alliance, this is a good place to change it.
				int iEra = owningPlayer.GetCurrentEra();
				if(iEra <= 0)
				{
					iEra = 1;
				}
				for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
				{
					YieldTypes eYield = (YieldTypes) iI;
					if(owningPlayer.GetPlayerTraits()->GetYieldFromCSAlly(eYield) > 0)
					{
						SetBaseYieldRateFromCSAlliance(eYield, (owningPlayer.GetPlayerTraits()->GetYieldFromCSAlly(eYield) * iNumAllies * iEra));
					}
					if(owningPlayer.GetPlayerTraits()->GetYieldFromCSAlly(eYield) > 0)
					{
						SetBaseYieldRateFromCSFriendship(eYield, (owningPlayer.GetPlayerTraits()->GetYieldFromCSFriend(eYield) * iNumFriends * iEra));
					}
				}
			}
		}
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && owningPlayer.GetNumCitiesFounded() <= 1)
		{
			GET_PLAYER(getOwner()).ChangePuppetUnhappinessMod(GC.getBALANCE_HAPPINESS_PUPPET_THRESHOLD_MOD());

			int iCapitalMod = GC.getBALANCE_HAPPINESS_CAPITAL_MODIFIER();

			if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoAnnexing() || GC.getGame().isOption(GAMEOPTION_ONE_CITY_CHALLENGE))
			{
				iCapitalMod = (GC.getBALANCE_HAPPINESS_CAPITAL_MODIFIER() * 2);
			}

			GET_PLAYER(getOwner()).ChangeCapitalUnhappinessModCBP(iCapitalMod);
		}
#endif
		// Free resources under city?
		for(int i = 0; i < GC.getNumResourceInfos(); i++)
		{
			ResourceTypes eResource = (ResourceTypes)i;
			FreeResourceXCities freeResource = owningPlayer.GetPlayerTraits()->GetFreeResourceXCities(eResource);

			if(freeResource.m_iResourceQuantity > 0)
			{
				if(owningPlayer.GetNumCitiesFounded() <= freeResource.m_iNumCities)
				{
					plot()->setResourceType(NO_RESOURCE, 0);
					plot()->setResourceType(eResource, freeResource.m_iResourceQuantity);
				}
			}
		}
#if defined(MOD_BALANCE_CORE_LUXURIES_TRAIT)
		if(MOD_BALANCE_CORE_LUXURIES_TRAIT && !owningPlayer.isMinorCiv() && (owningPlayer.GetPlayerTraits()->GetUniqueLuxuryQuantity() > 0))
		{
			owningPlayer.GetPlayerTraits()->AddUniqueLuxuriesAround(this, owningPlayer.GetPlayerTraits()->GetUniqueLuxuryQuantity());
		}
		else
		{
#endif
			owningPlayer.GetPlayerTraits()->AddUniqueLuxuries(this);
#if defined(MOD_BALANCE_CORE_LUXURIES_TRAIT)
		}
#endif
		if(owningPlayer.isMinorCiv())
		{
			owningPlayer.GetMinorCivAI()->DoAddStartingResources(plot());
		}
	}
#if defined(MOD_BALANCE_CORE_DIFFICULTY)
	if(MOD_BALANCE_CORE_DIFFICULTY && !owningPlayer.isMinorCiv() && !owningPlayer.isHuman() && bInitialFounding)
	{	
		int iYieldHandicap = owningPlayer.DoDifficultyBonus();
		if((GC.getLogging() && GC.getAILogging()))
		{
			CvString strLogString;
			strLogString.Format("CBP AI DIFFICULTY BONUS FROM CITY FOUNDING: Received Handicap Bonus (%d in Yields).", iYieldHandicap);
			GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
		}
	}
#endif
	// make sure that all the team members get the city connection update
	for(int i = 0; i < MAX_PLAYERS; i++)
	{
		PlayerTypes ePlayer = (PlayerTypes)i;
		if(GET_PLAYER(ePlayer).getTeam() == owningPlayer.getTeam())
		{
			GET_PLAYER(ePlayer).GetCityConnections()->Update();
		}
	}
#if defined(MOD_BALANCE_CORE)
	for(int iFeatureLoop = 0; iFeatureLoop < GC.getNumFeatureInfos(); iFeatureLoop++)
	{
		FeatureTypes eFeature2 = (FeatureTypes) iFeatureLoop;
		if(eFeature2 != NO_FEATURE)
		{
			GET_PLAYER(getOwner()).countCityFeatures(eFeature2, true);
		}
	}
#endif

	// Policy changes
	PolicyTypes ePolicy;
	for(int iPoliciesLoop = 0; iPoliciesLoop < GC.getNumPolicyInfos(); iPoliciesLoop++)
	{
		ePolicy = (PolicyTypes) iPoliciesLoop;

		if(owningPlayer.GetPlayerPolicies()->HasPolicy(ePolicy) && !owningPlayer.GetPlayerPolicies()->IsPolicyBlocked(ePolicy))
		{
			// Free Culture-per-turn in every City from Policies
			ChangeJONSCulturePerTurnFromPolicies(GC.getPolicyInfo(ePolicy)->GetCulturePerCity());
		}
	}

	// Add Resource Quantity to total
#if defined(MOD_BALANCE_CORE)
	if(plot()->getResourceType(getTeam()) != NO_RESOURCE)
#else
	if(plot()->getResourceType() != NO_RESOURCE)
#endif
	{
		if(GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes) GC.getResourceInfo(plot()->getResourceType())->getTechCityTrade()))
		{
			owningPlayer.changeNumResourceTotal(plot()->getResourceType(), plot()->getNumResourceForPlayer(getOwner()));
		}
	}
#if defined(MOD_BALANCE_CORE_EVENTS)
	if(MOD_BALANCE_CORE_EVENTS)
	{
		owningPlayer.CheckActivePlayerEvents(this);
	}
#endif
	CvPlot* pLoopPlot;

	// We may need to link Resources to this City if it's constructed within previous borders and the Resources were too far away for another City to link to

	for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
	{
		pLoopPlot = iterateRingPlots(getX(), getY(), iJ);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->getOwner() == getOwner())
			{
				if(pLoopPlot->getResourceType() != NO_RESOURCE)
				{
					// Is this Resource as of yet unlinked?
					if(pLoopPlot->GetResourceLinkedCity() == NULL)
					{
						pLoopPlot->DoFindCityToLinkResourceTo();
					}
				}
			}
		}
	}
#if defined(MOD_BALANCE_CORE)
	//Update our CoM for the diplo AI.
	owningPlayer.SetCenterOfMassEmpire();
#endif	
	PlayerTypes ePlayer;

	// Update Proximity between this Player and all others
	for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
	{
		ePlayer = (PlayerTypes) iPlayerLoop;

		if(ePlayer != getOwner())
		{
			if(GET_PLAYER(ePlayer).isAlive())
			{
				// Players do NOT have to know one another in order to calculate proximity.  Having this info available (even whey they haven't met) can be useful
				owningPlayer.DoUpdateProximityToPlayer(ePlayer);
				GET_PLAYER(ePlayer).DoUpdateProximityToPlayer(getOwner());
			}
		}
	}

	// Free Buildings in the first City
	if(GC.getGame().isFinalInitialized())
	{
		if(owningPlayer.getNumCities() == 1)
		{
#if defined(MOD_EVENTS_CITY_CAPITAL)
			int eCapitalBuilding = thisCiv.getCivilizationBuildings(GC.getCAPITAL_BUILDINGCLASS());
#endif
			
			for(iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
			{
				CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
				if(!pkBuildingClassInfo)
				{
					continue;
				}

				if(thisCiv.isCivilizationFreeBuildingClass(iI))
				{
					eLoopBuilding = ((BuildingTypes)(thisCiv.getCivilizationBuildings(iI)));

					if(eLoopBuilding != NO_BUILDING)
					{
						m_pCityBuildings->SetNumRealBuilding(eLoopBuilding, true);
#if defined(MOD_EVENTS_CITY_CAPITAL)
						if (iI == eCapitalBuilding && MOD_EVENTS_CITY_CAPITAL) {
							GAMEEVENTINVOKE_HOOK(GAMEEVENT_CapitalChanged, getOwner(), GetID(), -1);
						}
#endif
					}
				}
			}
#if defined(MOD_BALANCE_CORE)
			//Free building in Capital from Trait?
			if(owningPlayer.GetPlayerTraits()->GetFreeCapitalBuilding() != NO_BUILDING)
			{
				if(owningPlayer.GetPlayerTraits()->GetCapitalFreeBuildingPrereqTech() == NO_TECH)
				{
					BuildingTypes eBuilding = owningPlayer.GetPlayerTraits()->GetFreeCapitalBuilding();
					if(eBuilding != NO_BUILDING)
					{
						if(isValidBuildingLocation(eBuilding))
						{
							m_pCityBuildings->SetNumFreeBuilding(eBuilding, 1);
						}
					}
				}
			}
#endif

			if(!isHuman())
			{
				changeOverflowProduction(GC.getINITIAL_AI_CITY_PRODUCTION());
#if defined(ACHIEVEMENT_HACKS)
			} else {
				CvAchievementUnlocker::UnlockFromDatabase();
#endif
			}
		}
	}

	// How long before this City picks a Resource to demand?
	DoSeedResourceDemandedCountdown();

	// Garrisoned?
	SetGarrison( plot()->getBestGarrison(getOwner()) );

	// Update Unit Maintenance for the player
	CvPlayer& kPlayer = GET_PLAYER(getOwner());
	kPlayer.UpdateUnitProductionMaintenanceMod();
#if defined(MOD_BALANCE_CORE)
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		UpdateYieldPerXUnimprovedFeature(((YieldTypes)iI));
	}
	int iRange3 = 3;
	int iMountain = 0;
	for(int iDX = -iRange3; iDX <= iRange3; iDX++)
	{
		for(int iDY = -iRange3; iDY <= iRange3; iDY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iRange3);
			if(pLoopPlot != NULL)
			{
#if defined(MOD_PSEUDO_NATURAL_WONDER)
				if(pLoopPlot->isMountain() && !pLoopPlot->IsNaturalWonder(true))
#else
				if(pLoopPlot->isMountain() && !pLoopPlot->IsNaturalWonder())
#endif
				{
					iMountain++;
				}
			}
		}
	}
	SetNearbyMountains(iMountain);
#endif
	// Spread a pantheon here if one is active
	CvPlayerReligions* pReligions = kPlayer.GetReligions();
	if(pReligions->HasCreatedPantheon() && !pReligions->HasCreatedReligion())
	{
		GetCityReligions()->AddReligiousPressure(FOLLOWER_CHANGE_PANTHEON_FOUNDED, RELIGION_PANTHEON, GC.getRELIGION_ATHEISM_PRESSURE_PER_POP() * getPopulation() * 2);
	}
	
#if defined(MOD_API_EXTENSIONS)
	if (bInitialFounding) {
		if (eInitialReligion != NO_RELIGION) {
			// Spread an initial religion here if one was given
			GetCityReligions()->AdoptReligionFully(eInitialReligion);
		}

#if defined(MOD_RELIGION_LOCAL_RELIGIONS)
		else if (MOD_RELIGION_LOCAL_RELIGIONS) {
			// Spread a local religion here if one is active
			if(pReligions->HasCreatedReligion() && GC.getReligionInfo(pReligions->GetReligionCreatedByPlayer())->IsLocalReligion()) {
				GetCityReligions()->AdoptReligionFully(pReligions->GetReligionCreatedByPlayer());
			}
		}
#endif
	}
#endif

	// A new City might change our victory progress
	GET_TEAM(getTeam()).DoTestSmallAwards();

	DLLUI->setDirty(NationalBorders_DIRTY_BIT, true);

#if defined(MOD_GLOBAL_CITY_FOREST_BONUS)
	if (bClearedForest || bClearedJungle) 
	{
		int iProduction = 0;

		// Base value
		if (GET_PLAYER(getOwner()).GetAllFeatureProduction() > 0) 
		{
			iProduction = GET_PLAYER(getOwner()).GetAllFeatureProduction();
		}
		else if(bClearedForest)
		{
			iProduction = GC.getBuildInfo(eBuildRemoveForest)->getFeatureProduction(FEATURE_FOREST);
		}
		else if(bClearedJungle)
		{
			iProduction = GC.getBuildInfo(eBuildRemoveJungle)->getFeatureProduction(FEATURE_JUNGLE);
		}

		iProduction *= std::max(0, (GET_PLAYER(getOwner()).getFeatureProductionModifier() + 100));
		iProduction /= 100;

		iProduction *= GC.getGame().getGameSpeedInfo().getFeatureProductionPercent();
		iProduction /= 100;

		if (iProduction > 0) 
		{
			// Make the production higher than a "ring-1 chop"
			iProduction *= gCustomMods.getOption("GLOBAL_CITY_FOREST_BONUS_PERCENT", 125);
			iProduction /= 100;

			changeFeatureProduction(iProduction);
			if(bClearedForest)
			{
				//CUSTOMLOG("Founding of %s on a forest created %d initial production", getName().GetCString(), iProduction);
			}
			else if(bClearedJungle)
			{
				//CUSTOMLOG("Founding of %s on a jungle created %d initial production", getName().GetCString(), iProduction);
			}

			if (getOwner() == GC.getGame().getActivePlayer())
			{
					CvString strBuffer = GetLocalizedText("TXT_KEY_MISC_CLEARING_FEATURE_RESOURCE", GC.getFeatureInfo(eFeature)->GetTextKey(), iProduction, getNameKey());
					GC.GetEngineUserInterface()->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), strBuffer);
			}
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsReconquista() && bInitialFounding)
	{
		ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer(false);
		if(eReligion != NO_RELIGION)
		{
			GetCityReligions()->AdoptReligionFully(eReligion);
		}
		else
		{
			eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities();
			if(eReligion != NO_RELIGION)
			{
				GetCityReligions()->AdoptReligionFully(eReligion);
			}
		}
	}
	if(bInitialFounding)
	{		
		owningPlayer.doInstantYield(INSTANT_YIELD_TYPE_FOUND, false, NO_GREATPERSON, NO_BUILDING, 0, true, NO_PLAYER, NULL, false, this);
	}
	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes) iI;
		if(eYield == NO_YIELD)
			continue;

		UpdateSpecialReligionYields(eYield);
		UpdateCityYields(eYield);
	}
	if(bInitialFounding && owningPlayer.GetPlayerTraits()->GetStartingSpies() > 0 && owningPlayer.getNumCities() == 1)
	{
		CvPlayerEspionage* pEspionage = owningPlayer.GetEspionage();
		CvAssertMsg(pEspionage, "pEspionage is null! What's up with that?!");
		if(pEspionage)
		{
			int iNumSpies = owningPlayer.GetPlayerTraits()->GetStartingSpies();
			for(int i = 0; i < iNumSpies; i++)
			{
				pEspionage->CreateSpy();
			}
		}
	}
#endif

	owningPlayer.CalculateNetHappiness();

	updateEconomicValue();

	AI_init();

	if (GC.getGame().getGameTurn() == 0)
	{
		chooseProduction();	
	}
	if (!GET_PLAYER(getOwner()).isHuman())
	{
		AI_chooseProduction(false, false);
	}
}

//	--------------------------------------------------------------------------------
void CvCity::uninit()
{
	VALIDATE_OBJECT

	if(m_aaiBuildingSpecialistUpgradeProgresses)
	{
		for(int i=0; i < GC.getNumBuildingInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_aaiBuildingSpecialistUpgradeProgresses[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_aaiBuildingSpecialistUpgradeProgresses);

	if(m_ppaiResourceYieldChange)
	{
		for(int i=0; i < GC.getNumResourceInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiResourceYieldChange[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiResourceYieldChange);
#if defined(MOD_BALANCE_CORE)
	if(m_ppaiFeatureYieldChange || m_ppaiYieldPerXFeature || m_ppaiYieldPerXUnimprovedFeature || m_ppaiEventFeatureYield)
#else
	if(m_ppaiFeatureYieldChange)
#endif
	{
		for(int i=0; i < GC.getNumFeatureInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiFeatureYieldChange[i]);
#if defined(MOD_BALANCE_CORE)
			SAFE_DELETE_ARRAY(m_ppaiYieldPerXFeature[i]);
			SAFE_DELETE_ARRAY(m_ppaiYieldPerXUnimprovedFeature[i]);
			SAFE_DELETE_ARRAY(m_ppaiEventFeatureYield[i]);
#endif
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiFeatureYieldChange);
#if defined(MOD_BALANCE_CORE)
	SAFE_DELETE_ARRAY(m_ppaiYieldPerXFeature);
	SAFE_DELETE_ARRAY(m_ppaiYieldPerXUnimprovedFeature);
	SAFE_DELETE_ARRAY(m_ppaiEventFeatureYield);
#endif

#if defined(MOD_BALANCE_CORE)
	if(m_ppaiEventResourceYield)
	{
		for(int i=0; i < GC.getNumResourceInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiEventResourceYield[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiEventResourceYield);

	if(m_ppaiEventSpecialistYield)
	{
		for(int i=0; i < GC.getNumSpecialistInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiEventSpecialistYield[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiEventSpecialistYield);
	
	if(m_ppaiImprovementYieldChange || m_ppaiEventImprovementYield)
	{
		for(int i=0; i < GC.getNumImprovementInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiImprovementYieldChange[i]);
			SAFE_DELETE_ARRAY(m_ppaiEventImprovementYield[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiImprovementYieldChange);
	SAFE_DELETE_ARRAY(m_ppaiEventImprovementYield);
#endif
#if defined(MOD_BALANCE_CORE)
	if(m_ppaiEventTerrainYield || m_ppaiTerrainYieldChange || m_ppaiYieldPerXTerrainFromBuildings || m_ppaiYieldPerXTerrainFromReligion || m_ppaiYieldPerXTerrain)
#else
	if(m_ppaiTerrainYieldChange)
#endif
	{
		for(int i=0; i < GC.getNumTerrainInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiTerrainYieldChange[i]);
#if defined(MOD_BALANCE_CORE)
			SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrainFromBuildings[i]);
			SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrainFromReligion[i]);
			SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrain[i]);
			SAFE_DELETE_ARRAY(m_ppaiEventTerrainYield[i]);
#endif
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiTerrainYieldChange);
#if defined(MOD_BALANCE_CORE)
	SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrainFromBuildings);
	SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrainFromReligion);
	SAFE_DELETE_ARRAY(m_ppaiYieldPerXTerrain);
	SAFE_DELETE_ARRAY(m_ppaiEventTerrainYield);
#endif

#if defined(MOD_API_UNIFIED_YIELDS) && defined(MOD_API_PLOT_YIELDS)
	if(m_ppaiPlotYieldChange)
	{
		for(int i=0; i < GC.getNumPlotInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiPlotYieldChange[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiPlotYieldChange);
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	if(m_ppaiReligionBuildingYieldRateModifier || m_ppaiLocalBuildingClassYield || m_ppaiEventBuildingClassYield || m_ppaiEventBuildingClassYieldModifier)
	{
		for(int i=0; i < GC.getNumBuildingClassInfos(); i++)
		{
			SAFE_DELETE_ARRAY(m_ppaiReligionBuildingYieldRateModifier[i]);
			SAFE_DELETE_ARRAY(m_ppaiLocalBuildingClassYield[i]);
			SAFE_DELETE_ARRAY(m_ppaiEventBuildingClassYield[i]);
			SAFE_DELETE_ARRAY(m_ppaiEventBuildingClassYieldModifier[i]);
		}
	}
	SAFE_DELETE_ARRAY(m_ppaiReligionBuildingYieldRateModifier);
	SAFE_DELETE_ARRAY(m_ppaiLocalBuildingClassYield);
	SAFE_DELETE_ARRAY(m_ppaiEventBuildingClassYield);
	SAFE_DELETE_ARRAY(m_ppaiEventBuildingClassYieldModifier);

	m_ppaaiSpecialistExtraYield.clear();
#endif

	m_pCityBuildings->Uninit();
	m_pCityStrategyAI->Uninit();
	m_pCityCitizens->Uninit();
	m_pCityReligions->Uninit();
	m_pEmphases->Uninit();
	m_pCityEspionage->Uninit();

	m_orderQueue.clear();
}

//	--------------------------------------------------------------------------------
// FUNCTION: reset()
// Initializes data members that are serialized.
void CvCity::reset(int iID, PlayerTypes eOwner, int iX, int iY, bool bConstructorCall)
{
	VALIDATE_OBJECT
	m_syncArchive.reset();

	int iI;

	//--------------------------------
	// Uninit class
	uninit();

	m_iID = iID;
	m_iX = iX;
	m_iY = iY;
	m_iRallyX = INVALID_PLOT_COORD;
	m_iRallyY = INVALID_PLOT_COORD;
	m_iGameTurnFounded = 0;
	m_iGameTurnAcquired = 0;
	m_iPopulation = 0;
#if defined(MOD_BALANCE_CORE)
	m_iAdditionalFood = 0;
#endif
	m_iHighestPopulation = 0;
	m_iExtraHitPoints = 0;
	m_iNumGreatPeople = 0;
	m_iBaseGreatPeopleRate = 0;
	m_iGreatPeopleRateModifier = 0;
	m_iJONSCultureStored = 0;
	m_iJONSCultureLevel = 0;
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	m_iJONSCulturePerTurnFromBuildings = 0;
#endif
	m_iJONSCulturePerTurnFromPolicies = 0;
	m_iJONSCulturePerTurnFromSpecialists = 0;
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	m_iJONSCulturePerTurnFromReligion = 0;
#endif
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	m_iFaithPerTurnFromBuildings = 0;
#endif
	m_iFaithPerTurnFromPolicies = 0;
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	m_iFaithPerTurnFromReligion = 0;
#endif
	m_iCultureRateModifier = 0;
	m_iNumWorldWonders = 0;
	m_iNumTeamWonders = 0;
	m_iNumNationalWonders = 0;
	m_iWonderProductionModifier = 0;
	m_iCapturePlunderModifier = 0;
	m_iPlotCultureCostModifier = 0;
	m_iPlotBuyCostModifier = 0;
#if defined(MOD_BUILDINGS_CITY_WORKING)
	m_iCityWorkingChange = 0;
#endif
	m_iMaintenance = 0;
	m_iHealRate = 0;
	m_iEspionageModifier = 0;
#if defined(MOD_RELIGION_CONVERSION_MODIFIERS)
	m_iConversionModifier = 0;
#endif
	m_iNoOccupiedUnhappinessCount = 0;
	m_iFood = 0;
	m_iFoodKept = 0;
	m_iMaxFoodKeptPercent = 0;
	m_iOverflowProduction = 0;
	m_iFeatureProduction = 0;
	m_iMilitaryProductionModifier = 0;
	m_iSpaceProductionModifier = 0;
	m_iFreeExperience = 0;
	m_iCurrAirlift = 0; // unused
	m_iMaxAirUnits = GC.getBASE_CITY_AIR_STACKING();
	m_iAirModifier = 0; // unused
	m_iNukeModifier = 0;
	m_iTradeRouteRecipientBonus = 0;
	m_iTradeRouteSeaGoldBonus = 0;
	m_iTradeRouteLandGoldBonus = 0;
	m_iNumTradeRouteBonus = 0;
	m_iTradeRouteTargetBonus = 0;
	m_iCultureUpdateTimer = 0;
	m_iCitySizeBoost = 0;
	m_iSpecialistFreeExperience = 0;
	m_iStrengthValue = 0;
	m_iDamage = 0;
	m_iThreatValue = 0;
	m_hGarrison = -1;
	m_iResourceDemanded = -1;
	m_iWeLoveTheKingDayCounter = 0;
	m_iLastTurnGarrisonAssigned = -1;
	m_iThingsProduced = 0;
	m_iDemandResourceCounter = 0;
	m_iResistanceTurns = 0;
	m_iRazingTurns = 0;
	m_iCountExtraLuxuries = 0;
	m_iCheapestPlotInfluenceDistance = 0;
	m_unitBeingBuiltForOperation.Invalidate();
	m_hGarrisonOverride = -1;

	m_bNeverLost = true;
	m_bDrafted = false;
	m_bAirliftTargeted = false;   // unused
	m_bProductionAutomated = false;
	m_bLayoutDirty = false;
	m_bMadeAttack = false;
	m_bOccupied = false;
	m_bPuppet = false;
	m_bIgnoreCityForHappiness = false;
	m_bIndustrialRouteToCapital = false;
	m_bFeatureSurrounded = false;
	m_bOwedCultureBuilding = false;
#if defined(MOD_BUGFIX_FREE_FOOD_BUILDING)
	m_bOwedFoodBuilding = false;
#endif

	m_eOwner = eOwner;
	m_ePreviousOwner = NO_PLAYER;
	m_eOriginalOwner = eOwner;
	m_ePlayersReligion = NO_PLAYER;


	m_aiSeaPlotYield.resize(NUM_YIELD_TYPES);
	m_aiRiverPlotYield.resize(NUM_YIELD_TYPES);
	m_aiSeaResourceYield.resize(NUM_YIELD_TYPES);
	m_aiLakePlotYield.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRateFromTerrain.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRateFromBuildings.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRateFromSpecialists.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRateFromMisc.resize(NUM_YIELD_TYPES);
#if defined(MOD_DIPLOMACY_CITYSTATES)
	m_aiBaseYieldRateFromLeague.resize(NUM_YIELD_TYPES);
	m_iTotalScienceyAid = 0;
	m_iTotalArtsyAid = 0;
	m_iTotalGreatWorkAid = 0;
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
	m_iChangePovertyUnhappiness = 0;
	m_iChangeDefenseUnhappiness = 0;
	m_iChangeUnculturedUnhappiness = 0;
	m_iChangeIlliteracyUnhappiness = 0;
	m_iChangeMinorityUnhappiness = 0;
	m_iEventHappiness = 0;
	m_iCityEventCooldown = 0;
#endif
#if defined(MOD_DIPLOMACY_CITYSTATES) || defined(MOD_BALANCE_CORE)
	m_aiChangeGrowthExtraYield.resize(NUM_YIELD_TYPES);
#endif
#if defined(MOD_BALANCE_CORE)
	m_iBorderObstacleWater = 0;
	m_iBorderObstacleCity = 0;
	m_iNumNearbyMountains = 0;
	m_iLocalUnhappinessMod = 0;
	m_iTradePriorityLand = 0;
	m_iTradeRouteSeaDistanceModifier = 0;
	m_iTradeRouteLandDistanceModifier = 0;
	m_iTradePrioritySea = 0;
	m_iNearbySettlerValue = 0;
	m_iThreatRank = 0;
	m_iUnitPurchaseCooldown = 0;
	m_iBuildingPurchaseCooldown = 0;
	m_iReligiousTradeModifier = 0;
	m_iFreeBuildingTradeTargetCity = -1;
	m_iBaseTourism = 0;
	m_iBaseTourismBeforeModifiers = 0;
	m_aiSpecialistRateModifier.resize(GC.getNumSpecialistInfos());
	m_aiChangeYieldFromVictory.resize(NUM_YIELD_TYPES);
	m_aiYieldFromKnownPantheons.resize(NUM_YIELD_TYPES);
	m_aiGoldenAgeYieldMod.resize(NUM_YIELD_TYPES);
	m_aiYieldFromWLTKD.resize(NUM_YIELD_TYPES);
	m_aiYieldFromConstruction.resize(NUM_YIELD_TYPES);
	m_aiYieldFromTech.resize(NUM_YIELD_TYPES);
	m_aiYieldFromBirth.resize(NUM_YIELD_TYPES);
	m_aiYieldFromUnitProduction.resize(NUM_YIELD_TYPES);
	m_aiYieldFromBorderGrowth.resize(NUM_YIELD_TYPES);
	m_aiYieldFromPolicyUnlock.resize(NUM_YIELD_TYPES);
	m_aiYieldFromPurchase.resize(NUM_YIELD_TYPES);
	m_aiYieldFromUnitLevelUp.resize(NUM_YIELD_TYPES);
	m_aiYieldPerAlly.resize(NUM_YIELD_TYPES);
	m_aiYieldPerFriend.resize(NUM_YIELD_TYPES);
	m_aiScienceFromYield.resize(NUM_YIELD_TYPES);
	m_aiBuildingScienceFromYield.resize(NUM_YIELD_TYPES);
	m_aiThemingYieldBonus.resize(NUM_YIELD_TYPES);
	m_aiNumTimesOwned.resize(REALLY_MAX_PLAYERS);
	m_aiStaticCityYield.resize(NUM_YIELD_TYPES);
#endif
#if defined(MOD_BALANCE_CORE)
	m_iBlockBuildingDestruction = 0;
	m_iBlockWWDestruction = 0;
	m_iBlockUDestruction = 0;
	m_iBlockGPDestruction = 0;
	m_iBlockRebellion = 0;
	m_iBlockUnrest = 0;
	m_iBlockScience = 0;
	m_iBlockGold = 0;
	m_iExtraBuildingMaintenance = 0;
	m_iLandTourismBonus = 0;
	m_iSeaTourismBonus = 0;
	m_iAlwaysHeal = 0;
	m_bIsBastion = false;
	m_bNoWarmonger = false;
#endif
#if defined(MOD_BALANCE_CORE_SPIES)
	m_iCityRank = 0;
	m_iTurnsSinceRankAnnouncement = 0;
	m_aiEconomicValue.resize(MAX_MAJOR_CIVS);
	for (iI = 0; iI < MAX_MAJOR_CIVS; iI++)
	{
		m_aiEconomicValue.setAt(iI, 0);
	}
#endif
	m_aiBaseYieldRateFromReligion.resize(NUM_YIELD_TYPES);
#if defined(MOD_BALANCE_CORE)	
	m_aiBaseYieldRateFromCSFriendship.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRateFromCSAlliance.resize(NUM_YIELD_TYPES);
#endif
	m_aiYieldPerPop.resize(NUM_YIELD_TYPES);
	m_aiYieldPerReligion.resize(NUM_YIELD_TYPES);
	m_aiYieldRateModifier.resize(NUM_YIELD_TYPES);
	m_aiPowerYieldRateModifier.resize(NUM_YIELD_TYPES);
	m_aiResourceYieldRateModifier.resize(NUM_YIELD_TYPES);
	m_aiExtraSpecialistYield.resize(NUM_YIELD_TYPES);
	m_aiProductionToYieldModifier.resize(NUM_YIELD_TYPES);
	m_aiEventCityYield.resize(NUM_YIELD_TYPES);
	for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		m_aiSeaPlotYield.setAt(iI, 0);
		m_aiRiverPlotYield.setAt(iI, 0);
		m_aiLakePlotYield.setAt(iI, 0);
		m_aiSeaResourceYield.setAt(iI, 0);
		m_aiBaseYieldRateFromTerrain.setAt(iI, 0);
		m_aiBaseYieldRateFromBuildings.setAt(iI, 0);
		m_aiBaseYieldRateFromSpecialists.setAt(iI, 0);
		m_aiBaseYieldRateFromMisc.setAt(iI, 0);
#if defined(MOD_DIPLOMACY_CITYSTATES)
		m_aiBaseYieldRateFromLeague.setAt(iI, 0);
#endif
#if defined(MOD_DIPLOMACY_CITYSTATES) || defined(MOD_BALANCE_CORE)
		m_aiChangeGrowthExtraYield.setAt(iI, 0);
#endif
#if defined(MOD_BALANCE_CORE)
		m_aiYieldFromKnownPantheons.setAt(iI, 0);
		m_aiChangeYieldFromVictory.setAt(iI, 0);
		m_aiGoldenAgeYieldMod.setAt(iI, 0);
		m_aiYieldFromWLTKD.setAt(iI, 0);
		m_aiYieldFromConstruction.setAt(iI, 0);
		m_aiYieldFromTech.setAt(iI, 0);
		m_aiYieldFromBirth.setAt(iI, 0);
		m_aiYieldFromUnitProduction.setAt(iI, 0);
		m_aiYieldFromBorderGrowth.setAt(iI, 0);
		m_aiYieldFromPolicyUnlock.setAt(iI, 0);
		m_aiYieldFromPurchase.setAt(iI, 0);
		m_aiYieldFromUnitLevelUp.setAt(iI, 0);
		m_aiYieldPerAlly.setAt(iI, 0);
		m_aiYieldPerFriend.setAt(iI, 0);
		m_aiScienceFromYield.setAt(iI, 0);
		m_aiBuildingScienceFromYield.setAt(iI, 0);
		m_aiThemingYieldBonus.setAt(iI, 0);
		m_aiEventCityYield.setAt(iI, 0);
#endif
		m_aiBaseYieldRateFromReligion.setAt(iI, 0);
#if defined(MOD_BALANCE_CORE)
		m_aiBaseYieldRateFromCSFriendship.setAt(iI, 0);
		m_aiBaseYieldRateFromCSAlliance.setAt(iI, 0);
		m_aiStaticCityYield.setAt(iI, 0);
#endif
		m_aiYieldPerPop.setAt(iI, 0);
		m_aiYieldPerReligion.setAt(iI, 0);
		m_aiYieldRateModifier.setAt(iI, 0);
		m_aiPowerYieldRateModifier.setAt(iI, 0);
		m_aiResourceYieldRateModifier.setAt(iI, 0);
		m_aiExtraSpecialistYield.setAt(iI, 0);
		m_aiProductionToYieldModifier.setAt(iI, 0);
	}
#if defined(MOD_BALANCE_CORE_EVENTS)
	m_abEventChoiceFired.resize(GC.getNumCityEventChoiceInfos());
	m_aiEventChoiceDuration.resize(GC.getNumCityEventChoiceInfos());
	m_abEventChoiceActive.resize(GC.getNumCityEventChoiceInfos());
	for(iI = 0; iI < GC.getNumCityEventChoiceInfos(); iI++)
	{
		m_aiEventChoiceDuration.setAt(iI, 0);
		m_abEventChoiceFired.setAt(iI, false);
		m_abEventChoiceActive.setAt(iI, false);
	}
	m_abEventFired.resize(GC.getNumCityEventInfos());
	m_abEventActive.resize(GC.getNumCityEventInfos());
	m_aiEventIncrement.resize(GC.getNumCityEventInfos());
	m_aiEventCooldown.resize(GC.getNumCityEventInfos());
	for(iI = 0; iI < GC.getNumCityEventInfos(); iI++)
	{
		m_abEventFired.setAt(iI, false);
		m_abEventActive.setAt(iI, false);
		m_aiEventIncrement.setAt(iI, 0);
		m_aiEventCooldown.setAt(iI, 0);
	}
#endif
	m_aiDomainFreeExperience.resize(NUM_DOMAIN_TYPES);
	m_aiDomainProductionModifier.resize(NUM_DOMAIN_TYPES);
	for(iI = 0; iI < NUM_DOMAIN_TYPES; iI++)
	{
		m_aiDomainFreeExperience.setAt(iI, 0);
		m_aiDomainProductionModifier.setAt(iI, 0);
	}

	m_abEverOwned.resize(REALLY_MAX_PLAYERS);
#if defined(MOD_BALANCE_CORE)
	m_abIsBestForWonder.resize(GC.getNumBuildingClassInfos());
	m_abIsPurchased.resize(GC.getNumBuildingClassInfos());
	for(iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
	{
		m_abIsPurchased.setAt(iI, false);
		m_abIsBestForWonder.setAt(iI, false);
	}
	m_abTraded.resize(REALLY_MAX_PLAYERS);
#endif
	for(iI = 0; iI < REALLY_MAX_PLAYERS; iI++)
	{
		m_abEverOwned.setAt(iI, false);
#if defined(MOD_BALANCE_CORE)
		m_abTraded.setAt(iI, false);
		m_aiNumTimesOwned.setAt(iI, false);
#endif
	}
#if defined(MOD_BALANCE_CORE)
	m_abPaidAdoptionBonus.resize(GC.getNumReligionInfos());
	for(iI = 0; iI < GC.getNumReligionInfos(); iI++)
	{
		m_abPaidAdoptionBonus.setAt(iI, false);
	}
#endif

	m_abRevealed.resize(REALLY_MAX_TEAMS);
	for(iI = 0; iI < REALLY_MAX_TEAMS; iI++)
	{
		m_abRevealed.setAt(iI, false);
	}

	m_strName = "";
	m_strNameIAmNotSupposedToBeUsedAnyMoreBecauseThisShouldNotBeCheckedAndWeNeedToPreserveSaveGameCompatibility = "";
	m_strScriptData = "";

	m_bPopulationRankValid = false;
	m_iPopulationRank = -1;
	m_iBaseHappinessFromBuildings = 0;
	m_iUnmoddedHappinessFromBuildings = 0;
	m_bRouteToCapitalConnectedLastTurn = false;
	m_bRouteToCapitalConnectedThisTurn = false;

	m_abBaseYieldRankValid.resize(NUM_YIELD_TYPES);
	m_abYieldRankValid.resize(NUM_YIELD_TYPES);
	m_aiBaseYieldRank.resize(NUM_YIELD_TYPES);
	m_aiYieldRank.resize(NUM_YIELD_TYPES);
#if defined(MOD_BALANCE_CORE)
	m_iComboUnhappiness = 0;
	m_iPillagedPlots = 0;
	m_iGrowthFromTourism = 0;
	m_iBuildingClassHappinessFromReligion = 0;
	m_aiYieldChangeFromCorporationFranchises.resize(NUM_YIELD_TYPES);
#endif
	for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		m_abBaseYieldRankValid.setAt(iI, false);
		m_abYieldRankValid.setAt(iI, false);
		m_aiBaseYieldRank.setAt(iI, -1);
		m_aiYieldRank.setAt(iI, -1);
#if defined(MOD_BALANCE_CORE)
		m_aiYieldChangeFromCorporationFranchises.setAt(iI, 0);
#endif
	}
#if defined(MOD_BALANCE_CORE)
	m_abOwedChosenBuilding.resize(GC.getNumBuildingClassInfos());
	m_abBuildingInvestment.resize(GC.getNumBuildingClassInfos());
	m_abUnitInvestment.resize(GC.getNumUnitClassInfos());
	m_abBuildingConstructed.resize(GC.getNumBuildingClassInfos());
	for(int iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
	{
		m_abOwedChosenBuilding.setAt(iI, false);
		m_abBuildingInvestment.setAt(iI, false);
		m_abBuildingConstructed.setAt(iI, false);
	}
	for(int iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
	{
		m_abUnitInvestment.setAt(iI, false);
	}
#endif

	if(!bConstructorCall)
	{
		int iNumResources = GC.getNumResourceInfos();
		CvAssertMsg((0 < iNumResources),  "GC.getNumResourceInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiNoResource.clear();
		m_paiNoResource.resize(iNumResources);
		m_paiFreeResource.clear();
		m_paiFreeResource.resize(iNumResources);
		m_paiNumResourcesLocal.clear();
		m_paiNumResourcesLocal.resize(iNumResources);
#if defined(MOD_BALANCE_CORE)
		m_aiResourceQuantityPerXFranchises.clear();
		m_aiResourceQuantityPerXFranchises.resize(iNumResources);
#endif
		for(iI = 0; iI < iNumResources; iI++)
		{
			m_paiNoResource.setAt(iI, 0);
			m_paiFreeResource.setAt(iI, 0);
			m_paiNumResourcesLocal.setAt(iI, 0);
#if defined(MOD_BALANCE_CORE)
			m_aiResourceQuantityPerXFranchises.setAt(iI, 0);
#endif
		}

		int iNumProjectInfos = GC.getNumProjectInfos();
		m_paiProjectProduction.clear();
		m_paiProjectProduction.resize(iNumProjectInfos);
		for(iI = 0; iI < iNumProjectInfos; iI++)
		{
			m_paiProjectProduction.setAt(iI, 0);
		}

		int iNumSpecialistInfos = GC.getNumSpecialistInfos();
		m_paiSpecialistProduction.clear();
		m_paiSpecialistProduction.resize(iNumSpecialistInfos);
		for(iI = 0; iI < GC.getNumSpecialistInfos(); iI++)
		{
			m_paiSpecialistProduction.setAt(iI, 0);
#if defined(MOD_BALANCE_CORE)
			m_aiSpecialistRateModifier.setAt(iI, 0);
#endif
		}

		m_pCityBuildings->Init(GC.GetGameBuildings(), this);

		int iNumUnitInfos = GC.getNumUnitInfos();
		CvAssertMsg((0 < iNumUnitInfos),  "GC.getNumUnitInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiUnitProduction.clear();
		m_paiUnitProduction.resize(iNumUnitInfos);
		m_paiUnitProductionTime.clear();
		m_paiUnitProductionTime.resize(iNumUnitInfos);
		for(iI = 0; iI < iNumUnitInfos; iI++)
		{
			m_paiUnitProduction.setAt(iI, 0);
			m_paiUnitProductionTime.setAt(iI, 0);
		}

		CvAssertMsg((0 < iNumSpecialistInfos),  "GC.getNumSpecialistInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiSpecialistCount.clear();
		m_paiSpecialistCount.resize(iNumSpecialistInfos);
		m_paiMaxSpecialistCount.clear();
		m_paiMaxSpecialistCount.resize(iNumSpecialistInfos);
		m_paiForceSpecialistCount.clear();
		m_paiForceSpecialistCount.resize(iNumSpecialistInfos);
		m_paiFreeSpecialistCount.clear();
		m_paiFreeSpecialistCount.resize(iNumSpecialistInfos);

		for(iI = 0; iI < iNumSpecialistInfos; iI++)
		{
			m_paiSpecialistCount.setAt(iI, 0);
			m_paiMaxSpecialistCount.setAt(iI, 0);
			m_paiForceSpecialistCount.setAt(iI, 0);
			m_paiFreeSpecialistCount.setAt(iI, 0);
		}

		int iNumImprovementInfos = GC.getNumImprovementInfos();
		CvAssertMsg((0 < iNumImprovementInfos),  "GC.getNumImprovementInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiImprovementFreeSpecialists.clear();
		m_paiImprovementFreeSpecialists.resize(iNumImprovementInfos);
		for(iI = 0; iI < iNumImprovementInfos; iI++)
		{
			m_paiImprovementFreeSpecialists.setAt(iI, 0);
		}

		int iNumUnitCombatClassInfos = GC.getNumUnitCombatClassInfos();
		CvAssertMsg((0 < iNumUnitCombatClassInfos),  "GC.getNumUnitCombatClassInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiUnitCombatFreeExperience.clear();
		m_paiUnitCombatFreeExperience.resize(iNumUnitCombatClassInfos);
		m_paiUnitCombatProductionModifier.clear();
		m_paiUnitCombatProductionModifier.resize(iNumUnitCombatClassInfos);
		for(iI = 0; iI < iNumUnitCombatClassInfos; iI++)
		{
			m_paiUnitCombatFreeExperience.setAt(iI, 0);
			m_paiUnitCombatProductionModifier.setAt(iI, 0);
		}

		int iNumPromotionInfos = GC.getNumPromotionInfos();
		CvAssertMsg((0 < iNumPromotionInfos),  "GC.getNumPromotionInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiFreePromotionCount.clear();
		m_paiFreePromotionCount.resize(iNumPromotionInfos);
		for(iI = 0; iI < iNumPromotionInfos; iI++)
		{
			m_paiFreePromotionCount.setAt(iI, 0);
		}
#if defined(MOD_BALANCE_CORE_POLICIES)
		int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
		CvAssertMsg((0 < iNumBuildingClassInfos),  "GC.getNumBuildingClassInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiBuildingClassCulture.clear();
		m_paiBuildingClassCulture.resize(iNumBuildingClassInfos);
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			m_paiBuildingClassCulture.setAt(iI, 0);
		}
		
		int iNumHurryInfos = GC.getNumHurryInfos();
		CvAssertMsg((0 < iNumHurryInfos),  "GC.getNumHurryInfos() is not greater than zero but an array is being allocated in CvCity::reset");
		m_paiHurryModifier.clear();
		m_paiHurryModifier.resize(iNumHurryInfos);
		for(iI = 0; iI < iNumHurryInfos; iI++)
		{
			m_paiHurryModifier.setAt(iI, 0);
		}

		Firaxis::Array< int, NUM_YIELD_TYPES > yield;
		for( unsigned int j = 0; j < NUM_YIELD_TYPES; ++j )
		{
			yield[j] = 0;
		}

		m_ppaaiSpecialistExtraYield.clear();
		m_ppaaiSpecialistExtraYield.resize(GC.getNumSpecialistInfos());
		for( unsigned int i = 0; i < m_ppaaiSpecialistExtraYield.size(); ++i )
		{
			m_ppaaiSpecialistExtraYield.setAt( i, yield );
		}
#endif

		int iJ;

		int iNumBuildingInfos = GC.getNumBuildingInfos();
		int iMAX_SPECIALISTS_FROM_BUILDING = GC.getMAX_SPECIALISTS_FROM_BUILDING();
		CvAssertMsg(m_aaiBuildingSpecialistUpgradeProgresses==NULL, "about to leak memory, CvCity::m_aaiBuildingSpecialistUpgradeProgresses");
		m_aaiBuildingSpecialistUpgradeProgresses = FNEW(int*[iNumBuildingInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumBuildingInfos; iI++)
		{
			m_aaiBuildingSpecialistUpgradeProgresses[iI] = FNEW(int[iMAX_SPECIALISTS_FROM_BUILDING], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < iMAX_SPECIALISTS_FROM_BUILDING; iJ++)
			{
				m_aaiBuildingSpecialistUpgradeProgresses[iI][iJ] = -1;
			}
		}

		int iNumResourceInfos = GC.getNumResourceInfos();
		CvAssertMsg(m_ppaiResourceYieldChange==NULL, "about to leak memory, CvCity::m_ppaiResourceYieldChange");
		m_ppaiResourceYieldChange = FNEW(int*[iNumResourceInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumResourceInfos; iI++)
		{
			m_ppaiResourceYieldChange[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiResourceYieldChange[iI][iJ] = 0;
			}
		}

		int iNumFeatureInfos = GC.getNumFeatureInfos();
		CvAssertMsg(m_ppaiFeatureYieldChange==NULL, "about to leak memory, CvCity::m_ppaiFeatureYieldChange");
		m_ppaiFeatureYieldChange = FNEW(int*[iNumFeatureInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumFeatureInfos; iI++)
		{
			m_ppaiFeatureYieldChange[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiFeatureYieldChange[iI][iJ] = 0;
			}
		}
#if defined(MOD_BALANCE_CORE)
		CvAssertMsg(m_ppaiImprovementYieldChange==NULL, "about to leak memory, CvCity::m_ppaiImprovementYieldChange");
		m_ppaiImprovementYieldChange = FNEW(int*[iNumImprovementInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumImprovementInfos; iI++)
		{
			m_ppaiImprovementYieldChange[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiImprovementYieldChange[iI][iJ] = 0;
			}
		}
#endif
		int iNumTerrainInfos = GC.getNumTerrainInfos();
		CvAssertMsg(m_ppaiTerrainYieldChange==NULL, "about to leak memory, CvCity::m_ppaiTerrainYieldChange");
		m_ppaiTerrainYieldChange = FNEW(int*[iNumTerrainInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_ppaiTerrainYieldChange[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiTerrainYieldChange[iI][iJ] = 0;
			}
		}
#if defined(MOD_BALANCE_CORE)
		m_paiNumTerrainWorked.clear();
		m_paiNumFeaturelessTerrainWorked.clear();
		m_paiNumFeaturelessTerrainWorked.resize(iNumTerrainInfos);
		m_paiNumTerrainWorked.resize(iNumTerrainInfos);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_paiNumTerrainWorked.setAt(iI, 0);
			m_paiNumFeaturelessTerrainWorked.setAt(iI, 0);
		}

		m_paiNumFeatureWorked.clear();
		m_paiNumFeatureWorked.resize(iNumFeatureInfos);
		for(iI = 0; iI < iNumFeatureInfos; iI++)
		{
			m_paiNumFeatureWorked.setAt(iI, 0);
		}

		m_paiNumResourceWorked.clear();
		m_paiNumResourceWorked.resize(iNumResourceInfos);
		for(iI = 0; iI < iNumResourceInfos; iI++)
		{
			m_paiNumResourceWorked.setAt(iI, 0);
		}

		m_paiNumImprovementWorked.clear();
		m_paiNumImprovementWorked.resize(iNumImprovementInfos);
		for(iI = 0; iI < iNumImprovementInfos; iI++)
		{
			m_paiNumImprovementWorked.setAt(iI, 0);
		}
#endif
#if defined(MOD_BALANCE_CORE)
		CvAssertMsg(m_ppaiYieldPerXTerrainFromBuildings==NULL, "about to leak memory, CvCity::m_ppaiYieldPerXTerrainFromBuildings");
		m_ppaiYieldPerXTerrainFromBuildings = FNEW(int*[iNumTerrainInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_ppaiYieldPerXTerrainFromBuildings[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiYieldPerXTerrainFromBuildings[iI][iJ] = 0;
			}
		}

		CvAssertMsg(m_ppaiYieldPerXTerrainFromReligion==NULL, "about to leak memory, CvCity::m_ppaiYieldPerXTerrainFromReligion");
		m_ppaiYieldPerXTerrainFromReligion = FNEW(int*[iNumTerrainInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_ppaiYieldPerXTerrainFromReligion[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiYieldPerXTerrainFromReligion[iI][iJ] = 0;
			}
		}

		CvAssertMsg(m_ppaiYieldPerXTerrain==NULL, "about to leak memory, CvCity::m_ppaiYieldPerXTerrain");
		m_ppaiYieldPerXTerrain = FNEW(int*[iNumTerrainInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_ppaiYieldPerXTerrain[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiYieldPerXTerrain[iI][iJ] = 0;
			}
		}

		CvAssertMsg(m_ppaiYieldPerXFeature==NULL, "about to leak memory, CvCity::m_ppaiYieldPerXFeature");
		m_ppaiYieldPerXFeature = FNEW(int*[iNumFeatureInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumFeatureInfos; iI++)
		{
			m_ppaiYieldPerXFeature[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiYieldPerXFeature[iI][iJ] = 0;
			}
		}

		CvAssertMsg(m_ppaiYieldPerXUnimprovedFeature==NULL, "about to leak memory, CvCity::m_ppaiYieldPerXUnimprovedFeature");
		m_ppaiYieldPerXUnimprovedFeature = FNEW(int*[iNumFeatureInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumFeatureInfos; iI++)
		{
			m_ppaiYieldPerXUnimprovedFeature[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiYieldPerXUnimprovedFeature[iI][iJ] = 0;
			}
		}
#endif

#if defined(MOD_API_UNIFIED_YIELDS) && defined(MOD_API_PLOT_YIELDS)
		int iNumPlotInfos = GC.getNumPlotInfos();
		CvAssertMsg(m_ppaiPlotYieldChange==NULL, "about to leak memory, CvCity::m_ppaiPlotYieldChange");
		m_ppaiPlotYieldChange = FNEW(int*[iNumPlotInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumPlotInfos; iI++)
		{
			m_ppaiPlotYieldChange[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiPlotYieldChange[iI][iJ] = 0;
			}
		}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
		CvAssertMsg(m_ppaiReligionBuildingYieldRateModifier==NULL, "about to leak memory, CvCity::m_ppaiReligionBuildingYieldRateModifier");
		m_ppaiReligionBuildingYieldRateModifier = FNEW(int*[iNumBuildingClassInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			m_ppaiReligionBuildingYieldRateModifier[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiReligionBuildingYieldRateModifier[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiLocalBuildingClassYield==NULL, "about to leak memory, CvCity::m_ppaiLocalBuildingClassYield");
		m_ppaiLocalBuildingClassYield = FNEW(int*[iNumBuildingClassInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			m_ppaiLocalBuildingClassYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiLocalBuildingClassYield[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiEventBuildingClassYield==NULL, "about to leak memory, CvCity::m_ppaiEventBuildingClassYield");
		m_ppaiEventBuildingClassYield = FNEW(int*[iNumBuildingClassInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			m_ppaiEventBuildingClassYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventBuildingClassYield[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiEventBuildingClassYieldModifier==NULL, "about to leak memory, CvCity::m_ppaiEventBuildingClassYieldModifier");
		m_ppaiEventBuildingClassYieldModifier = FNEW(int*[iNumBuildingClassInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			m_ppaiEventBuildingClassYieldModifier[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventBuildingClassYieldModifier[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiEventImprovementYield==NULL, "about to leak memory, CvCity::m_ppaiEventImprovementYield");
		m_ppaiEventImprovementYield = FNEW(int*[iNumImprovementInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumImprovementInfos; iI++)
		{
			m_ppaiEventImprovementYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventImprovementYield[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiEventResourceYield==NULL, "about to leak memory, CvCity::m_ppaiEventResourceYield");
		m_ppaiEventResourceYield = FNEW(int*[iNumResourceInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumResourceInfos; iI++)
		{
			m_ppaiEventResourceYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventResourceYield[iI][iJ] = 0;
			}
		}

		CvAssertMsg(m_ppaiEventSpecialistYield==NULL, "about to leak memory, CvCity::m_ppaiEventSpecialistYield");
		m_ppaiEventSpecialistYield = FNEW(int*[iNumSpecialistInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumSpecialistInfos; iI++)
		{
			m_ppaiEventSpecialistYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventSpecialistYield[iI][iJ] = 0;
			}
		}
		
		CvAssertMsg(m_ppaiEventTerrainYield==NULL, "about to leak memory, CvCity::m_ppaiEventTerrainYield");
		m_ppaiEventTerrainYield = FNEW(int*[iNumTerrainInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumTerrainInfos; iI++)
		{
			m_ppaiEventTerrainYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventTerrainYield[iI][iJ] = 0;
			}
		}
		CvAssertMsg(m_ppaiEventFeatureYield==NULL, "about to leak memory, CvCity::m_ppaiEventFeatureYield");
		m_ppaiEventFeatureYield = FNEW(int*[iNumFeatureInfos], c_eCiv5GameplayDLL, 0);
		for(iI = 0; iI < iNumFeatureInfos; iI++)
		{
			m_ppaiEventFeatureYield[iI] = FNEW(int[NUM_YIELD_TYPES], c_eCiv5GameplayDLL, 0);
			for(iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				m_ppaiEventFeatureYield[iI][iJ] = 0;
			}
		}
#endif
		
	}

	if(!bConstructorCall)
	{
		// Set up AI and hook it up to the flavor manager
		m_pCityStrategyAI->Init(GC.GetGameAICityStrategies(), this, true);
		if(m_eOwner != NO_PLAYER)
		{
			GET_PLAYER(getOwner()).GetFlavorManager()->AddFlavorRecipient(m_pCityStrategyAI);
			m_pCityStrategyAI->SetDefaultSpecialization(GET_PLAYER(getOwner()).GetCitySpecializationAI()->GetNextSpecializationDesired());
		}

		m_pCityCitizens->Init(this);
		m_pCityReligions->Init(this);
		m_pEmphases->Init(GC.GetGameEmphases(), this);
		m_pCityEspionage->Init(this);
		m_pCityCulture->Init(this);

		AI_reset();
	}
}


//////////////////////////////////////
// graphical only setup
//////////////////////////////////////
void CvCity::setupGraphical()
{
	VALIDATE_OBJECT
	if(!GC.IsGraphicsInitialized())
	{
		return;
	}

	CvPlayer& player = GET_PLAYER(getOwner());
	EraTypes eCurrentEra =(EraTypes) player.GetCurrentEra();

	auto_ptr<ICvCity1> pkDllCity(new CvDllCity(this));
	gDLL->GameplayCityCreated(pkDllCity.get(), eCurrentEra);
	gDLL->GameplayCitySetDamage(pkDllCity.get(), getDamage(), 0);

	// setup the wonders
	setupWonderGraphics();

	// setup any special buildings
	setupBuildingGraphics();

	// setup the spaceship
	setupSpaceshipGraphics();

	setLayoutDirty(true);
}

//	--------------------------------------------------------------------------------
void CvCity::setupWonderGraphics()
{
	VALIDATE_OBJECT
	PlayerTypes ePlayerID = getOwner();
	for(int eBuildingType = 0; eBuildingType < GC.getNumBuildingInfos(); eBuildingType++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(eBuildingType);
		CvBuildingEntry* buildingInfo = GC.getBuildingInfo(eBuilding);

		//Only work with valid buildings.
		if(buildingInfo == NULL)
			continue;

		// if this building exists
		int iExists = m_pCityBuildings->GetNumRealBuilding(eBuilding);
		int iPreferredPosition = buildingInfo->GetPreferredDisplayPosition();
		if(iPreferredPosition > 0)
		{
			if(iExists > 0)
			{
				// display the wonder
				auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
				gDLL->GameplayWonderCreated(ePlayerID, pDllPlot.get(), eBuilding, 1);
			}
			else
			{
				if (isWorldWonderClass(buildingInfo->GetBuildingClassInfo()))
				{
					bool bShowHalfBuilt = false;
					// Are we are constructing it?
					if (eBuilding == getProductionBuilding())
					{
						bShowHalfBuilt = true;
					}
					else
					{
						// Is it part of an international project?
						LeagueProjectTypes eThisBuildingProject = NO_LEAGUE_PROJECT;
						for (int i = 0; i < GC.getNumLeagueProjectInfos(); i++)
						{
							LeagueProjectTypes eProject = (LeagueProjectTypes)i;
							CvLeagueProjectEntry* pProjectInfo = GC.getLeagueProjectInfo(eProject);
							if (pProjectInfo != NULL && pProjectInfo->GetRewardTier3() != NO_LEAGUE_PROJECT_REWARD) // Only check top reward tier
							{
								CvLeagueProjectRewardEntry* pRewardInfo = GC.getLeagueProjectRewardInfo(pProjectInfo->GetRewardTier3());
								if (pRewardInfo != NULL && pRewardInfo->GetBuilding() == eBuilding)
								{
									eThisBuildingProject = eProject;
									break;
								}
							}
						}
						if (eThisBuildingProject != NO_LEAGUE_PROJECT)
						{
							// Have we contributed anything to it?
							if (GC.getGame().GetGameLeagues()->GetNumActiveLeagues() > 0)
							{
								CvLeague* pLeague = GC.getGame().GetGameLeagues()->GetActiveLeague();
								if (pLeague != NULL)
								{
									if (pLeague->IsProjectActive(eThisBuildingProject) && pLeague->GetMemberContribution(ePlayerID, eThisBuildingProject) > 0)
									{
										// Only show the graphic in the capital, since that is where the wonder would go
										if (isCapital())
										{
											bShowHalfBuilt = true;
										}
									}
								}
							}
						}

						
					}

					if (bShowHalfBuilt)
					{
						auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
						gDLL->GameplayWonderCreated(ePlayerID, pDllPlot.get(), eBuilding, 0);
					}
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
void CvCity::setupBuildingGraphics()
{
	VALIDATE_OBJECT
	for(int eBuildingType = 0; eBuildingType < GC.getNumBuildingInfos(); eBuildingType++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(eBuildingType);
		CvBuildingEntry* buildingInfo = GC.getBuildingInfo(eBuilding);

		if(buildingInfo)
		{
			int iExists = m_pCityBuildings->GetNumBuilding(eBuilding);
			if(iExists > 0 && buildingInfo->IsCityWall())
			{
				auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
				gDLL->GameplayWallCreated(pDllPlot.get());
			}
		}
	}
}

//	--------------------------------------------------------------------------------
void CvCity::setupSpaceshipGraphics()
{
	VALIDATE_OBJECT

	CvTeam& thisTeam = GET_TEAM(getTeam());

	ProjectTypes ApolloProgram = (ProjectTypes) GC.getSPACE_RACE_TRIGGER_PROJECT();

	int spaceshipState = 0;

	if(isCapital() && thisTeam.getProjectCount((ProjectTypes)ApolloProgram) == 1)
	{
		ProjectTypes capsuleID = (ProjectTypes) GC.getSPACESHIP_CAPSULE();
		ProjectTypes boosterID = (ProjectTypes) GC.getSPACESHIP_BOOSTER();
		ProjectTypes stasisID = (ProjectTypes) GC.getSPACESHIP_STASIS();
		ProjectTypes engineID = (ProjectTypes) GC.getSPACESHIP_ENGINE();

		enum eSpaceshipState
		{
		    eUnderConstruction	= 0x0000,
		    eFrame				= 0x0001,
		    eCapsule			= 0x0002,
		    eStasis_Chamber		= 0x0004,
		    eEngine				= 0x0008,
		    eBooster1			= 0x0010,
		    eBooster2			= 0x0020,
		    eBooster3			= 0x0040,
		    eConstructed		= 0x0080,
		};

		auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
		gDLL->GameplaySpaceshipRemoved(pDllPlot.get());
		gDLL->GameplaySpaceshipCreated(pDllPlot.get(), eUnderConstruction + eFrame);

		spaceshipState = eFrame;

		if((thisTeam.getProjectCount((ProjectTypes)capsuleID)) == 1)
		{
			spaceshipState += eCapsule;
		}

		if((thisTeam.getProjectCount((ProjectTypes)stasisID)) == 1)
		{
			spaceshipState += eStasis_Chamber;
		}

		if((thisTeam.getProjectCount((ProjectTypes)engineID)) == 1)
		{
			spaceshipState += eEngine;
		}

		if((thisTeam.getProjectCount((ProjectTypes)boosterID)) >= 1)
		{
			spaceshipState += eBooster1;
		}

		if((thisTeam.getProjectCount((ProjectTypes)boosterID)) >= 2)
		{
			spaceshipState += eBooster2;
		}

		if((thisTeam.getProjectCount((ProjectTypes)boosterID)) == 3)
		{
			spaceshipState += eBooster3;
		}

		gDLL->GameplaySpaceshipEdited(pDllPlot.get(), spaceshipState);
	}
}

//	--------------------------------------------------------------------------------
#if defined(MOD_GLOBAL_VENICE_KEEPS_RESOURCES)
void CvCity::PreKill(bool bVenice)
#else
void CvCity::PreKill()
#endif
{
	VALIDATE_OBJECT

	PlayerTypes eOwner;
	if(isCitySelected())
	{
		DLLUI->clearSelectedCities();
	}

	setPopulation(0);

	CvPlot* pPlot = plot();

#if defined(MOD_BUGFIX_MINOR)
	GC.getGame().GetGameTrade()->ClearAllCityTradeRoutes(pPlot, true);
#else
	GC.getGame().GetGameTrade()->ClearAllCityTradeRoutes(pPlot);
#endif

	// Update resources linked to this city

	for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		CvPlot* pLoopPlot;
		pLoopPlot = GetCityCitizens()->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->getWorkingCityOverride() == this)
			{
				pLoopPlot->setWorkingCityOverride(NULL);
			}

			// Unlink Resources from this City
			if(pLoopPlot->getOwner() == getOwner())
			{
				if(pLoopPlot->getResourceType() != NO_RESOURCE)
				{
					if(pLoopPlot->GetResourceLinkedCity() == this)
					{
						pLoopPlot->SetResourceLinkedCity(NULL);
						pLoopPlot->DoFindCityToLinkResourceTo(this);
					}
				}
			}
		}
	}

	// If this city was built on a Resource, remove its Quantity from total
	if(pPlot->getResourceType() != NO_RESOURCE)
	{
		if(GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes) GC.getResourceInfo(pPlot->getResourceType())->getTechCityTrade()))
		{
			GET_PLAYER(getOwner()).changeNumResourceTotal(pPlot->getResourceType(), -pPlot->getNumResourceForPlayer(getOwner()));
		}
	}

#if defined(MOD_GLOBAL_VENICE_KEEPS_RESOURCES)
	plot()->removeMinorResources(bVenice);
#else
	if(GET_PLAYER(getOwner()).isMinorCiv())
	{
		GET_PLAYER(getOwner()).GetMinorCivAI()->DoRemoveStartingResources(plot());
	}
#endif

	for(int iI = 0; iI < GC.getNumBuildingInfos(); iI++)
	{
		CvBuildingEntry* pkBuilding = GC.getBuildingInfo((BuildingTypes)iI);
		if(pkBuilding)
		{
			m_pCityBuildings->SetNumRealBuilding(((BuildingTypes)iI), 0);
			m_pCityBuildings->SetNumFreeBuilding(((BuildingTypes)iI), 0);
		}
	}

	clearOrderQueue();

	// Killing a city while in combat is not something we really expect to happen.
	// It is *mostly* safe for it to happen, but combat systems must be able to gracefully handle the disapperance of a city.
	CvAssertMsg_Debug(!isFighting(), "isFighting did not return false as expected");

	clearCombat();

	// Could also be non-garrisoned units here that we need to show
	CvUnit* pLoopUnit;
	for(int iUnitLoop = 0; iUnitLoop < pPlot->getNumUnits(); iUnitLoop++)
	{
		pLoopUnit = pPlot->getUnitByIndex(iUnitLoop);

		if (pLoopUnit->IsGarrisoned())
			pLoopUnit->SetGarrisonedCity(-1);

		// Only show units that belong to this city's owner - that way we don't show units on EVERY city capture (since the old city is deleted in this case)
		if(getOwner() == pLoopUnit->getOwner())
		{
			auto_ptr<ICvUnit1> pDllUnit(new CvDllUnit(pLoopUnit));
			gDLL->GameplayUnitVisibility(pDllUnit.get(), !pLoopUnit->isInvisible(GC.getGame().getActiveTeam(),true) /*bVisible*/);
		}
	}

	for(int iPlotLoop = 0; iPlotLoop < GC.getMap().numPlots(); iPlotLoop++)
	{
		CvPlot* pLoopPlot = GC.getMap().plotByIndexUnchecked(iPlotLoop);
		if(NULL != pLoopPlot && pLoopPlot->GetCityPurchaseOwner() == getOwner() && pLoopPlot->GetCityPurchaseID() == GetID())
		{
			pLoopPlot->ClearCityPurchaseInfo();
			pLoopPlot->setOwner(NO_PLAYER, NO_PLAYER, /*bCheckUnits*/ true, /*bUpdateResources*/ true);
		}
	}

	pPlot->setPlotCity(NULL);

	GC.getMap().getArea(pPlot->getArea())->changeCitiesPerPlayer(getOwner(), -1);
#if defined(MOD_BALANCE_CORE)
	std::vector<int> areas = pPlot->getAllAdjacentAreas();
	for (std::vector<int>::iterator it=areas.begin(); it!=areas.end(); ++it)
	{
		CvArea* pkArea = GC.getMap().getArea(*it);
		if (pkArea->isWater())
			pkArea->changeCitiesPerPlayer(getOwner(), -1);
	}
#endif

	GET_TEAM(getTeam()).changeNumCities(-1);

	GC.getGame().changeNumCities(-1);

	CvAssertMsg(getNumGreatPeople() == 0, "getNumGreatPeople is expected to be 0");
	CvAssertMsg(!isProduction(), "isProduction is expected to be false");

	eOwner = getOwner();

	GET_PLAYER(getOwner()).GetFlavorManager()->RemoveFlavorRecipient(m_pCityStrategyAI);

	if(m_unitBeingBuiltForOperation.IsValid())
	{
		GET_PLAYER(getOwner()).CityUncommitToBuildUnitForOperationSlot(m_unitBeingBuiltForOperation);
		m_unitBeingBuiltForOperation.Invalidate();
	}
}

//	--------------------------------------------------------------------------------
void CvCity::PostKill(bool bCapital, CvPlot* pPlot, PlayerTypes eOwner)
{
	VALIDATE_OBJECT

	CvPlayer& owningPlayer = GET_PLAYER(eOwner);

	owningPlayer.CalculateNetHappiness();

	// Update Unit Maintenance for the player
	owningPlayer.UpdateUnitProductionMaintenanceMod();

	// Update Proximity between this Player and all others
	PlayerTypes ePlayer;
	for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
	{
		ePlayer = (PlayerTypes) iPlayerLoop;

		if(ePlayer != eOwner)
		{
			if(GET_PLAYER(ePlayer).isAlive())
			{
				owningPlayer.DoUpdateProximityToPlayer(ePlayer);
				GET_PLAYER(ePlayer).DoUpdateProximityToPlayer(eOwner);
			}
		}
	}

	GC.getMap().updateWorkingCity(pPlot,getWorkPlotDistance()*2);
	if(bCapital)
	{
#if defined(MOD_GLOBAL_NO_CONQUERED_SPACESHIPS)
		owningPlayer.disassembleSpaceship(pPlot);
#endif
		owningPlayer.findNewCapital();
		owningPlayer.SetHasLostCapital(true, getOwner());
		GET_TEAM(owningPlayer.getTeam()).resetVictoryProgress();
	}

	pPlot->setImprovementType((ImprovementTypes)(GC.getRUINS_IMPROVEMENT()));

	if(eOwner == GC.getGame().getActivePlayer())
	{
		DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
	}

	DLLUI->setDirty(NationalBorders_DIRTY_BIT, true);

	if(GC.getGame().getActivePlayer() == eOwner)
	{
		CvMap& theMap = GC.getMap();
		theMap.updateDeferredFog();
	}

}

//	--------------------------------------------------------------------------------
#if defined(MOD_GLOBAL_VENICE_KEEPS_RESOURCES)
void CvCity::kill(bool bVenice)
#else
void CvCity::kill()
#endif
{
	VALIDATE_OBJECT
	CvPlot* pPlot = plot();
	PlayerTypes eOwner = getOwner();
	bool bCapital = isCapital();

	IDInfo* pUnitNode;
	CvUnit* pLoopUnit;
	pUnitNode = pPlot->headUnitNode();

	FFastSmallFixedList<IDInfo, 25, true, c_eCiv5GameplayDLL > oldUnits;

	while(pUnitNode != NULL)
	{
		oldUnits.insertAtEnd(pUnitNode);
		pUnitNode = pPlot->nextUnitNode((IDInfo*)pUnitNode);
	}

	pUnitNode = oldUnits.head();

	while(pUnitNode != NULL)
	{
		pLoopUnit = ::getUnit(*pUnitNode);
		pUnitNode = oldUnits.next(pUnitNode);

		if(pLoopUnit)
		{
			if(pLoopUnit->IsImmobile())
			{
				pLoopUnit->kill(false, eOwner);
			}
		}
	}

#if defined(MOD_GLOBAL_VENICE_KEEPS_RESOURCES)
	PreKill(bVenice);
#else
	PreKill();
#endif

	// get spies out of city
	CvCityEspionage* pCityEspionage = GetCityEspionage();
	if(pCityEspionage)
	{
		for(int i = 0; i < MAX_MAJOR_CIVS; i++)
		{
			int iAssignedSpy = pCityEspionage->m_aiSpyAssignment[i];
			// if there is a spy in the city
			if(iAssignedSpy != -1)
			{
				GET_PLAYER((PlayerTypes)i).GetEspionage()->ExtractSpyFromCity(iAssignedSpy);
			}
		}
	}

	// Delete the city's information here!!!
	CvGameTrade* pkGameTrade = GC.getGame().GetGameTrade();
	if(pkGameTrade)
	{
#if defined(MOD_BUGFIX_MINOR)
		pkGameTrade->ClearAllCityTradeRoutes(plot(), true);
#else
		pkGameTrade->ClearAllCityTradeRoutes(plot());
#endif
	}
	GET_PLAYER(getOwner()).deleteCity(m_iID);
	GET_PLAYER(eOwner).GetCityConnections()->Update();

	// clean up
	PostKill(bCapital, pPlot, eOwner);
}

//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
CvPlayer* CvCity::GetPlayer() const
#else
CvPlayer* CvCity::GetPlayer()
#endif
{
	VALIDATE_OBJECT
	return &GET_PLAYER(getOwner());
}

//	--------------------------------------------------------------------------------
void CvCity::doTurn()
{
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doTurn, Turn %03d, %s, %s,", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	VALIDATE_OBJECT

	if(getDamage() > 0 && !IsBlockadedWaterAndLand())
	{
		CvAssertMsg(m_iDamage <= GetMaxHitPoints(), "Somehow a city has more damage than hit points. Please show this to a gameplay programmer immediately.");

		int iHitsHealed = GC.getCITY_HIT_POINTS_HEALED_PER_TURN();
		if(isCapital() && !GET_PLAYER(getOwner()).isMinorCiv())
		{
			iHitsHealed++;
		}
		int iBuildingDefense = m_pCityBuildings->GetBuildingDefense();
		iBuildingDefense *= (100 + m_pCityBuildings->GetBuildingDefenseMod());
		iBuildingDefense /= 100;
		iHitsHealed += iBuildingDefense / 500;

#if defined(MOD_BALANCE_CORE)
		if(!GET_PLAYER(getOwner()).IsAtWar())
		{
			iHitsHealed *= 2;
		}
#endif
		changeDamage(-iHitsHealed);
	}

	if(getDamage() < 0)
	{
		setDamage(0);
	}

#if defined(MOD_BALANCE_CORE)
	//See if we are a defense-necessary city.
	TestBastion();
	//Do bad barb stuff!
	DoBarbIncursion();
	updateEconomicValue();
	UpdateNearbySettleSites();
	UpdateGrowthFromTourism();

	CvUnit* pLoopUnit;
	if(plot() != NULL)
	{
		for(int iUnitLoop = 0; iUnitLoop < plot()->getNumUnits(); iUnitLoop++)
		{
			pLoopUnit = plot()->getUnitByIndex(iUnitLoop);

			//Only get land combat units
			if(pLoopUnit != NULL && getOwner() == pLoopUnit->getOwner() && pLoopUnit->IsCombatUnit() && pLoopUnit->getDomainType() == DOMAIN_LAND)
			{
				if(pLoopUnit->getDamage() > 0)
				{
					if((pLoopUnit->getDamage() - GetAlwaysHeal()) <= 0)
					{
						pLoopUnit->setDamage(0);
					}
					else
					{
						pLoopUnit->changeDamage(-GetAlwaysHeal());
					}
				}
			}
		}
	}
#endif
#if defined(MOD_BALANCE_CORE_EVENTS)
	if(MOD_BALANCE_CORE_EVENTS)
	{
		if(GC.getGame().isOption(GAMEOPTION_EVENTS))
		{
			//Don't do events in MP
			bool bDontShowRewardPopup = (GC.getGame().isReallyNetworkMultiPlayer() || GC.getGame().isNetworkMultiPlayer());

			if (!bDontShowRewardPopup)
			{
				DoEvents();
			}
		}
	}
#endif
	setDrafted(false);
	setMadeAttack(false);
	GetCityBuildings()->SetSoldBuildingThisTurn(false);

	DoUpdateFeatureSurrounded();

	GetCityStrategyAI()->DoTurn();
#if !defined(MOD_BALANCE_CORE)
	GetCityCitizens()->DoTurn();
#endif
	AI_doTurn();

#if defined(MOD_BALANCE_CORE)
	if(GetUnitPurchaseCooldown() > 0)
	{
		ChangeUnitPurchaseCooldown(-1);
	}
	if(GetBuildingPurchaseCooldown() > 0)
	{
		ChangeBuildingPurchaseCooldown(-1);
	}
	if(!GET_PLAYER(getOwner()).isHuman())
	{
		CheckForOperationUnits();
	}
	if(isCapital() && IsPuppet())
	{
		SetPuppet(false);
	}
	GetCityReligions()->ComputeReligiousMajority();
	if(IsRazing())
	{
		DoSellBuilding();
	}

	if(MOD_BALANCE_CORE && !GET_PLAYER(getOwner()).isMinorCiv() && !GET_PLAYER(getOwner()).isBarbarian())
	{
		int iNumAllies = GET_PLAYER(getOwner()).GetNumCSAllies();
		int iNumFriends = GET_PLAYER(getOwner()).GetNumCSFriends();
		// Loop through all minors and get the total number we've met.
		if(isCapital())
		{
			//If we get a yield bonus in all cities because of CS alliance, this is a good place to refresh it.
			int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
			if(iEra <= 0)
			{
				iEra = 1;
			}
			for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				YieldTypes eYield = (YieldTypes) iI;
				if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromCSAlly(eYield) > 0)
				{
					SetBaseYieldRateFromCSAlliance(eYield, (GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromCSAlly(eYield) * iNumAllies * iEra));
				}
				if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromCSAlly(eYield) > 0)
				{
					SetBaseYieldRateFromCSFriendship(eYield, (GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromCSFriend(eYield) * iNumFriends * iEra));
				}
			}
		}
		if(MOD_BALANCE_CORE_BUILDING_RESOURCE_MAINTENANCE)
		{
			int iBad = 0;
			for (int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
			{
				ResourceTypes eResourceLoop = (ResourceTypes) iResourceLoop;
				if (eResourceLoop != NO_RESOURCE)
				{
					const CvResourceInfo* pkResourceInfo = GC.getResourceInfo(eResourceLoop);
					if(pkResourceInfo != NULL && pkResourceInfo->getResourceUsage() == RESOURCEUSAGE_STRATEGIC)
					{		
						// See if there are any BuildingClass requirements
						if(GET_PLAYER(getOwner()).getResourceOverValue(eResourceLoop) > 0)
						{
							const int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
							const CvCivilizationInfo& thisCivilization = GET_PLAYER(getOwner()).getCivilizationInfo();
							for(int iBuildingClassLoop = 0; iBuildingClassLoop < iNumBuildingClassInfos; iBuildingClassLoop++)
							{
								const BuildingClassTypes eBuildingClass = (BuildingClassTypes) iBuildingClassLoop;
								CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
								if(!pkBuildingClassInfo)
								{
									continue;
								}

								const BuildingTypes eResourceBuilding = (BuildingTypes)(thisCivilization.getCivilizationBuildings(eBuildingClass));

								if(GetCityBuildings()->GetNumBuilding(eResourceBuilding) > 0)
								{
									CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eResourceBuilding);
									if(pkBuildingInfo)
									{
										int iResourceDelta = 0;
										//If amount we're under is less than resource quantity, take the lesser value.
										if(GET_PLAYER(getOwner()).getResourceOverValue(eResourceLoop) < pkBuildingInfo->GetResourceQuantityRequirement(eResourceLoop))
										{
											iResourceDelta = GET_PLAYER(getOwner()).getResourceOverValue(eResourceLoop);
										}
										//Otherwise, take the building's full value.
										else
										{
											iResourceDelta = pkBuildingInfo->GetResourceQuantityRequirement(eResourceLoop);
										}
										if(iResourceDelta > 0)
										{
											iBad += (iResourceDelta * 2);
											GET_PLAYER(getOwner()).changeResourceOverValue(eResourceLoop, -iResourceDelta);
											if(GET_PLAYER(getOwner()).getResourceOverValue(eResourceLoop) < 0)
											{
												GET_PLAYER(getOwner()).setResourceOverValue(eResourceLoop, 0);
											}
											CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
											if(pNotifications)
											{
												Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_OVER_RESOURCE_LIMIT_CITY");
												strText << pkResourceInfo->GetTextKey();
												strText << getNameKey();
												strText << (iResourceDelta * 2);
												Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_OVER_RESOURCE_LIMIT_CITY");
												strSummary << pkResourceInfo->GetTextKey();
												strSummary << getNameKey();
												pNotifications->Add(NOTIFICATION_DISCOVERED_STRATEGIC_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResourceLoop);
											}
										}
									}
								}
							}
						}
					}
				}
			}
			if(iBad > 0)
			{
				 SetExtraBuildingMaintenance(iBad);
			}
			else
			{
				 SetExtraBuildingMaintenance(0);
			}
		}
	}
	UpdateComboHappiness();
#endif

	bool bRazed = DoRazingTurn();

	if(!bRazed)
	{
		DoResistanceTurn();

		bool bAllowNoProduction = !doCheckProduction();
#if defined(MOD_BALANCE_CORE)
		bool bWeGrew = false;
		int iDifference = (getYieldRateTimes100(YIELD_FOOD, false) - foodConsumption() * 100);
		if(isFoodProduction() || getFood() <= 5 || iDifference <= 0)
		{
			doGrowth();
			bWeGrew = true;
		}
#endif

		doProduction(bAllowNoProduction);

		doDecay();

		doMeltdown();

		{
			AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("doImprovement, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );

			for (int iI = 0; iI < GetNumWorkablePlots(); iI++)
			{
				CvPlot* pLoopPlot = GetCityCitizens()->GetCityPlotFromIndex(iI);

				if(pLoopPlot != NULL)
				{
					if(pLoopPlot->getWorkingCity() == this)
					{
						if(pLoopPlot->isBeingWorked())
						{
							pLoopPlot->doImprovement();
						}
					}
				}
			}
		}

		// Following function also looks at WLTKD stuff
		DoTestResourceDemanded();

		// Culture accumulation
		if(getJONSCulturePerTurn() > 0)
		{
			ChangeJONSCultureStored(getJONSCulturePerTurn());
#if defined(MOD_BALANCE_CORE_POLICIES)
			ChangeJONSCultureStored(getYieldRate(YIELD_CULTURE_LOCAL, false));
			//Doubles during Golden Age
			if(GET_PLAYER(getOwner()).IsDoubleBorderGA() && (GET_PLAYER(getOwner()).isGoldenAge() || (GetWeLoveTheKingDayCounter() > 0)))
			{
				ChangeJONSCultureStored(getJONSCulturePerTurn());
			}
#endif
		}

		// Enough Culture to acquire a new Plot?
		if(GetJONSCultureStored() >= GetJONSCultureThreshold())
		{
			DoJONSCultureLevelIncrease();
		}

		// Resource Demanded Counter
		if(GetResourceDemandedCountdown() > 0)
		{
			ChangeResourceDemandedCountdown(-1);

			if(GetResourceDemandedCountdown() == 0)
			{
				// Pick a Resource to demand
				DoPickResourceDemanded();
			}
		}

		updateStrengthValue();

		DoNearbyEnemy();
#if !defined(NO_ACHIEVEMENTS)
		//Check for Achievements
		if(isHuman() && !GC.getGame().isGameMultiPlayer() && GET_PLAYER(GC.getGame().getActivePlayer()).isLocalPlayer())
		{
			if(getJONSCulturePerTurn()>=100)
			{
				gDLL->UnlockAchievement(ACHIEVEMENT_CITY_100CULTURE);
			}
			if(getYieldRate(YIELD_GOLD, false)>=100)
			{
				gDLL->UnlockAchievement(ACHIEVEMENT_CITY_100GOLD);
			}
			if(getYieldRate(YIELD_SCIENCE, false)>=100)
			{
				gDLL->UnlockAchievement(ACHIEVEMENT_CITY_100SCIENCE);
			}
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(!bWeGrew)
		{
			doGrowth();
		}
		GetCityCitizens()->DoTurn();
#endif
		// sending notifications on when routes are connected to the capital
		if(!isCapital())
		{
			CvNotifications* pNotifications = GET_PLAYER(m_eOwner).GetNotifications();
			if(pNotifications)
			{
				CvCity* pPlayerCapital = GET_PLAYER(m_eOwner).getCapitalCity();
				CvAssertMsg(pPlayerCapital, "No capital city?");

				if(m_bRouteToCapitalConnectedLastTurn != m_bRouteToCapitalConnectedThisTurn && pPlayerCapital)
				{
					Localization::String strMessage;
					Localization::String strSummary;

					if(m_bRouteToCapitalConnectedThisTurn)  // connected this turn
					{
						strMessage = Localization::Lookup("TXT_KEY_NOTIFICATION_TRADE_ROUTE_ESTABLISHED");
						strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_TRADE_ROUTE_ESTABLISHED");
						strMessage << getNameKey();
						strMessage << pPlayerCapital->getNameKey();
						pNotifications->Add(NOTIFICATION_TRADE_ROUTE, strMessage.toUTF8(), strSummary.toUTF8(), -1, -1, -1);
					}
					else // lost connection this turn
					{
						strMessage = Localization::Lookup("TXT_KEY_NOTIFICATION_TRADE_ROUTE_BROKEN");
						strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_TRADE_ROUTE_BROKEN");
						strMessage << getNameKey();
						strMessage << pPlayerCapital->getNameKey();
						pNotifications->Add(NOTIFICATION_TRADE_ROUTE_BROKEN, strMessage.toUTF8(), strSummary.toUTF8(), -1, -1, -1);
					}
				}
			}
#if defined(MOD_BALANCE_CORE)
			updateYield(true);
#endif
			m_bRouteToCapitalConnectedLastTurn = m_bRouteToCapitalConnectedThisTurn;
		}
#if defined(MOD_BALANCE_CORE)
		for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			YieldTypes eYield = (YieldTypes) iI;
			if(eYield == NO_YIELD)
				continue;

			UpdateSpecialReligionYields(eYield);
			UpdateCityYields(eYield);
		}
		GetCityCulture()->CalculateBaseTourismBeforeModifiers();
		GetCityCulture()->CalculateBaseTourism();
#endif
		// XXX
#ifdef _DEBUG
		{
			CvPlot* pPlot;
			int iCount;

			for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				CvAssert(getBaseYieldRate((YieldTypes)iI) >= 0);
				CvAssert(getYieldRate((YieldTypes)iI, false) >= 0);

				iCount = 0;


				for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
				{
					pPlot = GetCityCitizens()->GetCityPlotFromIndex(iJ);

					if(pPlot != NULL)
					{
						if(GetCityCitizens()->IsWorkingPlot(pPlot))
						{
							iCount += pPlot->getYield((YieldTypes)iI);
						}
					}
				}

				for(int iJ = 0; iJ < GC.getNumSpecialistInfos(); iJ++)
				{
					iCount += (GET_PLAYER(getOwner()).specialistYield(((SpecialistTypes)iJ), ((YieldTypes)iI)) * (GetCityCitizens()->GetSpecialistCount((SpecialistTypes)iJ)));
				}

				for(int iJ = 0; iJ < GC.getNumBuildingInfos(); iJ++)
				{
					const BuildingTypes eBuilding = static_cast<BuildingTypes>(iJ);
					CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
					if(pkBuildingInfo)
					{
						iCount += m_pCityBuildings->GetNumActiveBuilding(eBuilding) * (pkBuildingInfo->GetYieldChange(iI) + m_pCityBuildings->GetBuildingYieldChange((BuildingClassTypes)pkBuildingInfo->GetBuildingClassType(), (YieldTypes)iI));
					}
				}

				// Science from Population
				if((YieldTypes) iI == YIELD_SCIENCE)
				{
					iCount += getPopulation() * GC.getSCIENCE_PER_POPULATION();
				}

				CvAssert(iCount == getBaseYieldRate((YieldTypes)iI));
			}
		}
#endif
		// XXX
	}
}


//	--------------------------------------------------------------------------------
bool CvCity::isCitySelected()
{
	VALIDATE_OBJECT
	auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);

	return DLLUI->isCitySelected(pCity.get());
}


//	--------------------------------------------------------------------------------
bool CvCity::canBeSelected() const
{
	VALIDATE_OBJECT
	if((getTeam() == GC.getGame().getActiveTeam()) || GC.getGame().isDebugMode())
	{
		return true;
	}

	return false;
}


//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
void CvCity::updateYield(bool bSkipCity)
#else
void CvCity::updateYield()
#endif
{
	VALIDATE_OBJECT
	CvPlot* pLoopPlot;
	int iI;


	for(iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		pLoopPlot = GetCityCitizens()->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			pLoopPlot->updateYield();
		}
	}
#if defined(MOD_BALANCE_CORE)
	if(!bSkipCity)
	{
		for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			const YieldTypes eYield = static_cast<YieldTypes>(iI);
			UpdateCityYields(eYield);
		}
		GetCityCulture()->CalculateBaseTourismBeforeModifiers();
		GetCityCulture()->CalculateBaseTourism();
	}
#endif
}
#if defined(MOD_BALANCE_CORE)
void CvCity::UpdateCityYields(YieldTypes eYield)
{
	if(eYield == YIELD_CULTURE)
	{
		SetStaticYield(eYield, getJONSCulturePerTurn(false) * 100);
	}
	else if(eYield == YIELD_FAITH)
	{
		SetStaticYield(eYield, GetFaithPerTurn(false) * 100);
	}
	else
	{
		SetStaticYield(eYield, getYieldRateTimes100(eYield, false, false));
	}
}
void CvCity::SetStaticYield(YieldTypes eYield, int iValue)
{
	CvAssertMsg(eYield >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eYield < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	m_aiStaticCityYield.setAt(eYield, iValue);
}
int CvCity::GetStaticYield(YieldTypes eYield) const
{
	VALIDATE_OBJECT
	return m_aiStaticCityYield[eYield];
}

void CvCity::SetThreatRank(int iValue)
{
	if(iValue != m_iThreatRank)
	{
		m_iThreatRank = iValue;
	}
}
int CvCity::GetThreatRank() const
{
	VALIDATE_OBJECT
	return m_iThreatRank;
}

void CvCity::SetTradePriorityLand(int iValue)
{
	if(iValue != m_iTradePriorityLand)
	{
		m_iTradePriorityLand = iValue;
	}
}
int CvCity::GetTradePriorityLand() const
{
	VALIDATE_OBJECT
	return m_iTradePriorityLand;
}

void CvCity::SetTradePrioritySea(int iValue)
{
	if(iValue != m_iTradePrioritySea)
	{
		m_iTradePrioritySea = iValue;
	}
}
int CvCity::GetTradePrioritySea() const
{
	VALIDATE_OBJECT
	return m_iTradePrioritySea;
}

void CvCity::UpdateNearbySettleSites()
{
	if(GET_PLAYER(getOwner()).isBarbarian() || GET_PLAYER(getOwner()).isHuman() || GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoAnnexing())
	{
		return;
	}

	int iSettlerDistance;
	int iDistanceDropoff;
	int iValue;
	int iDanger;
	int iBestValue = 0;
	m_iNearbySettlerValue = 0;

	int iSettlerX = getX();
	int iSettlerY = getY();

	int iEvalDistance = GC.getSETTLER_EVALUATION_DISTANCE();
	int iDistanceDropoffMod = GC.getSETTLER_DISTANCE_DROPOFF_MODIFIER();
	int iBeginSearchX = iSettlerX - iEvalDistance;
	int iBeginSearchY = iSettlerY - iEvalDistance;
	int iEndSearchX   = iSettlerX + iEvalDistance;
	int iEndSearchY   = iSettlerY + iEvalDistance;

	CvMap& kMap = GC.getMap();

	TeamTypes eUnitTeam = getTeam();

	CvCity* pCapital = GET_PLAYER(getOwner()).getCapitalCity();
	int iCapArea = NULL;
	if(pCapital != NULL)
	{
		iCapArea = pCapital->getArea();
	}

	for(int iPlotX = iBeginSearchX; iPlotX != iEndSearchX; iPlotX++)
	{
		for(int iPlotY = iBeginSearchY; iPlotY != iEndSearchY; iPlotY++)
		{
			CvPlot* pPlot = kMap.plot(iPlotX, iPlotY);
			if(!pPlot)
			{
				continue;
			}

			//if (!pPlot->isVisible(pUnit->getTeam(), false /*bDebug*/))
			if(!pPlot->isRevealed(eUnitTeam))
			{
				continue;
			}

			// Can't actually found here!
			if(!GET_PLAYER(getOwner()).canFound(iPlotX, iPlotY))
			{
				continue;
			}

			// Do we have to check if this is a safe place to go?
			if(!pPlot->isVisibleEnemyUnit(GET_PLAYER(getOwner()).GetID()))
			{
				iSettlerDistance = plotDistance(iPlotX, iPlotY, iSettlerX, iSettlerY);

				iValue = GET_PLAYER(getOwner()).getPlotFoundValue(iPlotX, iPlotY) / 100;

				iDistanceDropoff = (iDistanceDropoffMod * iSettlerDistance) / iEvalDistance;
				iValue = iValue * (100 - iDistanceDropoff) / 100;
				iDanger = GET_PLAYER(getOwner()).GetPlotDanger(*pPlot);
				if(iDanger < 1000)
				{
					iValue = ((1000 - iDanger) * iValue) / 7250;

					if(iValue > iBestValue)
					{
						iBestValue = iValue;
					}
				}
			}
		}
	}
	m_iNearbySettlerValue = iBestValue;
}
int CvCity::GetNearbySettleSiteValue() const
{
	VALIDATE_OBJECT
	return m_iNearbySettlerValue;
}

void CvCity::ChangeTradeRouteSeaDistanceModifier(int iValue)
{
	if(iValue != 0)
	{
		m_iTradeRouteSeaDistanceModifier += iValue;
	}
}
int CvCity::GetTradeRouteSeaDistanceModifier() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteSeaDistanceModifier;
}

void CvCity::ChangeTradeRouteLandDistanceModifier(int iValue)
{
	if(iValue != 0)
	{
		m_iTradeRouteLandDistanceModifier += iValue;
	}
}
int CvCity::GetTradeRouteLandDistanceModifier() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteLandDistanceModifier;
}
#endif
#if defined(MOD_BALANCE_CORE_EVENTS)
int CvCity::GetEventChoiceDuration(CityEventChoiceTypes eEventChoice) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEventChoice >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEventChoice < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	return m_aiEventChoiceDuration[eEventChoice];
}
void CvCity::ChangeEventChoiceDuration(CityEventChoiceTypes eEventChoice,int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEventChoice >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEventChoice < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	if(iValue != 0)
	{
		m_aiEventChoiceDuration.setAt(eEventChoice, m_aiEventChoiceDuration[eEventChoice] + iValue);
	}
}
void CvCity::SetEventChoiceDuration(CityEventChoiceTypes eEventChoice,int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEventChoice >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEventChoice < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	m_aiEventChoiceDuration.setAt(eEventChoice, iValue);
}
int CvCity::GetEventIncrement(CityEventTypes eEvent) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	return m_aiEventIncrement[eEvent];
}
void CvCity::IncrementEvent(CityEventTypes eEvent, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	if(iValue != 0)
	{
		m_aiEventIncrement.setAt(eEvent, m_aiEventIncrement[eEvent] + iValue);
	}
}
int CvCity::GetCityEventCooldown() const
{
	VALIDATE_OBJECT
	return m_iCityEventCooldown;
}
void CvCity::ChangeCityEventCooldown(int iValue)
{
	VALIDATE_OBJECT
	if(iValue != 0)
	{
		m_iCityEventCooldown += iValue;
	}
}
void CvCity::ChangeEventCityYield(YieldTypes eYield, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eYield >= 0, "eYield is expected to be non-negative (invalid Index)");
	CvAssertMsg(eYield < NUM_YIELD_TYPES, "eYield is expected to be within maximum bounds (invalid Index)");
	if(iValue != 0)
	{
		m_aiEventCityYield.setAt(eYield, m_aiEventCityYield[eYield] + iValue);
		updateYield();
	}
}
int CvCity::GetEventCityYield(YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eYield >= 0, "eYield is expected to be non-negative (invalid Index)");
	CvAssertMsg(eYield < NUM_YIELD_TYPES, "eYield is expected to be within maximum bounds (invalid Index)");
	return m_aiEventCityYield[eYield];
}
//	--------------------------------------------------------------------------------
int CvCity::GetEventBuildingClassCityYield(BuildingClassTypes eIndex1, YieldTypes eIndex2)	const
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventBuildingClassYield[eIndex1][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventBuildingClassYield(BuildingClassTypes eIndex1, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventBuildingClassYield[eIndex1][eIndex2] += iChange;
		updateYield();
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetEventBuildingClassCityYieldModifier(BuildingClassTypes eIndex1, YieldTypes eIndex2)	const
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventBuildingClassYieldModifier[eIndex1][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventBuildingClassYieldModifier(BuildingClassTypes eIndex1, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventBuildingClassYieldModifier[eIndex1][eIndex2] += iChange;
		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetEventImprovementYield(ImprovementTypes eImprovement, YieldTypes eIndex2)	const
{
	CvAssertMsg(eImprovement >= 0, "eImprovement is expected to be non-negative (invalid Index)");
	CvAssertMsg(eImprovement < GC.getNumFeatureInfos(), "eImprovement is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventImprovementYield[eImprovement][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventImprovementYield(ImprovementTypes eImprovement, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eImprovement >= 0, "eImprovement is expected to be non-negative (invalid Index)");
	CvAssertMsg(eImprovement < GC.getNumFeatureInfos(), "eImprovement is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventImprovementYield[eImprovement][eIndex2] += iChange;
		updateYield();
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetEventResourceYield(ResourceTypes eResource, YieldTypes eIndex2)	const
{
	CvAssertMsg(eResource >= 0, "eResource is expected to be non-negative (invalid Index)");
	CvAssertMsg(eResource < GC.getNumResourceInfos(), "eResource is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventResourceYield[eResource][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventResourceYield(ResourceTypes eResource, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eResource >= 0, "eResource is expected to be non-negative (invalid Index)");
	CvAssertMsg(eResource < GC.getNumResourceInfos(), "eResource is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventResourceYield[eResource][eIndex2] += iChange;
		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetEventSpecialistYield(SpecialistTypes eSpecialist, YieldTypes eIndex2)	const
{
	CvAssertMsg(eResource >= 0, "eResource is expected to be non-negative (invalid Index)");
	CvAssertMsg(eResource < GC.getNumSpecialistInfos(), "eResource is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventSpecialistYield[eSpecialist][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventSpecialistYield(SpecialistTypes eSpecialist, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eResource >= 0, "eResource is expected to be non-negative (invalid Index)");
	CvAssertMsg(eResource < GC.getNumSpecialistInfos(), "eResource is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventSpecialistYield[eSpecialist][eIndex2] += iChange;
		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetEventTerrainYield(TerrainTypes eTerrain, YieldTypes eIndex2)	const
{
	CvAssertMsg(eTerrain >= 0, "eImprovement is expected to be non-negative (invalid Index)");
	CvAssertMsg(eTerrain < GC.getNumTerrainInfos(), "eImprovement is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventTerrainYield[eTerrain][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventTerrainYield(TerrainTypes eTerrain, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eTerrain >= 0, "eTerrain is expected to be non-negative (invalid Index)");
	CvAssertMsg(eTerrain < GC.getNumTerrainInfos(), "eTerrain is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventTerrainYield[eTerrain][eIndex2] += iChange;
		updateYield();
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetEventFeatureYield(FeatureTypes eFeature, YieldTypes eIndex2)	const
{
	CvAssertMsg(eFeature >= 0, "eFeature is expected to be non-negative (invalid Index)");
	CvAssertMsg(eFeature < GC.getNumFeatureInfos(), "eFeature is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiEventFeatureYield[eFeature][eIndex2];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeEventFeatureYield(FeatureTypes eFeature, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eFeature >= 0, "eFeature is expected to be non-negative (invalid Index)");
	CvAssertMsg(eFeature < GC.getNumFeatureInfos(), "eFeature is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiEventFeatureYield[eFeature][eIndex2] += iChange;
		updateYield();
	}
}
void CvCity::DoEvents()
{
	//Minors? Barbs? Get out!
	if(GET_PLAYER(getOwner()).isMinorCiv() || GET_PLAYER(getOwner()).isBarbarian())
		return;

	//Event Choice Duration First - if we're in one, let's do the countdown now.
	//We need to do this first so we can cancel the event(s).
	for(int iLoop = 0; iLoop < GC.getNumCityEventChoiceInfos(); iLoop++)
	{
		CityEventChoiceTypes eEventChoice = (CityEventChoiceTypes)iLoop;
		if(eEventChoice != NO_EVENT_CHOICE_CITY)
		{
			if(GetEventChoiceDuration(eEventChoice) > 0)
			{
				ChangeEventChoiceDuration(eEventChoice, -1);
				if(GC.getLogging())
				{
					CvModEventCityChoiceInfo* pkEventInfo = GC.getCityEventChoiceInfo(eEventChoice);
					if(pkEventInfo != NULL)
					{
						CvString playerName;
						FILogFile* pLog;
						CvString strBaseString;
						CvString strOutBuf;
						CvString strFileName = "EventCityLogging.csv";
						playerName = getName();
						pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
						strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
						strBaseString += playerName + ", ";
						strOutBuf.Format("Event choice: %s. Cooldown Active. Changing Value by -1. Cooldown Remaining: %d", pkEventInfo->GetDescription(), GetEventChoiceDuration(eEventChoice));
						strBaseString += strOutBuf;
						pLog->Msg(strBaseString);
					}
				}
				if(GetEventChoiceDuration(eEventChoice) == 0)
				{
					DoCancelEventChoice(eEventChoice);
				}
			}
		}
	}

	if (GetCityEventCooldown() > 0)
	{
		if (GC.getLogging())
		{
			CvString playerName;
			FILogFile* pLog;
			CvString strBaseString;
			CvString strOutBuf;
			CvString strFileName = "EventCityLogging.csv";
			playerName = getName();
			pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
			strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
			strBaseString += playerName + ", ";
			strOutBuf.Format("City Event: Global Cooldown Active. Cooldown: %d", GetCityEventCooldown());
			strBaseString += strOutBuf;
			pLog->Msg(strBaseString);
		}
		ChangeCityEventCooldown(-1);
	}

	//Let's loop through all events.
	CvWeightedVector<int> veValidEvents;

	for(int iLoop = 0; iLoop < GC.getNumCityEventInfos(); iLoop++)
	{
		CityEventTypes eEvent = (CityEventTypes)iLoop;
		if(eEvent != NO_EVENT)
		{
			CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
			if(pkEventInfo == NULL)
			{
				continue;
			}
		
			if(pkEventInfo->getRandomChance() == -1)
				continue;

			if(pkEventInfo->isOneShot() && IsEventFired(eEvent))
				continue;

			//Lua Hook
			if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_CityEventCanTake, getOwner(), GetID(), eEvent) == GAMEEVENTRETURN_FALSE) 
			{
				continue;
			}

			//Global Cooldown Second - if we've had this event recently, let's check this.
			if(GetEventCooldown(eEvent) > 0)
			{
				if(GC.getLogging())
				{
					CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
					if(pkEventInfo != NULL)
					{
						CvString playerName;
						FILogFile* pLog;
						CvString strBaseString;
						CvString strOutBuf;
						CvString strFileName = "EventCityLogging.csv";
						playerName = getName();
						pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
						strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
						strBaseString += playerName + ", ";
						strOutBuf.Format("City Event: %s. Cooldown Active. Cooldown: %d", pkEventInfo->GetDescription(), GetEventCooldown(eEvent));
						strBaseString += strOutBuf;
						pLog->Msg(strBaseString);
					}
				}
				//This is done at the player level, not the City level!
				ChangeEventCooldown(eEvent, -1);
				continue;
			}

			if (GetCityEventCooldown() > 0 && !pkEventInfo->IgnoresGlobalCooldown())
			{
				continue;
			}

			//most expensive check last
			if (IsCityEventValid(eEvent))
			{
				veValidEvents.push_back(eEvent, pkEventInfo->getRandomChance() + GetEventIncrement(eEvent));
			}
		}
	}

	CityEventTypes eChosenEvent = NO_EVENT_CITY;

	if(veValidEvents.size() > 0)
	{
		veValidEvents.SortItems();
		if(GC.getLogging())
		{
			CvString strBaseString;
			CvString strOutBuf;
			CvString strFileName = "EventCityLogging.csv";
			CvString playerName = getName();
			FILogFile* pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
			strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
			strBaseString += playerName + ", ";
			strOutBuf.Format("Found %d Events for seeding", veValidEvents.size());
			strBaseString += strOutBuf;
			pLog->Msg(strBaseString);
		}

		int iRandIndex = GC.getGame().getJonRandNumVA(1000, "Picking random event for city %s.", getName().c_str());

		//which one is it?
		int iWeight = 0;
		for (int iLoop = 0; iLoop < veValidEvents.size(); iLoop++)
		{
			CityEventTypes eEvent = (CityEventTypes)veValidEvents.GetElement(iLoop);
			CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
			if (!pkEventInfo)
				continue;

			iWeight = veValidEvents.GetWeight(iLoop);
			if (iRandIndex < iWeight)
			{
				eChosenEvent = eEvent;
				break;
			}
		}

		if (eChosenEvent != NO_EVENT_CITY)
		{
			CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eChosenEvent);
			if (pkEventInfo != NULL)
			{
				DoStartEvent(eChosenEvent);

				//We did it! But reset our increment.
				IncrementEvent(eChosenEvent, -GetEventIncrement(eChosenEvent));
				if (GC.getLogging())
				{
					CvString strBaseString;
					CvString strOutBuf;
					CvString strFileName = "EventCityLogging.csv";
					CvString playerName = getName();
					FILogFile* pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
					strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
					strBaseString += playerName + ", ";
					strOutBuf.Format("Resetting event chance: %s", pkEventInfo->GetDescription());
					strBaseString += strOutBuf;
					pLog->Msg(strBaseString);
				}
			}
		}
	}

	for (int iLoop = 0; iLoop < veValidEvents.size(); iLoop++)
	{
		CityEventTypes eEvent = (CityEventTypes)veValidEvents.GetElement(iLoop);
		if (eEvent != NO_EVENT)
		{
			CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
			if (!pkEventInfo)
				continue;

			//But not for the one we just did!
			if (eEvent == eChosenEvent)
				continue;

			//We didn't do it? Bummer. BUT if there's a delta, the chance gets higher next turn...
			if (pkEventInfo->getRandomChanceDelta() > 0)
			{
				IncrementEvent(eEvent, pkEventInfo->getRandomChanceDelta());
				if (GC.getLogging())
				{
					CvString strBaseString;
					CvString strOutBuf;
					CvString strFileName = "EventCityLogging.csv";
					CvString playerName = getName();
					FILogFile* pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
					strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
					strBaseString += playerName + ", ";
					strOutBuf.Format("Incrementing event chance: %s, Increment: %d", pkEventInfo->GetDescription(), GetEventIncrement(eEvent));
					strBaseString += strOutBuf;
					pLog->Msg(strBaseString);
				}
			}
		}
	}
}
void CvCity::DoStartEvent(CityEventTypes eChosenEvent)
{
	if(eChosenEvent != NO_EVENT)
	{
		CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eChosenEvent);
		if(pkEventInfo != NULL)
		{
			//Set true so we know we're doing an event right now.
			SetEventActive(eChosenEvent, true);

			int iMin = GC.getEVENT_MIN_DURATION_BETWEEN();
			iMin *= GC.getGame().getGameSpeedInfo().getTrainPercent();
			iMin /= 100;
			ChangeCityEventCooldown(iMin);

			//Set oneshot stuff so this event can't fire ever again.
			if(pkEventInfo->isOneShot())
			{
				SetEventFired(eChosenEvent, true);
			}
			//Lua Hook
			GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityEventActivated, getOwner(), GetID(), eChosenEvent);

			//Gamespeed.
			int iEventDuration = pkEventInfo->getCooldown();
			iEventDuration *= GC.getGame().getGameSpeedInfo().getTrainPercent();
			iEventDuration /= 100;
			ChangeEventCooldown(eChosenEvent, iEventDuration);
			if(GC.getLogging())
			{
				CvString playerName;
				FILogFile* pLog;
				CvString strBaseString;
				CvString strOutBuf;
				CvString strFileName = "EventCityLogging.csv";
				playerName = getName();
				pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
				strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
				strBaseString += playerName + ", ";
				strOutBuf.Format("Event started for city: %s. Cooldown: %d", pkEventInfo->GetDescription(), iEventDuration);
				strBaseString += strOutBuf;
				pLog->Msg(strBaseString);
			}
			int iNumEvent = 0;
			CityEventChoiceTypes eEventChoice = NO_EVENT_CHOICE_CITY;
			for(int iLoop = 0; iLoop < GC.getNumCityEventChoiceInfos(); iLoop++)
			{
				eEventChoice = (CityEventChoiceTypes)iLoop;
				if(eEventChoice != NO_EVENT_CHOICE_CITY)
				{
					CvModEventCityChoiceInfo* pkEventChoiceInfo = GC.getCityEventChoiceInfo(eEventChoice);
					if(pkEventChoiceInfo != NULL)
					{
						if(IsCityEventChoiceValid(eEventChoice, eChosenEvent))
						{
							iNumEvent++;
							if(pkEventInfo->getNumChoices() == 1)
							{
								DoEventChoice(eEventChoice, eChosenEvent);
								if(isHuman())
								{
									CvPopupInfo kPopupInfo(BUTTONPOPUP_MODDER_7, eEventChoice, GetID(), getOwner());
									GC.GetEngineUserInterface()->AddPopup(kPopupInfo);
								}
								return;
							}
						}
					}
				}
			}
			if(iNumEvent > 0 && pkEventInfo->getNumChoices() > 1)
			{
				if(GET_PLAYER(getOwner()).isHuman())
				{
					CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
					if(pNotifications)
					{
						CvString strBuffer = GetLocalizedText("TXT_KEY_CHOOSE_EVENT_CHOICE_CITY");
						CvString strSummary = GetLocalizedText("TXT_KEY_CHOOSE_EVENT_CHOICE_CITY_TT");
						pNotifications->Add((NotificationTypes)FString::Hash("NOTIFICATION_EVENT_CHOICE_CITY"), strSummary.c_str(), strBuffer.c_str(), -1, -1, eChosenEvent, GetID());
					}
				}
				else
				{
					//Lua Hook
					if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_OverrideAICityEvent, getOwner(), GetID(), eChosenEvent) == GAMEEVENTRETURN_TRUE) 
					{
						return;
					}
					AI_DoEventChoice(eChosenEvent);
				}
			}
		}
	}
}
bool CvCity::IsCityEventValid(CityEventTypes eEvent)
{
	CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
	if(pkEventInfo == NULL)
	{
		return false;
	}

	//Lua Hook
	if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_CityEventCanActivate, getOwner(), GetID(), eEvent) == GAMEEVENTRETURN_FALSE) 
	{
		return false;
	}

	EventClassTypes eEventClass = (EventClassTypes)pkEventInfo->getEventClass();
	if(eEventClass != NO_EVENT_CLASS)
	{
		if(eEventClass == EVENT_CLASS_GOOD)
		{
			if(GC.getGame().isOption(GAMEOPTION_EVENTS_NO_GOOD))
				return false;
		}
		else if(eEventClass == EVENT_CLASS_BAD)
		{
			if(GC.getGame().isOption(GAMEOPTION_EVENTS_NO_BAD))
				return false;
		}
		else if(eEventClass == EVENT_CLASS_NEUTRAL)
		{
			if(GC.getGame().isOption(GAMEOPTION_EVENTS_NO_NEUTRAL))
				return false;
		}
		else if(eEventClass == EVENT_CLASS_TRADE)
		{
			if(GC.getGame().isOption(GAMEOPTION_EVENTS_NO_TRADE))
				return false;
		}
		else if(eEventClass == EVENT_CLASS_CIV_SPECIFIC)
		{
			if(GC.getGame().isOption(GAMEOPTION_EVENTS_NO_CIV_SPECIFIC))
				return false;
		}
	}

	CvPlayer &kPlayer = GET_PLAYER(m_eOwner);
	
	//Let's do our linker checks here.
	for(int iI = 0; iI <= pkEventInfo->GetNumLinkers(); iI++)
	{
		CvCityEventLinkingInfo *pLinkerInfo = pkEventInfo->GetLinkerInfo(iI);
		if(pLinkerInfo)
		{
			EventTypes eLinkerEvent = (EventTypes)pLinkerInfo->GetLinkingEvent();
			EventChoiceTypes eLinkerEventChoice = (EventChoiceTypes)pLinkerInfo->GetLinkingEventChoice();
			CityEventTypes eLinkerCityEvent = (CityEventTypes)pLinkerInfo->GetCityLinkingEvent();
			CityEventChoiceTypes eLinkerCityEventChoice = (CityEventChoiceTypes)pLinkerInfo->GetCityLinkingEventChoice();

			PlayerTypes ePlayer;
			for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
			{
				ePlayer = (PlayerTypes) iPlayerLoop;
				if(ePlayer == NO_PLAYER)
					continue;

				CvPlayer &kPlayer2 = GET_PLAYER(ePlayer);

				if(!pLinkerInfo->CheckOtherPlayers() && ePlayer != getOwner())
					continue;

				if(eLinkerEvent != NO_EVENT)
				{
					bool bActive = (kPlayer2.GetEventCooldown(eLinkerEvent) > 0 || kPlayer2.IsEventFired(eLinkerEvent));
					if(bActive != pLinkerInfo->CheckForActive())
						return false;
				}

				if(eLinkerEventChoice != NO_EVENT_CHOICE)
				{
					bool bActive = (kPlayer2.GetEventChoiceDuration(eLinkerEventChoice) > 0 || kPlayer2.IsEventChoiceFired(eLinkerEventChoice));
					if(bActive != pLinkerInfo->CheckForActive())
						return false;
				}

				if(eLinkerCityEvent != NO_EVENT_CITY || eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
				{
					int iLoop;
					CvCity* pLoopCity;
					for(pLoopCity = GET_PLAYER(ePlayer).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(ePlayer).nextCity(&iLoop))
					{
						if(pLoopCity != this && pLinkerInfo->CheckOnlyActiveCity())
							continue;

						if(eLinkerCityEvent != NO_EVENT_CITY)
						{
							bool bActive = (pLoopCity->GetEventCooldown(eLinkerCityEvent) > 0 || pLoopCity->IsEventFired(eLinkerCityEvent));
							if(bActive != pLinkerInfo->CheckForActive())
								return false;
						}

						if(eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
						{
							bool bActive = (pLoopCity->GetEventChoiceDuration(eLinkerCityEventChoice) > 0 || pLoopCity->IsEventChoiceFired(eLinkerCityEventChoice));
							if(bActive != pLinkerInfo->CheckForActive())
								return false;
						}
					}
				}
			}
		}
	}
		
	//Let's narrow down all events here!
	if(pkEventInfo->isCapital() && !isCapital())
		return false;

	if(pkEventInfo->isCoastal() && !isCoastal())
		return false;

	if(pkEventInfo->isRiver() && !plot()->isRiver())
		return false;

	if(pkEventInfo->isRequiresGarrison() && !HasGarrison())
		return false;

	if(pkEventInfo->hasStateReligion() && kPlayer.GetReligions()->GetStateReligion() == NO_RELIGION)
		return false;

	if(pkEventInfo->getPrereqTech() != -1 && !GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getPrereqTech()))
		return false;

	if(pkEventInfo->getObsoleteTech() != -1 && GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getPrereqTech()))
		return false;

	if(pkEventInfo->getRequiredEra() != -1 && kPlayer.GetCurrentEra() < (EraTypes)pkEventInfo->getRequiredEra())
		return false;

	if(pkEventInfo->getObsoleteEra() != -1 && kPlayer.GetCurrentEra() >= (EraTypes)pkEventInfo->getObsoleteEra())
		return false;

	if(pkEventInfo->getMinimumPopulation() > 0 && getPopulation() < pkEventInfo->getMinimumPopulation())
		return false;

	if(pkEventInfo->isResistance() && GetResistanceTurns() <= 0)
		return false;

	if(pkEventInfo->isWLTKD() && GetWeLoveTheKingDayCounter() <= 0)
		return false;

	if(pkEventInfo->isPuppet() && !IsPuppet())
		return false;

	if(pkEventInfo->isRazing() && !IsRazing())
		return false;

	if(pkEventInfo->isOccupied() && (!IsOccupied() || IsNoOccupiedUnhappiness()))
		return false;

	if(pkEventInfo->hasAnyReligion() && GetCityReligions()->GetReligiousMajority() <= RELIGION_PANTHEON)
		return false;

	if(pkEventInfo->getRequiredCiv() != -1 && getCivilizationType() != (CivilizationTypes)pkEventInfo->getRequiredCiv())
		return false;

	if(pkEventInfo->getRequiredPolicy() != -1 && !kPlayer.GetPlayerPolicies()->HasPolicy((PolicyTypes)pkEventInfo->getRequiredPolicy()))
		return false;

	if(pkEventInfo->getRequiredIdeology() != -1 && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() != (PolicyBranchTypes)pkEventInfo->getRequiredIdeology())
		return false;

	if(pkEventInfo->hasPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() != GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->lacksPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() == GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->hasPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() != GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->lacksPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() == GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->getRequiredStateReligion() != -1)
	{
		if(kPlayer.GetReligions()->GetStateReligion() != pkEventInfo->getRequiredStateReligion())
			return false;
	}

	if(!pkEventInfo->isRequiresHolyCity() && pkEventInfo->getRequiredReligion() != -1)
	{
		if(GetCityReligions()->GetReligiousMajority() != (ReligionTypes)pkEventInfo->getRequiredReligion())
			return false;
	}

	if(pkEventInfo->isRequiresHolyCity())
	{
		if(pkEventInfo->getRequiredReligion() != -1)
		{
			if(!GetCityReligions()->IsHolyCityForReligion((ReligionTypes)pkEventInfo->getRequiredReligion()))
				return false;
		}
		else
		{
			if(!GetCityReligions()->IsHolyCityAnyReligion())
				return false;
		}
	}
	if(pkEventInfo->isRequiresIdeology() && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() == NO_POLICY_BRANCH_TYPE)
		return false;

	if(pkEventInfo->isRequiresWar() && kPlayer.GetMilitaryAI()->GetNumberCivsAtWarWith(false) <= 0)
		return false;

	if(pkEventInfo->hasCityConnection() && !IsRouteToCapitalConnected())
		return false;

	if(pkEventInfo->hasTradeConnection() && !HasTradeRouteFromAnyCity() && !HasTradeRouteToAnyCity())
		return false;

	if(pkEventInfo->isNearMountain() && GetNearbyMountains() <= 0)
		return false;

	if(pkEventInfo->isNearNaturalWonder() && !HasAnyNaturalWonder())
		return false;

	if(pkEventInfo->getMaximumPopulation() != 0 && getPopulation() > pkEventInfo->getMaximumPopulation())
		return false;

	if(pkEventInfo->hasPantheon() && GetCityReligions()->GetReligiousMajority() != RELIGION_PANTHEON)
		return false;

	if(pkEventInfo->isUnhappy() && !kPlayer.IsEmpireUnhappy())
		return false;

	if(pkEventInfo->isSuperUnhappy() && !kPlayer.IsEmpireSuperUnhappy())
		return false;

	if(pkEventInfo->hasMetAnotherCiv())
	{
		if(GET_TEAM(getTeam()).getHasMetCivCount(true) <= 0)
			return false;
	}

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isMaster() && GET_TEAM(getTeam()).GetNumVassals() <= 0)
		return false;

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isVassal() && !GET_TEAM(getTeam()).IsVassalOfSomeone())
		return false;

	if(pkEventInfo->isRequiresWarMinor())
	{
		bool bHas = false;
		for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
		{
			PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;

			// Is this a player we have relations with?
			if(GET_PLAYER(eLoopPlayer).isBarbarian())
			{
				continue;
			}
			if(!GET_PLAYER(eLoopPlayer).isMinorCiv())
			{
				continue;
			}
			if(eLoopPlayer != getOwner())
			{
				if(GET_TEAM(getTeam()).isAtWar(GET_PLAYER(eLoopPlayer).getTeam()))
				{
					bHas = true;
					break;
				}
			}
		}
		if(!bHas)
		{
			return false;
		}
	}

	if(pkEventInfo->getBuildingRequired() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingRequired();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) <= 0)
				return false;
		}
	}
	if(pkEventInfo->getBuildingLimiter() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingLimiter();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) > 0)
				return false;
		}
	}
	if(pkEventInfo->getRequiredImprovement() != -1)
	{
		ImprovementTypes eImprovement = (ImprovementTypes)pkEventInfo->getRequiredImprovement();
		if(eImprovement != NO_IMPROVEMENT)
		{
			if(!HasImprovement(eImprovement))
				return false;
		}
	}
	if(pkEventInfo->getLocalResourceRequired() != -1)
	{
		ResourceTypes eResource = (ResourceTypes)pkEventInfo->getLocalResourceRequired();
		if(eResource != NO_IMPROVEMENT)
		{
			if(!HasResource(eResource))
				return false;
		}
	}
	if(pkEventInfo->hasNearbyFeature() != -1)
	{
		FeatureTypes eFeature = (FeatureTypes)pkEventInfo->hasNearbyFeature();
		if(eFeature != NO_FEATURE)
		{
			if(!IsHasFeatureLocal(eFeature))
				return false;
		}
	}
	if(pkEventInfo->hasNearbyTerrain() != -1)
	{
		TerrainTypes eTerrain = (TerrainTypes)pkEventInfo->hasNearbyTerrain();
		if(eTerrain != NO_TERRAIN)
		{
			if(!HasTerrain(eTerrain))
				return false;
		}
	}
	//Check our minimum yields - this looks at stored values, not yields per turn.
	bool bHas = true;
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes)iI;
		if(eYield == NO_YIELD)
			return false;
							
		int iNeededYield = pkEventInfo->getYieldMinimum(eYield);
		iNeededYield *= GC.getGame().getGameSpeedInfo().getTrainPercent();
		iNeededYield /= 100;
		if(pkEventInfo->isEraScaling())
		{
			int iEra = kPlayer.GetCurrentEra();
			if(iEra <= 0)
			{
				iEra = 1;
			}
			iNeededYield *= iEra;
		}

		if(iNeededYield != 0)
		{
			if(eYield == YIELD_FOOD)
			{
				if(iNeededYield > getFood())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_PRODUCTION)
			{
				if(iNeededYield > getProduction())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_GOLD)
			{
				if(iNeededYield > kPlayer.GetTreasury()->GetGold())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_SCIENCE)
			{
				TechTypes eCurrentTech = kPlayer.GetPlayerTechs()->GetCurrentResearch();
				int iTech = 0;
				if(eCurrentTech != NO_TECH)
				{
					iTech = kPlayer.GetPlayerTechs()->GetResearchProgress(eCurrentTech);
				}
				if(iNeededYield > iTech)
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_FAITH)
			{
				if(iNeededYield > kPlayer.GetFaith())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_GOLDEN_AGE_POINTS)
			{
				if(iNeededYield > kPlayer.GetGoldenAgeProgressMeter())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_CULTURE)
			{
				if(iNeededYield > kPlayer.getJONSCulture())
				{
					bHas = false;
					break;
				}
			}
		}
	}
	if(!bHas)
	{
		return false;
	}

	if(pkEventInfo->isInDebt() && kPlayer.GetTreasury()->GetGold() > 0)
		return false;

	if(pkEventInfo->isLosingMoney() && kPlayer.GetTreasury()->CalculateBaseNetGold() > 0)
		return false;

	return true;
}
bool CvCity::IsCityEventChoiceValid(CityEventChoiceTypes eChosenEventChoice, CityEventTypes eParentEvent)
{
	if(eChosenEventChoice == NO_EVENT_CHOICE)
		return false;

	CvModEventCityChoiceInfo* pkEventInfo = GC.getCityEventChoiceInfo(eChosenEventChoice);
	if(pkEventInfo == NULL)
	{
		return false;
	}

	CvPlayer &kPlayer = GET_PLAYER(m_eOwner);

	//Exploit checks.
	if(kPlayer.isEndTurn())
		return false;

	if(!IsEventActive(eParentEvent))
		return false;

	//Lua Hook
	if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_CityEventChoiceCanTake, getOwner(), GetID(), eChosenEventChoice) == GAMEEVENTRETURN_FALSE) {
		return false;
	}

	if(!pkEventInfo->isParentEvent(eParentEvent))
		return false;

	if(pkEventInfo->isOneShot() && IsEventChoiceFired(eChosenEventChoice))
		return false;

	//Event Choice already active for this event? Abort!
	if(GetEventChoiceDuration(eChosenEventChoice) > 0)
	{
		if(GC.getLogging())
		{
			CvString playerName;
			FILogFile* pLog;
			CvString strBaseString;
			CvString strOutBuf;
			CvString strFileName = "EventCityLogging.csv";
			playerName = getName();
			pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
			strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
			strBaseString += playerName + ", ";
			strOutBuf.Format("Event choice already active for city, skipping: %s, Event: %s. Cooldown: %d", pkEventInfo->GetDescription(), pkEventInfo->GetDescription(), GetEventChoiceDuration(eChosenEventChoice));
			strBaseString += strOutBuf;
			pLog->Msg(strBaseString);
		}
		return false;
	}

	//Let's do our linker checks here.
	for(int iI = 0; iI <= pkEventInfo->GetNumLinkers(); iI++)
	{
		CvCityEventChoiceLinkingInfo *pLinkerInfo = pkEventInfo->GetLinkerInfo(iI);
		if(pLinkerInfo)
		{
			EventTypes eLinkerEvent = (EventTypes)pLinkerInfo->GetLinkingEvent();
			EventChoiceTypes eLinkerEventChoice = (EventChoiceTypes)pLinkerInfo->GetLinkingEventChoice();
			CityEventTypes eLinkerCityEvent = (CityEventTypes)pLinkerInfo->GetCityLinkingEvent();
			CityEventChoiceTypes eLinkerCityEventChoice = (CityEventChoiceTypes)pLinkerInfo->GetCityLinkingEventChoice();

			PlayerTypes ePlayer;
			for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
			{
				ePlayer = (PlayerTypes) iPlayerLoop;
				if(ePlayer == NO_PLAYER)
					continue;

				CvPlayer &kPlayer2 = GET_PLAYER(ePlayer);

				if(!pLinkerInfo->CheckOtherPlayers() && ePlayer != getOwner())
					continue;

				if(eLinkerEvent != NO_EVENT)
				{
					bool bActive = (kPlayer2.GetEventCooldown(eLinkerEvent) > 0 || kPlayer2.IsEventFired(eLinkerEvent));
					if(bActive != pLinkerInfo->CheckForActive())
						return false;
				}

				if(eLinkerEventChoice != NO_EVENT_CHOICE)
				{
					bool bActive = (kPlayer2.GetEventChoiceDuration(eLinkerEventChoice) > 0 || kPlayer2.IsEventChoiceFired(eLinkerEventChoice));
					if(bActive != pLinkerInfo->CheckForActive())
						return false;
				}

				if(eLinkerCityEvent != NO_EVENT_CITY || eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
				{
					int iLoop;
					CvCity* pLoopCity;
					for(pLoopCity = GET_PLAYER(ePlayer).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(ePlayer).nextCity(&iLoop))
					{
						if(pLoopCity != this && pLinkerInfo->CheckOnlyActiveCity())
							continue;

						if(eLinkerCityEvent != NO_EVENT_CITY)
						{
							bool bActive = (pLoopCity->GetEventCooldown(eLinkerCityEvent) > 0 || pLoopCity->IsEventFired(eLinkerCityEvent));
							if(bActive != pLinkerInfo->CheckForActive())
								return false;
						}

						if(eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
						{
							bool bActive = (pLoopCity->GetEventChoiceDuration(eLinkerCityEventChoice) > 0 || pLoopCity->IsEventChoiceFired(eLinkerCityEventChoice));
							if(bActive != pLinkerInfo->CheckForActive())
								return false;
						}
					}
				}
			}
		}
	}

	//Let's narrow down all events here!
	if(pkEventInfo->isCapital() && !isCapital())
		return false;

	if(pkEventInfo->isCoastal() && !isCoastal())
		return false;

	if(pkEventInfo->isRiver() && !plot()->isRiver())
		return false;

	if(pkEventInfo->isResistance() && GetResistanceTurns() <= 0)
		return false;

	if(pkEventInfo->isWLTKD() && GetWeLoveTheKingDayCounter() <= 0)
		return false;

	if(pkEventInfo->isPuppet() && !IsPuppet())
		return false;

	if(pkEventInfo->isRazing() && !IsRazing())
		return false;

	if(pkEventInfo->isOccupied() && (!IsOccupied() || IsNoOccupiedUnhappiness()))
		return false;

	if(pkEventInfo->hasAnyReligion() && GetCityReligions()->GetReligiousMajority() <= RELIGION_PANTHEON)
		return false;

	if(pkEventInfo->isRequiresGarrison() && !HasGarrison())
		return false;

	if(pkEventInfo->hasStateReligion() && kPlayer.GetReligions()->GetStateReligion() == NO_RELIGION)
		return false;

	if(pkEventInfo->getPrereqTech() != -1 && !GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getPrereqTech()))
		return false;

	if(pkEventInfo->getObsoleteTech() != -1 && GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getPrereqTech()))
		return false;

	if(pkEventInfo->getRequiredEra() != -1 && kPlayer.GetCurrentEra() < (EraTypes)pkEventInfo->getRequiredEra())
		return false;

	if(pkEventInfo->getObsoleteEra() != -1 && kPlayer.GetCurrentEra() >= (EraTypes)pkEventInfo->getObsoleteEra())
		return false;

	if(pkEventInfo->getMinimumPopulation() > 0 && getPopulation() < pkEventInfo->getMinimumPopulation())
		return false;

	if(pkEventInfo->getRequiredCiv() != -1 && getCivilizationType() != (CivilizationTypes)pkEventInfo->getRequiredCiv())
		return false;

	if(pkEventInfo->getRequiredPolicy() != -1 && !kPlayer.GetPlayerPolicies()->HasPolicy((PolicyTypes)pkEventInfo->getRequiredPolicy()))
		return false;

	if(pkEventInfo->getRequiredIdeology() != -1 && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() != (PolicyBranchTypes)pkEventInfo->getRequiredIdeology())
		return false;

	if(pkEventInfo->hasPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() != GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->lacksPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() == GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->hasPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() != GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->lacksPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() == GetCityReligions()->GetReligiousMajority())
		return false;

	if(pkEventInfo->getRequiredStateReligion() != -1)
	{
		if(kPlayer.GetReligions()->GetStateReligion() != pkEventInfo->getRequiredStateReligion())
			return false;
	}

	if(!pkEventInfo->isRequiresHolyCity() && pkEventInfo->getRequiredReligion() != -1)
	{
		if(GetCityReligions()->GetReligiousMajority() != (ReligionTypes)pkEventInfo->getRequiredReligion())
			return false;
	}

	if(pkEventInfo->isRequiresHolyCity())
	{
		if(pkEventInfo->getRequiredReligion() != -1)
		{
			if(!GetCityReligions()->IsHolyCityForReligion((ReligionTypes)pkEventInfo->getRequiredReligion()))
				return false;
		}
		else
		{
			if(!GetCityReligions()->IsHolyCityAnyReligion())
				return false;
		}
	}
	if(pkEventInfo->isRequiresIdeology() && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() == NO_POLICY_BRANCH_TYPE)
		return false;

	if(pkEventInfo->isRequiresWar() && kPlayer.GetMilitaryAI()->GetNumberCivsAtWarWith(false) <= 0)
		return false;

	if(pkEventInfo->hasCityConnection() && !IsRouteToCapitalConnected())
		return false;

	if(pkEventInfo->hasTradeConnection() && !HasTradeRouteFromAnyCity() && !HasTradeRouteToAnyCity()) 
		return false;

	if(pkEventInfo->isNearMountain() && GetNearbyMountains() <= 0)
		return false;

	if(pkEventInfo->isNearNaturalWonder() && !HasAnyNaturalWonder())
		return false;

	if(pkEventInfo->getMaximumPopulation() != 0 && getPopulation() > pkEventInfo->getMaximumPopulation())
		return false;

	if(pkEventInfo->hasPantheon() && GetCityReligions()->GetReligiousMajority() != RELIGION_PANTHEON)
		return false;

	if(pkEventInfo->isUnhappy() && !kPlayer.IsEmpireUnhappy())
		return false;

	if(pkEventInfo->isSuperUnhappy() && !kPlayer.IsEmpireSuperUnhappy())
		return false;

	if(pkEventInfo->hasMetAnotherCiv())
	{
		if(GET_TEAM(getTeam()).getHasMetCivCount(true) <= 0)
			return false;
	}

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isMaster() && GET_TEAM(getTeam()).GetNumVassals() <= 0)
		return false;

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isVassal() && !GET_TEAM(getTeam()).IsVassalOfSomeone())
		return false;

	if(pkEventInfo->hasNearbyFeature() != -1)
	{
		FeatureTypes eFeature = (FeatureTypes)pkEventInfo->hasNearbyFeature();
		if(eFeature != NO_FEATURE)
		{
			if(!IsHasFeatureLocal(eFeature))
				return false;
		}
	}
	if(pkEventInfo->hasNearbyTerrain() != -1)
	{
		TerrainTypes eTerrain = (TerrainTypes)pkEventInfo->hasNearbyTerrain();
		if(eTerrain != NO_TERRAIN)
		{
			if(!HasTerrain(eTerrain))
				return false;
		}
	}

	if(pkEventInfo->isRequiresWarMinor())
	{
		bool bHas = false;
		for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
		{
			PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;

			// Is this a player we have relations with?
			if(GET_PLAYER(eLoopPlayer).isBarbarian())
			{
				continue;
			}
			if(!GET_PLAYER(eLoopPlayer).isMinorCiv())
			{
				continue;
			}
			if(eLoopPlayer != getOwner())
			{
				if(GET_TEAM(getTeam()).isAtWar(GET_PLAYER(eLoopPlayer).getTeam()))
				{
					bHas = true;
					break;
				}
			}
		}
		if(!bHas)
		{
			return false;
		}
	}

	if(pkEventInfo->getBuildingRequired() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingRequired();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) <= 0)
				return false;
		}
	}
	if(pkEventInfo->getBuildingLimiter() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingLimiter();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) > 0)
				return false;
		}
	}
	if(pkEventInfo->getRequiredImprovement() != -1)
	{
		ImprovementTypes eImprovement = (ImprovementTypes)pkEventInfo->getRequiredImprovement();
		if(eImprovement != NO_IMPROVEMENT)
		{
			if(!HasImprovement(eImprovement))
				return false;
		}
	}
	if(pkEventInfo->getLocalResourceRequired() != -1)
	{
		ResourceTypes eResource = (ResourceTypes)pkEventInfo->getLocalResourceRequired();
		if(eResource != NO_IMPROVEMENT)
		{
			if(!HasResource(eResource))
				return false;
		}
	}
	//Check our minimum yields - this looks at stored values, not yields per turn.
	bool bHas = true;
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes)iI;
		if(eYield == NO_YIELD)
			continue;
							
		int iNeededYield = pkEventInfo->getYieldMinimum(eYield);
		if(pkEventInfo->getPreCheckEventYield(eYield) != 0)
		{
			if(iNeededYield < pkEventInfo->getPreCheckEventYield(eYield))
			{
				iNeededYield = pkEventInfo->getPreCheckEventYield(eYield);
			}
		}
		iNeededYield *= GC.getGame().getGameSpeedInfo().getTrainPercent();
		iNeededYield /= 100;
		if(pkEventInfo->IsEraScaling())
		{
			int iEra = kPlayer.GetCurrentEra();
			if(iEra <= 0)
			{
				iEra = 1;
			}
			iNeededYield *= iEra;
		}
		if(iNeededYield != 0)
		{
			if(eYield == YIELD_FOOD)
			{
				if(iNeededYield > getFood())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_PRODUCTION)
			{
				if(iNeededYield > getProduction())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_GOLD)
			{
				if(iNeededYield > kPlayer.GetTreasury()->GetGold())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_SCIENCE)
			{
				TechTypes eCurrentTech = kPlayer.GetPlayerTechs()->GetCurrentResearch();
				int iTech = 0;
				if(eCurrentTech != NO_TECH)
				{
					iTech = kPlayer.GetPlayerTechs()->GetResearchProgress(eCurrentTech);
				}
				if(iNeededYield > iTech)
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_FAITH)
			{
				if(iNeededYield > kPlayer.GetFaith())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_GOLDEN_AGE_POINTS)
			{
				if(iNeededYield > kPlayer.GetGoldenAgeProgressMeter())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_CULTURE)
			{
				if(iNeededYield > kPlayer.getJONSCulture())
				{
					bHas = false;
					break;
				}
			}
		}
	}
	if(!bHas)
	{
		return false;
	}

	if(pkEventInfo->isInDebt() && kPlayer.GetTreasury()->GetGold() > 0)
		return false;

	if(pkEventInfo->isLosingMoney() && kPlayer.GetTreasury()->CalculateBaseNetGold() > 0)
		return false;

	return true;
}
void CvCity::DoCancelEventChoice(CityEventChoiceTypes eChosenEventChoice)
{
	if(eChosenEventChoice == NO_EVENT_CHOICE)
		return;

	CvModEventCityChoiceInfo* pkEventChoiceInfo = GC.getCityEventChoiceInfo(eChosenEventChoice);
	if(pkEventChoiceInfo != NULL)
	{
		//Lua Hook
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityEventChoiceEnded, getOwner(), GetID(), eChosenEventChoice);
		bool bChanged = false;
		if(GC.getLogging())
		{
			CvString playerName;
			FILogFile* pLog;
			CvString strBaseString;
			CvString strOutBuf;
			CvString strFileName = "EventCityLogging.csv";
			playerName = getName();
			pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
			strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
			strBaseString += playerName + ", ";
			strOutBuf.Format("Event choice ended for city: %s", pkEventChoiceInfo->GetDescription());
			strBaseString += strOutBuf;
			pLog->Msg(strBaseString);
		}
		//Let's make sure this is at zero.
		ChangeEventChoiceDuration(eChosenEventChoice, -GetEventChoiceDuration(eChosenEventChoice));
		
		//Let's only reverse if it expires, and it was active.
		if(IsEventChoiceActive(eChosenEventChoice) && pkEventChoiceInfo->Expires())
		{
			if(pkEventChoiceInfo->getEventBuilding() != -1)
			{
				BuildingClassTypes eBuildingClass = (BuildingClassTypes)pkEventChoiceInfo->getEventBuilding();
				if(eBuildingClass != NO_BUILDINGCLASS)
				{
					const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
	
					if (pkBuildingClassInfo)
					{
						CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());
		
						if (pCivilizationInfo != NULL)
						{
							BuildingTypes eBuildingType = (BuildingTypes) pCivilizationInfo->getCivilizationBuildings(eBuildingClass);
							if(eBuildingType != NO_BUILDING)
							{
								GetCityBuildings()->SetNumRealBuilding(eBuildingType, 0);
								bChanged = true;
							}
						}
					}
				}
			}
			if(pkEventChoiceInfo->getEventPromotion() != -1)
			{
				PromotionTypes ePromotion = (PromotionTypes)pkEventChoiceInfo->getEventPromotion();
				if(ePromotion != -1)
				{
					changeFreePromotionCount(ePromotion, -1);
				}
			}
			for(int iI = 0; iI < GC.getNumResourceInfos(); iI++)
			{
				ResourceTypes eResource = (ResourceTypes)iI;
				if(eResource != NO_RESOURCE)
				{
					int iBonus = pkEventChoiceInfo->getEventResourceChange(eResource);
					iBonus *= -1;
					if(iBonus != 0)
					{
						GET_PLAYER(getOwner()).changeNumResourceTotal(eResource, iBonus * -1);
					}
				}
			}
			for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				YieldTypes eYield = (YieldTypes)iI;
				if(eYield == NO_YIELD)
					continue;

				if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
				{
					int iUnhappinessNeedMod = pkEventChoiceInfo->getCityUnhappinessNeedMod(eYield);
					if(iUnhappinessNeedMod != 0)
					{
						iUnhappinessNeedMod *= -1;
						if(eYield == YIELD_GOLD)
						{
							ChangePovertyUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_CULTURE)
						{
							ChangeUnculturedUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_PRODUCTION)
						{
							ChangeDefenseUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_SCIENCE)
						{
							ChangeIlliteracyUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_FAITH)
						{
							ChangeMinorityUnhappiness(iUnhappinessNeedMod);
						}
					}
				}

				int iYieldChange = pkEventChoiceInfo->getCityYield(eYield);
				if(iYieldChange != 0)
				{
					if(pkEventChoiceInfo->IsEraScaling())
					{
						int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
						if(iEra <= 0)
						{
							iEra = 1;
						}
						iYieldChange *= iEra;
					}
					ChangeEventCityYield(eYield, iYieldChange * -1);
				}
				// Building modifiers
				for(int iJ = 0; iJ < GC.getNumBuildingClassInfos(); iJ++)
				{
					BuildingClassTypes eBuildingClass = (BuildingClassTypes) iJ;

					CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
					if(!pkBuildingClassInfo)
					{
						continue;
					}
					if(pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield) != 0)
					{
						BuildingTypes eBuilding = (BuildingTypes) getCivilizationInfo().getCivilizationBuildings(eBuildingClass);

						if(eBuilding != NO_BUILDING)
						{
							CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
							if(pkBuilding)
							{
								int iBuildingCount = GetCityBuildings()->GetNumBuilding(eBuilding);

								ChangeEventBuildingClassYield(eBuildingClass, eYield, pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield) * -1);
								if(iBuildingCount > 0)
								{
									ChangeBaseYieldRateFromBuildings(eYield, pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield) * -1);
									bChanged = true;
								}
							}
						}
					}
					if(pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield) != 0)
					{
						BuildingTypes eBuilding = (BuildingTypes) getCivilizationInfo().getCivilizationBuildings(eBuildingClass);

						if(eBuilding != NO_BUILDING)
						{
							CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
							if(pkBuilding)
							{
								int iBuildingCount = GetCityBuildings()->GetNumBuilding(eBuilding);
								ChangeEventBuildingClassYieldModifier(eBuildingClass, eYield, pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield) * -1);
								bChanged = true;
								if(iBuildingCount > 0)
								{
									changeYieldRateModifier(eYield, pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield) * -1);
								}
							}
						}
					}
				}
				for(int iJ = 0; iJ < GC.getNumImprovementInfos(); iJ++)
				{
					ImprovementTypes eImprovement = (ImprovementTypes)iJ;
					if(eImprovement != NO_IMPROVEMENT && pkEventChoiceInfo->getImprovementYield(eImprovement, eYield) != 0)
					{
						ChangeEventImprovementYield(eImprovement, eYield, pkEventChoiceInfo->getImprovementYield(eImprovement, eYield) * -1);
						bChanged = true;
					}
				}
				for(int iJ = 0; iJ < GC.getNumFeatureInfos(); iJ++)
				{
					FeatureTypes eFeature = (FeatureTypes)iJ;
					if(eFeature != NO_FEATURE && pkEventChoiceInfo->getFeatureYield(eFeature, eYield) != 0)
					{
						ChangeEventFeatureYield(eFeature, eYield, pkEventChoiceInfo->getFeatureYield(eFeature, eYield) * -1);
						bChanged = true;
					}
				}
				for(int iJ = 0; iJ < GC.getNumTerrainInfos(); iJ++)
				{
					TerrainTypes eTerrain = (TerrainTypes)iJ;
					if(eTerrain != NO_TERRAIN && pkEventChoiceInfo->getTerrainYield(eTerrain, eYield) != 0)
					{
						ChangeEventTerrainYield(eTerrain, eYield, pkEventChoiceInfo->getTerrainYield(eTerrain, eYield) * -1);
						bChanged = true;
					}
				}
				for(int iJ = 0; iJ < GC.getNumResourceInfos(); iJ++)
				{
					ResourceTypes eResource = (ResourceTypes)iJ;
					if(eResource != NO_RESOURCE && pkEventChoiceInfo->getResourceYield(eResource, eYield) != 0)
					{
						ChangeEventResourceYield(eResource, eYield, pkEventChoiceInfo->getResourceYield(eResource, eYield) * -1);
						bChanged = true;
					}
				}
				for(int iJ = 0; iJ < GC.getNumSpecialistInfos(); iJ++)
				{
					const SpecialistTypes eSpecialist = static_cast<SpecialistTypes>(iJ);
					CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo(eSpecialist);
					if(pkSpecialistInfo)
					{
						ChangeEventSpecialistYield(eSpecialist, eYield, pkEventChoiceInfo->getCitySpecialistYieldChange(eSpecialist, eYield) * -1);
						bChanged = true;
					}
				}
			}
			if(pkEventChoiceInfo->getCityHappiness() != 0)
			{
				ChangeEventHappiness(pkEventChoiceInfo->getCityHappiness() * -1);
				bChanged = true;
			}
			if(bChanged)
			{
				CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
				if(pNotifications && GC.getGame().getActivePlayer() == getOwner())
				{
					Localization::String strMessage;
					Localization::String strSummary;
					strMessage = Localization::Lookup("TXT_KEY_NOTIFICATION_EVENT_HAS_ENDED_CITY");
					strMessage << GetScaledHelpText(eChosenEventChoice, false);
					strMessage << getNameKey();
					strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_EVENT_HAS_ENDED_CITY_T");
					strSummary << getNameKey();

					pNotifications->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), GetID(), getOwner());
				}
				for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
				{
					YieldTypes eYield = (YieldTypes) iI;
					if(eYield == NO_YIELD)
						continue;

					UpdateSpecialReligionYields(eYield);
					UpdateCityYields(eYield);
				}
				UpdateReligion(GetCityReligions()->GetReligiousMajority());
				GET_PLAYER(getOwner()).CalculateNetHappiness();
				GetCityCulture()->CalculateBaseTourismBeforeModifiers();
				GetCityCulture()->CalculateBaseTourism();
			}
		}
		if (!pkEventChoiceInfo->isOneShot())
		{
			//Set it false here so we know the event choice is over now.
			SetEventChoiceActive(eChosenEventChoice, false);
		}
	}
}
CvString CvCity::GetScaledHelpText(CityEventChoiceTypes eEventChoice, bool bYieldsOnly)
{
	CvString CoreYieldTip = "";
	CvModEventCityChoiceInfo* pkEventChoiceInfo = GC.getCityEventChoiceInfo(eEventChoice);
	if(pkEventChoiceInfo == NULL)
	{
		return "";
	}
	Localization::String localizedCoreText;
	if(bYieldsOnly)
	{
		localizedCoreText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_CITY_HEADER");
	}
	else
	{
		localizedCoreText = Localization::Lookup(pkEventChoiceInfo->GetHelp());
	}

	int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
	if(iEra <= 0)
	{
		iEra = 1;
	}
	CvString yieldCostTip = "";
	CvString yieldInstantTip = "";
	CvString yieldCityTip = "";
	CvString yieldSpecialistTip = "";
	CvString turnsTip = "";
				
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eIndex = (YieldTypes)iI;
		if(eIndex == NO_YIELD)
		{
			continue;
		}
		CvYieldInfo* pYield = GC.getYieldInfo(eIndex);
		if(pYield)
		{
			int iPreValue = pkEventChoiceInfo->getPreCheckEventYield(eIndex);
			if(pkEventChoiceInfo->IsEraScaling())
			{
				iPreValue *= iEra;
			}
			iPreValue *= GC.getGame().getGameSpeedInfo().getTrainPercent();
			iPreValue /= 100;
			if(iPreValue != 0)
			{
				iPreValue *= -1;
				if(yieldCostTip != "")
				{
					yieldCostTip += ", ";
				}
				Localization::String localizedCostText;
				if(bYieldsOnly)
				{
					localizedCostText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_SPENT");
				}
				else
				{
					localizedCostText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED");
				}
				localizedCostText << pYield->GetDescription();
				localizedCostText << pYield->getIconString();
				localizedCostText << iPreValue;

				const char* const localized = localizedCostText.toUTF8();
				if(localized)
				{
					yieldCostTip += localized;
				}
			}
			int iYieldValue = pkEventChoiceInfo->getEventYield(eIndex);
			if(pkEventChoiceInfo->IsEraScaling())
			{
				iYieldValue *= iEra;
			}
			iYieldValue *= GC.getGame().getGameSpeedInfo().getTrainPercent();
			iYieldValue /= 100;
			if(iYieldValue != 0)
			{
				if(yieldInstantTip != "")
				{
					yieldInstantTip += ", ";
				}
				Localization::String localizedEventText;
				if(bYieldsOnly)
				{
					localizedEventText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_INSTANT");
				}
				else
				{
					localizedEventText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED");
				}
				localizedEventText << pYield->GetDescription();
				localizedEventText << pYield->getIconString();
				localizedEventText << iYieldValue;

				const char* const localized = localizedEventText.toUTF8();
				if(localized)
				{
					yieldInstantTip += localized;
				}
			}
			int iCityValue = pkEventChoiceInfo->getCityYield(eIndex);
			if(pkEventChoiceInfo->IsEraScaling())
			{
				iCityValue *= iEra;
			}
			if(iCityValue != 0)
			{
				if(yieldCityTip != "")
				{
					yieldCityTip += ", ";
				}

				Localization::String localizedCityText;
				if(bYieldsOnly)
				{
					localizedCityText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_CITY");
				}
				else
				{
					localizedCityText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED");
				}
				localizedCityText << pYield->GetDescription();
				localizedCityText << pYield->getIconString();
				localizedCityText << iCityValue;

				const char* const localized = localizedCityText.toUTF8();
				if(localized)
				{
					yieldCityTip += localized;
				}
			}
		}
	}
	for(int iSpecialistLoop = 0; iSpecialistLoop < GC.getNumSpecialistInfos(); iSpecialistLoop++)
	{
		const SpecialistTypes eSpecialist = static_cast<SpecialistTypes>(iSpecialistLoop);
		CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo(eSpecialist);
		if(pkSpecialistInfo)
		{
			int iValue = pkEventChoiceInfo->getEventGPChange(eSpecialist);
			if(pkEventChoiceInfo->IsEraScaling())
			{
				iValue *= iEra;
				iValue *= GC.getGame().getGameSpeedInfo().getGreatPeoplePercent();
				iValue /= 100;
			}
			if(iValue > 0)
			{
				if(yieldSpecialistTip != "")
				{
					yieldSpecialistTip += ", ";
				}
				Localization::String localizedSpecialistText;
				if(bYieldsOnly)
				{
					localizedSpecialistText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_GPP");
				}
				else
				{
					localizedSpecialistText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_SPECIALIST");
				}

				CvUnitClassInfo* pkUnitClassInfo = GC.getUnitClassInfo((UnitClassTypes)pkSpecialistInfo->getGreatPeopleUnitClass());
				if(pkUnitClassInfo)
				{
					localizedSpecialistText << pkUnitClassInfo->GetDescription();
					localizedSpecialistText << iValue;
				}

				const char* const localized = localizedSpecialistText.toUTF8();
				if(localized)
				{
					yieldSpecialistTip += localized;
				}
			}
		}
	}
	localizedCoreText << yieldCostTip;
	localizedCoreText << yieldInstantTip;
	localizedCoreText << yieldCityTip;
	localizedCoreText << yieldSpecialistTip;

	//Duration
	int iDuration = pkEventChoiceInfo->getEventDuration();
	if(iDuration > 0)
	{
		iDuration *= GC.getGame().getGameSpeedInfo().getTrainPercent();
		iDuration /= 100;
		Localization::String localizedDurationText;
		if(bYieldsOnly)
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_EVENT_YIELD_SCALED_NOTIFICATION_TURNS");
		}
		else
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_EVENT_YIELD_DURATION");
		}
		localizedDurationText << iDuration;
		const char* const localized = localizedDurationText.toUTF8();
		if(localized)
		{
			turnsTip += localized;
		}
	}
	localizedCoreText << turnsTip;

	const char* const finallocalized = localizedCoreText.toUTF8();
	if(finallocalized)
	{
		CoreYieldTip = finallocalized;
	}
	return CoreYieldTip.c_str();
}
CvString CvCity::GetDisabledTooltip(CityEventChoiceTypes eChosenEventChoice)
{
	CvString DisabledTT = Localization::Lookup("TXT_KEY_EVENT_DISABLED_REASONS_HEADER").toUTF8();
	Localization::String localizedDurationText;

	if(eChosenEventChoice == NO_EVENT_CHOICE)
		return "";

	CvModEventCityChoiceInfo* pkEventInfo = GC.getCityEventChoiceInfo(eChosenEventChoice);
	if(pkEventInfo == NULL)
	{
		return "";
	}

	CvString strOverrideText = GetLocalizedText(pkEventInfo->getDisabledTooltip());
	if(strOverrideText != "")
	{
		return strOverrideText.c_str();
	}

	CvPlayer &kPlayer = GET_PLAYER(m_eOwner);

	//Lua Hook
	if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_CityEventChoiceCanTake, getOwner(), GetID(), eChosenEventChoice) == GAMEEVENTRETURN_FALSE) 
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_EVENT_DISABLED_LUA");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isOneShot() && IsEventChoiceFired(eChosenEventChoice))
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_EVENT_ONESHOT");
		DisabledTT += localizedDurationText.toUTF8();
	}

	//Let's do our linker checks here.
	for(int iI = 0; iI <= pkEventInfo->GetNumLinkers(); iI++)
	{
		CvCityEventChoiceLinkingInfo *pLinkerInfo = pkEventInfo->GetLinkerInfo(iI);
		if(pLinkerInfo)
		{
			EventTypes eLinkerEvent = (EventTypes)pLinkerInfo->GetLinkingEvent();
			EventChoiceTypes eLinkerEventChoice = (EventChoiceTypes)pLinkerInfo->GetLinkingEventChoice();
			CityEventTypes eLinkerCityEvent = (CityEventTypes)pLinkerInfo->GetCityLinkingEvent();
			CityEventChoiceTypes eLinkerCityEventChoice = (CityEventChoiceTypes)pLinkerInfo->GetCityLinkingEventChoice();

			PlayerTypes ePlayer;

			bool bEventFound = false;
			bool bEventChoiceFound = false;
			bool bCityEventFound = false;
			bool bCityEventChoiceFound = false;
			for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
			{
				ePlayer = (PlayerTypes) iPlayerLoop;
				if(ePlayer == NO_PLAYER)
					continue;

				CvPlayer &kPlayer2 = GET_PLAYER(ePlayer);

				if(!pLinkerInfo->CheckOtherPlayers() && ePlayer != getOwner())
					continue;

				if(eLinkerEvent != NO_EVENT && !bEventFound)
				{
					bool bActive = (kPlayer2.GetEventCooldown(eLinkerEvent) > 0 || kPlayer2.IsEventFired(eLinkerEvent));
					if(bActive != pLinkerInfo->CheckForActive())
					{
						if(bActive)
						{
							if(pLinkerInfo->CheckOtherPlayers())
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_EVENT_ACTIVE");
							}
							else
							{	
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ACTIVE_EVENT");
							}
						}
						else
						{
							if(pLinkerInfo->CheckOtherPlayers())
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_EVENT_NO_ACTIVE");
							}
							else
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_ACTIVE_PLAYER_EVENT");
								
							}
						}
						localizedDurationText << GC.getEventInfo(eLinkerEvent)->GetDescription();
						DisabledTT += localizedDurationText.toUTF8();
						bEventFound = true;
					}
				}

				if(eLinkerEventChoice != NO_EVENT_CHOICE && !bEventChoiceFound)
				{
					bool bActive = (kPlayer2.GetEventChoiceDuration(eLinkerEventChoice) > 0 || kPlayer2.IsEventChoiceFired(eLinkerEventChoice));
					if(bActive != pLinkerInfo->CheckForActive())
					{
						if(bActive)
						{
							if(pLinkerInfo->CheckOtherPlayers())
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_EVENT_CHOICE_ACTIVE");

							}
							else
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ACTIVE_EVENT_CHOICE");
							}
						}
						else
						{
							if(pLinkerInfo->CheckOtherPlayers())
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_NO_EVENT_CHOICE_ACTIVE");
							}
							else
							{
								localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_ACTIVE_PLAYER_EVENT_CHOICE");
							}
						}
						localizedDurationText << GC.getEventChoiceInfo(eLinkerEventChoice)->GetDescription();
						DisabledTT += localizedDurationText.toUTF8();
						bEventChoiceFound = true;
						break;
					}
				}

				if(eLinkerCityEvent != NO_EVENT_CITY || eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
				{
					int iLoop;
					CvCity* pLoopCity;
					for(pLoopCity = GET_PLAYER(ePlayer).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(ePlayer).nextCity(&iLoop))
					{
						if(pLoopCity != this && pLinkerInfo->CheckOnlyActiveCity())
							continue;

						if(eLinkerCityEvent != NO_EVENT_CITY && !bCityEventFound)
						{
							bool bActive = (pLoopCity->GetEventCooldown(eLinkerCityEvent) > 0 || pLoopCity->IsEventFired(eLinkerCityEvent));
							if(bActive != pLinkerInfo->CheckForActive())
							{
								if(bActive)
								{
									if(pLinkerInfo->CheckOtherPlayers())
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_CITY_EVENT_ACTIVE");
									}
									else
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ACTIVE_CITY_EVENT");
									}
								}
								else
								{
									if(pLinkerInfo->CheckOtherPlayers())
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_NO_CITY_EVENT_ACTIVE");
									}
									else
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_ACTIVE_CITY_EVENT");
									}
								}
								localizedDurationText << GC.getCityEventInfo(eLinkerCityEvent)->GetDescription();
								DisabledTT += localizedDurationText.toUTF8();
								bCityEventFound = true;
								break;
							}
						}

						if(!bCityEventChoiceFound && eLinkerCityEventChoice != NO_EVENT_CHOICE_CITY)
						{
							bool bActive = (pLoopCity->GetEventChoiceDuration(eLinkerCityEventChoice) > 0 || pLoopCity->IsEventChoiceFired(eLinkerCityEventChoice));
							if(bActive != pLinkerInfo->CheckForActive())
							{
								if(bActive)
								{
									if(pLinkerInfo->CheckOtherPlayers())
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_CITY_EVENT_CHOICE_ACTIVE");
									}
									else
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ACTIVE_CITY_EVENT_CHOICE");
									}
								}
								else
								{
									if(pLinkerInfo->CheckOtherPlayers())
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_OTHER_PLAYER_CITY_EVENT_CHOICE_NO_ACTIVE");
									}
									else
									{
										localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_ACTIVE_CITY_EVENT_CHOICE");
									}
								}
								localizedDurationText << GC.getCityEventChoiceInfo(eLinkerCityEventChoice)->GetDescription();
								DisabledTT += localizedDurationText.toUTF8();
								bCityEventChoiceFound = true;
								break;
							}
						}
					}
				}
			}
		}
	}

	//Let's narrow down all events here!
	if(GetEventChoiceDuration(eChosenEventChoice) > 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_EVENT_ACTIVE");
		DisabledTT += localizedDurationText.toUTF8();
	}

	//Let's narrow down all events here!
	if(pkEventInfo->isCapital() && !isCapital())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_CAPITAL");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isCoastal() && !isCoastal())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_COASTAL");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isRiver() && !plot()->isRiver())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_RIVER");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isResistance() && GetResistanceTurns() <= 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_RESISTANCE");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isWLTKD() && GetWeLoveTheKingDayCounter() <= 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_WLTKD");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isPuppet() && !IsPuppet())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_PUPPET");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isRazing() && !IsRazing())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_RAZING");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isOccupied() && (!IsOccupied() || IsNoOccupiedUnhappiness()))
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_OCCUPIED");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasAnyReligion() && GetCityReligions()->GetReligiousMajority() <= RELIGION_PANTHEON)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ANY_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isRequiresGarrison() && !HasGarrison())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_GARRISON");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasStateReligion() && kPlayer.GetReligions()->GetStateReligion() == NO_RELIGION)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ANY_STATE_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getPrereqTech() != -1 && !GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getPrereqTech()))
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_TECH");
		localizedDurationText << GC.getTechInfo((TechTypes)pkEventInfo->getPrereqTech())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getObsoleteTech() != -1 && GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkEventInfo->getObsoleteTech()))
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_OBSOLETE_TECH");
		localizedDurationText << GC.getTechInfo((TechTypes)pkEventInfo->getObsoleteTech())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getRequiredEra() != -1 && kPlayer.GetCurrentEra() < (EraTypes)pkEventInfo->getRequiredEra())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ERA");
		localizedDurationText << GC.getEraInfo((EraTypes)pkEventInfo->getRequiredEra())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getObsoleteEra() != -1 && kPlayer.GetCurrentEra() >= (EraTypes)pkEventInfo->getObsoleteEra())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_OBSOLETE_ERA");
		localizedDurationText << GC.getEraInfo((EraTypes)pkEventInfo->getObsoleteEra())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getMinimumPopulation() > 0 && getPopulation() < pkEventInfo->getMinimumPopulation())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_POP_LOCAL");
		localizedDurationText << pkEventInfo->getMinimumPopulation();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getRequiredCiv() != -1 && getCivilizationType() != (CivilizationTypes)pkEventInfo->getRequiredCiv())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_CIV");
		localizedDurationText << GC.getCivilizationInfo((CivilizationTypes)pkEventInfo->getRequiredCiv())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getRequiredPolicy() != -1 && !kPlayer.GetPlayerPolicies()->HasPolicy((PolicyTypes)pkEventInfo->getRequiredPolicy()))
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_POLICY");
		localizedDurationText << GC.getPolicyInfo((PolicyTypes)pkEventInfo->getRequiredPolicy())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getRequiredIdeology() != -1 && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() != (PolicyBranchTypes)pkEventInfo->getRequiredIdeology())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_IDEOLOGY");
		localizedDurationText << GC.getPolicyBranchInfo((PolicyBranchTypes)pkEventInfo->getRequiredIdeology())->GetDescription();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() != GetCityReligions()->GetReligiousMajority())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_PLAYER_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->lacksPlayerReligion() && kPlayer.GetReligions()->GetReligionCreatedByPlayer() == GetCityReligions()->GetReligiousMajority())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_PLAYER_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() != GetCityReligions()->GetReligiousMajority())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_PLAYER_MAJORITY_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->lacksPlayerMajority() && kPlayer.GetReligions()->GetReligionInMostCities() == GetCityReligions()->GetReligiousMajority())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_PLAYER_MAJORITY_RELIGION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getRequiredStateReligion() != -1)
	{
		if(kPlayer.GetReligions()->GetStateReligion() != pkEventInfo->getRequiredStateReligion())
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_NEED_SPECIFIC_STATE_RELIGION");
			localizedDurationText << GC.getReligionInfo((ReligionTypes)pkEventInfo->getRequiredStateReligion())->GetDescription();
			DisabledTT += localizedDurationText.toUTF8();
		}
	}

	if(!pkEventInfo->isRequiresHolyCity() && pkEventInfo->getRequiredReligion() != -1)
	{
		if(GetCityReligions()->GetReligiousMajority() != (ReligionTypes)pkEventInfo->getRequiredReligion())
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_NEED_SPECIFIC_RELIGION");
			localizedDurationText << GC.getReligionInfo((ReligionTypes)pkEventInfo->getRequiredReligion())->GetDescription();
			DisabledTT += localizedDurationText.toUTF8();
		}
	}

	if(pkEventInfo->isRequiresHolyCity())
	{
		if(pkEventInfo->getRequiredReligion() != -1)
		{
			if(!GetCityReligions()->IsHolyCityForReligion((ReligionTypes)pkEventInfo->getRequiredReligion()))
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_HOLY_CITY_SPECIFIC_LOCAL");
				localizedDurationText << GC.getReligionInfo((ReligionTypes)pkEventInfo->getRequiredReligion())->GetDescription();
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
		else
		{
			if(!GetCityReligions()->IsHolyCityAnyReligion())
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_HOLY_CITY_LOCAL");
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
	}
	if(pkEventInfo->isRequiresIdeology() && kPlayer.GetPlayerPolicies()->GetLateGamePolicyTree() == NO_POLICY_BRANCH_TYPE)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ANY_IDEOLOGY");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isRequiresWar() && kPlayer.GetMilitaryAI()->GetNumberCivsAtWarWith(false) <= 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_WAR_MAJOR");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasCityConnection() && !IsRouteToCapitalConnected())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_CITY_CONNECTION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasTradeConnection() && !HasTradeRouteFromAnyCity() && !HasTradeRouteToAnyCity())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_TRADE_CONNECTION");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isNearMountain() && GetNearbyMountains() <= 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_MOUNTAIN");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isNearNaturalWonder() && !HasAnyNaturalWonder())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NATURAL_WONDER");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->getMaximumPopulation() != 0 && getPopulation() > pkEventInfo->getMaximumPopulation())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_OVER_MAX_POPULATION");
		localizedDurationText <<pkEventInfo->getMaximumPopulation();
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasPantheon() && GetCityReligions()->GetReligiousMajority() != RELIGION_PANTHEON)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_ANY_PANTHEON");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isUnhappy() && !kPlayer.IsEmpireUnhappy())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_UNHAPPY");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isSuperUnhappy() && !kPlayer.IsEmpireSuperUnhappy())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_SUPER_UNHAPPY");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasMetAnotherCiv())
	{
		if(GET_TEAM(getTeam()).getHasMetCivCount(true) <= 0)
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_NEED_MET_OTHER_CIV");
			DisabledTT += localizedDurationText.toUTF8();
		}
	}

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isMaster() && GET_TEAM(getTeam()).GetNumVassals() <= 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_VASSAL");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(MOD_DIPLOMACY_CIV4_FEATURES && pkEventInfo->isVassal() && !GET_TEAM(getTeam()).IsVassalOfSomeone())
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BE_VASSAL");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->hasNearbyFeature() != -1)
	{
		FeatureTypes eFeature = (FeatureTypes)pkEventInfo->hasNearbyFeature();
		if(eFeature != NO_FEATURE)
		{
			if(!IsHasFeatureLocal(eFeature))
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_FEATURE_NEARBY");
				localizedDurationText << GC.getFeatureInfo((FeatureTypes)pkEventInfo->hasNearbyFeature())->GetDescription();
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
	}
	if(pkEventInfo->hasNearbyTerrain() != -1)
	{
		TerrainTypes eTerrain = (TerrainTypes)pkEventInfo->hasNearbyTerrain();
		if(eTerrain != NO_TERRAIN)
		{
			if(!HasTerrain(eTerrain))
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_TERRAIN_NEARBY");
				localizedDurationText << GC.getTerrainInfo((TerrainTypes)pkEventInfo->hasNearbyTerrain())->GetDescription();
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
	}

	if(pkEventInfo->isRequiresWarMinor())
	{
		bool bHas = false;
		for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
		{
			PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;

			// Is this a player we have relations with?
			if(GET_PLAYER(eLoopPlayer).isBarbarian())
			{
				continue;
			}
			if(!GET_PLAYER(eLoopPlayer).isMinorCiv())
			{
				continue;
			}
			if(eLoopPlayer != getOwner())
			{
				if(GET_TEAM(getTeam()).isAtWar(GET_PLAYER(eLoopPlayer).getTeam()))
				{
					bHas = true;
					break;
				}
			}
		}
		if(!bHas)
		{
			localizedDurationText = Localization::Lookup("TXT_KEY_NEED_WAR_MINOR");
			DisabledTT += localizedDurationText.toUTF8();
		}
	}

	if(pkEventInfo->getBuildingRequired() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingRequired();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) <= 0)
			{
				CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());

				if (pCivilizationInfo != NULL)
				{
					BuildingTypes eBuildingType = (BuildingTypes)pCivilizationInfo->getCivilizationBuildings((BuildingClassTypes)pkEventInfo->getBuildingRequired());
					if (eBuildingType != NO_BUILDING)
					{
						localizedDurationText = Localization::Lookup("TXT_KEY_NEED_BUILDING_CLASS_LOCAL");
						localizedDurationText << GC.getBuildingInfo(eBuildingType)->GetDescription();
						DisabledTT += localizedDurationText.toUTF8();
					}
				}
			}
		}
	}
	if(pkEventInfo->getBuildingLimiter() != -1)
	{
		BuildingClassTypes eBuilding = (BuildingClassTypes)pkEventInfo->getBuildingLimiter();
		if(eBuilding != NO_BUILDINGCLASS)
		{
			if(GetCityBuildings()->GetNumBuildingClass(eBuilding) > 0)
			{
				CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());

				if (pCivilizationInfo != NULL)
				{
					BuildingTypes eBuildingType = (BuildingTypes)pCivilizationInfo->getCivilizationBuildings((BuildingClassTypes)pkEventInfo->getBuildingLimiter());
					if (eBuildingType != NO_BUILDING)
					{
						localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NO_BUILDING_CLASS_LOCAL");
						localizedDurationText << GC.getBuildingInfo(eBuildingType)->GetDescription();
						DisabledTT += localizedDurationText.toUTF8();
					}
				}
			}
		}
	}
	if(pkEventInfo->getRequiredImprovement() != -1)
	{
		ImprovementTypes eImprovement = (ImprovementTypes)pkEventInfo->getRequiredImprovement();
		if(eImprovement != NO_IMPROVEMENT)
		{
			if(!HasImprovement(eImprovement))
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_IMPROVEMENT_LOCAL");
				localizedDurationText << GC.getImprovementInfo((ImprovementTypes)pkEventInfo->getRequiredImprovement())->GetDescription();
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
	}
	if(pkEventInfo->getLocalResourceRequired() != -1)
	{
		ResourceTypes eResource = (ResourceTypes)pkEventInfo->getLocalResourceRequired();
		if(eResource != NO_IMPROVEMENT)
		{
			if(!HasResource(eResource))
			{
				localizedDurationText = Localization::Lookup("TXT_KEY_NEED_RESOURCE_LOCAL");
				localizedDurationText << GC.getResourceInfo((ResourceTypes)pkEventInfo->getLocalResourceRequired())->GetDescription();
				DisabledTT += localizedDurationText.toUTF8();
			}
		}
	}
	//Check our minimum yields - this looks at stored values, not yields per turn.
	bool bHas = true;
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes)iI;
		if(eYield == NO_YIELD)
			continue;
							
		int iNeededYield = pkEventInfo->getYieldMinimum(eYield);
		if(pkEventInfo->getPreCheckEventYield(eYield) != 0)
		{
			if(iNeededYield < pkEventInfo->getPreCheckEventYield(eYield))
			{
				iNeededYield = pkEventInfo->getPreCheckEventYield(eYield);
			}
		}
		iNeededYield *= GC.getGame().getGameSpeedInfo().getTrainPercent();
		iNeededYield /= 100;
		if(pkEventInfo->IsEraScaling())
		{
			int iEra = kPlayer.GetCurrentEra();
			if(iEra <= 0)
			{
				iEra = 1;
			}
			iNeededYield *= iEra;
		}
		if(iNeededYield != 0)
		{
			if(eYield == YIELD_FOOD)
			{
				if(iNeededYield > getFood())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_PRODUCTION)
			{
				if(iNeededYield > getProduction())
				{
					bHas = false;
					break;
				}
			}
			if(eYield == YIELD_GOLD)
			{
				if(iNeededYield > kPlayer.GetTreasury()->GetGold())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_SCIENCE)
			{
				TechTypes eCurrentTech = kPlayer.GetPlayerTechs()->GetCurrentResearch();
				int iTech = 0;
				if(eCurrentTech != NO_TECH)
				{
					iTech = kPlayer.GetPlayerTechs()->GetResearchProgress(eCurrentTech);
				}
				if(iNeededYield > iTech)
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_FAITH)
			{
				if(iNeededYield > kPlayer.GetFaith())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_GOLDEN_AGE_POINTS)
			{
				if(iNeededYield > kPlayer.GetGoldenAgeProgressMeter())
				{
					bHas = false;
					break;
				}
			}
			else if(eYield == YIELD_CULTURE)
			{
				if(iNeededYield > kPlayer.getJONSCulture())
				{
					bHas = false;
					break;
				}
			}
		}
	}
	if(!bHas)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_YIELDS");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isInDebt() && kPlayer.GetTreasury()->GetGold() > 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_DEBT");
		DisabledTT += localizedDurationText.toUTF8();
	}

	if(pkEventInfo->isLosingMoney() && kPlayer.GetTreasury()->CalculateBaseNetGold() > 0)
	{
		localizedDurationText = Localization::Lookup("TXT_KEY_NEED_NEGATIVE_GPT");
		DisabledTT += localizedDurationText.toUTF8();
	}

	return DisabledTT.c_str();

}
void CvCity::DoEventChoice(CityEventChoiceTypes eEventChoice, CityEventTypes eCityEvent)
{
	if(eEventChoice != NO_EVENT_CHOICE)
	{
		CvModEventCityChoiceInfo* pkEventChoiceInfo = GC.getCityEventChoiceInfo(eEventChoice);
		if(pkEventChoiceInfo != NULL)
		{		
			//Set oneshot stuff so this event can't fire ever again.
			if(pkEventChoiceInfo->isOneShot())
			{
				SetEventChoiceFired(eEventChoice, true);
			}
			//Set false so we know we've completed the city event.
			//Loop through all city events and set any related to this to false, just to be sure.
			if(eCityEvent == NO_EVENT_CITY)
			{
				for(int iLoop = 0; iLoop < GC.getNumCityEventInfos(); iLoop++)
				{
					CityEventTypes eEvent = (CityEventTypes)iLoop;
					if(eEvent != NO_EVENT_CITY)
					{
						if(pkEventChoiceInfo->isParentEvent(eEvent))
						{
							SetEventActive(eEvent, false);
						}
					}
				}
			}
			else
			{
				SetEventActive(eCityEvent, false);
			}

			//Lua Hook
			GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityEventChoiceActivated, getOwner(), GetID(), eEventChoice);

			if(GC.getLogging())
			{
				CvString playerName;
				FILogFile* pLog;
				CvString strBaseString;
				CvString strOutBuf;
				CvString strFileName = "EventCityLogging.csv";
				playerName = getName();
				pLog = LOGFILEMGR.GetLog(strFileName, FILogFile::kDontTimeStamp);
				strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
				strBaseString += playerName + ", ";
				strOutBuf.Format("Event choice chosen by City: %s", pkEventChoiceInfo->GetDescription());
				strBaseString += strOutBuf;
				pLog->Msg(strBaseString);
			}
			//Set the cooldown for the event choice.
			if(pkEventChoiceInfo->getEventDuration() > 0)
			{
				//Gamespeed.
				int iEventDuration = pkEventChoiceInfo->getEventDuration();
				iEventDuration *= GC.getGame().getGameSpeedInfo().getTrainPercent();
				iEventDuration /= 100;
				ChangeEventChoiceDuration(eEventChoice, max(1, iEventDuration));
			}
			//Do the cost first, as that goes through whether or not the event succeeds!
			for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				YieldTypes eYield = (YieldTypes)iI;
				if(eYield == NO_YIELD)
					continue;
							
				int iPassYield = pkEventChoiceInfo->getPreCheckEventYield(eYield);
				iPassYield *= -1;
				if(iPassYield != 0)
				{
					GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_INSTANT, false, NO_GREATPERSON, NO_BUILDING, iPassYield, pkEventChoiceInfo->IsEraScaling(), NO_PLAYER, NULL, true, this, false, true, true, eYield);
				}
			}
			//Let's see if it even happens.
			if(pkEventChoiceInfo->getEventChance() > 0)
			{
				int iRandom = GC.getGame().getJonRandNum(100, "Random Event Chance");
				int iLimit = pkEventChoiceInfo->getEventChance();
				if(iRandom < iLimit)
				{
					//Notify if it did not work.
					CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
					if(pNotifications && GC.getGame().getActivePlayer() == getOwner())
					{
						for(int iLoop = 0; iLoop < GC.getNumCityEventInfos(); iLoop++)
						{
							CityEventTypes eEvent = (CityEventTypes)iLoop;
							if(eEvent != NO_EVENT_CITY)
							{
								if(pkEventChoiceInfo->isParentEvent(eEvent))
								{
									CvModCityEventInfo* pkEventInfo = GC.getCityEventInfo(eEvent);
									if(pkEventInfo != NULL)
									{
										Localization::String strMessage;
										Localization::String strSummary;
										strMessage = Localization::Lookup("TXT_KEY_NOTIFICATION_EVENT_FAILED_CITY");
										strMessage << pkEventChoiceInfo->GetDescription();
										strMessage << GetScaledHelpText(eEventChoice, false);
										strMessage << pkEventInfo->GetDescription();
										strMessage << getNameKey();
										strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_EVENT_FAILED_CITY_T");
										strSummary << pkEventInfo->GetDescription();
										strSummary << getNameKey();

										pNotifications->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), GetID(), getOwner());
										break;
									}
								}
							}
						}
					}
					return;
				}
			}
			//Set the event choice active if it succeeded, so we know to cancel it later.
			SetEventChoiceActive(eEventChoice, true);

			//Now do the bonuses.
			if(pkEventChoiceInfo->getEventPromotion() != -1)
			{
				PromotionTypes ePromotion = (PromotionTypes)pkEventChoiceInfo->getEventPromotion();
				if(ePromotion != -1)
				{
					changeFreePromotionCount(ePromotion, 1);
				}
			}
			for(int iI = 0; iI < GC.getNumResourceInfos(); iI++)
			{
				ResourceTypes eResource = (ResourceTypes)iI;
				if(eResource != NO_RESOURCE)
				{
					int iBonus = pkEventChoiceInfo->getEventResourceChange(eResource);
					if(iBonus != 0)
					{
						GET_PLAYER(getOwner()).changeNumResourceTotal(eResource, iBonus);
					}
				}
			}
			if(pkEventChoiceInfo->getEventBuilding() != -1)
			{
				BuildingClassTypes eBuildingClass = (BuildingClassTypes)pkEventChoiceInfo->getEventBuilding();
				if(eBuildingClass != NO_BUILDINGCLASS)
				{
					const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
	
					if (pkBuildingClassInfo)
					{
						CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());
		
						if (pCivilizationInfo != NULL)
						{
							BuildingTypes eBuildingType = (BuildingTypes) pCivilizationInfo->getCivilizationBuildings(eBuildingClass);
							if(eBuildingType != NO_BUILDING)
							{
								GetCityBuildings()->SetNumRealBuilding(eBuildingType, 1);
							}
						}
					}
				}
			}
			if(pkEventChoiceInfo->getEventBuildingDestruction() != -1)
			{
				BuildingClassTypes eBuildingClass = (BuildingClassTypes)pkEventChoiceInfo->getEventBuildingDestruction();
				if(eBuildingClass != NO_BUILDINGCLASS)
				{
					const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
	
					if (pkBuildingClassInfo)
					{
						CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());
		
						if (pCivilizationInfo != NULL)
						{
							BuildingTypes eBuildingType = (BuildingTypes) pCivilizationInfo->getCivilizationBuildings(eBuildingClass);
							if(eBuildingType != NO_BUILDING)
							{
								GetCityBuildings()->SetNumRealBuilding(eBuildingType, 0, true);
							}
						}
					}
				}
			}

			for(int iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
			{
				BuildingClassTypes eBuildingClass = (BuildingClassTypes)iI;
				if(eBuildingClass == NO_BUILDINGCLASS || GetCityBuildings()->GetNumBuildingClass(eBuildingClass) <= 0)
					continue;

				const CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
	
				if (!pkBuildingClassInfo)
				{
					continue;
				}

				int iChance = pkEventChoiceInfo->getBuildingDestructionChance(iI);
				if(pkEventChoiceInfo->getCityWideDestructionChance() > iChance && pkEventChoiceInfo->getCityWideDestructionChance() > 0)
				{
					if(pkBuildingClassInfo->getMaxGlobalInstances() != -1)
					{
						continue;
					}
					else if(pkBuildingClassInfo->getMaxPlayerInstances() != -1)
					{
						continue;
					}
					iChance = pkEventChoiceInfo->getCityWideDestructionChance();
				}
				if(iChance <= 0)
				{
					continue;
				}
				else
				{
					CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());
		
					if (pCivilizationInfo != NULL)
					{
						BuildingTypes eBuildingType = (BuildingTypes) pCivilizationInfo->getCivilizationBuildings(eBuildingClass);
						if(eBuildingType != NO_BUILDING)
						{
							if(GetCityBuildings()->GetNumFreeBuilding(eBuildingType) > 0)
								continue;

							int iRandom = GC.getGame().getJonRandNum(100, "Random Event Chance");
							if(iRandom < iChance)
							{					
								GetCityBuildings()->SetNumRealBuilding(eBuildingType, 0, true);
								if (getOwner() == GC.getGame().getActivePlayer())
								{
									CvString strBuffer = GetLocalizedText("TXT_KEY_MISC_BUILDING_DESTROYED_EVENT", GC.getBuildingInfo(eBuildingType)->GetTextKey(), getNameKey());
									GC.GetEngineUserInterface()->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), strBuffer);
								}
							}
						}
					}
				}
			}
			for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				YieldTypes eYield = (YieldTypes)iI;
				if(eYield == NO_YIELD)
					continue;

				if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
				{
					int iUnhappinessNeedMod = pkEventChoiceInfo->getCityUnhappinessNeedMod(eYield);
					if(iUnhappinessNeedMod != 0)
					{
						if(eYield == YIELD_GOLD)
						{
							ChangePovertyUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_CULTURE)
						{
							ChangeUnculturedUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_PRODUCTION)
						{
							ChangeDefenseUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_SCIENCE)
						{
							ChangeIlliteracyUnhappiness(iUnhappinessNeedMod);
						}
						else if(eYield == YIELD_FAITH)
						{
							ChangeMinorityUnhappiness(iUnhappinessNeedMod);
						}
					}
				}
				
				int iYieldChange = pkEventChoiceInfo->getCityYield(eYield);
				if(iYieldChange != 0)
				{
					if(pkEventChoiceInfo->IsEraScaling())
					{
						int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
						if(iEra <= 0)
						{
							iEra = 1;
						}
						iYieldChange *= iEra;
					}
					ChangeEventCityYield(eYield, iYieldChange);
				}
				// Building modifiers
				for(int iJ = 0; iJ < GC.getNumBuildingClassInfos(); iJ++)
				{
					BuildingClassTypes eBuildingClass = (BuildingClassTypes) iJ;

					CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
					if(!pkBuildingClassInfo)
					{
						continue;
					}
					if(pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield) != 0)
					{
						BuildingTypes eBuilding = (BuildingTypes) getCivilizationInfo().getCivilizationBuildings(eBuildingClass);

						if(eBuilding != NO_BUILDING)
						{
							CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
							if(pkBuilding)
							{
								int iBuildingCount = GetCityBuildings()->GetNumBuilding(eBuilding);

								ChangeEventBuildingClassYield(eBuildingClass, eYield, pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield));
								if(iBuildingCount > 0)
								{
									ChangeBaseYieldRateFromBuildings(eYield, pkEventChoiceInfo->getBuildingClassYield(eBuildingClass, eYield));
								}
							}
						}
					}
					if(pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield) != 0)
					{
						BuildingTypes eBuilding = (BuildingTypes) getCivilizationInfo().getCivilizationBuildings(eBuildingClass);

						if(eBuilding != NO_BUILDING)
						{
							CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
							if(pkBuilding)
							{
								int iBuildingCount = GetCityBuildings()->GetNumBuilding(eBuilding);
								ChangeEventBuildingClassYieldModifier(eBuildingClass, eYield, pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield));
								if(iBuildingCount > 0)
								{
									changeYieldRateModifier(eYield, pkEventChoiceInfo->getBuildingClassYieldModifier(eBuildingClass, eYield));
								}
							}
						}
					}
				}

				int iPassYield = pkEventChoiceInfo->getEventYield(eYield);
				if(iPassYield != 0)
				{
					GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_INSTANT, false, NO_GREATPERSON, NO_BUILDING, iPassYield, pkEventChoiceInfo->IsEraScaling(), NO_PLAYER, NULL, true, this, false, true, true, eYield);
				}
				for(int iJ = 0; iJ < GC.getNumImprovementInfos(); iJ++)
				{
					ImprovementTypes eImprovement = (ImprovementTypes)iJ;
					if(eImprovement != NO_IMPROVEMENT && pkEventChoiceInfo->getImprovementYield(eImprovement, eYield) != 0)
					{
						ChangeEventImprovementYield(eImprovement, eYield, pkEventChoiceInfo->getImprovementYield(eImprovement, eYield));
					}
				}
				for(int iJ = 0; iJ < GC.getNumFeatureInfos(); iJ++)
				{
					FeatureTypes eFeature = (FeatureTypes)iJ;
					if(eFeature != NO_FEATURE && pkEventChoiceInfo->getFeatureYield(eFeature, eYield) != 0)
					{
						ChangeEventFeatureYield(eFeature, eYield, pkEventChoiceInfo->getFeatureYield(eFeature, eYield));
					}
				}
				for(int iJ = 0; iJ < GC.getNumTerrainInfos(); iJ++)
				{
					TerrainTypes eTerrain = (TerrainTypes)iJ;
					if(eTerrain != NO_TERRAIN && pkEventChoiceInfo->getTerrainYield(eTerrain, eYield) != 0)
					{
						ChangeEventTerrainYield(eTerrain, eYield, pkEventChoiceInfo->getTerrainYield(eTerrain, eYield));
					}
				}
				for(int iJ = 0; iJ < GC.getNumResourceInfos(); iJ++)
				{
					ResourceTypes eResource = (ResourceTypes)iJ;
					if(eResource != NO_RESOURCE && pkEventChoiceInfo->getResourceYield(eResource, eYield) != 0)
					{
						ChangeEventResourceYield(eResource, eYield, pkEventChoiceInfo->getResourceYield(eResource, eYield));
					}
				}
				for(int iJ = 0; iJ < GC.getNumSpecialistInfos(); iJ++)
				{
					const SpecialistTypes eSpecialist = static_cast<SpecialistTypes>(iJ);
					CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo(eSpecialist);
					if(pkSpecialistInfo)
					{
						ChangeEventSpecialistYield(eSpecialist, eYield, pkEventChoiceInfo->getCitySpecialistYieldChange(eSpecialist, eYield));
					}
				}
			}
			for(int iSpecialistLoop = 0; iSpecialistLoop < GC.getNumSpecialistInfos(); iSpecialistLoop++)
			{
				const SpecialistTypes eSpecialist = static_cast<SpecialistTypes>(iSpecialistLoop);
				CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo(eSpecialist);
				if(pkSpecialistInfo)
				{
					int iBonus = pkEventChoiceInfo->getEventGPChange(eSpecialist);
					if(pkEventChoiceInfo->IsEraScaling())
					{
						int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
						if(iEra <= 0)
						{
							iEra = 1;
						}
						iBonus *= iEra;
						iBonus *= GC.getGame().getGameSpeedInfo().getGreatPeoplePercent();
						iBonus /= 100;
					}
					if(iBonus != 0)
					{
						GetCityCitizens()->ChangeSpecialistGreatPersonProgressTimes100(eSpecialist, iBonus * 100);
					}
				}
			}
			typedef CvWeightedVector<CvPlot*, SAFE_ESTIMATE_NUM_BUILDINGS, true> WeightedPlotVector;
			WeightedPlotVector aBestPlots;
			for(int iI = 0; iI < GC.getNumImprovementInfos(); iI++)
			{
				ImprovementTypes eImprovement = (ImprovementTypes)iI;
				if(eImprovement != NO_IMPROVEMENT && pkEventChoiceInfo->getImprovementDestruction(eImprovement) > 0)
				{
					aBestPlots.clear();
					int iX = getX(); int iY = getY(); int iOwner = getOwner();

					for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
					{
						CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

						// Invalid plot or not owned by this player
						if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
						{
							continue;
						}

						if (pLoopPlot->HasImprovement(eImprovement)) 
						{
							int iValue = GC.getGame().getJonRandNum(100, "Improvement Destruction");
							if(pkEventChoiceInfo->isCoastal() && pLoopPlot->isCoastalLand())
							{
								iValue += 100;
							}
							if(pkEventChoiceInfo->isRiver()  && pLoopPlot->isRiver())
							{
								iValue += 100;
							}
							if(pkEventChoiceInfo->isNearMountain()  && pLoopPlot->GetNumAdjacentMountains() > 0)
							{
								iValue += 100;
							}
							aBestPlots.push_back(pLoopPlot, iValue);
						}
					}
					if(aBestPlots.size() > 0)
					{
						aBestPlots.SortItems();
						//Delete improvents up to the total on the event.
						int iNumber = 0;
						int iRuns = pkEventChoiceInfo->getImprovementDestruction(eImprovement);
						if(aBestPlots.size() < iRuns)
						{
							iRuns = aBestPlots.size();
						}
						for(int iI = 0; iI < iRuns ; iI++)
						{
							CvPlot* pPlot = aBestPlots.GetElement(iI);
							if(pPlot != NULL && pPlot->getOwner() == getOwner() && pPlot->getImprovementType() == eImprovement)
							{
								pPlot->SetImprovementPillaged(true);
								iNumber++; 
							}
						}
						if (getOwner() == GC.getGame().getActivePlayer())
						{
							CvString strBuffer = GetLocalizedText("TXT_KEY_MISC_IMPROVEMENT_DESTROYED_EVENT", GC.getImprovementInfo(eImprovement)->GetTextKey(), iNumber, getNameKey());
							GC.GetEngineUserInterface()->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), strBuffer);
						}
					}
				}
			}
			for(int iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
			{
				const UnitClassTypes eUnitClass = static_cast<UnitClassTypes>(iI);
				CvUnitClassInfo* pkUnitClassInfo = GC.getUnitClassInfo(eUnitClass);
				if(pkUnitClassInfo)
				{
					if(pkEventChoiceInfo->getNumFreeUnits((UnitClassTypes)iI) <= 0)
						continue;

					CvCivilizationInfo* pCivilizationInfo = GC.getCivilizationInfo(getCivilizationType());
		
					if (pCivilizationInfo != NULL)
					{
						const UnitTypes eLoopUnit = (UnitTypes)pCivilizationInfo->getCivilizationUnits(iI);
						if(eLoopUnit != NO_UNIT)
						{
							CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eLoopUnit);
							if(pkUnitEntry)
							{
								for(int iJ = 0; iJ < pkEventChoiceInfo->getNumFreeUnits((UnitClassTypes)iI); iJ++)
								{
									UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
									int iResult = CreateUnit(eLoopUnit, eUnitAI);

									CvAssertMsg(iResult != -1, "Unable to create unit");

									if (iResult != -1)
									{
										CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
										if (!pUnit->jumpToNearestValidPlot())
										{
											pUnit->kill(false);	// Could not find a valid spot!
										}
										else
										{
											pUnit->setMoves(0);
											//Lua Hook
											GAMEEVENTINVOKE_HOOK(GAMEEVENT_EventUnitCreated, getOwner(), eEventChoice, pUnit);
										}
									}
								}
							}
						}
					}
				}
			}
			for(int iI = 0; iI < GC.getNumUnitInfos(); iI++)
			{
				const UnitTypes eUnit = static_cast<UnitTypes>(iI);
				CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eUnit);
				if(pkUnitEntry)
				{
					if(pkEventChoiceInfo->getNumFreeSpecificUnits((UnitTypes)iI) <= 0)
						continue;
		
					for(int iJ = 0; iJ < pkEventChoiceInfo->getNumFreeSpecificUnits((UnitTypes)iI); iJ++)
					{
						UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
						int iResult = CreateUnit(eUnit, eUnitAI);

						CvAssertMsg(iResult != -1, "Unable to create unit");

						if (iResult != -1)
						{
							CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
							if (!pUnit->jumpToNearestValidPlot())
							{
								pUnit->kill(false);	// Could not find a valid spot!
							}
							else
							{
								pUnit->setMoves(0);
								//Lua Hook
								GAMEEVENTINVOKE_HOOK(GAMEEVENT_EventUnitCreated, getOwner(), eEventChoice, pUnit);
							}
						}
					}
				}
			}
			if(pkEventChoiceInfo->ConvertsCityToPlayerReligion() != 0)
			{
				ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer();
				if(eReligion != NO_RELIGION)
				{
					GetCityReligions()->ConvertPercentForcedFollowers(eReligion, pkEventChoiceInfo->ConvertsCityToPlayerReligion());
				}
			}
			if(pkEventChoiceInfo->ConvertsCityToPlayerMajorityReligion() != 0)
			{
				ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities();
				if(eReligion != NO_RELIGION && eReligion > RELIGION_PANTHEON)
				{
					GetCityReligions()->ConvertPercentForcedFollowers(eReligion, pkEventChoiceInfo->ConvertsCityToPlayerReligion());
				}
			}
			if(pkEventChoiceInfo->getResistanceTurns() != 0)
			{
				int iTurns = pkEventChoiceInfo->getResistanceTurns();
				iTurns *= GC.getGame().getGameSpeedInfo().getTrainPercent();
				iTurns /= 100;
				ChangeResistanceTurns(max(1, iTurns));
			}
			if(pkEventChoiceInfo->getWLTKD() != 0)
			{
				int iTurns = pkEventChoiceInfo->getWLTKD();
				iTurns *= GC.getGame().getGameSpeedInfo().getTrainPercent();
				iTurns /= 100;
				ChangeWeLoveTheKingDayCounter(max(1, iTurns));
			}
			if(pkEventChoiceInfo->getCityHappiness() != 0)
			{
				ChangeEventHappiness(pkEventChoiceInfo->getCityHappiness());
			}
			for(int iI = 0; iI < GC.getNumReligionInfos(); iI++)
			{
				ReligionTypes eReligion = (ReligionTypes)iI;
				if(eReligion == NO_RELIGION)
					continue;

				int iPercent = (ReligionTypes)pkEventChoiceInfo->getEventConvertReligionPercent(iI);
				if(iPercent > 0)
				{
					GetCityReligions()->ConvertPercentForcedFollowers(eReligion, iPercent);
				}
				int iPop = (ReligionTypes)pkEventChoiceInfo->getEventConvertReligion(iI);
				if(iPop > 0)
				{
					GetCityReligions()->ConvertNumberFollowers(eReligion, iPop);
				}
			}
			if(pkEventChoiceInfo->getRandomBarbs() > 0)
			{
				// In hundreds
				int iNumRebels = pkEventChoiceInfo->getRandomBarbs();
				GC.getGame().DoSpawnUnitsAroundTargetCity(BARBARIAN_PLAYER, this, iNumRebels, false, false, false, false);
			}
			if(pkEventChoiceInfo->getFreeScaledUnits() > 0)
			{
				// In hundreds
				int iNumRecruits = pkEventChoiceInfo->getFreeScaledUnits();

				GC.getGame().DoSpawnUnitsAroundTargetCity(getOwner(), this, iNumRecruits, false, false, false, false);
			}
			//Let's do our notification stuff here.
			for(int iI = 0; iI < pkEventChoiceInfo->GetNumNotifications(); iI++)
			{
				CvString strNotificationString = pkEventChoiceInfo->GetNotificationInfo(iI)->GetNotificationString();		
				if(strNotificationString != NULL && strNotificationString != "")
				{
					NotificationTypes eNotificationType = (NotificationTypes)FString::Hash(strNotificationString);
					
					if(eNotificationType != NO_NOTIFICATION_TYPE)
					{
						Localization::String strMessage;
						Localization::String strSummary;
						strSummary = Localization::Lookup(pkEventChoiceInfo->GetNotificationInfo(iI)->GetShortDescription());
						strSummary << GET_PLAYER(getOwner()).getCivilizationShortDescriptionKey();
						strSummary << getNameKey();
						strSummary << GET_PLAYER(getOwner()).getCivilizationDescription();
						strSummary << GET_PLAYER(getOwner()).getName();
						strMessage = Localization::Lookup(pkEventChoiceInfo->GetNotificationInfo(iI)->GetDescription());
						strMessage <<  GET_PLAYER(getOwner()).getCivilizationShortDescriptionKey();
						strMessage << getNameKey();
						strMessage << GET_PLAYER(getOwner()).getCivilizationDescription();
						strMessage << GET_PLAYER(getOwner()).getName();
						strMessage << GetScaledHelpText(eEventChoice, true);
						bool bGlobal = pkEventChoiceInfo->GetNotificationInfo(iI)->IsWorldEvent();
						int iX = -1;
						int iY = -1;
						int iVariable = pkEventChoiceInfo->GetNotificationInfo(iI)->GetVariable();
						if(pkEventChoiceInfo->GetNotificationInfo(iI)->IsNeedCityCoordinates())
						{
							iX = getX();
							iY = getY();
						}
						for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
						{
							PlayerTypes ePlayer = (PlayerTypes) iPlayerLoop;
							if(ePlayer != NO_PLAYER && GET_PLAYER(ePlayer).isMajorCiv())
							{
								//Not global? Skip all but me.
								if(!bGlobal && ePlayer != getOwner())
									continue;

								//Global? Seed only to known players.
								if(bGlobal && ePlayer != getOwner() && !GET_TEAM(GET_PLAYER(ePlayer).getTeam()).isHasMet(getTeam()))
									continue;

								//Send out notifications!
								CvNotifications* pNotifications = GET_PLAYER(ePlayer).GetNotifications();
								if(pNotifications && GC.getGame().getActivePlayer() == ePlayer)
								{
									if(pkEventChoiceInfo->GetNotificationInfo(iI)->IsNeedPlayerID())
									{
										pNotifications->Add(eNotificationType, strMessage.toUTF8(), strSummary.toUTF8(), iX, iY, getOwner(), GetID());
									}
									else
									{
										pNotifications->Add(eNotificationType, strMessage.toUTF8(), strSummary.toUTF8(), iX, iY, iVariable, GetID());
									}
								}
							}
						}
					}
				}
			}
			//Some yield cleanup and refresh here - note that not all of this has to do with religion, however any time religion is updated, that's a good time to update the city's yields.
			for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				YieldTypes eYield = (YieldTypes) iI;
				if(eYield == NO_YIELD)
					continue;

				UpdateSpecialReligionYields(eYield);
				UpdateCityYields(eYield);
			}
			UpdateReligion(GetCityReligions()->GetReligiousMajority());
			GET_PLAYER(getOwner()).CalculateNetHappiness();
			GetCityCulture()->CalculateBaseTourismBeforeModifiers();
			GetCityCulture()->CalculateBaseTourism();
		}
	}
}
void CvCity::SetEventActive(CityEventTypes eEvent, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	m_abEventActive.setAt(eEvent, bValue);
}
bool CvCity::IsEventActive(CityEventTypes eEvent) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	return m_abEventActive[eEvent];
}
void CvCity::SetEventChoiceActive(CityEventChoiceTypes eEventChoice, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEventChoice >= 0, "eEventChoice is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEventChoice < GC.getNumCityEventChoiceInfos(), "eEventChoice is expected to be within maximum bounds (invalid Index)");

	m_abEventChoiceActive.setAt(eEventChoice, bValue);
}
bool CvCity::IsEventChoiceActive(CityEventChoiceTypes eEventChoice) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEventChoice >= 0, "eEventChoice is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEventChoice < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	return m_abEventChoiceActive[eEventChoice];
}

void CvCity::SetEventChoiceFired(CityEventChoiceTypes eEvent, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	m_abEventChoiceFired.setAt(eEvent, bValue);
}
bool CvCity::IsEventChoiceFired(CityEventChoiceTypes eEvent) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventChoiceInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	return m_abEventChoiceFired[eEvent];
}
void CvCity::SetEventFired(CityEventTypes eEvent, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	m_abEventFired.setAt(eEvent, bValue);
}
bool CvCity::IsEventFired(CityEventTypes eEvent) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");

	return m_abEventFired[eEvent];
}
int CvCity::GetEventCooldown(CityEventTypes eEvent) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	return m_aiEventCooldown[eEvent];
}
void CvCity::ChangeEventCooldown(CityEventTypes eEvent,int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	if(iValue != 0)
	{
		m_aiEventCooldown.setAt(eEvent, m_aiEventCooldown[eEvent] + iValue);
	}
}
void CvCity::SetEventCooldown(CityEventTypes eEvent,int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eEvent >= 0, "eEvent is expected to be non-negative (invalid Index)");
	CvAssertMsg(eEvent < GC.getNumCityEventInfos(), "eEvent is expected to be within maximum bounds (invalid Index)");
	m_aiEventCooldown.setAt(eEvent, iValue);
}
int CvCity::GetEventHappiness() const
{
	VALIDATE_OBJECT
	return m_iEventHappiness;
}
void CvCity::ChangeEventHappiness(int iValue)
{
	VALIDATE_OBJECT
	m_iEventHappiness += iValue;
}
#endif
//	--------------------------------------------------------------------------------
/// Connected to capital with industrial route? (Railroads)
bool CvCity::IsIndustrialRouteToCapitalConnected() const
{
	return m_bIndustrialRouteToCapital;
}

//	--------------------------------------------------------------------------------
/// Connected to capital with industrial route? (Railroads)
void CvCity::SetIndustrialRouteToCapitalConnected(bool bValue)
{
	m_bIndustrialRouteToCapital = bValue;
}

//	--------------------------------------------------------------------------------
void CvCity::SetRouteToCapitalConnected(bool bValue)
{
	bool bUpdateReligion = (bValue != m_bRouteToCapitalConnectedThisTurn);

	//do this before the religion recalculation ...
	m_bRouteToCapitalConnectedThisTurn = bValue;

	if(bUpdateReligion)
	{
#if defined(MOD_BALANCE_CORE)
		UpdateReligion(GetCityReligions()->GetReligiousMajority(), false);
#else
		UpdateReligion(GetCityReligions()->GetReligiousMajority());
#endif
	}
}

//	--------------------------------------------------------------------------------
bool CvCity::IsRouteToCapitalConnected(void) const
{
	return m_bRouteToCapitalConnectedThisTurn;
}


//	--------------------------------------------------------------------------------
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
void CvCity::createGreatGeneral(UnitTypes eGreatPersonUnit, bool bIsFree)
#else
void CvCity::createGreatGeneral(UnitTypes eGreatPersonUnit)
#endif
{
	VALIDATE_OBJECT
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
	GET_PLAYER(getOwner()).createGreatGeneral(eGreatPersonUnit, getX(), getY(), bIsFree);
#else
	GET_PLAYER(getOwner()).createGreatGeneral(eGreatPersonUnit, getX(), getY());
#endif
}

//	--------------------------------------------------------------------------------
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
void CvCity::createGreatAdmiral(UnitTypes eGreatPersonUnit, bool bIsFree)
#else
void CvCity::createGreatAdmiral(UnitTypes eGreatPersonUnit)
#endif
{
	VALIDATE_OBJECT
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
	GET_PLAYER(getOwner()).createGreatAdmiral(eGreatPersonUnit, getX(), getY(), bIsFree);
#else
	GET_PLAYER(getOwner()).createGreatAdmiral(eGreatPersonUnit, getX(), getY());
#endif
}

//	--------------------------------------------------------------------------------
CityTaskResult CvCity::doTask(TaskTypes eTask, int iData1, int iData2, bool bOption, bool bAlt, bool bShift, bool bCtrl)
{
	VALIDATE_OBJECT
	CityTaskResult eResult = TASK_COMPLETED;
	switch(eTask)
	{
	case TASK_RAZE:
		GET_PLAYER(getOwner()).raze(this);
		break;

	case TASK_UNRAZE:
		GET_PLAYER(getOwner()).unraze(this);
		break;

	case TASK_DISBAND:
		GET_PLAYER(getOwner()).disband(this);
		break;

	case TASK_GIFT:
		GET_PLAYER((PlayerTypes)iData1).acquireCity(this, false, true);
		break;

	case TASK_SET_AUTOMATED_CITIZENS:
		break;

	case TASK_SET_AUTOMATED_PRODUCTION:
		setProductionAutomated(bOption, bAlt && bShift && bCtrl);
		break;

	case TASK_SET_EMPHASIZE:
		m_pEmphases->SetEmphasize(((EmphasizeTypes)iData1), bOption);
		break;

	case TASK_NO_AUTO_ASSIGN_SPECIALISTS:
		GetCityCitizens()->SetNoAutoAssignSpecialists(bOption);
		break;

	case TASK_ADD_SPECIALIST:
		GetCityCitizens()->DoAddSpecialistToBuilding(/*eBuilding*/ (BuildingTypes) iData2, true);
		break;

	case TASK_REMOVE_SPECIALIST:
	{
		GetCityCitizens()->DoRemoveSpecialistFromBuilding(/*eBuilding*/ (BuildingTypes)iData2, true);
		std::map<SpecialistTypes, int> specialistValueCache;
		GetCityCitizens()->DoAddBestCitizenFromUnassigned(specialistValueCache);
		break;
	}
	case TASK_CHANGE_WORKING_PLOT:
		GetCityCitizens()->DoAlterWorkingPlot(/*CityPlotIndex*/ iData1);
		break;

	case TASK_REMOVE_SLACKER:
		if (GetCityCitizens()->GetNumDefaultSpecialists() > 0)
		{
#if defined(MOD_BALANCE_CORE)
			GetCityCitizens()->ChangeNumDefaultSpecialists(-1, false);
			GetCityCitizens()->DoReallocateCitizens(true);
#else
			GetCityCitizens()->ChangeNumDefaultSpecialists(-1);
			GetCityCitizens()->DoReallocateCitizens();
#endif
		}
		break;

	case TASK_CLEAR_WORKING_OVERRIDE:
		clearWorkingOverride(iData1);
		break;

	case TASK_HURRY:
		hurry((HurryTypes)iData1);
		break;

	case TASK_CONSCRIPT:
		conscript();
		break;

	case TASK_CLEAR_ORDERS:
		clearOrderQueue();
		break;

	case TASK_RALLY_PLOT:
		setRallyPlot(GC.getMap().plot(iData1, iData2));
		break;

	case TASK_CLEAR_RALLY_PLOT:
		setRallyPlot(NULL);
		break;

	case TASK_RANGED_ATTACK:
		eResult = rangeStrike(iData1,iData2);
		break;

	case TASK_CREATE_PUPPET:
		DoCreatePuppet();
		break;

	case TASK_ANNEX_PUPPET:
		DoAnnex();
		break;

	default:
		CvAssertMsg(false, "eTask failed to match a valid option");
		break;
	}

	return eResult;
}


//	--------------------------------------------------------------------------------
void CvCity::chooseProduction(UnitTypes eTrainUnit, BuildingTypes eConstructBuilding, ProjectTypes eCreateProject, bool /*bFinish*/, bool /*bFront*/)
{
	VALIDATE_OBJECT
	CvString strTooltip = GetLocalizedText("TXT_KEY_NOTIFICATION_NEW_CONSTRUCTION", getNameKey());
#if defined(MOD_BALANCE_CORE)
	if(IsRazing())
	{
		//No popup if razing.
		return;
	}
#endif
	CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
	if(pNotifications)
	{
		// Figure out what we just finished so we can package it into something the lua will understand
		OrderTypes eOrder = NO_ORDER;
		int iItemID = -1;

		if(eTrainUnit != NO_UNIT)
		{
			eOrder = ORDER_TRAIN;
			iItemID = eTrainUnit;
		}
		else if(eConstructBuilding != NO_BUILDING)
		{
			eOrder = ORDER_CONSTRUCT;
			iItemID = eConstructBuilding;
		}
		else if(eCreateProject != NO_PROJECT)
		{
			eOrder = ORDER_CREATE;
			iItemID = eCreateProject;
		}

		pNotifications->Add(NOTIFICATION_PRODUCTION, strTooltip, strTooltip, getX(), getY(), eOrder, iItemID);
	}
}

#if defined(MOD_BALANCE_CORE)
void CvCity::updateEconomicValue()
{
	//todo: take into account player personality

	int iYieldValue = 0;

	CvWeightedVector<int, SAFE_ESTIMATE_NUM_BUILDINGS, true> validResources;

	//notes:
	//- economic value is in gold, so use a rough conversion factor for the others
	//- for food and gold only surplus is interesting, rest is converted to other yields already
	//- ignore trade, as the city might the change owner
	iYieldValue += (getYieldRateTimes100(YIELD_FOOD, true) - foodConsumption() * 100) * 3;
	iYieldValue += getYieldRateTimes100(YIELD_PRODUCTION, true) * 4;
	iYieldValue += getYieldRateTimes100(YIELD_SCIENCE, true) * 3;
	iYieldValue += (getYieldRateTimes100(YIELD_GOLD, true) - GetCityBuildings()->GetTotalBaseBuildingMaintenance() * 100) * 1;
	iYieldValue += getJONSCulturePerTurn() * 3;
	iYieldValue += GetFaithPerTurn() * 3;

#if defined(MOD_API_UNIFIED_YIELDS_TOURISM)
	iYieldValue += getYieldRateTimes100(YIELD_TOURISM, true) * 3;
#endif
#if defined(MOD_API_UNIFIED_YIELDS_GOLDEN_AGE)
	iYieldValue += getYieldRateTimes100(YIELD_GOLDEN_AGE_POINTS, true) * 3;
#endif

	//divide by avg conversion factor
	iYieldValue /= 3;

	//now check access to resources
	//todo: call CvDealAI::GetResourceValue() for each resource

	int iWonders = getNumWorldWonders() * 50;
	iYieldValue += iWonders;

	for (int iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		CvPlot* pLoopPlot = GetCityCitizens()->GetCityPlotFromIndex(iI);
		//for plots owned by this city
		if (NULL != pLoopPlot && GetID() == pLoopPlot->GetCityPurchaseID())
		{
			//todo: add something for currently unworked plots (future potential)
			ResourceTypes eResource = pLoopPlot->getResourceType(getTeam());
			if (eResource == NO_RESOURCE)
				continue;

			const CvResourceInfo* pkResourceInfo = GC.getResourceInfo(eResource);
			if (!pkResourceInfo)
				continue;

			if (GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned(getOwner(), eResource))
				continue;

			int iResourceQuantity = pLoopPlot->getNumResource();

			validResources.push_back(eResource, iResourceQuantity);
		} //owned plots
	} //all plots

	for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
	{
		PlayerTypes ePossibleOwner = (PlayerTypes)iPlayerLoop;
		m_aiEconomicValue.setAt(iPlayerLoop, 0); //everybody gets a new value

		if (ePossibleOwner != NO_PLAYER && GET_PLAYER(ePossibleOwner).isAlive())
		{
			int iResourceValue = 0;
			if (validResources.size() > 0)
			{
				for (int iResourceLoop = 0; iResourceLoop < validResources.size(); iResourceLoop++)
				{
					//todo: add something for currently unworked plots (future potential)
					ResourceTypes eResource = (ResourceTypes)validResources.GetElement(iResourceLoop);
					if (eResource == NO_RESOURCE)
						continue;

					if (GET_TEAM(GET_PLAYER(ePossibleOwner).getTeam()).IsResourceObsolete(eResource))
						continue;

					const CvResourceInfo* pkResourceInfo = GC.getResourceInfo(eResource);
					if (!pkResourceInfo)
						continue;
	
					int iResourceQuantity = validResources.GetWeight(iResourceLoop);
					if (iResourceQuantity > 0)
					{
						ResourceUsageTypes eUsage = pkResourceInfo->getResourceUsage();
						if (eUsage == RESOURCEUSAGE_LUXURY)
						{
							int iValue = 200;

							// If the new owner doesn't have it or the old owner would lose it completely, it's worth more
							if ((GET_PLAYER(ePossibleOwner).getNumResourceAvailable(eResource) == 0) || (GET_PLAYER(getOwner()).getNumResourceAvailable(eResource) == iResourceQuantity))
								iValue = 600;

							int iHappinessFromResource = pkResourceInfo->getHappiness();
							iResourceValue += iResourceQuantity * iHappinessFromResource * iValue;
						}
						else if (eUsage == RESOURCEUSAGE_STRATEGIC)
						{
							int iValue = 400;

							// If the new owner doesn't have it or the old owner would lose it completely, it's worth more
							if ((GET_PLAYER(ePossibleOwner).getNumResourceAvailable(eResource) == 0) || (GET_PLAYER(getOwner()).getNumResourceAvailable(eResource) == iResourceQuantity))
								iValue = 800;

							iResourceValue += iResourceQuantity * iValue;
						}
					}
				}
			}

			m_aiEconomicValue.setAt(ePossibleOwner, iYieldValue + iResourceValue);
		}
	}
}

int CvCity::getEconomicValue(PlayerTypes ePossibleOwner)
{
	if (ePossibleOwner==NO_PLAYER || ePossibleOwner>=MAX_MAJOR_CIVS)
		return 0;

	if ((int)m_aiEconomicValue.size() <= ePossibleOwner)
		updateEconomicValue();

	return m_aiEconomicValue[ePossibleOwner];
}

#endif

#if defined(MOD_BALANCE_CORE_SPIES)
void CvCity::SetRank(int iRank)
{
	VALIDATE_OBJECT
	m_iCityRank = iRank;
	CvAssert(GetRank() >= 0);
}
int CvCity::GetRank() const
{
	VALIDATE_OBJECT
	return m_iCityRank;
}
void CvCity::SetTurnsSinceLastRankMessage(int iTurns)
{
	VALIDATE_OBJECT
	m_iTurnsSinceRankAnnouncement = iTurns;
	CvAssert(GetTurnsSinceLastRankMessage() >= 0);
}
int CvCity::GetTurnsSinceLastRankMessage() const
{
	VALIDATE_OBJECT
	return m_iTurnsSinceRankAnnouncement;
}
void CvCity::ChangeTurnsSinceLastRankMessage(int iTurns)
{
	VALIDATE_OBJECT
	SetTurnsSinceLastRankMessage(GetTurnsSinceLastRankMessage() + iTurns);
}
void CvCity::SetEspionageRanking(int iPotential, bool bNotify)
{
	int iRank = 0;

	//Don't want to divide by zero!
	if(GC.getGame().GetLargestSpyPotential() > 0)
	{
		iRank = ((iPotential * 100) / GC.getGame().GetLargestSpyPotential());
		//Rank time - 10 is worst, 1 is best
		iRank /= 10;
		if (iRank <= 0)
		{
			iRank = 1;
		}
	}
	//Seed rank warning and update rank.
	DoRankIncreaseWarning(iRank, bNotify);
}
void CvCity::DoRankIncreaseWarning(int iRank, bool bNotify)
{
	if(bNotify)
	{
		if(GetTurnsSinceLastRankMessage() >= (GC.getBALANCE_SPY_SABOTAGE_RATE() * 2))
		{
			if((iRank > GetRank()) && (GetRank() > 4))
			{
				CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
				if(pNotifications)
				{
					Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RANK_INCREASING_SUMMARY");
					strSummary <<  getNameKey();
					Localization::String strNotification = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RANK_INCREASING");
					strNotification <<  getNameKey();
					strNotification <<  iRank;
					pNotifications->Add(NOTIFICATION_SPY_YOU_STAGE_COUP_FAILURE, strNotification.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
				}
				if(GC.getLogging())
				{
					CvString strMsg;
					strMsg.Format("Advanced Action: Sent out Rank warning. Rank: %d,", iRank);
					strMsg += " , ";
					strMsg += GET_PLAYER(getOwner()).getCivilizationShortDescription();
					strMsg += " , ";
					strMsg += getName();
					GET_PLAYER(getOwner()).GetEspionage()->LogEspionageMsg(strMsg);
				}
				SetTurnsSinceLastRankMessage(0);
			}
		}
		else
		{
			if(GetTurnsSinceLastRankMessage() <= 0)
			{
				SetTurnsSinceLastRankMessage(0);
			}
			ChangeTurnsSinceLastRankMessage(1);
		}
	}
	SetRank(iRank);
}
#endif

//	--------------------------------------------------------------------------------
/// How far out this city may buy plots
int CvCity::getBuyPlotDistance() const
{
	int iDistance = GET_PLAYER(getOwner()).getBuyPlotDistance();
	
	iDistance = std::min(MAX_CITY_RADIUS, std::max(getWorkPlotDistance(), iDistance));
	return iDistance;
}

//	--------------------------------------------------------------------------------
/// How far out this city may buy/work plots
int CvCity::getWorkPlotDistance(int iChange) const
{
	int iDistance = GET_PLAYER(getOwner()).getWorkPlotDistance();
	
	// Change distance based on buildings/wonders in this city
	iDistance += GetCityWorkingChange();
	
	iDistance = std::min(MAX_CITY_RADIUS, std::max(MIN_CITY_RADIUS, iDistance+iChange));
	return iDistance;
}

//	--------------------------------------------------------------------------------
/// How many plots this city may work
int CvCity::GetNumWorkablePlots(int iChange) const
{
	int iWorkablePlots = ((6 * (1+getWorkPlotDistance(iChange)) * getWorkPlotDistance(iChange) / 2) + 1);
	return iWorkablePlots;
}

//	--------------------------------------------------------------------------------
void CvCity::clearWorkingOverride(int iIndex)
{
	VALIDATE_OBJECT
	CvPlot* pPlot;

	pPlot = GetCityCitizens()->GetCityPlotFromIndex(iIndex);

	if(pPlot != NULL)
	{
		pPlot->setWorkingCityOverride(NULL);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::countNumImprovedPlots(ImprovementTypes eImprovement, bool bPotential) const
{
	VALIDATE_OBJECT
	CvPlot* pLoopPlot;
	int iCount;
	int iI;

	iCount = 0;

	CvCityCitizens* pCityCitizens = GetCityCitizens();


	for(iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		pLoopPlot = pCityCitizens->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->getWorkingCity() == this)
			{
				if(eImprovement != NO_IMPROVEMENT)
				{
					if((pLoopPlot->getImprovementType() == eImprovement && !pLoopPlot->IsImprovementPillaged()) || (bPotential && pLoopPlot->canHaveImprovement(eImprovement, getOwner())))
					{
						++iCount;
					}
				}
				else if(pLoopPlot->getImprovementType() != NO_IMPROVEMENT && !pLoopPlot->IsImprovementPillaged())
				{
					iCount++;
				}
			}
		}
	}

	return iCount;
}


//	--------------------------------------------------------------------------------
int CvCity::countNumWaterPlots() const
{
	VALIDATE_OBJECT
	CvPlot* pLoopPlot;
	int iCount;
	int iI;

	iCount = 0;

	CvCityCitizens* pCityCitizens = GetCityCitizens();


	for(iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		pLoopPlot = pCityCitizens->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->isWater())
			{
				if(pLoopPlot->getWorkingCity() == this)
				{
					iCount++;
				}
			}
		}
	}

	return iCount;
}

//	--------------------------------------------------------------------------------
int CvCity::countNumRiverPlots() const
{
	VALIDATE_OBJECT
	int iCount = 0;

	CvCityCitizens* pCityCitizens = GetCityCitizens();


	for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		CvPlot* pLoopPlot = pCityCitizens->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->isRiver())
			{
				if(pLoopPlot->getWorkingCity() == this)
				{
					++iCount;
				}
			}
		}
	}

	return iCount;
}

//	--------------------------------------------------------------------------------
int CvCity::countNumForestPlots() const
{
	VALIDATE_OBJECT
	int iCount = 0;


	for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
	{
		CvPlot* pLoopPlot = GetCityCitizens()->GetCityPlotFromIndex(iI);

		if(pLoopPlot != NULL)
		{
			if(pLoopPlot->getWorkingCity() == this)
			{
				if(pLoopPlot->getFeatureType() == FEATURE_FOREST)
				{
					++iCount;
				}
			}
		}
	}

	return iCount;
}

//	--------------------------------------------------------------------------------
int CvCity::findPopulationRank()
{
	VALIDATE_OBJECT
	if(!m_bPopulationRankValid)
	{
		int iRank = 1;

		int iLoop;
		CvCity* pLoopCity;
		for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
		{
			if((pLoopCity->getPopulation() > getPopulation()) ||
			        ((pLoopCity->getPopulation() == getPopulation()) && (pLoopCity->GetID() < GetID())))
			{
				iRank++;
			}
		}

		// shenanigans are to get around the const check
		m_bPopulationRankValid = true;
		m_iPopulationRank = iRank;
	}

	return m_iPopulationRank;
}


//	--------------------------------------------------------------------------------
int CvCity::findBaseYieldRateRank(YieldTypes eYield)
{
	VALIDATE_OBJECT
	if(!m_abBaseYieldRankValid[eYield])
	{
		int iRate = getBaseYieldRate(eYield);

		int iRank = 1;

		int iLoop;
		CvCity* pLoopCity;
		for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
		{
			if((pLoopCity->getBaseYieldRate(eYield) > iRate) ||
			        ((pLoopCity->getBaseYieldRate(eYield) == iRate) && (pLoopCity->GetID() < GetID())))
			{
				iRank++;
			}
		}

		m_abBaseYieldRankValid.setAt(eYield, true);
		m_aiBaseYieldRank.setAt(eYield, iRank);
	}

	return m_aiBaseYieldRank[eYield];
}


//	--------------------------------------------------------------------------------
int CvCity::findYieldRateRank(YieldTypes eYield)
{
	if(!m_abYieldRankValid[eYield])
	{
		int iRate = getYieldRateTimes100(eYield, false);

		int iRank = 1;

		int iLoop;
		CvCity* pLoopCity;
		for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
		{
			if ((pLoopCity->getYieldRateTimes100(eYield, false) > iRate) ||
			    ((pLoopCity->getYieldRateTimes100(eYield, false) == iRate) && (pLoopCity->GetID() < GetID())))
			{
				iRank++;
			}
		}

		m_abYieldRankValid.setAt(eYield, true);
		m_aiYieldRank.setAt(eYield, iRank);
	}

	return m_aiYieldRank[eYield];
}


//	--------------------------------------------------------------------------------
// Returns one of the upgrades...
UnitTypes CvCity::allUpgradesAvailable(UnitTypes eUnit, int iUpgradeCount) const
{
	VALIDATE_OBJECT
	UnitTypes eUpgradeUnit;
	bool bUpgradeFound;
	bool bUpgradeAvailable;
	bool bUpgradeUnavailable;

	CvAssertMsg(eUnit != NO_UNIT, "eUnit is expected to be assigned (not NO_UNIT)");

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo == NULL)
		return NO_UNIT;

	if(iUpgradeCount > GC.getNumUnitClassInfos())
	{
		return NO_UNIT;
	}

	eUpgradeUnit = NO_UNIT;

	bUpgradeFound = false;
	bUpgradeAvailable = false;
	bUpgradeUnavailable = false;

	const CvCivilizationInfo& thisCiv = getCivilizationInfo();

	for(int iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
	{
		const UnitClassTypes eUnitClass = static_cast<UnitClassTypes>(iI);
		CvUnitClassInfo* pkUnitClassInfo = GC.getUnitClassInfo(eUnitClass);
		if(pkUnitClassInfo)
		{
			if(pkUnitInfo->GetUpgradeUnitClass(iI))
			{
				const UnitTypes eLoopUnit = (UnitTypes) thisCiv.getCivilizationUnits(iI);

				if(eLoopUnit != NO_UNIT)
				{
					bUpgradeFound = true;

					const UnitTypes eTempUnit = allUpgradesAvailable(eLoopUnit, (iUpgradeCount + 1));

					if(eTempUnit != NO_UNIT)
					{
						eUpgradeUnit = eTempUnit;
						bUpgradeAvailable = true;
					}
					else
					{
						bUpgradeUnavailable = true;
					}
				}
			}
		}
	}

	if(iUpgradeCount > 0)
	{
		if(bUpgradeFound && bUpgradeAvailable)
		{
			CvAssertMsg(eUpgradeUnit != NO_UNIT, "eUpgradeUnit is expected to be assigned (not NO_UNIT)");
			return eUpgradeUnit;
		}

		if(canTrain(eUnit, false, false, false, true))
		{
			return eUnit;
		}
	}
	else
	{
		if(bUpgradeFound && !bUpgradeUnavailable)
		{
			return eUpgradeUnit;
		}
	}

	return NO_UNIT;
}


//	--------------------------------------------------------------------------------
bool CvCity::isWorldWondersMaxed() const
{
	VALIDATE_OBJECT
	if(GC.getGame().isOption(GAMEOPTION_ONE_CITY_CHALLENGE) && isHuman())
	{
		return false;
	}

	if(GC.getMAX_WORLD_WONDERS_PER_CITY() == -1)
	{
		return false;
	}

	if(getNumWorldWonders() >= GC.getMAX_WORLD_WONDERS_PER_CITY())
	{
		return true;
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isTeamWondersMaxed() const
{
	VALIDATE_OBJECT
	if(GC.getGame().isOption(GAMEOPTION_ONE_CITY_CHALLENGE) && isHuman())
	{
		return false;
	}

	if(GC.getMAX_TEAM_WONDERS_PER_CITY() == -1)
	{
		return false;
	}

	if(getNumTeamWonders() >= GC.getMAX_TEAM_WONDERS_PER_CITY())
	{
		return true;
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isNationalWondersMaxed() const
{
	VALIDATE_OBJECT
	int iMaxNumWonders = (GC.getGame().isOption(GAMEOPTION_ONE_CITY_CHALLENGE) && isHuman()) ? GC.getMAX_NATIONAL_WONDERS_PER_CITY_FOR_OCC() : GC.getMAX_NATIONAL_WONDERS_PER_CITY();

	if(iMaxNumWonders == -1)
	{
		return false;
	}

	if(getNumNationalWonders() >= iMaxNumWonders)
	{
		return true;
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isBuildingsMaxed() const
{
	VALIDATE_OBJECT
	if(GC.getMAX_BUILDINGS_PER_CITY() == -1)
	{
		return false;
	}

	if(GC.getGame().isOption(GAMEOPTION_ONE_CITY_CHALLENGE) && isHuman())
	{
		return false;
	}

	if(m_pCityBuildings->GetNumBuildings() >= GC.getMAX_BUILDINGS_PER_CITY())
	{
		return true;
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::canTrain(UnitTypes eUnit, bool bContinue, bool bTestVisible, bool bIgnoreCost, bool bWillPurchase, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	if(eUnit == NO_UNIT)
	{
		return false;
	}

	CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eUnit);
	if(pkUnitEntry == NULL)
	{
		return false;
	}

	if(!(GET_PLAYER(getOwner()).canTrain(eUnit, bContinue, bTestVisible, bIgnoreCost, false, toolTipSink)))
	{
		return false;
	}

	if (!bWillPurchase && pkUnitEntry->IsPurchaseOnly())
	{
		return false;
	}
#if defined(MOD_BALANCE_CORE)
	// If Zulu Player has this trait and Pikeman are an immediate upgrade to Impi, let's not let player exploit lower production cost of pikeman->impi. So, let's make it immediately obsolete.
	CvUnitEntry& pUnitInfo = *pkUnitEntry;
	const UnitClassTypes eUnitClass = (UnitClassTypes) pUnitInfo.GetUnitClassType();
	UnitClassTypes ePikemanClass = (UnitClassTypes)GC.getInfoTypeForString("UNITCLASS_PIKEMAN");
	UnitTypes eZuluImpi = (UnitTypes)GC.getInfoTypeForString("UNIT_ZULU_IMPI");
	if(&pUnitInfo != NULL && GET_PLAYER(getOwner()).GetPlayerTraits()->IsFreeZuluPikemanToImpi())
	{
		if(eUnitClass != NO_UNITCLASS && (eUnitClass == ePikemanClass) && GET_PLAYER(getOwner()).canTrain(eZuluImpi, false, false, true))
		{
			return false;
		}
	}
#endif
	if(!bTestVisible)
	{
		CvUnitEntry& thisUnitInfo = *pkUnitEntry;
		// Settlers may not be trained in Cities that are too small
		if(thisUnitInfo.IsFound() || thisUnitInfo.IsFoundAbroad())
		{
			int iSizeRequirement = /*2*/ GC.getCITY_MIN_SIZE_FOR_SETTLERS();
			if(getPopulation() < iSizeRequirement)
			{
				GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_SETTLER_SIZE_LIMIT", "", "", iSizeRequirement);
				if(toolTipSink == NULL)
					return false;
			}
		}

		// See if there are any BuildingClass requirements
		const int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
		const CvCivilizationInfo& thisCivilization = getCivilizationInfo();
		for(int iBuildingClassLoop = 0; iBuildingClassLoop < iNumBuildingClassInfos; iBuildingClassLoop++)
		{
			const BuildingClassTypes eBuildingClass = (BuildingClassTypes) iBuildingClassLoop;
			CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
			if(!pkBuildingClassInfo)
			{
				continue;
			}

			// Requires Building
			if(thisUnitInfo.GetBuildingClassRequireds(eBuildingClass))
			{
				const BuildingTypes ePrereqBuilding = (BuildingTypes)(thisCivilization.getCivilizationBuildings(eBuildingClass));

				if(GetCityBuildings()->GetNumBuilding(ePrereqBuilding) == 0)
				{
					CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(ePrereqBuilding);
					if(pkBuildingInfo)
					{
						GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_UNIT_REQUIRES_BUILDING", pkBuildingInfo->GetDescriptionKey());
						if(toolTipSink == NULL)
							return false;
					}
				}
			}
		}

		// Air units can't be built above capacity
		if (pkUnitEntry->GetDomainType() == DOMAIN_AIR)
		{
			int iNumAirUnits = plot()->countNumAirUnits(getTeam());
			if (iNumAirUnits >= GetMaxAirUnits())
			{
				GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_CITY_AT_AIR_CAPACITY");
				if(toolTipSink == NULL)
					return false;
			}
		}
	}

	if(!plot()->canTrain(eUnit, bContinue, bTestVisible))
	{
		return false;
	}

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(eUnit);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanTrain", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::canTrain(UnitCombatTypes eUnitCombat) const
{
	VALIDATE_OBJECT
	for(int i = 0; i < GC.getNumUnitInfos(); i++)
	{
		const UnitTypes eUnit = static_cast<UnitTypes>(i);
		CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
		if(pkUnitInfo)
		{
			if(pkUnitInfo->GetUnitCombatType() == eUnitCombat)
			{
				if(canTrain(eUnit))
				{
					return true;
				}
			}
		}
	}

	return false;
}


//	--------------------------------------------------------------------------------
#if defined(MOD_API_EXTENSIONS)
bool CvCity::canConstruct(BuildingTypes eBuilding, bool bContinue, bool bTestVisible, bool bIgnoreCost, bool bWillPurchase, CvString* toolTipSink) const
{
	std::vector<int> vTotalBuildingCount( GC.getNumBuildingInfos(), 0);
	int iLoop;
	for(const CvCity* pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
	{
		if(pLoopCity && !pLoopCity->IsPuppet())
		{
			const std::vector<BuildingTypes>& vBuildings = pLoopCity->GetCityBuildings()->GetAllBuildingsHere();
			for (size_t i=0; i<vBuildings.size(); i++)
				vTotalBuildingCount[ vBuildings[i] ]++;
		}
	}

	return canConstruct(eBuilding,vTotalBuildingCount,bContinue,bTestVisible,bIgnoreCost,bWillPurchase,toolTipSink);
}

bool CvCity::canConstruct(BuildingTypes eBuilding, const std::vector<int>& vPreExistingBuildings, bool bContinue, bool bTestVisible, bool bIgnoreCost, bool bWillPurchase, CvString* toolTipSink) const
#else
bool CvCity::canConstruct(BuildingTypes eBuilding, bool bContinue, bool bTestVisible, bool bIgnoreCost, CvString* toolTipSink) const
#endif
{
	VALIDATE_OBJECT
	BuildingTypes ePrereqBuilding;
	int iI;

	if(eBuilding == NO_BUILDING)
	{
		return false;
	}

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
	{
		return false;
	}

	if(!(GET_PLAYER(getOwner()).canConstruct(eBuilding, vPreExistingBuildings, bContinue, bTestVisible, bIgnoreCost, toolTipSink)))
	{
		return false;
	}

#if defined(MOD_API_EXTENSIONS)
	if (!bWillPurchase && pkBuildingInfo->IsPurchaseOnly())
	{
		return false;
	}
	if(pkBuildingInfo->IsRequiresRail())
	{
		if(!IsIndustrialRouteToCapitalConnected())
		{
			return false;
		}
		else
		{
			//Check for an actual railroad here.
			bool bRailroad = false;
			for (int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
			{
				CvPlot* pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));
				if (pAdjacentPlot && pAdjacentPlot->getRouteType() == ROUTE_RAILROAD && pAdjacentPlot->IsCityConnection(getOwner()))
				{
					bRailroad = true;
					break;
				}
			}
			if(!bRailroad)
			{
				return false;
			}
		}
	}
	if((pkBuildingInfo->GetCivType() != NO_CIVILIZATION) && (getCivilizationType() != pkBuildingInfo->GetCivType()))
	{
		return false;
	}
#endif

	if(m_pCityBuildings->GetNumBuilding(eBuilding) >= GC.getCITY_MAX_NUM_BUILDINGS())
	{
		return false;
	}

	if(!isValidBuildingLocation(eBuilding))
	{
		return false;
	}

#if defined(MOD_BALANCE_CORE_BELIEFS)
	// Religion-enabled national wonder
	if(pkBuildingInfo && pkBuildingInfo->IsUnlockedByBelief() && pkBuildingInfo->IsReformation())
	{
		CvGameReligions* pReligions = GC.getGame().GetGameReligions();
		ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer();
		if(eReligion != NO_RELIGION)
		{
			const CvReligion* pReligion = pReligions->GetReligion(eReligion, getOwner());
			if(pReligion == NULL || !pReligion->m_Beliefs.IsBuildingClassEnabled((BuildingClassTypes)pkBuildingInfo->GetBuildingClassType(), getOwner()))
			{
				return false;
			}
		}
		else
		{
			return false;
		}
	}
#endif

	// Local Resource requirements met?
	if(!IsBuildingLocalResourceValid(eBuilding, bTestVisible, toolTipSink))
	{
		return false;
	}
#if defined(MOD_BALANCE_CORE_DEALS)
	// Resource Monopoly requirements met?
	if(MOD_BALANCE_CORE_DEALS && !IsBuildingResourceMonopolyValid(eBuilding, toolTipSink))
	{
		return false;
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if(!IsBuildingFeatureValid(eBuilding, toolTipSink))
	{
		return false;
	}
#endif
#if defined(MOD_BALANCE_CORE)
	// Corporation building?
	if (pkBuildingInfo->GetBuildingClassInfo().getCorporationType() != NO_CORPORATION)
	{
		// Must have Corporations tech
		if (!GET_TEAM(GET_PLAYER(getOwner()).getTeam()).IsCorporationsEnabled())
		{
			return false;
		}

		// This building is a franchise - cannot construct EVER
		if (pkBuildingInfo->GetBuildingClassInfo().IsFranchise())
		{
			return false;
		}
		// Corporation HQ
		if (pkBuildingInfo->GetBuildingClassInfo().IsHeadquarters())
		{
			CvPlayerCorporations* pPlayerCorporation = GET_PLAYER(getOwner()).GetCorporations();
			// Cannot construct if corporation exists or we've founded a corporation already
			if (pPlayerCorporation->HasFoundedCorporation() ||
				GC.getGame().GetGameCorporations()->IsCorporationFounded(pkBuildingInfo->GetBuildingClassInfo().getCorporationType()))
			{
				return false;
			}
		}
		// Corporation Office
		if (pkBuildingInfo->GetBuildingClassInfo().IsOffice())
		{
			CvPlayerCorporations* pPlayerCorporation = GET_PLAYER(getOwner()).GetCorporations();
			// Cannot construct if we do not have this corporation
			if (pPlayerCorporation->GetFoundedCorporation() != pkBuildingInfo->GetBuildingClassInfo().getCorporationType())
			{
				return false;
			}
		}
	}
#endif
	// Holy city requirement
	if (pkBuildingInfo->IsRequiresHolyCity() && !GetCityReligions()->IsHolyCityAnyReligion())
	{
		return false;
	}

	const CvCivilizationInfo& thisCivInfo = *GC.getCivilizationInfo(getCivilizationType());
	int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();

	// Can't construct a building to reduce occupied unhappiness if the city isn't occupied
#if defined(MOD_BALANCE_CORE)
	if(pkBuildingInfo->IsNoOccupiedUnhappiness() && !IsOccupied() && !pkBuildingInfo->IsBuildAnywhere())
#else
	if(pkBuildingInfo->IsNoOccupiedUnhappiness() && !IsOccupied())
#endif
		return false;

	// Does this city have prereq buildings?
	for(iI = 0; iI < iNumBuildingClassInfos; iI++)
	{
		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
		if(!pkBuildingClassInfo)
		{
			continue;
		}

		if(pkBuildingInfo->IsBuildingClassNeededInCity(iI))
		{
			ePrereqBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

#if defined(MOD_BALANCE_CORE)
			//Exception for new Rome UA, because civ type doesn't help you here.
			if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsKeepConqueredBuildings())
			{
				if(0 == m_pCityBuildings->GetNumBuildingClass((BuildingClassTypes)iI))
				{
					return false;
				}
			}
			else
#endif

			if(ePrereqBuilding != NO_BUILDING)
			{
				if(0 == m_pCityBuildings->GetNumBuilding(ePrereqBuilding) /* && (bContinue || (getFirstBuildingOrder(ePrereqBuilding) == -1))*/)
				{
					return false;
				}
			}
		}
	}
#if defined(MOD_BALANCE_CORE)
	// Does this city have prereq buildings?
	if(MOD_BALANCE_CORE)
	{
		int iNumBuildings = 0;
		for(iI = 0; iI < iNumBuildingClassInfos; iI++)
		{
			CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
			if(!pkBuildingClassInfo)
			{
				continue;
			}

			if(pkBuildingInfo->IsBuildingClassNeededAnywhere(iI))
			{
				ePrereqBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

				if(ePrereqBuilding != NO_BUILDING)
				{
					CvCity* pLoopCity;
					int iLoop;
					for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
					{
						if(pLoopCity->GetCityBuildings()->GetNumBuilding(ePrereqBuilding) > 0)
						{
							iNumBuildings++;
						}
					}
					if(iNumBuildings == 0)
					{
						return false;
					}
				}
			}
			// Does this city have prereq buildings?
			if(MOD_BALANCE_CORE && pkBuildingInfo->IsBuildingClassNeededNowhere(iI))
			{
				BuildingTypes ePrereqBuilding = ((BuildingTypes)(getCivilizationInfo().getCivilizationBuildings(iI)));

				if(ePrereqBuilding != NO_BUILDING)
				{
					CvCity* pLoopCity;
					int iLoop;
					for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
					{
						if(pLoopCity->GetCityBuildings()->GetNumBuilding(ePrereqBuilding) > 0)
						{
							return false;
						}
					}
				}
			}
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if(MOD_BALANCE_CORE && pkBuildingInfo->GetNeedBuildingThisCity() != NO_BUILDING)
	{
		BuildingTypes ePrereqBuilding = (BuildingTypes)pkBuildingInfo->GetNeedBuildingThisCity();
		if(m_pCityBuildings->GetNumBuilding(ePrereqBuilding) <= 0)
		{
			return false;
		}
	}
	int iNumBuildingInfos = GC.getNumBuildingInfos();
	//Check for uniques of the same type.
	for(iI = 0; iI < iNumBuildingInfos; iI++)
	{
		CvBuildingEntry* pkBuildingInfo2 = GC.getBuildingInfo((BuildingTypes)iI);
		if(pkBuildingInfo2 == NULL)
		{
			continue;
		}
		//Same class?
		if(pkBuildingInfo2->GetBuildingClassType() == pkBuildingInfo->GetBuildingClassType())
		{
			//Do we already have this building? Return false if so.
			if(m_pCityBuildings->GetNumBuilding((BuildingTypes)iI) > 0)
			{
				return false;
			}
		}
	}
#endif

	///////////////////////////////////////////////////////////////////////////////////
	// Everything above this is what is checked to see if Building shows up in the list of construction items
	///////////////////////////////////////////////////////////////////////////////////

	const CvBuildingClassInfo& kBuildingClassInfo = pkBuildingInfo->GetBuildingClassInfo();
	if(!bTestVisible)
	{
		if(!bContinue)
		{
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
			if(!MOD_BALANCE_CORE_BUILDING_INVESTMENTS && getFirstBuildingOrder(eBuilding) != -1)
#else
			if(getFirstBuildingOrder(eBuilding) != -1)
#endif
			{
				return false;
			}
		}

		if(!(kBuildingClassInfo.isNoLimit()))
		{
			if(isWorldWonderClass(kBuildingClassInfo))
			{
				if(isWorldWondersMaxed())
				{
					return false;
				}
			}
			else if(isTeamWonderClass(kBuildingClassInfo))
			{
				if(isTeamWondersMaxed())
				{
					return false;
				}
			}
			else if(isNationalWonderClass(kBuildingClassInfo))
			{
				if(isNationalWondersMaxed())
				{
					return false;
				}
			}
			else
			{
				if(isBuildingsMaxed())
				{
					return false;
				}
			}
		}
	}

	// Locked Buildings (Mutually Exclusive Buildings?) - not quite sure how this works
	for(iI = 0; iI < iNumBuildingClassInfos; iI++)
	{
		BuildingClassTypes eLockedBuildingClass = (BuildingClassTypes) pkBuildingInfo->GetLockedBuildingClasses(iI);

		if(eLockedBuildingClass != NO_BUILDINGCLASS)
		{
			BuildingTypes eLockedBuilding = (BuildingTypes)(thisCivInfo.getCivilizationBuildings(eLockedBuildingClass));

			if(eLockedBuilding != NO_BUILDING)
			{
				if(m_pCityBuildings->GetNumBuilding(eLockedBuilding) > 0)
				{
					return false;
				}
			}
		}
	}

	// Mutually Exclusive Buildings 2
	if(pkBuildingInfo->GetMutuallyExclusiveGroup() != -1)
	{
		int iNumBuildingInfos = GC.getNumBuildingInfos();
		for(iI = 0; iI < iNumBuildingInfos; iI++)
		{
			const BuildingTypes eBuildingLoop = static_cast<BuildingTypes>(iI);

			CvBuildingEntry* pkLoopBuilding = GC.getBuildingInfo(eBuildingLoop);
			if(pkLoopBuilding)
			{
				// Buildings are in a Mutually Exclusive Group, so only one is allowed
				if(pkLoopBuilding->GetMutuallyExclusiveGroup() == pkBuildingInfo->GetMutuallyExclusiveGroup())
				{
					if(m_pCityBuildings->GetNumBuilding(eBuildingLoop) > 0)
					{
						return false;
					}
				}
			}
		}
	}

#if defined(MOD_BALANCE_CORE)
	if (!bTestVisible) // Test visible check here is so that the buildings will show up in the build list, but can't be selected (for every other city!)
	{
		// Check if it's a Corporation headquarters
		if (pkBuildingInfo->GetBuildingClassInfo().IsHeadquarters())
		{
			CvCity* pLoopCity;
			int iLoop;
			for (pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
			{
				if (pLoopCity == NULL)
					continue;

				if (pLoopCity->GetID() == GetID())
					continue;

				BuildingTypes eTestBuilding = pLoopCity->getProductionBuilding();
				if (eTestBuilding != NO_BUILDING)
				{
					CvBuildingEntry* pkLoopBuildingInfo = GC.getBuildingInfo(eTestBuilding);
					if (pkLoopBuildingInfo && pkLoopBuildingInfo->GetBuildingClassInfo().IsHeadquarters())
					{
						GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_ONE_CORP_ONLY", pkBuildingInfo->GetTextKey(), pkLoopBuildingInfo->GetDescription());
						if (toolTipSink == NULL)
							return false;
					}
				}
			}
		}
	}
#endif

#if defined(MOD_BALANCE_CORE_POP_REQ_BUILDINGS)
	//Requires a certain population size, either nationally or locally.
	if(MOD_BALANCE_CORE_POP_REQ_BUILDINGS)
	{
		if(pkBuildingInfo->GetLocalPopulationRequired() > 0)
		{
			int iPopRequired = pkBuildingInfo->GetLocalPopulationRequired();
			if(getPopulation() < iPopRequired)
			{
				GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_NEED_LOCAL_POP", pkBuildingInfo->GetTextKey(), "", iPopRequired - getPopulation());
				if(toolTipSink == NULL)
				return false;
			}
		}
	}
#endif

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(eBuilding);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanConstruct", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}


//	--------------------------------------------------------------------------------
bool CvCity::canCreate(ProjectTypes eProject, bool bContinue, bool bTestVisible) const
{
	VALIDATE_OBJECT

	if(!(GET_PLAYER(getOwner()).canCreate(eProject, bContinue, bTestVisible)))
	{
		return false;
	}
#if defined(MOD_WWII_PROJECTS)
	CvProjectEntry* pkProjectInfo = GC.getProjectInfo(eProject);
	const int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
	const CvCivilizationInfo& thisCivilization = getCivilizationInfo();

	for(int iBuildingClassLoop = 0; iBuildingClassLoop < iNumBuildingClassInfos; iBuildingClassLoop++)
	{
		const BuildingClassTypes eBuildingClass = (BuildingClassTypes) iBuildingClassLoop;
		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
		if(!pkBuildingClassInfo)
			continue;

		if(pkProjectInfo->GetBuildingClassRequireds(eBuildingClass))
		{
			const BuildingTypes ePrereqBuilding = (BuildingTypes) (thisCivilization.getCivilizationBuildings(eBuildingClass));
			if(GetCityBuildings()->GetNumBuilding(ePrereqBuilding) == 0)
				return false;
		}
	}
#endif
	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(eProject);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanCreate", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}


//	--------------------------------------------------------------------------------
bool CvCity::canPrepare(SpecialistTypes eSpecialist, bool bContinue) const
{
	VALIDATE_OBJECT

	if(!(GET_PLAYER(getOwner()).canPrepare(eSpecialist, bContinue)))
	{
		return false;
	}

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(eSpecialist);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanPrepare", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::canMaintain(ProcessTypes eProcess, bool bContinue) const
{
	VALIDATE_OBJECT

	if(!(GET_PLAYER(getOwner()).canMaintain(eProcess, bContinue)))
	{
		return false;
	}

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(eProcess);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanMaintain", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}


//	--------------------------------------------------------------------------------
bool CvCity::canJoin() const
{
	VALIDATE_OBJECT
	return true;
}

//	--------------------------------------------------------------------------------
// Are there a lot of clearable features around this city?
bool CvCity::IsFeatureSurrounded() const
{
	return m_bFeatureSurrounded;
}

//	--------------------------------------------------------------------------------
// Are there a lot of clearable features around this city?
void CvCity::SetFeatureSurrounded(bool bValue)
{
	if(IsFeatureSurrounded() != bValue)
		m_bFeatureSurrounded = bValue;
}

//	--------------------------------------------------------------------------------
// Are there a lot of clearable features around this city?
void CvCity::DoUpdateFeatureSurrounded()
{
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::DoUpdateFeatureSurrounded, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	int iTotalPlots = 0;
	int iFeaturedPlots = 0;

	// Look two tiles around this city in every direction to see if at least half the plots are covered in a removable feature
	const int iRange = 2;

	for(int iDX = -iRange; iDX <= iRange; iDX++)
	{
		for(int iDY = -iRange; iDY <= iRange; iDY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iRange);

			// Increase total plot count
			iTotalPlots++;

			if(pLoopPlot == NULL)
				continue;

			if(pLoopPlot->getFeatureType() == NO_FEATURE)
				continue;

			// Must be able to remove this thing?
			if(!GC.getFeatureInfo(pLoopPlot->getFeatureType())->IsClearable())
				continue;

			iFeaturedPlots++;
		}
	}

	bool bFeatureSurrounded = false;

	// At least half have coverage?
	if(iFeaturedPlots >= iTotalPlots / 2)
		bFeatureSurrounded = true;

	SetFeatureSurrounded(bFeatureSurrounded);
}

//	--------------------------------------------------------------------------------
/// Extra yield for a resource this city is working?
int CvCity::GetResourceExtraYield(ResourceTypes eResource, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource > -1 && eResource < GC.getNumResourceInfos(), "Invalid resource index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiResourceYieldChange[eResource][eYield];
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeResourceExtraYield(ResourceTypes eResource, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource > -1 && eResource < GC.getNumResourceInfos(), "Invalid resource index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(iChange != 0)
	{
		m_ppaiResourceYieldChange[eResource][eYield] += iChange;

		updateYield();
	}
}

//	--------------------------------------------------------------------------------
/// Extra yield for a Feature this city is working?
int CvCity::GetFeatureExtraYield(FeatureTypes eFeature, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Feature index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiFeatureYieldChange[eFeature][eYield];
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeFeatureExtraYield(FeatureTypes eFeature, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Feature index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(iChange != 0)
	{
		m_ppaiFeatureYieldChange[eFeature][eYield] += iChange;

		updateYield();
	}
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
/// Extra yield for a improvement this city is working?
int CvCity::GetImprovementExtraYield(ImprovementTypes eImprovement, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eImprovement > -1 && eImprovement < GC.getNumImprovementInfos(), "Invalid Improvement index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiImprovementYieldChange[eImprovement][eYield];
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeImprovementExtraYield(ImprovementTypes eImprovement, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eImprovement > -1 && eImprovement < GC.getNumImprovementInfos(), "Invalid Improvement index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(iChange != 0)
	{
		m_ppaiImprovementYieldChange[eImprovement][eYield] += iChange;

		updateYield();
	}
}

/// Extra yield for a building this city is lacking resources for?
int CvCity::GetExtraBuildingMaintenance() const
{
	VALIDATE_OBJECT
	return m_iExtraBuildingMaintenance;
}

//	--------------------------------------------------------------------------------
void CvCity::SetExtraBuildingMaintenance(int iChange)
{
	VALIDATE_OBJECT
	
	m_iExtraBuildingMaintenance = iChange;
}
#endif
//	--------------------------------------------------------------------------------
/// Extra yield for a Terrain this city is working?
int CvCity::GetTerrainExtraYield(TerrainTypes eTerrain, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiTerrainYieldChange[eTerrain][eYield];
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeTerrainExtraYield(TerrainTypes eTerrain, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(iChange != 0)
	{
		m_ppaiTerrainYieldChange[eTerrain][eYield] += iChange;

		updateYield();
	}
}

#if defined(MOD_API_UNIFIED_YIELDS) && defined(MOD_API_PLOT_YIELDS)
//	--------------------------------------------------------------------------------
/// Extra yield for a Plot this city is working?
int CvCity::GetPlotExtraYield(PlotTypes ePlot, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	if (MOD_API_PLOT_YIELDS) {
		CvAssertMsg(ePlot > -1 && ePlot < GC.getNumPlotInfos(), "Invalid Plot index.");
		CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

		return m_ppaiPlotYieldChange[ePlot][eYield];
	} else {
		return 0;
	}
}

//	--------------------------------------------------------------------------------
void CvCity::ChangePlotExtraYield(PlotTypes ePlot, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	if (MOD_API_PLOT_YIELDS) {
		CvAssertMsg(ePlot > -1 && ePlot < GC.getNumPlotInfos(), "Invalid Plot index.");
		CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

		if(iChange != 0)
		{
			m_ppaiPlotYieldChange[ePlot][eYield] += iChange;

			updateYield();
		}
	}
}
#endif
#if defined(MOD_BALANCE_CORE)
bool CvCity::IsHasFeatureLocal(FeatureTypes eFeature) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid resource index.");

	// See if we have the resource linked to this city, but not connected yet

	bool bFoundFeature = false;

	// Loop through all plots near this City to see if we can find eResource - tests are ordered to optimize performance
	CvPlot* pLoopPlot;

	for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);

		// Invalid plot
		if(pLoopPlot == NULL)
			continue;

		// Doesn't have the resource (ignore team first to save time)
		if(pLoopPlot->getFeatureType() != eFeature)
			continue;

		// Not owned by this player
		if(pLoopPlot->getOwner() != getOwner())
			continue;

		if(pLoopPlot->getWorkingCity() != this)
			continue;

		bFoundFeature = true;
		break;
	}

	return bFoundFeature;
}
#endif
//	--------------------------------------------------------------------------------
/// Does this City have eResource nearby?
bool CvCity::IsHasResourceLocal(ResourceTypes eResource, bool bTestVisible) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource > -1 && eResource < GC.getNumResourceInfos(), "Invalid resource index.");

	// Actually check to see if we have this Resource to use right now
	if(!bTestVisible)
	{
		return m_paiNumResourcesLocal[eResource] > 0;
	}

	// See if we have the resource linked to this city, but not connected yet

	bool bFoundResourceLinked = false;

	// Loop through all plots near this City to see if we can find eResource - tests are ordered to optimize performance
	CvPlot* pLoopPlot;

	for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);

		// Invalid plot
		if(pLoopPlot == NULL)
			continue;

		// Doesn't have the resource (ignore team first to save time)
		if(pLoopPlot->getResourceType() != eResource)
			continue;

		// Not owned by this player
		if(pLoopPlot->getOwner() != getOwner())
			continue;

		// Team can't see the resource here
		if(pLoopPlot->getResourceType(getTeam()) != eResource)
			continue;

		// Resource not linked to this city
		if(pLoopPlot->GetResourceLinkedCity() != this)
			continue;

		bFoundResourceLinked = true;
		break;
	}

	return bFoundResourceLinked;
}

#if defined(MOD_API_EXTENSIONS) || defined(MOD_TRADE_WONDER_RESOURCE_ROUTES)
int CvCity::GetNumResourceLocal(ResourceTypes eResource, bool bImproved)
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource > -1 && eResource < GC.getNumResourceInfos(), "Invalid resource index.");

	if (!bImproved) {
		return m_paiNumResourcesLocal[eResource];
	} else {
		int iCount = 0;
		CvImprovementEntry* pImprovement = GC.GetGameImprovements()->GetImprovementForResource(eResource);

		CvPlot* pLoopPlot;

		for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
		{
			pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);

			if (pLoopPlot != NULL && pLoopPlot->getWorkingCity() == this) {
				if (pLoopPlot->getResourceType() == eResource && pLoopPlot->getImprovementType() == ((ImprovementTypes) pImprovement->GetID()) && !pLoopPlot->IsImprovementPillaged()) {
					++iCount;
				}
			}
		}
		
		return iCount;
	}
}
#endif

//	--------------------------------------------------------------------------------
void CvCity::ChangeNumResourceLocal(ResourceTypes eResource, int iChange)
{
	VALIDATE_OBJECT

	CvAssertMsg(eResource >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eResource < GC.getNumResourceInfos(), "eIndex expected to be < GC.getNumResourceInfos()");

	if(iChange != 0)
	{
		bool bOldHasResource = IsHasResourceLocal(eResource, /*bTestVisible*/ false);

		m_paiNumResourcesLocal.setAt(eResource, m_paiNumResourcesLocal[eResource] + iChange);

		if(bOldHasResource != IsHasResourceLocal(eResource, /*bTestVisible*/ false))
		{
			if(IsHasResourceLocal(eResource, /*bTestVisible*/ false))
			{
				processResource(eResource, 1);

				// Notification letting player know his city gets a bonus for wonders
				int iWonderMod = GC.getResourceInfo(eResource)->getWonderProductionMod();
				if(iWonderMod != 0)
				{
#if defined(MOD_BALANCE_CORE_RESOURCE_FLAVORS)
					if(MOD_BALANCE_CORE_RESOURCE_FLAVORS && GC.getResourceInfo(eResource)->getWonderProductionModObsoleteEra() == GC.getInfoTypeForString("ERA_MEDIEVAL", true /*bHideAssert*/))
					{
						if(GET_PLAYER(getOwner()).GetCurrentEra() < GC.getInfoTypeForString("ERA_MEDIEVAL", true /*bHideAssert*/))
						{
							CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
							if(pNotifications)
							{
								Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD");
								strText << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey() << iWonderMod;
								Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD_SUMMARY");
								strSummary << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
								pNotifications->Add(NOTIFICATION_DISCOVERED_BONUS_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
							}
						}
					}
					else if(MOD_BALANCE_CORE_RESOURCE_FLAVORS && GC.getResourceInfo(eResource)->getWonderProductionModObsoleteEra() == GC.getInfoTypeForString("ERA_INDUSTRIAL", true /*bHideAssert*/))
					{
						if(GET_PLAYER(getOwner()).GetCurrentEra() < GC.getInfoTypeForString("ERA_INDUSTRIAL", true /*bHideAssert*/))
						{
							CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
							if(pNotifications)
							{
								Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD_LATE");
								strText << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey() << iWonderMod;
								Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD_SUMMARY");
								strSummary << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
								pNotifications->Add(NOTIFICATION_DISCOVERED_BONUS_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
							}
						}
					}
					else
					{
#endif
					CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
					if(pNotifications)
					{
						Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD");
						strText << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey() << iWonderMod;
						Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_WONDER_MOD_SUMMARY");
						strSummary << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
						pNotifications->Add(NOTIFICATION_DISCOVERED_BONUS_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
					}
#if defined(MOD_BALANCE_CORE_RESOURCE_FLAVORS)
					}
#endif
				}
			}
			else
			{
				processResource(eResource, -1);
			}
		}

		// Building Culture change for a local resource
		for(int iBuildingLoop = 0; iBuildingLoop < GC.getNumBuildingInfos(); iBuildingLoop++)
		{
			const BuildingTypes eBuilding = static_cast<BuildingTypes>(iBuildingLoop);
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
			if(pkBuildingInfo)
			{
				// Do we have this building?
				if(GetCityBuildings()->GetNumBuilding(eBuilding) > 0)
				{
					// Does eBuilding give culture with eResource?
					int iCulture = pkBuildingInfo->GetResourceCultureChange(eResource);

					if(iCulture != 0)
#if defined(MOD_BUGFIX_MINOR)
						iCulture *= GetCityBuildings()->GetNumBuilding(eBuilding);
#endif
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
						ChangeBaseYieldRateFromBuildings(YIELD_CULTURE, iCulture * iChange);
#else
						ChangeJONSCulturePerTurnFromBuildings(iCulture * iChange);
#endif

					// Does eBuilding give faith with eResource?
					int iFaith = pkBuildingInfo->GetResourceFaithChange(eResource);

					if(iFaith != 0)
#if defined(MOD_BUGFIX_MINOR)
						iFaith *= GetCityBuildings()->GetNumBuilding(eBuilding);
#endif
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
						ChangeBaseYieldRateFromBuildings(YIELD_FAITH, iFaith * iChange);
#else
						ChangeFaithPerTurnFromBuildings(iFaith * iChange);
#endif
#if defined(MOD_BALANCE_CORE)
					int iHappiness = pkBuildingInfo->GetResourceHappiness(eResource);
					if(iHappiness != 0)
						iHappiness *= GetCityBuildings()->GetNumBuilding(eBuilding);
						ChangeBaseHappinessFromBuildings(iHappiness * iChange);
#endif
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
/// Does eBuilding pass the local resource requirement test?
bool CvCity::IsBuildingLocalResourceValid(BuildingTypes eBuilding, bool bTestVisible, CvString* toolTipSink) const
{
	VALIDATE_OBJECT

	int iResourceLoop;
	ResourceTypes eResource;

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return false;

	// ANDs: City must have ALL of these nearby
#if defined(MOD_BALANCE_CORE)
	for(iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
#else
	for(iResourceLoop = 0; iResourceLoop < GC.getNUM_BUILDING_RESOURCE_PREREQS(); iResourceLoop++)
#endif
	{
		eResource = (ResourceTypes) pkBuildingInfo->GetLocalResourceAnd(iResourceLoop);

		// Doesn't require a resource in this AND slot
		if(eResource == NO_RESOURCE)
			continue;

		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if(pkResource == NULL)
			continue;

		// City doesn't have resource locally - return false immediately
		if(!IsHasResourceLocal(eResource, bTestVisible))
		{
			GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_LOCAL_RESOURCE", pkResource->GetTextKey(), pkResource->GetIconString());
			return false;
		}
	}

	int iOrResources = 0;

	// ORs: City must have ONE of these nearby
#if defined(MOD_BALANCE_CORE)
	for(iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
#else
	for(iResourceLoop = 0; iResourceLoop < GC.getNUM_BUILDING_RESOURCE_PREREQS(); iResourceLoop++)
#endif
	{
		eResource = (ResourceTypes) pkBuildingInfo->GetLocalResourceOr(iResourceLoop);

		// Doesn't require a resource in this AND slot
		if(eResource == NO_RESOURCE)
			continue;

		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if(pkResource == NULL)
			continue;

		// City has resource locally - return true immediately
		if(IsHasResourceLocal(eResource, bTestVisible))
			return true;

		// If we get here it means we passed the AND tests but not one of the OR tests
		GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_LOCAL_RESOURCE", pkResource->GetTextKey(), pkResource->GetIconString());

		// Increment counter for OR we don't have
		iOrResources++;
	}

	// No OR resource requirements (and passed the AND test above)
	if(iOrResources == 0)
		return true;

	return false;
}
#if defined(MOD_BALANCE_CORE_DEALS)
//	--------------------------------------------------------------------------------
/// Does eBuilding pass the resource monopoly requirement test?
bool CvCity::IsBuildingResourceMonopolyValid(BuildingTypes eBuilding, CvString* toolTipSink) const
{
	VALIDATE_OBJECT

	int iResourceLoop;
	ResourceTypes eResource = NO_RESOURCE;

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return false;

	// ANDs: City must have ALL of these nearby
	for(iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
	{
		// If this is a corporation HQ, consider it's corporation resource ANDs instead!
		// Still want to support Buildings
		const CvBuildingClassInfo& kBuildingClass = pkBuildingInfo->GetBuildingClassInfo();
		CorporationTypes eCorporation = kBuildingClass.getCorporationType();
		if (kBuildingClass.IsHeadquarters() && eCorporation != NO_CORPORATION)
		{

			CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
			if (pkCorporationInfo)
			{
				eResource = (ResourceTypes)pkCorporationInfo->GetResourceMonopolyAnd(iResourceLoop);
			}
		}
		else
		{
			eResource = (ResourceTypes)pkBuildingInfo->GetResourceMonopolyAnd(iResourceLoop);
		}

		// Doesn't require a resource in this AND slot
		if(eResource == NO_RESOURCE)
			continue;

		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if(pkResource == NULL)
			continue;

		// City doesn't have resource locally - return false immediately
		if(!GET_PLAYER(getOwner()).HasGlobalMonopoly(eResource))
		{
			int iOwnedNumResource = GET_PLAYER(getOwner()).getNumResourceTotal(eResource, false) + GET_PLAYER(getOwner()).getResourceExport(eResource);
			if(iOwnedNumResource > 0)
			{
				int iTotalNumResource = GC.getMap().getNumResources(eResource);
				if(iTotalNumResource > 0)
				{
					int iValue = ((iOwnedNumResource * 100) / iTotalNumResource);
					GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_NEED_MONOPOLY", pkResource->GetTextKey(), pkResource->GetIconString(), iValue);
				}
			}
			return false;
		}
		else if(GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned(getOwner(), eResource))
		{
			return false;
		}
	}

	int iOrResources = 0;

	// ORs: City must have ONE of these nearby
	for(iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
	{
		eResource = NO_RESOURCE;

		// If this is a corporation HQ, consider it's corporation resource ORs instead!
		// Still want to support Buildings
		const CvBuildingClassInfo& kBuildingClass =  pkBuildingInfo->GetBuildingClassInfo();
		CorporationTypes eCorporation = kBuildingClass.getCorporationType();
		if (kBuildingClass.IsHeadquarters() && eCorporation != NO_CORPORATION)
		{
			
			CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
			if (pkCorporationInfo)
			{
				eResource = (ResourceTypes)pkCorporationInfo->GetResourceMonopolyOr(iResourceLoop);
			}
		}
		else
		{
			eResource = (ResourceTypes)pkBuildingInfo->GetResourceMonopolyOr(iResourceLoop);
		}

		// Doesn't require a resource in this AND slot
		if(eResource == NO_RESOURCE)
			continue;

		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if(pkResource == NULL)
			continue;

		if(GC.getGame().GetGameLeagues()->IsLuxuryHappinessBanned(getOwner(), eResource))
			continue;

		// City has resource locally - return true immediately
		if(GET_PLAYER(getOwner()).HasGlobalMonopoly(eResource))
			return true;

		// If we get here it means we passed the AND tests but not one of the OR tests
		int iOwnedNumResource = GET_PLAYER(getOwner()).getNumResourceTotal(eResource, false) + GET_PLAYER(getOwner()).getResourceExport(eResource);
		if(iOwnedNumResource > 0)
		{
			int iTotalNumResource = GC.getMap().getNumResources(eResource);
			if(iTotalNumResource > 0)
			{
				int iValue = ((iOwnedNumResource * 100) / iTotalNumResource);
				GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_NEED_MONOPOLY", pkResource->GetTextKey(), pkResource->GetIconString(), iValue);
			}
		}

		// Increment counter for OR we don't have
		iOrResources++;
	}

	// No OR resource requirements (and passed the AND test above)
	if(iOrResources == 0)
		return true;

	return false;
}
#endif
#if defined(MOD_BALANCE_CORE)
bool CvCity::IsBuildingFeatureValid(BuildingTypes eBuilding, CvString* toolTipSink) const
{
	VALIDATE_OBJECT

	int iFeatureLoop;
	FeatureTypes eFeature;

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return false;

	// ANDs: City must have ALL of these nearby
	for(iFeatureLoop = 0; iFeatureLoop < GC.getNumFeatureInfos(); iFeatureLoop++)
	{
		eFeature = (FeatureTypes) pkBuildingInfo->GetFeatureAnd(iFeatureLoop);

		// Doesn't require a feature in this AND slot
		if(eFeature == NO_FEATURE)
			continue;

		CvFeatureInfo* pkFeature = GC.getFeatureInfo(eFeature);
		if(pkFeature == NULL)
			continue;

		// City doesn't have feature locally - return false immediately
		if(!IsHasFeatureLocal(eFeature))
		{
			GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_LOCAL_FEATURE", pkFeature->GetTextKey());
			return false;
		}
	}

	int iOrFeatures = 0;

	// ORs: City must have ONE of these nearby
	for(iFeatureLoop = 0; iFeatureLoop < GC.getNumFeatureInfos(); iFeatureLoop++)
	{
		eFeature = (FeatureTypes) pkBuildingInfo->GetFeatureOr(iFeatureLoop);

		// Doesn't require a feature in this AND slot
		if(eFeature == NO_FEATURE)
			continue;

		CvFeatureInfo* pkFeature = GC.getFeatureInfo(eFeature);
		if(pkFeature == NULL)
			continue;

		// City has feature locally - return true immediately
		if(IsHasFeatureLocal(eFeature))
			return true;

		// If we get here it means we passed the AND tests but not one of the OR tests
		GC.getGame().BuildCannotPerformActionHelpText(toolTipSink, "TXT_KEY_NO_ACTION_BUILDING_LOCAL_FEATURE", pkFeature->GetTextKey());

		// Increment counter for OR we don't have
		iOrFeatures++;
	}

	// No OR resource requirements (and passed the AND test above)
	if(iOrFeatures == 0)
		return true;

	return false;
}
#endif
//	--------------------------------------------------------------------------------
/// What Resource does this City want so that it goes into WLTKD?
ResourceTypes CvCity::GetResourceDemanded(bool bHideUnknown) const
{
	VALIDATE_OBJECT
	ResourceTypes eResourceDemanded = static_cast<ResourceTypes>(m_iResourceDemanded.get());

	// If we're not hiding the result then don't bother with looking at tech
	if(!bHideUnknown)
	{
		return eResourceDemanded;
	}

	if(eResourceDemanded != NO_RESOURCE)
	{
		CvResourceInfo* pInfo = GC.getResourceInfo(eResourceDemanded);
		if (pInfo)
		{
			TechTypes eRevealTech = (TechTypes)pInfo->getTechReveal();

			// Is there no Reveal Tech or do we have it?
			if (eRevealTech == NO_TECH || GET_TEAM(getTeam()).GetTeamTechs()->HasTech(eRevealTech))
			{
				return eResourceDemanded;
			}
		}
	}

	// We don't have the Tech to reveal the currently demanded Resource
	return NO_RESOURCE;
}

//	--------------------------------------------------------------------------------
/// Sets what Resource this City wants so that it goes into WLTKD
void CvCity::SetResourceDemanded(ResourceTypes eResource)
{
	VALIDATE_OBJECT
	m_iResourceDemanded = (ResourceTypes) eResource;
}

//	--------------------------------------------------------------------------------
/// Picks a Resource for this City to want
void CvCity::DoPickResourceDemanded(bool bCurrentResourceInvalid)
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::DoPickResourceDemanded, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	// Create the list of invalid Luxury Resources
	FStaticVector<ResourceTypes, 64, true, c_eCiv5GameplayDLL, 0> veInvalidLuxuryResources;
	CvPlot* pLoopPlot;
	ResourceTypes eResource;

	// Loop through all resource infos and invalidate resources that only come from minor civs
	for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
	{
		eResource = (ResourceTypes) iResourceLoop;
		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if (pkResource && pkResource->getResourceUsage() == RESOURCEUSAGE_LUXURY)
		{
			if (pkResource->isOnlyMinorCivs())
			{
				veInvalidLuxuryResources.push_back(eResource);
			}
		}
	}

	// Loop through all Plots near this City to see if there's Luxuries we should invalidate

	for(int iPlotLoop = 0; iPlotLoop < GetNumWorkablePlots(); iPlotLoop++)
	{
		pLoopPlot = iterateRingPlots(getX(), getY(), iPlotLoop);

		if(pLoopPlot != NULL)
		{
			eResource = pLoopPlot->getResourceType();

			if(eResource != NO_RESOURCE)
			{
				if(GC.getResourceInfo(eResource)->getResourceUsage() == RESOURCEUSAGE_LUXURY)
				{
					veInvalidLuxuryResources.push_back(eResource);
				}
			}
		}
	}

	// Current Resource demanded may not be a valid choice
	ResourceTypes eCurrentResource = GetResourceDemanded(false);
	if(bCurrentResourceInvalid && eCurrentResource != NO_RESOURCE)
	{
		veInvalidLuxuryResources.push_back(eCurrentResource);
	}

	// Create list of valid Luxuries
	FStaticVector<ResourceTypes, 64, true, c_eCiv5GameplayDLL, 0> veValidLuxuryResources;
	for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
	{
		eResource = (ResourceTypes) iResourceLoop;

		// Is this a Luxury Resource?
		CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
		if(pkResource && pkResource->getResourceUsage() == RESOURCEUSAGE_LUXURY)
		{
			// Is the Resource actually on the map?
			if(GC.getMap().getNumResources(eResource) > 0)
			{
				// Can't be a minor civ only resource!
				if(!GC.getResourceInfo(eResource)->isOnlyMinorCivs())
				{
					// We must not have this already
					if(GET_PLAYER(getOwner()).getNumResourceAvailable(eResource) == 0)
						veValidLuxuryResources.push_back(eResource);
				}
			}
		}
	}

	// Is there actually anything in our vector? - 0 can be valid if we already have everything, for example
	if(veValidLuxuryResources.size() == 0)
	{
		return;
	}

	// Now pick a Luxury we can use
	int iNumAttempts = 0;
	int iVectorLoop;
	int iVectorIndex;
	bool bResourceValid;

	do
	{
		iVectorIndex = GC.getGame().getJonRandNum(veValidLuxuryResources.size(), "Picking random Luxury for City to demand.");
		eResource = (ResourceTypes) veValidLuxuryResources[iVectorIndex];
		bResourceValid = true;

		// Look at all invalid Resources found to see if our randomly-picked Resource matches any
		for(iVectorLoop = 0; iVectorLoop < (int) veInvalidLuxuryResources.size(); iVectorLoop++)
		{
			if(eResource == veInvalidLuxuryResources[iVectorLoop])
			{
				bResourceValid = false;
				break;
			}
		}

		// Not found nearby?
		if(bResourceValid)
		{
			SetResourceDemanded(eResource);

			// Notification
			CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
			if(pNotifications)
			{
#if defined(MOD_BALANCE_CORE)

				if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon() > 0)
				{
					Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_DEMAND_UA");
					strText << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
					Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_RESOURCE_DEMAND");
					strSummary << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
					pNotifications->Add(NOTIFICATION_REQUEST_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
				}
				else
				{
#endif
				Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_RESOURCE_DEMAND");
				strText << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
				Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_RESOURCE_DEMAND");
				strSummary << getNameKey() << GC.getResourceInfo(eResource)->GetTextKey();
				pNotifications->Add(NOTIFICATION_REQUEST_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
#if defined(MOD_BALANCE_CORE)
				}
#endif
			}

			return;
		}

		iNumAttempts++;
	}
	while(iNumAttempts < 500);

	// If we're on the debug map it's too small for us to care
	if(GC.getMap().getWorldSize() != WORLDSIZE_DEBUG)
	{
		CvAssertMsg(false, "Gameplay: Didn't find a Luxury for City to demand.");
	}
}

//	--------------------------------------------------------------------------------
/// Checks to see if we have the Resource demanded and if so starts WLTKD in this City
void CvCity::DoTestResourceDemanded()
{
	VALIDATE_OBJECT
	ResourceTypes eResource = GetResourceDemanded();

	if(GetWeLoveTheKingDayCounter() > 0)
	{
		ChangeWeLoveTheKingDayCounter(-1);

		// WLTKD over!
		if(GetWeLoveTheKingDayCounter() == 0)
		{
			DoPickResourceDemanded();

			if(getOwner() == GC.getGame().getActivePlayer())
			{
				Localization::String localizedText;
				// Know what the next Demanded Resource is
				if(GetResourceDemanded() != NO_RESOURCE)
				{
					localizedText = Localization::Lookup("TXT_KEY_MISC_CITY_WLTKD_ENDED_KNOWN_RESOURCE");
					localizedText << getNameKey() << GC.getResourceInfo(GetResourceDemanded())->GetTextKey();
				}
				// Don't know what the next Demanded Resource is
				else
				{
					localizedText = Localization::Lookup("TXT_KEY_MISC_CITY_WLTKD_ENDED_UNKNOWN_RESOURCE");
					localizedText << getNameKey();
				}

				DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8());
			}
		}
	}
	else
	{
		if(eResource != NO_RESOURCE)
		{
			// Do we have the right Resource?
			if(GET_PLAYER(getOwner()).getNumResourceTotal(eResource) > 0)
			{
				SetWeLoveTheKingDayCounter(/*20*/ GC.getCITY_RESOURCE_WLTKD_TURNS());
#if defined(MOD_BALANCE_CORE)
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityBeginsWLTKD, getOwner(), getX(), getY(), GC.getCITY_RESOURCE_WLTKD_TURNS());
#endif

				CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
				if(pNotifications)
				{
#if defined(MOD_BALANCE_CORE)
					if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon() > 0)
					{
						Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD_UA_RESOURCE");
						strText << GC.getResourceInfo(eResource)->GetTextKey() << getNameKey();
						Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD_UA_RESOURCE");
						strSummary << getNameKey();
						pNotifications->Add(NOTIFICATION_REQUEST_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
					}
					else
					{
#endif
					Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD");
					strText << GC.getResourceInfo(eResource)->GetTextKey() << getNameKey();
					Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD");
					strSummary << getNameKey();
					pNotifications->Add(NOTIFICATION_REQUEST_RESOURCE, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), eResource);
#if defined(MOD_BALANCE_CORE)
					}
#endif
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
/// Figure out how long it should be before this City demands a Resource
void CvCity::DoSeedResourceDemandedCountdown()
{
	VALIDATE_OBJECT

	int iNumTurns = /*15*/ GC.getRESOURCE_DEMAND_COUNTDOWN_BASE();

	if(isCapital())
	{
		iNumTurns += /*25*/ GC.getRESOURCE_DEMAND_COUNTDOWN_CAPITAL_ADD();
	}

	int iRand = /*10*/ GC.getRESOURCE_DEMAND_COUNTDOWN_RAND();
#if defined(MOD_CORE_REDUCE_RANDOMNESS)
	iNumTurns += GC.getGame().getSmallFakeRandNum(iRand, getPopulation());
#else
	iNumTurns += GC.getGame().getJonRandNum(iRand, "City Resource demanded rand.");
#endif

	SetResourceDemandedCountdown(iNumTurns);
}

//	--------------------------------------------------------------------------------
/// How long before we pick a Resource to demand
int CvCity::GetResourceDemandedCountdown() const
{
	VALIDATE_OBJECT
	return m_iDemandResourceCounter;
}

//	--------------------------------------------------------------------------------
/// How long before we pick a Resource to demand
void CvCity::SetResourceDemandedCountdown(int iValue)
{
	VALIDATE_OBJECT
	m_iDemandResourceCounter = iValue;
}

//	--------------------------------------------------------------------------------
/// How long before we pick a Resource to demand
void CvCity::ChangeResourceDemandedCountdown(int iChange)
{
	VALIDATE_OBJECT
	SetResourceDemandedCountdown(GetResourceDemandedCountdown() + iChange);
}

//	--------------------------------------------------------------------------------
int CvCity::getFoodTurnsLeft() const
{
	VALIDATE_OBJECT
	int iFoodLeft;
	int iTurnsLeft;

	iFoodLeft = (growthThreshold() * 100 - getFoodTimes100());

	if(foodDifferenceTimes100() <= 0)
	{
		return iFoodLeft;
	}

	iTurnsLeft = (iFoodLeft / foodDifferenceTimes100());

	if((iTurnsLeft * foodDifferenceTimes100()) <  iFoodLeft)
	{
		iTurnsLeft++;
	}

	return std::max(1, iTurnsLeft);
}


//	--------------------------------------------------------------------------------
bool CvCity::isProduction() const
{
	VALIDATE_OBJECT
	return (headOrderQueueNode() != NULL);
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionLimited() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		{
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo((UnitTypes)(pOrderNode->iData1));
			if(pkUnitInfo)
			{
				return isLimitedUnitClass((UnitClassTypes)(pkUnitInfo->GetUnitClassType()));
			}
		}
		break;

		case ORDER_CONSTRUCT:
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo((BuildingTypes)pOrderNode->iData1);
			if(pkBuildingInfo)
			{
				return isLimitedWonderClass(pkBuildingInfo->GetBuildingClassInfo());
			}
		}
		break;

		case ORDER_CREATE:
			return isLimitedProject((ProjectTypes)(pOrderNode->iData1));
			break;

		case ORDER_PREPARE:
			break;

		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionUnit() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		return (pOrderNode->eOrderType == ORDER_TRAIN);
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionBuilding() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		return (pOrderNode->eOrderType == ORDER_CONSTRUCT);
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionProject() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		return (pOrderNode->eOrderType == ORDER_CREATE);
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionSpecialist() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		return (pOrderNode->eOrderType == ORDER_PREPARE);
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isProductionProcess() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		return (pOrderNode->eOrderType == ORDER_MAINTAIN);
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::canContinueProduction(OrderData order)
{
	VALIDATE_OBJECT
	switch(order.eOrderType)
	{
	case ORDER_TRAIN:
		return canTrain((UnitTypes)(order.iData1), true);
		break;

	case ORDER_CONSTRUCT:
		return canConstruct((BuildingTypes)(order.iData1), true);
		break;

	case ORDER_CREATE:
		return canCreate((ProjectTypes)(order.iData1), true);
		break;

	case ORDER_PREPARE:
		return canPrepare((SpecialistTypes)(order.iData1), true);
		break;

	case ORDER_MAINTAIN:
		return canMaintain((ProcessTypes)(order.iData1), true);
		break;

	default:
		CvAssertMsg(false, "order.eOrderType failed to match a valid option");
		break;
	}

	return false;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionExperience(UnitTypes eUnit)
{
	VALIDATE_OBJECT
	int iExperience;

	CvPlayerAI& kOwner = GET_PLAYER(getOwner());

	iExperience = getFreeExperience();
	iExperience += kOwner.getFreeExperience();

	if(eUnit != NO_UNIT)
	{
		CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
		if(pkUnitInfo)
		{
			if(pkUnitInfo->GetUnitCombatType() != NO_UNITCOMBAT)
			{
				iExperience += getUnitCombatFreeExperience((UnitCombatTypes)(pkUnitInfo->GetUnitCombatType()));
				iExperience += kOwner.getUnitCombatFreeExperiences((UnitCombatTypes) pkUnitInfo->GetUnitCombatType());
			}
			iExperience += getDomainFreeExperience((DomainTypes)(pkUnitInfo->GetDomainType()));
			iExperience += getDomainFreeExperienceFromGreatWorks((DomainTypes)(pkUnitInfo->GetDomainType()));
#if defined(MOD_BALANCE_CORE)
			iExperience += getDomainFreeExperienceFromGreatWorksGlobal((DomainTypes)(pkUnitInfo->GetDomainType()));
#endif

			iExperience += getSpecialistFreeExperience();
		}
	}

	return std::max(0, iExperience);
}


//	--------------------------------------------------------------------------------
void CvCity::addProductionExperience(CvUnit* pUnit, bool bConscript)
{
	VALIDATE_OBJECT

	if(pUnit->canAcquirePromotionAny())
	{
#if defined(MOD_UNITS_XP_TIMES_100)
		pUnit->changeExperienceTimes100(getProductionExperience(pUnit->getUnitType()) * 100 / ((bConscript) ? 2 : 1));
#else
		pUnit->changeExperience(getProductionExperience(pUnit->getUnitType()) / ((bConscript) ? 2 : 1));
#endif
		
#if !defined(NO_ACHIEVEMENTS)
		// XP2 Achievement
		if (getOwner() != NO_PLAYER)
		{
			CvPlayer& kOwner = GET_PLAYER(getOwner());
			if (!GC.getGame().isGameMultiPlayer() && kOwner.isHuman() && kOwner.isLocalPlayer())
			{
				// This unit begins with a promotion from XP, and part of that XP came from filled Great Work slots
#if defined(MOD_UNITS_XP_TIMES_100)
				if ((pUnit->getExperienceTimes100() / 100) >= pUnit->experienceNeeded() && getDomainFreeExperienceFromGreatWorks((DomainTypes)pUnit->getUnitInfo().GetDomainType()) > 0)
#else
				if (pUnit->getExperience() >= pUnit->experienceNeeded() && getDomainFreeExperienceFromGreatWorks((DomainTypes)pUnit->getUnitInfo().GetDomainType()) > 0)
#endif
				{
					// We have a Royal Library
					BuildingTypes eRoyalLibrary = (BuildingTypes) GC.getInfoTypeForString("BUILDING_ROYAL_LIBRARY", true);
					if (eRoyalLibrary != NO_BUILDING && GetCityBuildings()->GetNumBuilding(eRoyalLibrary) > 0)
					{
						gDLL->UnlockAchievement(ACHIEVEMENT_XP2_19);
					}
				}
			}
		}
#endif
	}

	for(int iI = 0; iI < GC.getNumPromotionInfos(); iI++)
	{
		const PromotionTypes ePromotion = static_cast<PromotionTypes>(iI);
		CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(ePromotion);
		if(pkPromotionInfo)
		{
			if(isFreePromotion(ePromotion))
			{
#if defined(MOD_BALANCE_CORE)
				if((pUnit->getUnitCombatType() != NO_UNITCOMBAT && pkPromotionInfo->GetUnitCombatClass(pUnit->getUnitCombatType())) || (::IsPromotionValidForCivilianUnitType(ePromotion, pUnit->getUnitType())))
#else
				if((pUnit->getUnitCombatType() != NO_UNITCOMBAT) && pkPromotionInfo->GetUnitCombatClass(pUnit->getUnitCombatType()))
#endif
				{
					pUnit->setHasPromotion(ePromotion, true);
				}
			}
		}
	}

	pUnit->testPromotionReady();
}


//	--------------------------------------------------------------------------------
UnitTypes CvCity::getProductionUnit() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return ((UnitTypes)(pOrderNode->iData1));
			break;

		case ORDER_CONSTRUCT:
		case ORDER_CREATE:
		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_UNIT;
}


//	--------------------------------------------------------------------------------
UnitAITypes CvCity::getProductionUnitAI() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return ((UnitAITypes)(pOrderNode->iData2));
			break;

		case ORDER_CONSTRUCT:
		case ORDER_CREATE:
		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_UNITAI;
}


//	--------------------------------------------------------------------------------
BuildingTypes CvCity::getProductionBuilding() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			break;

		case ORDER_CONSTRUCT:
			return ((BuildingTypes)(pOrderNode->iData1));
			break;

		case ORDER_CREATE:
		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_BUILDING;
}


//	--------------------------------------------------------------------------------
ProjectTypes CvCity::getProductionProject() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		case ORDER_CONSTRUCT:
			break;

		case ORDER_CREATE:
			return ((ProjectTypes)(pOrderNode->iData1));
			break;

		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_PROJECT;
}


//	--------------------------------------------------------------------------------
SpecialistTypes CvCity::getProductionSpecialist() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		case ORDER_CONSTRUCT:
		case ORDER_CREATE:
			break;

		case ORDER_PREPARE:
			return ((SpecialistTypes)(pOrderNode->iData1));
			break;

		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_SPECIALIST;
}

//	--------------------------------------------------------------------------------
ProcessTypes CvCity::getProductionProcess() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		case ORDER_CONSTRUCT:
		case ORDER_CREATE:
		case ORDER_PREPARE:
			break;

		case ORDER_MAINTAIN:
			return ((ProcessTypes)(pOrderNode->iData1));
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return NO_PROCESS;
}


//	--------------------------------------------------------------------------------
const char* CvCity::getProductionName() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		{
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo((UnitTypes)pOrderNode->iData1);
			if(pkUnitInfo)
			{
				return pkUnitInfo->GetDescription();
			}
		}
		break;

		case ORDER_CONSTRUCT:
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo((BuildingTypes)pOrderNode->iData1);
			if(pkBuildingInfo)
			{
				return pkBuildingInfo->GetDescription();
			}
		}
		break;

		case ORDER_CREATE:
		{
			CvProjectEntry* pkProjectInfo = GC.getProjectInfo((ProjectTypes)pOrderNode->iData1);
			if(pkProjectInfo)
			{
				return pkProjectInfo->GetDescription();
			}
		}
		break;

		case ORDER_PREPARE:
		{
			CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo((SpecialistTypes)pOrderNode->iData1);
			if(pkSpecialistInfo)
			{
				return pkSpecialistInfo->GetDescription();
			}
		}
		break;

		case ORDER_MAINTAIN:
		{
			CvProcessInfo* pkProcessInfo = GC.getProcessInfo((ProcessTypes)pOrderNode->iData1);
			if(pkProcessInfo)
			{
				return pkProcessInfo->GetDescription();
			}
		}
		break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return "";
}


//	--------------------------------------------------------------------------------
int CvCity::getGeneralProductionTurnsLeft() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return getProductionTurnsLeft((UnitTypes)pOrderNode->iData1, 0);
			break;

		case ORDER_CONSTRUCT:
			return getProductionTurnsLeft((BuildingTypes)pOrderNode->iData1, 0);
			break;

		case ORDER_CREATE:
			return getProductionTurnsLeft((ProjectTypes)pOrderNode->iData1, 0);
			break;

		case ORDER_PREPARE:
			return getProductionTurnsLeft((SpecialistTypes)pOrderNode->iData1, 0);
			break;

		case ORDER_MAINTAIN:
#if defined(MOD_PROCESS_STOCKPILE)
			return getProductionTurnsLeft((ProcessTypes)pOrderNode->iData1, 0);
#else
			return 0;
#endif
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return 0;
}


//	--------------------------------------------------------------------------------
const char* CvCity::getProductionNameKey() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
		{
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo((UnitTypes)pOrderNode->iData1);
			if(pkUnitInfo)
			{
				return pkUnitInfo->GetTextKey();
			}
		}
		break;

		case ORDER_CONSTRUCT:
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo((BuildingTypes)pOrderNode->iData1);
			if(pkBuildingInfo)
			{
				return pkBuildingInfo->GetTextKey();
			}
		}
		break;

		case ORDER_CREATE:
		{
			CvProjectEntry* pkProjectInfo = GC.getProjectInfo((ProjectTypes)pOrderNode->iData1);
			if(pkProjectInfo)
			{
				return pkProjectInfo->GetTextKey();
			}
		}
		break;

		case ORDER_PREPARE:
		{
			CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo((SpecialistTypes)pOrderNode->iData1);
			if(pkSpecialistInfo)
			{
				return pkSpecialistInfo->GetTextKey();
			}
		}
		break;

		case ORDER_MAINTAIN:
		{
			CvProcessInfo* pkProcessInfo = GC.getProcessInfo((ProcessTypes)pOrderNode->iData1);
			if(pkProcessInfo)
			{
				return pkProcessInfo->GetTextKey();
			}
		}
		break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return "";
}


//	--------------------------------------------------------------------------------
bool CvCity::isFoodProduction() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return isFoodProduction((UnitTypes)(pOrderNode->iData1));
			break;

		case ORDER_CONSTRUCT:
		case ORDER_CREATE:
		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return false;
}


//	--------------------------------------------------------------------------------
bool CvCity::isFoodProduction(UnitTypes eUnit) const
{
	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo == NULL)
		return false;

	if(pkUnitInfo->IsFoodProduction())
	{
		return true;
	}

	if(GET_PLAYER(getOwner()).isMilitaryFoodProduction())
	{
		if(pkUnitInfo->IsMilitaryProduction())
		{
			return true;
		}
	}

	return false;
}


//	--------------------------------------------------------------------------------
int CvCity::getFirstUnitOrder(UnitTypes eUnit) const
{
	VALIDATE_OBJECT
	int iCount = 0;

	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_TRAIN)
		{
			if(pOrderNode->iData1 == eUnit)
			{
				return iCount;
			}
		}

		iCount++;

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	return -1;
}


//	--------------------------------------------------------------------------------
int CvCity::getFirstBuildingOrder(BuildingTypes eBuilding) const
{
	VALIDATE_OBJECT
	int iCount = 0;

	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_CONSTRUCT)
		{
			if(pOrderNode->iData1 == eBuilding)
			{
				return iCount;
			}
		}

		iCount++;

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	return -1;
}


//	--------------------------------------------------------------------------------
int CvCity::getFirstProjectOrder(ProjectTypes eProject) const
{
	VALIDATE_OBJECT
	int iCount = 0;

	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_CREATE)
		{
			if(pOrderNode->iData1 == eProject)
			{
				return iCount;
			}
		}

		iCount++;

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	return -1;
}


//	--------------------------------------------------------------------------------
int CvCity::getFirstSpecialistOrder(SpecialistTypes eSpecialist) const
{
	VALIDATE_OBJECT
	int iCount = 0;

	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_PREPARE)
		{
			if(pOrderNode->iData1 == eSpecialist)
			{
				return iCount;
			}
		}

		iCount++;

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	return -1;
}

//	--------------------------------------------------------------------------------
int CvCity::getNumTrainUnitAI(UnitAITypes eUnitAI) const
{
	VALIDATE_OBJECT
	int iCount = 0;

	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_TRAIN)
		{
			if(pOrderNode->iData2 == eUnitAI)
			{
				iCount++;
			}
		}

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	return iCount;
}


//	--------------------------------------------------------------------------------
int CvCity::getProduction() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return getUnitProduction((UnitTypes)(pOrderNode->iData1));
			break;

		case ORDER_CONSTRUCT:
			return m_pCityBuildings->GetBuildingProduction((BuildingTypes)(pOrderNode->iData1));
			break;

		case ORDER_CREATE:
			return getProjectProduction((ProjectTypes)(pOrderNode->iData1));
			break;

		case ORDER_PREPARE:
			return getSpecialistProduction((SpecialistTypes)(pOrderNode->iData1));
			break;

		case ORDER_MAINTAIN:
#if defined(MOD_PROCESS_STOCKPILE)
			return getProcessProduction((ProcessTypes)(pOrderNode->iData1));
#endif
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return 0;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionTimes100() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return getUnitProductionTimes100((UnitTypes)(pOrderNode->iData1));
			break;

		case ORDER_CONSTRUCT:
			return m_pCityBuildings->GetBuildingProductionTimes100((BuildingTypes)(pOrderNode->iData1));
			break;

		case ORDER_CREATE:
			return getProjectProductionTimes100((ProjectTypes)(pOrderNode->iData1));
			break;

		case ORDER_PREPARE:
			return getSpecialistProductionTimes100((SpecialistTypes)(pOrderNode->iData1));
			break;

		case ORDER_MAINTAIN:
#if defined(MOD_PROCESS_STOCKPILE)
			return getProcessProductionTimes100((ProcessTypes)(pOrderNode->iData1));
#endif
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return 0;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return getProductionNeeded((UnitTypes)(pOrderNode->iData1));
			break;

		case ORDER_CONSTRUCT:
			return getProductionNeeded((BuildingTypes)(pOrderNode->iData1));
			break;

		case ORDER_CREATE:
			return getProductionNeeded((ProjectTypes)(pOrderNode->iData1));
			break;

		case ORDER_PREPARE:
			return getProductionNeeded((SpecialistTypes)(pOrderNode->iData1));
			break;

		case ORDER_MAINTAIN:
#if defined(MOD_PROCESS_STOCKPILE)
			return getProductionNeeded((ProcessTypes)(pOrderNode->iData1));
#endif
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return INT_MAX;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded(UnitTypes eUnit) const
{
	VALIDATE_OBJECT
	int iNumProductionNeeded = GET_PLAYER(getOwner()).getProductionNeeded(eUnit);
#if defined(MOD_BALANCE_CORE_UNIT_INVESTMENTS)
	if(MOD_BALANCE_CORE_UNIT_INVESTMENTS && eUnit != NO_UNIT)
	{
		CvUnitEntry* pGameUnit = GC.getUnitInfo(eUnit);
		const UnitClassTypes eUnitClass = (UnitClassTypes)(pGameUnit->GetUnitClassType());
		if(IsUnitInvestment(eUnitClass))
		{
			int iTotalDiscount = (/*-50*/ GC.getBALANCE_UNIT_INVESTMENT_BASELINE() + GET_PLAYER(getOwner()).GetPlayerTraits()->GetInvestmentModifier() + GET_PLAYER(getOwner()).GetInvestmentModifier());
			iNumProductionNeeded *= (iTotalDiscount + 100);
			iNumProductionNeeded /= 100;
		}
	}
#endif

	return iNumProductionNeeded;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded(BuildingTypes eBuilding) const
{
	VALIDATE_OBJECT
	int iNumProductionNeeded = GET_PLAYER(getOwner()).getProductionNeeded(eBuilding);
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
	if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS && eBuilding != NO_BUILDING)
	{
		CvBuildingEntry* pGameBuilding = GC.getBuildingInfo(eBuilding);
		if(pGameBuilding)
		{
			const BuildingClassTypes eBuildingClass = (BuildingClassTypes)(pGameBuilding->GetBuildingClassType());
			if(eBuildingClass != NO_BUILDINGCLASS && IsBuildingInvestment(eBuildingClass))
			{
				int iTotalDiscount = (/*-50*/ GC.getBALANCE_BUILDING_INVESTMENT_BASELINE() + GET_PLAYER(getOwner()).GetPlayerTraits()->GetInvestmentModifier() + GET_PLAYER(getOwner()).GetInvestmentModifier());
				const CvBuildingClassInfo& kBuildingClassInfo = pGameBuilding->GetBuildingClassInfo();
				if(::isWorldWonderClass(kBuildingClassInfo))

				{
					iTotalDiscount /= 2;
				}
				iNumProductionNeeded *= (iTotalDiscount + 100);
				iNumProductionNeeded /= 100;
			}
		}
	}
#endif

	return iNumProductionNeeded;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded(ProjectTypes eProject) const
{
	VALIDATE_OBJECT
	int iNumProductionNeeded = GET_PLAYER(getOwner()).getProductionNeeded(eProject);

	return iNumProductionNeeded;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded(SpecialistTypes eSpecialist) const
{
	VALIDATE_OBJECT
	int iNumProductionNeeded = GET_PLAYER(getOwner()).getProductionNeeded(eSpecialist);

	return iNumProductionNeeded;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft() const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			return getProductionTurnsLeft(((UnitTypes)(pOrderNode->iData1)), 0);
			break;

		case ORDER_CONSTRUCT:
			return getProductionTurnsLeft(((BuildingTypes)(pOrderNode->iData1)), 0);
			break;

		case ORDER_CREATE:
			return getProductionTurnsLeft(((ProjectTypes)(pOrderNode->iData1)), 0);
			break;

		case ORDER_PREPARE:
			return getProductionTurnsLeft(((SpecialistTypes)(pOrderNode->iData1)), 0);
			break;

		case ORDER_MAINTAIN:
#if defined(MOD_PROCESS_STOCKPILE)
			return getProductionTurnsLeft(((ProcessTypes)(pOrderNode->iData1)), 0);
#endif
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return INT_MAX;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(UnitTypes eUnit, int iNum) const
{
	VALIDATE_OBJECT
	int iProduction;
	int iFirstUnitOrder;
	int iProductionNeeded;
	int iProductionModifier;

	iProduction = 0;

	iFirstUnitOrder = getFirstUnitOrder(eUnit);

	if((iFirstUnitOrder == -1) || (iFirstUnitOrder == iNum))
	{
		iProduction += getUnitProductionTimes100(eUnit);
	}

	iProductionNeeded = getProductionNeeded(eUnit) * 100;
	iProductionModifier = getProductionModifier(eUnit);

	return getProductionTurnsLeft(iProductionNeeded, iProduction, getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, isFoodProduction(eUnit), (iNum == 0)), getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, isFoodProduction(eUnit), false));
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(BuildingTypes eBuilding, int iNum) const
{
	VALIDATE_OBJECT
	int iProduction;
	int iFirstBuildingOrder;
	int iProductionNeeded;
	int iProductionModifier;

	iProduction = 0;

	iFirstBuildingOrder = getFirstBuildingOrder(eBuilding);

	if((iFirstBuildingOrder == -1) || (iFirstBuildingOrder == iNum))
	{
		iProduction += m_pCityBuildings->GetBuildingProductionTimes100(eBuilding);
	}

	iProductionNeeded = getProductionNeeded(eBuilding) * 100;

	iProductionModifier = getProductionModifier(eBuilding);

	return getProductionTurnsLeft(iProductionNeeded, iProduction, getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, (iNum == 0)), getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, false));
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(ProjectTypes eProject, int iNum) const
{
	VALIDATE_OBJECT
	int iProduction;
	int iFirstProjectOrder;
	int iProductionNeeded;
	int iProductionModifier;

	iProduction = 0;

	iFirstProjectOrder = getFirstProjectOrder(eProject);

	if((iFirstProjectOrder == -1) || (iFirstProjectOrder == iNum))
	{
		iProduction += getProjectProductionTimes100(eProject);
	}

	iProductionNeeded = getProductionNeeded(eProject) * 100;
	iProductionModifier = getProductionModifier(eProject);

	return getProductionTurnsLeft(iProductionNeeded, iProduction, getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, (iNum == 0)), getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, false));
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(SpecialistTypes eSpecialist, int iNum) const
{
	VALIDATE_OBJECT
	int iProduction;
	int iFirstSpecialistOrder;
	int iProductionNeeded;
	int iProductionModifier;

	iProduction = 0;

	iFirstSpecialistOrder = getFirstSpecialistOrder(eSpecialist);

	if((iFirstSpecialistOrder == -1) || (iFirstSpecialistOrder == iNum))
	{
		iProduction += getSpecialistProductionTimes100(eSpecialist);
	}

	iProductionNeeded = getProductionNeeded(eSpecialist) * 100;
	iProductionModifier = getProductionModifier(eSpecialist);

	return getProductionTurnsLeft(iProductionNeeded, iProduction, getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, (iNum == 0)), getProductionDifferenceTimes100(iProductionNeeded, iProduction, iProductionModifier, false, false));
}

#if defined(MOD_PROCESS_STOCKPILE)
//	--------------------------------------------------------------------------------
int CvCity::getProductionNeeded(ProcessTypes eProcess) const
{
	VALIDATE_OBJECT

	if (eProcess == GC.getInfoTypeForString("PROCESS_STOCKPILE")) {
		return GET_PLAYER(getOwner()).getMaxStockpile();
	}

	return INT_MAX;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(ProcessTypes eProcess, int) const
{
	VALIDATE_OBJECT

	if (eProcess == GC.getInfoTypeForString("PROCESS_STOCKPILE")) {
		int iProduction = getOverflowProduction();
		int iProductionNeeded = GET_PLAYER(getOwner()).getMaxStockpile();
		int iProductionModifier = getProductionModifier(eProcess);
		int iProductionDifference = getProductionDifference(iProductionNeeded, iProduction, iProductionModifier, false, false);

		return getProductionTurnsLeft(iProductionNeeded, iProduction, iProductionDifference, iProductionDifference);
	}

	return INT_MAX;
}
#endif
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
bool CvCity::IsBuildingInvestment(BuildingClassTypes eBuildingClass) const
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	return m_abBuildingInvestment[eBuildingClass];
}
//	--------------------------------------------------------------------------------
void CvCity::SetBuildingInvestment(BuildingClassTypes eBuildingClass, bool bNewValue)
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	if(bNewValue)
	{
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityInvestedBuilding, getOwner(), GetID(), eBuildingClass, bNewValue);
	}

	m_abBuildingInvestment.setAt(eBuildingClass, bNewValue);
}

//	--------------------------------------------------------------------------------
bool CvCity::IsUnitInvestment(UnitClassTypes eUnitClass) const
{
	FAssert(eUnitClass >= 0);
	FAssert(eUnitClass < GC.getNumUnitClassInfos());

	return m_abUnitInvestment[eUnitClass];
}
//	--------------------------------------------------------------------------------
void CvCity::SetUnitInvestment(UnitClassTypes eUnitClass, bool bNewValue)
{
	FAssert(eUnitClass >= 0);
	FAssert(eUnitClass < GC.getNumUnitClassInfos());

	if(bNewValue)
	{
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityInvestedUnit, getOwner(), GetID(), eUnitClass, bNewValue);
	}

	m_abUnitInvestment.setAt(eUnitClass, bNewValue);
}

//	--------------------------------------------------------------------------------
bool CvCity::IsBuildingConstructed(BuildingClassTypes eBuildingClass) const
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	return m_abBuildingConstructed[eBuildingClass];
}
//	--------------------------------------------------------------------------------
void CvCity::SetBuildingConstructed(BuildingClassTypes eBuildingClass, bool bNewValue)
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	m_abBuildingConstructed.setAt(eBuildingClass, bNewValue);
}	
#endif
//	--------------------------------------------------------------------------------
int CvCity::getProductionTurnsLeft(int iProductionNeeded, int iProduction, int iFirstProductionDifference, int iProductionDifference) const
{
	VALIDATE_OBJECT
	int iProductionLeft;
	int iTurnsLeft;

	iProductionLeft = std::max(0, (iProductionNeeded - iProduction - iFirstProductionDifference));

	if(iProductionDifference == 0)
	{
		return iProductionLeft + 1;
	}

	iTurnsLeft = (iProductionLeft / iProductionDifference);

	if((iTurnsLeft * iProductionDifference) < iProductionLeft)
	{
		iTurnsLeft++;
	}

	iTurnsLeft++;

	return std::max(1, iTurnsLeft);
}

//	--------------------------------------------------------------------------------
int CvCity::GetPurchaseCost(UnitTypes eUnit)
{
	VALIDATE_OBJECT

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo == NULL)
	{
		//Should never happen
		return 0;
	}

	int iModifier = pkUnitInfo->GetHurryCostModifier();

	bool bIsSpaceshipPart = pkUnitInfo->GetSpaceshipProject() != NO_PROJECT;

	if (iModifier == -1 && (!bIsSpaceshipPart || !GET_PLAYER(getOwner()).IsEnablesSSPartPurchase()))
	{
		return -1;
	}

	int iCost = GetPurchaseCostFromProduction(getProductionNeeded(eUnit));
	iCost *= (100 + iModifier);
	iCost /= 100;

	// Cost of purchasing units modified?
	iCost *= (100 + GET_PLAYER(getOwner()).GetUnitPurchaseCostModifier());
	iCost /= 100;

#if defined(MOD_DIPLOMACY_CITYSTATES_RESOLUTIONS)
	if (MOD_DIPLOMACY_CITYSTATES_RESOLUTIONS) {
		int iLimitSpaceshipPurchase = GC.getGame().GetGameLeagues()->GetSpaceShipPurchaseMod(getOwner());
		if(bIsSpaceshipPart && iLimitSpaceshipPurchase != 0)
		{
			iCost *= (100 + GC.getGame().GetGameLeagues()->GetSpaceShipPurchaseMod(getOwner()));
			iCost /= 100;
		}
	}
#endif

#if defined(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	if(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	{
		//Increase cost based on # of techs researched.
		int iTechProgress = (GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100) / GC.getNumTechInfos();
		iTechProgress /= 2;
		if(iTechProgress > 0)
		{
			iCost *= (100 + iTechProgress);
			iCost /= 100;
		}
	}
#endif

	// Make the number not be funky
	int iDivisor = /*10*/ GC.getGOLD_PURCHASE_VISIBLE_DIVISOR();
	iCost /= iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
int CvCity::GetFaithPurchaseCost(UnitTypes eUnit, bool bIncludeBeliefDiscounts)
{
	VALIDATE_OBJECT

	int iCost = 0;
	CvPlayer &kPlayer = GET_PLAYER(m_eOwner);

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo == NULL)
	{
		//Should never happen
		return iCost;
	}

	// LATE-GAME GREAT PERSON
	SpecialUnitTypes eSpecialUnitGreatPerson = (SpecialUnitTypes) GC.getInfoTypeForString("SPECIALUNIT_PEOPLE");
	if (pkUnitInfo->GetSpecialUnitType() == eSpecialUnitGreatPerson)
	{
		// We must be into the industrial era
#if defined(MOD_CONFIG_GAME_IN_XML)
		if(kPlayer.GetCurrentEra() >= GD_INT_GET(RELIGION_GP_FAITH_PURCHASE_ERA))
#else
		if(kPlayer.GetCurrentEra() >= GC.getInfoTypeForString("ERA_INDUSTRIAL", true /*bHideAssert*/))
#endif
		{
			// Must be proper great person for our civ
			const UnitClassTypes eUnitClass = (UnitClassTypes)pkUnitInfo->GetUnitClassType();
			if (eUnitClass != NO_UNITCLASS)
			{
				const UnitTypes eThisPlayersUnitType = (UnitTypes)kPlayer.getCivilizationInfo().getCivilizationUnits(eUnitClass);
				ReligionTypes eReligion = kPlayer.GetReligions()->GetReligionCreatedByPlayer();

				if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_PROPHET", true /*bHideAssert*/)) //here
				{
					// Can't be bought if didn't start religion
					if (eReligion == NO_RELIGION)
					{
						iCost = -1;
					}
					else
					{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
						iCost = kPlayer.GetReligions()->GetCostNextProphet(true /*bIncludeBeliefDiscounts*/, false /*bAdjustForSpeedDifficulty*/, MOD_GLOBAL_TRULY_FREE_GP);
#else
						iCost = kPlayer.GetReligions()->GetCostNextProphet(true /*bIncludeBeliefDiscounts*/, false /*bAdjustForSpeedDifficulty*/);
#endif
					}
				}
				else if (eThisPlayersUnitType == eUnit)
				{
					PolicyBranchTypes eBranch = NO_POLICY_BRANCH_TYPE;
					int iNum = 0;

					// Check social policy tree
					if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_WRITER", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_AESTHETICS", true /*bHideAssert*/);
						iNum = kPlayer.getWritersFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_ARTIST", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_AESTHETICS", true /*bHideAssert*/);
						iNum = kPlayer.getArtistsFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_MUSICIAN", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_AESTHETICS", true /*bHideAssert*/);
						iNum = kPlayer.getMusiciansFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_SCIENTIST", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_RATIONALISM", true /*bHideAssert*/);
						iNum = kPlayer.getScientistsFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_MERCHANT", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_COMMERCE", true /*bHideAssert*/);
						iNum = kPlayer.getMerchantsFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_ENGINEER", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_TRADITION", true /*bHideAssert*/);
						iNum = kPlayer.getEngineersFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_GENERAL", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_HONOR", true /*bHideAssert*/);
						iNum = kPlayer.getGeneralsFromFaith();
					}
					else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_ADMIRAL", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_EXPLORATION", true /*bHideAssert*/);
						iNum = kPlayer.getAdmiralsFromFaith();
					}
#if defined(MOD_DIPLOMACY_CITYSTATES)
					else if (MOD_DIPLOMACY_CITYSTATES && eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_DIPLOMAT", true /*bHideAssert*/))
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getDiplomatsFromFaith();
					}
#endif
#if defined(MOD_BALANCE_CORE)
					if(pkUnitInfo->IsGPExtra() == 1)
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getGPExtra1FromFaith();
					}
					else if(pkUnitInfo->IsGPExtra() == 2)
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getGPExtra2FromFaith();
					}

					else if(pkUnitInfo->IsGPExtra() == 3)
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getGPExtra3FromFaith();
					}

					else if(pkUnitInfo->IsGPExtra() == 4)
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getGPExtra4FromFaith();
					}

					else if(pkUnitInfo->IsGPExtra() == 5)
					{
						eBranch = (PolicyBranchTypes)GC.getInfoTypeForString("POLICY_BRANCH_PATRONAGE", true /*bHideAssert*/);
						iNum = kPlayer.getGPExtra5FromFaith();
					}
#endif

					bool bAllUnlockedByBelief = false;
					const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligion, getOwner());
					if(pReligion)
					{	
						if (pReligion->m_Beliefs.IsFaithPurchaseAllGreatPeople(getOwner()))
						{
							bAllUnlockedByBelief = true;
						}
					}

#if defined(MOD_RELIGION_POLICY_BRANCH_FAITH_GP)
					if (MOD_RELIGION_POLICY_BRANCH_FAITH_GP)
					{
						bool bIsUnlocked = bAllUnlockedByBelief;

						if (!bIsUnlocked)
						{
							EraTypes eCurrentEra = kPlayer.GetCurrentEra();

							for (int iPolicyLoop = 0; iPolicyLoop < kPlayer.GetPlayerPolicies()->GetPolicies()->GetNumPolicies(); iPolicyLoop++)
							{
								const PolicyTypes eLoopPolicy = static_cast<PolicyTypes>(iPolicyLoop);
								CvPolicyEntry* pkLoopPolicyInfo = GC.getPolicyInfo(eLoopPolicy);
								if (pkLoopPolicyInfo)
								{
									// We have this policy
									if (kPlayer.HasPolicy(eLoopPolicy))
									{
										if (pkLoopPolicyInfo->IsFaithPurchaseUnitClass(eUnitClass, eCurrentEra))
										{
											bIsUnlocked = true;
											break;
										}
									}
								}
							}
						}

						if (bIsUnlocked)
						{
							iCost = GC.getGame().GetGameReligions()->GetFaithGreatPersonNumber(iNum + 1);	
						}
					}
					else
					{
#endif
					
						if (bAllUnlockedByBelief || (eBranch != NO_POLICY_BRANCH_TYPE && kPlayer.GetPlayerPolicies()->IsPolicyBranchFinished(eBranch) && !kPlayer.GetPlayerPolicies()->IsPolicyBranchBlocked(eBranch)))
						{
							iCost = GC.getGame().GetGameReligions()->GetFaithGreatPersonNumber(iNum + 1);	
						}
#if defined(MOD_RELIGION_POLICY_BRANCH_FAITH_GP)
					}
#endif
				}
			}
		}
	}

	// ALL OTHERS
	else
	{
		// Cost goes up in later eras
		iCost = pkUnitInfo->GetFaithCost();
		EraTypes eEra = GET_TEAM(GET_PLAYER(getOwner()).getTeam()).GetCurrentEra();
		int iMultiplier = GC.getEraInfo(eEra)->getFaithCostMultiplier();
		iCost = iCost * iMultiplier / 100;

		if (pkUnitInfo->IsSpreadReligion() || pkUnitInfo->IsRemoveHeresy())
		{
			iMultiplier = (100 + GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_FAITH_COST_MODIFIER));
			iCost = iCost * iMultiplier / 100;
		}
	}

	// Adjust for game speed
	iCost *= GC.getGame().getGameSpeedInfo().getTrainPercent();
	iCost /= 100;

	// Adjust for difficulty
	if(!isHuman() && !GET_PLAYER(getOwner()).IsAITeammateOfHuman() && !isBarbarian())
	{
		iCost *= GC.getGame().getHandicapInfo().getAITrainPercent();
		iCost /= 100;
	}

	// Modify by any beliefs
	if(bIncludeBeliefDiscounts && (pkUnitInfo->IsSpreadReligion() || pkUnitInfo->IsRemoveHeresy()) && !pkUnitInfo->IsFoundReligion())
	{
		CvGameReligions* pReligions = GC.getGame().GetGameReligions();
		ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
		if(eMajority > RELIGION_PANTHEON)
		{
			const CvReligion* pReligion = pReligions->GetReligion(eMajority, getOwner());
			if(pReligion)
			{
				int iReligionCostMod = pReligion->m_Beliefs.GetMissionaryCostModifier(getOwner());

				if(iReligionCostMod != 0)
				{
					iCost *= (100 + iReligionCostMod);
					iCost /= 100;
				}
			}
		}
	}
#if defined(MOD_DIPLOMACY_CITYSTATES_RESOLUTIONS)
	if (MOD_DIPLOMACY_CITYSTATES_RESOLUTIONS) {
		//Modify for Resolution
		int iGetSpaceShipPurchaseMod = GC.getGame().GetGameLeagues()->GetSpaceShipPurchaseMod(getOwner());
		if((pkUnitInfo->GetBaseHurry() > 0) && (iGetSpaceShipPurchaseMod != 0))
		{
			iCost *= (100 + iGetSpaceShipPurchaseMod);
			iCost /= 100;
		}
	}
#endif
#if defined(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	if(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	{
		//Increase cost based on # of techs researched.
		int iTechProgress = (GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100) / GC.getNumTechInfos();
		iTechProgress /= 3;
		if(iTechProgress > 0)
		{
			iCost *= (100 + iTechProgress);
			iCost /= 100;
		}
	}
#endif
	// Make the number not be funky
	int iDivisor = /*10*/ GC.getGOLD_PURCHASE_VISIBLE_DIVISOR();
	iCost /= iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
int CvCity::GetPurchaseCost(BuildingTypes eBuilding)
{
	VALIDATE_OBJECT

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return -1;

	int iModifier = pkBuildingInfo->GetHurryCostModifier();

	if(iModifier == -1)
		return -1;

#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
	if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS && (NO_BUILDING != eBuilding))
	{
		//Have we already invested here?
		CvBuildingEntry* pGameBuilding = GC.getBuildingInfo(eBuilding);
		const BuildingClassTypes eBuildingClass = (BuildingClassTypes)(pGameBuilding->GetBuildingClassType());
		if(IsBuildingInvestment(eBuildingClass))
		{
			return -1;
		}
	}
#endif
#if defined(MOD_BUILDINGS_PRO_RATA_PURCHASE)
	int iProductionNeeded = getProductionNeeded(eBuilding);

	if (MOD_BUILDINGS_PRO_RATA_PURCHASE) {
		// Deduct any current production towards this building
		int iProductionToDate = m_pCityBuildings->GetBuildingProduction(eBuilding);
		iProductionNeeded -= (iProductionToDate * gCustomMods.getOption("BUILDINGS_PRO_RATA_PURCHASE_DEPRECIATION", 80)) / 100;
	}
	
	int iCost = GetPurchaseCostFromProduction(iProductionNeeded);
#else
	int iCost = GetPurchaseCostFromProduction(getProductionNeeded(eBuilding));
#endif
	iCost *= (100 + iModifier);
	iCost /= 100;

	// Cost of purchasing buildings modified?
	iCost *= (100 + GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_BUILDING_PURCHASE_COST_MODIFIER));
	iCost /= 100;

#if defined(MOD_BALANCE_CORE)
	if(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	{
		//Increase cost based on # of techs researched.
		int iTechProgress = (GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100) / GC.getNumTechInfos();
		iTechProgress /= 2;
		if(iTechProgress > 0)
		{
			iCost *= (100 + iTechProgress);
			iCost /= 100;
		}
	}
	if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
	{
		//Decrease base cost, then increase based on # of cities in empire.
		iCost /= 2;
		iCost *= (100 + GET_PLAYER(getOwner()).getNumCities() * 10);
		iCost /= 100;
	}
#endif

	// Make the number not be funky
	int iDivisor = /*10*/ GC.getGOLD_PURCHASE_VISIBLE_DIVISOR();
	iCost /= iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
int CvCity::GetFaithPurchaseCost(BuildingTypes eBuilding)
{
	int iCost;

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
	{
		//Should never happen
		return 0;
	}

	// Cost goes up in later eras
	iCost = pkBuildingInfo->GetFaithCost();
	EraTypes eEra = GET_TEAM(GET_PLAYER(getOwner()).getTeam()).GetCurrentEra();
	int iMultiplier = GC.getEraInfo(eEra)->getFaithCostMultiplier();
	iCost = iCost * iMultiplier / 100;
	iMultiplier = (100 + GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_FAITH_COST_MODIFIER));
	iCost = iCost * iMultiplier / 100;

	// Adjust for game speed
	iCost *= GC.getGame().getGameSpeedInfo().getConstructPercent();
	iCost /= 100;

	// Adjust for difficulty
	if(!isHuman() && !GET_PLAYER(getOwner()).IsAITeammateOfHuman() && !isBarbarian())
	{
		iCost *= GC.getGame().getHandicapInfo().getAIConstructPercent();
		iCost /= 100;
	}
#if defined(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	if(MOD_BALANCE_CORE_PURCHASE_COST_INCREASE)
	{
		//Increase cost based on # of techs researched.
		int iTechProgress = (GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100) / GC.getNumTechInfos();
		iTechProgress /= 2;
		if(iTechProgress > 0)
		{
			iCost *= (100 + iTechProgress);
			iCost /= 100;
		}
	}
#endif

	// Make the number not be funky
	int iDivisor = /*10*/ GC.getGOLD_PURCHASE_VISIBLE_DIVISOR();
	iCost /= iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
int CvCity::GetPurchaseCost(ProjectTypes eProject)
{
	VALIDATE_OBJECT

	int iCost = GetPurchaseCostFromProduction(getProductionNeeded(eProject));

	// Make the number not be funky
	int iDivisor = /*10*/ GC.getGOLD_PURCHASE_VISIBLE_DIVISOR();
	iCost /= iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
/// Cost of Purchasing something based on the amount of Production it requires to construct
int CvCity::GetPurchaseCostFromProduction(int iProduction)
{
	VALIDATE_OBJECT
	int iPurchaseCost;

	// Gold per Production
	int iPurchaseCostBase = iProduction* /*30*/ GC.getGOLD_PURCHASE_GOLD_PER_PRODUCTION();
	// Cost ramps up
	iPurchaseCost = (int) pow((double) iPurchaseCostBase, (double) /*0.75f*/ GC.getHURRY_GOLD_PRODUCTION_EXPONENT());

	// Hurry Mod (Policies, etc.)
	HurryTypes eHurry = (HurryTypes) GC.getInfoTypeForString("HURRY_GOLD");

	if(eHurry != NO_HURRY)
	{
		int iHurryMod = GET_PLAYER(getOwner()).getHurryModifier(eHurry);

		if(iHurryMod != 0)
		{
			iPurchaseCost *= (100 + iHurryMod);
			iPurchaseCost /= 100;
		}
#if defined(MOD_BALANCE_CORE)
		if(getHurryModifier(eHurry) != 0)
		{
			iPurchaseCost *= (100 + getHurryModifier(eHurry));
			iPurchaseCost /= 100;
		}
#endif
	}

	// Game Speed modifier
	iPurchaseCost *= GC.getGame().getGameSpeedInfo().getHurryPercent();
	iPurchaseCost /= 100;

	return iPurchaseCost;
}

//	--------------------------------------------------------------------------------
void CvCity::setProduction(int iNewValue)
{
	VALIDATE_OBJECT
	if(isProductionUnit())
	{
		setUnitProduction(getProductionUnit(), iNewValue);
	}
	else if(isProductionBuilding())
	{
		m_pCityBuildings->SetBuildingProduction(getProductionBuilding(), iNewValue);
	}
	else if(isProductionProject())
	{
		setProjectProduction(getProductionProject(), iNewValue);
	}
	else if(isProductionSpecialist())
	{
		setSpecialistProduction(getProductionSpecialist(), iNewValue);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeProduction(int iChange)
{
	VALIDATE_OBJECT
	if(isProductionUnit())
	{
		changeUnitProduction(getProductionUnit(), iChange);
	}
	else if(isProductionBuilding())
	{
		m_pCityBuildings->ChangeBuildingProduction(getProductionBuilding(), iChange);
	}
	else if(isProductionProject())
	{
		changeProjectProduction(getProductionProject(), iChange);
	}
	else if(isProductionSpecialist())
	{
		changeSpecialistProduction(getProductionSpecialist(), iChange);
	}
#if defined(MOD_BALANCE_CORE)
	else
	{
		changeOverflowProduction(iChange);
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strLogString;
			strLogString.Format("MOD - overflow production from policy/belief/trait/ruin/etc added: %d in %s.",
				iChange, getName().GetCString());
			GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
		}
	}
#endif
}


//	--------------------------------------------------------------------------------
void CvCity::setProductionTimes100(int iNewValue)
{
	VALIDATE_OBJECT
	if(isProductionUnit())
	{
		setUnitProductionTimes100(getProductionUnit(), iNewValue);
	}
	else if(isProductionBuilding())
	{
		m_pCityBuildings->SetBuildingProductionTimes100(getProductionBuilding(), iNewValue);
	}
	else if(isProductionProject())
	{
		setProjectProductionTimes100(getProductionProject(), iNewValue);
	}
	else if(isProductionSpecialist())
	{
		setSpecialistProductionTimes100(getProductionSpecialist(), iNewValue);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeProductionTimes100(int iChange)
{
	VALIDATE_OBJECT
	if(isProductionUnit())
	{
		changeUnitProductionTimes100(getProductionUnit(), iChange);
	}
	else if(isProductionBuilding())
	{
		m_pCityBuildings->ChangeBuildingProductionTimes100(getProductionBuilding(), iChange);
	}
	else if(isProductionProject())
	{
		changeProjectProductionTimes100(getProductionProject(), iChange);
	}
	else if(isProductionSpecialist())
	{
		changeSpecialistProductionTimes100(getProductionSpecialist(), iChange);
	}
	else if(isProductionProcess())
	{
		doProcess();
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	const OrderData* pOrderNode = headOrderQueueNode();

	int iMultiplier = 0;

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			iMultiplier += getProductionModifier((UnitTypes)(pOrderNode->iData1), toolTipSink);
			break;

		case ORDER_CONSTRUCT:
			iMultiplier += getProductionModifier((BuildingTypes)(pOrderNode->iData1), toolTipSink);
			break;

		case ORDER_CREATE:
			iMultiplier += getProductionModifier((ProjectTypes)(pOrderNode->iData1), toolTipSink);
			break;

		case ORDER_PREPARE:
			iMultiplier += getProductionModifier((SpecialistTypes)(pOrderNode->iData1), toolTipSink);
			break;

		case ORDER_MAINTAIN:
			iMultiplier += getProductionModifier((ProcessTypes)(pOrderNode->iData1), toolTipSink);
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType failed to match a valid option");
			break;
		}
	}

	return iMultiplier;
}

//	--------------------------------------------------------------------------------
int CvCity::getGeneralProductionModifiers(CvString* toolTipSink) const
{
	int iMultiplier = 0;

	// Railroad to capital?
	if(IsIndustrialRouteToCapitalConnected())
	{
		const int iTempMod = GC.getINDUSTRIAL_ROUTE_PRODUCTION_MOD();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod != 0)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_RAILROAD_CONNECTION", iTempMod);
		}
	}
#if defined(MOD_DIPLOMACY_CITYSTATES)
	if(MOD_DIPLOMACY_CITYSTATES && GetBaseYieldRateFromLeague(YIELD_PRODUCTION) > 0)
	{
		int iTempLeagueMod = GetBaseYieldRateFromLeague(YIELD_PRODUCTION);
		iMultiplier += iTempLeagueMod;
		if(toolTipSink && iTempLeagueMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_LEAGUE", iTempLeagueMod);
		}
	}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	if(MOD_BALANCE_CORE_POLICIES && GET_PLAYER(getOwner()).IsPuppetProdMod() && IsPuppet())
	{
		int iTempMod = GET_PLAYER(getOwner()).GetPuppetProdMod();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_PUPPET_POLICY", iTempMod);
		}
	}
	if(MOD_BALANCE_CORE_POLICIES && GET_PLAYER(getOwner()).IsOccupiedProdMod() && IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		int iTempMod = GET_PLAYER(getOwner()).GetOccupiedProdMod();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_OCCUPIED_POLICY", iTempMod);
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsProductionModFromNumSpecialists())
	{
		if(GetCityCitizens()->GetTotalSpecialistCount() > 0)
		{
			int iTempMod = GetCityCitizens()->GetTotalSpecialistCount();
			iMultiplier += iTempMod;
			if(toolTipSink && iTempMod)
			{
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_NUM_SPECIALISTS", iTempMod);
			}
		}
	}
#endif
	return iMultiplier;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(UnitTypes eUnit, CvString* toolTipSink) const
{
	VALIDATE_OBJECT

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo == NULL)
	{
		//Unit type doesn't exist!
		return 0;
	}

	CvPlayerAI& thisPlayer = GET_PLAYER(getOwner());

	int iMultiplier = getGeneralProductionModifiers(toolTipSink);

	iMultiplier += thisPlayer.getProductionModifier(eUnit, toolTipSink);

	int iTempMod;

	// Capital Settler bonus
	if(isCapital() && pkUnitInfo->IsFound())
	{
		iTempMod = GET_PLAYER(getOwner()).getCapitalSettlerProductionModifier();
		iMultiplier += iTempMod;
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_CAPITAL_SETTLER_PLAYER", iTempMod);
	}

#if defined(MOD_BALANCE_CORE)
	// Class Production Bonus
	if (thisPlayer.GetUnitClassProductionModifier((UnitClassTypes)pkUnitInfo->GetUnitClassType()) != 0)
	{
		iTempMod = GET_PLAYER(getOwner()).GetUnitClassProductionModifier((UnitClassTypes)pkUnitInfo->GetUnitClassType());
		iMultiplier += iTempMod;
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_UNIT_CLASS", iTempMod);
	}
#endif

	// Domain bonus
	iTempMod = getDomainProductionModifier((DomainTypes)(pkUnitInfo->GetDomainType()));
	iMultiplier += iTempMod;
	if(toolTipSink && iTempMod)
	{
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_UNIT_DOMAIN", iTempMod);
	}
#if defined(MOD_BALANCE_CORE)
	if(thisPlayer.GetPlayerTraits()->GetNumPledgeDomainProductionModifier((DomainTypes)(pkUnitInfo->GetDomainType())) != NO_DOMAIN)
	{
		int iProtections = 0;
		for(int iMinorLoop = 0; iMinorLoop < MAX_MINOR_CIVS; iMinorLoop++)
		{
			PlayerTypes eLoopPlayer = (PlayerTypes) iMinorLoop;
			if(eLoopPlayer != NO_PLAYER && GET_PLAYER(eLoopPlayer).GetMinorCivAI()->IsProtectedByMajor(GetPlayer()->GetID()))
			{
				iProtections++;
			}
		}
		iTempMod = (thisPlayer.GetPlayerTraits()->GetNumPledgeDomainProductionModifier((DomainTypes)(pkUnitInfo->GetDomainType())) * iProtections);
		iMultiplier += iTempMod;
		if(toolTipSink &&iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_PLEDGES_UNIT_DOMAIN", iTempMod);
		}
	}
#endif

	// UnitCombat class bonus
	UnitCombatTypes eUnitCombatType = (UnitCombatTypes)(pkUnitInfo->GetUnitCombatType());
	if(eUnitCombatType != NO_UNITCOMBAT)
	{
		iTempMod = getUnitCombatProductionModifier(eUnitCombatType);
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_UNIT_COMBAT_TYPE", iTempMod);
		}
	}

#if defined(MOD_BALANCE_CORE)
	// Production bonus from Corporation
	CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
	if (eCorporation != NO_CORPORATION)
	{
		CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
		if (pkCorporationInfo)
		{
			for (int iResource = 0; iResource < GC.getNumResourceInfos(); iResource++)
			{
				if (pkUnitInfo->GetResourceQuantityRequirement(iResource) > 0)
				{
					iTempMod = pkCorporationInfo->GetUnitResourceProductionModifier(iResource);
					iMultiplier += iTempMod;
					if (toolTipSink && iTempMod)
					{
						GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_UNIT_CORPORATION", iTempMod);
					}
				}
			}
		}
	}
#endif

	// Military production bonus
	if(pkUnitInfo->IsMilitaryProduction())
	{
		iTempMod = getMilitaryProductionModifier();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_MILITARY", iTempMod);
		}
		CvPlot* pCityPlot = plot();
		for(int iUnitLoop = 0; iUnitLoop < pCityPlot->getNumUnits(); iUnitLoop++)
		{
			for(int iI = 0; iI < GC.getNumPromotionInfos(); iI++)
			{
				const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
				CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
				if(pkPromotionInfo != NULL)
				{
					if(pkPromotionInfo->GetMilitaryProductionModifier() > 0)
					{
						if(pCityPlot->getUnitByIndex(iUnitLoop)->isHasPromotion(eLoopPromotion))
						{
							iTempMod = pkPromotionInfo->GetMilitaryProductionModifier();
							iMultiplier += iTempMod;
							if(toolTipSink && iTempMod)
							{
								GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_MILITARY_UNITPROMOTION", iTempMod);
							}
						}
					}
				}
			}
		}
	}

	// City Space mod
	if(pkUnitInfo->GetSpaceshipProject() != NO_PROJECT)
	{
		iTempMod = getSpaceProductionModifier();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_SPACE", iTempMod);
		}

		iTempMod = thisPlayer.getSpaceProductionModifier();

#if defined(MOD_DIPLOMACY_CITYSTATES)
		if (MOD_DIPLOMACY_CITYSTATES) {
			int iLimitSpaceshipProduction = GC.getGame().GetGameLeagues()->GetSpaceShipProductionMod(getOwner());
			if(iLimitSpaceshipProduction != 0)
			{
				iTempMod += iLimitSpaceshipProduction;
			}
		}
#endif
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_SPACE_PLAYER", iTempMod);
		}
	}

	// Production bonus for having a particular building
	iTempMod = 0;
	int iBuildingMod = 0;
	BuildingTypes eBuilding;
	for(int iI = 0; iI < GC.getNumBuildingInfos(); iI++)
	{
		eBuilding = (BuildingTypes) iI;
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			if(GetCityBuildings()->GetNumBuilding(eBuilding) > 0)
			{
				iTempMod = pkUnitInfo->GetBuildingProductionModifier(eBuilding);

				if(iTempMod != 0)
				{
#if defined(MOD_BUGFIX_MINOR)
					iTempMod *= GetCityBuildings()->GetNumBuilding(eBuilding);
#endif
					iBuildingMod += iTempMod;
					if(toolTipSink && iTempMod)
					{
						GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_UNIT_WITH_BUILDING", iTempMod, pkBuildingInfo->GetDescription());
					}
				}
			}
		}
	}
	if(iBuildingMod != 0)
	{
		iMultiplier += iBuildingMod;
	}

	return iMultiplier;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(BuildingTypes eBuilding, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iMultiplier = getGeneralProductionModifiers(toolTipSink);
	iMultiplier += GET_PLAYER(getOwner()).getProductionModifier(eBuilding, toolTipSink);

	CvBuildingEntry* thisBuildingEntry = GC.getBuildingInfo(eBuilding);
	if(thisBuildingEntry == NULL)	//should never happen
		return -1;

	const CvBuildingClassInfo& kBuildingClassInfo = thisBuildingEntry->GetBuildingClassInfo();

	int iTempMod;

	// Wonder bonus
	if(::isWorldWonderClass(kBuildingClassInfo) ||
	        ::isTeamWonderClass(kBuildingClassInfo) ||
	        ::isNationalWonderClass(kBuildingClassInfo))
	{
		iTempMod = GetWonderProductionModifier();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_CITY", iTempMod);
		}

		iTempMod = GET_PLAYER(getOwner()).getWonderProductionModifier();
#if defined(MOD_BALANCE_CORE)
		if(GET_PLAYER(getOwner()).isGoldenAge() && GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModGA() > 0)
		{
			iTempMod += GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModGA();
		}
#endif
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_PLAYER", iTempMod);
		}

		iTempMod = GetLocalResourceWonderProductionMod(eBuilding, toolTipSink);
		iMultiplier += iTempMod;

		ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
		if(eMajority != NO_RELIGION)
		{
			const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
			if(pReligion)
			{
				// Depends on era of wonder
				EraTypes eEra;
				TechTypes eTech = (TechTypes)thisBuildingEntry->GetPrereqAndTech();
				if(eTech != NO_TECH)
				{
					CvTechEntry* pEntry = GC.GetGameTechs()->GetEntry(eTech);
					if(pEntry)
					{
						eEra = (EraTypes)pEntry->GetEra();
						if(eEra != NO_ERA)
						{
							iTempMod = pReligion->m_Beliefs.GetWonderProductionModifier(eEra, getOwner());
							BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
							if (eSecondaryPantheon != NO_BELIEF)
							{
								if((int)eEra < GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetObsoleteEra())
								{
									iTempMod += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetWonderProductionModifier();
								}
							}
							iMultiplier += iTempMod;
							if(toolTipSink && iTempMod)
							{
								GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_RELIGION", iTempMod);
							}
						}
					}
				}
			}
		}
		CvPlot* pCityPlot = plot();
		for(int iUnitLoop = 0; iUnitLoop < pCityPlot->getNumUnits(); iUnitLoop++)
		{
			int iI;
			for(iI = 0; iI < GC.getNumPromotionInfos(); iI++)
			{
				const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
				CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
				if(pkPromotionInfo != NULL)
				{
					if(pkPromotionInfo->GetWonderProductionModifier() > 0)
					{
						if(pCityPlot->getUnitByIndex(iUnitLoop)->isHasPromotion(eLoopPromotion))
						{
							iTempMod = pkPromotionInfo->GetWonderProductionModifier();
							iMultiplier += iTempMod;
							if(toolTipSink && iTempMod)
							{
								GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_UNITPROMOTION", iTempMod);
							}
						}
					}
				}
			}
		}
		int iNumberOfImprovements = 0;
		CvPlot* pLoopPlot;
		for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
		{
			pLoopPlot = iterateRingPlots(getX(), getY(), iJ);
			if(pLoopPlot != NULL && pLoopPlot->getOwner() == getOwner())
			{
				if(pLoopPlot->getImprovementType() != NO_IMPROVEMENT && !pLoopPlot->IsImprovementPillaged())
				{
					CvImprovementEntry* pImprovementInfo = GC.getImprovementInfo(pLoopPlot->getImprovementType());
					if(pImprovementInfo->GetWonderProductionModifier() > 0)
					{
						iTempMod = pImprovementInfo->GetWonderProductionModifier();
						iMultiplier += iTempMod;
						iNumberOfImprovements++;
					}
				}
			}
		}
		if(toolTipSink && iTempMod && iNumberOfImprovements)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_IMPROVEMENT", iTempMod * iNumberOfImprovements);
		}
	}
	// Not-wonder bonus
	else
	{
		iTempMod = m_pCityBuildings->GetBuildingProductionModifier();
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_BUILDING_CITY", iTempMod);
		}
	}

	// From policies
	iTempMod = GET_PLAYER(getOwner()).GetPlayerPolicies()->GetBuildingClassProductionModifier((BuildingClassTypes)kBuildingClassInfo.GetID());
	if(iTempMod != 0)
	{
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_BUILDING_POLICY", iTempMod);
		}
	}

	// From traits
	iTempMod = GET_PLAYER(getOwner()).GetPlayerTraits()->GetCapitalBuildingDiscount(eBuilding);
	if(iTempMod != 0)
	{
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_CAPITAL_BUILDING_TRAIT", iTempMod);
		}
	}

	if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModifierToBuilding() > 0)
	{
		int iMod = GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionToBuildingDiscount(eBuilding);
		iTempMod = (GetWonderProductionModifier() * iMod) / 100;
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_CITY_TRAIT", iTempMod);
		}

		iTempMod = (GET_PLAYER(getOwner()).getWonderProductionModifier() * iMod) / 100;
		if(GET_PLAYER(getOwner()).isGoldenAge() && GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModGA() > 0)
		{
			iTempMod += (GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModGA() * iMod) / 100;
		}
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_PLAYER_TRAIT", iTempMod);
		}

		BuildingTypes ePyramidWonder = (BuildingTypes) GC.getInfoTypeForString("BUILDING_PYRAMID");
		iTempMod = (GetLocalResourceWonderProductionMod(ePyramidWonder) * iMod) / 100;
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_RESOURCE_TRAIT", iTempMod);
		}

		ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
		if(eMajority != NO_RELIGION)
		{
			const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
			if(pReligion)
			{
				// Depends on era of wonder
				EraTypes eEra;
				TechTypes eTech = (TechTypes)thisBuildingEntry->GetPrereqAndTech();
				if(eTech != NO_TECH)
				{
					CvTechEntry* pEntry = GC.GetGameTechs()->GetEntry(eTech);
					if(pEntry)
					{
						eEra = (EraTypes)pEntry->GetEra();
						if(eEra != NO_ERA)
						{
							iTempMod = (pReligion->m_Beliefs.GetWonderProductionModifier(eEra, getOwner()) * iMod) / 100;
							BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
							if (eSecondaryPantheon != NO_BELIEF)
							{
								if((int)eEra < GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetObsoleteEra())
								{
									iTempMod += (GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetWonderProductionModifier() * iMod) / 100;
								}
							}
							iMultiplier += iTempMod;
							if(toolTipSink && iTempMod)
							{
								GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_RELIGION_TRAIT", iTempMod);
							}
						}
					}
				}
			}
		}
		iTempMod = (GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_WONDER_PRODUCTION_MODIFIER) * iMod) / 100;
		iMultiplier += iTempMod;
		if(toolTipSink && iTempMod)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_POLICY_TRAIT", iTempMod);
		}

		CvPlot* pCityPlot = plot();
		for(int iUnitLoop = 0; iUnitLoop < pCityPlot->getNumUnits(); iUnitLoop++)
		{
			int iI;
			for(iI = 0; iI < GC.getNumPromotionInfos(); iI++)
			{
				const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
				CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
				if(pkPromotionInfo != NULL)
				{
					if(pkPromotionInfo->GetWonderProductionModifier() > 0)
					{
						if(pCityPlot->getUnitByIndex(iUnitLoop)->isHasPromotion(eLoopPromotion))
						{
							iTempMod = (pkPromotionInfo->GetWonderProductionModifier() * iMod) / 100;
							iMultiplier += iTempMod;
							if(toolTipSink && iTempMod)
							{
								GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_UNIT_TRAIT", iTempMod);
							}
						}
					}
				}
			}
		}
		int iNumberOfImprovements = 0;
		CvPlot* pLoopPlot;
		for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
		{
			pLoopPlot = iterateRingPlots(getX(), getY(), iJ);
			if(pLoopPlot != NULL && pLoopPlot->getOwner() == getOwner())
			{
				if(pLoopPlot->getImprovementType() != NO_IMPROVEMENT && !pLoopPlot->IsImprovementPillaged())
				{
					CvImprovementEntry* pImprovementInfo = GC.getImprovementInfo(pLoopPlot->getImprovementType());
					if(pImprovementInfo->GetWonderProductionModifier() > 0)
					{
						iTempMod = (pImprovementInfo->GetWonderProductionModifier() * iMod) / 100;
						iMultiplier += iTempMod;
						iNumberOfImprovements++;						
					}
				}
			}
		}
		if(toolTipSink && iTempMod && iNumberOfImprovements)
		{
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_TO_BUILDING_FROM_IMPROVEMENT_TRAIT", iTempMod * iNumberOfImprovements);
		}
	}
	return iMultiplier;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(ProjectTypes eProject, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iMultiplier = getGeneralProductionModifiers(toolTipSink);
	iMultiplier += GET_PLAYER(getOwner()).getProductionModifier(eProject, toolTipSink);

	int iTempMod;

	// City Space mod
	if(GC.getProjectInfo(eProject)->IsSpaceship())
	{
		iTempMod = getSpaceProductionModifier();
		iMultiplier += iTempMod;
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_SPACE", iTempMod);
	}

	return iMultiplier;
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(SpecialistTypes eSpecialist, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iMultiplier = getGeneralProductionModifiers(toolTipSink);
	iMultiplier += GET_PLAYER(getOwner()).getProductionModifier(eSpecialist, toolTipSink);

	return iMultiplier;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionModifier(ProcessTypes eProcess, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iMultiplier = getGeneralProductionModifiers(toolTipSink);
	iMultiplier += GET_PLAYER(getOwner()).getProductionModifier(eProcess, toolTipSink);

	return iMultiplier;
}

//	--------------------------------------------------------------------------------
int CvCity::getProductionDifference(int /*iProductionNeeded*/, int /*iProduction*/, int iProductionModifier, bool bFoodProduction, bool bOverflow) const
{
	VALIDATE_OBJECT
	// If we're in anarchy, then no Production is done!
	if(GET_PLAYER(getOwner()).IsAnarchy())
	{
		return 0;
	}
	// If we're in Resistance, then no Production is done!
	if(IsResistance() || IsRazing())
	{
		return 0;
	}

	int iFoodProduction = ((bFoodProduction) ? (GetFoodProduction(getYieldRate(YIELD_FOOD, false) - foodConsumption(true))) : 0);

	int iOverflow = ((bOverflow) ? (getOverflowProduction() + getFeatureProduction()) : 0);

	// Sum up difference
	int iBaseProduction = getBaseYieldRate(YIELD_PRODUCTION) * 100;
	iBaseProduction += (GetYieldPerPopTimes100(YIELD_PRODUCTION) * getPopulation());

	int iModifiedProduction = iBaseProduction * getBaseYieldRateModifier(YIELD_PRODUCTION, iProductionModifier);
	iModifiedProduction /= 10000;

	iModifiedProduction += iOverflow;
	iModifiedProduction += iFoodProduction;

	return iModifiedProduction;

}


//	--------------------------------------------------------------------------------
int CvCity::getCurrentProductionDifference(bool bIgnoreFood, bool bOverflow) const
{
	VALIDATE_OBJECT
	return getProductionDifference(getProductionNeeded(), getProduction(), getProductionModifier(), (!bIgnoreFood && isFoodProduction()), bOverflow);
}

//	--------------------------------------------------------------------------------
// What is the production of this city, not counting modifiers specific to what we happen to be building?
int CvCity::getRawProductionDifference(bool bIgnoreFood, bool bOverflow) const
{
	VALIDATE_OBJECT
	return getProductionDifference(getProductionNeeded(), getProduction(), getGeneralProductionModifiers(), (!bIgnoreFood && isFoodProduction()), bOverflow);
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionDifferenceTimes100(int /*iProductionNeeded*/, int /*iProduction*/, int iProductionModifier, bool bFoodProduction, bool bOverflow) const
{
	VALIDATE_OBJECT
	// If we're in anarchy, then no Production is done!
	if(GET_PLAYER(getOwner()).IsAnarchy())
	{
		return 0;
	}
	// If we're in Resistance, then no Production is done!
	if(IsResistance() || IsRazing())
	{
		return 0;
	}

	int iFoodProduction = ((bFoodProduction) ? GetFoodProduction(getYieldRate(YIELD_FOOD, false) - foodConsumption(true)) : 0);
	iFoodProduction *= 100;

	int iOverflow = ((bOverflow) ? (getOverflowProductionTimes100() + getFeatureProduction() * 100) : 0);

	// Sum up difference
	int iBaseProduction = getBaseYieldRate(YIELD_PRODUCTION) * 100;
	iBaseProduction += (GetYieldPerPopTimes100(YIELD_PRODUCTION) * getPopulation());

	int iModifiedProduction = iBaseProduction * getBaseYieldRateModifier(YIELD_PRODUCTION, iProductionModifier);
	iModifiedProduction /= 100;

	iModifiedProduction += iOverflow;
	iModifiedProduction += iFoodProduction;

	int iTradeYield = GET_PLAYER(m_eOwner).GetTrade()->GetTradeValuesAtCityTimes100(this, YIELD_PRODUCTION);
	iModifiedProduction += iTradeYield;

	return iModifiedProduction;
}


//	--------------------------------------------------------------------------------
int CvCity::getCurrentProductionDifferenceTimes100(bool bIgnoreFood, bool bOverflow) const
{
	VALIDATE_OBJECT
	return getProductionDifferenceTimes100(getProductionNeeded(), getProductionTimes100(), getProductionModifier(), (!bIgnoreFood && isFoodProduction()), bOverflow);
}

//	--------------------------------------------------------------------------------
// What is the production of this city, not counting modifiers specific to what we happen to be building?
int CvCity::getRawProductionDifferenceTimes100(bool bIgnoreFood, bool bOverflow) const
{
	VALIDATE_OBJECT
	return getProductionDifferenceTimes100(getProductionNeeded(), getProductionTimes100(), getGeneralProductionModifiers(), (!bIgnoreFood && isFoodProduction()), bOverflow);
}


//	--------------------------------------------------------------------------------
int CvCity::getExtraProductionDifference(int iExtra) const
{
	VALIDATE_OBJECT
	return getExtraProductionDifference(iExtra, getProductionModifier());
}

//	--------------------------------------------------------------------------------
int CvCity::getExtraProductionDifference(int iExtra, UnitTypes eUnit) const
{
	VALIDATE_OBJECT
	return getExtraProductionDifference(iExtra, getProductionModifier(eUnit));
}

//	--------------------------------------------------------------------------------
int CvCity::getExtraProductionDifference(int iExtra, BuildingTypes eBuilding) const
{
	VALIDATE_OBJECT
	return getExtraProductionDifference(iExtra, getProductionModifier(eBuilding));
}

//	--------------------------------------------------------------------------------
int CvCity::getExtraProductionDifference(int iExtra, ProjectTypes eProject) const
{
	VALIDATE_OBJECT
	return getExtraProductionDifference(iExtra, getProductionModifier(eProject));
}

//	--------------------------------------------------------------------------------
int CvCity::getExtraProductionDifference(int iExtra, int iModifier) const
{
	VALIDATE_OBJECT
	return ((iExtra * getBaseYieldRateModifier(YIELD_PRODUCTION, iModifier)) / 100);
}

//	--------------------------------------------------------------------------------
/// Convert extra food to production if building a unit built partially from food
int CvCity::GetFoodProduction(int iExcessFood) const
{
	int iRtnValue;

	if(iExcessFood <= 0)
	{
		iRtnValue = 0;
	}
	else if(iExcessFood <= 2)
	{
		iRtnValue = iExcessFood * 100;
	}
	else if(iExcessFood > 2 && iExcessFood <= 4)
	{
		iRtnValue = 200 + (iExcessFood - 2) * 50;
	}
	else
	{
		iRtnValue = 300 + (iExcessFood - 4) * 25;
	}

	return (iRtnValue / 100);
}

//	--------------------------------------------------------------------------------
bool CvCity::canHurry(HurryTypes eHurry, bool bTestVisible) const
{
	VALIDATE_OBJECT
	if(!(GET_PLAYER(getOwner()).IsHasAccessToHurry(eHurry)))
	{
		return false;
	}

	if(getProduction() >= getProductionNeeded())
	{
		return false;
	}

	CvHurryInfo* pkHurryInfo = GC.getHurryInfo(eHurry);
	if(pkHurryInfo == NULL)
		return false;


	// City cannot Hurry Player-level things
	if(pkHurryInfo->getGoldPerBeaker() > 0 || pkHurryInfo->getGoldPerCulture() > 0)
	{
		return false;
	}

	if(!bTestVisible)
	{
		if(!isProductionUnit() && !isProductionBuilding())
		{
			return false;
		}

		if(GET_PLAYER(getOwner()).GetTreasury()->GetGold() < hurryGold(eHurry))
		{
			return false;
		}

		if(maxHurryPopulation() < hurryPopulation(eHurry))
		{
			return false;
		}
	}

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::canHurryUnit(HurryTypes eHurry, UnitTypes eUnit, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	if(!(GET_PLAYER(getOwner()).IsHasAccessToHurry(eHurry)))
	{
		return false;
	}

	if(getUnitProduction(eUnit) >= getProductionNeeded(eUnit))
	{
		return false;
	}

	if(GET_PLAYER(getOwner()).GetTreasury()->GetGold() < getHurryGold(eHurry, getHurryCost(eHurry, false, eUnit, bIgnoreNew), getProductionNeeded(eUnit)))
	{
		return false;
	}

	if(maxHurryPopulation() < getHurryPopulation(eHurry, getHurryCost(eHurry, true, eUnit, bIgnoreNew)))
	{
		return false;
	}

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::canHurryBuilding(HurryTypes eHurry, BuildingTypes eBuilding, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	if(!(GET_PLAYER(getOwner()).IsHasAccessToHurry(eHurry)))
	{
		return false;
	}

	if(m_pCityBuildings->GetBuildingProduction(eBuilding) >= getProductionNeeded(eBuilding))
	{
		return false;
	}

	if(GET_PLAYER(getOwner()).GetTreasury()->GetGold() < getHurryGold(eHurry, getHurryCost(eHurry, false, eBuilding, bIgnoreNew), getProductionNeeded(eBuilding)))
	{
		return false;
	}

	if(maxHurryPopulation() < getHurryPopulation(eHurry, getHurryCost(eHurry, true, eBuilding, bIgnoreNew)))
	{
		return false;
	}

	return true;
}


//	--------------------------------------------------------------------------------
void CvCity::hurry(HurryTypes eHurry)
{
	VALIDATE_OBJECT
	int iHurryGold;
	int iHurryPopulation;

	if(!canHurry(eHurry))
	{
		return;
	}

	iHurryGold = hurryGold(eHurry);
	iHurryPopulation = hurryPopulation(eHurry);

	changeProduction(hurryProduction(eHurry));

	GET_PLAYER(getOwner()).GetTreasury()->ChangeGold(-(iHurryGold));
	changePopulation(-(iHurryPopulation));

	if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
	{
		DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
	}
}


//	--------------------------------------------------------------------------------
UnitTypes CvCity::getConscriptUnit() const
{
	VALIDATE_OBJECT
	UnitTypes eBestUnit = NO_UNIT;
	int iBestValue = 0;

	for(int iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
	{
		const UnitClassTypes eUnitClass = static_cast<UnitClassTypes>(iI);
		CvUnitClassInfo* pkUnitClassInfo = GC.getUnitClassInfo(eUnitClass);
		if(pkUnitClassInfo)
		{
			const UnitTypes eLoopUnit = (UnitTypes)getCivilizationInfo().getCivilizationUnits(iI);
			if(eLoopUnit != NO_UNIT)
			{
				CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eLoopUnit);
				if(pkUnitInfo)
				{
					if(canTrain(eLoopUnit))
					{
						const int iValue = pkUnitInfo->GetConscriptionValue();
						if(iValue > iBestValue)
						{
							iBestValue = iValue;
							eBestUnit = eLoopUnit;
						}
					}
				}
			}
		}
	}

	return eBestUnit;
}


//	--------------------------------------------------------------------------------
int CvCity::getConscriptPopulation() const
{
	VALIDATE_OBJECT
	UnitTypes eConscriptUnit = getConscriptUnit();

	if(eConscriptUnit == NO_UNIT)
	{
		return 0;
	}

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eConscriptUnit);
	if(pkUnitInfo == NULL)
	{
		return 0;
	}

	int iConscriptPopulationPerCost = GC.getCONSCRIPT_POPULATION_PER_COST();
	if(iConscriptPopulationPerCost == 0)
	{
		return 0;
	}

	return std::max(1, ((pkUnitInfo->GetProductionCost()) / iConscriptPopulationPerCost));
}


//	--------------------------------------------------------------------------------
int CvCity::conscriptMinCityPopulation() const
{
	VALIDATE_OBJECT
	int iPopulation = GC.getCONSCRIPT_MIN_CITY_POPULATION();

	iPopulation += getConscriptPopulation();

	return iPopulation;
}


//	--------------------------------------------------------------------------------
bool CvCity::canConscript() const
{
	VALIDATE_OBJECT
	if(isDrafted())
	{
		return false;
	}

	if(GET_PLAYER(getOwner()).getConscriptCount() >= GET_PLAYER(getOwner()).getMaxConscript())
	{
		return false;
	}

	if(getPopulation() <= getConscriptPopulation())
	{
		return false;
	}

	if(getPopulation() < conscriptMinCityPopulation())
	{
		return false;
	}

	if(getConscriptUnit() == NO_UNIT)
	{
		return false;
	}

	return true;
}

//	--------------------------------------------------------------------------------
CvUnit* CvCity::initConscriptedUnit()
{
	VALIDATE_OBJECT
	UnitAITypes eCityAI = NO_UNITAI;
	UnitTypes eConscriptUnit = getConscriptUnit();

	if(NO_UNIT == eConscriptUnit)
	{
		return NULL;
	}

	CvUnit* pUnit = GET_PLAYER(getOwner()).initUnit(eConscriptUnit, getX(), getY(), eCityAI);
	CvAssertMsg(pUnit != NULL, "pUnit expected to be assigned (not NULL)");

	if(NULL != pUnit)
	{
		addProductionExperience(pUnit, true);

		pUnit->setMoves(pUnit->maxMoves());
	}

	return pUnit;
}


//	--------------------------------------------------------------------------------
void CvCity::conscript()
{
	VALIDATE_OBJECT
	if(!canConscript())
	{
		return;
	}

	changePopulation(-(getConscriptPopulation()));

	setDrafted(true);

	GET_PLAYER(getOwner()).changeConscriptCount(1);

	CvUnit* pUnit = initConscriptedUnit();
	CvAssertMsg(pUnit != NULL, "pUnit expected to be assigned (not NULL)");

	if(NULL != pUnit)
	{
		if(GC.getGame().getActivePlayer() == getOwner())
		{
			auto_ptr<ICvUnit1> pDllUnit = GC.WrapUnitPointer(pUnit);
			DLLUI->selectUnit(pDllUnit.get(), true, false, true);
		}
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getResourceYieldRateModifier(YieldTypes eIndex, ResourceTypes eResource) const
{
	VALIDATE_OBJECT
	int iModifier = 0;

	for(int iI = 0; iI < GC.getNumBuildingInfos(); iI++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			iModifier += m_pCityBuildings->GetNumActiveBuilding(eBuilding) * pkBuildingInfo->GetResourceYieldModifier(eResource, eIndex);
		}
	}

	return iModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::processResource(ResourceTypes eResource, int iChange)
{
	VALIDATE_OBJECT

	// Yield modifier for having a local resource
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		const YieldTypes eYield = static_cast<YieldTypes>(iI);
		changeResourceYieldRateModifier(eYield, (getResourceYieldRateModifier(eYield, eResource) * iChange));
#if defined(MOD_BALANCE_CORE)
		UpdateCityYields(eYield);
		if(eYield == YIELD_CULTURE || eYield == YIELD_TOURISM)
		{
			GetCityCulture()->CalculateBaseTourismBeforeModifiers();
			GetCityCulture()->CalculateBaseTourism();
		}
#endif
	}
}


//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
void CvCity::processBuilding(BuildingTypes eBuilding, int iChange, bool bFirst, bool bObsolete, bool /*bApplyingAllCitiesBonus*/, bool bNoBonus)
#else
void CvCity::processBuilding(BuildingTypes eBuilding, int iChange, bool bFirst, bool bObsolete, bool /*bApplyingAllCitiesBonus*/)
#endif
{
	VALIDATE_OBJECT

	CvBuildingEntry* pBuildingInfo = GC.getBuildingInfo(eBuilding);

	if(pBuildingInfo == NULL)
		return;

	BuildingClassTypes eBuildingClass = (BuildingClassTypes) pBuildingInfo->GetBuildingClassType();

	CvPlayer& owningPlayer = GET_PLAYER(getOwner());
	CvTeam& owningTeam = GET_TEAM(getTeam());
	const CvCivilizationInfo& thisCiv = getCivilizationInfo();
	if(!(owningTeam.isObsoleteBuilding(eBuilding)) || bObsolete)
	{
		// One-shot items
		if(bFirst && iChange > 0)
		{
			// Capital
			if(pBuildingInfo->IsCapital())
				owningPlayer.setCapitalCity(this);
#if defined(MOD_BALANCE_CORE)
			if(!bNoBonus && ::isWorldWonderClass(pBuildingInfo->GetBuildingClassInfo()))
			{
				int iTourism = owningPlayer.GetEventTourism();
				owningPlayer.ChangeNumHistoricEvents(1);
				// Culture boost based on previous turns
				int iPreviousTurnsToCount = 10;
				// Calculate boost
				iTourism *= owningPlayer.GetCultureYieldFromPreviousTurns(GC.getGame().getGameTurn(), iPreviousTurnsToCount);
				iTourism /= 100;
				owningPlayer.GetCulture()->AddTourismAllKnownCivs(iTourism);
				if(iTourism > 0)
				{
					if(owningPlayer.GetID() == GC.getGame().getActivePlayer())
					{
						char text[256] = {0};
						float fDelay = 0.5f;
						sprintf_s(text, "[COLOR_WHITE]+%d[ENDCOLOR][ICON_TOURISM]", iTourism);
						DLLUI->AddPopupText(getX(), getY(), text, fDelay);
						CvNotifications* pNotification = owningPlayer.GetNotifications();
						if(pNotification)
						{
							CvString strMessage;
							CvString strSummary;
							strMessage = GetLocalizedText("TXT_KEY_TOURISM_EVENT_WW", iTourism);
							strSummary = GetLocalizedText("TXT_KEY_TOURISM_EVENT_SUMMARY");
							pNotification->Add(NOTIFICATION_CULTURE_VICTORY_SOMEONE_INFLUENTIAL, strMessage, strSummary, getX(), getY(), owningPlayer.GetID());
						}
					}
				}
			}
#endif
			// Free Units
			CvUnit* pFreeUnit;

			int iFreeUnitLoop;
			int iFreeSpecUnitLoop;
			for(int iUnitLoop = 0; iUnitLoop < GC.getNumUnitInfos(); iUnitLoop++)
			{
				const UnitTypes eUnit = static_cast<UnitTypes>(iUnitLoop);
				CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
				if(pkUnitInfo)
				{
					for(iFreeUnitLoop = 0; iFreeUnitLoop < pBuildingInfo->GetNumFreeUnits(iUnitLoop); iFreeUnitLoop++)
					{
						// Get the right unit of this class for this civ
						const UnitTypes eFreeUnitType = (UnitTypes)thisCiv.getCivilizationUnits((UnitClassTypes)pkUnitInfo->GetUnitClassType());
#if defined(MOD_BALANCE_CORE)
						//Test for forbidden or locked units.
						if(eFreeUnitType == NO_UNIT)
						{
							// Great prophet?
							if(GC.GetGameUnits()->GetEntry(eUnit)->IsFoundReligion())
							{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
								GetCityCitizens()->DoSpawnGreatPerson(eUnit, true /*bIncrementCount*/, true, MOD_GLOBAL_TRULY_FREE_GP);
#else
								GetCityCitizens()->DoSpawnGreatPerson(eUnit, true /*bIncrementCount*/, true);
#endif
							}
							else
							{
#if defined(MOD_BALANCE_CORE)
								// for venice
								pFreeUnit = NULL;
								if (pkUnitInfo->IsFound() && owningPlayer.GetPlayerTraits()->IsNoAnnexing())
								{
									// drop a merchant of venice instead
									// find the eUnit replacement that's the merchant of venice
									for(int iVeniceSearch = 0; iVeniceSearch < GC.getNumUnitClassInfos(); iVeniceSearch++)
									{
										const UnitClassTypes eVeniceUnitClass = static_cast<UnitClassTypes>(iVeniceSearch);
										CvUnitClassInfo* pkVeniceUnitClassInfo = GC.getUnitClassInfo(eVeniceUnitClass);
										if(pkVeniceUnitClassInfo)
										{
											const UnitTypes eMerchantOfVeniceUnit = (UnitTypes) getCivilizationInfo().getCivilizationUnits(eVeniceUnitClass);
											if (eMerchantOfVeniceUnit != NO_UNIT)
											{
												CvUnitEntry* pVeniceUnitEntry = GC.getUnitInfo(eMerchantOfVeniceUnit);
												if (pVeniceUnitEntry->IsCanBuyCityState())
												{
													pFreeUnit = owningPlayer.initUnit(eMerchantOfVeniceUnit, getX(), getY());				
													break;
												}
											}
										}
									}
								}
								else
								{
#endif
									pFreeUnit = owningPlayer.initUnit(eUnit, getX(), getY());
#if defined(MOD_BALANCE_CORE)
								}
								if(pFreeUnit->IsGreatPerson())
								{
									if(owningPlayer.GetPlayerTraits()->IsGPWLTKD())
									{
										int iWLTKD = (GC.getCITY_RESOURCE_WLTKD_TURNS() / 2);
										iWLTKD *= GC.getGame().getGameSpeedInfo().getTrainPercent();
										iWLTKD /= 100;
										if(iWLTKD > 0)
										{
											this->ChangeWeLoveTheKingDayCounter(iWLTKD);
											CvNotifications* pNotifications = owningPlayer.GetNotifications();
											if(pNotifications)
											{
												Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD_UA");
												strText <<  pFreeUnit->getNameKey() << this->getNameKey();
												Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD_UA");
												strSummary << this->getNameKey();
												pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), this->getX(), this->getY(), -1);
											}
										}
									}
									if(pFreeUnit->isWLKTKDOnBirth())
									{
										CvCity* pLoopCity;
										int iLoop;
										for(pLoopCity = owningPlayer.firstCity(&iLoop); pLoopCity != NULL; pLoopCity = owningPlayer.nextCity(&iLoop))
										{
											if(pLoopCity != NULL && pLoopCity->getOwner() == owningPlayer.GetID())
											{
												int iWLTKD = (GC.getCITY_RESOURCE_WLTKD_TURNS() / 2);
												iWLTKD *= GC.getGame().getGameSpeedInfo().getTrainPercent();
												iWLTKD /= 100;
												if (iWLTKD > 0)
												{
													pLoopCity->ChangeWeLoveTheKingDayCounter(iWLTKD);
													CvNotifications* pNotifications = owningPlayer.GetNotifications();
													if (pNotifications)
													{
														Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD_UNIT");
														strText << pFreeUnit->getNameKey() << pLoopCity->getNameKey();
														Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD_UNIT");
														strSummary << pLoopCity->getNameKey();
														pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pLoopCity->getX(), pLoopCity->getY(), -1);
													}
												}
											}
										}
									}
									if(pFreeUnit->isGoldenAgeOnBirth())
									{
										int iGoldenAgeTurns = owningPlayer.getGoldenAgeLength();
										int iValue = owningPlayer.GetGoldenAgeProgressMeter();
										owningPlayer.changeGoldenAgeTurns(iGoldenAgeTurns, iValue);
									}
									if(pFreeUnit->isCultureBoost())
									{
										int iValue = owningPlayer.GetTotalJONSCulturePerTurn() * 4;
										owningPlayer.changeJONSCulture(iValue);
										if(owningPlayer.getCapitalCity() != NULL)
										{
											owningPlayer.getCapitalCity()->ChangeJONSCultureStored(iValue);
										}
										CvNotifications* pNotifications = owningPlayer.GetNotifications();
										if (pNotifications)
										{
											Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CULTURE_UNIT");
											strText << pFreeUnit->getNameKey();
											Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CULTURE_UNIT");
											strSummary << pFreeUnit->getNameKey();
											pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pFreeUnit->getX(), pFreeUnit->getY(), -1);
										}
									}
								}
#endif
								// Bump up the count
								if(pFreeUnit->IsGreatGeneral())
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatGeneralsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatGeneralsCreated();
#endif
									bool bJumpSuccess = pFreeUnit->jumpToNearestValidPlot();
									if (bJumpSuccess)
									{
										addProductionExperience(pFreeUnit);
									}
									else
									{
										pFreeUnit->kill(false);
									}
								}
								else if(pFreeUnit->IsGreatAdmiral())
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatAdmiralsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatAdmiralsCreated();
#endif
									CvPlot *pSpawnPlot = owningPlayer.GetGreatAdmiralSpawnPlot(pFreeUnit);
									if (pFreeUnit->plot() != pSpawnPlot)
									{
										pFreeUnit->setXY(pSpawnPlot->getX(), pSpawnPlot->getY());
										addProductionExperience(pFreeUnit);
									}
								}
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_WRITER"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatWritersCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatWritersCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_ARTIST"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatArtistsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatArtistsCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_MUSICIAN"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatMusiciansCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatMusiciansCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
#if defined(MOD_DIPLOMACY_CITYSTATES)
								else if (MOD_DIPLOMACY_CITYSTATES && pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_GREAT_DIPLOMAT"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatDiplomatsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatDiplomatsCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
#endif
#if defined(MOD_BALANCE_CORE)
								else if (pkUnitInfo->IsGPExtra() == 1)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra1Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra1Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 2)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra2Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra2Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 3)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra3Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra3Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 4)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra4Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra4Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 5)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra5Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra5Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
#endif
								else if (pFreeUnit->IsGreatPerson())
								{
#if defined(MOD_GLOBAL_SEPARATE_GP_COUNTERS)
									if (MOD_GLOBAL_SEPARATE_GP_COUNTERS)
									{
										if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_MERCHANT"))
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatMerchantsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatMerchantsCreated();
#endif
										}
										else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_SCIENTIST"))
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatScientistsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatScientistsCreated();
#endif
										}
										else
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatEngineersCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatEngineersCreated();
#endif
										}
									}
									else
#endif
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
										owningPlayer.incrementGreatPeopleCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
										owningPlayer.incrementGreatPeopleCreated();
#endif
									bool bJumpSuccess = pFreeUnit->jumpToNearestValidPlot();
									if (bJumpSuccess)
									{
											addProductionExperience(pFreeUnit);
									}
									else
									{
										pFreeUnit->kill(false);
									}
								}
							}
						}
#endif
#if defined(MOD_BALANCE_CORE)
						//Test for forbidden or locked units.
						if(eFreeUnitType != NO_UNIT)
						{
#endif
							// Great prophet?
							if(GC.GetGameUnits()->GetEntry(eFreeUnitType)->IsFoundReligion())
							{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
								GetCityCitizens()->DoSpawnGreatPerson(eFreeUnitType, true /*bIncrementCount*/, true, MOD_GLOBAL_TRULY_FREE_GP);
#else
								GetCityCitizens()->DoSpawnGreatPerson(eFreeUnitType, true /*bIncrementCount*/, true);
#endif
							}
							else
							{
#if defined(MOD_BALANCE_CORE)
								// for venice
								pFreeUnit = NULL;
								if (pkUnitInfo->IsFound() && owningPlayer.GetPlayerTraits()->IsNoAnnexing())
								{
									// drop a merchant of venice instead
									// find the eUnit replacement that's the merchant of venice
									for(int iVeniceSearch = 0; iVeniceSearch < GC.getNumUnitClassInfos(); iVeniceSearch++)
									{
										const UnitClassTypes eVeniceUnitClass = static_cast<UnitClassTypes>(iVeniceSearch);
										CvUnitClassInfo* pkVeniceUnitClassInfo = GC.getUnitClassInfo(eVeniceUnitClass);
										if(pkVeniceUnitClassInfo)
										{
											const UnitTypes eMerchantOfVeniceUnit = (UnitTypes) getCivilizationInfo().getCivilizationUnits(eVeniceUnitClass);
											if (eMerchantOfVeniceUnit != NO_UNIT)
											{
												CvUnitEntry* pVeniceUnitEntry = GC.getUnitInfo(eMerchantOfVeniceUnit);
												if (pVeniceUnitEntry->IsCanBuyCityState())
												{
													pFreeUnit = owningPlayer.initUnit(eMerchantOfVeniceUnit, getX(), getY());				
													break;
												}
											}
										}
									}
								}
								else
								{
#endif
									pFreeUnit = owningPlayer.initUnit(eFreeUnitType, getX(), getY());
#if defined(MOD_BALANCE_CORE)
									if(pFreeUnit && pFreeUnit->isTrade())
									{
										if(GC.getLogging() && GC.getAILogging())
										{
											CvString strCiv = GET_PLAYER(getOwner()).getCivilizationAdjective();
											CvString strLogString;
											strLogString.Format("FREE TRADE UNIT CREATED: %s %s at %d,d", strCiv.c_str(), pFreeUnit->getName().c_str(), pFreeUnit->getX(), pFreeUnit->getY() );
											GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
										}
									}
#endif
#if defined(MOD_BALANCE_CORE)
								}
								if(pFreeUnit->IsGreatPerson())
								{
									if(owningPlayer.GetPlayerTraits()->IsGPWLTKD())
									{
										int iWLTKD = (GC.getCITY_RESOURCE_WLTKD_TURNS() / 2);
										iWLTKD *= GC.getGame().getGameSpeedInfo().getTrainPercent();
										iWLTKD /= 100;
										if(iWLTKD > 0)
										{
											this->ChangeWeLoveTheKingDayCounter(iWLTKD);
											CvNotifications* pNotifications = owningPlayer.GetNotifications();
											if(pNotifications)
											{
												Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD_UA");
												strText <<  pFreeUnit->getNameKey() << this->getNameKey();
												Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD_UA");
												strSummary << this->getNameKey();
												pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), this->getX(), this->getY(), -1);
											}
										}
									}
									if(pFreeUnit->isWLKTKDOnBirth())
									{
										CvCity* pLoopCity;
										int iLoop;
										for(pLoopCity = owningPlayer.firstCity(&iLoop); pLoopCity != NULL; pLoopCity = owningPlayer.nextCity(&iLoop))
										{
											if(pLoopCity != NULL && pLoopCity->getOwner() == owningPlayer.GetID())
											{
												int iWLTKD = (GC.getCITY_RESOURCE_WLTKD_TURNS() / 2);
												iWLTKD *= GC.getGame().getGameSpeedInfo().getTrainPercent();
												iWLTKD /= 100;
												if (iWLTKD > 0)
												{
													pLoopCity->ChangeWeLoveTheKingDayCounter(iWLTKD);
													CvNotifications* pNotifications = owningPlayer.GetNotifications();
													if (pNotifications)
													{
														Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_WLTKD_UNIT");
														strText << pFreeUnit->getNameKey() << pLoopCity->getNameKey();
														Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_WLTKD_UNIT");
														strSummary << pLoopCity->getNameKey();
														pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pLoopCity->getX(), pLoopCity->getY(), -1);
													}
												}
											}
										}
									}
									if(pFreeUnit->isGoldenAgeOnBirth())
									{
										int iGoldenAgeTurns = owningPlayer.getGoldenAgeLength();
										int iValue = owningPlayer.GetGoldenAgeProgressMeter();
										owningPlayer.changeGoldenAgeTurns(iGoldenAgeTurns, iValue);
									}
									if(pFreeUnit->isCultureBoost())
									{
										int iValue = owningPlayer.GetTotalJONSCulturePerTurn() * 4;
										owningPlayer.changeJONSCulture(iValue);
										if(owningPlayer.getCapitalCity() != NULL)
										{
											owningPlayer.getCapitalCity()->ChangeJONSCultureStored(iValue);
										}
										CvNotifications* pNotifications = owningPlayer.GetNotifications();
										if (pNotifications)
										{
											Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CULTURE_UNIT");
											strText << pFreeUnit->getNameKey();
											Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CULTURE_UNIT");
											strSummary << pFreeUnit->getNameKey();
											pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pFreeUnit->getX(), pFreeUnit->getY(), -1);
										}
									}
								}
#endif
								// Bump up the count
								if(pFreeUnit->IsGreatGeneral())
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatGeneralsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatGeneralsCreated();
#endif
									bool bJumpSuccess = pFreeUnit->jumpToNearestValidPlot();
									if (bJumpSuccess)
									{
										addProductionExperience(pFreeUnit);
									}
									else
									{
										pFreeUnit->kill(false);
									}
								}
								else if(pFreeUnit->IsGreatAdmiral())
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatAdmiralsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatAdmiralsCreated();
#endif
									CvPlot *pSpawnPlot = owningPlayer.GetGreatAdmiralSpawnPlot(pFreeUnit);
									if (pFreeUnit->plot() != pSpawnPlot)
									{
										pFreeUnit->setXY(pSpawnPlot->getX(), pSpawnPlot->getY());
										addProductionExperience(pFreeUnit);
									}
								}
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_WRITER"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatWritersCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatWritersCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_ARTIST"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatArtistsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatArtistsCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
								else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_MUSICIAN"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatMusiciansCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatMusiciansCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}							
#if defined(MOD_DIPLOMACY_CITYSTATES)
								else if (MOD_DIPLOMACY_CITYSTATES && pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_GREAT_DIPLOMAT"))
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGreatDiplomatsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGreatDiplomatsCreated();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
#endif
#if defined(MOD_BALANCE_CORE)
								else if (pkUnitInfo->IsGPExtra() == 1)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra1Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra1Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 2)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra2Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra2Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 3)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra3Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra3Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 4)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra4Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra4Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
								else if (pkUnitInfo->IsGPExtra() == 5)
								{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
									owningPlayer.incrementGPExtra5Created(MOD_GLOBAL_TRULY_FREE_GP);
#else
									owningPlayer.incrementGPExtra5Created();
#endif
									if (!pFreeUnit->jumpToNearestValidPlot())
										pFreeUnit->kill(false);	// Could not find a valid spot!
								}
#endif
								else if (pFreeUnit->IsGreatPerson())
								{
#if defined(MOD_GLOBAL_SEPARATE_GP_COUNTERS)
									if (MOD_GLOBAL_SEPARATE_GP_COUNTERS)
									{
										if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_MERCHANT"))
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatMerchantsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatMerchantsCreated();
#endif
										}
										else if (pkUnitInfo->GetUnitClassType() == GC.getInfoTypeForString("UNITCLASS_SCIENTIST"))
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatScientistsCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatScientistsCreated();
#endif
										}
										else
										{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
											owningPlayer.incrementGreatEngineersCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
											owningPlayer.incrementGreatEngineersCreated();
#endif
										}
									}
									else
#endif
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
										owningPlayer.incrementGreatPeopleCreated(MOD_GLOBAL_TRULY_FREE_GP);
#else
										owningPlayer.incrementGreatPeopleCreated();
#endif
									bool bJumpSuccess = pFreeUnit->jumpToNearestValidPlot();
									if (bJumpSuccess)
									{
										addProductionExperience(pFreeUnit);
									}
									else
									{
										pFreeUnit->kill(false);
									}
								}
#if defined(MOD_BALANCE_CORE)
							}
#endif
						}
					}
#if defined(MOD_BALANCE_CORE)
					for(iFreeSpecUnitLoop = 0; iFreeSpecUnitLoop < pBuildingInfo->GetNumFreeSpecialUnits(iUnitLoop); iFreeSpecUnitLoop++)
					{
						pFreeUnit = NULL;
						const UnitTypes eFreeSpecUnitType = (UnitTypes)pkUnitInfo->GetUnitClassType();
						if(eFreeSpecUnitType != NO_UNIT)
						{
							pFreeUnit = owningPlayer.initUnit(eUnit, getX(), getY());
						}
						bool bJumpSuccess = pFreeUnit->jumpToNearestValidPlot();
						if (bJumpSuccess)
						{
							addProductionExperience(pFreeUnit);
						}
						else
						{
							pFreeUnit->kill(false);
						}
					}
#endif
				}
			}

#if defined(MOD_BALANCE_CORE)
			// Is this a Corporation building?
			CvBuildingClassInfo* pkBuildingClass = GC.getBuildingClassInfo(eBuildingClass);
			if (pkBuildingClass)
			{
				CorporationTypes eCorporation = pkBuildingClass->getCorporationType();
				if (eCorporation != NO_CORPORATION)
				{
					// Valid corporation
					CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
					if (pkCorporationInfo != NULL)
					{
						// Should we found this Corporation?
						if (pkCorporationInfo->GetHeadquartersBuildingClass() == eBuildingClass)
						{
							if (iChange > 0)
							{
								GC.getGame().GetGameCorporations()->FoundCorporation(getOwner(), eCorporation, this);
							}
						}
					}
				}
			}
#endif

			// Free building
			BuildingClassTypes eFreeBuildingClassThisCity = (BuildingClassTypes)pBuildingInfo->GetFreeBuildingThisCity();
			if(eFreeBuildingClassThisCity != NO_BUILDINGCLASS)
			{
				BuildingTypes eFreeBuildingThisCity = (BuildingTypes)(thisCiv.getCivilizationBuildings(eFreeBuildingClassThisCity));

				if (eFreeBuildingThisCity != NO_BUILDING)
				{
					m_pCityBuildings->SetNumRealBuilding(eFreeBuildingThisCity, 0);
					m_pCityBuildings->SetNumFreeBuilding(eFreeBuildingThisCity, 1);
				}
			}

			// Free Great Work
			GreatWorkType eGWType = pBuildingInfo->GetFreeGreatWork();
			if (eGWType != NO_GREAT_WORK)
			{
				GreatWorkClass eClass = CultureHelpers::GetGreatWorkClass(eGWType);
				int iGWindex = 	GC.getGame().GetGameCulture()->CreateGreatWork(eGWType, eClass, m_eOwner, owningPlayer.GetCurrentEra(), pBuildingInfo->GetDescription());
				m_pCityBuildings->SetBuildingGreatWork(eBuildingClass, 0, iGWindex);
#if defined(MOD_BALANCE_CORE)
				GetCityCulture()->CalculateBaseTourismBeforeModifiers();
				GetCityCulture()->CalculateBaseTourism();
#endif
			}

			// Tech boost for science buildings in capital
			if(owningPlayer.GetPlayerTraits()->IsTechBoostFromCapitalScienceBuildings())
			{
				if(isCapital())
				{
					if(pBuildingInfo->IsScienceBuilding())
					{
						int iMedianTechResearch = owningPlayer.GetPlayerTechs()->GetMedianTechResearch();
						iMedianTechResearch = (iMedianTechResearch * owningPlayer.GetMedianTechPercentage()) / 100;
#if defined(MOD_BALANCE_CORE)
						owningPlayer.doInstantYield(INSTANT_YIELD_TYPE_INSTANT, false, NO_GREATPERSON, eBuilding, iMedianTechResearch, false);
#else

						TechTypes eCurrentTech = owningPlayer.GetPlayerTechs()->GetCurrentResearch();
						if(eCurrentTech == NO_TECH)
						{
							owningPlayer.changeOverflowResearch(iMedianTechResearch);
						}
						else
						{
							owningTeam.GetTeamTechs()->ChangeResearchProgress(eCurrentTech, iMedianTechResearch, owningPlayer.GetID());
						}
#endif
					}
				}
			}

			// TERRA COTTA AWESOME
			if (pBuildingInfo->GetInstantMilitaryIncrease())
			{
				std::vector<UnitTypes> aExtraUnits;
				std::vector<UnitAITypes> aExtraUnitAITypes;
				CvUnit* pLoopUnit = NULL;
				int iLoop = 0;
				for(pLoopUnit = GET_PLAYER(m_eOwner).firstUnit(&iLoop); pLoopUnit != NULL; pLoopUnit = GET_PLAYER(m_eOwner).nextUnit(&iLoop))
				{
					if (pLoopUnit->getDomainType() == DOMAIN_LAND && pLoopUnit->IsCombatUnit())
					{
						UnitTypes eCurrentUnitType = pLoopUnit->getUnitType();
						UnitAITypes eCurrentUnitAIType = pLoopUnit->AI_getUnitAIType();

						// check for duplicate unit
						bool bAddUnit = true;
						for (uint ui = 0; ui < aExtraUnits.size(); ui++)
						{
							if (aExtraUnits[ui] == eCurrentUnitType)
							{
								bAddUnit = false;
							}
						}

						if (bAddUnit)
						{
							aExtraUnits.push_back(eCurrentUnitType);
							aExtraUnitAITypes.push_back(eCurrentUnitAIType);
						}
					}
				}

				for (uint ui = 0; ui < aExtraUnits.size(); ui++)
				{
					CvUnit* pNewUnit = GET_PLAYER(m_eOwner).initUnit(aExtraUnits[ui], m_iX, m_iY, aExtraUnitAITypes[ui]);
					bool bJumpSuccess = pNewUnit->jumpToNearestValidPlot();
#if defined(MOD_BALANCE_CORE)
					if(bJumpSuccess)
					{
						addProductionExperience(pNewUnit);
					}
#endif
					if (!bJumpSuccess)
					{
						pNewUnit->kill(false);
						break;
					}
				}
			}
			// END TERRA COTTA AWESOME
#if defined(MOD_BALANCE_CORE)
			if(MOD_BALANCE_CORE && pBuildingInfo->IsReformation() && (iChange > 0) && bFirst)
			{
				GET_PLAYER(getOwner()).SetReformation(true);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GrantsRandomResourceTerritory() > 0) && (iChange > 0) && bFirst)
			{
				if(!GET_PLAYER(getOwner()).GetPlayerTraits()->GetUniqueLuxuryCities())
				{
					// Does this building add resources?
					int iNumResource = pBuildingInfo->GrantsRandomResourceTerritory();
					if(iNumResource != 0)
					{
						// Loop through all resources and see if we can find this many unique ones
						ResourceTypes eResourceToGive = NO_RESOURCE;
						int iBestFlavor = 0;
						for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
						{
							ResourceTypes eResource = (ResourceTypes) iResourceLoop;
							CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
							if (pkResource != NULL && pkResource->GetRequiredCivilization() == owningPlayer.getCivilizationType())
							{
								int iRandomFlavor = GC.getGame().getJonRandNum(100, "Resource Flavor");
								//If we've already got this resource, divide the value by the amount.
								if(owningPlayer.getNumResourceTotal(eResource, false) > 0)
								{
									iRandomFlavor /= owningPlayer.getNumResourceTotal(eResource, false);
									iRandomFlavor += 1;
								}
								if(iRandomFlavor > iBestFlavor)
								{
									eResourceToGive = eResource;
									iBestFlavor = iRandomFlavor;
								}
							}
						}

						if (eResourceToGive != NO_RESOURCE)
						{
							int iNumResourceGiven = 0;
							int iNumResourceTotal = iNumResource;
							CvPlot* pLoopPlot;

							for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
							{
								pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);
#if defined(MOD_PSEUDO_NATURAL_WONDER)
								if(pLoopPlot != NULL && pLoopPlot->getOwner() == owningPlayer.GetID() && !pLoopPlot->isCity() && 
									pLoopPlot->isValidMovePlot(getOwner()) && !pLoopPlot->isWater() && !pLoopPlot->IsNaturalWonder(true) && !pLoopPlot->isMountain() && (pLoopPlot->getFeatureType() == NO_FEATURE))
#else
								if( pLoopPlot != NULL && pLoopPlot->getOwner() == owningPlayer.GetID() && !pLoopPlot->isCity() && 
									pLoopPlot->isValidMovePlot(pCity->getOwner()) && !pLoopPlot->isWater() && !pLoopPlot->IsNaturalWonder()  && !pLoopPlot->isMountain() && (pLoopPlot->getFeatureType() == NO_FEATURE))
#endif
								{
									if(pLoopPlot->getResourceType() == NO_RESOURCE && pLoopPlot->getImprovementType() == NO_IMPROVEMENT)
									{
										pLoopPlot->setResourceType(NO_RESOURCE, 0, false);
										pLoopPlot->setResourceType(eResourceToGive, 1, false);
										iNumResourceGiven++;
										if(iNumResourceGiven >= iNumResourceTotal)
										{
											break;
										}
									}
								}
							}
							if(iNumResourceGiven < iNumResourceTotal)
							{
								for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
								{
									pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);
#if defined(MOD_PSEUDO_NATURAL_WONDER)
									if( pLoopPlot != NULL && (pLoopPlot->getOwner() == NO_PLAYER) && pLoopPlot->isValidMovePlot(getOwner()) && 
										!pLoopPlot->isWater() && !pLoopPlot->IsNaturalWonder(true) && (pLoopPlot->getFeatureType() != FEATURE_OASIS))
#else
									if( pLoopPlot != NULL && (pLoopPlot->getOwner() == NO_PLAYER) && pLoopPlot->isValidMovePlot(getOwner()) && 
										!pLoopPlot->isWater() && !pLoopPlot->IsNaturalWonder() && (pLoopPlot->getFeatureType() != FEATURE_OASIS))
#endif
									{
										if(pLoopPlot->getResourceType() == NO_RESOURCE && pLoopPlot->getImprovementType() == NO_IMPROVEMENT)
										{
											pLoopPlot->setResourceType(NO_RESOURCE, 0, false);
											pLoopPlot->setResourceType(eResourceToGive, 1, false);
											iNumResourceGiven++;
											if(iNumResourceGiven >= iNumResourceTotal)
											{
												break;
											}
										}
									}
								}
							}
							if(iNumResourceGiven < iNumResourceTotal)
							{
								if(plot()->getResourceType() == NO_RESOURCE)
								{
									plot()->setResourceType(NO_RESOURCE, 0, false);
									plot()->setResourceType(eResourceToGive, iNumResourceGiven, false);
								}
								else
								{
									if (eResourceToGive != NO_RESOURCE)
									{
										owningPlayer.changeNumResourceTotal(eResourceToGive, iNumResource);
									}
								}
							}
						}
					}
				}
				else
				{
					GET_PLAYER(getOwner()).GetPlayerTraits()->AddUniqueLuxuriesAround(this, pBuildingInfo->GrantsRandomResourceTerritory() * iChange);
				}
			}
#endif
#if defined(MOD_BALANCE_CORE_BUILDING_INSTANT_YIELD)
			if(MOD_BALANCE_CORE_BUILDING_INSTANT_YIELD && (iChange > 0))
			{
				if(!IsBuildingConstructed(eBuildingClass))
				{
					SetBuildingConstructed(eBuildingClass, true);
					GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_INSTANT, false, NO_GREATPERSON, eBuilding, 0, true, NO_PLAYER, NULL, false, this);
				}
			}
#endif
		}

		if(pBuildingInfo->GetTrainedFreePromotion() != NO_PROMOTION)
		{
			changeFreePromotionCount(((PromotionTypes)(pBuildingInfo->GetTrainedFreePromotion())), iChange);
#if defined(MOD_BALANCE_CORE)
			//Let's give this to all units from this city.
			if (MOD_BALANCE_RETROACTIVE_PROMOS)
			{
				CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo((PromotionTypes)pBuildingInfo->GetTrainedFreePromotion());
				if (pkPromotionInfo)
				{
					int iUnitLoop;
					CvUnit* pLoopUnit = NULL;
					for (pLoopUnit = GetPlayer()->firstUnit(&iUnitLoop); pLoopUnit != NULL; pLoopUnit = GetPlayer()->nextUnit(&iUnitLoop))
					{
						if(pLoopUnit->getOriginCity() == NULL)
						{
							if(this == GetPlayer()->getCapitalCity())
							{
								if (((pLoopUnit->getUnitCombatType() != NO_UNITCOMBAT) && pkPromotionInfo->GetUnitCombatClass(pLoopUnit->getUnitCombatType())) || ::IsPromotionValidForCivilianUnitType((PromotionTypes)pBuildingInfo->GetTrainedFreePromotion(), pLoopUnit->getUnitType()))
								{
									pLoopUnit->setHasPromotion((PromotionTypes)pBuildingInfo->GetTrainedFreePromotion(), true);
								}
							}
						}
						else if (pLoopUnit->getOriginCity() != this)
							continue;

						if (((pLoopUnit->getUnitCombatType() != NO_UNITCOMBAT) && pkPromotionInfo->GetUnitCombatClass(pLoopUnit->getUnitCombatType())) || ::IsPromotionValidForCivilianUnitType((PromotionTypes)pBuildingInfo->GetTrainedFreePromotion(), pLoopUnit->getUnitType()))
						{
							pLoopUnit->setHasPromotion((PromotionTypes)pBuildingInfo->GetTrainedFreePromotion(), true);
						}
					}
				}
			}
#endif
		}
#if defined(MOD_BALANCE_CORE)
		GetCityBuildings()->ChangeBuildingDefenseMod(pBuildingInfo->GetBuildingDefenseModifier() * iChange);
#endif
		changeGreatPeopleRateModifier(pBuildingInfo->GetGreatPeopleRateModifier() * iChange);
		changeFreeExperience(pBuildingInfo->GetFreeExperience() * iChange);
		ChangeMaxAirUnits(pBuildingInfo->GetAirModifier() * iChange);
		changeNukeModifier(pBuildingInfo->GetNukeModifier() * iChange);
		changeHealRate(pBuildingInfo->GetHealRateChange() * iChange);
		ChangeExtraHitPoints(pBuildingInfo->GetExtraCityHitPoints() * iChange);

		ChangeNoOccupiedUnhappinessCount(pBuildingInfo->IsNoOccupiedUnhappiness() * iChange);

#if !defined(MOD_API_EXTENSIONS)
		// Trust the modder if they set a building to negative happiness
		if(pBuildingInfo->GetHappiness() > 0)
#endif
		{
			ChangeBaseHappinessFromBuildings(pBuildingInfo->GetHappiness() * iChange);
		}

#if !defined(MOD_API_EXTENSIONS)
		// Trust the modder if they set a building to negative global happiness
		if(pBuildingInfo->GetUnmoddedHappiness() > 0)
#endif
		{
			ChangeUnmoddedHappinessFromBuildings(pBuildingInfo->GetUnmoddedHappiness() * iChange);
		}

		if(pBuildingInfo->GetUnhappinessModifier() != 0)
		{
			owningPlayer.ChangeUnhappinessMod(pBuildingInfo->GetUnhappinessModifier() * iChange);
		}
#if defined(MOD_BALANCE_CORE)
		if (pBuildingInfo->GetLocalUnhappinessModifier() != 0)
		{
			ChangeLocalUnhappinessMod(pBuildingInfo->GetLocalUnhappinessModifier() * iChange);
		}
#endif
		int iBuildingCulture = pBuildingInfo->GetYieldChange(YIELD_CULTURE);
		if(iBuildingCulture != 0)
		{
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
			iBuildingCulture = owningPlayer.GetPlayerTraits()->GetCultureBuildingYieldChange();
#else
			iBuildingCulture += owningPlayer.GetPlayerTraits()->GetCultureBuildingYieldChange();
#endif
		}
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
		ChangeBaseYieldRateFromBuildings(YIELD_CULTURE, iBuildingCulture * iChange);
#else
		ChangeJONSCulturePerTurnFromBuildings(iBuildingCulture * iChange);
#endif
		changeCultureRateModifier(pBuildingInfo->GetCultureRateModifier() * iChange);
		changePlotCultureCostModifier(pBuildingInfo->GetPlotCultureCostModifier() * iChange);
		changePlotBuyCostModifier(pBuildingInfo->GetPlotBuyCostModifier() * iChange);
#if defined(MOD_BUILDINGS_CITY_WORKING)
		changeCityWorkingChange(pBuildingInfo->GetCityWorkingChange() * iChange);
#endif

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
		int iBuildingFaith = pBuildingInfo->GetYieldChange(YIELD_FAITH);
		ChangeFaithPerTurnFromBuildings(iBuildingFaith * iChange);
#endif
		m_pCityReligions->ChangeReligiousPressureModifier(pBuildingInfo->GetReligiousPressureModifier() * iChange);

		PolicyTypes ePolicy;
		for(int iPolicyLoop = 0; iPolicyLoop < GC.getNumPolicyInfos(); iPolicyLoop++)
		{
			ePolicy = (PolicyTypes) iPolicyLoop;

			if(owningPlayer.GetPlayerPolicies()->HasPolicy(ePolicy) && !owningPlayer.GetPlayerPolicies()->IsPolicyBlocked(ePolicy))
			{
				ChangeJONSCulturePerTurnFromPolicies(GC.getPolicyInfo(ePolicy)->GetBuildingClassCultureChange(eBuildingClass) * iChange);
#if defined(MOD_BALANCE_CORE_POLICIES)
				changeBuildingClassCultureChange(eBuildingClass, GC.getPolicyInfo(ePolicy)->GetBuildingClassCultureChange(eBuildingClass) * iChange);
#endif
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)				
				//I don't believe we need this, if it is being defined in cvplayer already...
				ChangeFaithPerTurnFromPolicies(GC.getPolicyInfo(ePolicy)->GetBuildingClassYieldChanges(eBuildingClass, YIELD_FAITH) * iChange);
#endif
			}
		}

#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && pBuildingInfo->GetPovertyHappinessChangeBuilding() != 0)
		{
			ChangePovertyUnhappiness(pBuildingInfo->GetPovertyHappinessChangeBuilding() * iChange);
		}
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && pBuildingInfo->GetDefenseHappinessChangeBuilding() != 0)
		{
			ChangeDefenseUnhappiness(pBuildingInfo->GetDefenseHappinessChangeBuilding() * iChange);
		}
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && pBuildingInfo->GetIlliteracyHappinessChangeBuilding() != 0)
		{
			ChangeIlliteracyUnhappiness(pBuildingInfo->GetIlliteracyHappinessChangeBuilding() * iChange);
		}
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && pBuildingInfo->GetUnculturedHappinessChangeBuilding() != 0)
		{
			ChangeUnculturedUnhappiness(pBuildingInfo->GetUnculturedHappinessChangeBuilding() * iChange);
		}
		if(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS && pBuildingInfo->GetMinorityHappinessChangeBuilding() != 0)
		{
			ChangeMinorityUnhappiness(pBuildingInfo->GetMinorityHappinessChangeBuilding() * iChange);
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(MOD_BALANCE_CORE && (pBuildingInfo->GetTradeReligionModifier() != 0))
		{
			ChangeReligiousTradeModifier(pBuildingInfo->GetTradeReligionModifier() * iChange);
		}
		if(MOD_BALANCE_CORE && (pBuildingInfo->GetBorderObstacleCity() > 0))
		{
			ChangeBorderObstacleCity(pBuildingInfo->GetBorderObstacleCity() * iChange);
		}
		if(MOD_BALANCE_CORE && (pBuildingInfo->GetBorderObstacleWater() > 0))
		{
			ChangeBorderObstacleWater(pBuildingInfo->GetBorderObstacleWater() * iChange);
		}
		if(bFirst && (iChange > 0) && (pBuildingInfo->GetWLTKDTurns() > 0))
		{
			int iWLTKD = pBuildingInfo->GetWLTKDTurns();
			iWLTKD *= GC.getGame().getGameSpeedInfo().getTrainPercent();
			iWLTKD /= 100;
			if(iWLTKD > 0)
			{
				ChangeWeLoveTheKingDayCounter(iWLTKD);
			}
		}
		if(MOD_BALANCE_CORE && pBuildingInfo->GetLandTourismEnd() > 0)
		{
			ChangeLandTourismBonus(pBuildingInfo->GetLandTourismEnd() * iChange);
		}
		if(MOD_BALANCE_CORE && pBuildingInfo->GetSeaTourismEnd() > 0)
		{
			ChangeSeaTourismBonus(pBuildingInfo->GetSeaTourismEnd() * iChange);
		}
		if(MOD_BALANCE_CORE && pBuildingInfo->GetAlwaysHeal() > 0)
		{
			ChangeAlwaysHeal(pBuildingInfo->GetAlwaysHeal() * iChange);
		}
		if(bFirst && iChange > 0 && pBuildingInfo->GetNumFreeArtifacts() > 0)
		{
			for(int iI = 0; iI < pBuildingInfo->GetNumFreeArtifacts(); iI++)
			{
				if(iI <= pBuildingInfo->GetGreatWorkCount())
				{
					if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetWonderProductionModifier() > 0)
					{
						plot()->AddArchaeologicalRecord(CvTypes::getARTIFACT_SARCOPHAGUS(), GET_PLAYER(getOwner()).GetCurrentEra(), getOwner(), NO_PLAYER);
					}
					else
					{
						plot()->AddArchaeologicalRecord(CvTypes::getARTIFACT_ANCIENT_RUIN(), GET_PLAYER(getOwner()).GetCurrentEra(), getOwner(), NO_PLAYER);
					}
					GreatWorkType eGreatArtifact = CultureHelpers::GetArtifact(plot());
					if(eGreatArtifact != NO_GREAT_WORK)
					{
						GreatWorkClass eClass = CultureHelpers::GetGreatWorkClass(eGreatArtifact);
						if(eClass != NO_GREAT_WORK_CLASS)
						{
							int iGWindex = 	GC.getGame().GetGameCulture()->CreateGreatWork(eGreatArtifact, eClass, plot()->GetArchaeologicalRecord().m_ePlayer1, plot()->GetArchaeologicalRecord().m_eEra, "");
							if(iGWindex != -1)
							{
								GetCityBuildings()->SetBuildingGreatWork(eBuildingClass, iI, iGWindex);
							}
						}
					}
				}
			}
		}
		// Hurries
		for(int iI = 0; iI < GC.getNumHurryInfos(); iI++)
		{
			changeHurryModifier((HurryTypes) iI, (pBuildingInfo->GetHurryModifierLocal(iI) * iChange));
		}
		for (int iI = 0; iI < GC.getNumSpecialistInfos(); iI++)
		{
			for (int iJ = 0; iJ < NUM_YIELD_TYPES; iJ++)
			{
				changeSpecialistExtraYield(((SpecialistTypes)iI), ((YieldTypes)iJ), (pBuildingInfo->GetSpecialistYieldChangeLocal(iI, iJ) * iChange));
			}
		}

		//Trade Routes
		if(pBuildingInfo->GetTradeRouteLandDistanceModifier() != 0)
		{
			ChangeTradeRouteLandDistanceModifier(pBuildingInfo->GetTradeRouteLandDistanceModifier() * iChange);
		}
		if(pBuildingInfo->GetTradeRouteSeaDistanceModifier() != 0)
		{
			ChangeTradeRouteSeaDistanceModifier(pBuildingInfo->GetTradeRouteSeaDistanceModifier() * iChange);
		}
#endif
#if defined(MOD_BALANCE_CORE_SPIES)
		if(MOD_BALANCE_CORE_SPIES)
		{
			if(pBuildingInfo->GetBlockBuildingDestruction() > 0)
			{
				ChangeBlockBuildingDestruction(pBuildingInfo->GetBlockBuildingDestruction() * iChange);
			}
			if(pBuildingInfo->GetBlockWWDestruction() > 0)
			{
				ChangeBlockWWDestruction(pBuildingInfo->GetBlockWWDestruction() * iChange);
			}
			if(pBuildingInfo->GetBlockUDestruction() > 0)
			{
				ChangeBlockUDestruction(pBuildingInfo->GetBlockUDestruction() * iChange);
			}
			if(pBuildingInfo->GetBlockGPDestruction() > 0)
			{
				ChangeBlockGPDestruction(pBuildingInfo->GetBlockGPDestruction() * iChange);
			}
			if(pBuildingInfo->GetBlockRebellion() > 0)
			{
				ChangeBlockRebellion(pBuildingInfo->GetBlockRebellion() * iChange);
			}
			if(pBuildingInfo->GetBlockUnrest() > 0)
			{
				ChangeBlockUnrest(pBuildingInfo->GetBlockUnrest() * iChange);
			}
			if(pBuildingInfo->GetBlockScience() > 0)
			{
				ChangeBlockScience(pBuildingInfo->GetBlockScience() * iChange);
			}
			if(pBuildingInfo->GetBlockGold() > 0)
			{
				ChangeBlockGold(pBuildingInfo->GetBlockGold() * iChange);
			}
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if (pBuildingInfo->GetBuildingClassInfo().IsOffice() || pBuildingInfo->GetBuildingClassInfo().IsHeadquarters() || pBuildingInfo->GetBuildingClassInfo().IsFranchise())
		{
			for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
			{
				UpdateYieldFromCorporationFranchises((YieldTypes)iI);
			}
		}
		if (pBuildingInfo->GetSpecialistType() != NO_SPECIALIST && pBuildingInfo->GetSpecialistCount() > 0)
		{
			GetCityCitizens()->ChangeNumSpecialistSlots((SpecialistTypes)pBuildingInfo->GetSpecialistType(), (pBuildingInfo->GetSpecialistCount() * iChange));
		}
#endif
		changeMaxFoodKeptPercent(pBuildingInfo->GetFoodKept() * iChange);
		changeMilitaryProductionModifier(pBuildingInfo->GetMilitaryProductionModifier() * iChange);
		changeSpaceProductionModifier(pBuildingInfo->GetSpaceProductionModifier() * iChange);
		m_pCityBuildings->ChangeBuildingProductionModifier(pBuildingInfo->GetBuildingProductionModifier() * iChange);
		m_pCityBuildings->ChangeMissionaryExtraSpreads(pBuildingInfo->GetExtraMissionarySpreads() * iChange);
		m_pCityBuildings->ChangeLandmarksTourismPercent(pBuildingInfo->GetLandmarksTourismPercent() * iChange);
		m_pCityBuildings->ChangeGreatWorksTourismModifier(pBuildingInfo->GetGreatWorksTourismModifier() * iChange);
		ChangeWonderProductionModifier(pBuildingInfo->GetWonderProductionModifier() * iChange);
		changeCapturePlunderModifier(pBuildingInfo->GetCapturePlunderModifier() * iChange);
		ChangeEspionageModifier(pBuildingInfo->GetEspionageModifier() * iChange);
#if defined(MOD_RELIGION_CONVERSION_MODIFIERS)
		ChangeConversionModifier(pBuildingInfo->GetConversionModifier() * iChange);
		owningPlayer.ChangeConversionModifier(pBuildingInfo->GetGlobalConversionModifier() * iChange);
#endif

		ChangeTradeRouteTargetBonus(pBuildingInfo->GetTradeRouteTargetBonus() * iChange);
		ChangeTradeRouteSeaGoldBonus(pBuildingInfo->GetTradeRouteSeaGoldBonus() * iChange);
		ChangeTradeRouteLandGoldBonus(pBuildingInfo->GetTradeRouteLandGoldBonus() * iChange);
		ChangeTradeRouteRecipientBonus(pBuildingInfo->GetTradeRouteRecipientBonus() * iChange);
		ChangeNumTradeRouteBonus(pBuildingInfo->GetTradeRouteLandGoldBonus() * iChange);
		

		if (pBuildingInfo->AffectSpiesNow() && iChange > 0)
		{
			for (uint ui = 0; ui < MAX_MAJOR_CIVS; ui++)
			{
				PlayerTypes ePlayer = (PlayerTypes)ui;
				GET_PLAYER(ePlayer).GetEspionage()->UpdateCity(this);
			}
		}

		// Resource loop
		int iCulture, iFaith;
		ResourceTypes eResource;
		for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
		{
			eResource = (ResourceTypes) iResourceLoop;

			// Does this building add resources?
			int iNumResource = pBuildingInfo->GetResourceQuantity(iResourceLoop) * iChange;
			if(iNumResource != 0)
			{
				owningPlayer.changeNumResourceTotal(eResource, iNumResource);
			}

#if defined(MOD_BALANCE_CORE)
			int iNumResourcePlotsGiven = 0;
			int iNumResourceTotalPlots = pBuildingInfo->GetNumResourcesToPlace(iResourceLoop);
			if(pBuildingInfo->GetNumResourcesToPlace(iResourceLoop) > 0)
			{
				//const ResourceTypes eResourceToPlace = static_cast<ResourceTypes>(iResourceLoop);
				CvPlot* pLoopPlot;
				for(int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
				{
					pLoopPlot = iterateRingPlots(getX(), getY(), iCityPlotLoop);
					if(pLoopPlot != NULL && ((pLoopPlot->getOwner() == owningPlayer.GetID()) || (pLoopPlot->getOwner() == NO_PLAYER && pLoopPlot->isValidMovePlot(getOwner()))) && !pLoopPlot->isCity())
					{
						if(pLoopPlot->canHaveResource(eResource, false, true) && pLoopPlot->getResourceType() == NO_RESOURCE)
						{
							pLoopPlot->setResourceType(NO_RESOURCE, 0, false);
							pLoopPlot->setResourceType(eResource, pBuildingInfo->GetResourceQuantityToPlace(), false);
							iNumResourcePlotsGiven++;
							if(pLoopPlot->getImprovementType() != NO_IMPROVEMENT && !pLoopPlot->IsImprovementPillaged())
							{
								CvImprovementEntry* ImprovementEntry = GC.getImprovementInfo(pLoopPlot->getImprovementType());
								{
									if(ImprovementEntry)
									{
										if(ImprovementEntry->IsImprovementResourceMakesValid(eResource))
										{
											owningPlayer.changeNumResourceTotal(eResource, pBuildingInfo->GetResourceQuantityToPlace());
										}
									}
								}
							}
							if(iNumResourcePlotsGiven >= iNumResourceTotalPlots)
							{
								break;
							}
							if(pLoopPlot->getOwner() == GC.getGame().getActivePlayer())
							{
								if(!CvPreGame::loadWBScenario() || GC.getGame().getGameTurn() > 0)
								{
									CvString strBuffer;
									CvResourceInfo* pResourceInfo = GC.getResourceInfo(eResource);
									CvAssert(pResourceInfo);
									NotificationTypes eNotificationType = NO_NOTIFICATION_TYPE;
									strBuffer = GetLocalizedText("TXT_KEY_NOTIFICATION_FOUND_RESOURCE", pResourceInfo->GetTextKey());
									
									CvString strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_SUMMARY_FOUND_RESOURCE", pResourceInfo->GetTextKey());

									switch(pResourceInfo->getResourceUsage())
									{
										case RESOURCEUSAGE_LUXURY:
											eNotificationType = NOTIFICATION_DISCOVERED_LUXURY_RESOURCE;
											break;
										case RESOURCEUSAGE_STRATEGIC:
											eNotificationType = NOTIFICATION_DISCOVERED_STRATEGIC_RESOURCE;
											break;
										case RESOURCEUSAGE_BONUS:
											eNotificationType = NOTIFICATION_DISCOVERED_BONUS_RESOURCE;
											break;
									}

									CvNotifications* pNotifications = GET_PLAYER(pLoopPlot->getOwner()).GetNotifications();
									if(pNotifications)
									{
										pNotifications->Add(eNotificationType, strBuffer, strSummary, pLoopPlot->getX(), pLoopPlot->getY(), eResource);
									}
								}
							}
						}
					}
				}
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetResourceQuantityPerXFranchises(iResourceLoop) > 0))
			{
				ChangeResourceQuantityPerXFranchises(eResource, pBuildingInfo->GetResourceQuantityPerXFranchises(iResourceLoop) * iChange);
			}
#endif

			// Do we have this resource local?
			if(IsHasResourceLocal(eResource, /*bTestVisible*/ false))
			{
				// Our Building does give culture with eResource
				iCulture = GC.getBuildingInfo(eBuilding)->GetResourceCultureChange(eResource);

				if(iCulture != 0)
				{
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					ChangeBaseYieldRateFromBuildings(YIELD_CULTURE, iCulture * m_paiNumResourcesLocal[eResource]);
#else
					ChangeJONSCulturePerTurnFromBuildings(iCulture * m_paiNumResourcesLocal[eResource]);
#endif
				}

				// What about faith?
				iFaith = GC.getBuildingInfo(eBuilding)->GetResourceFaithChange(eResource);

				if(iFaith != 0)
				{
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					ChangeBaseYieldRateFromBuildings(YIELD_FAITH, iFaith * m_paiNumResourcesLocal[eResource]);
#else
					ChangeFaithPerTurnFromBuildings(iFaith * m_paiNumResourcesLocal[eResource]);
#endif
				}
			}
		}

		if(pBuildingInfo->IsExtraLuxuries())
		{
			CvPlot* pLoopPlot;

			// Subtract off old luxury counts

			for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
			{
				pLoopPlot = iterateRingPlots(getX(), getY(), iJ);

				if(pLoopPlot != NULL && pLoopPlot->getOwner() == getOwner())
				{
					ResourceTypes eLoopResource = pLoopPlot->getResourceType();
					if(eLoopResource != NO_RESOURCE && GC.getResourceInfo(eLoopResource)->getResourceUsage() == RESOURCEUSAGE_LUXURY)
					{
						if(owningTeam.GetTeamTechs()->HasTech((TechTypes) GC.getResourceInfo(eLoopResource)->getTechCityTrade()))
						{
#if defined(MOD_BALANCE_CORE)
							if(pLoopPlot == plot() || (pLoopPlot->getImprovementType() != NO_IMPROVEMENT && (GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsCreatedByGreatPerson() || GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsAdjacentCity() || GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsImprovementResourceTrade(eLoopResource))))
#else
							if(pLoopPlot == plot() || (pLoopPlot->getImprovementType() != NO_IMPROVEMENT && GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsImprovementResourceTrade(eLoopResource)))
#endif
							{
								if(!pLoopPlot->IsImprovementPillaged())
								{
									owningPlayer.changeNumResourceTotal(pLoopPlot->getResourceType(), -pLoopPlot->getNumResourceForPlayer(getOwner()), /*bIgnoreResourceWarning*/ true);
								}
							}
						}
					}
				}
			}

			ChangeExtraLuxuryResources(iChange);

			// Add in new luxury counts

			for(int iJ = 0; iJ < GetNumWorkablePlots(); iJ++)
			{
				pLoopPlot = iterateRingPlots(getX(), getY(), iJ);

				if(pLoopPlot != NULL && pLoopPlot->getOwner() == getOwner())
				{
					ResourceTypes eLoopResource = pLoopPlot->getResourceType();
					if(eLoopResource != NO_RESOURCE && GC.getResourceInfo(eLoopResource)->getResourceUsage() == RESOURCEUSAGE_LUXURY)
					{
						if(owningTeam.GetTeamTechs()->HasTech((TechTypes) GC.getResourceInfo(eLoopResource)->getTechCityTrade()))
						{
#if defined(MOD_BALANCE_CORE)
							if(pLoopPlot == plot() || (pLoopPlot->getImprovementType() != NO_IMPROVEMENT && (GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsCreatedByGreatPerson() || GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsAdjacentCity() || GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsImprovementResourceTrade(eLoopResource))))
#else
							if(pLoopPlot == plot() || (pLoopPlot->getImprovementType() != NO_IMPROVEMENT && GC.getImprovementInfo(pLoopPlot->getImprovementType())->IsImprovementResourceTrade(eLoopResource)))
#endif
							{
								if(!pLoopPlot->IsImprovementPillaged())
								{
									owningPlayer.changeNumResourceTotal(pLoopPlot->getResourceType(), pLoopPlot->getNumResourceForPlayer(getOwner()));
								}
							}
						}
					}
				}
			}
		}

#if defined(MOD_API_UNIFIED_YIELDS)
		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
#endif
#if defined(MOD_BALANCE_CORE)
		for(int iL = 0; iL < GC.getNumSpecialistInfos(); iL++)
		{
			SpecialistTypes eSpecialist = (SpecialistTypes)iL;
			CvSpecialistInfo* pSpecialistInfo = GC.getSpecialistInfo((SpecialistTypes)iL);
			if(pSpecialistInfo)
			{
				 int iValue = pBuildingInfo->GetSpecificGreatPersonRateModifier((SpecialistTypes)iL);
				 if(iValue > 0)
				 {
					ChangeSpecialistRateModifier(eSpecialist, (pBuildingInfo->GetSpecificGreatPersonRateModifier((SpecialistTypes)iL) * iChange));
				 }
			}
		}
#endif
		YieldTypes eYield;

		for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			eYield = (YieldTypes) iI;

#if defined(MOD_DIPLOMACY_CITYSTATES) || defined(MOD_BALANCE_CORE)
			//Yield from growth
			if((MOD_DIPLOMACY_CITYSTATES || MOD_BALANCE_CORE) && pBuildingInfo->GetGrowthExtraYield(eYield) != 0)
			{
				ChangeGrowthExtraYield(eYield, pBuildingInfo->GetGrowthExtraYield(eYield) * iChange);
			}
#endif
#if defined(MOD_BALANCE_CORE)
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromVictory(eYield) > 0))
			{
				ChangeYieldFromVictory(eYield, pBuildingInfo->GetYieldFromVictory(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetGoldenAgeYieldMod(eYield) > 0))
			{
				ChangeGoldenAgeYieldMod(eYield, pBuildingInfo->GetGoldenAgeYieldMod(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromWLTKD(eYield) > 0))
			{
				ChangeYieldFromWLTKD(eYield, pBuildingInfo->GetYieldFromWLTKD(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromConstruction(eYield) > 0))
			{
				ChangeYieldFromConstruction(eYield, pBuildingInfo->GetYieldFromConstruction(eYield) * iChange);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromTech(eYield) > 0))
			{
				ChangeYieldFromTech(eYield, pBuildingInfo->GetYieldFromTech(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetScienceFromYield(eYield) > 0))
			{
				ChangeBuildingScienceFromYield(eYield, (pBuildingInfo->GetScienceFromYield(eYield) * iChange));
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetThemingYieldBonus(eYield) > 0))
			{
				ChangeThemingYieldBonus(eYield, pBuildingInfo->GetThemingYieldBonus(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromBirth(eYield) > 0))
			{
				ChangeYieldFromBirth(eYield, pBuildingInfo->GetYieldFromBirth(eYield) * iChange);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromUnitProduction(eYield) > 0))
			{
				ChangeYieldFromUnitProduction(eYield, pBuildingInfo->GetYieldFromUnitProduction(eYield) * iChange);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromBorderGrowth(eYield) > 0))
			{
				ChangeYieldFromBorderGrowth(eYield, pBuildingInfo->GetYieldFromBorderGrowth(eYield) * iChange);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromPolicyUnlock(eYield) > 0))
			{
				ChangeYieldFromPolicyUnlock(eYield, pBuildingInfo->GetYieldFromPolicyUnlock(eYield) * iChange);
			}
			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromPurchase(eYield) > 0))
			{
				ChangeYieldFromPurchase(eYield, pBuildingInfo->GetYieldFromPurchase(eYield) * iChange);
			}

			if(MOD_BALANCE_CORE && (pBuildingInfo->GetYieldFromUnitLevelUp(eYield) > 0))
			{
				ChangeYieldFromUnitLevelUp(eYield, pBuildingInfo->GetYieldFromUnitLevelUp(eYield) * iChange);
			}

			ChangeYieldPerAlly(eYield, pBuildingInfo->GetYieldPerAlly(eYield) * iChange);
			ChangeYieldPerFriend(eYield, pBuildingInfo->GetYieldPerFriend(eYield) * iChange);
#endif
#if defined(MOD_BALANCE_CORE_EVENTS)
			if(GetEventBuildingClassCityYield(eBuildingClass, eYield) != 0)
			{
				ChangeBaseYieldRateFromBuildings(eYield, GetEventBuildingClassCityYield(eBuildingClass, eYield) * iChange);
			}
			if(GetEventBuildingClassCityYieldModifier(eBuildingClass, eYield) != 0)
			{
				changeYieldRateModifier(eYield, GetEventBuildingClassCityYieldModifier(eBuildingClass, eYield) * iChange);
			}
#endif
			changeSeaPlotYield(eYield, (pBuildingInfo->GetSeaPlotYieldChange(eYield) * iChange));
			changeRiverPlotYield(eYield, (pBuildingInfo->GetRiverPlotYieldChange(eYield) * iChange));
			changeLakePlotYield(eYield, (pBuildingInfo->GetLakePlotYieldChange(eYield) * iChange));
			changeSeaResourceYield(eYield, (pBuildingInfo->GetSeaResourceYieldChange(eYield) * iChange));
			ChangeBaseYieldRateFromBuildings(eYield, ((pBuildingInfo->GetYieldChange(eYield) + m_pCityBuildings->GetBuildingYieldChange(eBuildingClass, eYield)) * iChange));
			ChangeYieldPerPopTimes100(eYield, pBuildingInfo->GetYieldChangePerPop(eYield) * iChange);
			ChangeYieldPerReligionTimes100(eYield, pBuildingInfo->GetYieldChangePerReligion(eYield) * iChange);
			changeYieldRateModifier(eYield, (pBuildingInfo->GetYieldModifier(eYield) * iChange));

			CvPlayerPolicies* pPolicies = GET_PLAYER(getOwner()).GetPlayerPolicies();
			changeYieldRateModifier(eYield, pPolicies->GetBuildingClassYieldModifier(eBuildingClass, eYield) * iChange);
			ChangeBaseYieldRateFromBuildings(eYield, pPolicies->GetBuildingClassYieldChange(eBuildingClass, eYield) * iChange);
#if defined(MOD_API_UNIFIED_YIELDS)
			ChangeBaseYieldRateFromBuildings(eYield, GET_PLAYER(getOwner()).GetPlayerTraits()->GetBuildingClassYieldChange(eBuildingClass, eYield) * iChange);
#endif
#if defined(MOD_BALANCE_CORE)
			// Building modifiers
			BuildingClassTypes eBuildingClassLocal;
			for(int iJ = 0; iJ < GC.getNumBuildingClassInfos(); iJ++)
			{
				eBuildingClassLocal = (BuildingClassTypes) iJ;

				CvBuildingClassInfo* pkBuildingClassLocalInfo = GC.getBuildingClassInfo(eBuildingClassLocal);
				if(!pkBuildingClassLocalInfo)
				{
					continue;
				}

				BuildingTypes eLocalBuilding = (BuildingTypes) getCivilizationInfo().getCivilizationBuildings(eBuildingClassLocal);

				if(eLocalBuilding != NO_BUILDING)
				{
					CvBuildingEntry* pkLocalBuilding = GC.getBuildingInfo(eLocalBuilding);
					if(pkLocalBuilding)
					{
						int iYieldChange = pBuildingInfo->GetBuildingClassLocalYieldChange(iJ, iI);
						if(iYieldChange != 0)
						{
							m_pCityBuildings->ChangeBuildingYieldChange(eBuildingClassLocal, eYield, (iYieldChange * iChange));
							changeLocalBuildingClassYield(eBuildingClassLocal, eYield, (iYieldChange * iChange));
						}
					}
				}
			}
			CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
			if (eCorporation != NO_CORPORATION)
			{
				CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
				if (pkCorporationInfo)
				{
					ChangeBaseYieldRateFromBuildings(eYield,pkCorporationInfo->GetBuildingClassYieldChange(eBuildingClass, eYield) * iChange);
				}
			}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
			//Policy-Religion Fusion Yield Changes
			if(MOD_BALANCE_CORE_POLICIES)
			{
				changeReligionBuildingYieldRateModifier(eBuildingClass, eYield, (pPolicies->GetReligionBuildingClassYieldModifier(eBuildingClass, eYield) * iChange));
			}
#endif

#if defined(MOD_API_UNIFIED_YIELDS)
			if (pkBuildingClassInfo && isWorldWonderClass(*pkBuildingClassInfo))
			{
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
				if(eYield == YIELD_CULTURE)
				{
					ChangeJONSCulturePerTurnFromBuildings(GetPlayer()->GetYieldChangeWorldWonder(eYield) * iChange);
					ChangeJONSCulturePerTurnFromBuildings(GetPlayer()->GetPlayerTraits()->GetYieldChangeWorldWonder(eYield) * iChange);
				}
				else if(eYield == YIELD_FAITH)
				{
					ChangeFaithPerTurnFromBuildings(GetPlayer()->GetYieldChangeWorldWonder(eYield) * iChange);
					ChangeFaithPerTurnFromBuildings(GetPlayer()->GetPlayerTraits()->GetYieldChangeWorldWonder(eYield) * iChange);
				}
				else
#endif
				{
					ChangeBaseYieldRateFromBuildings(eYield, GetPlayer()->GetYieldChangeWorldWonder(eYield) * iChange);
					ChangeBaseYieldRateFromBuildings(eYield, GetPlayer()->GetPlayerTraits()->GetYieldChangeWorldWonder(eYield) * iChange);
				}
			}
#endif

			for(int iJ = 0; iJ < GC.getNumResourceInfos(); iJ++)
			{
				ChangeResourceExtraYield(((ResourceTypes)iJ), eYield, (GC.getBuildingInfo(eBuilding)->GetResourceYieldChange(iJ, eYield) * iChange));
			}

			for(int iJ = 0; iJ < GC.getNumFeatureInfos(); iJ++)
			{
				ChangeFeatureExtraYield(((FeatureTypes)iJ), eYield, (GC.getBuildingInfo(eBuilding)->GetFeatureYieldChange(iJ, eYield) * iChange));
			}

#if defined(MOD_BALANCE_CORE)
			// Is this building part of a Corporation?
			CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
			if (pkBuildingClassInfo)
			{
				CorporationTypes eCorporation = pkBuildingClassInfo->getCorporationType();
				if (eCorporation != NO_CORPORATION)
				{
					CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
					if (pkCorporationInfo)
					{
						// Only if this is the headquarters or Office
						if (pkCorporationInfo->GetOfficeBuildingClass() == eBuildingClass)
						{
							for (int iJ = 0; iJ < GC.getNumResourceInfos(); iJ++)
							{
								ChangeResourceExtraYield(((ResourceTypes)iJ), eYield, (pkCorporationInfo->GetResourceYieldChange(iJ, eYield) * iChange));
							}
						}
					}
				}
			}

			for(int iJ = 0; iJ < GC.getNumImprovementInfos(); iJ++)
			{
				ImprovementTypes eImprovement = (ImprovementTypes)iJ;
				if(eImprovement != NO_IMPROVEMENT)
				{
					int iYieldChange = pBuildingInfo->GetImprovementYieldChange(eImprovement, eYield);
					if(iYieldChange > 0)
					{
						ChangeImprovementExtraYield(eImprovement, eYield, (iYieldChange * iChange));
					}
				}		
			}
#endif

			for(int iJ = 0; iJ < GC.getNumTerrainInfos(); iJ++)
			{
				ChangeTerrainExtraYield(((TerrainTypes)iJ), eYield, (GC.getBuildingInfo(eBuilding)->GetTerrainYieldChange(iJ, eYield) * iChange));
#if defined(MOD_BALANCE_CORE)
				ChangeYieldPerXTerrainFromBuildingsTimes100(((TerrainTypes)iJ), eYield, (GC.getBuildingInfo(eBuilding)->GetYieldPerXTerrain(iJ, eYield) * iChange));
#endif
			}

#if defined(MOD_API_UNIFIED_YIELDS) && defined(MOD_API_PLOT_YIELDS)
			for(int iJ = 0; iJ < GC.getNumPlotInfos(); iJ++)
			{
				ChangePlotExtraYield(((PlotTypes)iJ), eYield, (GC.getBuildingInfo(eBuilding)->GetPlotYieldChange(iJ, eYield) * iChange));
			}
#endif

#if defined(MOD_DIPLOMACY_CIV4_FEATURES)
			// Research agreements are not active, therefore this building now increases science yield by 25%
			if(MOD_DIPLOMACY_CIV4_FEATURES && !GC.getGame().isOption(GAMEOPTION_RESEARCH_AGREEMENTS))
			{
				if(pBuildingInfo->GetMedianTechPercentChange() > 0)
				{
					if(eYield == YIELD_SCIENCE)
					{
						int iChange = pBuildingInfo->GetMedianTechPercentChange(); //default 25
					
						changeYieldRateModifier(eYield, iChange);
					}
				}
			}
#endif

			if(pBuildingInfo->GetEnhancedYieldTech() != NO_TECH)
			{
				if(owningTeam.GetTeamTechs()->HasTech((TechTypes)pBuildingInfo->GetEnhancedYieldTech()))
				{
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					if(eYield == YIELD_CULTURE)
					{
						ChangeJONSCulturePerTurnFromBuildings(pBuildingInfo->GetTechEnhancedYieldChange(eYield) * iChange);
					}
					else if(eYield == YIELD_FAITH)
					{
						ChangeFaithPerTurnFromBuildings(pBuildingInfo->GetTechEnhancedYieldChange(eYield) * iChange);
					}
					else
#endif
					{
						ChangeBaseYieldRateFromBuildings(eYield, pBuildingInfo->GetTechEnhancedYieldChange(eYield) * iChange);
					}
				}
			}

			int iBuildingClassBonus = owningPlayer.GetBuildingClassYieldChange(eBuildingClass, eYield);
			if(iBuildingClassBonus > 0)
			{
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
				if(eYield == YIELD_CULTURE)
				{
					ChangeJONSCulturePerTurnFromBuildings(iBuildingClassBonus * iChange);
				}
				else if(eYield == YIELD_FAITH)
				{
					ChangeFaithPerTurnFromBuildings(iBuildingClassBonus * iChange);
				}
				else
#endif
				{
					ChangeBaseYieldRateFromBuildings(eYield, iBuildingClassBonus * iChange);
				}
			}
		}

		if(GC.getBuildingInfo(eBuilding)->GetSpecialistType() != NO_SPECIALIST)
		{
			GetCityCitizens()->ChangeBuildingGreatPeopleRateChanges((SpecialistTypes) GC.getBuildingInfo(eBuilding)->GetSpecialistType(), pBuildingInfo->GetGreatPeopleRateChange() * iChange);
		}

		for(int iI = 0; iI < GC.getNumUnitCombatClassInfos(); iI++)
		{
			const UnitCombatTypes eUnitCombatClass = static_cast<UnitCombatTypes>(iI);
			CvBaseInfo* pkUnitCombatClassInfo = GC.getUnitCombatClassInfo(eUnitCombatClass);
			if(pkUnitCombatClassInfo)
			{
				changeUnitCombatFreeExperience(eUnitCombatClass, pBuildingInfo->GetUnitCombatFreeExperience(iI) * iChange);
				changeUnitCombatProductionModifier(eUnitCombatClass, pBuildingInfo->GetUnitCombatProductionModifier(iI) * iChange);
			}
		}
		for(int iI = 0; iI < NUM_DOMAIN_TYPES; iI++)
		{
			changeDomainFreeExperience(((DomainTypes)iI), pBuildingInfo->GetDomainFreeExperience(iI) * iChange);
			changeDomainProductionModifier(((DomainTypes)iI), pBuildingInfo->GetDomainProductionModifier(iI) * iChange);
		}

		// Process for our player
		for(int iI = 0; iI < MAX_PLAYERS; iI++)
		{
			if(GET_PLAYER((PlayerTypes)iI).getTeam() == getTeam())
			{
				if(pBuildingInfo->IsTeamShare() || (iI == getOwner()))
				{
					CvArea* pArea = GC.getMap().getArea( getArea() );
					GET_PLAYER((PlayerTypes)iI).processBuilding(eBuilding, iChange, bFirst, pArea);
				}
			}
		}

		// Process for our team
		owningTeam.processBuilding(eBuilding, iChange, bFirst);
	}

	if(!bObsolete)
	{
		m_pCityBuildings->ChangeBuildingDefense(pBuildingInfo->GetDefenseModifier() * iChange);

		owningTeam.changeBuildingClassCount(eBuildingClass, iChange);
		owningPlayer.changeBuildingClassCount(eBuildingClass, iChange);
	}

	UpdateReligion(GetCityReligions()->GetReligiousMajority());
	GET_PLAYER(getOwner()).CalculateNetHappiness();
#if defined(MOD_BALANCE_CORE)
	GetCityCitizens()->SetDirty(true);
#endif

#if defined(MOD_BALANCE_CORE_POLICIES)
	if(IsPurchased(eBuildingClass))
	{
		bNoBonus = true;
	}
	//If a building is being processed, it has been here before. No more bonuses!
	if(MOD_BALANCE_CORE_POLICIES && !IsResistance() && !IsRazing() && !bNoBonus && bFirst && (iChange > 0))
	{
		if(GetCityBuildings()->IsFirstTimeBuilding(eBuilding) <= 0)
		{
			GetCityBuildings()->SetFirstTimeBuilding(eBuilding, 1);
			GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_CONSTRUCTION, false, NO_GREATPERSON, eBuilding, 0, true, NO_PLAYER, NULL, false, this);
		}
	}
	UpdateComboHappiness();
#endif
	setLayoutDirty(true);
}


//	--------------------------------------------------------------------------------
void CvCity::processProcess(ProcessTypes eProcess, int iChange)
{
	VALIDATE_OBJECT
	int iI;

	const CvProcessInfo* pkProcessInfo = GC.getProcessInfo(eProcess);
	CvAssertFmt(pkProcessInfo != NULL, "Process type %d is invalid", eProcess);
	if (pkProcessInfo != NULL)
	{
		// Convert to another yield
		for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			changeProductionToYieldModifier(((YieldTypes)iI), (pkProcessInfo->getProductionToYieldModifier(iI) * iChange));
#if defined(MOD_BALANCE_CORE)
			UpdateCityYields((YieldTypes)iI);
			if((YieldTypes)iI == YIELD_CULTURE || (YieldTypes)iI == YIELD_TOURISM)
			{
				GetCityCulture()->CalculateBaseTourismBeforeModifiers();
				GetCityCulture()->CalculateBaseTourism();
			}
#endif
		}
	}
}


//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
void CvCity::processSpecialist(SpecialistTypes eSpecialist, int iChange, bool bSkip)
#else
void CvCity::processSpecialist(SpecialistTypes eSpecialist, int iChange)
#endif
{
	VALIDATE_OBJECT
	int iI;

	CvSpecialistInfo* pkSpecialist = GC.getSpecialistInfo(eSpecialist);
	if(pkSpecialist == NULL)
	{
		//This function requires a valid specialist type.
		return;
	}

	changeBaseGreatPeopleRate(pkSpecialist->getGreatPeopleRateChange() * iChange);

	for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		ChangeBaseYieldRateFromSpecialists(((YieldTypes)iI), (pkSpecialist->getYieldChange(iI) * iChange));
	}
	updateExtraSpecialistYield();
	changeSpecialistFreeExperience(pkSpecialist->getExperience() * iChange);

	// Culture
	int iCulturePerSpecialist = GetCultureFromSpecialist(eSpecialist);
	ChangeJONSCulturePerTurnFromSpecialists(iCulturePerSpecialist * iChange);
#if defined(MOD_BALANCE_CORE)
	if(!bSkip)
	{
		for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			YieldTypes eYield = (YieldTypes) iI;
			if(eYield == NO_YIELD)
				continue;

			UpdateCityYields(eYield);
		}
		GetCityCulture()->CalculateBaseTourismBeforeModifiers();
		GetCityCulture()->CalculateBaseTourism();
	}
#endif
}

//	--------------------------------------------------------------------------------
/// Process the majority religion changing for a city
#if defined(MOD_BALANCE_CORE)
void CvCity::UpdateReligion(ReligionTypes eNewMajority, bool bRecalcPlotYields)
{
	//avoid this expensive call if only a specialist was added/removed
	if (bRecalcPlotYields)
#if defined(MOD_BALANCE_CORE)
		updateYield(true);
#else
		updateYield();
#endif
#else
void CvCity::UpdateReligion(ReligionTypes eNewMajority)
{
	updateYield();
#endif

	// Reset city level yields
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	m_iJONSCulturePerTurnFromReligion = 0;
	m_iFaithPerTurnFromReligion = 0;
#endif
#if defined(MOD_BUGFIX_MINOR)
	for(int iYield = 0; iYield < NUM_YIELD_TYPES; iYield++)
#else
	for(int iYield = 0; iYield <= YIELD_SCIENCE; iYield++)
#endif
	{
		m_aiBaseYieldRateFromReligion.setAt(iYield, 0);
	}

	for(int iYield = 0; iYield < NUM_YIELD_TYPES; iYield++)
	{
		int iYieldPerReligion = GetYieldPerReligionTimes100((YieldTypes)iYield);
		if (iYieldPerReligion > 0)
		{
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
			switch(iYield)
			{
				case YIELD_CULTURE:
					ChangeJONSCulturePerTurnFromReligion((GetCityReligions()->GetNumReligionsWithFollowers() * iYieldPerReligion) / 100);
					break;
				case YIELD_FAITH:
					ChangeFaithPerTurnFromReligion((GetCityReligions()->GetNumReligionsWithFollowers() * iYieldPerReligion) / 100);
					break;
				default:
#endif
					ChangeBaseYieldRateFromReligion((YieldTypes)iYield, (GetCityReligions()->GetNumReligionsWithFollowers() * iYieldPerReligion) / 100);
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					break;
			}
#endif
		}

		if(eNewMajority != NO_RELIGION)
		{
			const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eNewMajority, getOwner());
			if(pReligion)
			{
				int iFollowers = GetCityReligions()->GetNumFollowers(eNewMajority);

				int iReligionYieldChange = pReligion->m_Beliefs.GetCityYieldChange(getPopulation(), (YieldTypes)iYield, getOwner());
#if defined(MOD_BALANCE_CORE_BELIEFS)
				int iReligionPop = pReligion->m_Beliefs.GetYieldPerPop((YieldTypes)iYield, getOwner());
				if((getPopulation() > 0) && (iReligionPop > 0))
				{
					int iFaithPerPop = (getPopulation() / iReligionPop);
					if(iFaithPerPop != 0)
					{
						iReligionYieldChange += iFaithPerPop;
					}
				}
				int iReligionYieldMaxFollowers = pReligion->m_Beliefs.GetMaxYieldPerFollower((YieldTypes)iYield, getOwner());
				if (iReligionYieldMaxFollowers > 0)
				{
					int iFollowers = GetCityReligions()->GetNumFollowers(pReligion->m_eReligion);
#if defined(MOD_BALANCE_CORE_BELIEFS)
					if(pReligion->m_Beliefs.IsHalvedFollowers(getOwner()))
					{
						iFollowers /= 2;
					}
#endif
					int iTempMod = min(iFollowers, iReligionYieldMaxFollowers);
					iReligionYieldChange += iTempMod;
				}
#endif
#if defined(MOD_API_UNIFIED_YIELDS)
				if (isCapital()) {
					iReligionYieldChange += pReligion->m_Beliefs.GetCapitalYieldChange(getPopulation(), (YieldTypes)iYield, getOwner());
				}
				if (isCoastal()) {
					iReligionYieldChange += pReligion->m_Beliefs.GetCoastalCityYieldChange(getPopulation(), (YieldTypes)iYield, getOwner());
				}
#endif
				BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
				if (eSecondaryPantheon != NO_BELIEF && getPopulation() >= GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetMinPopulation())
				{
					iReligionYieldChange += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityYieldChange((YieldTypes)iYield);
#if defined(MOD_BALANCE_CORE_BELIEFS)
					if((getPopulation() > 0) && (GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetYieldPerPop((YieldTypes)iYield) > 0))
					{
						int iFaithPerPop = (getPopulation() / GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetYieldPerPop((YieldTypes)iYield));
						if(iFaithPerPop != 0)
						{
							iReligionYieldChange += iFaithPerPop;
						}
					}
#endif
#if defined(MOD_API_UNIFIED_YIELDS)
					if (isCapital()) {
						iReligionYieldChange += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCapitalYieldChange((YieldTypes)iYield);
					}
					if (isCoastal()) {
						iReligionYieldChange += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCoastalCityYieldChange((YieldTypes)iYield);
					}
#endif
				}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
				switch(iYield)
				{
				case YIELD_CULTURE:
					ChangeJONSCulturePerTurnFromReligion(iReligionYieldChange);
					break;
				case YIELD_FAITH:
					ChangeFaithPerTurnFromReligion(iReligionYieldChange);
					break;
				default:
#endif
					ChangeBaseYieldRateFromReligion((YieldTypes)iYield, iReligionYieldChange);
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					break;
				}
#endif

				if(IsRouteToCapitalConnected())
				{
					int iReligionChange = pReligion->m_Beliefs.GetYieldChangeTradeRoute((YieldTypes)iYield, getOwner());
					//BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
					if (eSecondaryPantheon != NO_BELIEF)
					{
						iReligionChange += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetYieldChangeTradeRoute((YieldTypes)iYield);
					}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					switch(iYield)
					{
					case YIELD_CULTURE:
						ChangeJONSCulturePerTurnFromReligion(iReligionChange);
						break;
					case YIELD_FAITH:
						ChangeFaithPerTurnFromReligion(iReligionChange);
						break;
					default:
#endif
						ChangeBaseYieldRateFromReligion((YieldTypes)iYield, iReligionChange);
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
						break;
					}
#endif
				}
				
				if (GetCityCitizens()->GetTotalSpecialistCount() > 0)
				{
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
					switch(iYield)
					{
					case YIELD_CULTURE:
						ChangeJONSCulturePerTurnFromReligion(pReligion->m_Beliefs.GetYieldChangeAnySpecialist((YieldTypes)iYield));
						break;
					case YIELD_FAITH:
						ChangeFaithPerTurnFromReligion(pReligion->m_Beliefs.GetYieldChangeAnySpecialist((YieldTypes)iYield));
						break;
					default:
#endif
						ChangeBaseYieldRateFromReligion((YieldTypes)iYield, pReligion->m_Beliefs.GetYieldChangeAnySpecialist((YieldTypes)iYield, getOwner()));
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
						break;
					}
#endif
				}

				// Buildings
				for(int jJ = 0; jJ < GC.getNumBuildingClassInfos(); jJ++)
				{
					BuildingClassTypes eBuildingClass = (BuildingClassTypes)jJ;

					CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
					if(!pkBuildingClassInfo)
					{
						continue;
					}

					const CvCivilizationInfo& playerCivilizationInfo = getCivilizationInfo();
					BuildingTypes eBuilding = (BuildingTypes)playerCivilizationInfo.getCivilizationBuildings(eBuildingClass);

					if(eBuilding != NO_BUILDING)
					{
						if(GetCityBuildings()->GetNumBuilding(eBuilding) > 0)
						{
							int iYieldFromBuilding = pReligion->m_Beliefs.GetBuildingClassYieldChange(eBuildingClass, (YieldTypes)iYield, iFollowers, getOwner());
#if defined(MOD_BUGFIX_MINOR)
							iYieldFromBuilding *= GetCityBuildings()->GetNumBuilding(eBuilding);
#endif

							if (isWorldWonderClass(*pkBuildingClassInfo))
							{
								iYieldFromBuilding += pReligion->m_Beliefs.GetYieldChangeWorldWonder((YieldTypes)iYield, getOwner());
							}
//New majority, not a pantheon
							if(eNewMajority > RELIGION_PANTHEON)
							{
								//Our new majority matches our total majority, so let's add in our new yields.
								if(GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities() == eNewMajority)
								{
									CvBuildingEntry *pkInfo = GC.getBuildingInfo(eBuilding);
									if (pkInfo)
									{
										iYieldFromBuilding += getReligionBuildingYieldRateModifier( (BuildingClassTypes)pkInfo->GetBuildingClassType(), (YieldTypes)iYield);
									}
								}
							}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
							switch(iYield)
							{
							case YIELD_CULTURE:
								ChangeJONSCulturePerTurnFromReligion(iYieldFromBuilding);
								break;
							case YIELD_FAITH:
								ChangeFaithPerTurnFromReligion(iYieldFromBuilding);
								break;
							default:
#endif
								ChangeBaseYieldRateFromReligion((YieldTypes)iYield, iYieldFromBuilding);
#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
								break;
							}
#endif
						}
					}
				}
			}
		}
	}
#if defined(MOD_BALANCE_CORE)
	//Some yield cleanup and refresh here - note that not all of this has to do with religion, however any time religion is updated, that's a good time to update the city's yields.
	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes) iI;
		if(eYield == NO_YIELD)
			continue;

		UpdateSpecialReligionYields(eYield);
		UpdateCityYields(eYield);
		//Also mountains, because they aren't called anywhere else!
		UpdateYieldPerXTerrainFromReligion(eYield, TERRAIN_MOUNTAIN);
		UpdateYieldPerXTerrain(eYield, TERRAIN_MOUNTAIN);
		UpdateYieldPerXFeature(eYield);
		updateExtraSpecialistYield(eYield);
	}
	GetCityCulture()->CalculateBaseTourismBeforeModifiers();
	GetCityCulture()->CalculateBaseTourism();
	UpdateBuildingClassHappinessFromReligion();
#endif
	GET_PLAYER(getOwner()).UpdateReligion();
}
#if defined(MOD_BALANCE_CORE)
bool CvCity::HasPaidAdoptionBonus(ReligionTypes eReligion) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eReligion >= 0, "eReligion expected to be >= 0");
	CvAssertMsg(eReligion < GC.getNumReligionInfos(), "eReligion expected to be < getNumReligionInfos");
	return m_abPaidAdoptionBonus[eReligion];
}
void CvCity::SetPaidAdoptionBonus(ReligionTypes eReligion, bool bNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eReligion >= 0, "eReligion expected to be >= 0");
	CvAssertMsg(eReligion < GC.getNumReligionInfos(), "eReligion expected to be < getNumReligionInfos");
	m_abPaidAdoptionBonus.setAt(eReligion, bNewValue);
}
#endif
//	--------------------------------------------------------------------------------
/// Culture from eSpecialist
int CvCity::GetCultureFromSpecialist(SpecialistTypes eSpecialist) const
{
	CvSpecialistInfo* pkSpecialistInfo = GC.getSpecialistInfo(eSpecialist);
	if(pkSpecialistInfo == NULL)
	{
		//This function REQUIRES a valid specialist type.
		return 0;
	}

	int iCulture = pkSpecialistInfo->getCulturePerTurn();
	iCulture += GET_PLAYER(getOwner()).GetSpecialistCultureChange();

	return iCulture;
}

//	--------------------------------------------------------------------------------
const CvHandicapInfo& CvCity::getHandicapInfo() const
{
	return GET_PLAYER(getOwner()).getHandicapInfo();
}

//	--------------------------------------------------------------------------------
HandicapTypes CvCity::getHandicapType() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).getHandicapType();
}

//	--------------------------------------------------------------------------------
const CvCivilizationInfo& CvCity::getCivilizationInfo() const
{
	return GET_PLAYER(getOwner()).getCivilizationInfo();
}

//	--------------------------------------------------------------------------------
CivilizationTypes CvCity::getCivilizationType() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).getCivilizationType();
}


//	--------------------------------------------------------------------------------
LeaderHeadTypes CvCity::getPersonalityType() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).getPersonalityType();
}


//	--------------------------------------------------------------------------------
ArtStyleTypes CvCity::getArtStyleType() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).getArtStyleType();
}


//	--------------------------------------------------------------------------------
CitySizeTypes CvCity::getCitySizeType() const
{
	VALIDATE_OBJECT
	return ((CitySizeTypes)(range((getPopulation() / 7), 0, (NUM_CITYSIZE_TYPES - 1))));
}


//	--------------------------------------------------------------------------------
bool CvCity::isBarbarian() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).isBarbarian();
}


//	--------------------------------------------------------------------------------
bool CvCity::isHuman() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).isHuman();
}


//	--------------------------------------------------------------------------------
bool CvCity::isVisible(TeamTypes eTeam, bool bDebug) const
{
	VALIDATE_OBJECT
	return plot()->isVisible(eTeam, bDebug);
}


//	--------------------------------------------------------------------------------
bool CvCity::isCapital() const
{
	VALIDATE_OBJECT
	return (GET_PLAYER(getOwner()).getCapitalCity() == this);
}

//	--------------------------------------------------------------------------------
/// Was this city originally any player's capital?
bool CvCity::IsOriginalCapital() const
{
	VALIDATE_OBJECT

	CvPlayerAI& kPlayer = GET_PLAYER(m_eOriginalOwner);
	if (getX() == kPlayer.GetOriginalCapitalX() && getY() == kPlayer.GetOriginalCapitalY())
	{
		return true;
	}

	return false;
}

//	--------------------------------------------------------------------------------
/// Was this city originally a major civ's capital?
bool CvCity::IsOriginalMajorCapital() const
{
	VALIDATE_OBJECT

	PlayerTypes ePlayer;
	for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
	{
		ePlayer = (PlayerTypes) iPlayerLoop;
		CvPlayerAI& kPlayer = GET_PLAYER(ePlayer);
		if (getX() == kPlayer.GetOriginalCapitalX() && getY() == kPlayer.GetOriginalCapitalY())
		{
			return true;
		}
	}

	return false;
}

//	--------------------------------------------------------------------------------
bool CvCity::isCoastal(int iMinWaterSize) const
{
	VALIDATE_OBJECT
	return plot()->isCoastalLand(iMinWaterSize);
}

#if defined(MOD_API_EXTENSIONS)
//	--------------------------------------------------------------------------------
bool CvCity::isAddsFreshWater() const {
	VALIDATE_OBJECT

	//ideally this should be cached and changed when a building is added/removed ...
	const std::vector<BuildingTypes>& vBuildings = GetCityBuildings()->GetAllBuildingsHere();
	for(size_t iBuilding = 0; iBuilding < vBuildings.size(); iBuilding++)
	{
		CvBuildingEntry* pInfo = GC.getBuildingInfo(vBuildings[iBuilding]);
		if (pInfo && pInfo->IsAddsFreshWater())
			return true;
	}

	return false;
}
#endif

#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::GetUnitPurchaseCooldown() const
{
	VALIDATE_OBJECT
	return m_iUnitPurchaseCooldown;
}
//	--------------------------------------------------------------------------------
void CvCity::SetUnitPurchaseCooldown(int iValue)
{
	VALIDATE_OBJECT
	m_iUnitPurchaseCooldown = iValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeUnitPurchaseCooldown(int iValue)
{
	VALIDATE_OBJECT
	if(iValue != 0)
	{
		m_iUnitPurchaseCooldown += iValue;
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetBuildingPurchaseCooldown() const
{
	VALIDATE_OBJECT
	return m_iBuildingPurchaseCooldown;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBuildingPurchaseCooldown(int iValue)
{
	VALIDATE_OBJECT
	m_iBuildingPurchaseCooldown = iValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBuildingPurchaseCooldown(int iValue)
{
	VALIDATE_OBJECT
	if(iValue != 0)
	{
		m_iBuildingPurchaseCooldown += iValue;
	}
}
void CvCity::DoSellBuilding()
{
	if(!IsRazing() || GetCityBuildings()->IsSoldBuildingThisTurn())
	{
		return;
	}
	int iBestRefund = 0;
	BuildingTypes eBestBuilding = NO_BUILDING;
	for(int iBuildingLoop = 0; iBuildingLoop < GC.getNumBuildingInfos(); iBuildingLoop++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iBuildingLoop);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);

		if(pkBuildingInfo)
		{
			// Has this Building
			if(GetCityBuildings()->GetNumBuilding(eBuilding) > 0 && GetCityBuildings()->IsBuildingSellable(*pkBuildingInfo))
			{
				int iRefund = GetCityBuildings()->GetSellBuildingRefund(eBuilding);
				if(iRefund > iBestRefund)
				{
					iBestRefund = iRefund;
					eBestBuilding = eBuilding;
				}
			}
		}
	}
	if(eBestBuilding != NO_BUILDING)
	{
		GetCityBuildings()->DoSellBuilding(eBestBuilding);
		if(GC.getLogging() && GC.getAILogging())
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBestBuilding);
			if(pkBuildingInfo)
			{
				CvString strLogString;
				strLogString.Format("MOD - Selling building in a razed city: %s in %s. Gold Recovered: %d",
					pkBuildingInfo->GetDescription(), getName().c_str(), iBestRefund);
				
				GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
			}
		}
	}
}
void CvCity::SetTraded(PlayerTypes ePlayer, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(ePlayer >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(ePlayer < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	m_abTraded.setAt(ePlayer, bValue);
}
bool CvCity::IsTraded(PlayerTypes ePlayer)
{
	VALIDATE_OBJECT
	CvAssertMsg(ePlayer >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(ePlayer < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	return m_abTraded[ePlayer];
}
void CvCity::CheckForOperationUnits()
{
	VALIDATE_OBJECT
	UnitTypes eBestUnit;
	UnitAITypes eUnitAI;
	if(IsPuppet() || IsRazing() || this == NULL)
	{
		return;
	}
	if (GET_PLAYER(getOwner()).isMinorCiv() || isBarbarian())
		return;

	CvPlayerAI& kPlayer = GET_PLAYER(getOwner());

	int iGPT = GET_PLAYER(getOwner()).GetTreasury()->CalculateBaseNetGold();
	//Don't if we're in debt.
	if(iGPT <= 0)
	{
		return;
	}

	//Do we already have a military unit in the queue? If so, let's not flood the queue.
	bool bAlreadyUnderConstruction = false;
	const OrderData* pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_TRAIN)
		{
			bAlreadyUnderConstruction = true;
			break;
		}
		pOrderNode = nextOrderQueueNode(pOrderNode);
	}
	if(bAlreadyUnderConstruction)
	{
		return;
	}
	bool bAppend = true;
	if(kPlayer.GetMilitaryAI()->GetNumberCivsAtWarWith(false) > 0)
	{
		bAppend = false;
	}

	bool bCitySameAsMuster = false;
	OperationSlot thisOperationSlot = kPlayer.PeekAtNextUnitToBuildForOperationSlot(this, bCitySameAsMuster);
	if (thisOperationSlot.IsValid() && bCitySameAsMuster)
	{
		CvArmyAI* pThisArmy = kPlayer.getArmyAI(thisOperationSlot.m_iArmyID);

		if(pThisArmy)
		{
			// figure out the primary and secondary unit type to potentially build
			int iFormationIndex = pThisArmy->GetFormationIndex();
			CvMultiUnitFormationInfo* thisFormation = GC.getMultiUnitFormationInfo(iFormationIndex);
			if(thisFormation)
			{
				const CvFormationSlotEntry& slotEntry = thisFormation->getFormationSlotEntry(thisOperationSlot.m_iSlotID);

				eUnitAI = (UnitAITypes)slotEntry.m_primaryUnitType;
				eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI, true);
				if(eBestUnit == NO_UNIT)
				{
					eUnitAI = (UnitAITypes)slotEntry.m_secondaryUnitType;
					eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI, true);
				}
				if(eBestUnit != NO_UNIT)
				{
					int iTempWeight = 100;
					iTempWeight = GetCityStrategyAI()->GetUnitProductionAI()->CheckUnitBuildSanity(eBestUnit, true, pThisArmy, iTempWeight, iGPT, -1, -1, true);
					if(iTempWeight > 0)
					{
						int iGoldCost = GetPurchaseCost(eBestUnit);
						CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eBestUnit);
						if (pkUnitEntry && kPlayer.GetEconomicAI()->CanWithdrawMoneyForPurchase(PURCHASE_TYPE_UNIT, iGoldCost) && IsCanPurchase(/*bTestPurchaseCost*/ true, /*bTestTrainable*/ true, eBestUnit, NO_BUILDING, NO_PROJECT, YIELD_GOLD))
						{
							//Log it
							CvString strLogString;
							strLogString.Format("MOD - Buying unit for active operation from City root function: %s in %s. Cost: %d, Balance (before buy): %d",
								pkUnitEntry->GetDescription(), getName().c_str(), iGoldCost, GET_PLAYER(getOwner()).GetTreasury()->GetGold());
							GetCityStrategyAI()->LogHurryMessage(strLogString);

							//take the money...
							kPlayer.GetTreasury()->ChangeGold(-iGoldCost);

							//and train it!
							UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
							int iResult = CreateUnit(eBestUnit, eUnitAI, true);
							CvAssertMsg(iResult != -1, "Unable to create unit");
							if (iResult != -1)
							{
								CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
								if (!pUnit->getUnitInfo().CanMoveAfterPurchase())
								{
									pUnit->setMoves(0);
								}
								CleanUpQueue();
								kPlayer.CityFinishedBuildingUnitForOperationSlot(thisOperationSlot, pUnit);
								return;
							}
						}
					}
					else
					{
						if(getProductionTurnsLeft(eBestUnit, 0) >= 10)
						{
							return;
						}
						iTempWeight = 100;
						iTempWeight = GetCityStrategyAI()->GetUnitProductionAI()->CheckUnitBuildSanity(eBestUnit, true, pThisArmy, iTempWeight, iGPT, -1, -1);
						if (iTempWeight > 0)
						{
							pushOrder(ORDER_TRAIN, eBestUnit, eUnitAI, false, false, bAppend, false /*bRush*/);
							if(!bAppend)
							{
								OperationSlot thisOperationSlot2 = kPlayer.CityCommitToBuildUnitForOperationSlot(getArea(), getProductionTurnsLeft(), this);
								m_unitBeingBuiltForOperation = thisOperationSlot2;
							}
							//Log it
							CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eBestUnit);
							if(pkUnitEntry)
							{
								if(GC.getLogging() && GC.getAILogging())
								{
									CvString strLogString;
									strLogString.Format("MOD - Building unit for active operation from City root function: %s in %s. Turns: %d",
										pkUnitEntry->GetDescription(), getName().c_str(), getProductionTurnsLeft(eBestUnit, 0));
									GetCityStrategyAI()->LogHurryMessage(strLogString);
								}
							}
							return;
						}
					}
				}
			}
		}
	}
	eBestUnit = kPlayer.GetMilitaryAI()->GetUnitForArmy(this);
	if(eBestUnit != NO_UNIT)
	{
		if(getProductionTurnsLeft(eBestUnit, 0) >= 10)
		{
			return;
		}
	}
	bool bTarget = false;
	PlayerTypes eLoopPlayer;
	for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
	{
		eLoopPlayer = (PlayerTypes) iPlayerLoop;

		if(eLoopPlayer != NO_PLAYER && GET_PLAYER(eLoopPlayer).isAlive() && eLoopPlayer != getOwner())
		{
			if(kPlayer.GetDiplomacyAI()->IsWantsSneakAttack(eLoopPlayer))
			{
				bTarget = true;
				break;
			}
		}
	}
	if(bTarget)
	{
		if(eBestUnit != NO_UNIT)
		{
			int iGoldCost = GetPurchaseCost(eBestUnit);
			CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eBestUnit);
			if(pkUnitEntry && kPlayer.GetEconomicAI()->CanWithdrawMoneyForPurchase(PURCHASE_TYPE_UNIT, iGoldCost) && IsCanPurchase(/*bTestPurchaseCost*/ true, /*bTestTrainable*/ true, eBestUnit, NO_BUILDING, NO_PROJECT, YIELD_GOLD))
			{	
				//Log it
				CvString strLogString;
				strLogString.Format("MOD - Buying unit for sneak attack (or at war) from City root function: %s in %s. Cost: %d, Balance (before buy): %d",
					pkUnitEntry->GetDescription(), getName().c_str(), iGoldCost, GET_PLAYER(getOwner()).GetTreasury()->GetGold());
				kPlayer.GetHomelandAI()->LogHomelandMessage(strLogString);

				//take the money...
				kPlayer.GetTreasury()->ChangeGold(-iGoldCost);

				//and train it!
				UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
				int iResult = CreateUnit(eBestUnit, eUnitAI, false);
				CvAssertMsg(iResult != -1, "Unable to create unit");
				if (iResult != -1)
				{
					CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
					if (!pUnit->getUnitInfo().CanMoveAfterPurchase())
					{
						pUnit->setMoves(0);
					}
					CleanUpQueue();
				}
				return;
			}
			else
			{
				CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eBestUnit);					
				if(pkUnitEntry)
				{
					UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
					pushOrder(ORDER_TRAIN, eBestUnit, eUnitAI, false, false, bAppend, false /*bRush*/);
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("MOD - Building unit for sneak attack (or at war) from City root function: %s in %s. Turns: %d",
							pkUnitEntry->GetDescription(), getName().c_str(), getProductionTurnsLeft(eBestUnit, 0));
						kPlayer.GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
				return;
			}
		}
	}	
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::foodConsumption(bool /*bNoAngry*/, int iExtra) const
{
	VALIDATE_OBJECT
#if defined(MOD_BALANCE_YIELD_SCALE_ERA)
	if(MOD_BALANCE_YIELD_SCALE_ERA)
	{
		int iSpecialists = GetCityCitizens()->GetTotalSpecialistCount();
		iSpecialists += iExtra;
		int iPopulation = max(0,(getPopulation() - iSpecialists)); //guard against stupidity

		int iFoodPerPop = /*2*/ GC.getFOOD_CONSUMPTION_PER_POPULATION();
#if defined(MOD_BALANCE_CORE)
		iFoodPerPop += GetAdditionalFood();
#endif

		int iNormalFood = iPopulation * iFoodPerPop;

		int iEra = GET_PLAYER(getOwner()).GetCurrentEra();
		if(iEra <= GC.getFOOD_CONSUMPTION_PER_POPULATION())
		{
			iEra = GC.getFOOD_CONSUMPTION_PER_POPULATION();
		}
		if(iEra > 7)
		{
			iEra = 7;
		}

		int iSpecialistFood = (iEra * iSpecialists);
		if(GET_PLAYER(getOwner()).isHalfSpecialistFood())
		{
			iSpecialistFood /= 2;
		}
		if(GET_PLAYER(getOwner()).isHalfSpecialistFoodCapital() && isCapital())
		{
			iSpecialistFood /= 2;
		}

		return iNormalFood + iSpecialistFood;
	}
	else
	{
#endif
	int iPopulation = getPopulation() + iExtra;

	int iFoodPerPop = /*2*/ GC.getFOOD_CONSUMPTION_PER_POPULATION();
#if defined(MOD_BALANCE_CORE)
	iFoodPerPop += GetAdditionalFood();
#endif
	int iNum = iPopulation * iFoodPerPop;
	// Specialists eat less food? (Policies, etc.)
	if(GET_PLAYER(getOwner()).isHalfSpecialistFood())
	{
		int iFoodReduction = GetCityCitizens()->GetTotalSpecialistCount() * iFoodPerPop;
		iFoodReduction /= 2;
		iNum -= iFoodReduction;
	}
#if defined(MOD_BALANCE_CORE)
	else if(GET_PLAYER(getOwner()).isHalfSpecialistFoodCapital() && isCapital())
	{
		int iFoodReduction = GetCityCitizens()->GetTotalSpecialistCount() * iFoodPerPop;
		iFoodReduction /= 2;
		iNum -= iFoodReduction;
	}
#endif
	return iNum;
#if defined(MOD_BALANCE_YIELD_SCALE_ERA)
	}
#endif
}

//	--------------------------------------------------------------------------------
int CvCity::foodDifference(bool bBottom) const
{
	VALIDATE_OBJECT
	return foodDifferenceTimes100(bBottom) / 100;
}


//	--------------------------------------------------------------------------------
int CvCity::foodDifferenceTimes100(bool bBottom, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iDifference;

	if(isFoodProduction())
	{
		iDifference = std::min(0, GetFoodProduction(getYieldRate(YIELD_FOOD, false) - foodConsumption()) * 100);
	}
	else
	{
		iDifference = (getYieldRateTimes100(YIELD_FOOD, false) - foodConsumption() * 100);
	}

	if(bBottom)
	{
		if((getPopulation() == 1) && (getFood() == 0))
		{
			iDifference = std::max(0, iDifference);
		}
	}

	// Growth Mods - Only apply if the City is growing (and not starving, otherwise it would actually have the OPPOSITE of the intended effect!)
	if(iDifference > 0)
	{
		int iTotalMod = 100;

		// Capital Mod for player. Used for Policies and such
		if(isCapital())
		{
			int iCapitalGrowthMod = GET_PLAYER(getOwner()).GetCapitalGrowthMod();
			if(iCapitalGrowthMod != 0)
			{
				iTotalMod += iCapitalGrowthMod;
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_CAPITAL", iCapitalGrowthMod);
			}
		}

		// City Mod for player. Used for Policies and such
		int iCityGrowthMod = GET_PLAYER(getOwner()).GetCityGrowthMod();
		if(iCityGrowthMod != 0)
		{
			iTotalMod += iCityGrowthMod;
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_PLAYER", iCityGrowthMod);
		}
#if defined(MOD_BALANCE_CORE)
		int iCorpMod = (GetTradeRouteCityMod(YIELD_FOOD));
		if(iCorpMod > 0)
		{
			iTotalMod += iCorpMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_CORPORATION", iCorpMod);
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(GET_PLAYER(getOwner()).isGoldenAge() && (GetGoldenAgeYieldMod(YIELD_FOOD) != 0))
		{
			int iBuildingMod = GetGoldenAgeYieldMod(YIELD_FOOD);
			iTotalMod += iBuildingMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_YIELD_GOLDEN_AGE_BUILDINGS", iBuildingMod);
		}
		if(GET_PLAYER(getOwner()).isGoldenAge() && GET_PLAYER(getOwner()).getGoldenAgeYieldMod(YIELD_FOOD) != 0)
		{
			int iPolicyMod = GET_PLAYER(getOwner()).getGoldenAgeYieldMod(YIELD_FOOD);
			iTotalMod += iPolicyMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_YIELD_GOLDEN_AGE_POLICIES", iPolicyMod);
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(MOD_BALANCE_CORE_DIPLOMACY_ADVANCED)
		{
			int iGrowthTourism = GetGrowthFromTourism();
			iTotalMod += iGrowthTourism;
			if(iGrowthTourism != 0)
			{
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_TOURISM", iGrowthTourism);
			}
		}
#endif

		// Religion growth mod
		int iReligionGrowthMod = 0;
		ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
		if(eMajority != NO_RELIGION)
		{
			const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
			if(pReligion)
			{
				bool bAtPeace = GET_TEAM(getTeam()).getAtWarCount(false) == 0;
				iReligionGrowthMod = pReligion->m_Beliefs.GetCityGrowthModifier(bAtPeace, getOwner());
				BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
#if defined(MOD_BALANCE_CORE)
				if (GET_PLAYER(getOwner()).GetPlayerTraits()->IsPopulationBoostReligion() && eMajority == (GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities() || GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer()))
				{
					int iFollowers = GetCityReligions()->GetNumFollowers(eMajority);
					iReligionGrowthMod += (iFollowers * GC.getMOD_BALANCE_FOLLOWER_GROWTH_BONUS());
				}
#endif
				if (eSecondaryPantheon != NO_BELIEF)
				{
					iReligionGrowthMod += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityGrowthModifier();
				}
				iTotalMod += iReligionGrowthMod;
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_RELIGION", iReligionGrowthMod);
			}
		}

#if defined(MOD_BALANCE_CORE_RESOURCE_MONOPOLIES)
		if(MOD_BALANCE_CORE_RESOURCE_MONOPOLIES)
		{
			// Do we get increased yields from a resource monopoly?
			int iTempMod = GET_PLAYER(getOwner()).getCityYieldModFromMonopoly(YIELD_FOOD);
			if(iTempMod != 0)
			{
				iTempMod += GET_PLAYER(getOwner()).GetMonopolyModPercent();
				iTotalMod += iTempMod;
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_MONOPOLY_RESOURCE", iTempMod);
			}
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon() > 0)
		{
			int iBoon = GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon();
			if(GET_PLAYER(getOwner()).getGoldenAgeTurns() > 0)
			{
				iTotalMod += iBoon;
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_PLAYER_UA_GOLDEN_AGE", iBoon);
			}
		}
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
		if(MOD_BALANCE_CORE_HAPPINESS_NATIONAL && !GET_PLAYER(getOwner()).IsEmpireUnhappy())
		{
			//Mechanic to allow for growth malus from happiness/unhappiness.
			int iHappiness = GET_PLAYER(getOwner()).GetExcessHappiness();
			int iTempMod = 0;

			//If Happiness is greater than or over threshold, calculate city bonus mod.
			if(iHappiness >= GC.getBALANCE_HAPPINESS_THRESHOLD())
			{
				iHappiness = (iHappiness - GC.getBALANCE_HAPPINESS_THRESHOLD());
				//Are there minimums/maximums for the bonus? Restrict this value.
				if(iHappiness > GC.getBALANCE_HAPPINESS_BONUS_MAXIMUM())
				{
					iHappiness = GC.getBALANCE_HAPPINESS_BONUS_MAXIMUM();
				}
				else if(iHappiness < GC.getBALANCE_HAPPINESS_BONUS_MINIMUM())
				{
					iHappiness = GC.getBALANCE_HAPPINESS_BONUS_MINIMUM();
				}
				
			}
			if(iHappiness != 0)
			{
				iTempMod = (/*2*/ GC.getBALANCE_HAPPINESS_FOOD_MODIFIER() * iHappiness);
			}
			iTotalMod += iTempMod;
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_HAPPY", iTempMod);
		}
#endif
		// Cities stop growing when empire is very unhappy
		if(GET_PLAYER(getOwner()).IsEmpireVeryUnhappy())
		{
			int iMod = /*-100*/ GC.getVERY_UNHAPPY_GROWTH_PENALTY();
#if defined(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
		if(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
		{
			//Mechanic to allow for growth malus from happiness/unhappiness.
			int iHappiness = GET_PLAYER(getOwner()).GetExcessHappiness();

			//If happiness is less than the main threshold, calculate city penalty mod.
			if(iHappiness < GC.getBALANCE_HAPPINESS_THRESHOLD_MAIN())
			{
				//Are there minimums/maximums for the penalty? Restrict this value.
				if(iHappiness > GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM())
				{
					iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM();
				}
				else if(iHappiness < GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM())
				{
					iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM();
				}
			}
			if(iHappiness != 0)
			{
				iMod = (/*2*/ GC.getBALANCE_HAPPINESS_FOOD_MODIFIER() * iHappiness);
			}
		}
#endif
			iTotalMod += iMod;
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_UNHAPPY", iMod);
		}
		// Cities grow slower if the player is over his Happiness Limit
		else if(GET_PLAYER(getOwner()).IsEmpireUnhappy())
		{
			int iMod = /*-75*/ GC.getUNHAPPY_GROWTH_PENALTY();
#if defined(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
			if(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
			{
				//Mechanic to allow for growth malus from happiness/unhappiness.
				int iHappiness = GET_PLAYER(getOwner()).GetExcessHappiness();

				//If happiness is less than the main threshold, calculate city penalty mod.
				if(iHappiness < GC.getBALANCE_HAPPINESS_THRESHOLD_MAIN())
				{
					//Are there minimums/maximums for the penalty? Restrict this value.
					if(iHappiness > GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM())
					{
						iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM();
					}
					else if(iHappiness < GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM())
					{
						iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM();
					}
				}
				if(iHappiness != 0)
				{
					iMod = (/*2*/ GC.getBALANCE_HAPPINESS_FOOD_MODIFIER() * iHappiness);
				}
			}
#endif
			iTotalMod += iMod;
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_UNHAPPY", iMod);
		}
		// WLTKD Growth Bonus
#if defined(MOD_BALANCE_CORE)
		if(GetWeLoveTheKingDayCounter() > 0 && GET_PLAYER(getOwner()).GetPlayerTraits()->GetWLTKDCulture() <= 0)
#else
		else if(GetWeLoveTheKingDayCounter() > 0)
#endif
		{
			int iMod = /*25*/ GC.getWLTKD_GROWTH_MULTIPLIER();
#if defined(MOD_BALANCE_CORE)
			if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon() > 0)
			{
				iMod += GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon();
			}
#endif
			iTotalMod += iMod;
#if defined(MOD_BALANCE_CORE)
			if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetGrowthBoon() > 0)
			{
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_WLTKD_UA", iMod);
			}
			else
			{
#endif
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_WLTKD", iMod);
#if defined(MOD_BALANCE_CORE)
			}
#endif
		}
#if defined(MOD_DIPLOMACY_CITYSTATES)
		//Resolution League Bonus	
		if(MOD_DIPLOMACY_CITYSTATES && GetBaseYieldRateFromLeague(YIELD_FOOD) > 0)
		{
			int iMod = GetBaseYieldRateFromLeague(YIELD_FOOD);
			iTotalMod += iMod;
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_FOODMOD_LEAGUE", iMod);
		}
#endif

		iDifference *= iTotalMod;
		iDifference /= 100;
	}

	return iDifference;
}


//	--------------------------------------------------------------------------------
int CvCity::growthThreshold() const
{
	VALIDATE_OBJECT
	int iNumFoodNeeded = GET_PLAYER(getOwner()).getGrowthThreshold(getPopulation());

	return (iNumFoodNeeded);
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::GetUnhappinessFromCitySpecialists()
{
	int iUnhappiness = 0;
	int iUnhappinessFromThisCity;
	int iUnhappinessPerPop = /*1*/ GC.getUNHAPPINESS_PER_POPULATION() * 100;
	int iPopulation;

	bool bCityValid;

	bCityValid = false;

	// Assume city doesn't exist, and does NOT count
	if (IsIgnoreCityForHappiness())
		bCityValid = false;
	// Occupied Cities don't get counted here (see the next function)
	else if (!IsOccupied() || IsNoOccupiedUnhappiness())
		bCityValid = true;

	if (bCityValid)
	{
		iPopulation = GetCityCitizens()->GetTotalSpecialistCount();

		// No Unhappiness from Specialist Pop? (Policies, etc.)
		if (GET_PLAYER(getOwner()).isHalfSpecialistUnhappiness())
		{
			iPopulation++; // Round up
			iPopulation /= 2;
		}
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		//Less unhappiness from specialists....
		if (MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		{
			iUnhappinessPerPop = /*25*/ GC.getBALANCE_UNHAPPINESS_PER_SPECIALIST();
			int iNoHappinessSpecialists = 0;
			if (iPopulation > 0)
			{
				//...in capital?
				if (isCapital())
				{
					iNoHappinessSpecialists += GET_PLAYER(getOwner()).GetNoUnhappfromXSpecialistsCapital();
				}
				//...elsewhere?	
				iNoHappinessSpecialists += GET_PLAYER(getOwner()).GetNoUnhappfromXSpecialists();
			}
			//Can't give more free happiness than specialists.
			if (iNoHappinessSpecialists > iPopulation)
			{
				iNoHappinessSpecialists = iPopulation;
			}
			if (iNoHappinessSpecialists > 0)
			{
				iPopulation -= iNoHappinessSpecialists;
			}
		}
#endif

		iUnhappinessFromThisCity = iPopulation * iUnhappinessPerPop;

#if defined(MOD_BALANCE_CORE_HAPPINESS)
		if (MOD_BALANCE_CORE_HAPPINESS)
		{
			iUnhappiness += iUnhappinessFromThisCity;
		}
		if (!MOD_BALANCE_CORE_HAPPINESS)
		{
			//Took these away as they were making specialists do weird things.
#endif
			if (isCapital() && GET_PLAYER(getOwner()).GetCapitalUnhappinessMod() != 0)
			{
				iUnhappinessFromThisCity *= (100 + GET_PLAYER(getOwner()).GetCapitalUnhappinessMod());
				iUnhappinessFromThisCity /= 100;
			}
#if defined(MOD_BALANCE_CORE)
			if (GetLocalUnhappinessMod() != 0)
			{
				iUnhappinessFromThisCity *= (100 + GetLocalUnhappinessMod());
				iUnhappinessFromThisCity /= 100;
			}
#endif

			iUnhappiness += iUnhappinessFromThisCity;
#if defined(MOD_BALANCE_CORE_HAPPINESS)
		}
#endif
	}
#if defined(MOD_BALANCE_CORE_HAPPINESS)
	if (!MOD_BALANCE_CORE_HAPPINESS)
	{
#endif
		iUnhappiness *= (100 + GET_PLAYER(getOwner()).GetUnhappinessMod());
		iUnhappiness /= 100;

		iUnhappiness *= 100 + GET_PLAYER(getOwner()).GetPlayerTraits()->GetPopulationUnhappinessModifier();
		iUnhappiness /= 100;

		// Handicap mod
		iUnhappiness *= GET_PLAYER(getOwner()).getHandicapInfo().getPopulationUnhappinessMod();
		iUnhappiness /= 100;

#if defined(MOD_BALANCE_CORE_HAPPINESS)
	}
#endif
	return iUnhappiness;
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::productionLeft() const
{
	VALIDATE_OBJECT
	return (getProductionNeeded() - getProduction());
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCostModifier(HurryTypes eHurry, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	int iModifier = 100;
	const OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		switch(pOrderNode->eOrderType)
		{
		case ORDER_TRAIN:
			iModifier = getHurryCostModifier(eHurry, (UnitTypes) pOrderNode->iData1, bIgnoreNew);
			break;

		case ORDER_CONSTRUCT:
			iModifier = getHurryCostModifier(eHurry, (BuildingTypes) pOrderNode->iData1, bIgnoreNew);
			break;

		case ORDER_CREATE:
		case ORDER_PREPARE:
		case ORDER_MAINTAIN:
			break;

		default:
			CvAssertMsg(false, "pOrderNode->eOrderType did not match a valid option");
			break;
		}
	}

	return iModifier;
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCostModifier(HurryTypes eHurry, UnitTypes eUnit, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
	if(pkUnitInfo)
	{
		return getHurryCostModifier(eHurry, pkUnitInfo->GetHurryCostModifier(), getUnitProduction(eUnit), bIgnoreNew);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCostModifier(HurryTypes eHurry, BuildingTypes eBuilding, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo)
	{
		return getHurryCostModifier(eHurry, pkBuildingInfo->GetHurryCostModifier(), m_pCityBuildings->GetBuildingProduction(eBuilding), bIgnoreNew);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCostModifier(HurryTypes eHurry, int iBaseModifier, int iProduction, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	int iModifier = 100;
	iModifier *= std::max(0, iBaseModifier + 100);
	iModifier /= 100;

	if(iProduction == 0 && !bIgnoreNew)
	{
		iModifier *= std::max(0, (GC.getNEW_HURRY_MODIFIER() + 100));
		iModifier /= 100;
	}

	// Some places just don't care what kind of Hurry it is (leftover from Civ 4)
	if(eHurry != NO_HURRY)
	{
		if(GET_PLAYER(getOwner()).getHurryModifier(eHurry) != 0)
		{
			iModifier *= (100 + GET_PLAYER(getOwner()).getHurryModifier(eHurry));
			iModifier /= 100;
		}
#if defined(MOD_BALANCE_CORE)
		if(getHurryModifier(eHurry) != 0)
		{
			iModifier *= (100 + getHurryModifier(eHurry));
			iModifier /= 100;
		}
#endif
	}

	return iModifier;
}


//	--------------------------------------------------------------------------------
int CvCity::hurryCost(HurryTypes eHurry, bool bExtra) const
{
	VALIDATE_OBJECT
	return (getHurryCost(bExtra, productionLeft(), getHurryCostModifier(eHurry), getProductionModifier()));
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCost(HurryTypes eHurry, bool bExtra, UnitTypes eUnit, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	int iProductionLeft = getProductionNeeded(eUnit) - getUnitProduction(eUnit);

	return getHurryCost(bExtra, iProductionLeft, getHurryCostModifier(eHurry, eUnit, bIgnoreNew), getProductionModifier(eUnit));
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCost(HurryTypes eHurry, bool bExtra, BuildingTypes eBuilding, bool bIgnoreNew) const
{
	VALIDATE_OBJECT
	int iProductionLeft = getProductionNeeded(eBuilding) - m_pCityBuildings->GetBuildingProduction(eBuilding);

	return getHurryCost(bExtra, iProductionLeft, getHurryCostModifier(eHurry, eBuilding, bIgnoreNew), getProductionModifier(eBuilding));
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryCost(bool bExtra, int iProductionLeft, int iHurryModifier, int iModifier) const
{
	VALIDATE_OBJECT
	int iProduction = (iProductionLeft * iHurryModifier + 99) / 100; // round up

	if(bExtra)
	{
		int iExtraProduction = getExtraProductionDifference(iProduction, iModifier);
		if(iExtraProduction > 0)
		{
			int iAdjustedProd = iProduction * iProduction;

			// round up
			iProduction = (iAdjustedProd + (iExtraProduction - 1)) / iExtraProduction;
		}
	}

	return std::max(0, iProduction);
}

//	--------------------------------------------------------------------------------
int CvCity::hurryGold(HurryTypes eHurry) const
{
	VALIDATE_OBJECT
	int iFullCost = getProductionNeeded();

	return getHurryGold(eHurry, hurryCost(eHurry, false), iFullCost);
}

//	--------------------------------------------------------------------------------
/// Amount of Gold required to hurry Production in a City.  Full cost is the original Production cost of whatever we're rushing - the more expensive the ORIGINAL cost, the more it also costs to Gold rush
int CvCity::getHurryGold(HurryTypes /*eHurry*/, int /*iHurryCost*/, int /*iFullCost*/) const
{
	VALIDATE_OBJECT

	// This should not be used any more. Check GetPurchaseCostFromProduction()
	return 0;
}


//	--------------------------------------------------------------------------------
int CvCity::hurryPopulation(HurryTypes eHurry) const
{
	VALIDATE_OBJECT
	return (getHurryPopulation(eHurry, hurryCost(eHurry, true)));
}

//	--------------------------------------------------------------------------------
int CvCity::getHurryPopulation(HurryTypes eHurry, int iHurryCost) const
{
	VALIDATE_OBJECT
	CvHurryInfo* pkHurryInfo = GC.getHurryInfo(eHurry);
	if(pkHurryInfo == NULL)
	{
		return 0;
	}

	if(pkHurryInfo->getProductionPerPopulation() == 0)
	{
		return 0;
	}

	int iPopulation = (iHurryCost - 1) / GC.getGame().getProductionPerPopulation(eHurry);

	return std::max(1, (iPopulation + 1));
}

//	--------------------------------------------------------------------------------
int CvCity::hurryProduction(HurryTypes eHurry) const
{
	VALIDATE_OBJECT
	int iProduction = 0;

	CvHurryInfo* pkHurryInfo = GC.getHurryInfo(eHurry);
	if(pkHurryInfo)
	{
		if(pkHurryInfo->getProductionPerPopulation() > 0)
		{
			iProduction = (100 * getExtraProductionDifference(hurryPopulation(eHurry) * GC.getGame().getProductionPerPopulation(eHurry))) / std::max(1, getHurryCostModifier(eHurry));
			CvAssert(iProduction >= productionLeft());
		}
		else
		{
			iProduction = productionLeft();
		}
	}

	return iProduction;
}

//	--------------------------------------------------------------------------------
int CvCity::maxHurryPopulation() const
{
	VALIDATE_OBJECT
	return (getPopulation() / 2);
}

//	--------------------------------------------------------------------------------
bool CvCity::hasActiveWorldWonder() const
{
	VALIDATE_OBJECT

	CvTeam& kTeam = GET_TEAM(getTeam());

	for(int iI = 0; iI < GC.getNumBuildingInfos(); iI++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);

		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			if(isWorldWonderClass(pkBuildingInfo->GetBuildingClassInfo()))
			{
				if(m_pCityBuildings->GetNumRealBuilding(eBuilding) > 0 && !(kTeam.isObsoleteBuilding(eBuilding)))
				{
					return true;
				}
			}
		}
	}

	return false;
}

//	--------------------------------------------------------------------------------
int CvCity::getIndex() const
{
	VALIDATE_OBJECT
	return GetID();
}


//	--------------------------------------------------------------------------------
IDInfo CvCity::GetIDInfo() const
{
	VALIDATE_OBJECT
	IDInfo city(getOwner(), GetID());
	return city;
}


//	--------------------------------------------------------------------------------
void CvCity::SetID(int iID)
{
	VALIDATE_OBJECT
	m_iID = iID;
}

//	--------------------------------------------------------------------------------
CvPlot* CvCity::plot() const 
{ 
	if ((m_iX != INVALID_PLOT_COORD) && (m_iY != INVALID_PLOT_COORD))
		return GC.getMap().plotUnchecked(m_iX, m_iY); 
	return NULL;
}

//	--------------------------------------------------------------------------------
bool CvCity::at(int iX,  int iY) const
{
	VALIDATE_OBJECT
	return ((getX() == iX) && (getY() == iY));
}


//	--------------------------------------------------------------------------------
bool CvCity::at(CvPlot* pPlot) const
{
	VALIDATE_OBJECT
	return (plot() == pPlot);
}

//	--------------------------------------------------------------------------------
int CvCity::getArea() const
{
	VALIDATE_OBJECT
	CvPlot* pPlot = plot();
	return pPlot ? pPlot->getArea() : -1;
}

//	--------------------------------------------------------------------------------
bool CvCity::isAdjacentToArea(int iAreaID) const
{
	CvPlot* pPlot = plot();
	if (pPlot)
	{
		if (pPlot->getArea()==iAreaID)
			return true;
		else
		{
			std::vector<int> allAreas = pPlot->getAllAdjacentAreas();
			return std::find(allAreas.begin(),allAreas.end(),iAreaID) != allAreas.end();
		}
	}
	else
		return false;
}

bool CvCity::isMatchingArea(const CvPlot* pTestPlot) const
{
	if (!pTestPlot)
		return false;

	if (pTestPlot->isWater() && isAdjacentToArea(pTestPlot->getArea()))
		return true;
	else if (!pTestPlot->isWater() && pTestPlot->getArea()==getArea())
		return true;

	return false;
}

bool CvCity::hasSharedAdjacentArea(const CvCity * pOtherCity) const
{
	if (!pOtherCity)
		return false;

	std::vector<int> myAreas = plot()->getAllAdjacentAreas();
	std::vector<int> theirAreas = pOtherCity->plot()->getAllAdjacentAreas();
	std::vector<int> shared( MAX(myAreas.size(),theirAreas.size()) );

	std::vector<int>::iterator result = std::set_intersection(myAreas.begin(),myAreas.end(),theirAreas.begin(),theirAreas.end(),shared.begin());
	return (result!=shared.begin());
}
//	--------------------------------------------------------------------------------
// if called with an invalid unit as argument, the current garrison is removed but no new garrison created!
void CvCity::SetGarrison(CvUnit* pUnit)
{
	bool bPreviousGarrison = (m_hGarrison!=-1);
	CvUnit* pOldGarrison = bPreviousGarrison ? GET_PLAYER(getOwner()).getUnit(m_hGarrison) : NULL;
	if (pOldGarrison)
		pOldGarrison->SetGarrisonedCity(-1);

	if (pUnit && pUnit->CanGarrison() && pUnit->getOwner()==getOwner())
	{
		m_hGarrison = pUnit->GetID();
		m_iLastTurnGarrisonAssigned = GC.getGame().getGameTurn();

		pUnit->SetGarrisonedCity( GetID() );

		//no previous garrison. we might earn culture / happiness from this
		if (!bPreviousGarrison)
		{
			ChangeJONSCulturePerTurnFromPolicies(GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_CULTURE_FROM_GARRISON));
#if defined(MOD_BALANCE_CORE)
			UpdateCityYields(YIELD_CULTURE);
			ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer();
			if(eReligion != NO_RELIGION)
			{
				for(int iI = 0; iI < GC.getNumPromotionInfos(); iI++)
				{
					const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
					CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
					if(pkPromotionInfo != NULL)
					{
						if(pkPromotionInfo->GetReligiousPressureModifier() > 0)
						{
							if(pUnit->isHasPromotion(eLoopPromotion))
							{
								if(m_pCityReligions->IsReligionInCity())
								{
									ReligionTypes eMajority = m_pCityReligions->GetReligiousMajority();
									ReligionTypes eSecondary = m_pCityReligions->GetSecondaryReligion();
									if(eMajority != NO_RELIGION && eMajority == eReligion)
									{
										m_pCityReligions->ChangeReligiousPressureModifier(pkPromotionInfo->GetReligiousPressureModifier());
									}
									else if(eSecondary != NO_RELIGION && eSecondary == eReligion)
									{
										m_pCityReligions->ChangeReligiousPressureModifier(pkPromotionInfo->GetReligiousPressureModifier());
									}
								}
							}
						}
					}
				}
			}
#endif	
		}
	}
	else
	{
		m_hGarrison = -1;

		//had a previous garrison. bonuses be gone
		if (bPreviousGarrison)
		{
			ChangeJONSCulturePerTurnFromPolicies(-(GET_PLAYER(getOwner()).GetPlayerPolicies()->GetNumericModifier(POLICYMOD_CULTURE_FROM_GARRISON)));
#if defined(MOD_BALANCE_CORE)
			UpdateCityYields(YIELD_CULTURE);
			ReligionTypes eReligion = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer();
			if(eReligion != NO_RELIGION)
			{
				for(int iI = 0; iI < GC.getNumPromotionInfos(); iI++)
				{
					const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
					CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
					if(pkPromotionInfo != NULL)
					{
						if(pkPromotionInfo->GetReligiousPressureModifier() > 0)
						{
							if(pOldGarrison->isHasPromotion(eLoopPromotion))
							{
								if(m_pCityReligions->IsReligionInCity())
								{
									ReligionTypes eMajority = m_pCityReligions->GetReligiousMajority();
									ReligionTypes eSecondary = m_pCityReligions->GetSecondaryReligion();
									if(eMajority != NO_RELIGION && eMajority == eReligion)
									{
										m_pCityReligions->ChangeReligiousPressureModifier(-pkPromotionInfo->GetReligiousPressureModifier());
									}
									else if(eSecondary != NO_RELIGION && eSecondary == eReligion)
									{
										m_pCityReligions->ChangeReligiousPressureModifier(-pkPromotionInfo->GetReligiousPressureModifier());
									}
								}
							}
						}
					}
				}
			}
#endif	
		}
	}

	// Update City Strength
	updateStrengthValue();

	GET_PLAYER(getOwner()).CalculateNetHappiness();
}

bool CvCity::HasGarrison() const
{
#if defined(MOD_CORE_DEBUGGING)
	if (MOD_CORE_DEBUGGING)
	{
		if (m_hGarrison>-1 && GetGarrisonedUnit()==NULL)
		{
			OutputDebugString(CvString::format("error! invalid garrison %d is set in %s!\n",m_hGarrison,getName().c_str()).c_str());
			(const_cast<CvCity*>(this))->m_hGarrison = -1;
			return false;
		}
	}
#endif

	return m_hGarrison>-1;
}

CvUnit* CvCity::GetGarrisonedUnit() const
{
	if (m_hGarrisonOverride != -1)
	{
		CvUnit* pGarrison = GET_PLAYER(getOwner()).getUnit(m_hGarrisonOverride);
		if (pGarrison)
			return pGarrison;
		else
			OutputDebugString("warning: invalid garrison override!\n");
	}

	if (m_hGarrison>-1)
		return GET_PLAYER(getOwner()).getUnit(m_hGarrison);

	return NULL;
}

void CvCity::OverrideGarrison(const CvUnit* pUnit)
{
	if (pUnit && pUnit->getDomainType()==DOMAIN_LAND)
		m_hGarrisonOverride = pUnit->GetID();
	else
		m_hGarrisonOverride = -1;
}

//	--------------------------------------------------------------------------------
CvPlot* CvCity::getRallyPlot() const
{
	VALIDATE_OBJECT
	if ((m_iRallyX != INVALID_PLOT_COORD) && (m_iRallyY != INVALID_PLOT_COORD))
	{
		return GC.getMap().plotUnchecked(m_iRallyX, m_iRallyY);
	}
	else
		return NULL;
}


//	--------------------------------------------------------------------------------
void CvCity::setRallyPlot(CvPlot* pPlot)
{
	VALIDATE_OBJECT
	if(getRallyPlot() != pPlot)
	{
		if(pPlot != NULL)
		{
			m_iRallyX = pPlot->getX();
			m_iRallyY = pPlot->getY();
		}
		else
		{
			m_iRallyX = INVALID_PLOT_COORD;
			m_iRallyY = INVALID_PLOT_COORD;
		}

		if(isCitySelected())
		{
			DLLUI->setDirty(ColoredPlots_DIRTY_BIT, true);
		}
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getGameTurnFounded() const
{
	VALIDATE_OBJECT
	return m_iGameTurnFounded;
}


//	--------------------------------------------------------------------------------
void CvCity::setGameTurnFounded(int iNewValue)
{
	VALIDATE_OBJECT
	if(m_iGameTurnFounded != iNewValue)
	{
		m_iGameTurnFounded = iNewValue;
		CvAssert(getGameTurnFounded() >= 0);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getGameTurnAcquired() const
{
	VALIDATE_OBJECT
	return m_iGameTurnAcquired;
}


//	--------------------------------------------------------------------------------
void CvCity::setGameTurnAcquired(int iNewValue)
{
	VALIDATE_OBJECT
	m_iGameTurnAcquired = iNewValue;
	CvAssert(getGameTurnAcquired() >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getGameTurnLastExpanded() const
{
	VALIDATE_OBJECT
	return m_iGameTurnLastExpanded;
}


//	--------------------------------------------------------------------------------
void CvCity::setGameTurnLastExpanded(int iNewValue)
{
	VALIDATE_OBJECT
	if(m_iGameTurnLastExpanded != iNewValue)
	{
		m_iGameTurnLastExpanded = iNewValue;
		CvAssert(m_iGameTurnLastExpanded >= 0);
	}
}

#if defined(MOD_BALANCE_CORE)
int CvCity::GetAdditionalFood() const
{
	VALIDATE_OBJECT
	return m_iAdditionalFood;
}
void CvCity::SetAdditionalFood(int iValue)
{
	VALIDATE_OBJECT
	m_iAdditionalFood = iValue;
}
#endif
//	--------------------------------------------------------------------------------
int CvCity::getPopulation() const
{
	VALIDATE_OBJECT
	return m_iPopulation;
}

//	---------------------------------------------------------------------------------
//	Be very careful with setting bReassignPop to false.  This assumes that the caller
//  is manually adjusting the worker assignments *and* handling the setting of
//  the CityCitizens unassigned worker value.
#if defined(MOD_BALANCE_CORE)
void CvCity::setPopulation(int iNewValue, bool bReassignPop /* = true */, bool bNoBonus)
#else
void CvCity::setPopulation(int iNewValue, bool bReassignPop /* = true */)
#endif
{
	VALIDATE_OBJECT
	int iOldPopulation;
	
#if defined(MOD_BUGFIX_CITY_CENTRE_WORKING)
	// To fix the "not working the centre tile" bug always call GetCityCitizens()->SetWorkingPlot(plot(), true, false); here
	GetCityCitizens()->SetWorkingPlot(plot(), true, false, false);
#endif

	iOldPopulation = getPopulation();
	int iPopChange = iNewValue - iOldPopulation;

	if(iOldPopulation != iNewValue)
	{
		// If we are reducing population, remove the workers first
		if(bReassignPop)
		{
			if(iPopChange < 0)
			{
				// Need to Remove Citizens
				for(int iNewPopLoop = -iPopChange; iNewPopLoop--;)
				{
					GetCityCitizens()->DoRemoveWorstCitizen(true, NO_SPECIALIST, iNewValue);
				}

				// Fixup the unassigned workers
				int iUnassignedWorkers = GetCityCitizens()->GetNumUnassignedCitizens();
				CvAssert(iUnassignedWorkers >= -iPopChange);
				GetCityCitizens()->ChangeNumUnassignedCitizens(std::max(iPopChange, -iUnassignedWorkers));
			}
		}

		m_iPopulation = iNewValue;

		CvAssert(getPopulation() >= 0);

		GET_PLAYER(getOwner()).invalidatePopulationRankCache();

		if(getPopulation() > getHighestPopulation())
		{
			setHighestPopulation(getPopulation());
#if defined(MOD_BALANCE_CORE_BELIEFS)
			int iGameTurn = GC.getGame().getGameTurn() - getGameTurnFounded();
			if(!IsResistance() && (iGameTurn > 0) && !bNoBonus)
			{
				GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_BIRTH, true, NO_GREATPERSON, NO_BUILDING, 0, true, NO_PLAYER, NULL, false, this);
			}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
			if(MOD_BALANCE_CORE_POLICIES && GET_PLAYER(getOwner()).GetBestRangedUnitSpawnSettle() > 0 && !IsRazing())
			{
				int iDiff = foodDifference();

				if(iDiff >= 0)
				{
					int iRemainder = (getPopulation() % GET_PLAYER(getOwner()).GetBestRangedUnitSpawnSettle());
					if(iRemainder == 0)
					{
						UnitTypes eBestUnit = NO_UNIT;
						int iStrengthBest = 0;
						// Loop through adding the available units
						for(int iUnitLoop = 0; iUnitLoop < GC.getNumUnitInfos(); iUnitLoop++)
						{
							UnitTypes eLoopUnit = (UnitTypes)iUnitLoop;
							if(eLoopUnit != NO_UNIT)
							{
								CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eLoopUnit);
								if(pkUnitEntry)
								{
									if(!canTrain(eLoopUnit))
									{
										continue;
									}
									if(pkUnitEntry->GetRangedCombat() > 0)
									{
										continue;
									}
									if(pkUnitEntry->GetDomainType() == DOMAIN_SEA)
									{
										int iChance = GC.getGame().getJonRandNum(100, "Random Boat Chance");
										if(iChance < 50)
										{
											continue;
										}
									}
									bool bBad = false;
									ResourceTypes eResource;
									for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
									{
										eResource = (ResourceTypes) iResourceLoop;
										int iNumResource = pkUnitEntry->GetResourceQuantityRequirement(eResource);
										if (iNumResource > 0)
										{
											bBad = true;
											break;
										}
										if(pkUnitEntry->GetResourceType() == eResource)
										{
											bBad = true;
											break;
										}	
									}

									if(bBad)
									{
										continue;
									}
									int iCombatStrength = (pkUnitEntry->GetCombat() + GC.getGame().getJonRandNum(pkUnitEntry->GetCombat(), "Random Unit bump"));
									if(iCombatStrength > iStrengthBest)
									{
										iStrengthBest = iCombatStrength;
										eBestUnit = eLoopUnit;
									}
								}
							}
						}
						if(eBestUnit != NO_UNIT)
						{
							CvUnitEntry* pkUnitEntry = GC.getUnitInfo(eBestUnit);
							if(pkUnitEntry)
							{
								UnitAITypes eUnitAI = pkUnitEntry->GetDefaultUnitAIType();
								int iResult = CreateUnit(eBestUnit, eUnitAI);

								CvAssertMsg(iResult != -1, "Unable to create unit");

								if (iResult != -1)
								{
									CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
									if (!pUnit->jumpToNearestValidPlot())
									{
										pUnit->kill(false);	// Could not find a valid spot!
									}
									pUnit->setMoves(0);
									CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
									if(pUnit && pNotifications)
									{
										Localization::String localizedText = Localization::Lookup("TXT_KEY_NOTIFICATION_CONSCRIPTION_SPAWN");
										localizedText << getNameKey() << getPopulation() << pUnit->getNameKey();
										Localization::String localizedSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_CONSCRIPTION_SPAWN_SUMMARY");
										localizedSummary << getNameKey() << pUnit->getNameKey();
										pNotifications->Add(NOTIFICATION_GREAT_PERSON_ACTIVE_PLAYER, localizedText.toUTF8(), localizedSummary.toUTF8(), pUnit->getX(), pUnit->getY(), eBestUnit);
									}
									if(GC.getLogging() && GC.getAILogging())
									{
										CvString strLogString;
										strLogString.Format("Conscripted %s spawned at %s. Population: %d", pUnit->getName().GetCString(), getName().GetCString(), getPopulation());
										GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
									}
								}
							}
						}
					}
				}
			}
#endif
		}


		GC.getMap().getArea(getArea())->changePopulationPerPlayer(getOwner(), (getPopulation() - iOldPopulation));
#if defined(MOD_BALANCE_CORE)
		std::vector<int> areas = plot()->getAllAdjacentAreas();
		for (std::vector<int>::iterator it=areas.begin(); it!=areas.end(); ++it)
		{
			CvArea* pkArea = GC.getMap().getArea(*it);
			if (pkArea->isWater())
				pkArea->changePopulationPerPlayer(getOwner(), (getPopulation() - iOldPopulation));
		}
#endif

		GET_PLAYER(getOwner()).changeTotalPopulation(getPopulation() - iOldPopulation);
		GET_TEAM(getTeam()).changeTotalPopulation(getPopulation() - iOldPopulation);
		GC.getGame().changeTotalPopulation(getPopulation() - iOldPopulation);

		plot()->updateYield();

		UpdateReligion(GetCityReligions()->GetReligiousMajority());

		ChangeBaseYieldRateFromMisc(YIELD_SCIENCE, (iNewValue - iOldPopulation) * GC.getSCIENCE_PER_POPULATION());

		if(iPopChange > 0)
		{
			// Give new Population something to do in the City
			if(bReassignPop)
			{
				GetCityCitizens()->ChangeNumUnassignedCitizens(iPopChange);

				// Need to Add Citizens
				for(int iNewPopLoop = 0; iNewPopLoop < iPopChange; iNewPopLoop++)
				{
					std::map<SpecialistTypes, int> specialistValueCache;
					GetCityCitizens()->DoAddBestCitizenFromUnassigned(specialistValueCache);
				}
			}
		}

		setLayoutDirty(true);
		{
			auto_ptr<ICvCity1> pkDllCity(new CvDllCity(this));
			gDLL->GameplayCityPopulationChanged(pkDllCity.get(), iNewValue);
		}

		plot()->plotAction(PUF_makeInfoBarDirty);

		if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
		{
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
		}

		// Update Unit Maintenance for the player
		GET_PLAYER(getOwner()).UpdateUnitProductionMaintenanceMod();
		GET_PLAYER(getOwner()).CalculateNetHappiness();

		//updateGenericBuildings();
		updateStrengthValue();


		DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
	}

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getX());
		args->Push(getY());
		args->Push(iOldPopulation);
		args->Push(iNewValue);

		bool bResult;
		LuaSupport::CallHook(pkScriptSystem, "SetPopulation", args.get(), bResult);
	}
}

//	---------------------------------------------------------------------------------
//	Be very careful with setting bReassignPop to false.  This assumes that the caller
//  is manually adjusting the worker assignments *and* handling the setting of
//  the CityCitizens unassigned worker value.
void CvCity::changePopulation(int iChange, bool bReassignPop)
{
	VALIDATE_OBJECT
	setPopulation(getPopulation() + iChange, bReassignPop);

	// Update the religious system
	GetCityReligions()->DoPopulationChange(iChange);
}


//	--------------------------------------------------------------------------------
long CvCity::getRealPopulation() const
{
	VALIDATE_OBJECT
	return (((long)(pow((double)getPopulation(), 2.8))) * 1000);
}

//	--------------------------------------------------------------------------------
int CvCity::getHighestPopulation() const
{
	VALIDATE_OBJECT
	return m_iHighestPopulation;
}


//	--------------------------------------------------------------------------------
void CvCity::setHighestPopulation(int iNewValue)
{
	VALIDATE_OBJECT
	m_iHighestPopulation = iNewValue;
	CvAssert(getHighestPopulation() >= 0);
}

//	--------------------------------------------------------------------------------
int CvCity::getNumGreatPeople() const
{
	VALIDATE_OBJECT
	return m_iNumGreatPeople;
}


//	--------------------------------------------------------------------------------
void CvCity::changeNumGreatPeople(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iNumGreatPeople = (m_iNumGreatPeople + iChange);
		CvAssert(getNumGreatPeople() >= 0);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getBaseGreatPeopleRate() const
{
	VALIDATE_OBJECT
	return m_iBaseGreatPeopleRate;
}


//	--------------------------------------------------------------------------------
int CvCity::getGreatPeopleRate() const
{
	VALIDATE_OBJECT
	return ((getBaseGreatPeopleRate() * getTotalGreatPeopleRateModifier()) / 100);
}


//	--------------------------------------------------------------------------------
int CvCity::getTotalGreatPeopleRateModifier() const
{
	VALIDATE_OBJECT
	int iModifier;

	iModifier = getGreatPeopleRateModifier();

	iModifier += GET_PLAYER(getOwner()).getGreatPeopleRateModifier();

	if(GET_PLAYER(getOwner()).isGoldenAge())
	{
		iModifier += GC.getGOLDEN_AGE_GREAT_PEOPLE_MODIFIER();
	}

	return std::max(0, (iModifier + 100));
}


//	--------------------------------------------------------------------------------
void CvCity::changeBaseGreatPeopleRate(int iChange)
{
	VALIDATE_OBJECT
	m_iBaseGreatPeopleRate = (m_iBaseGreatPeopleRate + iChange);
	CvAssert(getBaseGreatPeopleRate() >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getGreatPeopleRateModifier() const
{
	VALIDATE_OBJECT
#if defined(MOD_BALANCE_CORE)
	int iNewValue = 0;
	if(isCapital())
	{
		int iNumMarried = 0;
		// Loop through all minors and get the total number we've met.
		for(int iPlayerLoop = 0; iPlayerLoop < MAX_CIV_PLAYERS; iPlayerLoop++)
		{
			PlayerTypes eMinor = (PlayerTypes) iPlayerLoop;

			if (eMinor != GET_PLAYER(getOwner()).GetID() && GET_PLAYER(eMinor).isAlive() && GET_PLAYER(eMinor).isMinorCiv())
			{
				if (GET_PLAYER(getOwner()).IsDiplomaticMarriage() && GET_PLAYER(eMinor).GetMinorCivAI()->IsMarried(GET_PLAYER(getOwner()).GetID()))
				{
					iNumMarried++;
				}
			}
		}
		if(GET_PLAYER(getOwner()).IsDiplomaticMarriage() && iNumMarried > 0)
		{
			iNewValue = (iNumMarried * GC.getBALANCE_MARRIAGE_GP_RATE());
		}
	}

	// Corporations: Great people rate modifier by number of franchises
	int iGPRateCorp = GetGPRateModifierPerXFranchises();
	if (iGPRateCorp > 0)
	{
		iNewValue += iGPRateCorp;
	}
#endif
#if defined(MOD_BALANCE_CORE)
	return m_iGreatPeopleRateModifier + iNewValue;
#else
	return m_iGreatPeopleRateModifier;
#endif
}

//	--------------------------------------------------------------------------------
void CvCity::changeGreatPeopleRateModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iGreatPeopleRateModifier = (m_iGreatPeopleRateModifier + iChange);
}

//	--------------------------------------------------------------------------------
/// Amount of Culture in this City
int CvCity::GetJONSCultureStored() const
{
	VALIDATE_OBJECT
	return m_iJONSCultureStored;
}

//	--------------------------------------------------------------------------------
/// Sets the amount of Culture in this City
void CvCity::SetJONSCultureStored(int iValue)
{
	VALIDATE_OBJECT
	m_iJONSCultureStored = iValue;
}

//	--------------------------------------------------------------------------------
/// Changes the amount of Culture in this City
void CvCity::ChangeJONSCultureStored(int iChange)
{
	VALIDATE_OBJECT
	SetJONSCultureStored(GetJONSCultureStored() + iChange);
}


//	--------------------------------------------------------------------------------
/// Culture level of this City
int CvCity::GetJONSCultureLevel() const
{
	VALIDATE_OBJECT
	return m_iJONSCultureLevel;
}

//	--------------------------------------------------------------------------------
/// Sets the Culture level of this City
void CvCity::SetJONSCultureLevel(int iValue)
{
	VALIDATE_OBJECT
	m_iJONSCultureLevel = iValue;
}

//	--------------------------------------------------------------------------------
/// Changes the Culture level of this City
void CvCity::ChangeJONSCultureLevel(int iChange)
{
	VALIDATE_OBJECT
	SetJONSCultureLevel(GetJONSCultureLevel() + iChange);
}

//	--------------------------------------------------------------------------------
/// What happens when you have enough Culture to acquire a new Plot?
void CvCity::DoJONSCultureLevelIncrease()
{
	VALIDATE_OBJECT

	int iOverflow = GetJONSCultureStored() - GetJONSCultureThreshold();
#if defined(MOD_UI_CITY_EXPANSION)
	bool bIsHumanControlled = (GET_PLAYER(getOwner()).isHuman() && !IsPuppet());
	bool bSendEvent = true;
	if (!(MOD_UI_CITY_EXPANSION && bIsHumanControlled)) {
		// We need to defer this for humans picking their own tiles
#endif
		SetJONSCultureStored(iOverflow);
		ChangeJONSCultureLevel(1);
#if defined(MOD_UI_CITY_EXPANSION)
	}
#endif
#if defined(MOD_BALANCE_CORE)
	CvPlot* pPlotToAcquire = GetNextBuyablePlot(false);
#else
	CvPlot* pPlotToAcquire = GetNextBuyablePlot();
#endif

	// maybe the player owns ALL of the plots or there are none avaialable?
	if(pPlotToAcquire)
	{
#if defined(MOD_UI_CITY_EXPANSION)
		// For human players, let them decide which plot to acquire
		if (MOD_UI_CITY_EXPANSION && bIsHumanControlled) 
		{
			// Yep CITY_PLOTS_RADIUS is a #define and not taken from the database - well done Firaxis!

			bool bCanAcquirePlot = plotDistance(getX(), getY(), pPlotToAcquire->getX(), pPlotToAcquire->getY()) <= getWorkPlotDistance();
			if (bCanAcquirePlot && GetBuyPlotCost(pPlotToAcquire->getX(), pPlotToAcquire->getY()) < 1) {
				// Within working/buying distance
				bSendEvent = false;

				CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
				if (pNotifications) {
					Localization::String localizedText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_CULTURE_ACQUIRED_NEW_PLOT");
					localizedText << getNameKey();
					Localization::String localizedSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_CULTURE_ACQUIRED_NEW_PLOT");
					localizedSummary << getNameKey();
					pNotifications->Add(NOTIFICATION_CITY_TILE, localizedText.toUTF8(), localizedSummary.toUTF8(), getX(), getY(), GetID());
				}
			} 
			else 
			{
				// The cheapest plot we can have is outside our working/buying distance, so just acquire it
				DoAcquirePlot(pPlotToAcquire->getX(), pPlotToAcquire->getY());

				ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
				if (pkScriptSystem) 
				{
					CvLuaArgsHandle args;
					args->Push(getOwner());
					args->Push(GetID());
					args->Push(pPlotToAcquire->getX());
					args->Push(pPlotToAcquire->getY());
					args->Push(false); // bGold
					args->Push(true); // bFaith/bCulture

					bool bResult;
					LuaSupport::CallHook(pkScriptSystem, "CityBoughtPlot", args.get(), bResult);
				}
				// and also the deferred stuff
				SetJONSCultureStored(iOverflow);
				ChangeJONSCultureLevel(1);
			}
		} 
		else 
		{
			// AI or dis-interested human, just acquire the plot normally
#endif
		if(GC.getLogging() && GC.getAILogging())
		{
			CvPlayerAI& kOwner = GET_PLAYER(getOwner());
			CvString playerName;
			FILogFile* pLog;
			CvString strBaseString;
			CvString strOutBuf;
			playerName = kOwner.getCivilizationShortDescription();
			pLog = LOGFILEMGR.GetLog(kOwner.GetCitySpecializationAI()->GetLogFileName(playerName), FILogFile::kDontTimeStamp);
			strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
			strBaseString += playerName + ", ";
			strOutBuf.Format("%s, City Culture Leveled Up. Level: %d Border Expanded, X: %d, Y: %d", getName().GetCString(), 
												GetJONSCultureLevel(), pPlotToAcquire->getX(), pPlotToAcquire->getY());
			strBaseString += strOutBuf;
			pLog->Msg(strBaseString);
		}
		DoAcquirePlot(pPlotToAcquire->getX(), pPlotToAcquire->getY());
#if defined(MOD_UI_CITY_EXPANSION)
		}
#endif

#if defined(MOD_UI_CITY_EXPANSION)
		// If the human is picking their own tile, the event will be sent when the tile is "bought"
		if (bSendEvent) 
		{
#endif
#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) 
			{
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityBoughtPlot, getOwner(), GetID(), pPlotToAcquire->getX(), pPlotToAcquire->getY(), false, true);
			}
			else 
			{
#endif
		ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
		if (pkScriptSystem) 
		{
			CvLuaArgsHandle args;
			args->Push(getOwner());
			args->Push(GetID());
			args->Push(pPlotToAcquire->getX());
			args->Push(pPlotToAcquire->getY());
			args->Push(false); // bGold
			args->Push(true); // bFaith/bCulture

			bool bResult;
			LuaSupport::CallHook(pkScriptSystem, "CityBoughtPlot", args.get(), bResult);
		}
#if defined(MOD_EVENTS_CITY)
			}
#endif
#if defined(MOD_UI_CITY_EXPANSION)
		}
#endif
#if defined(MOD_BALANCE_CORE)
			GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_BORDERS, true, NO_GREATPERSON, NO_BUILDING, 0, true, NO_PLAYER, NULL, false, this, false, true, false, NO_YIELD, NULL, pPlotToAcquire->getTerrainType());
#endif
#if defined(MOD_BALANCE_CORE)
		if(pPlotToAcquire->getTerrainType() != NO_TERRAIN && GET_PLAYER(getOwner()).GetPlayerTraits()->TerrainClaimBoost(pPlotToAcquire->getTerrainType()))
		{
			for (int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
			{
				CvPlot* pAdjacentPlot = plotDirection(pPlotToAcquire->getX(), pPlotToAcquire->getY(), ((DirectionTypes)iDirectionLoop));
				if (pAdjacentPlot && pAdjacentPlot->getTerrainType() == pPlotToAcquire->getTerrainType())
				{
					if(pAdjacentPlot->getOwner() == NO_PLAYER)
					{
						DoAcquirePlot(pAdjacentPlot->getX(), pAdjacentPlot->getY());
					}
				}
			}
		}
#endif
#if defined(MOD_UI_CITY_EXPANSION)
	}
	else if (MOD_UI_CITY_EXPANSION && bIsHumanControlled) 
	{
		// Do the stuff we deferred as we though we'd do it when the human bought a tile but can't as there are no tiles to buy!
		SetJONSCultureStored(iOverflow);
		ChangeJONSCultureLevel(1);
#endif
	}
}

//	--------------------------------------------------------------------------------
/// Amount of Culture needed in this City to acquire a new Plot
int CvCity::GetJONSCultureThreshold() const
{
	VALIDATE_OBJECT
	int iCultureThreshold = /*15*/ GC.getCULTURE_COST_FIRST_PLOT();

	float fExponent = /*1.1f*/ GC.getCULTURE_COST_LATER_PLOT_EXPONENT();

	int iPolicyExponentMod = GET_PLAYER(m_eOwner).GetPlotCultureExponentModifier();
	if(iPolicyExponentMod != 0)
	{
		fExponent = fExponent * (float)((100 + iPolicyExponentMod));
		fExponent /= 100.0f;
	}

	int iAdditionalCost = GetJONSCultureLevel() * /*8*/ GC.getCULTURE_COST_LATER_PLOT_MULTIPLIER();
	iAdditionalCost = (int) pow((double) iAdditionalCost, (double)fExponent);

	iCultureThreshold += iAdditionalCost;

	// More expensive for Minors to claim territory
	if(GET_PLAYER(getOwner()).isMinorCiv())
	{
		iCultureThreshold *= /*150*/ GC.getMINOR_CIV_PLOT_CULTURE_COST_MULTIPLIER();
		iCultureThreshold /= 100;
	}

	// Religion modifier
	int iReligionMod = 0;
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(eMajority != NO_RELIGION)
	{
		const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
		if(pReligion)
		{
			iReligionMod = pReligion->m_Beliefs.GetPlotCultureCostModifier(getOwner());
			BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
			if (eSecondaryPantheon != NO_BELIEF)
			{
				iReligionMod += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetPlotCultureCostModifier();
			}
		}
	}

	// -50 = 50% cost
	int iModifier = GET_PLAYER(getOwner()).GetPlotCultureCostModifier() + m_iPlotCultureCostModifier + iReligionMod;
	if(iModifier != 0)
	{
		iModifier = max(iModifier, /*-85*/ GC.getCULTURE_PLOT_COST_MOD_MINIMUM());	// value cannot reduced by more than 85%
		iCultureThreshold *= (100 + iModifier);
		iCultureThreshold /= 100;
	}

	// Game Speed Mod
	iCultureThreshold *= GC.getGame().getGameSpeedInfo().getCulturePercent();
	iCultureThreshold /= 100;

	// Make the number not be funky
	int iDivisor = /*5*/ GC.getCULTURE_COST_VISIBLE_DIVISOR();
	if(iCultureThreshold > iDivisor * 2)
	{
		iCultureThreshold /= iDivisor;
		iCultureThreshold *= iDivisor;
	}

	return iCultureThreshold;
}


//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
int CvCity::getJONSCulturePerTurn(bool bStatic) const
#else
int CvCity::getJONSCulturePerTurn() const
#endif
{
	VALIDATE_OBJECT

	// No culture during Resistance
	if(IsResistance() || IsRazing())
	{
		return 0;
	}
#if defined(MOD_BALANCE_CORE)
	if(bStatic)
	{
		return (GetStaticYield(YIELD_CULTURE) / 100);
	}
#endif
	int iCulture = GetBaseJONSCulturePerTurn();

	int iModifier = 100;

	// City modifier
#if defined(MOD_API_UNIFIED_YIELDS)
	// getCultureRateModifier() is just the culture specific building modifiers
	// we want getBaseYieldRateModifier(YIELD_CULTURE) as well
	iModifier = getBaseYieldRateModifier(YIELD_CULTURE, getCultureRateModifier());
#else
	iModifier += getCultureRateModifier();
#endif

	// Player modifier
	iModifier += GET_PLAYER(getOwner()).GetJONSCultureCityModifier();

	// Wonder here?
	if(getNumWorldWonders() > 0)
		iModifier += GET_PLAYER(getOwner()).GetCultureWonderMultiplier();

	// Puppet?
	if(IsPuppet())
	{
		iModifier += GC.getPUPPET_CULTURE_MODIFIER();
	}

#if defined(MOD_DIPLOMACY_CITYSTATES)
	if(MOD_DIPLOMACY_CITYSTATES && GET_PLAYER(getOwner()).IsLeagueAid() && GET_PLAYER(getOwner()).GetLeagueCultureCityModifier() > 0)
	{
		iModifier += GET_PLAYER(getOwner()).GetLeagueCultureCityModifier();
	}
#endif

	iCulture *= iModifier;
	iCulture /= 100;

	return iCulture;
}

//	--------------------------------------------------------------------------------
int CvCity::GetBaseJONSCulturePerTurn() const
{
	VALIDATE_OBJECT

	int iCulturePerTurn = 0;
	iCulturePerTurn += GetJONSCulturePerTurnFromBuildings();
	iCulturePerTurn += GetJONSCulturePerTurnFromPolicies();
	iCulturePerTurn += GetJONSCulturePerTurnFromSpecialists();
#if defined(MOD_API_UNIFIED_YIELDS)
	// GetJONSCulturePerTurnFromSpecialists() uses the Specialists.CulturePerTurn column,
	// GetBaseYieldRateFromSpecialists(YIELD_CULTURE) gets everything else!
	iCulturePerTurn += GetBaseYieldRateFromSpecialists(YIELD_CULTURE);
	iCulturePerTurn += (GetYieldPerPopTimes100(YIELD_CULTURE) * getPopulation()) / 100;
#endif
#if defined(MOD_API_UNIFIED_YIELDS)
	if (IsRouteToCapitalConnected())
	{
		iCulturePerTurn += GET_PLAYER(getOwner()).GetYieldChangeTradeRoute(YIELD_CULTURE);
		iCulturePerTurn += GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldChangeTradeRoute(YIELD_CULTURE);
	}
#endif

#if defined(MOD_GLOBAL_GREATWORK_YIELDTYPES) || defined(MOD_API_UNIFIED_YIELDS)
	iCulturePerTurn += GetBaseYieldRateFromGreatWorks(YIELD_CULTURE);
#else
	iCulturePerTurn += GetJONSCulturePerTurnFromGreatWorks();
#endif
	iCulturePerTurn += GetBaseYieldRateFromTerrain(YIELD_CULTURE);
#if defined(MOD_API_UNIFIED_YIELDS)
	for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
	{
		FeatureTypes eFeature = (FeatureTypes) iI;
		if(eFeature != NO_FEATURE)
		{
			iCulturePerTurn += GetYieldPerTurnFromUnimprovedFeatures(eFeature, YIELD_CULTURE);
		}
	}
#endif
	iCulturePerTurn += GetJONSCulturePerTurnFromTraits();
	iCulturePerTurn += GetJONSCulturePerTurnFromReligion();
	iCulturePerTurn += GetJONSCulturePerTurnFromLeagues();

#if defined(MOD_API_UNIFIED_YIELDS)
	// Process production into culture
	iCulturePerTurn += (getBasicYieldRateTimes100(YIELD_PRODUCTION, false) / 100) * getProductionToYieldModifier(YIELD_CULTURE) / 100;

	// Culture from having trade routes
	iCulturePerTurn += GET_PLAYER(m_eOwner).GetTrade()->GetTradeValuesAtCityTimes100(this, YIELD_CULTURE) / 100;
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(MOD_BALANCE_CORE_POLICIES && eMajority != NO_RELIGION && eMajority > RELIGION_PANTHEON)
	{
		if(GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities() == eMajority)
		{	
			iCulturePerTurn += GET_PLAYER(getOwner()).getReligionYieldRateModifier(YIELD_CULTURE);
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	iCulturePerTurn += GetBaseYieldRateFromCSAlliance(YIELD_CULTURE);
	iCulturePerTurn += GetBaseYieldRateFromCSFriendship(YIELD_CULTURE);
	iCulturePerTurn += GetYieldPerTurnFromTraits(YIELD_CULTURE);
	iCulturePerTurn += GetYieldChangeFromCorporationFranchises(YIELD_CULTURE);
#endif
#if defined(MOD_BALANCE_CORE)
	iCulturePerTurn += GetEventCityYield(YIELD_CULTURE);
#endif

	return iCulturePerTurn;
}

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromBuildings() const
{
	VALIDATE_OBJECT
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	return GetBaseYieldRateFromBuildings(YIELD_CULTURE); 
#else
	return m_iJONSCulturePerTurnFromBuildings;
#endif
}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
//	--------------------------------------------------------------------------------
void CvCity::ChangeJONSCulturePerTurnFromBuildings(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iJONSCulturePerTurnFromBuildings = (m_iJONSCulturePerTurnFromBuildings + iChange);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromPolicies() const
{
	VALIDATE_OBJECT
	return m_iJONSCulturePerTurnFromPolicies;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeJONSCulturePerTurnFromPolicies(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iJONSCulturePerTurnFromPolicies = (m_iJONSCulturePerTurnFromPolicies + iChange);
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromSpecialists() const
{
	VALIDATE_OBJECT
	return m_iJONSCulturePerTurnFromSpecialists;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeJONSCulturePerTurnFromSpecialists(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iJONSCulturePerTurnFromSpecialists = (m_iJONSCulturePerTurnFromSpecialists + iChange);
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromGreatWorks() const
{
#if defined(MOD_GLOBAL_GREATWORK_YIELDTYPES) || defined(MOD_API_UNIFIED_YIELDS)
	return GetCityBuildings()->GetYieldFromGreatWorks(YIELD_CULTURE);
#else
	return GetCityBuildings()->GetCultureFromGreatWorks();
#endif
}

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromTraits() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(m_eOwner).GetPlayerTraits()->GetCityCultureBonus();
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::GetYieldPerTurnFromTraits(YieldTypes eYield) const
{
	if(!isCapital())
	{
		return 0;
	}
	//Currently only used by Arabian CBP UA.
	int iYield = (GET_PLAYER(m_eOwner).GetPlayerTraits()->GetYieldFromHistoricEvent(eYield) * GET_PLAYER(m_eOwner).GetNumHistoricEvents());
	return iYield;
}
#endif
//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromReligion() const
{
	VALIDATE_OBJECT
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	return GetBaseYieldRateFromReligion(YIELD_CULTURE);
#else
	return m_iJONSCulturePerTurnFromReligion;
#endif
}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
//	--------------------------------------------------------------------------------
void CvCity::ChangeJONSCulturePerTurnFromReligion(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iJONSCulturePerTurnFromReligion = (m_iJONSCulturePerTurnFromReligion + iChange);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::GetJONSCulturePerTurnFromLeagues() const
{
	VALIDATE_OBJECT
	int iValue = 0;

	iValue += (getNumWorldWonders() * GC.getGame().GetGameLeagues()->GetWorldWonderYieldChange(getOwner(), YIELD_CULTURE));

	return iValue;
}

//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
int CvCity::GetFaithPerTurn(bool bStatic) const
#else
int CvCity::GetFaithPerTurn() const
#endif
{
	VALIDATE_OBJECT

	// No faith during Resistance
	if(IsResistance() || IsRazing())
	{
		return 0;
	}
#if defined(MOD_BALANCE_CORE)
	if(bStatic)
	{
		return (GetStaticYield(YIELD_FAITH) / 100);
	}
#endif
	int iFaith = GetFaithPerTurnFromBuildings();
#if defined(MOD_API_UNIFIED_YIELDS)
	iFaith += GetBaseYieldRateFromSpecialists(YIELD_FAITH);
	iFaith += (GetYieldPerPopTimes100(YIELD_FAITH) * getPopulation()) / 100;
#endif
#if defined(MOD_API_UNIFIED_YIELDS)
	if (IsRouteToCapitalConnected())
	{
		iFaith += GET_PLAYER(getOwner()).GetYieldChangeTradeRoute(YIELD_FAITH);
		iFaith += GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldChangeTradeRoute(YIELD_FAITH);
	}
#endif

#if defined(MOD_GLOBAL_GREATWORK_YIELDTYPES) || defined(MOD_API_UNIFIED_YIELDS)
	iFaith += GetBaseYieldRateFromGreatWorks(YIELD_FAITH);
#endif
	iFaith += GetBaseYieldRateFromTerrain(YIELD_FAITH);
	iFaith += GetFaithPerTurnFromPolicies();
#if defined(MOD_API_UNIFIED_YIELDS)
	for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
	{
		FeatureTypes eFeature = (FeatureTypes) iI;
		if(eFeature != NO_FEATURE)
		{
			iFaith += GetYieldPerTurnFromUnimprovedFeatures(eFeature, YIELD_FAITH);
		}
	}
#else
	iFaith += GetFaithPerTurnFromTraits();
#endif
	iFaith += GetFaithPerTurnFromReligion();

#if defined(MOD_API_UNIFIED_YIELDS)
	// Process production into faith
	iFaith += (getBasicYieldRateTimes100(YIELD_PRODUCTION, false) / 100) * getProductionToYieldModifier(YIELD_FAITH) / 100;

	// Faith from having trade routes
	iFaith += GET_PLAYER(m_eOwner).GetTrade()->GetTradeValuesAtCityTimes100(this, YIELD_FAITH) / 100;
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(MOD_BALANCE_CORE_POLICIES && eMajority != NO_RELIGION && eMajority > RELIGION_PANTHEON)
	{
		if(GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities() == eMajority)
		{	
			iFaith += GET_PLAYER(getOwner()).getReligionYieldRateModifier(YIELD_FAITH);
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	iFaith += GetBaseYieldRateFromCSAlliance(YIELD_FAITH);
	iFaith += GetBaseYieldRateFromCSFriendship(YIELD_FAITH);
	iFaith += GetYieldPerTurnFromTraits(YIELD_FAITH);
	iFaith += GetYieldChangeFromCorporationFranchises(YIELD_FAITH);
#endif
#if defined(MOD_BALANCE_CORE)
	iFaith += GetEventCityYield(YIELD_FAITH);
#endif

#if defined(MOD_API_UNIFIED_YIELDS)
	int iModifier = 100;

	// City modifier
	iModifier = getBaseYieldRateModifier(YIELD_FAITH);

	// Puppet?
	if(IsPuppet())
	{
		iModifier += GC.getPUPPET_FAITH_MODIFIER();
	}

	iFaith *= iModifier;
	iFaith /= 100;
#else
	// Puppet?
	int iModifier = 0;
	if(IsPuppet())
	{
		iModifier = GC.getPUPPET_FAITH_MODIFIER();
		iFaith *= (100 + iModifier);
		iFaith /= 100;
	}
#endif

	return iFaith;
}

//	--------------------------------------------------------------------------------
int CvCity::GetFaithPerTurnFromBuildings() const
{
	VALIDATE_OBJECT
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	return GetBaseYieldRateFromBuildings(YIELD_FAITH);
#else
	return m_iFaithPerTurnFromBuildings;
#endif
}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
//	--------------------------------------------------------------------------------
void CvCity::ChangeFaithPerTurnFromBuildings(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iFaithPerTurnFromBuildings = (m_iFaithPerTurnFromBuildings + iChange);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::GetFaithPerTurnFromPolicies() const
{
	VALIDATE_OBJECT
	return m_iFaithPerTurnFromPolicies;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeFaithPerTurnFromPolicies(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iFaithPerTurnFromPolicies = (m_iFaithPerTurnFromPolicies + iChange);
	}
}

//	--------------------------------------------------------------------------------
#if defined(MOD_API_UNIFIED_YIELDS)
void CvCity::UpdateYieldPerXTerrain(YieldTypes eYield, TerrainTypes eTerrain)
{
	VALIDATE_OBJECT
	int iYield;

	int iValidTilesTerrain = 0;
	int iBaseYieldBuildings = 0;

	//Passed in a terrain? Let's only update that.
	if(eTerrain != NO_TERRAIN)
	{
		iBaseYieldBuildings = GetYieldPerXTerrainFromBuildingsTimes100(eTerrain, eYield);

		if(iBaseYieldBuildings > 0)
		{
			if(eTerrain == TERRAIN_MOUNTAIN)
			{
				iValidTilesTerrain = GetNearbyMountains();
			}
			else
			{
				iValidTilesTerrain = GetNumTerrainWorked(eTerrain);
			}

			iYield = (iValidTilesTerrain * iBaseYieldBuildings) / 100;

			//iDifference determines +/- of difference of old value
			int iDifference = iYield - GetYieldPerXTerrain(eTerrain, eYield);

			//Change base rate first
			ChangeBaseYieldRateFromBuildings(eYield, iDifference);

			//then set base rate for retrieval next time.
			SetYieldPerXTerrain(eTerrain, eYield, iYield);
		}
		else if(GetYieldPerXTerrain(eTerrain, eYield) > 0)
		{
			//No bonuses? Clear it out.
			ChangeBaseYieldRateFromBuildings(eYield, -GetYieldPerXTerrain(eTerrain, eYield));
			SetYieldPerXTerrain(eTerrain, eYield, 0);
		}
	}
	else
	{
		for (int iI = 0; iI < GC.getNumTerrainInfos(); iI++)
		{
			eTerrain = (TerrainTypes) iI;
			if(eTerrain == NO_TERRAIN)
			{
				continue;
			}
		
			iBaseYieldBuildings = GetYieldPerXTerrainFromBuildingsTimes100(eTerrain, eYield);

			if(iBaseYieldBuildings > 0)
			{
				if(eTerrain == TERRAIN_MOUNTAIN)
				{
					iValidTilesTerrain = GetNearbyMountains();
				}
				else
				{
					iValidTilesTerrain = GetNumTerrainWorked(eTerrain);
				}

				iYield = (iValidTilesTerrain * iBaseYieldBuildings) / 100;

				//iDifference determines +/- of difference of old value
				int iDifference = iYield - GetYieldPerXTerrain(eTerrain, eYield);

				//Change base rate first
				ChangeBaseYieldRateFromBuildings(eYield, iDifference);

				//then set base rate for retrieval next time.
				SetYieldPerXTerrain(eTerrain, eYield, iYield);
			}
			else if(GetYieldPerXTerrain(eTerrain, eYield) > 0)
			{
				//No bonuses? Clear it out.
				ChangeBaseYieldRateFromBuildings(eYield, -GetYieldPerXTerrain(eTerrain, eYield));
				SetYieldPerXTerrain(eTerrain, eYield, 0);
			}
		}
	}
}
void CvCity::UpdateYieldPerXTerrainFromReligion(YieldTypes eYield, TerrainTypes eTerrain)
{
	VALIDATE_OBJECT
	int iYield;

	int iValidTilesTerrain = 0;
	int iBaseYieldReligion = 0;
	ReligionTypes eReligionFounded = GetCityReligions()->GetReligiousMajority();
	if(eReligionFounded != NO_RELIGION)
	{
		const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligionFounded, getOwner());
		if(pReligion)
		{
			//Passed in a Terrain? Use that.
			if(eTerrain != NO_TERRAIN)
			{
				iYield = 0;
				iBaseYieldReligion = pReligion->m_Beliefs.GetYieldPerXTerrainTimes100(eTerrain, eYield, getOwner());
				if(iBaseYieldReligion > 0)
				{
					if(eTerrain == TERRAIN_MOUNTAIN)
					{
						iValidTilesTerrain = GetNearbyMountains();
					}
					else if(pReligion->m_Beliefs.RequiresNoFeature(getOwner()))
					{
						iValidTilesTerrain = GetNumFeaturelessTerrainWorked(eTerrain);
					}
					else
					{
						iValidTilesTerrain = GetNumTerrainWorked(eTerrain);
					}

					iYield += ((iValidTilesTerrain * iBaseYieldReligion) / 100);

					SetYieldPerXTerrainFromReligion(eTerrain, eYield, iYield);
				}
				else if(GetYieldPerXTerrainFromReligion(eTerrain, eYield) > 0)
				{
					SetYieldPerXTerrainFromReligion(eTerrain, eYield, 0);
				}
			}
			else
			{
				for (int iI = 0; iI < GC.getNumTerrainInfos(); iI++)
				{
					eTerrain = (TerrainTypes) iI;
					iYield = 0;
					if(eTerrain == NO_TERRAIN)
					{
						continue;
					}
					iBaseYieldReligion = pReligion->m_Beliefs.GetYieldPerXTerrainTimes100(eTerrain, eYield, getOwner());
					if(iBaseYieldReligion > 0)
					{
						if(eTerrain == TERRAIN_MOUNTAIN)
						{
							iValidTilesTerrain = GetNearbyMountains();
						}
						else if(pReligion->m_Beliefs.RequiresNoFeature(getOwner()))
						{
							iValidTilesTerrain = GetNumFeaturelessTerrainWorked(eTerrain);
						}
						else
						{
							iValidTilesTerrain = GetNumTerrainWorked(eTerrain);
						}

						iYield += ((iValidTilesTerrain * iBaseYieldReligion) / 100);

						SetYieldPerXTerrainFromReligion(eTerrain, eYield, iYield);
					}
					else if(GetYieldPerXTerrainFromReligion(eTerrain, eYield) > 0)
					{
						SetYieldPerXTerrainFromReligion(eTerrain, eYield, 0);
					}
				}	
			}
		}
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetNumTerrainWorked(TerrainTypes eTerrain)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumTerrainInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiNumTerrainWorked[eTerrain];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumTerrainWorked(TerrainTypes eTerrain, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumTerrainInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiNumTerrainWorked.setAt(eTerrain, m_paiNumTerrainWorked[eTerrain] + iChange);
	CvAssert(GetNumTerrainWorked(eTerrain) >= 0);

	//Update yields
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		UpdateYieldPerXTerrain(((YieldTypes)iI), eTerrain);
		UpdateYieldPerXTerrainFromReligion(((YieldTypes)iI), eTerrain);
	}
}

int CvCity::GetNumFeaturelessTerrainWorked(TerrainTypes eTerrain)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumTerrainInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiNumFeaturelessTerrainWorked[eTerrain];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumFeaturelessTerrainWorked(TerrainTypes eTerrain, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumTerrainInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiNumFeaturelessTerrainWorked.setAt(eTerrain, m_paiNumFeaturelessTerrainWorked[eTerrain] + iChange);
	CvAssert(GetNumFeaturelessTerrainWorked(eTerrain) >= 0);

	//Update yields
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		UpdateYieldPerXTerrainFromReligion(((YieldTypes)iI), eTerrain);
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetNumFeatureWorked(FeatureTypes eFeature)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumFeatureInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiNumFeatureWorked[eFeature];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumFeatureWorked(FeatureTypes eFeature, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumFeatureInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiNumFeatureWorked.setAt(eFeature, m_paiNumFeatureWorked[eFeature] + iChange);
	CvAssert(GetNumFeatureWorked(eFeature) >= 0);
	
	//Update yields
	for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		UpdateYieldPerXFeature(((YieldTypes)iI), eFeature);
	}
}
//	--------------------------------------------------------------------------------
int CvCity::GetNumResourceWorked(ResourceTypes eResource)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumResourceInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiNumResourceWorked[eResource];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumResourceWorked(ResourceTypes eResource, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumResourceInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiNumResourceWorked.setAt(eResource, m_paiNumResourceWorked[eResource] + iChange);
	CvAssert(GetNumResourceWorked(eResource) >= 0);
}
//	--------------------------------------------------------------------------------
int CvCity::GetNumImprovementWorked(ImprovementTypes eImprovement)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumImprovementInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiNumImprovementWorked[eImprovement];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumImprovementWorked(ImprovementTypes eImprovement, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumImprovementInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiNumImprovementWorked.setAt(eImprovement, m_paiNumImprovementWorked[eImprovement] + iChange);
	CvAssert(GetNumImprovementWorked(eImprovement) >= 0);
}
//	--------------------------------------------------------------------------------
void CvCity::SetYieldPerXTerrain(TerrainTypes eTerrain, YieldTypes eYield, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(m_ppaiYieldPerXTerrain[eTerrain][eYield] != iValue)
	{
		m_ppaiYieldPerXTerrain[eTerrain][eYield] = iValue;

		updateYield();
	}
}
//	--------------------------------------------------------------------------------
void CvCity::SetYieldPerXTerrainFromReligion(TerrainTypes eTerrain, YieldTypes eYield, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(m_ppaiYieldPerXTerrainFromReligion[eTerrain][eYield] != iValue)
	{
		m_ppaiYieldPerXTerrainFromReligion[eTerrain][eYield] = iValue;

		updateYield();
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield for a Terrain this city is working?
int CvCity::GetYieldPerXTerrain(TerrainTypes eTerrain, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiYieldPerXTerrain[eTerrain][eYield];
}
//	--------------------------------------------------------------------------------
/// Extra yield for a Terrain this city is working?
int CvCity::GetYieldPerXTerrainFromReligion(TerrainTypes eTerrain, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiYieldPerXTerrainFromReligion[eTerrain][eYield];
}
//	--------------------------------------------------------------------------------
/// Extra yield for a Terrain this city is working?
int CvCity::GetYieldPerXTerrainFromBuildingsTimes100(TerrainTypes eTerrain, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiYieldPerXTerrainFromBuildings[eTerrain][eYield];
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeYieldPerXTerrainFromBuildingsTimes100(TerrainTypes eTerrain, YieldTypes eYield, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eTerrain > -1 && eTerrain < GC.getNumTerrainInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(iChange != 0)
	{
		m_ppaiYieldPerXTerrainFromBuildings[eTerrain][eYield] += iChange;

		updateYield();
		UpdateYieldPerXTerrain(eYield, eTerrain);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield for a Feature this city is working?
int CvCity::GetYieldPerXFeature(FeatureTypes eFeature, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiYieldPerXFeature[eFeature][eYield];
}
//	--------------------------------------------------------------------------------
/// Extra yield for an unimproved Feature this city is working?
int CvCity::GetYieldPerTurnFromUnimprovedFeatures(FeatureTypes eFeature, YieldTypes eYield) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Terrain index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	return m_ppaiYieldPerXUnimprovedFeature[eFeature][eYield];
}
//	--------------------------------------------------------------------------------
void CvCity::SetYieldPerXFeature(FeatureTypes eFeature, YieldTypes eYield, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Feature index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(m_ppaiYieldPerXFeature[eFeature][eYield] != iValue)
	{
		m_ppaiYieldPerXFeature[eFeature][eYield] = iValue;

		updateYield();
	}
}
//	--------------------------------------------------------------------------------
void CvCity::SetYieldPerXUnimprovedFeature(FeatureTypes eFeature, YieldTypes eYield, int iValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eFeature > -1 && eFeature < GC.getNumFeatureInfos(), "Invalid Feature index.");
	CvAssertMsg(eYield > -1 && eYield < NUM_YIELD_TYPES, "Invalid yield index.");

	if(m_ppaiYieldPerXUnimprovedFeature[eFeature][eYield] != iValue)
	{
		m_ppaiYieldPerXUnimprovedFeature[eFeature][eYield] = iValue;

		updateYield();
	}
}
void CvCity::UpdateYieldPerXUnimprovedFeature(YieldTypes eYield, FeatureTypes eFeature)
{
	VALIDATE_OBJECT
	int iYield = 0;

	CvPlayer& kPlayer = GET_PLAYER(getOwner());
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	const CvReligion* pReligion = (eMajority == NO_RELIGION) ? NULL : GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
	BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();

	//Passed in a feature? Use that.
	if(eFeature != NO_FEATURE)
	{
#if defined(MOD_PSEUDO_NATURAL_WONDER)
		if (!GC.getFeatureInfo(eFeature)->IsNaturalWonder(true))
#else
		if (!GC.getFeatureInfo(eFeature)->IsNaturalWonder())
#endif
		{
			int iBaseYield = kPlayer.getCityYieldFromUnimprovedFeature(eFeature, eYield);
			iBaseYield += kPlayer.GetPlayerTraits()->GetCityYieldFromUnimprovedFeature(eFeature, eYield);
		
			if(pReligion)
			{
				iBaseYield += pReligion->m_Beliefs.GetCityYieldFromUnimprovedFeature(eFeature, eYield, getOwner());
				if (eSecondaryPantheon != NO_BELIEF)
				{
					iBaseYield += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityYieldFromUnimprovedFeature(eFeature, eYield);
				}
			}

			if (eYield == YIELD_FAITH && eFeature == FEATURE_FOREST && kPlayer.GetPlayerTraits()->IsFaithFromUnimprovedForest())
			{
				++iBaseYield;
			}
		
			if (iBaseYield > 0)
			{
				int iAdjacentFeatures = 0;

				for (int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
				{
					CvPlot* pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));
					if (pAdjacentPlot && pAdjacentPlot->getFeatureType() == eFeature && pAdjacentPlot->getImprovementType() == NO_IMPROVEMENT)
					{
						iAdjacentFeatures++;
					}
				}
				if (iAdjacentFeatures > 2)
				{
#if defined(MOD_ALTERNATE_CELTS)
					if(MOD_ALTERNATE_CELTS)
					{
						iYield += iBaseYield * 3;
					}
					else
					{
						iYield += iBaseYield * 2;
					}
#else
					iYield += iBaseYield * 2;
#endif
				}
#if defined(MOD_ALTERNATE_CELTS)
				else if (iAdjacentFeatures > 1 && MOD_ALTERNATE_CELTS)
				{
					iYield += iBaseYield * 2;
				}
#endif
				else if (iAdjacentFeatures > 0)
				{
					iYield += iBaseYield;
				}
				SetYieldPerXUnimprovedFeature(eFeature, eYield, iYield);
			}
		}
	}
	else
	{
		for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
		{
			eFeature = (FeatureTypes) iI;
#if defined(MOD_PSEUDO_NATURAL_WONDER)
			if (!GC.getFeatureInfo(eFeature)->IsNaturalWonder(true))
#else
			if (!GC.getFeatureInfo(eFeature)->IsNaturalWonder())
#endif
			{
				int iBaseYield = kPlayer.getCityYieldFromUnimprovedFeature(eFeature, eYield);
				iBaseYield += kPlayer.GetPlayerTraits()->GetCityYieldFromUnimprovedFeature(eFeature, eYield);
		
				if(pReligion)
				{
					iBaseYield += pReligion->m_Beliefs.GetCityYieldFromUnimprovedFeature(eFeature, eYield, getOwner());
					if (eSecondaryPantheon != NO_BELIEF)
					{
						iBaseYield += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityYieldFromUnimprovedFeature(eFeature, eYield);
					}
				}

				if (eYield == YIELD_FAITH && eFeature == FEATURE_FOREST && kPlayer.GetPlayerTraits()->IsFaithFromUnimprovedForest())
				{
					++iBaseYield;
				}
		
				if (iBaseYield > 0)
				{
					int iAdjacentFeatures = 0;

					for (int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
					{
						CvPlot* pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));
						if (pAdjacentPlot && pAdjacentPlot->getFeatureType() == eFeature && pAdjacentPlot->getImprovementType() == NO_IMPROVEMENT)
						{
							iAdjacentFeatures++;
						}
					}
					if (iAdjacentFeatures > 2)
					{
#if defined(MOD_ALTERNATE_CELTS)
						if(MOD_ALTERNATE_CELTS)
						{
							iYield += iBaseYield * 3;
						}
						else
						{
							iYield += iBaseYield * 2;
						}
#else
						iYield += iBaseYield * 2;
#endif
					}
#if defined(MOD_ALTERNATE_CELTS)
					else if (iAdjacentFeatures > 1 && MOD_ALTERNATE_CELTS)
					{
						iYield += iBaseYield * 2;
					}
#endif
					else if (iAdjacentFeatures > 0)
					{
						iYield += iBaseYield;
					}
					SetYieldPerXUnimprovedFeature(eFeature, eYield, iYield);
				}
			}
		}
	}
}
void CvCity::UpdateYieldPerXFeature(YieldTypes eYield, FeatureTypes eFeature)
{
	VALIDATE_OBJECT
	int iYield;

	int iValidTiles = 0;
	int iBaseYield = 0;
	ReligionTypes eReligionFounded = GetCityReligions()->GetReligiousMajority();
	if(eReligionFounded != NO_RELIGION)
	{
		const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligionFounded, getOwner());
		if(pReligion)
		{
			//If we passed in a feature, let's only refresh that.
			if(eFeature != NO_FEATURE)
			{
				iYield = 0;
				iBaseYield = pReligion->m_Beliefs.GetYieldPerXFeatureTimes100(eFeature, eYield, getOwner());
				if(iBaseYield > 0)
				{
					iValidTiles = GetNumFeatureWorked(eFeature);
					if(iValidTiles > 0)
					{
						//Gain 1 yield per x valid tiles - so if 'x' is 3, and you have 3 tiles that match, you get 1 yield
						iYield = (iValidTiles * iBaseYield) / 100;
					}
				}
				SetYieldPerXFeature(eFeature, eYield, iYield);
			}
			else
			{
				for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
				{
					eFeature = (FeatureTypes) iI;
					if(eFeature == NO_FEATURE)
					{
						continue;
					}
					iYield = 0;
					if(pReligion)
					{
						iBaseYield = pReligion->m_Beliefs.GetYieldPerXFeatureTimes100(eFeature, eYield, getOwner());
						if(iBaseYield > 0)
						{
							iValidTiles = GetNumFeatureWorked(eFeature);
							if(iValidTiles > 0)
							{
								//Gain 1 yield per x valid tiles - so if 'x' is 3, and you have 3 tiles that match, you get 1 yield
								iYield = (iValidTiles * iBaseYield) / 100;
							}
						}
					}
					SetYieldPerXFeature(eFeature, eYield, iYield);
				}
			}
		}
	}
}
#else
int CvCity::GetFaithPerTurnFromTraits() const
{
	VALIDATE_OBJECT

	int iRtnValue = 0;

	if(GET_PLAYER(m_eOwner).GetPlayerTraits()->IsFaithFromUnimprovedForest())
	{
		// See how many tiles adjacent to city are unimproved forest
		int iAdjacentForests = 0;

		for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; ++iDirectionLoop)
		{
			CvPlot* pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));
			if(pAdjacentPlot != NULL)
			{
				if(pAdjacentPlot->getFeatureType() == FEATURE_FOREST && pAdjacentPlot->getImprovementType() == NO_IMPROVEMENT)
				{
					iAdjacentForests++;
				}
			}
		}

		// If 3 or more, bonus is +2
		if(iAdjacentForests > 2)
		{
			iRtnValue = 2;
		}
		else if(iAdjacentForests > 0)
		{
			iRtnValue = 1;
		}
	}

	return iRtnValue;
}
#endif
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::getHurryModifier(HurryTypes eIndex) const
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumHurryInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiHurryModifier[eIndex];
}

//	--------------------------------------------------------------------------------
void CvCity::changeHurryModifier(HurryTypes eIndex, int iChange)
{
	if(iChange != 0)
	{
		CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
		CvAssertMsg(eIndex < GC.getNumHurryInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
		m_paiHurryModifier.setAt(eIndex, m_paiHurryModifier[eIndex] + iChange);
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getSpecialistExtraYield(SpecialistTypes eIndex1, YieldTypes eIndex2) const
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 expected to be >= 0");
	CvAssertMsg(eIndex1 < GC.getNumSpecialistInfos(), "eIndex1 expected to be < GC.getNumSpecialistInfos()");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 expected to be >= 0");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 expected to be < NUM_YIELD_TYPES");
	return m_ppaaiSpecialistExtraYield[eIndex1][eIndex2];
}


//	--------------------------------------------------------------------------------
void CvCity::changeSpecialistExtraYield(SpecialistTypes eIndex1, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 expected to be >= 0");
	CvAssertMsg(eIndex1 < GC.getNumSpecialistInfos(), "eIndex1 expected to be < GC.getNumSpecialistInfos()");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 expected to be >= 0");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 expected to be < NUM_YIELD_TYPES");

	if (iChange != 0)
	{
		Firaxis::Array<int, NUM_YIELD_TYPES> yields = m_ppaaiSpecialistExtraYield[eIndex1];
		yields[eIndex2] = (m_ppaaiSpecialistExtraYield[eIndex1][eIndex2] + iChange);
		m_ppaaiSpecialistExtraYield.setAt(eIndex1, yields);
		CvAssert(getSpecialistExtraYield(eIndex1, eIndex2) >= 0);

		updateExtraSpecialistYield();
	}
}

#endif

//	--------------------------------------------------------------------------------
int CvCity::GetFaithPerTurnFromReligion() const
{
	VALIDATE_OBJECT
#if defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
	return GetBaseYieldRateFromReligion(YIELD_FAITH);
#else
	return m_iFaithPerTurnFromReligion;
#endif
}

#if !defined(MOD_API_UNIFIED_YIELDS_CONSOLIDATION)
//	--------------------------------------------------------------------------------
void CvCity::ChangeFaithPerTurnFromReligion(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iFaithPerTurnFromReligion = (m_iFaithPerTurnFromReligion + iChange);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getCultureRateModifier() const
{
	VALIDATE_OBJECT
	return m_iCultureRateModifier;
}

//	--------------------------------------------------------------------------------
void CvCity::changeCultureRateModifier(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iCultureRateModifier = (m_iCultureRateModifier + iChange);
	}
}
#if defined(MOD_BALANCE_CORE_POLICIES)
//	--------------------------------------------------------------------------------
int CvCity::getBuildingClassCultureChange(BuildingClassTypes eIndex) const
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumBuildingClassInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	return m_paiBuildingClassCulture[eIndex];
}
//	--------------------------------------------------------------------------------
void CvCity::changeBuildingClassCultureChange(BuildingClassTypes eIndex, int iChange)
{
	CvAssertMsg(eIndex >= 0, "eIndex is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex < GC.getNumBuildingClassInfos(), "eIndex is expected to be within maximum bounds (invalid Index)");
	m_paiBuildingClassCulture.setAt(eIndex, m_paiBuildingClassCulture[eIndex] + iChange);
	CvAssert(getBuildingClassCultureChange(eIndex) >= 0);
}
#endif
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::GetBaseTourism() const
{
	VALIDATE_OBJECT
	return m_iBaseTourism;
}

//	--------------------------------------------------------------------------------
void CvCity::SetBaseTourism(int iChange)
{
	VALIDATE_OBJECT
	m_iBaseTourism = iChange;
}
//	--------------------------------------------------------------------------------
int CvCity::GetBaseTourismBeforeModifiers() const
{
	VALIDATE_OBJECT
	return m_iBaseTourismBeforeModifiers;
}

//	--------------------------------------------------------------------------------
void CvCity::SetBaseTourismBeforeModifiers(int iChange)
{
	VALIDATE_OBJECT
	m_iBaseTourismBeforeModifiers = iChange;
}
#endif
#if defined(MOD_API_EXTENSIONS)
//	--------------------------------------------------------------------------------
int CvCity::getTourismRateModifier() const
{
	VALIDATE_OBJECT
	return (GetCityBuildings()->GetGreatWorksTourismModifier() + GET_PLAYER(getOwner()).GetGreatWorksTourismModifierGlobal());
}

//	--------------------------------------------------------------------------------
void CvCity::changeTourismRateModifier(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		GetCityBuildings()->ChangeGreatWorksTourismModifier(iChange);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getNumWorldWonders() const
{
	VALIDATE_OBJECT
	return m_iNumWorldWonders;
}


//	--------------------------------------------------------------------------------
void CvCity::changeNumWorldWonders(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iNumWorldWonders = (m_iNumWorldWonders + iChange);
		CvAssert(getNumWorldWonders() >= 0);

		// Extra culture for Wonders (Policies, etc.)
		ChangeJONSCulturePerTurnFromPolicies(GET_PLAYER(getOwner()).GetCulturePerWonder() * iChange);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getNumTeamWonders() const
{
	VALIDATE_OBJECT
	return m_iNumTeamWonders;
}


//	--------------------------------------------------------------------------------
void CvCity::changeNumTeamWonders(int iChange)
{
	VALIDATE_OBJECT
	m_iNumTeamWonders = (m_iNumTeamWonders + iChange);
	CvAssert(getNumTeamWonders() >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getNumNationalWonders() const
{
	VALIDATE_OBJECT
	return m_iNumNationalWonders;
}


//	--------------------------------------------------------------------------------
void CvCity::changeNumNationalWonders(int iChange)
{
	VALIDATE_OBJECT
	m_iNumNationalWonders = (m_iNumNationalWonders + iChange);
	CvAssert(getNumNationalWonders() >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::GetWonderProductionModifier() const
{
	VALIDATE_OBJECT
	return m_iWonderProductionModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::ChangeWonderProductionModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iWonderProductionModifier = (m_iWonderProductionModifier + iChange);
	CvAssert(GetWonderProductionModifier() >= 0);
}

//	--------------------------------------------------------------------------------
int CvCity::GetLocalResourceWonderProductionMod(BuildingTypes eBuilding, CvString* toolTipSink) const
{
	VALIDATE_OBJECT

	int iMultiplier = 0;

	CvAssertMsg(eBuilding > -1 && eBuilding < GC.getNumBuildingInfos(), "Invalid building index. Please show Jon.");
	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo)
	{
		// Is this even a wonder?
		const CvBuildingClassInfo& kBuildingClassInfo = pkBuildingInfo->GetBuildingClassInfo();
		if(!::isWorldWonderClass(kBuildingClassInfo) &&
		        !::isTeamWonderClass(kBuildingClassInfo) &&
		        !::isNationalWonderClass(kBuildingClassInfo))
		{
			return 0;
		}
		
#if defined(MOD_TRADE_WONDER_RESOURCE_ROUTES)
		// Are we using a trade route to ship the wonder resource from/to this city?
		bool bWonderResourceIn = false;
		bool bWonderResourceOut = false;
		if (MOD_TRADE_WONDER_RESOURCE_ROUTES) {
			CvGameTrade* pGameTrade = GC.getGame().GetGameTrade();
			for (uint ui = 0; ui < pGameTrade->GetNumTradeConnections(); ui++)
			{
				if (pGameTrade->IsTradeRouteIndexEmpty(ui))
				{
					continue;
				}

				if (pGameTrade->GetTradeConnection(ui).m_eConnectionType == TRADE_CONNECTION_WONDER_RESOURCE)
				{
					CvCity* pOriginCity = CvGameTrade::GetOriginCity(pGameTrade->GetTradeConnection(ui));
					CvCity* pDestCity = CvGameTrade::GetDestCity(pGameTrade->GetTradeConnection(ui));

					if (pDestCity->getX() == getX() && pDestCity->getY() == getY())
					{
						ResourceTypes eWonderResource = ::getWonderResource();
						bWonderResourceIn = (eWonderResource != NO_RESOURCE && pOriginCity->GetNumResourceLocal(eWonderResource) > 0);
					}
					else
					{
						if (pOriginCity->getX() == getX() && pOriginCity->getY() == getY())
						{
							bWonderResourceOut = true;
						}
					}

					break;
				}
			}
			// if (bWonderResourceIn) CUSTOMLOG("Shipping a wonder resource into %s", getName().c_str());
			// if (bWonderResourceOut) CUSTOMLOG("Shipping a wonder resource out of %s", getName().c_str());
		}
#endif

		// Resource wonder bonus
		for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
		{
			const ResourceTypes eResource = static_cast<ResourceTypes>(iResourceLoop);
			CvResourceInfo* pkResource = GC.getResourceInfo(eResource);
			if(pkResource)
			{
				int iBonus = pkResource->getWonderProductionMod();
				if(iBonus != 0)
				{
#if defined(MOD_TRADE_WONDER_RESOURCE_ROUTES)
					bool bHasLocalResource = IsHasResourceLocal(eResource, /*bTestVisible*/ false);
					if (MOD_TRADE_WONDER_RESOURCE_ROUTES) {
						// We're shipping one in, or we're not shipping our only one out, or we have more than one
						bHasLocalResource = bWonderResourceIn || (m_paiNumResourcesLocal[eResource] == 1 && !bWonderResourceOut) || (m_paiNumResourcesLocal[eResource] > 1);
					}
					if(bHasLocalResource)
#else
					if(IsHasResourceLocal(eResource, /*bTestVisible*/ false))
#endif
					{
						// Depends on era of wonder?
						EraTypes eResourceObsoleteEra = pkResource->getWonderProductionModObsoleteEra();
						if (eResourceObsoleteEra != NO_ERA)
						{
							EraTypes eWonderEra;
							TechTypes eTech = (TechTypes)pkBuildingInfo->GetPrereqAndTech();
							if(eTech != NO_TECH)
							{
								CvTechEntry* pEntry = GC.GetGameTechs()->GetEntry(eTech);
								if(pEntry)
								{
									eWonderEra = (EraTypes)pEntry->GetEra();
									if(eWonderEra != NO_ERA)
									{
										if (eWonderEra >= eResourceObsoleteEra)
										{
											continue;
										}
									}
								}
							}
						}

						iMultiplier += iBonus;
						GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_WONDER_LOCAL_RES", iBonus, pkBuildingInfo->GetDescription());
					}
				}
			}
		}

	}

	return iMultiplier;
}


//	--------------------------------------------------------------------------------
int CvCity::getCapturePlunderModifier() const
{
	VALIDATE_OBJECT
	return m_iCapturePlunderModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::changeCapturePlunderModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iCapturePlunderModifier = (m_iCapturePlunderModifier + iChange);
	CvAssert(m_iCapturePlunderModifier >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getPlotCultureCostModifier() const
{
	VALIDATE_OBJECT
	return m_iPlotCultureCostModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::changePlotCultureCostModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iPlotCultureCostModifier = (m_iPlotCultureCostModifier + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getPlotBuyCostModifier() const
{
	VALIDATE_OBJECT
	return m_iPlotBuyCostModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::changePlotBuyCostModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iPlotBuyCostModifier = (m_iPlotBuyCostModifier + iChange);
}

#if defined(MOD_BUILDINGS_CITY_WORKING)
//	--------------------------------------------------------------------------------
int CvCity::GetCityWorkingChange() const
{
	VALIDATE_OBJECT
	return m_iCityWorkingChange;
}


//	--------------------------------------------------------------------------------
void CvCity::changeCityWorkingChange(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		int iOldPlots = GetNumWorkablePlots();
		m_iCityWorkingChange = (m_iCityWorkingChange + iChange);
		int iNewPlots = GetNumWorkablePlots();
			
		for (int iI = std::min(iOldPlots, iNewPlots); iI < std::max(iOldPlots, iNewPlots); ++iI) {
			CvPlot* pLoopPlot = iterateRingPlots(getX(), getY(), iI);

			if (pLoopPlot) {
				pLoopPlot->changeCityRadiusCount(iChange);
				pLoopPlot->changePlayerCityRadiusCount(getOwner(), iChange);
			}
		}
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getHealRate() const
{
	VALIDATE_OBJECT
	return m_iHealRate;
}

//	--------------------------------------------------------------------------------
void CvCity::changeHealRate(int iChange)
{
	VALIDATE_OBJECT
	m_iHealRate = (m_iHealRate + iChange);
	CvAssert(getHealRate() >= 0);
}

//	--------------------------------------------------------------------------------
int CvCity::GetEspionageModifier() const
{
	VALIDATE_OBJECT
	return m_iEspionageModifier;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeEspionageModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iEspionageModifier = (m_iEspionageModifier + iChange);
}

#if defined(MOD_RELIGION_CONVERSION_MODIFIERS)
//	--------------------------------------------------------------------------------
int CvCity::GetConversionModifier() const
{
	VALIDATE_OBJECT
	return m_iConversionModifier;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeConversionModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iConversionModifier = (m_iConversionModifier + iChange);
}
#endif

//	--------------------------------------------------------------------------------
/// Does this city not produce occupied Unhappiness?
bool CvCity::IsNoOccupiedUnhappiness() const
{
	VALIDATE_OBJECT
	return GetNoOccupiedUnhappinessCount() > 0;
}

//	--------------------------------------------------------------------------------
/// Does this city not produce occupied Unhappiness?
int CvCity::GetNoOccupiedUnhappinessCount() const
{
	VALIDATE_OBJECT
	return m_iNoOccupiedUnhappinessCount;
}

//	--------------------------------------------------------------------------------
/// Does this city not produce occupied Unhappiness?
void CvCity::ChangeNoOccupiedUnhappinessCount(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
		m_iNoOccupiedUnhappinessCount += iChange;
}


//	--------------------------------------------------------------------------------
int CvCity::getFood() const
{
	VALIDATE_OBJECT
	return m_iFood / 100;
}

//	--------------------------------------------------------------------------------
int CvCity::getFoodTimes100() const
{
	VALIDATE_OBJECT
	return m_iFood;
}


//	--------------------------------------------------------------------------------
void CvCity::setFood(int iNewValue)
{
	VALIDATE_OBJECT
	setFoodTimes100(iNewValue*100);
}

//	--------------------------------------------------------------------------------
void CvCity::setFoodTimes100(int iNewValue)
{
	VALIDATE_OBJECT
	if(getFoodTimes100() != iNewValue)
	{
		m_iFood = iNewValue;
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeFood(int iChange)
{
	VALIDATE_OBJECT
	setFoodTimes100(getFoodTimes100() + 100 * iChange);
}


//	--------------------------------------------------------------------------------
void CvCity::changeFoodTimes100(int iChange)
{
	VALIDATE_OBJECT
	setFoodTimes100(getFoodTimes100() + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getFoodKept() const
{
	VALIDATE_OBJECT
	return m_iFoodKept;
}

//	--------------------------------------------------------------------------------
void CvCity::setFoodKept(int iNewValue)
{
	VALIDATE_OBJECT
	m_iFoodKept = iNewValue;
}


//	--------------------------------------------------------------------------------
void CvCity::changeFoodKept(int iChange)
{
	VALIDATE_OBJECT
	setFoodKept(getFoodKept() + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getMaxFoodKeptPercent() const
{
	VALIDATE_OBJECT
	return m_iMaxFoodKeptPercent;
}


//	--------------------------------------------------------------------------------
void CvCity::changeMaxFoodKeptPercent(int iChange)
{
	VALIDATE_OBJECT
	m_iMaxFoodKeptPercent = (m_iMaxFoodKeptPercent + iChange);
	CvAssert(getMaxFoodKeptPercent() >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getOverflowProduction() const
{
	VALIDATE_OBJECT
	return m_iOverflowProduction / 100;
}


//	--------------------------------------------------------------------------------
void CvCity::setOverflowProduction(int iNewValue)
{
	VALIDATE_OBJECT
	setOverflowProductionTimes100(iNewValue * 100);
}


//	--------------------------------------------------------------------------------
void CvCity::changeOverflowProduction(int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(iChange >= 0, "Production overflow is too low.  Please send a save to Ed.");
	CvAssertMsg(iChange < 250, "Production overflow is too high.  Please send a save to Ed.");
	changeOverflowProductionTimes100(iChange * 100);
}


//	--------------------------------------------------------------------------------
int CvCity::getOverflowProductionTimes100() const
{
	VALIDATE_OBJECT
	return m_iOverflowProduction;
}


//	--------------------------------------------------------------------------------
void CvCity::setOverflowProductionTimes100(int iNewValue)
{
	VALIDATE_OBJECT
	m_iOverflowProduction = iNewValue;
	CvAssert(getOverflowProductionTimes100() >= 0);
}


//	--------------------------------------------------------------------------------
void CvCity::changeOverflowProductionTimes100(int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(iChange >= 0, "Production overflow is too low.  Please send a save to Ed.");
	CvAssertMsg(iChange < 25000, "Production overflow is too high.  Please send a save to Ed.");
	setOverflowProductionTimes100(getOverflowProductionTimes100() + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getFeatureProduction() const
{
	VALIDATE_OBJECT
	return m_iFeatureProduction;
}


//	--------------------------------------------------------------------------------
void CvCity::setFeatureProduction(int iNewValue)
{
	VALIDATE_OBJECT
	m_iFeatureProduction = iNewValue;
	CvAssert(getFeatureProduction() >= 0);
}


//	--------------------------------------------------------------------------------
void CvCity::changeFeatureProduction(int iChange)
{
	VALIDATE_OBJECT
	setFeatureProduction(getFeatureProduction() + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getMilitaryProductionModifier()	const
{
	VALIDATE_OBJECT
	return m_iMilitaryProductionModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::changeMilitaryProductionModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iMilitaryProductionModifier = (m_iMilitaryProductionModifier + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getSpaceProductionModifier() const
{
	VALIDATE_OBJECT
	return m_iSpaceProductionModifier;
}


//	--------------------------------------------------------------------------------
void CvCity::changeSpaceProductionModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iSpaceProductionModifier = (m_iSpaceProductionModifier + iChange);
}


//	--------------------------------------------------------------------------------
int CvCity::getFreeExperience() const
{
	VALIDATE_OBJECT
	return m_iFreeExperience;
}


//	--------------------------------------------------------------------------------
void CvCity::changeFreeExperience(int iChange)
{
	VALIDATE_OBJECT
	m_iFreeExperience = (m_iFreeExperience + iChange);
	CvAssert(getFreeExperience() >= 0);
}

//	--------------------------------------------------------------------------------
bool CvCity::CanAirlift() const
{
	int iBuildingClassLoop;
	BuildingClassTypes eBuildingClass;
	CvPlayer &kPlayer = GET_PLAYER(getOwner());

	for(iBuildingClassLoop = 0; iBuildingClassLoop < GC.getNumBuildingClassInfos(); iBuildingClassLoop++)
	{
		eBuildingClass = (BuildingClassTypes) iBuildingClassLoop;

		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
		if(!pkBuildingClassInfo)
		{
			continue;
		}

		BuildingTypes eBuilding = (BuildingTypes)kPlayer.getCivilizationInfo().getCivilizationBuildings(eBuildingClass);
		if(eBuilding != NO_BUILDING && GetCityBuildings()->GetNumBuilding(eBuilding) > 0) // slewis - added the NO_BUILDING check for the ConquestDLX scenario which has civ specific wonders
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
			if(!pkBuildingInfo)
			{
				continue;
			}

			if (pkBuildingInfo->IsAirlift())
			{
				return true;
			}
		}
	}

#if defined(MOD_EVENTS_CITY_AIRLIFT)
	if (MOD_EVENTS_CITY_AIRLIFT) {
		if (GAMEEVENTINVOKE_TESTANY(GAMEEVENT_CityCanAirlift, getOwner(), GetID()) == GAMEEVENTRETURN_TRUE) {
			return true;
		}
	}
#endif				

	return false;
}

//	--------------------------------------------------------------------------------
int CvCity::GetMaxAirUnits() const
{
	VALIDATE_OBJECT
	return m_iMaxAirUnits;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeMaxAirUnits(int iChange)
{
	VALIDATE_OBJECT
	m_iMaxAirUnits += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::getNukeModifier() const
{
	VALIDATE_OBJECT
	return m_iNukeModifier;
}
//	--------------------------------------------------------------------------------
void CvCity::changeNukeModifier(int iChange)
{
	VALIDATE_OBJECT
	m_iNukeModifier = (m_iNukeModifier + iChange);
}

//	--------------------------------------------------------------------------------
int CvCity::GetTradeRouteTargetBonus() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteTargetBonus;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeTradeRouteTargetBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iTradeRouteTargetBonus += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetTradeRouteRecipientBonus() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteRecipientBonus;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeTradeRouteRecipientBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iTradeRouteRecipientBonus += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetTradeRouteSeaGoldBonus() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteSeaGoldBonus;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeTradeRouteSeaGoldBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iTradeRouteSeaGoldBonus += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetTradeRouteLandGoldBonus() const
{
	VALIDATE_OBJECT
	return m_iTradeRouteLandGoldBonus;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeTradeRouteLandGoldBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iTradeRouteLandGoldBonus += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetNumTradeRouteBonus() const
{
	VALIDATE_OBJECT
		return m_iNumTradeRouteBonus;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeNumTradeRouteBonus(int iChange)
{
	VALIDATE_OBJECT
		m_iNumTradeRouteBonus += iChange;
}



//	--------------------------------------------------------------------------------
bool CvCity::IsResistance() const
{
	VALIDATE_OBJECT
	return GetResistanceTurns() > 0;
}

//	--------------------------------------------------------------------------------
int CvCity::GetResistanceTurns() const
{
	VALIDATE_OBJECT
	return m_iResistanceTurns;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeResistanceTurns(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iResistanceTurns += iChange;

		auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
		DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
	}
}

//	--------------------------------------------------------------------------------
void CvCity::DoResistanceTurn()
{
	VALIDATE_OBJECT
	if(IsResistance())
	{
		ChangeResistanceTurns(-1);
	}
}

//	--------------------------------------------------------------------------------
bool CvCity::IsRazing() const
{
	VALIDATE_OBJECT
	return GetRazingTurns() > 0;
}

//	--------------------------------------------------------------------------------
int CvCity::GetRazingTurns() const
{
	VALIDATE_OBJECT
	return m_iRazingTurns;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeRazingTurns(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		m_iRazingTurns += iChange;


		auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
		DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
	}
}

//	--------------------------------------------------------------------------------
bool CvCity::DoRazingTurn()
{
	VALIDATE_OBJECT

	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::DoRazingTurn, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	if(IsRazing())
	{
		CvPlayer& kPlayer = GET_PLAYER(getOwner());
		int iPopulationDrop = 1;
		iPopulationDrop *= (100 + kPlayer.GetPlayerTraits()->GetRazeSpeedModifier());
		iPopulationDrop /= 100;

		ChangeRazingTurns(-1);

		// Counter has reached 0, disband the City
		if(GetRazingTurns() <= 0 || getPopulation() <= 1)
		{
			CvPlot* pkPlot = plot();

			pkPlot->AddArchaeologicalRecord(CvTypes::getARTIFACT_RAZED_CITY(), getOriginalOwner(), getOwner());

			kPlayer.disband(this);
			GC.getGame().addReplayMessage(REPLAY_MESSAGE_CITY_DESTROYED, getOwner(), "", pkPlot->getX(), pkPlot->getY());
#if defined(MOD_BALANCE_CORE)
			GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityRazed, getOwner(), getX(), getY());
#endif
			return true;
		}
		// Counter is positive, reduce population
		else
		{
			changePopulation(-iPopulationDrop, true);
		}
#if defined(MOD_BALANCE_CORE)
		PlayerTypes eFormerOwner = getPreviousOwner();
		if(eFormerOwner == NO_PLAYER || eFormerOwner == GetID() || eFormerOwner == BARBARIAN_PLAYER)
		{
			return false;
		}
		if(GET_PLAYER(eFormerOwner).isMinorCiv() || !GET_PLAYER(eFormerOwner).isAlive())
		{
			return false;
		}
		
		int iDefaultCityValue = /*150*/ GC.getWAR_DAMAGE_LEVEL_CITY_WEIGHT();

		// Notify Diplo AI that damage has been done
		int iValue = iDefaultCityValue;

		iValue += getPopulation() * /*100*/ GC.getWAR_DAMAGE_LEVEL_INVOLVED_CITY_POP_MULTIPLIER();
		iValue /= GetRazingTurns();

		// My viewpoint
		GET_PLAYER(getOwner()).GetDiplomacyAI()->ChangeOtherPlayerWarValueLost(eFormerOwner, getOwner(), iValue);
		// Bad guy's viewpoint
		GET_PLAYER(eFormerOwner).GetDiplomacyAI()->ChangeWarValueLost(getOwner(), iValue);

		int iEra = GET_PLAYER(eFormerOwner).GetCurrentEra();
		if(iEra <= 0)
		{
			iEra = 1;
		}
		if(!GET_PLAYER(getOwner()).isMinorCiv())
		{
			GET_PLAYER(eFormerOwner).GetDiplomacyAI()->ChangeNumTimesRazed(getOwner(), (5 * iEra));
		}

		if(MOD_BALANCE_CORE_DIPLOMACY_ADVANCED && !GET_PLAYER(getOwner()).IsNoPartisans())
		{
			if(GET_PLAYER(getOwner()).GetSpawnCooldown() < 0)
			{
				GET_PLAYER(getOwner()).SetSpawnCooldown(0);
			}
			else
			{
				GET_PLAYER(getOwner()).ChangeSpawnCooldown(-1);
			}
			
			if(GET_PLAYER(getOwner()).GetSpawnCooldown() > 0)
			{
				return false;
			}

			// In hundreds
			int iNumRebels = (getPopulation() * 9); //Based on city size.
			int iExtraRoll = (GC.getGame().getCurrentEra() * 9 * getPopulation()); //Increase possible partisan spawns as game continues and cities grow.
			iNumRebels += GC.getGame().getJonRandNum(iExtraRoll, "Rebel count rand roll");
			iNumRebels /= 100;		
	
			if(iNumRebels <= 0)
			{
				return false;
			}
			if(iNumRebels > getPopulation())
			{
				iNumRebels = getPopulation();
			}
			int iStatic = iNumRebels;
			GET_PLAYER(getOwner()).SetSpawnCooldown(iNumRebels * 2);
			
			if(GET_TEAM(GET_PLAYER(eFormerOwner).getTeam()).isAtWar(getTeam()))
			{
				bool bNotification = GC.getGame().DoSpawnUnitsAroundTargetCity(eFormerOwner, this, iNumRebels, true, false, false, true);
				if(bNotification)
				{
					if(!GET_PLAYER(eFormerOwner).GetTacticalAI()->IsTemporaryZoneCity(this))
					{
						CvTemporaryZone zone;
						zone.SetX(getX());
						zone.SetY(getY());
						zone.SetTargetType(AI_TACTICAL_TARGET_CITY);
						zone.SetLastTurn(GC.getGame().getGameTurn() + GC.getAI_TACTICAL_MAP_TEMP_ZONE_TURNS());
						GET_PLAYER(eFormerOwner).GetTacticalAI()->AddTemporaryZone(zone);
					}
					CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
					if(pNotifications)
					{
						Localization::String strMessage = GetLocalizedText("TXT_KEY_NOTIFICATION_PARTISANS_NEAR_RAZING_CITY", getName());

						Localization::String strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_PARTISANS_NEAR_RAZING_CITY_S", getName());
						pNotifications->Add(NOTIFICATION_CITY_REVOLT_POSSIBLE, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
					}
					CvNotifications* pNotifications2 = GET_PLAYER(eFormerOwner).GetNotifications();
					if(pNotifications2)
					{
						Localization::String strMessage = GetLocalizedText("TXT_KEY_NOTIFICATION_FRIENDLY_PARTISANS_NEAR_RAZING_CITY", getName());

						Localization::String strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_FRIENDLY_PARTISANS_NEAR_RAZING_CITY_S", getName());
						pNotifications2->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
					}
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Unfriendly Partisans near %s. Number: %d.", getName().c_str(), iStatic);
						GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
			}
			else
			{
				bool bNotification = GC.getGame().DoSpawnUnitsAroundTargetCity(BARBARIAN_PLAYER, this, iNumRebels, false, false, false, false);
				if(bNotification)
				{
					CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
					if(pNotifications)
					{
						Localization::String strMessage = GetLocalizedText("TXT_KEY_NOTIFICATION_PARTISANS_NEAR_RAZING_CITY", getName());

						Localization::String strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_PARTISANS_NEAR_RAZING_CITY_S", getName());
						pNotifications->Add(NOTIFICATION_CITY_REVOLT_POSSIBLE, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
					}
					CvNotifications* pNotifications2 = GET_PLAYER(eFormerOwner).GetNotifications();
					if(pNotifications2)
					{
						Localization::String strMessage = GetLocalizedText("TXT_KEY_NOTIFICATION_FRIENDLY_PARTISANS_NEAR_RAZING_CITY", getName());

						Localization::String strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_FRIENDLY_PARTISANS_NEAR_RAZING_CITY_S", getName());
						pNotifications2->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
					}
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Unfriendly Partisans near %s. Number: %d.", getName().c_str(), iStatic);
						GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
			}
		}
#endif
	}

	return false;
}

/// Has this City been taken from its owner?
//	--------------------------------------------------------------------------------
bool CvCity::IsOccupied() const
{
	VALIDATE_OBJECT

	// If we're a puppet then we don't count as an occupied city
	if(IsPuppet())
		return false;

	return m_bOccupied;
}

//	--------------------------------------------------------------------------------
/// Has this City been taken from its owner?
void CvCity::SetOccupied(bool bValue)
{
	VALIDATE_OBJECT
	if(IsOccupied() != bValue)
	{
		m_bOccupied = bValue;
	}
}

//	--------------------------------------------------------------------------------
/// Has this City been turned into a puppet by someone capturing it?
bool CvCity::IsPuppet() const
{
	VALIDATE_OBJECT
	return m_bPuppet;
}

//	--------------------------------------------------------------------------------
/// Has this City been turned into a puppet by someone capturing it?
void CvCity::SetPuppet(bool bValue)
{
	VALIDATE_OBJECT
	if(IsPuppet() != bValue)
	{
		m_bPuppet = bValue;
	}
#if defined(MOD_BALANCE_CORE)
	if(bValue)
	{
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityPuppeted, getOwner(), GetID());
	}

	if(bValue && IsNoWarmongerYet())
	{
		PlayerTypes eFormerOwner = getPreviousOwner();
		if(eFormerOwner != NO_PLAYER)
		{
			CvDiplomacyAIHelpers::ApplyWarmongerPenalties(getOwner(), eFormerOwner, IsOriginalMajorCapital(), this);
			SetNoWarmonger(false);
		}
	}
#endif
}

//	--------------------------------------------------------------------------------
/// Turn this City into a puppet
void CvCity::DoCreatePuppet()
{
	VALIDATE_OBJECT

	// Turn this off - used to display info for annex/puppet/raze popup
	SetIgnoreCityForHappiness(false);

	SetPuppet(true);

	setProductionAutomated(true, true);

	int iForceWorkingPuppetRange = 2;

	CvPlot* pLoopPlot;

	// Loop through all plots near this City

	for(int iPlotLoop = 0; iPlotLoop < GetNumWorkablePlots(); iPlotLoop++)
	{
		pLoopPlot = iterateRingPlots(getX(), getY(), iPlotLoop);

		if(pLoopPlot != NULL)
		{
			// Cut off areas around the city we don't care about
			pLoopPlot = plotXYWithRangeCheck(pLoopPlot->getX(), pLoopPlot->getY(), getX(), getY(), iForceWorkingPuppetRange);

			if(pLoopPlot != NULL)
			{
				pLoopPlot->setWorkingCityOverride(this);
			}
		}
	}

	// Remove any buildings that are not applicable to puppets (but might have been earned through traits/policies)
	for (int iI = 0; iI < GC.getNumBuildingInfos(); iI++)
	{
		BuildingTypes eBuilding = (BuildingTypes) iI;
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			if (pkBuildingInfo->IsNoOccupiedUnhappiness())
			{
				GetCityBuildings()->SetNumFreeBuilding(eBuilding, 0);
			}
		}
	}
#if defined(MOD_BALANCE_CORE)
	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes) iI;
		if(eYield == NO_YIELD)
			continue;

		UpdateCityYields(eYield);
	}
#endif
	GET_PLAYER(getOwner()).CalculateNetHappiness();

	GET_PLAYER(getOwner()).DoUpdateNextPolicyCost();

	DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
	DLLUI->setDirty(GameData_DIRTY_BIT, true);
}

//	--------------------------------------------------------------------------------
/// Un-puppet a City and force it into the empire
void CvCity::DoAnnex()
{
	VALIDATE_OBJECT

	// Turn this off - used to display info for annex/puppet/raze popup
#if defined(MOD_BALANCE_CORE)
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoAnnexing())
	{
		if(!IsPuppet())
		{
			SetPuppet(true);
		}
		return;
	}
#endif
	SetIgnoreCityForHappiness(false);
#if defined(MOD_BALANCE_CORE)
	if(IsNoWarmongerYet())
	{
		PlayerTypes eFormerOwner = getPreviousOwner();
		if(eFormerOwner != NO_PLAYER)
		{
			CvDiplomacyAIHelpers::ApplyWarmongerPenalties(getOwner(), eFormerOwner, IsOriginalMajorCapital(), this);
			SetNoWarmonger(false);
		}
	}
	//Immediate Annex? Bonus for Courthouse
	if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS && !IsPuppet())
	{
		for(int iBuildingLoop = 0; iBuildingLoop < GC.getNumBuildingInfos(); iBuildingLoop++)
		{
			const BuildingTypes eBuilding = static_cast<BuildingTypes>(iBuildingLoop);
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);

			if(pkBuildingInfo && pkBuildingInfo->IsNoOccupiedUnhappiness())
			{
				const BuildingClassTypes eBuildingClass = (BuildingClassTypes)(pkBuildingInfo->GetBuildingClassType());
				if(eBuildingClass != NO_BUILDINGCLASS)
				{
					SetBuildingInvestment(eBuildingClass, true);
				}
			}
		}
		if(IsResistance())
		{
			int iResistanceTurns = GetResistanceTurns();
			iResistanceTurns /= 2;
			ChangeResistanceTurns(-iResistanceTurns);
		}
	}				
#endif
	SetPuppet(false);

	setProductionAutomated(false, true);
#if defined(MOD_BALANCE_CORE)
	for (int iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		YieldTypes eYield = (YieldTypes) iI;
		if(eYield == NO_YIELD)
			continue;

		UpdateCityYields(eYield);
	}
#endif
	GET_PLAYER(getOwner()).CalculateNetHappiness();

#if !defined(NO_ACHIEVEMENTS)
	if(getOriginalOwner() != GetID())
	{
		if(GET_PLAYER(getOriginalOwner()).isMinorCiv())
		{
			if(!GC.getGame().isGameMultiPlayer() && GET_PLAYER(getOwner()).isHuman())
			{
				bool bUsingXP1Scenario1 = gDLL->IsModActivated(CIV5_XP1_SCENARIO1_MODID);
				if(!bUsingXP1Scenario1)
				{
					gDLL->UnlockAchievement(ACHIEVEMENT_CITYSTATE_ANNEX);
				}
			}
		}
	}
#endif

	GET_PLAYER(getOwner()).DoUpdateNextPolicyCost();

	DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
	DLLUI->setDirty(GameData_DIRTY_BIT, true);
}

//	--------------------------------------------------------------------------------
int CvCity::GetLocalHappiness() const
{
	CvPlayer& kPlayer = GET_PLAYER(m_eOwner);

	int iLocalHappiness = GetBaseHappinessFromBuildings();

	int iHappinessPerGarrison = kPlayer.GetHappinessPerGarrisonedUnit();
	if(iHappinessPerGarrison > 0)
	{
		CvUnit* pDefender = plot()->getBestDefender(getOwner());
		if(pDefender && pDefender->getDomainType() == DOMAIN_LAND)
		{
			iLocalHappiness += kPlayer.GetHappinessPerGarrisonedUnit();
		}
	}
	// Follower beliefs
	int iHappinessFromReligion = 0;
	CvGameReligions* pReligions = GC.getGame().GetGameReligions();

	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(eMajority != NO_RELIGION)
	{
		BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();

		const CvReligion* pReligion = pReligions->GetReligion(eMajority, kPlayer.GetID());
		if(pReligion)
		{
			iHappinessFromReligion += pReligion->m_Beliefs.GetHappinessPerCity(getPopulation(), getOwner());
			if (eSecondaryPantheon != NO_BELIEF && getPopulation() >= GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetMinPopulation())
			{
				iHappinessFromReligion += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetHappinessPerCity();
			}
			if(plot()->isRiver())
			{
				iHappinessFromReligion += pReligion->m_Beliefs.GetRiverHappiness(getOwner());
				if (eSecondaryPantheon != NO_BELIEF)
				{
					iHappinessFromReligion += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetRiverHappiness();
				}
			}

			iHappinessFromReligion += GetBuildingClassHappinessFromReligion();
			
		}
		iLocalHappiness += iHappinessFromReligion;
	}

#if defined(MOD_BALANCE_CORE)
	int iSpecialHappiness = GetComboUnhappiness();
	iLocalHappiness += iSpecialHappiness;
#else

	// Policy Building Mods
	int iSpecialPolicyBuildingHappiness = 0;
	for(size_t jJ = 0; jJ < vBuildings.size(); jJ++)
	{
		BuildingTypes eBuilding = vBuildings[jJ];
		CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
		if (!pkBuilding)
			continue;

		int iHappinessMod = kPlayer.GetPlayerPolicies()->GetBuildingClassHappinessModifier( (BuildingClassTypes)pkBuilding->GetBuildingClassType() );
		if (iHappinessMod != 0)
			iSpecialPolicyBuildingHappiness += iHappinessMod * GetCityBuildings()->GetNumBuilding(eBuilding);
	}

	iLocalHappiness += iSpecialPolicyBuildingHappiness;
#endif
	int iLocalHappinessCap = getPopulation();

	// India has unique way to compute local happiness cap
	if(kPlayer.GetPlayerTraits()->GetCityUnhappinessModifier() != 0)
	{
		// 0.67 per population, rounded up
		iLocalHappinessCap = (iLocalHappinessCap * 20) + 15;
		iLocalHappinessCap /= 30;
	}

	if(iLocalHappinessCap < iLocalHappiness)
	{
		return iLocalHappinessCap;
	}
	else
	{
		return iLocalHappiness;
	}
}
#if defined(MOD_BALANCE_CORE_HAPPINESS)
void CvCity::UpdateComboHappiness()
{
	CvPlayer& kPlayer = GET_PLAYER(m_eOwner);
	int iTotalHappiness = 0;
	// Building Class Combo Mods
	int iSpecialBuildingHappiness = 0;

	const std::vector<BuildingTypes>& vBuildings = GetCityBuildings()->GetAllBuildingsHere();
	for(size_t jJ = 0; jJ < vBuildings.size(); jJ++)
	{
		BuildingTypes eBuildingA = vBuildings[jJ];

		if(GetCityBuildings()->GetNumBuilding(eBuildingA) <= 0)
			continue;

		CvBuildingEntry* pkBuildingA = GC.getBuildingInfo(eBuildingA);
		if (!pkBuildingA)
			continue;

		for(size_t kK = 0; kK < vBuildings.size(); kK++)
		{
			BuildingTypes eBuildingB = vBuildings[kK];

			if(GetCityBuildings()->GetNumBuilding(eBuildingB) <= 0)
				continue;

			CvBuildingEntry *pkBuildingB = GC.getBuildingInfo(eBuildingB);
			if (!pkBuildingB)
				continue;

			BuildingClassTypes eBuildingClassB = (BuildingClassTypes)pkBuildingB->GetBuildingClassType();

			int iHappinessPerBuilding = pkBuildingA->GetBuildingClassLocalHappiness(eBuildingClassB);
			if(iHappinessPerBuilding > 0)
				iSpecialBuildingHappiness += iHappinessPerBuilding;
		}
	}

	iTotalHappiness += iSpecialBuildingHappiness;

	// Policy Building Mods
	int iSpecialPolicyBuildingHappiness = 0;
	for(size_t jJ = 0; jJ < vBuildings.size(); jJ++)
	{
		BuildingTypes eBuilding = vBuildings[jJ];
		CvBuildingEntry* pkBuilding = GC.getBuildingInfo(eBuilding);
		if (!pkBuilding)
			continue;

		int iHappinessMod = kPlayer.GetPlayerPolicies()->GetBuildingClassHappinessModifier( (BuildingClassTypes)pkBuilding->GetBuildingClassType() );
		if (iHappinessMod != 0)
			iSpecialPolicyBuildingHappiness += iHappinessMod * GetCityBuildings()->GetNumBuilding(eBuilding);
	}

	UpdateBuildingClassHappinessFromReligion();

	iTotalHappiness += iSpecialPolicyBuildingHappiness;

	SetComboHappiness(iTotalHappiness);
}
void CvCity::SetComboHappiness(int iValue)
{
	VALIDATE_OBJECT
	if(m_iComboUnhappiness != iValue)
	{
		m_iComboUnhappiness = iValue;
	}
}
int CvCity::GetComboUnhappiness() const
{
	return m_iComboUnhappiness;
}
void CvCity::SetBuildingClassHappinessFromReligion(int iValue)
{
	VALIDATE_OBJECT
	if (m_iBuildingClassHappinessFromReligion != iValue)
	{
		m_iBuildingClassHappinessFromReligion = iValue;
	}
}
int CvCity::GetBuildingClassHappinessFromReligion() const
{
	return m_iBuildingClassHappinessFromReligion;
}
void CvCity::UpdateBuildingClassHappinessFromReligion()
{
	CvGameReligions* pReligions = GC.getGame().GetGameReligions();
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if (eMajority != NO_RELIGION)
	{
		int iFollowers = GetCityReligions()->GetNumFollowers(eMajority);

		int iHappiness = 0;
		const CvReligion* pReligion = pReligions->GetReligion(eMajority, getOwner());
		if (pReligion)
		{
			// Buildings
			const std::vector<BuildingTypes>& vBuildings = GetCityBuildings()->GetAllBuildingsHere();
			for (size_t jJ = 0; jJ < vBuildings.size(); jJ++)
			{
				BuildingTypes eBuilding = vBuildings[jJ];
				CvBuildingEntry *pkInfo = GC.getBuildingInfo(eBuilding);
				if (pkInfo)
				{
					BuildingClassTypes eBuildingClass = (BuildingClassTypes)pkInfo->GetBuildingClassType();
					iHappiness += (pReligion->m_Beliefs.GetBuildingClassHappiness(eBuildingClass, iFollowers, getOwner()) * GetCityBuildings()->GetNumBuilding(eBuilding));
				}
			}
		}
		SetBuildingClassHappinessFromReligion(iHappiness);
	}
}
int CvCity::getUnhappinessAggregated() const
{
	int iNegativeHappiness = 0;
	int iLimit = getPopulation();
	int iContribution;

	//order is very important
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromConnectionRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromReligionRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromScienceRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;
	iContribution = getUnhappinessFromCultureRaw(iLimit);
	iNegativeHappiness += iContribution;
	iLimit -= iContribution;

	return iNegativeHappiness;
}
int CvCity::GetNumPillagedPlots() const
{
	return m_iPillagedPlots;
}
void CvCity::SetNumPillagedPlots(int iValue)
{
	if (m_iPillagedPlots != iValue)
	{
		m_iPillagedPlots = iValue;
		if (m_iPillagedPlots <= 0)
		{
			m_iPillagedPlots = 0;
		}
	}
}
void CvCity::ChangeNumPillagedPlots(int iValue)
{
	if (iValue != 0)
	{
		m_iPillagedPlots += iValue;
		if (m_iPillagedPlots <= 0)
		{
			m_iPillagedPlots = 0;
		}
	}
}
int CvCity::GetGrowthFromTourism() const
{
	return m_iGrowthFromTourism;
}
void CvCity::SetGrowthFromTourism(int iValue)
{
	if (m_iGrowthFromTourism != iValue)
	{
		m_iGrowthFromTourism = iValue;
	}
}
void CvCity::ChangeGrowthFromTourism(int iValue)
{
	if (iValue != 0)
	{
		m_iGrowthFromTourism += iValue;
		if (m_iGrowthFromTourism <= 0)
		{
			m_iGrowthFromTourism = 0;
		}
	}
}
void CvCity::UpdateGrowthFromTourism()
{
	int iTotalGrowth = 0;
	CvGameTrade* pGameTrade = GC.getGame().GetGameTrade();
	for (uint ui = 0; ui < pGameTrade->GetNumTradeConnections(); ui++)
	{
		if (pGameTrade->IsTradeRouteIndexEmpty(ui))
		{
			continue;
		}

		if (pGameTrade->GetTradeConnection(ui).m_eConnectionType != TRADE_CONNECTION_INTERNATIONAL)
			continue;

		if (CvGameTrade::GetOriginCity(pGameTrade->GetTradeConnection(ui)) != this)
			continue;

		CvCity* pDestCity = CvGameTrade::GetDestCity(pGameTrade->GetTradeConnection(ui));

		if (pDestCity->getOwner() != NO_PLAYER && GET_PLAYER(pDestCity->getOwner()).isMajorCiv())
		{
			int iGrowthTourism = GET_PLAYER(getOwner()).GetCulture()->GetInfluenceTradeRouteGrowthBonus(pDestCity->getOwner());
			if (iGrowthTourism != 0)
			{
				iTotalGrowth += iGrowthTourism;
			}
		}
	}
	SetGrowthFromTourism(iTotalGrowth);
}
int CvCity::getHappinessDelta() const
{
	GET_PLAYER(getOwner()).CalculateNetHappiness();

	int iPositiveHappiness = GetLocalHappiness();
	int iNegativeHappiness = getUnhappinessAggregated();
	
	if(IsRazing() || IsResistance())
	{
		iNegativeHappiness += (getPopulation() / 2);
	}
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		iNegativeHappiness += int(getPopulation() * GC.getUNHAPPINESS_PER_OCCUPIED_POPULATION());
	}

	return iPositiveHappiness-iNegativeHappiness;
}

//	--------------------------------------------------------------------------------
int CvCity::getThresholdAdditions() const
{
	int iModifier = GC.getBALANCE_UNHAPPY_CITY_BASE_VALUE();

	//Let's modify this based on the number of player techs - more techs means the threshold goes higher.
	int iTech = (int)(GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100 * /*1.5*/ GC.getBALANCE_HAPPINESS_TECH_BASE_MODIFIER());
	//Dividing it by the num of techs to get a % - num of techs artificially increased to slow rate of growth
	iTech /= max(1, GC.getNumTechInfos());
	
	iModifier += iTech;

	//Increase threshold based on # of citizens. Is slight, but makes larger cities more and more difficult to maintain.
	int iPopMod = getPopulation() * GC.getBALANCE_HAPPINESS_BASE_CITY_COUNT_MULTIPLIER();
	iPopMod /= 100;

	iModifier += iPopMod;

	if(isCapital())
	{
		iModifier += GET_PLAYER(getOwner()).GetCapitalUnhappinessModCBP();
	}
	
	return iModifier;
}
//	--------------------------------------------------------------------------------
int CvCity::getThresholdSubtractions(YieldTypes eYield, int iMod) const
{
	int iModifier = 0;
	if(IsPuppet())
	{
		iModifier += GET_PLAYER(getOwner()).GetPuppetUnhappinessMod();
	}
	if(GetWeLoveTheKingDayCounter() > 0)
	{
		iModifier += GET_PLAYER(getOwner()).GetPlayerTraits()->GetGAUnhappinesNeedMod();
	}
	if(eYield == YIELD_CULTURE)
	{
		//Trait is % reduction to this value (bigger negative trait = lower threshold).
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetUnculturedHappinessChange() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPlayerTraits()->GetUnculturedHappinessChange();
		}
		//Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetUnculturedUnhappinessMod() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetUnculturedUnhappinessMod();
		}
		//Capital only -  Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetUnculturedUnhappinessModCapital() != 0)
		{
			if(isCapital())
			{
				iModifier += GET_PLAYER(getOwner()).GetUnculturedUnhappinessModCapital();
			}
		}
		//Buildings decrease this by a flat integer.
		if(GetUnculturedUnhappiness() != 0)
		{
			iModifier += GetUnculturedUnhappiness();
		}
		if(iMod != 0)
		{
			iModifier += iMod;
		}
		if(GET_PLAYER(getOwner()).GetUnculturedUnhappinessGlobal() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetUnculturedUnhappinessGlobal();
		}
	}
	else if(eYield == YIELD_GOLD)
	{
		//Trait is % reduction of this value (higher trait = lower threshold).
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetPovertyHappinessChange() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPlayerTraits()->GetPovertyHappinessChange();
		}
		//Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetPovertyUnhappinessMod() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPovertyUnhappinessMod();
		}
		if(iMod != 0)
		{
			iModifier += iMod;
		}
		//Capital only -  Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetPovertyUnhappinessModCapital() != 0)
		{
			if(isCapital())
			{
				iModifier += GET_PLAYER(getOwner()).GetPovertyUnhappinessModCapital();
			}
		}
		//Buildings decrease this by a flat integer.
		if(GetPovertyUnhappiness() != 0)
		{
			iModifier += GetPovertyUnhappiness();
		}
		if(GET_PLAYER(getOwner()).GetPovertyUnhappinessGlobal() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPovertyUnhappinessGlobal();
		}
	}
	else if(eYield == YIELD_SCIENCE)
	{
		//Trait is % reduction to this value (higher trait = lower threshold).
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetIlliteracyHappinessChange() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPlayerTraits()->GetIlliteracyHappinessChange();
		}
		//Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetIlliteracyUnhappinessMod() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetIlliteracyUnhappinessMod();
		}
		//Capital only -  Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetIlliteracyUnhappinessModCapital() != 0)
		{
			if(isCapital())
			{
				iModifier += GET_PLAYER(getOwner()).GetIlliteracyUnhappinessModCapital();
			}
		}
		//Buildings decrease this by a flat integer.
		if(GetIlliteracyUnhappiness() != 0)
		{
			iModifier += GetIlliteracyUnhappiness();
		}
		if(iMod != 0)
		{
			iModifier += iMod;
		}
		if(GET_PLAYER(getOwner()).GetIlliteracyUnhappinessGlobal() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetIlliteracyUnhappinessGlobal();
		}
	}
	else if(eYield == YIELD_PRODUCTION)
	{
		//Trait is % reduction of this value (higher trait = lower threshold).
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetDefenseHappinessChange() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetPlayerTraits()->GetDefenseHappinessChange();
		}
		//Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetDefenseUnhappinessMod() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetDefenseUnhappinessMod();
		}
		//Capital only -  Policy cuts threshold for this value (bigger negative number = lower threshold).
		if(GET_PLAYER(getOwner()).GetDefenseUnhappinessModCapital() != 0)
		{
			if(isCapital())
			{
				iModifier += GET_PLAYER(getOwner()).GetDefenseUnhappinessModCapital();
			}
		}
		//Buildings decrease this by a flat integer.
		if(GetDefenseUnhappiness() != 0)
		{
			iModifier += GetDefenseUnhappiness();
		}
		if(iMod != 0)
		{
			iModifier += iMod;
		}
		if(GET_PLAYER(getOwner()).GetDefenseUnhappinessGlobal() != 0)
		{
			iModifier += GET_PLAYER(getOwner()).GetDefenseUnhappinessGlobal();
		}
		int iDamage = getDamage() / 10;
		iModifier += iDamage;
	}
	return iModifier;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappyCitizenCount() const
{
	VALIDATE_OBJECT
	return (getPopulation() - GetLocalHappiness());
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromCultureYield() const
{
	int iCityCulture = getJONSCulturePerTurn() * 100;

	//Per Pop Yield
	if(getPopulation() != 0)
	{
		iCityCulture = (iCityCulture / getPopulation());
	}

	//This is for LUA.
	return iCityCulture;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromCultureNeeded(int iMod) const
{
	int iThreshold = GET_PLAYER(getOwner()).getGlobalAverage(YIELD_CULTURE);

	int iModifier = getThresholdAdditions();
	iModifier += getThresholdSubtractions(YIELD_CULTURE, iMod);

	iThreshold *= (iModifier + 100);
	iThreshold /= 100;

	//This is for LUA.
	return iThreshold;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromCultureRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iUnhappiness = 0;
	int iThreshold = getUnhappinessFromCultureNeeded();
	int iCityCulture = getUnhappinessFromCultureYield();

	if(iThreshold > iCityCulture)
	{
		iUnhappiness = iThreshold - iCityCulture;
		iUnhappiness /= /* 100 */ max(1, GC.getBALANCE_UNHAPPY_CITY_BASE_VALUE_BOREDOM());

		return range(iUnhappiness,1,iLimit);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromCulture() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromConnectionRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromReligionRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromScienceRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromCultureRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromScienceYield() const
{
	int iCityResearch = getYieldRateTimes100(YIELD_SCIENCE, false);

	//Per Pop Yield
	if(getPopulation() != 0)
	{
		iCityResearch = (iCityResearch / getPopulation());
	}

	//This is for LUA.
	return iCityResearch;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromScienceNeeded(int iMod) const
{
	int iThreshold = GET_PLAYER(getOwner()).getGlobalAverage(YIELD_SCIENCE);

	int iModifier = getThresholdAdditions();
	iModifier += getThresholdSubtractions(YIELD_SCIENCE, iMod);

	iThreshold *= (iModifier + 100);
	iThreshold /= 100;

	//This is for LUA.
	return iThreshold;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromScienceRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iUnhappiness = 0;
	int iThreshold = getUnhappinessFromScienceNeeded();
	int iCityResearch = getUnhappinessFromScienceYield();

	if(iThreshold > iCityResearch)
	{
		iUnhappiness = iThreshold - iCityResearch;
		iUnhappiness /= /* 100 */ max(1, GC.getBALANCE_UNHAPPY_CITY_BASE_VALUE_ILLITERACY());

		return range(iUnhappiness,1,iLimit);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromScience() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromConnectionRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromReligionRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromScienceRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromDefenseYield() const
{
	int iDefenseYield = getStrengthValue(false);

	//Per Pop Yield
	if(getPopulation() != 0)
	{
		iDefenseYield = (iDefenseYield / getPopulation());
	}
	
	return iDefenseYield;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromDefenseNeeded(int iMod) const
{
	int iThreshold = GET_PLAYER(getOwner()).getGlobalAverage(YIELD_PRODUCTION);

	int iModifier = getThresholdAdditions();
	iModifier += getThresholdSubtractions(YIELD_PRODUCTION, iMod);

	iThreshold *= (iModifier + 100);
	iThreshold /= 100;

	//This is for LUA.
	return iThreshold;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromDefenseRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iUnhappiness = 0;
	int iThreshold = getUnhappinessFromDefenseNeeded();
	int iBuildingDefense = getUnhappinessFromDefenseYield();

	if(iThreshold > iBuildingDefense)
	{
		iUnhappiness = iThreshold - iBuildingDefense;
		iUnhappiness /= /* 100 */ max(1, GC.getBALANCE_UNHAPPY_CITY_BASE_VALUE_DISORDER());

		return range(iUnhappiness,1,iLimit);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromDefense() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromGoldYield() const
{

	int iGold = getYieldRateTimes100(YIELD_GOLD, false);

	//Per Pop Yield
	if(getPopulation() != 0)
	{
		iGold = (iGold / getPopulation());
	}

	//This is for LUA.
	return iGold;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromGoldNeeded(int iMod) const
{
	int iThreshold = GET_PLAYER(getOwner()).getGlobalAverage(YIELD_GOLD);

	int iModifier = getThresholdAdditions();
	iModifier += getThresholdSubtractions(YIELD_GOLD, iMod);

	iThreshold *= (iModifier + 100);
	iThreshold /= 100;

	return iThreshold;
}
//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromGoldRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iUnhappiness = 0;
	int iThreshold = getUnhappinessFromGoldNeeded();
	int iGold = getUnhappinessFromGoldYield();

	if(iThreshold > iGold)
	{
		iUnhappiness = iThreshold - iGold;
		iUnhappiness /= /* 100 */ max(1, GC.getBALANCE_UNHAPPY_CITY_BASE_VALUE_POVERTY());

		return range(iUnhappiness,1,iLimit);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromGold() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromConnectionRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	if(isCapital() && !IsBlockadedWaterAndLand())
	{
		return 0;
	}
	if(GET_PLAYER(getOwner()).getCapitalCity() == NULL)
	{
		return 0;
	}
	if(GET_PLAYER(getOwner()).getCapitalCity() == this)
	{
		return 0;
	}
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
	//Trait takes away unhappiness from lack of connection.
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoConnectionUnhappiness())
	{
		return 0;
	}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	//Policy takes away unhappiness from lack of connection.
	if(GET_PLAYER(getOwner()).IsNoUnhappyIsolation())
	{
		return 0;
	}
#endif

	//note: it's only connected to the capital if it's not blockaded
	if(IsRouteToCapitalConnected())
	{
		return 0;
	}

	//if we have a traderoute to our capital or to another city which has a route to the capital, it's just as good as a route
	int iLoop = 0;
	for(CvCity* pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
	{
		if(!pLoopCity)
			continue;

		if(pLoopCity->isCapital() || pLoopCity->IsRouteToCapitalConnected())
		{
			if(HasTradeRouteTo(pLoopCity) || HasTradeRouteFrom(pLoopCity))
			{
				return 0;
			}
		}
	}

	//ok, how much unhappiness do we generate?
	float fUnhappiness = 0.0f;
	if(GC.getBALANCE_UNHAPPINESS_FROM_UNCONNECTED_PER_POP() > 0) /*0.25f*/
	{
		int iRealCityPop = getPopulation();
		fUnhappiness += (float) iRealCityPop * /*0.25f*/ GC.getBALANCE_UNHAPPINESS_FROM_UNCONNECTED_PER_POP();
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		if(IsPuppet())
		{
			fUnhappiness += (float) ((GET_PLAYER(getOwner()).GetPuppetUnhappinessMod() * fUnhappiness) / 100);
		}
#endif
	}

	return range((int)fUnhappiness,1,iLimit);
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromConnection() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromConnectionRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromPillagedRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iPillaged = GetNumPillagedPlots();

	float fUnhappiness = 0.0f;
	if(iPillaged > 0)
	{
		float fExponent = /*0.25f*/ GC.getBALANCE_UNHAPPINESS_PER_PILLAGED();

		fUnhappiness += (float) iPillaged * (float) fExponent;
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		if(IsPuppet())
		{
			fUnhappiness += (float) ((GET_PLAYER(getOwner()).GetPuppetUnhappinessMod() * fUnhappiness) / 100);
		}
#endif
	}

	return range((int)fUnhappiness,0,iLimit);
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromPillaged() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromStarvingRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

	int iDiff = foodDifference();
	if(iDiff < 0 && !isFoodProduction())
	{
		iDiff = (iDiff * -1);

		float fUnhappiness = 0.0f;
		float fExponent = /*.25*/ GC.getBALANCE_UNHAPPINESS_FROM_STARVING_PER_POP();
		fUnhappiness += (float) iDiff * fExponent;

#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
		if(IsPuppet())
		{
			fUnhappiness += (float) ((GET_PLAYER(getOwner()).GetPuppetUnhappinessMod() * fUnhappiness) / 100);
		}
#endif
		return range((int)fUnhappiness,0,iLimit);
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromStarving() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);

	return iContribution;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromReligionRaw(int iLimit) const
{
	if(IsOccupied() && !IsNoOccupiedUnhappiness())
	{
		return 0;
	}
	if(IsResistance())
	{
		return 0;
	}
	if (iLimit<1)
	{
		return 0;
	}

#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
	//Trait takes away unhappiness from religious strife.
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoReligiousStrife())
	{
		return 0;
	}
#endif
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	const CvReligion* pReligion = NULL;
	float fUnhappiness = 0.0f;
	
	float fExponent = /*.50*/ GC.getBALANCE_UNHAPPINESS_PER_MINORITY_POP();
	int iCityPop = getPopulation();

	if(eMajority != NO_RELIGION && eMajority != RELIGION_PANTHEON)
	{
		pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
	}
	if(pReligion != NULL)
	{
		int iFollowers = GetCityReligions()->GetNumFollowers(eMajority);

		int iMinority = iCityPop - iFollowers;

		if(iMinority > 0)
		{
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
			//Buildings increase this by a flat integer.
			if(GetMinorityUnhappiness() != 0)
			{
				iMinority += ((GetMinorityUnhappiness() * iFollowers) / 100);
			}
			//Mod from global unhappiness building modifier.
			if(GET_PLAYER(getOwner()).GetMinorityUnhappinessGlobal() != 0)
			{
				iMinority += ((GET_PLAYER(getOwner()).GetMinorityUnhappinessGlobal() * iFollowers) / 100);
			}
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
			//Trait adds % boost to this value (higher trait = smaller minority population).
			if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetMinorityHappinessChange() != 0)
			{
				iMinority -= ((GET_PLAYER(getOwner()).GetPlayerTraits()->GetMinorityHappinessChange() * iMinority) / 100);
			}
			//Policy cuts threshold for this value (bigger negative number = lower threshold).
			if(GET_PLAYER(getOwner()).GetMinorityUnhappinessMod() != 0)
			{
				iMinority += ((GET_PLAYER(getOwner()).GetMinorityUnhappinessMod() * iMinority) / 100);
			}
			//Capital only -  Policy cuts threshold for this value (bigger negative number = lower threshold).
			if(GET_PLAYER(getOwner()).GetMinorityUnhappinessModCapital() != 0)
			{
				if(isCapital())
				{
					iMinority += ((GET_PLAYER(getOwner()).GetMinorityUnhappinessModCapital() * iMinority) / 100);
				}
			}
#endif
			fUnhappiness = (float) iMinority * fExponent;
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
			if(IsPuppet())
			{
				fUnhappiness += (float) ((GET_PLAYER(getOwner()).GetPuppetUnhappinessMod() * fUnhappiness) / 100);
			}
#endif
			int iEra = GET_PLAYER(getOwner()).GetCurrentEra() * 25;
			if(iEra > 0)
			{
				fUnhappiness -= (float)((iMinority + (100 + iEra)) / 100);
			}

			return range((int)fUnhappiness,0,iLimit);
		}
	}

	return 0;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnhappinessFromReligion() const
{
	//according to the hierarchy of needs
	int iLimit = getPopulation();
	int iContribution;
	iContribution = getUnhappinessFromStarvingRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromPillagedRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromGoldRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromDefenseRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromConnectionRaw(iLimit);
	iLimit -= iContribution;
	iContribution = getUnhappinessFromReligionRaw(iLimit);

	return iContribution;
}

#endif

//	--------------------------------------------------------------------------------
int CvCity::GetHappinessFromBuildings() const
{
	return GetUnmoddedHappinessFromBuildings();
}

//	--------------------------------------------------------------------------------
int CvCity::GetBaseHappinessFromBuildings() const
{
	return m_iBaseHappinessFromBuildings;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeBaseHappinessFromBuildings(int iChange)
{
	m_iBaseHappinessFromBuildings += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetUnmoddedHappinessFromBuildings() const
{
	return m_iUnmoddedHappinessFromBuildings;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeUnmoddedHappinessFromBuildings(int iChange)
{
	m_iUnmoddedHappinessFromBuildings += iChange;
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::GetLocalUnhappinessMod() const
{
	return m_iLocalUnhappinessMod;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeLocalUnhappinessMod(int iChange)
{
	m_iLocalUnhappinessMod += iChange;
}
#endif
//	--------------------------------------------------------------------------------
/// Used when gathering info for "Annex/Puppet/Raze" popup
bool CvCity::IsIgnoreCityForHappiness() const
{
	return m_bIgnoreCityForHappiness;
}

//	--------------------------------------------------------------------------------
/// Used when gathering info for "Annex/Puppet/Raze" popup
void CvCity::SetIgnoreCityForHappiness(bool bValue)
{
	m_bIgnoreCityForHappiness = bValue;
}

//	--------------------------------------------------------------------------------
/// Find the non-wonder building that provides the highest culture at the least cost
BuildingTypes CvCity::ChooseFreeCultureBuilding() const
{
	BuildingTypes eRtnValue = NO_BUILDING;
	int iNumBuildingInfos = GC.getNumBuildingInfos();
	CvWeightedVector<int, SAFE_ESTIMATE_NUM_BUILDINGS, true> buildingChoices;

	for(int iI = 0; iI < iNumBuildingInfos; iI++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			const CvBuildingClassInfo& kBuildingClassInfo = pkBuildingInfo->GetBuildingClassInfo();
			if(!isWorldWonderClass(kBuildingClassInfo) && !isNationalWonderClass(kBuildingClassInfo))
			{
				int iCulture = pkBuildingInfo->GetYieldChange(YIELD_CULTURE);
				int iCost = pkBuildingInfo->GetProductionCost();
				if(getFirstBuildingOrder(eBuilding) != -1 || canConstruct(eBuilding))
				{
					if(iCulture > 0 && iCost > 0)
					{
						int iWeight = iCulture * 10000 / iCost;

						if(iWeight > 0)
						{
							buildingChoices.push_back(iI, iWeight);
						}
					}
				}
			}
		}
	}

	if(buildingChoices.size() > 0)
	{
		buildingChoices.SortItems();
		eRtnValue = (BuildingTypes)buildingChoices.GetElement(0);
	}

	return eRtnValue;
}

//	--------------------------------------------------------------------------------
/// Find the non-wonder building that provides the highest culture at the least cost
BuildingTypes CvCity::ChooseFreeFoodBuilding() const
{
	BuildingTypes eRtnValue = NO_BUILDING;
	int iNumBuildingInfos = GC.getNumBuildingInfos();
	CvWeightedVector<int, SAFE_ESTIMATE_NUM_BUILDINGS, true> buildingChoices;

	for(int iI = 0; iI < iNumBuildingInfos; iI++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(pkBuildingInfo)
		{
			const CvBuildingClassInfo& kBuildingClassInfo = pkBuildingInfo->GetBuildingClassInfo();
			if(!isWorldWonderClass(kBuildingClassInfo) && !isNationalWonderClass(kBuildingClassInfo))
			{
#if defined(MOD_BUGFIX_FREE_FOOD_BUILDING)
				if(!MOD_BUGFIX_FREE_FOOD_BUILDING || (getFirstBuildingOrder(eBuilding) != -1 || canConstruct(eBuilding)))
				{
#endif
					int iFood = pkBuildingInfo->GetFoodKept();
					int iCost = pkBuildingInfo->GetProductionCost();
					if(iFood > 0 && iCost > 0)
					{
						int iWeight = iFood * 10000 / iCost;

						if(iWeight > 0)
						{
							buildingChoices.push_back(iI, iWeight);
						}
					}
#if defined(MOD_BUGFIX_FREE_FOOD_BUILDING)
				}
#endif
			}
		}
	}

	if(buildingChoices.size() > 0)
	{
		buildingChoices.SortItems();
		eRtnValue = (BuildingTypes)buildingChoices.GetElement(0);
	}

	return eRtnValue;
}

//	--------------------------------------------------------------------------------
int CvCity::getCitySizeBoost() const
{
	VALIDATE_OBJECT
	return m_iCitySizeBoost;
}


//	--------------------------------------------------------------------------------
void CvCity::setCitySizeBoost(int iBoost)
{
	VALIDATE_OBJECT
	if(getCitySizeBoost() != iBoost)
	{
		m_iCitySizeBoost = iBoost;

		setLayoutDirty(true);
	}
}


//	--------------------------------------------------------------------------------
bool CvCity::isNeverLost() const
{
	VALIDATE_OBJECT
	return m_bNeverLost;
}


//	--------------------------------------------------------------------------------
void CvCity::setNeverLost(bool bNewValue)
{
	VALIDATE_OBJECT
	m_bNeverLost = bNewValue;
}


//	--------------------------------------------------------------------------------
bool CvCity::isDrafted() const
{
	VALIDATE_OBJECT
	return m_bDrafted;
}


//	--------------------------------------------------------------------------------
void CvCity::setDrafted(bool bNewValue)
{
	VALIDATE_OBJECT
	m_bDrafted = bNewValue;
}

//	--------------------------------------------------------------------------------
bool CvCity::IsOwedCultureBuilding() const
{
	return m_bOwedCultureBuilding;
}

//	--------------------------------------------------------------------------------
void CvCity::SetOwedCultureBuilding(bool bNewValue)
{
	m_bOwedCultureBuilding = bNewValue;
}

#if defined(MOD_BUGFIX_FREE_FOOD_BUILDING)
//	--------------------------------------------------------------------------------
bool CvCity::IsOwedFoodBuilding() const
{
	return m_bOwedFoodBuilding;
}

//	--------------------------------------------------------------------------------
void CvCity::SetOwedFoodBuilding(bool bNewValue)
{
	m_bOwedFoodBuilding = bNewValue;
}
#endif
#if defined(MOD_BALANCE_CORE)

//	--------------------------------------------------------------------------------
int CvCity::GetBorderObstacleCity() const
{
	VALIDATE_OBJECT
	return m_iBorderObstacleCity;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeBorderObstacleCity(int iChange)
{
	VALIDATE_OBJECT
	SetBorderObstacleCity(GetBorderObstacleCity() + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetBorderObstacleCity(int iValue)
{
	VALIDATE_OBJECT
	m_iBorderObstacleCity = iValue;
}

//	--------------------------------------------------------------------------------
int CvCity::GetBorderObstacleWater() const
{
	VALIDATE_OBJECT
	return m_iBorderObstacleWater;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeBorderObstacleWater(int iChange)
{
	VALIDATE_OBJECT
	SetBorderObstacleWater(GetBorderObstacleWater() + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetBorderObstacleWater(int iValue)
{
	VALIDATE_OBJECT
	m_iBorderObstacleWater = iValue;
}


//	--------------------------------------------------------------------------------
int CvCity::GetNearbyMountains() const
{
	VALIDATE_OBJECT
	return m_iNumNearbyMountains;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeNearbyMountains(int iChange)
{
	VALIDATE_OBJECT
	SetNearbyMountains(GetNearbyMountains() + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetNearbyMountains(int iValue)
{
	VALIDATE_OBJECT
	m_iNumNearbyMountains = iValue;
}

#endif
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
bool CvCity::IsOwedChosenBuilding(BuildingClassTypes eBuildingClass) const
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	return m_abOwedChosenBuilding[eBuildingClass];
}
//	--------------------------------------------------------------------------------
void CvCity::SetOwedChosenBuilding(BuildingClassTypes eBuildingClass, bool bNewValue)
{
	FAssert(eBuildingClass >= 0);
	FAssert(eBuildingClass < GC.getNumBuildingClassInfos());

	m_abOwedChosenBuilding.setAt(eBuildingClass, bNewValue);
}
#endif

//	--------------------------------------------------------------------------------
bool CvCity::IsBlockadedWaterAndLand() const
{
	return IsBlockaded(true) && IsBlockaded(false);
}

bool CvCity::IsBlockaded(bool bWater) const
{
	for (int iLoop = 0; iLoop < NUM_DIRECTION_TYPES; ++iLoop) 
	{
		CvPlot* pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iLoop));
		if (pAdjacentPlot && 
			pAdjacentPlot->isWater()==bWater && 
			!pAdjacentPlot->isImpassable(getTeam()) && 
			!pAdjacentPlot->isBlockaded(getOwner())) 
		{
			return false;
		}
	}
	
	//note: if a city is landlocked, it is permanently blockaded from sea side by definition
	return true;
}

//	--------------------------------------------------------------------------------
/// Amount of turns left in WLTKD
int CvCity::GetWeLoveTheKingDayCounter() const
{
	VALIDATE_OBJECT
	return m_iWeLoveTheKingDayCounter;
}

//	--------------------------------------------------------------------------------
///Sets number of turns left in WLTKD
void CvCity::SetWeLoveTheKingDayCounter(int iValue)
{
	VALIDATE_OBJECT
	m_iWeLoveTheKingDayCounter = iValue;
}

//	--------------------------------------------------------------------------------
///Changes number of turns left in WLTKD
void CvCity::ChangeWeLoveTheKingDayCounter(int iChange)
{
	VALIDATE_OBJECT
	SetWeLoveTheKingDayCounter(GetWeLoveTheKingDayCounter() + iChange);
}

//	--------------------------------------------------------------------------------
/// Turn number when AI placed a garrison here
int CvCity::GetLastTurnGarrisonAssigned() const
{
	VALIDATE_OBJECT
	return m_iLastTurnGarrisonAssigned;
}

//	--------------------------------------------------------------------------------
int CvCity::GetNumThingsProduced() const
{
	VALIDATE_OBJECT
	return m_iThingsProduced;
}

//	--------------------------------------------------------------------------------
bool CvCity::isProductionAutomated() const
{
	VALIDATE_OBJECT
	return m_bProductionAutomated;
}


//	--------------------------------------------------------------------------------
void CvCity::setProductionAutomated(bool bNewValue, bool bClear)
{
	VALIDATE_OBJECT
	if(isProductionAutomated() != bNewValue)
	{
		m_bProductionAutomated = bNewValue;

		if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
		{
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
		}

		// if automated and not network game and all 3 modifiers down, clear the queue and choose again
		if(bNewValue && bClear)
		{
			clearOrderQueue();
		}
		if(!isProduction())
		{
			AI_chooseProduction(false /*bInterruptWonders*/, false);
		}
	}
}


//	--------------------------------------------------------------------------------
bool CvCity::isLayoutDirty() const
{
	VALIDATE_OBJECT
	return m_bLayoutDirty;
}


//	--------------------------------------------------------------------------------
void CvCity::setLayoutDirty(bool bNewValue)
{
	VALIDATE_OBJECT
	m_bLayoutDirty = bNewValue;
}

//	--------------------------------------------------------------------------------
PlayerTypes CvCity::getPreviousOwner() const
{
	VALIDATE_OBJECT
	return m_ePreviousOwner;
}


//	--------------------------------------------------------------------------------
void CvCity::setPreviousOwner(PlayerTypes eNewValue)
{
	VALIDATE_OBJECT
	m_ePreviousOwner = eNewValue;
}


//	--------------------------------------------------------------------------------
PlayerTypes CvCity::getOriginalOwner() const
{
	VALIDATE_OBJECT
	return m_eOriginalOwner;
}


//	--------------------------------------------------------------------------------
void CvCity::setOriginalOwner(PlayerTypes eNewValue)
{
	VALIDATE_OBJECT
	m_eOriginalOwner = eNewValue;
}


//	--------------------------------------------------------------------------------
PlayerTypes CvCity::GetPlayersReligion() const
{
	VALIDATE_OBJECT
	return m_ePlayersReligion;
}


//	--------------------------------------------------------------------------------
void CvCity::SetPlayersReligion(PlayerTypes eNewValue)
{
	VALIDATE_OBJECT
	m_ePlayersReligion = eNewValue;
}
#if defined(MOD_BALANCE_CORE)
void CvCity::SetNoWarmonger(bool bValue)
{
	VALIDATE_OBJECT
	m_bNoWarmonger = bValue;
}
bool CvCity::IsNoWarmongerYet()
{
	VALIDATE_OBJECT
	return m_bNoWarmonger;
}

void CvCity::SetNumTimesOwned(PlayerTypes ePlayer, int iValue)
{
	CvAssertMsg(ePlayer >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(ePlayer < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	m_aiNumTimesOwned.setAt(ePlayer, iValue);
}
void CvCity::ChangeNumTimesOwned(PlayerTypes ePlayer, int iValue)
{
	CvAssertMsg(ePlayer >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(ePlayer < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	SetNumTimesOwned(ePlayer, (GetNumTimesOwned(ePlayer) + iValue));
}
int CvCity::GetNumTimesOwned(PlayerTypes ePlayer)
{
	VALIDATE_OBJECT
	return m_aiNumTimesOwned[ePlayer];
}
#endif
//	--------------------------------------------------------------------------------
TeamTypes CvCity::getTeam() const
{
	VALIDATE_OBJECT
	return GET_PLAYER(getOwner()).getTeam();
}


//	--------------------------------------------------------------------------------
int CvCity::getSeaPlotYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiSeaPlotYield[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeSeaPlotYield(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiSeaPlotYield.setAt(eIndex, m_aiSeaPlotYield[eIndex] + iChange);
		CvAssert(getSeaPlotYield(eIndex) >= 0);

		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getRiverPlotYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiRiverPlotYield[eIndex];
}

//	--------------------------------------------------------------------------------
void CvCity::changeRiverPlotYield(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiRiverPlotYield.setAt(eIndex, m_aiRiverPlotYield[eIndex] + iChange);
		CvAssert(getRiverPlotYield(eIndex) >= 0);

		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getLakePlotYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiLakePlotYield[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeLakePlotYield(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiLakePlotYield.setAt(eIndex, m_aiLakePlotYield[eIndex] + iChange);
		CvAssert(getLakePlotYield(eIndex) >= 0);

		updateYield();
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getSeaResourceYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiSeaResourceYield[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeSeaResourceYield(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiSeaResourceYield.setAt(eIndex, m_aiSeaResourceYield[eIndex] + iChange);
		CvAssert(getSeaResourceYield(eIndex) >= 0);

		updateYield();
	}
}

#if defined(MOD_API_UNIFIED_YIELDS)
//	--------------------------------------------------------------------------------
/// Yield per turn from Religion
int CvCity::GetYieldPerTurnFromReligion(YieldTypes eYield) const
{
	int iYieldPerTurn = 0;
#if !defined(MOD_BALANCE_CORE)
	const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligion, NO_PLAYER);

	// Only do this for food and production, other yields from religion are handled at the player level

	if (pReligion && (eYield == YIELD_FOOD || eYield == YIELD_PRODUCTION))
	{
		if (GetCityReligions()->IsHolyCityForReligion(eReligion))
		{
			iYieldPerTurn += pReligion->m_Beliefs.GetHolyCityYieldChange(eYield);
		}
	}
#endif
	iYieldPerTurn += GetSpecialReligionYields(eYield);

	return iYieldPerTurn;
}
void CvCity::UpdateSpecialReligionYields(YieldTypes eYield)
{
	int iYieldValue = 0;
	ReligionTypes eReligion = GetCityReligions()->GetReligiousMajority();
	if(eReligion != NO_RELIGION)
	{
		CvPlayer &kPlayer = GET_PLAYER(getOwner());
		CvGameReligions* pReligions = GC.getGame().GetGameReligions();
		const CvReligion* pReligion = pReligions->GetReligion(eReligion, getOwner());
		if(pReligion)
		{
			iYieldValue += pReligion->m_Beliefs.GetHolyCityYieldChange(eYield, getOwner(), this);
			int iPantheon = 0;
			int iYield = pReligion->m_Beliefs.GetYieldFromKnownPantheons(eYield, getOwner(), this);
			if(iYield > 0)
			{
				iPantheon = GC.getGame().GetGameReligions()->GetNumPantheonsCreated();
				if(iPantheon > 0)
				{
					if(iPantheon > 8)
					{
						iPantheon = 8;
					}
					iYieldValue += (iPantheon * iYield);
				}
			}

			int iTemp = pReligion->m_Beliefs.GetYieldChangePerForeignCity(eYield, getOwner(), this);
			if (iTemp > 0)
			{
				iYieldValue += (iTemp * kPlayer.GetReligions()->GetNumForeignCitiesFollowing(eReligion));
			}

			iTemp = pReligion->m_Beliefs.GetYieldChangePerXForeignFollowers(eYield, getOwner(), this);
			if (iTemp > 0)
			{
				int iFollowers = kPlayer.GetReligions()->GetNumForeignFollowers(false, eReligion);
				if (iFollowers > 0)
				{
					iYieldValue += (iFollowers / iTemp);
				}
			}

			// This came from CvTreasury::GetGoldPerTurnFromReligion()
			//Only useable in religions!
			if (eYield == YIELD_GOLD)
			{
				int iGoldPerFollowingCity = pReligion->m_Beliefs.GetGoldPerFollowingCity(getOwner(), this);
				iYieldValue += (pReligions->GetNumCitiesFollowing(eReligion) * iGoldPerFollowingCity);

				int iGoldPerXFollowers = pReligion->m_Beliefs.GetGoldPerXFollowers(getOwner(), this);
				if(iGoldPerXFollowers > 0)
				{
					iYieldValue += (pReligions->GetNumFollowers(eReligion) / iGoldPerXFollowers);
				}
			}

			int iYieldPerFollowingCity = pReligion->m_Beliefs.GetYieldPerFollowingCity(eYield);
			if(iYieldPerFollowingCity > 0)
			{
				iYieldValue += iYieldPerFollowingCity;
			}

			//Pantheon safe (only checks for local followers)
			int iYieldPerXFollowers = pReligion->m_Beliefs.GetYieldPerXFollowers(eYield, getOwner(), this);
			if(iYieldPerXFollowers > 0)
			{
				iYieldValue += (pReligions->GetNumFollowers(eReligion, getOwner()) / iYieldPerXFollowers);
			}

			int iLuxCulture = pReligion->m_Beliefs.GetYieldPerLux(eYield, getOwner(), this);
			if(iLuxCulture > 0)
			{		
				int iNumHappinessResources = 0;
				ResourceTypes eResource;
				for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
				{
					eResource = (ResourceTypes) iResourceLoop;

					if(kPlayer.GetHappinessFromLuxury(eResource) > 0)
					{
						iNumHappinessResources++;
					}
				}
				if(iNumHappinessResources > 0)
				{
					iLuxCulture *= iNumHappinessResources;
					iYieldValue += iLuxCulture;
				}
			}
			int iYieldPerGPT = pReligion->m_Beliefs.GetYieldPerGPT(eYield, getOwner(), this);
			if(iYieldPerGPT > 0)
			{
				int iNetGold = kPlayer.GetTreasury()->CalculateGrossGold();
				if (iNetGold > 0)
				{
					iYieldValue += (iNetGold / iYieldPerGPT);
				}
			}
			int iYieldPerScience = pReligion->m_Beliefs.GetYieldPerScience(eYield, getOwner(), this);
			if(iYieldPerScience > 0)
			{
				if(kPlayer.GetScience() > 0)
				{
					iYieldValue += (kPlayer.GetScience() / iYieldPerScience);
				}
			}
			CvLeague* pLeague = GC.getGame().GetGameLeagues()->GetActiveLeague();
			if(pLeague != NULL)
			{
				int iEra = kPlayer.GetCurrentEra();
				if(iEra <= 1)
				{
					iEra = 1;
				}
				int iHostYield = (pReligion->m_Beliefs.GetYieldFromHost(eYield, getOwner(), this) * iEra);
				if(iHostYield > 0)
				{
					if(pLeague->GetHostMember() == getOwner())
					{
						iYieldValue += iHostYield;
					}
				}
			}
		}
	}
	SetSpecialReligionYields(eYield, iYieldValue);
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetSpecialReligionYields(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromKnownPantheons[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::SetSpecialReligionYields(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromKnownPantheons.setAt(eIndex, iChange);
		CvAssert(GetSpecialReligionYields(eIndex) >= 0);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getBaseYieldRateModifier(YieldTypes eIndex, int iExtra, CvString* toolTipSink) const
{
	VALIDATE_OBJECT
	int iModifier = 0;
	int iTempMod;

	// Yield Rate Modifier
	iTempMod = getYieldRateModifier(eIndex);
	iModifier += iTempMod;
	if(toolTipSink)
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD", iTempMod);

	CvPlot* pCityPlot = plot();
	for(int iUnitLoop = 0; iUnitLoop < pCityPlot->getNumUnits(); iUnitLoop++)
	{
		for(int iI = 0; iI < GC.getNumPromotionInfos(); iI++)
		{
			const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iI);
			CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
			if(pkPromotionInfo != NULL)
			{
				if(pkPromotionInfo->GetYieldModifier(eIndex) > 0)
				{
					if(pCityPlot->getUnitByIndex(iUnitLoop)->isHasPromotion(eLoopPromotion))
					{
						iTempMod = pkPromotionInfo->GetYieldModifier(eIndex);
						iModifier += iTempMod;
						if(toolTipSink && iTempMod)
						{
							GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_UNITPROMOTION", iTempMod);
						}
					}
				}
			}
		}
	}

	// Resource Yield Rate Modifier
	iTempMod = getResourceYieldRateModifier(eIndex);
	iModifier += iTempMod;
	if(toolTipSink)
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_RESOURCES", iTempMod);

	// Happiness Yield Rate Modifier
	iTempMod = getHappinessModifier(eIndex);
	iModifier += iTempMod;
	if(toolTipSink)
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_HAPPINESS", iTempMod);

	// Area Yield Rate Modifier
	CvArea* pArea = GC.getMap().getArea( getArea() );
	if(pArea != NULL)
	{
		iTempMod = pArea->getYieldRateModifier(getOwner(), eIndex);
		iModifier += iTempMod;
		if(toolTipSink)
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_AREA", iTempMod);
	}

	// Player Yield Rate Modifier
	iTempMod = GET_PLAYER(getOwner()).getYieldRateModifier(eIndex);
	iModifier += iTempMod;
	if(toolTipSink)
		GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_PLAYER", iTempMod);

	// Player Capital Yield Rate Modifier
	if(isCapital())
	{
		iTempMod = GET_PLAYER(getOwner()).getCapitalYieldRateModifier(eIndex);
		iModifier += iTempMod;
		if(toolTipSink)
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_CAPITAL", iTempMod);
	}
#if defined(MOD_BALANCE_CORE)
	iTempMod = (GetTradeRouteCityMod(eIndex));
	if(iTempMod > 0)
	{
		iModifier += iTempMod;
		if(toolTipSink)
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_CORPORATION", iTempMod);
	}
#endif

	// Golden Age Yield Modifier
	if(GET_PLAYER(getOwner()).isGoldenAge() && eIndex != YIELD_FOOD)
	{ 
		CvYieldInfo* pYield = GC.getYieldInfo(eIndex);
		if(pYield)
		{
			iTempMod = pYield->getGoldenAgeYieldMod();
			iModifier += iTempMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_GOLDEN_AGE", iTempMod);
		}
#if defined(MOD_BALANCE_CORE)
		if(GetGoldenAgeYieldMod(eIndex) != 0)
		{
			iTempMod = GetGoldenAgeYieldMod(eIndex);
			iModifier += iTempMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_GOLDEN_AGE_BUILDINGS", iTempMod);
		}

		if(GET_PLAYER(getOwner()).getGoldenAgeYieldMod(eIndex) != 0)
		{
			iTempMod = GET_PLAYER(getOwner()).getGoldenAgeYieldMod(eIndex);
			iModifier += iTempMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_GOLDEN_AGE_POLICIES", iTempMod);
		}
#endif
	}

	// Religion Yield Rate Modifier
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
	if(pReligion)
	{
		int iReligionYieldMaxFollowers = pReligion->m_Beliefs.GetMaxYieldModifierPerFollower(eIndex, getOwner());
		if (iReligionYieldMaxFollowers > 0)
		{
			int iFollowers = GetCityReligions()->GetNumFollowers(eMajority);
#if defined(MOD_BALANCE_CORE_BELIEFS)
			if(pReligion->m_Beliefs.IsHalvedFollowers(getOwner()))
			{
				iFollowers /= 2;
			}
#endif
			iTempMod = min(iFollowers, iReligionYieldMaxFollowers);
			iModifier += iTempMod;
			if(toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_BELIEF", iTempMod);
		}
	}

	// Production Yield Rate Modifier from City States
	if(eIndex == YIELD_PRODUCTION && GetCityBuildings()->GetCityStateTradeRouteProductionModifier() > 0 )
	{	
		iTempMod = GetCityBuildings()->GetCityStateTradeRouteProductionModifier();
		iModifier += iTempMod;
		if(toolTipSink){
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_HANSE", iTempMod);
		}
	}
#if defined(MOD_BALANCE_CORE_RESOURCE_MONOPOLIES)
	if(MOD_BALANCE_CORE_RESOURCE_MONOPOLIES && (eIndex != YIELD_FOOD))
	{
		// Do we get increased yields from a resource monopoly?
		int iTempMod = GET_PLAYER(getOwner()).getCityYieldModFromMonopoly(eIndex);
		if (iTempMod != 0)
		{
			iTempMod += GET_PLAYER(getOwner()).GetMonopolyModPercent();
			iModifier += iTempMod;
			if(toolTipSink)
			{
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_YIELD_MONOPOLY_RESOURCE", iTempMod);
			}
		}
	}
#endif
#if defined(MOD_BALANCE_CORE_BELIEFS)
	ReligionTypes eReligionFounded = GC.getGame().GetGameReligions()->GetFounderBenefitsReligion(getOwner());
	if(eReligionFounded == NO_RELIGION)
	{
		eReligionFounded = GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities();
	}
	if(MOD_BALANCE_CORE_BELIEFS && eReligionFounded != NO_RELIGION)
	{
		const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligionFounded, getOwner());
		if(pReligion)
		{
			ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
			if(GetCityReligions()->IsHolyCityForReligion(pReligion->m_eReligion))
			{
				int iGoldenAge = pReligion->m_Beliefs.GetYieldBonusGoldenAge(eIndex, getOwner());
				if(iGoldenAge > 0)
				{
					if(GET_PLAYER(getOwner()).getGoldenAgeTurns() > 0)
					{
						iTempMod = iGoldenAge;
						iModifier += iTempMod;
						if(toolTipSink){
							GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODUCTION_GOLDEN_AGE", iTempMod);
						}
					}
				}
			}
			if(eMajority == eReligionFounded)
			{
				int iWLTKD = pReligion->m_Beliefs.GetYieldFromWLTKD(eIndex, getOwner());
				if(iWLTKD != 0)
				{
					if(GetWeLoveTheKingDayCounter() > 0)
					{
						iTempMod = iWLTKD;
						iModifier += iTempMod;
						if(toolTipSink){
							GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODUCTION_WLTKD_BELIEF", iTempMod);
						}
					}
				}
			}
		}
	}
	if(GetWeLoveTheKingDayCounter() > 0)
	{
		if (GET_PLAYER(getOwner()).GetYieldFromWLTKD(eIndex) + GetYieldFromWLTKD(eIndex) != 0)
		{
			iTempMod = (GetYieldFromWLTKD(eIndex) + GET_PLAYER(getOwner()).GetYieldFromWLTKD(eIndex));
			iModifier += iTempMod;
			if(toolTipSink){
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODUCTION_WLTKD_BUILDING", iTempMod);
			}
		}
		if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetWLTKDCulture() != 0 && eIndex == YIELD_CULTURE)
		{
			iTempMod = GET_PLAYER(getOwner()).GetPlayerTraits()->GetWLTKDCulture();
			iModifier += iTempMod;
			if(toolTipSink){
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODUCTION_WLTKD_TRAIT", iTempMod);
			}
		}
	}
#endif

	// Puppet
	if(IsPuppet())
	{
		switch(eIndex)
		{
		case YIELD_SCIENCE:
			iTempMod = GC.getPUPPET_SCIENCE_MODIFIER();
			iModifier += iTempMod;
			if(iTempMod != 0 && toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_PUPPET", iTempMod);
#if defined(MOD_BUGFIX_MINOR)
			break;
#endif
		case YIELD_GOLD:
			iTempMod = GC.getPUPPET_GOLD_MODIFIER();
			iModifier += iTempMod;
			if(iTempMod != 0 && toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_PUPPET", iTempMod);
#if defined(MOD_BUGFIX_MINOR)
			break;
#endif
#if defined(MOD_API_UNIFIED_YIELDS_TOURISM)
		case YIELD_TOURISM:
			iTempMod = GC.getPUPPET_TOURISM_MODIFIER();
			iModifier += iTempMod;
			if(iTempMod != 0 && toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_PUPPET", iTempMod);
			break;
#endif
#if defined(MOD_API_UNIFIED_YIELDS_GOLDEN_AGE)
		case YIELD_GOLDEN_AGE_POINTS:
			iTempMod = GC.getPUPPET_GOLDEN_AGE_MODIFIER();
			iModifier += iTempMod;
			if(iTempMod != 0 && toolTipSink)
				GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_PUPPET", iTempMod);
			break;
#endif
		}
	}

	iModifier += iExtra;

#if defined(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
	if(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
	{
		//Mechanic to allow for production malus from happiness/unhappiness.
		int iHappiness = GET_PLAYER(getOwner()).GetExcessHappiness();

		//If Happiness is greater than or over threshold, calculate city bonus mod.
		if(iHappiness >= GC.getBALANCE_HAPPINESS_THRESHOLD())
		{
			iHappiness = (iHappiness - GC.getBALANCE_HAPPINESS_THRESHOLD());
			//Are there minimums/maximums for the bonus? Restrict this value.
			if(iHappiness > GC.getBALANCE_HAPPINESS_BONUS_MAXIMUM())
			{
				iHappiness = GC.getBALANCE_HAPPINESS_BONUS_MAXIMUM();
			}
			else if(iHappiness < GC.getBALANCE_HAPPINESS_BONUS_MINIMUM())
			{
				iHappiness = GC.getBALANCE_HAPPINESS_BONUS_MINIMUM();
			}
		}
		//If happiness is less than the main threshold, calculate city penalty mod.
		else if(iHappiness < GC.getBALANCE_HAPPINESS_THRESHOLD_MAIN())
		{
			//Are there minimums/maximums for the penalty? Restrict this value.
			if(iHappiness > GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM())
			{
				iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MINIMUM();
			}
			else if(iHappiness < GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM())
			{
				iHappiness = GC.getBALANCE_HAPPINESS_PENALTY_MAXIMUM();
			}
			
		}
		else
		{
			iHappiness = 0;
		}
		//Let's do the yield mods.			
		if(eIndex == YIELD_PRODUCTION  && (iHappiness != 0))
		{
			iTempMod = (GC.getBALANCE_HAPPINESS_PRODUCTION_MODIFIER() * iHappiness);
			iModifier += iTempMod;
			if(iTempMod != 0 && toolTipSink)
			GC.getGame().BuildProdModHelpText(toolTipSink, "TXT_KEY_PRODMOD_BALANCE_HAPPINESS_MOD", iTempMod);
		}
	}
#endif

	// note: player->invalidateYieldRankCache() must be called for anything that is checked here
	// so if any extra checked things are added here, the cache needs to be invalidated

	return std::max(0, (iModifier + 100));
}

//	--------------------------------------------------------------------------------
int CvCity::getHappinessModifier(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	int iModifier = 0;
	CvPlayer &kPlayer = GET_PLAYER(getOwner());

#if defined(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
	if(MOD_BALANCE_CORE_HAPPINESS_NATIONAL)
	{
		return iModifier;
	}
#endif
	if (kPlayer.IsEmpireUnhappy())
	{
		int iUnhappy = -1 * kPlayer.GetExcessHappiness();

		// Production and Gold slow down when Empire is Unhappy
		if(eIndex == YIELD_PRODUCTION)
		{
			iModifier = iUnhappy * GC.getVERY_UNHAPPY_PRODUCTION_PENALTY_PER_UNHAPPY();
			iModifier = max (iModifier, GC.getVERY_UNHAPPY_MAX_PRODUCTION_PENALTY());
		}
		else if (eIndex == YIELD_GOLD)
		{
			iModifier = iUnhappy * GC.getVERY_UNHAPPY_GOLD_PENALTY_PER_UNHAPPY();
			iModifier = max (iModifier, GC.getVERY_UNHAPPY_MAX_GOLD_PENALTY());
		}
	}

	return iModifier;
}
//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
int CvCity::getYieldRate(YieldTypes eIndex, bool bIgnoreTrade, bool bStatic) const
#else
int CvCity::getYieldRate(YieldTypes eIndex, bool bIgnoreTrade) const
#endif
{
	VALIDATE_OBJECT
#if defined(MOD_BALANCE_CORE)
	if(bStatic && !bIgnoreTrade)
	{
		return (GetStaticYield(eIndex) / 100);
	}
#endif
	return (getYieldRateTimes100(eIndex, bIgnoreTrade) / 100);
}
//	--------------------------------------------------------------------------------
#if defined(MOD_BALANCE_CORE)
int CvCity::getYieldRateTimes100(YieldTypes eIndex, bool bIgnoreTrade, bool bStatic) const
#else
int CvCity::getYieldRateTimes100(YieldTypes eIndex, bool bIgnoreTrade) const
#endif
{
	VALIDATE_OBJECT
#if defined(MOD_BALANCE_CORE)
	if(bStatic && !bIgnoreTrade)
	{
		return GetStaticYield(eIndex);
	}
#endif

	// Resistance - no Science, Gold or Production (Prod handled in ProductionDifference)
	if(IsResistance() || IsRazing())
	{
		if(eIndex == YIELD_GOLD || eIndex == YIELD_SCIENCE)
		{
			return 0;
		}

#if defined(MOD_API_UNIFIED_YIELDS_TOURISM)
		if(eIndex == YIELD_TOURISM)
		{
			return 0;
		}
#endif

#if defined(MOD_API_UNIFIED_YIELDS_GOLDEN_AGE)
		if(eIndex == YIELD_GOLDEN_AGE_POINTS)
		{
			return 0;
		}
#endif
#if defined(MOD_BALANCE_CORE)
		if(eIndex == YIELD_GREAT_GENERAL_POINTS)
		{
			return 0;
		}
		if(eIndex == YIELD_GREAT_ADMIRAL_POINTS)
		{
			return 0;
		}
		if(eIndex == YIELD_POPULATION)
		{
			return 0;
		}
		if(eIndex == YIELD_CULTURE_LOCAL)
		{
			return 0;
		}
#endif
	}

	int iProcessYield = 0;

	if(getProductionToYieldModifier(eIndex) != 0)
	{
#if defined(MOD_PROCESS_STOCKPILE)
		// We want to process production to production and call it stockpiling!
		iProcessYield = getBasicYieldRateTimes100(YIELD_PRODUCTION, false) * getProductionToYieldModifier(eIndex) / 100;
#else
		CvAssertMsg(eIndex != YIELD_PRODUCTION, "GAMEPLAY: should not be trying to convert Production into Production via process.");

		iProcessYield = getYieldRateTimes100(YIELD_PRODUCTION, false) * getProductionToYieldModifier(eIndex) / 100;
#endif
	}

#if defined(MOD_PROCESS_STOCKPILE)
	int iYield = getBasicYieldRateTimes100(eIndex, bIgnoreTrade) + iProcessYield;

	return iYield;
}

int CvCity::getBasicYieldRateTimes100(YieldTypes eIndex, bool bIgnoreTrade) const
{
#endif
	// Sum up yield rate
	int iBaseYield = getBaseYieldRate(eIndex) * 100;
	iBaseYield += (GetYieldPerPopTimes100(eIndex) * getPopulation());
	iBaseYield += (GetYieldPerReligionTimes100(eIndex) * GetCityReligions()->GetNumReligionsWithFollowers());

	int iModifiedYield = iBaseYield * getBaseYieldRateModifier(eIndex);
	iModifiedYield /= 100;

#if !defined(MOD_PROCESS_STOCKPILE)
	iModifiedYield += iProcessYield;
#endif

	if (!bIgnoreTrade)
	{
		int iTradeYield = GET_PLAYER(m_eOwner).GetTrade()->GetTradeValuesAtCityTimes100(this, eIndex);
		iModifiedYield += iTradeYield;
	}

	return iModifiedYield;
}
#if defined(MOD_BALANCE_CORE)
void CvCity::UpdateCityScienceFromYield(YieldTypes eIndex, int iModifiedYield)
{
	if(iModifiedYield > 0)
	{
		if(GetBuildingScienceFromYield(eIndex) > 0)
		{
			int iBonusYield = (iModifiedYield * GetBuildingScienceFromYield(eIndex) / 100);
			if(iBonusYield > 0)
			{
				SetScienceFromYield(eIndex, iBonusYield);
			}
			else if(GetScienceFromYield(eIndex) > 0)
			{
				SetScienceFromYield(eIndex, 0);
			}
		}
	}
	else if(GetScienceFromYield(eIndex) > 0)
	{
		SetScienceFromYield(eIndex, 0);
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getBaseYieldRate(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	int iValue = 0;
#if defined(MOD_GLOBAL_GREATWORK_YIELDTYPES) || defined(MOD_API_UNIFIED_YIELDS)
	iValue += GetBaseYieldRateFromGreatWorks(eIndex);
#endif
	iValue += GetBaseYieldRateFromTerrain(eIndex);
#if defined(MOD_API_UNIFIED_YIELDS)
	for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
	{
		FeatureTypes eFeature = (FeatureTypes) iI;
		if(eFeature != NO_FEATURE)
		{
			iValue += GetYieldPerTurnFromUnimprovedFeatures(eFeature, eIndex);
		}
	}
#endif
	iValue += GetBaseYieldRateFromBuildings(eIndex);
	iValue += GetBaseYieldRateFromSpecialists(eIndex);
	iValue += GetBaseYieldRateFromMisc(eIndex);
	iValue += GetBaseYieldRateFromReligion(eIndex);
#if defined(MOD_BALANCE_CORE)
	iValue += GetBaseYieldRateFromCSAlliance(eIndex);
	iValue += GetBaseYieldRateFromCSFriendship(eIndex);
	iValue += GetYieldPerTurnFromTraits(eIndex);
	iValue += GetYieldChangeFromCorporationFranchises(eIndex);
#endif
#if defined(MOD_BALANCE_CORE)
	iValue += GetEventCityYield(eIndex);
#endif

#if defined(MOD_API_UNIFIED_YIELDS)
	if (IsRouteToCapitalConnected())
	{
		iValue += GET_PLAYER(getOwner()).GetYieldChangeTradeRoute(eIndex);
		iValue += GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldChangeTradeRoute(eIndex);
	}
#endif

#if defined(MOD_DIPLOMACY_CITYSTATES)
	if(GET_PLAYER(getOwner()).IsLeagueArt() && eIndex == YIELD_SCIENCE)
	{
		iValue += GetBaseScienceFromArt();
	}
#endif
#if defined(MOD_BALANCE_CORE_POLICIES)
	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(eMajority != NO_RELIGION && eMajority > RELIGION_PANTHEON)
	{
		if(GET_PLAYER(getOwner()).GetReligions()->GetReligionInMostCities() == eMajority)
		{	
			iValue += GET_PLAYER(getOwner()).getReligionYieldRateModifier(eIndex);
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	//Update Science from yields
	if(eIndex != YIELD_SCIENCE)
	{
		CvCity* pCity = this->plot()->getPlotCity();
		if(pCity)
		{
			pCity->UpdateCityScienceFromYield(eIndex, iValue);
		}
	}
	else if(eIndex == YIELD_SCIENCE)
	{
		for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			YieldTypes eIndex = (YieldTypes)iI;
			if(eIndex == NO_YIELD)
			{
				continue;
			}
			iValue += GetScienceFromYield(eIndex);
		}
	}
#endif

	return iValue;
}

#if defined(MOD_DIPLOMACY_CITYSTATES)
/// Where is our Science coming from?
int CvCity::GetBaseScienceFromArt() const
{
	int iScience = GetBaseYieldRateFromLeague(YIELD_SCIENCE);

	return iScience;
}	
#endif

#if defined(MOD_GLOBAL_GREATWORK_YIELDTYPES) || defined(MOD_API_UNIFIED_YIELDS)
//	--------------------------------------------------------------------------------
/// Base yield rate from Great Works
int CvCity::GetBaseYieldRateFromGreatWorks(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return GetCityBuildings()->GetYieldFromGreatWorks(eIndex);
}
#endif

//	--------------------------------------------------------------------------------
/// Base yield rate from Terrain
int CvCity::GetBaseYieldRateFromTerrain(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiBaseYieldRateFromTerrain[eIndex];
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Terrain
void CvCity::ChangeBaseYieldRateFromTerrain(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromTerrain.setAt(eIndex, m_aiBaseYieldRateFromTerrain[eIndex] + iChange);

		// JAR - debugging
		s_lastYieldUsedToUpdateRateFromTerrain = eIndex;
		s_changeYieldFromTerreain = iChange;


		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}
		}
	}
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Buildings
int CvCity::GetBaseYieldRateFromBuildings(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiBaseYieldRateFromBuildings[eIndex];
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Buildings
void CvCity::ChangeBaseYieldRateFromBuildings(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromBuildings.setAt(eIndex, m_aiBaseYieldRateFromBuildings[eIndex] + iChange);

		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
				//DLLUI->setDirty(InfoPane_DIRTY_BIT, true );
			}
		}
	}
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Specialists
int CvCity::GetBaseYieldRateFromSpecialists(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiBaseYieldRateFromSpecialists[eIndex];
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Specialists
void CvCity::ChangeBaseYieldRateFromSpecialists(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromSpecialists.setAt(eIndex, m_aiBaseYieldRateFromSpecialists[eIndex] + iChange);

		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}
		}
	}
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Misc
int CvCity::GetBaseYieldRateFromMisc(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiBaseYieldRateFromMisc[eIndex];
}

//	--------------------------------------------------------------------------------
/// Base yield rate from Misc
void CvCity::ChangeBaseYieldRateFromMisc(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromMisc.setAt(eIndex, m_aiBaseYieldRateFromMisc[eIndex] + iChange);

		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}
		}
	}
}
#if defined(MOD_DIPLOMACY_CITYSTATES)
// Base yield rate from League
int CvCity::GetBaseYieldRateFromLeague(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiBaseYieldRateFromLeague[eIndex];
}

//	--------------------------------------------------------------------------------
/// Base yield rate from League
void CvCity::ChangeBaseYieldRateFromLeague(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromLeague.setAt(eIndex, m_aiBaseYieldRateFromLeague[eIndex] + iChange);

		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}
		}
	}
}
//SCIENCY AID - Used for negation if cancelled
void CvCity::ChangeTotalScienceyAid(int iChange)
{
	SetTotalScienceyAid(GetTotalScienceyAid() + iChange);
}

int CvCity::GetTotalScienceyAid() const
{
	return m_iTotalScienceyAid;
}

void CvCity::SetTotalScienceyAid(int iValue)
{
	if(GetTotalScienceyAid() != iValue)
		m_iTotalScienceyAid = iValue;
}

//ARTSY AID TOTALS  - Used for negation if cancelled
void CvCity::ChangeTotalArtsyAid(int iChange)
{
	SetTotalArtsyAid(GetTotalArtsyAid() + iChange);
}

int CvCity::GetTotalArtsyAid() const
{
	return m_iTotalArtsyAid;
}

void CvCity::SetTotalArtsyAid(int iValue)
{
	if(GetTotalArtsyAid() != iValue)
		m_iTotalArtsyAid = iValue;
}

//GREAT WORK AID TOTALS  - Used for negation if cancelled
void CvCity::ChangeTotalGreatWorkAid(int iChange)
{
	SetTotalGreatWorkAid(GetTotalGreatWorkAid() + iChange);
}

int CvCity::GetTotalGreatWorkAid() const
{
	return m_iTotalGreatWorkAid;
}

void CvCity::SetTotalGreatWorkAid(int iValue)
{
	if(GetTotalGreatWorkAid() != iValue)
		m_iTotalGreatWorkAid = iValue;
}
#endif
#if defined(MOD_DIPLOMACY_CITYSTATES) || defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetGrowthExtraYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiChangeGrowthExtraYield[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeGrowthExtraYield(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiChangeGrowthExtraYield.setAt(eIndex, m_aiChangeGrowthExtraYield[eIndex] + iChange);
		CvAssert(GetGrowthExtraYield(eIndex) >= 0);
	}
}
#endif
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromVictory(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiChangeYieldFromVictory[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromVictory(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiChangeYieldFromVictory.setAt(eIndex, m_aiChangeYieldFromVictory[eIndex] + iChange);
		CvAssert(GetYieldFromVictory(eIndex) >= 0);
	}
}


//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetGoldenAgeYieldMod(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiGoldenAgeYieldMod[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeGoldenAgeYieldMod(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiGoldenAgeYieldMod.setAt(eIndex, m_aiGoldenAgeYieldMod[eIndex] + iChange);
		CvAssert(GetGoldenAgeYieldMod(eIndex) >= 0);
	}
}


//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromWLTKD(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromWLTKD[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromWLTKD(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromWLTKD.setAt(eIndex, m_aiYieldFromWLTKD[eIndex] + iChange);
		CvAssert(GetYieldFromWLTKD(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromConstruction(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromConstruction[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromConstruction(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromConstruction.setAt(eIndex, m_aiYieldFromConstruction[eIndex] + iChange);
		CvAssert(GetYieldFromConstruction(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromTech(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromTech[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromTech(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromTech.setAt(eIndex, m_aiYieldFromTech[eIndex] + iChange);
		CvAssert(GetYieldFromTech(eIndex) >= 0);
	}
}

	//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromBirth(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromBirth[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromBirth(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromBirth.setAt(eIndex, m_aiYieldFromBirth[eIndex] + iChange);
		CvAssert(GetYieldFromBirth(eIndex) >= 0);
	}
}
	//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromUnitProduction(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromUnitProduction[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromUnitProduction(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromUnitProduction.setAt(eIndex, m_aiYieldFromUnitProduction[eIndex] + iChange);
		CvAssert(GetYieldFromUnitProduction(eIndex) >= 0);
	}
}

	//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromBorderGrowth(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromBorderGrowth[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromBorderGrowth(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromBorderGrowth.setAt(eIndex, m_aiYieldFromBorderGrowth[eIndex] + iChange);
		CvAssert(GetYieldFromBorderGrowth(eIndex) >= 0);
	}
}
	//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromPolicyUnlock(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromPolicyUnlock[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromPolicyUnlock(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromPolicyUnlock.setAt(eIndex, m_aiYieldFromPolicyUnlock[eIndex] + iChange);
		CvAssert(GetYieldFromPolicyUnlock(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromPurchase(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromPurchase[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromPurchase(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromPurchase.setAt(eIndex, m_aiYieldFromPurchase[eIndex] + iChange);
		CvAssert(GetYieldFromPurchase(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldFromUnitLevelUp(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldFromUnitLevelUp[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldFromUnitLevelUp(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldFromUnitLevelUp.setAt(eIndex, m_aiYieldFromUnitLevelUp[eIndex] + iChange);
		CvAssert(GetYieldFromUnitLevelUp(eIndex) >= 0);
	}
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldPerAlly(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldPerAlly[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldPerAlly(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if (iChange != 0)
	{
		m_aiYieldPerAlly.setAt(eIndex, m_aiYieldPerAlly[eIndex] + iChange);
		CvAssert(GetYieldPerAlly(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetYieldPerFriend(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldPerFriend[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeYieldPerFriend(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if (iChange != 0)
	{
		m_aiYieldPerFriend.setAt(eIndex, m_aiYieldPerFriend[eIndex] + iChange);
		CvAssert(GetYieldPerFriend(eIndex) >= 0);
	}
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetScienceFromYield(YieldTypes eIndex1) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex1 >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex1 < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiScienceFromYield[eIndex1];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::SetScienceFromYield(YieldTypes eIndex1, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex1 >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex1 < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	m_aiScienceFromYield.setAt(eIndex1, iChange);
	CvAssert(GetScienceFromYield(eIndex1) >= 0);
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetBuildingScienceFromYield(YieldTypes eIndex1) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex1 >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex1 < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiBuildingScienceFromYield[eIndex1];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeBuildingScienceFromYield(YieldTypes eIndex1, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex1 >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex1 < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	if(iChange != 0)
	{
		m_aiBuildingScienceFromYield.setAt(eIndex1, m_aiBuildingScienceFromYield[eIndex1] + iChange);
		CvAssert(GetBuildingScienceFromYield(eIndex1) >= 0);
	}
}

/// Extra yield from building
int CvCity::GetSpecialistRateModifier(SpecialistTypes eSpecialist) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eSpecialist >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eSpecialist < GC.getNumSpecialistInfos(), "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiSpecialistRateModifier[eSpecialist];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeSpecialistRateModifier(SpecialistTypes eSpecialist, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eSpecialist >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eSpecialist < GC.getNumSpecialistInfos(), "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiSpecialistRateModifier.setAt(eSpecialist, m_aiSpecialistRateModifier[eSpecialist] + iChange);
		CvAssert(GetSpecialistRateModifier(eIndex) >= 0);
	}
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetThemingYieldBonus(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiThemingYieldBonus[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeThemingYieldBonus(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiThemingYieldBonus.setAt(eIndex, m_aiThemingYieldBonus[eIndex] + iChange);
		CvAssert(GetThemingYieldBonus(eIndex) >= 0);
	}
}
#endif
#if defined(MOD_BALANCE_CORE_HAPPINESS_MODIFIERS)
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetPovertyUnhappiness() const
{
	return m_iChangePovertyUnhappiness;
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangePovertyUnhappiness(int iChange)
{
	m_iChangePovertyUnhappiness += iChange;
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetDefenseUnhappiness() const
{
	return m_iChangeDefenseUnhappiness;
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeDefenseUnhappiness(int iChange)
{
	m_iChangeDefenseUnhappiness += iChange;
}
/// Extra yield from building
int CvCity::GetUnculturedUnhappiness() const
{
	return m_iChangeUnculturedUnhappiness;
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeUnculturedUnhappiness(int iChange)
{
	m_iChangeUnculturedUnhappiness += iChange;
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetIlliteracyUnhappiness() const
{
	return m_iChangeIlliteracyUnhappiness;
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeIlliteracyUnhappiness(int iChange)
{
	m_iChangeIlliteracyUnhappiness += iChange;
}
//	--------------------------------------------------------------------------------
/// Extra yield from building
int CvCity::GetMinorityUnhappiness() const
{
	return m_iChangeMinorityUnhappiness;
}

//	--------------------------------------------------------------------------------
/// Extra yield from building
void CvCity::ChangeMinorityUnhappiness(int iChange)
{
	m_iChangeMinorityUnhappiness += iChange;
}
#endif
//	--------------------------------------------------------------------------------
/// Base yield rate from Religion
int CvCity::GetBaseYieldRateFromReligion(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

#if defined(MOD_API_UNIFIED_YIELDS)
	int iBaseYield = m_aiBaseYieldRateFromReligion[eIndex];
	for (int iI = 0; iI < GC.getNumTerrainInfos(); iI++)
	{
		TerrainTypes eTerrain = (TerrainTypes) iI;
		if(eTerrain != NO_TERRAIN)
		{
			iBaseYield += GetYieldPerXTerrainFromReligion(eTerrain, eIndex);
		}
	}
	for (int iI = 0; iI < GC.getNumFeatureInfos(); iI++)
	{
		FeatureTypes eFeature = (FeatureTypes) iI;
		if(eFeature != NO_FEATURE)
		{
			iBaseYield += GetYieldPerXFeature(eFeature, eIndex);
		}
	}
	if(GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromOwnPantheon(eIndex) > 0)
	{
		ReligionTypes eReligionFounded = GET_PLAYER(getOwner()).GetReligions()->GetReligionCreatedByPlayer(true);
		if(MOD_BALANCE_CORE_BELIEFS && eReligionFounded != NO_RELIGION && eReligionFounded >= RELIGION_PANTHEON)
		{
			if(GetCityReligions()->GetReligiousMajority() == eReligionFounded)
			{
				iBaseYield += GET_PLAYER(getOwner()).GetPlayerTraits()->GetYieldFromOwnPantheon(eIndex);
			}
		}
	}
	// This will only return a value for food and production
	iBaseYield += GetYieldPerTurnFromReligion(eIndex);
		
	return iBaseYield;
#else
	return m_aiBaseYieldRateFromReligion[eIndex];
#endif
}
//	--------------------------------------------------------------------------------
/// Base yield rate from Religion
void CvCity::ChangeBaseYieldRateFromReligion(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromReligion.setAt(eIndex, m_aiBaseYieldRateFromReligion[eIndex] + iChange);

		if(getTeam() == GC.getGame().getActiveTeam())
		{
			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}
		}
	}
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
/// Base yield rate from CS Alliances
int CvCity::GetBaseYieldRateFromCSAlliance(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	int iValue = 0;
	if (GetYieldPerAlly(eIndex) != 0)
	{
		iValue += (GetYieldPerAlly(eIndex) * GET_PLAYER(getOwner()).GetNumCSAllies());
	}
	return (m_aiBaseYieldRateFromCSAlliance[eIndex] + iValue);
}
//	--------------------------------------------------------------------------------
/// Base yield rate from CS Alliances
void CvCity::ChangeBaseYieldRateFromCSAlliance(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromCSAlliance.setAt(eIndex, m_aiBaseYieldRateFromCSAlliance[eIndex] + iChange);
		CvAssert(GetBaseYieldRateFromCSAlliance(eIndex) >= 0); 
	}
}
void CvCity::SetBaseYieldRateFromCSAlliance(YieldTypes eIndex, int iValue)
{
	if(GetBaseYieldRateFromCSAlliance(eIndex) != iValue)
	{
		m_aiBaseYieldRateFromCSAlliance.setAt(eIndex, iValue);
	}
}
//	--------------------------------------------------------------------------------
/// Base yield rate from CS Friendships
int CvCity::GetBaseYieldRateFromCSFriendship(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	int iValue = 0;
	if (GetYieldPerFriend(eIndex) != 0)
	{
		iValue += (GetYieldPerFriend(eIndex) * GET_PLAYER(getOwner()).GetNumCSFriends());
	}
	return (m_aiBaseYieldRateFromCSFriendship[eIndex] + iValue);
}
void CvCity::ChangeBaseYieldRateFromCSFriendship(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiBaseYieldRateFromCSFriendship.setAt(eIndex, m_aiBaseYieldRateFromCSFriendship[eIndex] + iChange);
		CvAssert(GetBaseYieldRateFromCSFriendship(eIndex) >= 0); 
	}
}
void CvCity::SetBaseYieldRateFromCSFriendship(YieldTypes eIndex, int iValue)
{
	if(GetBaseYieldRateFromCSFriendship(eIndex) != iValue)
	{
		m_aiBaseYieldRateFromCSFriendship.setAt(eIndex, iValue);
	}
}
//CORPORATIONS
//	--------------------------------------------------------------------------------
// Get the yield modifier change from having a Corporation
int CvCity::GetTradeRouteCityMod(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
	if (eCorporation == NO_CORPORATION)
		return 0;

	CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
	if (pkCorporationInfo == NULL)
		return 0;

	// If our Corporation does not have a city yield mod, remove
	if (pkCorporationInfo->GetTradeRouteCityMod(eIndex) == 0)
		return 0;

	int iMod = 0;
	CvGameTrade* pGameTrade = GC.getGame().GetGameTrade();
	for (uint ui = 0; ui < pGameTrade->GetNumTradeConnections(); ui++)
	{
		if (pGameTrade->IsTradeRouteIndexEmpty(ui))
		{
			continue;
		}

		if(pGameTrade->GetOriginCity(pGameTrade->GetTradeConnection(ui)) != this)
			continue;

		CvCity* pOriginCity = CvGameTrade::GetOriginCity(pGameTrade->GetTradeConnection(ui));
		CvCity* pDestCity = CvGameTrade::GetDestCity(pGameTrade->GetTradeConnection(ui));
		if (pOriginCity != NULL && pDestCity != NULL)
		{
			if (pOriginCity->IsHasOffice() && pDestCity->IsHasFranchise(eCorporation))
			{
				iMod += pkCorporationInfo->GetTradeRouteCityMod(eIndex);
			}
		}
	}
	return iMod;
}

int CvCity::GetGPRateModifierPerXFranchises() const
{
	CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
	if (eCorporation == NO_CORPORATION)
		return false;

	CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
	if (pkCorporationInfo == NULL || !pkCorporationInfo)
		return 0;

	// Calculate what our input into the corporation helper we need
	int iCurrentValue = 0;

	BuildingTypes eOffice = (BuildingTypes)GET_PLAYER(getOwner()).getCivilizationInfo().getCivilizationBuildings(pkCorporationInfo->GetOfficeBuildingClass());
	if(eOffice == NO_BUILDING)
		return 0;

	CvBuildingEntry* pkOfficeInfo = GC.getBuildingInfo(eOffice);
	if (pkOfficeInfo == NULL)
		return 0;

	int iNumFranchises = GET_PLAYER(getOwner()).GetCorporations()->GetNumFranchises();

	if (iNumFranchises > 0)
	{
		// Civilized Jewelers
		if (pkOfficeInfo->GetGPRateModifierPerXFranchises() > 0)
		{
			iCurrentValue = iNumFranchises * pkOfficeInfo->GetGPRateModifierPerXFranchises();
		}
	}
	return iCurrentValue;
}

bool CvCity::IsHeadquarters() const
{
	VALIDATE_OBJECT

	CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
	if (eCorporation == NO_CORPORATION)
		return false;

	CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
	if (pkCorporationInfo == NULL)
		return false;

	BuildingClassTypes eHeadquarters = pkCorporationInfo->GetHeadquartersBuildingClass();
	if (eHeadquarters == NO_BUILDINGCLASS)
		return false;

	return HasBuildingClass(eHeadquarters);
}

bool CvCity::IsHasOffice() const
{
	VALIDATE_OBJECT

	CorporationTypes eCorporation = GET_PLAYER(getOwner()).GetCorporations()->GetFoundedCorporation();
	if (eCorporation == NO_CORPORATION)
		return false;

	CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
	if (pkCorporationInfo == NULL)
		return false;

	BuildingClassTypes eOffice = pkCorporationInfo->GetOfficeBuildingClass();
	if (eOffice == NO_BUILDINGCLASS)
		return false;

	const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
	BuildingTypes eBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(eOffice)));

	if(eBuilding == NO_BUILDING)
		return false;

	return (m_pCityBuildings->GetNumBuilding(eBuilding) > 0);
}

bool CvCity::IsHasFranchise(CorporationTypes eCorporation) const
{
	VALIDATE_OBJECT

	if (eCorporation == NO_CORPORATION)
		return false;

	CvCorporationEntry* pkCorporationInfo = GC.getCorporationInfo(eCorporation);
	if (pkCorporationInfo == NULL)
		return false;

	// Evaluate offices as franchises if we count Offices as Franchises
	bool bOfficesAsFranchises = GetPlayer()->GetCorporations()->IsCorporationOfficesAsFranchises();
	BuildingClassTypes eFranchise = bOfficesAsFranchises ? pkCorporationInfo->GetOfficeBuildingClass() : pkCorporationInfo->GetFranchiseBuildingClass();
	if (eFranchise == NO_BUILDINGCLASS)
		return false;

	// If nationalized, then the City owner and the corporation owner must be equal
	if (bOfficesAsFranchises)
	{
		CvCorporation* pCorporation = GC.getGame().GetGameCorporations()->GetCorporation(eCorporation);
		if (pCorporation)
		{
			if (getOwner() != pCorporation->m_eFounder)
				return false;
		}
	}

	const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
	BuildingTypes eBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(eFranchise)));

	if(eBuilding == NO_BUILDING)
		return false;

	return m_pCityBuildings->GetNumBuilding(eBuilding) > 0;
}

// Returns the yield change for this building based on the number of franchises
int CvCity::GetBuildingYieldChangeFromCorporationFranchises(BuildingClassTypes eBuildingClass, YieldTypes eIndex) const
{
	VALIDATE_OBJECT

	CvAssertMsg(eBuildingClass >= 0, "eBuildingClass expected to be greater or equal to 0");
	CvAssertMsg(eBuildingClass < GC.getNumBuildingClassInfos(), "eBuildingClass expected to be < GC.getNumBuildingClassInfos()");

	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	CvPlayer& kPlayer = GET_PLAYER(getOwner());

	CvCorporationEntry* pkCorporationInfo = kPlayer.GetCorporations()->GetCorporationEntry();
	if (!pkCorporationInfo)
		return 0;

	BuildingTypes eBuilding = (BuildingTypes)kPlayer.getCivilizationInfo().getCivilizationBuildings(eBuildingClass);
	CvBuildingEntry* pBuildingInfo = GC.getBuildingInfo(eBuilding);
	if (pBuildingInfo == NULL)
		return 0;

	int iYieldPerFranchise = pBuildingInfo->GetYieldPerFranchise(eIndex);
	int iFranchises = kPlayer.GetCorporations()->GetNumFranchises();

	return iYieldPerFranchise * iFranchises;
}
void CvCity::SetYieldChangeFromCorporationFranchises(YieldTypes eIndex, int iTotal)
{
	if(GetYieldChangeFromCorporationFranchises(eIndex) != iTotal)
	{
		m_aiYieldChangeFromCorporationFranchises.setAt(eIndex, iTotal);
	}
}
int CvCity::GetYieldChangeFromCorporationFranchises(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eResource < GC.getNumResourceInfos(), "eIndex expected to be < GC.getNumResourceInfos()");

	return m_aiYieldChangeFromCorporationFranchises[eIndex];
}
void CvCity::UpdateYieldFromCorporationFranchises(YieldTypes eIndex)
{
	int iTotal = 0;
	const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
	for(int iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
	{
		CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
		if(!pkBuildingClassInfo)
		{
			continue;
		}
		BuildingTypes eLoopBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

		if(eLoopBuilding != NO_BUILDING && m_pCityBuildings->GetNumBuilding(eLoopBuilding) > 0)
		{
			iTotal += GetBuildingYieldChangeFromCorporationFranchises((BuildingClassTypes)iI, eIndex);
		}
	}

	SetYieldChangeFromCorporationFranchises(eIndex, iTotal);
}

//	--------------------------------------------------------------------------------
int CvCity::GetResourceQuantityPerXFranchises(ResourceTypes eResource) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eResource >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eResource < GC.getNumResourceInfos(), "eIndex expected to be < GC.getNumResourceInfos()");
	return m_aiResourceQuantityPerXFranchises[eResource];
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeResourceQuantityPerXFranchises(ResourceTypes eResource, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumResourceInfos(), "eIndex expected to be < GC.getNumResourceInfos()");

	if(iChange != 0)
	{
		m_aiResourceQuantityPerXFranchises.setAt(eResource, m_aiResourceQuantityPerXFranchises[eResource] + iChange);
		CvAssert(GetCorporationResourceQuantity(eResource) >= 0); 
	}
}
void CvCity::SetResourceQuantityPerXFranchises(ResourceTypes eResource, int iValue)
{
	if(GetResourceQuantityPerXFranchises(eResource) != iValue)
	{
		m_aiResourceQuantityPerXFranchises.setAt(eResource, iValue);
	}
}
//	--------------------------------------------------------------------------------
/// Trade Route Religious Spread Boost
int CvCity::GetReligiousTradeModifier() const
{
	VALIDATE_OBJECT
	return m_iReligiousTradeModifier;
}
void CvCity::ChangeReligiousTradeModifier(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		SetReligiousTradeModifier(GetReligiousTradeModifier() + iChange);
	}
}
void CvCity::SetReligiousTradeModifier(int iValue)
{
	VALIDATE_OBJECT
	m_iReligiousTradeModifier = iValue;
}
//	--------------------------------------------------------------------------------
/// Free building built in target trade city (foreign)
int CvCity::GetFreeBuildingTradeTargetCity() const
{
	VALIDATE_OBJECT
	return m_iFreeBuildingTradeTargetCity;
}
void CvCity::ChangeFreeBuildingTradeTargetCity(int iChange)
{
	VALIDATE_OBJECT
	if(iChange != 0)
	{
		SetFreeBuildingTradeTargetCity(GetFreeBuildingTradeTargetCity() + iChange);
	}
}
void CvCity::SetFreeBuildingTradeTargetCity(int iValue)
{
	VALIDATE_OBJECT
	m_iFreeBuildingTradeTargetCity = iValue;
}


//	--------------------------------------------------------------------------------
int CvCity::GetLandTourismBonus() const
{
	VALIDATE_OBJECT
	return m_iLandTourismBonus;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeLandTourismBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iLandTourismBonus = (m_iLandTourismBonus + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetLandTourismBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iLandTourismBonus = iChange;
}


//	--------------------------------------------------------------------------------
int CvCity::GetSeaTourismBonus() const
{
	VALIDATE_OBJECT
	return m_iSeaTourismBonus;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeSeaTourismBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iSeaTourismBonus = (m_iSeaTourismBonus + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetSeaTourismBonus(int iChange)
{
	VALIDATE_OBJECT
	m_iSeaTourismBonus = iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::GetAlwaysHeal() const
{
	VALIDATE_OBJECT
	return m_iAlwaysHeal;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeAlwaysHeal(int iChange)
{
	VALIDATE_OBJECT
	m_iAlwaysHeal = (m_iAlwaysHeal + iChange);
}
//	--------------------------------------------------------------------------------
void CvCity::SetAlwaysHeal(int iChange)
{
	VALIDATE_OBJECT
	m_iAlwaysHeal = iChange;
}

//	--------------------------------------------------------------------------------
bool CvCity::IsBastion() const
{
	VALIDATE_OBJECT
	return m_bIsBastion;
}

//	--------------------------------------------------------------------------------
void CvCity::TestBastion()
{
	//Check to see if this is a city we really need to defend.
	if(isCapital())
	{
		SetBastion(true);
		return;
	}
	if(plot()->IsChokePoint() || plot()->IsLandbridge(12, 54))
	{
		SetBastion(true);
		return;
	}
	//Coastal and we can embark across oceans? Check for lake, otherwise make a bastion (better safe than sorry).
	if(plot()->isCoastalLand() && GET_TEAM(getTeam()).canEmbarkAllWaterPassage())
	{
		AICityStrategyTypes eStrategyLakeBound = (AICityStrategyTypes) GC.getInfoTypeForString("AICITYSTRATEGY_LAKEBOUND");
		if(eStrategyLakeBound != NO_ECONOMICAISTRATEGY)
		{
			if(!GetCityStrategyAI()->IsUsingCityStrategy(eStrategyLakeBound))
			{
				SetBastion(true);
				return;
			}
		}
	}
	for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
	{
		PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;
		if(eLoopPlayer != NO_PLAYER && !GET_PLAYER(eLoopPlayer).isMinorCiv() && eLoopPlayer != getOwner())
		{
			if(plot()->IsHomeFrontForPlayer(eLoopPlayer))
			{
				SetBastion(true);
				return;
			}
		}
	}
	//Not a frontier city, and not a chokepoint? Not a bastion.
	if(IsBastion())
	{
		SetBastion(false);
	}
	return;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBastion(bool bValue)
{
	VALIDATE_OBJECT
	m_bIsBastion = bValue;
}

void CvCity::DoBarbIncursion()
{
	if(GC.getGame().isOption(GAMEOPTION_NO_BARBARIANS))
	{
		return;
	}
	// Found a CS city to spawn near
	if (MOD_DIPLOMACY_CITYSTATES_QUESTS && GET_PLAYER(getOwner()).isBarbarian() && GET_PLAYER(getOriginalOwner()).isMinorCiv())
	{
		if(CvBarbarians::ShouldSpawnBarbFromCity(plot()))
		{
			CvBarbarians::DoSpawnBarbarianUnit(plot(), false, false);
			CvBarbarians::DoCityActivationNotice(plot());
			if(GC.getLogging() && GC.getAILogging())
			{
				CvString strLogString;
				strLogString.Format("Unit spawned in barbarian city of %s at X: %d, Y: %d", getName().c_str(), getX(), getY());
				if(GET_PLAYER(BARBARIAN_PLAYER).GetID() != NO_PLAYER)
				{
					GET_PLAYER(BARBARIAN_PLAYER).GetTacticalAI()->LogTacticalMessage(strLogString);
				}
			}
		}
	}
	if(MOD_BALANCE_CORE_BARBARIAN_THEFT)
	{
		if(GET_PLAYER(BARBARIAN_PLAYER).GetID() == NO_PLAYER || getOwner() == NO_PLAYER || GET_PLAYER(getOwner()).isMinorCiv() || GET_PLAYER(getOwner()).isBarbarian())
			return;

		int iCityStrength = getStrengthValue(true);
		iCityStrength += (GC.getGame().getSmallFakeRandNum(10, getOwner()) * 10);
		iCityStrength /= 100;

		CvPlot* pLoopPlot;
		for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
		{
			pLoopPlot = plotDirection(getX(), getY(), ((DirectionTypes)iI));

			if(pLoopPlot != NULL && pLoopPlot->getOwner() == getOwner())
			{
				CvUnit* pUnit = pLoopPlot->getUnitByIndex(0);
				if(pUnit != NULL && pUnit->isBarbarian() && pUnit->IsCombatUnit())
				{			
					int iBarbStrength = pUnit->isRanged() ? (pUnit->GetBaseRangedCombatStrength() * 4) : (pUnit->GetBaseCombatStrength() * 4);
					iBarbStrength += GC.getGame().getSmallFakeRandNum(10, pUnit->GetID()) * 18;
					if(iBarbStrength > iCityStrength)
					{
						int iTheft = (iBarbStrength - iCityStrength);

						if(iTheft > 0)
						{
							int iYield = GC.getGame().getSmallFakeRandNum(10, pUnit->GetID());
							if(iYield <= 2)
							{
								int iGold = ((getBaseYieldRate(YIELD_GOLD) * iTheft) / 100);
								if(iGold > 0)
								{
									changeDamage(iTheft / 8);
									pUnit->changeDamage(iTheft / 8);
									GET_PLAYER(getOwner()).GetTreasury()->ChangeGold(-iGold);

									Localization::String strMessage = Localization::Lookup("TXT_KEY_BARBARIAN_GOLD_THEFT_CITY_DETAILED");
									strMessage << iGold;
									strMessage << getNameKey();
									Localization::String strSummary = Localization::Lookup("TXT_KEY_BARBARIAN_GOLD_THEFT_CITY");
									strSummary << getNameKey();

									CvNotifications* pNotification = GET_PLAYER(getOwner()).GetNotifications();
									if(pNotification)
									{
										pNotification->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), getOwner());
									}
								}
							}
							else if(iYield <= 4)
							{
								int iCulture = ((getJONSCulturePerTurn() * iTheft) / 100);
								if(iCulture > 0)
								{
									changeDamage(iTheft / 8);
									pUnit->changeDamage(iTheft / 8);
									GET_PLAYER(getOwner()).changeJONSCulture(-iCulture);

									Localization::String strMessage = Localization::Lookup("TXT_KEY_BARBARIAN_CULTURE_THEFT_CITY_DETAILED");
									strMessage << iCulture;
									strMessage << getNameKey();
									Localization::String strSummary = Localization::Lookup("TXT_KEY_BARBARIAN_CULTURE_THEFT_CITY");
									strSummary << getNameKey();

									CvNotifications* pNotification = GET_PLAYER(getOwner()).GetNotifications();
									if(pNotification)
									{
										pNotification->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), getOwner());
									}
								}
							}
							else if(iYield <= 6)
							{
								TechTypes eCurrentTech = GET_PLAYER(getOwner()).GetPlayerTechs()->GetCurrentResearch();
								int iScience = 0;
								if(eCurrentTech != NO_TECH)
								{
									iScience = ((getBaseYieldRate(YIELD_SCIENCE) * iTheft) / 100);
									if(iScience > 0)
									{
										changeDamage(iTheft / 8);
										pUnit->changeDamage(iTheft / 8);
										GET_TEAM(GET_PLAYER(getOwner()).getTeam()).GetTeamTechs()->ChangeResearchProgress(eCurrentTech, -iScience, getOwner());

										Localization::String strMessage = Localization::Lookup("TXT_KEY_BARBARIAN_SCIENCE_THEFT_CITY_DETAILED");
										strMessage << iScience;
										strMessage << getNameKey();
										Localization::String strSummary = Localization::Lookup("TXT_KEY_BARBARIAN_SCIENCE_THEFT_CITY");
										strSummary << getNameKey();

										CvNotifications* pNotification = GET_PLAYER(getOwner()).GetNotifications();
										if(pNotification)
										{
											pNotification->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), getOwner());
										}
									}
								}
							}
							else if(iYield <= 8)
							{
								int iFood = ((getBaseYieldRate(YIELD_FOOD) * iTheft) / 100);
								if(iFood > 0)
								{
									if(iFood > getFood())
									{
										setFood(0);
									}
									else
									{
										changeFood(-iFood);
									}

									changeDamage(iTheft / 8);
									pUnit->changeDamage(iTheft / 8);
									Localization::String strMessage = Localization::Lookup("TXT_KEY_BARBARIAN_FOOD_THEFT_CITY_DETAILED");
									strMessage << iFood;
									strMessage << getNameKey();
									Localization::String strSummary = Localization::Lookup("TXT_KEY_BARBARIAN_FOOD_THEFT_CITY");
									strSummary << getNameKey();

									CvNotifications* pNotification = GET_PLAYER(getOwner()).GetNotifications();
									if(pNotification)
									{
										pNotification->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), getOwner());
									}
								}
							}
							else
							{
								if((getProduction() > 0) && (getProductionTurnsLeft() >= 2) && (getProductionTurnsLeft() != INT_MAX))
								{
									int iProduction = ((getBaseYieldRate(YIELD_PRODUCTION) * iTheft) / 100);
									if(iProduction > 0)
									{
										if(iProduction > getProduction())
										{
											setProduction(0);
										}
										else
										{
											changeProduction(-iProduction);
										}

										changeDamage(iTheft / 8);
										pUnit->changeDamage(iTheft / 8);
										Localization::String strMessage = Localization::Lookup("TXT_KEY_BARBARIAN_PRODUCTION_THEFT_CITY_DETAILED");
										strMessage << iProduction;
										strMessage << getNameKey();
										Localization::String strSummary = Localization::Lookup("TXT_KEY_BARBARIAN_PRODUCTION_THEFT_CITY");
										strSummary << getNameKey();

										CvNotifications* pNotification = GET_PLAYER(getOwner()).GetNotifications();
										if(pNotification)
										{
											pNotification->Add(NOTIFICATION_GENERIC, strMessage.toUTF8(), strSummary.toUTF8(), getX(), getY(), getOwner());
										}
									}
								}
							}
						}
					}
				}
			}
		}
	}
}
#endif
#if defined(MOD_BALANCE_CORE_SPIES)
//	--------------------------------------------------------------------------------
//	--------------------------------------------------------------------------------
void CvCity::SetBlockBuildingDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockBuildingDestruction = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockBuildingDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockBuildingDestruction(GetBlockBuildingDestruction() + iNewValue);
}
int CvCity::GetBlockBuildingDestruction() const
{
	VALIDATE_OBJECT
	return m_iBlockBuildingDestruction;
}
//	--------------------------------------------------------------------------------
//	--------------------------------------------------------------------------------
void CvCity::SetBlockWWDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockWWDestruction = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockWWDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockWWDestruction(GetBlockWWDestruction() + iNewValue);
}
int CvCity::GetBlockWWDestruction() const
{
	VALIDATE_OBJECT
	return m_iBlockWWDestruction;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockUDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockUDestruction = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockUDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockUDestruction(GetBlockUDestruction() + iNewValue);
}
int CvCity::GetBlockUDestruction() const
{
	VALIDATE_OBJECT
	return m_iBlockUDestruction;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockGPDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockGPDestruction = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockGPDestruction(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockGPDestruction(GetBlockGPDestruction() + iNewValue);
}
int CvCity::GetBlockGPDestruction() const
{
	VALIDATE_OBJECT
	return m_iBlockGPDestruction;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockRebellion(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockRebellion = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockRebellion(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockRebellion (GetBlockRebellion () + iNewValue);
}
int CvCity::GetBlockRebellion() const
{
	VALIDATE_OBJECT
	return m_iBlockRebellion;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockUnrest(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockUnrest = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockUnrest(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockUnrest(GetBlockUnrest() + iNewValue);
}
int CvCity::GetBlockUnrest() const
{
	VALIDATE_OBJECT
	return m_iBlockUnrest;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockScience(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockScience = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockScience(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockScience(GetBlockScience() + iNewValue);
}
int CvCity::GetBlockScience() const
{
	VALIDATE_OBJECT
	return m_iBlockScience;
}
//	--------------------------------------------------------------------------------
void CvCity::SetBlockGold(int iNewValue)
{
	VALIDATE_OBJECT
	m_iBlockGold = iNewValue;
}
//	--------------------------------------------------------------------------------
void CvCity::ChangeBlockGold(int iNewValue)
{
	VALIDATE_OBJECT
	SetBlockGold(GetBlockGold() + iNewValue);
}
int CvCity::GetBlockGold() const
{
	VALIDATE_OBJECT
	return m_iBlockGold;
}
#endif
#if defined(MOD_BALANCE_CORE)
void CvCity::SetPurchased(BuildingClassTypes eBuildingClass, bool bValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eBuildingClass >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eBuildingClass < GC.getNumBuildingClassInfos(), "eIndex expected to be < MAX_PLAYERS");
	if (m_abIsPurchased[eBuildingClass] != bValue)
	{
		m_abIsPurchased.setAt(eBuildingClass, bValue);
	}
}
bool CvCity::IsPurchased(BuildingClassTypes eBuildingClass)
{
	VALIDATE_OBJECT
	CvAssertMsg(eBuildingClass >= 0, "eBuildingClass expected to be >= 0");
	CvAssertMsg(eBuildingClass < GC.getNumBuildingClassInfos(), "eBuildingClass expected to be < GC.getNumBuildingClassInfos()");
	return m_abIsPurchased[eBuildingClass];
}
void CvCity::SetBestForWonder(BuildingClassTypes eBuildingClass, bool bValue)
{
	VALIDATE_OBJECT
		CvAssertMsg(eBuildingClass >= 0, "eBuildingClass expected to be >= 0");
	CvAssertMsg(eBuildingClass < GC.getNumBuildingClassInfos(), "eBuildingClass expected to be < GC.getNumBuildingClassInfos()");
	if (m_abIsBestForWonder[eBuildingClass] != bValue)
	{
		m_abIsBestForWonder.setAt(eBuildingClass, bValue);
	}
}
bool CvCity::IsBestForWonder(BuildingClassTypes eBuildingClass)
{
	VALIDATE_OBJECT
		CvAssertMsg(eBuildingClass >= 0, "eBuildingClass expected to be >= 0");
	CvAssertMsg(eBuildingClass < GC.getNumBuildingClassInfos(), "eBuildingClass expected to be < GC.getNumBuildingClassInfos()");
	return m_abIsBestForWonder[eBuildingClass];
}

#endif
//	--------------------------------------------------------------------------------
/// Extra yield for each pop point
int CvCity::GetYieldPerPopTimes100(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiYieldPerPop[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield for each pop point
void CvCity::ChangeYieldPerPopTimes100(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
		m_aiYieldPerPop.setAt(eIndex, m_aiYieldPerPop[eIndex] + iChange);
}

//	--------------------------------------------------------------------------------
/// Extra yield for each religion with a follower
int CvCity::GetYieldPerReligionTimes100(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	return m_aiYieldPerReligion[eIndex];
}

//	--------------------------------------------------------------------------------
/// Extra yield for each religion with a follower
void CvCity::ChangeYieldPerReligionTimes100(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiYieldPerReligion.setAt(eIndex, m_aiYieldPerReligion[eIndex] + iChange);
	}
}

//	--------------------------------------------------------------------------------
int CvCity::getYieldRateModifier(YieldTypes eIndex)	const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiYieldRateModifier[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeYieldRateModifier(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	if(iChange != 0)
	{
		m_aiYieldRateModifier.setAt(eIndex, m_aiYieldRateModifier[eIndex] + iChange);
		CvAssert(getYieldRate(eIndex, false) != 0);

		GET_PLAYER(getOwner()).invalidateYieldRankCache(eIndex);
	}
}

#if defined(MOD_BALANCE_CORE_POLICIES)
//	--------------------------------------------------------------------------------
int CvCity::getReligionBuildingYieldRateModifier(BuildingClassTypes eIndex1, YieldTypes eIndex2)	const
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiReligionBuildingYieldRateModifier[eIndex1][eIndex2];
}


//	--------------------------------------------------------------------------------
void CvCity::changeReligionBuildingYieldRateModifier(BuildingClassTypes eIndex1, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiReligionBuildingYieldRateModifier[eIndex1][eIndex2] += iChange;
	}
}
//	--------------------------------------------------------------------------------
int CvCity::getLocalBuildingClassYield(BuildingClassTypes eIndex1, YieldTypes eIndex2)	const
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");
	return m_ppaiLocalBuildingClassYield[eIndex1][eIndex2];
}


//	--------------------------------------------------------------------------------
void CvCity::changeLocalBuildingClassYield(BuildingClassTypes eIndex1, YieldTypes eIndex2, int iChange)
{
	CvAssertMsg(eIndex1 >= 0, "eIndex1 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex1 < GC.getNumBuildingClassInfos(), "eIndex1 is expected to be within maximum bounds (invalid Index)");
	CvAssertMsg(eIndex2 >= 0, "eIndex2 is expected to be non-negative (invalid Index)");
	CvAssertMsg(eIndex2 < NUM_YIELD_TYPES, "eIndex2 is expected to be within maximum bounds (invalid Index)");

	if(iChange != 0)
	{
		m_ppaiLocalBuildingClassYield[eIndex1][eIndex2] += iChange;
	}
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getResourceYieldRateModifier(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiResourceYieldRateModifier[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeResourceYieldRateModifier(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiResourceYieldRateModifier.setAt(eIndex, m_aiResourceYieldRateModifier[eIndex] + iChange);
		CvAssert(getYieldRate(eIndex, false) >= 0);

		GET_PLAYER(getOwner()).invalidateYieldRankCache(eIndex);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getExtraSpecialistYield(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiExtraSpecialistYield[eIndex];
}


//	--------------------------------------------------------------------------------
int CvCity::getExtraSpecialistYield(YieldTypes eIndex, SpecialistTypes eSpecialist) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	CvAssertMsg(eSpecialist >= 0, "eSpecialist expected to be >= 0");
	CvAssertMsg(eSpecialist < GC.getNumSpecialistInfos(), "GC.getNumSpecialistInfos expected to be >= 0");
#if !defined(MOD_BALANCE_CORE)
	if (eSpecialist == GC.getDEFAULT_SPECIALIST())
	{
		return 0;
	}
#endif

	int iYieldMultiplier = GET_PLAYER(getOwner()).getSpecialistExtraYield(eSpecialist, eIndex) +
	                       GET_PLAYER(getOwner()).getSpecialistExtraYield(eIndex) +
	                       GET_PLAYER(getOwner()).GetPlayerTraits()->GetSpecialistYieldChange(eSpecialist, eIndex);
#if defined(MOD_BALANCE_CORE_EVENTS)
	iYieldMultiplier += GetEventSpecialistYield(eSpecialist, eIndex);
#endif
#if defined(MOD_API_UNIFIED_YIELDS)
	iYieldMultiplier += getSpecialistExtraYield(eSpecialist, eIndex);
	iYieldMultiplier += GET_PLAYER(getOwner()).getSpecialistYieldChange(eSpecialist, eIndex);

	ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
	if(eMajority >= RELIGION_PANTHEON)
	{
		const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
		if(pReligion)
		{
			iYieldMultiplier += pReligion->m_Beliefs.GetSpecialistYieldChange(eSpecialist, eIndex, getOwner());
			BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
			if (eSecondaryPantheon != NO_BELIEF)
			{
				iYieldMultiplier += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetSpecialistYieldChange(eSpecialist, eIndex);
			}
		}
	}
#endif
	int iExtraYield = GetCityCitizens()->GetSpecialistCount(eSpecialist) * iYieldMultiplier;

	return iExtraYield;
}


//	--------------------------------------------------------------------------------
void CvCity::updateExtraSpecialistYield(YieldTypes eYield)
{
	VALIDATE_OBJECT
	int iOldYield;
	int iNewYield;
	int iI;

	CvAssertMsg(eYield >= 0, "eYield expected to be >= 0");
	CvAssertMsg(eYield < NUM_YIELD_TYPES, "eYield expected to be < NUM_YIELD_TYPES");

	iOldYield = getExtraSpecialistYield(eYield);

	iNewYield = 0;

	for(iI = 0; iI < GC.getNumSpecialistInfos(); iI++)
	{
		iNewYield += getExtraSpecialistYield(eYield, ((SpecialistTypes)iI));
	}

	if(iOldYield != iNewYield)
	{
		m_aiExtraSpecialistYield.setAt(eYield, iNewYield);
		CvAssert(getExtraSpecialistYield(eYield) >= 0);

		ChangeBaseYieldRateFromSpecialists(eYield, (iNewYield - iOldYield));
	}
}


//	--------------------------------------------------------------------------------
void CvCity::updateExtraSpecialistYield()
{
	VALIDATE_OBJECT
	int iI;

	for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		updateExtraSpecialistYield((YieldTypes)iI);
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getProductionToYieldModifier(YieldTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");
	return m_aiProductionToYieldModifier[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeProductionToYieldModifier(YieldTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_YIELD_TYPES, "eIndex expected to be < NUM_YIELD_TYPES");

	if(iChange != 0)
	{
		m_aiProductionToYieldModifier.setAt(eIndex, m_aiProductionToYieldModifier[eIndex] + iChange);
	}
}

//	--------------------------------------------------------------------------------
int CvCity::GetTradeYieldModifier(YieldTypes eIndex, CvString* toolTipSink) const
{
	int iReturnValue = GET_PLAYER(m_eOwner).GetTrade()->GetTradeValuesAtCityTimes100(this, eIndex);
	if (toolTipSink)
	{
		if (iReturnValue != 0)
		{
			switch (eIndex)
			{
			case YIELD_FOOD:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_FOOD_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
			case YIELD_PRODUCTION:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_PRODUCTION_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
			case YIELD_GOLD:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_GOLD_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
			case YIELD_SCIENCE:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_SCIENCE_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
			case YIELD_CULTURE:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_CULTURE_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
			case YIELD_FAITH:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_FAITH_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
#if defined(MOD_API_UNIFIED_YIELDS_TOURISM)
			case YIELD_TOURISM:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_TOURISM_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
#endif
#if defined(MOD_API_UNIFIED_YIELDS_GOLDEN_AGE)
			case YIELD_GOLDEN_AGE_POINTS:
				*toolTipSink += "[NEWLINE][BULLET]";
				*toolTipSink += GetLocalizedText("TXT_KEY_GOLDEN_AGE_POINTS_FROM_TRADE_ROUTES", iReturnValue / 100.0f);
				break;
#endif
			}
		}
	}
	return iReturnValue;
}

//	--------------------------------------------------------------------------------
int CvCity::getDomainFreeExperience(DomainTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");
	return m_aiDomainFreeExperience[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeDomainFreeExperience(DomainTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");
	m_aiDomainFreeExperience.setAt(eIndex, m_aiDomainFreeExperience[eIndex] + iChange);
	CvAssert(getDomainFreeExperience(eIndex) >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getDomainFreeExperienceFromGreatWorks(DomainTypes eIndex) const
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");

	int iXP = 0;

	CvBuildingXMLEntries* pkBuildings = GetCityBuildings()->GetPossibleBuildings();
	for(int iBuilding = 0; iBuilding < pkBuildings->GetNumBuildings(); iBuilding++)
	{
		CvBuildingEntry* pInfo = pkBuildings->GetEntry(iBuilding);
		if(pInfo)
		{
			if (pInfo->GetDomainFreeExperiencePerGreatWork(eIndex) != 0)
			{
				int iGreatWorks = GetCityBuildings()->GetNumGreatWorksInBuilding((BuildingClassTypes)pInfo->GetBuildingClassType());
				iXP += (iGreatWorks * pInfo->GetDomainFreeExperiencePerGreatWork(eIndex));
			}
		}
	}

	return iXP;
}
#if defined(MOD_BALANCE_CORE)
//	--------------------------------------------------------------------------------
int CvCity::getDomainFreeExperienceFromGreatWorksGlobal(DomainTypes eIndex) const
{
	VALIDATE_OBJECT
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");

	int iMod = GET_PLAYER(getOwner()).GetDomainFreeExperiencePerGreatWorkGlobal(eIndex);
	if(iMod <= 0)
	{
		return iMod;
	}

	int iXP = 0;
	int iLoop = 0;
	int iGreatWorks = 0;
	for(const CvCity* pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
	{
		if(pLoopCity != NULL)
		{
			iGreatWorks += pLoopCity->GetCityBuildings()->GetNumGreatWorks(CvTypes::getGREAT_WORK_SLOT_LITERATURE());
		}
	}
	iXP += (iGreatWorks * iMod);

	if(iXP > 45)
	{
		iXP = 45;
	}

	return iXP;
}
#endif

//	--------------------------------------------------------------------------------
int CvCity::getDomainProductionModifier(DomainTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");
	return m_aiDomainProductionModifier[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeDomainProductionModifier(DomainTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < NUM_DOMAIN_TYPES, "eIndex expected to be < NUM_DOMAIN_TYPES");
	m_aiDomainProductionModifier.setAt(eIndex, m_aiDomainProductionModifier[eIndex] + iChange);
}


//	--------------------------------------------------------------------------------
bool CvCity::isEverOwned(PlayerTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	return m_abEverOwned[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::setEverOwned(PlayerTypes eIndex, bool bNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < MAX_PLAYERS, "eIndex expected to be < MAX_PLAYERS");
	m_abEverOwned.setAt(eIndex, bNewValue);
}

//	--------------------------------------------------------------------------------
bool CvCity::isRevealed(TeamTypes eIndex, bool bDebug) const
{
	VALIDATE_OBJECT
	if(bDebug && GC.getGame().isDebugMode())
	{
		return true;
	}
	else
	{
		CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
		CvAssertMsg(eIndex < MAX_TEAMS, "eIndex expected to be < MAX_TEAMS");

		return m_abRevealed[eIndex];
	}
}


//	--------------------------------------------------------------------------------
bool CvCity::setRevealed(TeamTypes eIndex, bool bNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < MAX_TEAMS, "eIndex expected to be < MAX_TEAMS");

	if(isRevealed(eIndex, false) != bNewValue)
	{
		m_abRevealed.setAt(eIndex, bNewValue);

		return true;
	}
	return false;
}


//	--------------------------------------------------------------------------------
const char* CvCity::getNameKey() const
{
	VALIDATE_OBJECT
	return m_strName.get();
}


//	--------------------------------------------------------------------------------
const CvString CvCity::getName() const
{
	VALIDATE_OBJECT
	return GetLocalizedText(m_strName.get());
}


//	--------------------------------------------------------------------------------
void CvCity::setName(const char* szNewValue, bool bFound)
{
	VALIDATE_OBJECT
	CvString strName(szNewValue);
	gDLL->stripSpecialCharacters(strName);

	if(!strName.IsEmpty())
	{
		if(GET_PLAYER(getOwner()).isCityNameValid(strName, false))
		{
			m_strName = strName;

			if(isCitySelected())
			{
				DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			}


			auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
			DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
		}
		if(bFound)
		{
			doFoundMessage();
		}
	}
}


//	--------------------------------------------------------------------------------
void CvCity::doFoundMessage()
{
	VALIDATE_OBJECT
	if(getOwner() == GC.getGame().getActivePlayer())
	{
		Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_CITY_HAS_BEEN_FOUNDED");
		localizedText << getNameKey();
		DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, -1, localizedText.toUTF8(), NULL /*ARTFILEMGR.getInterfaceArtInfo("WORLDBUILDER_CITY_EDIT")->getPath()*/, MESSAGE_TYPE_MAJOR_EVENT, NULL, NO_COLOR, getX(), getY());
	}

	Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_CITY_IS_FOUNDED");
	localizedText << getNameKey();
	GC.getGame().addReplayMessage(REPLAY_MESSAGE_CITY_FOUNDED, getOwner(), localizedText.toUTF8(), getX(), getY());
}

//	--------------------------------------------------------------------------------
bool CvCity::IsExtraLuxuryResources()
{
	return (m_iCountExtraLuxuries > 0);
}

//	--------------------------------------------------------------------------------
void CvCity::SetExtraLuxuryResources(int iNewValue)
{
	m_iCountExtraLuxuries = iNewValue;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeExtraLuxuryResources(int iChange)
{
	m_iCountExtraLuxuries += iChange;
}

//	--------------------------------------------------------------------------------
int CvCity::getProjectProduction(ProjectTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumProjectInfos(), "eIndex expected to be < GC.getNumProjectInfos()");
	return m_paiProjectProduction[eIndex] / 100;
}


//	--------------------------------------------------------------------------------
void CvCity::setProjectProduction(ProjectTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	setProjectProductionTimes100(eIndex, iNewValue*100);
}


//	--------------------------------------------------------------------------------
void CvCity::changeProjectProduction(ProjectTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	changeProjectProductionTimes100(eIndex, iChange*100);
}

//	--------------------------------------------------------------------------------
int CvCity::getProjectProductionTimes100(ProjectTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumProjectInfos(), "eIndex expected to be < GC.getNumProjectInfos()");
	return m_paiProjectProduction[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::setProjectProductionTimes100(ProjectTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumProjectInfos(), "eIndex expected to be < GC.getNumProjectInfos()");

	if(getProjectProductionTimes100(eIndex) != iNewValue)
	{
		m_paiProjectProduction.setAt(eIndex, iNewValue);
		CvAssert(getProjectProductionTimes100(eIndex) >= 0);

		if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
		{
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
		}

		auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
		DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeProjectProductionTimes100(ProjectTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumProjectInfos(), "eIndex expected to be < GC.getNumProjectInfos()");
	setProjectProductionTimes100(eIndex, (getProjectProductionTimes100(eIndex) + iChange));
}


//	--------------------------------------------------------------------------------
int CvCity::getSpecialistProduction(SpecialistTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumSpecialistInfos(), "eIndex expected to be < GC.getNumSpecialistInfos()");
	return m_paiSpecialistProduction[eIndex] / 100;
}


//	--------------------------------------------------------------------------------
void CvCity::setSpecialistProduction(SpecialistTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	setSpecialistProductionTimes100(eIndex, iNewValue*100);
}


//	--------------------------------------------------------------------------------
void CvCity::changeSpecialistProduction(SpecialistTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	changeSpecialistProductionTimes100(eIndex,iChange*100);
}

//	--------------------------------------------------------------------------------
int CvCity::getSpecialistProductionTimes100(SpecialistTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumSpecialistInfos(), "eIndex expected to be < GC.getNumSpecialistInfos()");
	return m_paiSpecialistProduction[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::setSpecialistProductionTimes100(SpecialistTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumSpecialistInfos(), "eIndex expected to be < GC.getNumSpecialistInfos()");

	if(getSpecialistProductionTimes100(eIndex) != iNewValue)
	{
		m_paiSpecialistProduction.setAt(eIndex, iNewValue);
		CvAssert(getSpecialistProductionTimes100(eIndex) >= 0);

		if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
		{
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
		}

		auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
		DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeSpecialistProductionTimes100(SpecialistTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumSpecialistInfos(), "eIndex expected to be < GC.getNumSpecialistInfos()");
	setSpecialistProductionTimes100(eIndex, (getSpecialistProductionTimes100(eIndex) + iChange));
}

#if defined(MOD_PROCESS_STOCKPILE)
//	--------------------------------------------------------------------------------
int CvCity::getProcessProduction(ProcessTypes eIndex) const
{
	return getProcessProductionTimes100(eIndex) / 100;
}

//	--------------------------------------------------------------------------------
int CvCity::getProcessProductionTimes100(ProcessTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumProcessInfos(), "eIndex expected to be < GC.getNumProcessInfos()");
	
	if (eIndex == GC.getInfoTypeForString("PROCESS_STOCKPILE")) {
		return getBasicYieldRateTimes100(YIELD_PRODUCTION, false);
	}

	return 0;
}
#endif

//	--------------------------------------------------------------------------------
CvCityBuildings* CvCity::GetCityBuildings() const
{
	VALIDATE_OBJECT
	return m_pCityBuildings;
}

//	--------------------------------------------------------------------------------
int CvCity::getUnitProduction(UnitTypes eIndex)	const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitInfos(), "eIndex expected to be < GC.getNumUnitInfos()");
	return m_paiUnitProduction[eIndex] / 100;
}


//	--------------------------------------------------------------------------------
void CvCity::setUnitProduction(UnitTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	setUnitProductionTimes100(eIndex, iNewValue * 100);
}


//	--------------------------------------------------------------------------------
void CvCity::changeUnitProduction(UnitTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	changeUnitProductionTimes100(eIndex, iChange * 100);
}


//	--------------------------------------------------------------------------------
int CvCity::getUnitProductionTimes100(UnitTypes eIndex)	const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitInfos(), "eIndex expected to be < GC.getNumUnitInfos()");
	return m_paiUnitProduction[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::setUnitProductionTimes100(UnitTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitInfos(), "eIndex expected to be < GC.getNumUnitInfos()");

	if(getUnitProductionTimes100(eIndex) != iNewValue)
	{
		m_paiUnitProduction.setAt(eIndex, iNewValue);
		CvAssert(getUnitProductionTimes100(eIndex) >= 0);

		if((getOwner() == GC.getGame().getActivePlayer()) && isCitySelected())
		{
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
		}

		auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
		DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_BANNER);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::changeUnitProductionTimes100(UnitTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	setUnitProductionTimes100(eIndex, (getUnitProductionTimes100(eIndex) + iChange));
}


//	--------------------------------------------------------------------------------
int CvCity::getUnitProductionTime(UnitTypes eIndex)	const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitInfos(), "eIndex expected to be < GC.getNumUnitInfos()");
	return m_paiUnitProductionTime[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::setUnitProductionTime(UnitTypes eIndex, int iNewValue)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitInfos(), "eIndex expected to be < GC.getNumUnitInfos()");
	m_paiUnitProductionTime.setAt(eIndex, iNewValue);
	CvAssert(getUnitProductionTime(eIndex) >= 0);
}


//	--------------------------------------------------------------------------------
void CvCity::changeUnitProductionTime(UnitTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	setUnitProductionTime(eIndex, (getUnitProductionTime(eIndex) + iChange));
}


//	--------------------------------------------------------------------------------
int CvCity::getUnitCombatFreeExperience(UnitCombatTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitCombatClassInfos(), "eIndex expected to be < GC.getNumUnitCombatInfos()");
	return m_paiUnitCombatFreeExperience[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeUnitCombatFreeExperience(UnitCombatTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitCombatClassInfos(), "eIndex expected to be < GC.getNumUnitCombatInfos()");
	m_paiUnitCombatFreeExperience.setAt(eIndex, m_paiUnitCombatFreeExperience[eIndex] + iChange);
	CvAssert(getUnitCombatFreeExperience(eIndex) >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getUnitCombatProductionModifier(UnitCombatTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitCombatClassInfos(), "eIndex expected to be < GC.getNumUnitCombatInfos()");
	return m_paiUnitCombatProductionModifier[eIndex];
}


//	--------------------------------------------------------------------------------
void CvCity::changeUnitCombatProductionModifier(UnitCombatTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumUnitCombatClassInfos(), "eIndex expected to be < GC.getNumUnitCombatInfos()");
	m_paiUnitCombatProductionModifier.setAt(eIndex, m_paiUnitCombatProductionModifier[eIndex] + iChange);
	CvAssert(getUnitCombatProductionModifier(eIndex) >= 0);
}


//	--------------------------------------------------------------------------------
int CvCity::getFreePromotionCount(PromotionTypes eIndex) const
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumPromotionInfos(), "eIndex expected to be < GC.getNumPromotionInfos()");
	return m_paiFreePromotionCount[eIndex];
}


//	--------------------------------------------------------------------------------
bool CvCity::isFreePromotion(PromotionTypes eIndex) const
{
	VALIDATE_OBJECT
	return (getFreePromotionCount(eIndex) > 0);
}


//	--------------------------------------------------------------------------------
void CvCity::changeFreePromotionCount(PromotionTypes eIndex, int iChange)
{
	VALIDATE_OBJECT
	CvAssertMsg(eIndex >= 0, "eIndex expected to be >= 0");
	CvAssertMsg(eIndex < GC.getNumPromotionInfos(), "eIndex expected to be < GC.getNumPromotionInfos()");
	m_paiFreePromotionCount.setAt(eIndex, m_paiFreePromotionCount[eIndex] + iChange);
	CvAssert(getFreePromotionCount(eIndex) >= 0);
}

//	--------------------------------------------------------------------------------
int CvCity::getSpecialistFreeExperience() const
{
	VALIDATE_OBJECT
	return m_iSpecialistFreeExperience;
}


//	--------------------------------------------------------------------------------
void CvCity::changeSpecialistFreeExperience(int iChange)
{
	VALIDATE_OBJECT
	m_iSpecialistFreeExperience += iChange;
	CvAssert(m_iSpecialistFreeExperience >= 0);
}


//	--------------------------------------------------------------------------------
void CvCity::updateStrengthValue()
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::updateStrengthValue, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	// Default Strength
	int iStrengthValue = /*600*/ GC.getCITY_STRENGTH_DEFAULT();

	// Population mod
	iStrengthValue += getPopulation() * /*25*/ GC.getCITY_STRENGTH_POPULATION_CHANGE();

	// Building Defense
	int iBuildingDefense = m_pCityBuildings->GetBuildingDefense();
#if defined(MOD_BALANCE_CORE)
	CvPlot* pCityPlot = plot();
	CvPlot* pAdjacentPlot = NULL;
	int iNumAdjUnits = 0;
	int iAdjUnitDefense = 0;
	for(int iI = 0; iI < NUM_DIRECTION_TYPES; ++iI)
	{
		pAdjacentPlot = plotDirection(pCityPlot->getX(), pCityPlot->getY(), ((DirectionTypes)iI));
		if(pAdjacentPlot != NULL)
		{
			for(int iUnitLoop = 0; iUnitLoop < pAdjacentPlot->getNumUnits(); iUnitLoop++)
			{
				for(int iJ = 0; iJ < GC.getNumPromotionInfos(); iJ++)
				{
					const PromotionTypes eLoopPromotion = static_cast<PromotionTypes>(iJ);
					CvPromotionEntry* pkPromotionInfo = GC.getPromotionInfo(eLoopPromotion);
					if(pkPromotionInfo != NULL)
					{
						if(pkPromotionInfo->GetAdjacentCityDefenseMod() > 0)
						{
							const CvUnit* const adjUnit = pAdjacentPlot->getUnitByIndex(iUnitLoop);
							if(adjUnit != NULL && adjUnit->isHasPromotion(eLoopPromotion))
							{
								CvPlayer &adjUnitPlayer = GET_PLAYER(adjUnit->getOwner());
								if(GET_TEAM(adjUnitPlayer.getTeam()).GetID() == getTeam())
								{
									iNumAdjUnits++;
								}
							}
							iAdjUnitDefense = (pkPromotionInfo->GetAdjacentCityDefenseMod() * iNumAdjUnits);
						}
					}
				}
			}
		}
	}
#endif
	iBuildingDefense *= (100 + m_pCityBuildings->GetBuildingDefenseMod() + iAdjUnitDefense);
	iBuildingDefense /= 100;

	iStrengthValue += iBuildingDefense;

	// Garrisoned Unit
	int iStrengthFromUnits = 0;
	CvUnit* pGarrisonedUnit = GetGarrisonedUnit();
	if(pGarrisonedUnit)
	{
		iStrengthFromUnits = ((pGarrisonedUnit->GetBaseCombatStrength() * 100 * 100) / /*300*/ GC.getCITY_STRENGTH_UNIT_DIVISOR());

#if defined(MOD_BALANCE_CORE_MILITARY)
		if(MOD_BALANCE_CORE_MILITARY)
		{
			CvPlot* pTarget = plot();
			for(int iUnitLoop = 0; iUnitLoop < pTarget->getNumUnits(); iUnitLoop++)
			{
				if(pTarget->getUnitByIndex(iUnitLoop)->IsGreatGeneral() || pTarget->getUnitByIndex(iUnitLoop)->IsGreatAdmiral())
				{
					iStrengthFromUnits *= 2;
					break;
				}
			}
		}
#endif
	}

	iStrengthValue += iStrengthFromUnits;

	// Tech Progress increases City Strength
	int iTechProgress = GET_TEAM(getTeam()).GetTeamTechs()->GetNumTechsKnown() * 100 / GC.getNumTechInfos();

	// Want progress to be a value between 0 and 5
	double fTechProgress = iTechProgress / 100.0 * /*5*/ GC.getCITY_STRENGTH_TECH_BASE();
	double fTechExponent = /*2.0f*/ GC.getCITY_STRENGTH_TECH_EXPONENT();
	int iTechMultiplier = /*2*/ GC.getCITY_STRENGTH_TECH_MULTIPLIER();

	// The way all of this adds up...
	// 25% of the way through the game provides an extra 3.12
	// 50% of the way through the game provides an extra 12.50
	// 75% of the way through the game provides an extra 28.12
	// 100% of the way through the game provides an extra 50.00

	double fTechMod = pow(fTechProgress, fTechExponent);
	fTechMod *= iTechMultiplier;

	fTechMod *= 100;	// Bring it back into hundreds
	iStrengthValue += (int)(fTechMod + 0.005);	// Adding a small amount to prevent small fp accuracy differences from generating a different integer result on the Mac and PC. Assuming fTechMod is positive, round to nearest hundredth

	int iStrengthMod = 0;

	// Player-wide strength mod (Policies, etc.)
	iStrengthMod += GET_PLAYER(getOwner()).GetCityStrengthMod();

	// Apply Mod
	iStrengthValue *= (100 + iStrengthMod);
	iStrengthValue /= 100;

#if defined(MOD_BALANCE_CORE_DIPLOMACY_ADVANCED)
	if(MOD_BALANCE_CORE_DIPLOMACY_ADVANCED && GET_PLAYER(getOwner()).isMinorCiv() && isCapital())
	{
		PlayerTypes eAlly = GET_PLAYER(getOwner()).GetMinorCivAI()->GetAlly();
		if(eAlly != NO_PLAYER)
		{
			int iAllyBonus = (GET_PLAYER(eAlly).GetPlayerTraits()->GetAllianceCSDefense() + GC.getBALANCE_CS_ALLIANCE_DEFENSE_BONUS());
			if(iAllyBonus > 0)
			{
				iStrengthValue *= (100 + iAllyBonus);
				iStrengthValue /= 100;
			}
		}
		if(GET_PLAYER(getOwner()).GetMinorCivAI()->IsProtectedByAnyMajor())
		{
			int iProtections = 0;
			for(int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
			{
				PlayerTypes eLoopPlayer = (PlayerTypes) iPlayerLoop;
				if(eLoopPlayer != NO_PLAYER && GET_PLAYER(getOwner()).GetMinorCivAI()->IsProtectedByMajor(eLoopPlayer))
				{
					iProtections++;
				}
			}
			int iPtPBonus = GC.getBALANCE_CS_PLEDGE_TO_PROTECT_DEFENSE_BONUS() * iProtections;
			if(iPtPBonus > GC.getBALANCE_CS_PLEDGE_TO_PROTECT_DEFENSE_BONUS_MAX())
			{
				iPtPBonus = GC.getBALANCE_CS_PLEDGE_TO_PROTECT_DEFENSE_BONUS_MAX();
			}
			if(iPtPBonus > 0)
			{
				iStrengthValue *= (100 + iPtPBonus);
				iStrengthValue /= 100;
			}
		}
	}
#endif

	m_iStrengthValue = iStrengthValue;

	// Terrain mod
	if(plot()->isHills())
	{
		m_iStrengthValue += /*3*/ GC.getCITY_STRENGTH_HILL_CHANGE();
	}
#if defined(MOD_BALANCE_CORE)
	if(plot()->isMountain())
	{
		m_iStrengthValue += (/*3*/ GC.getCITY_STRENGTH_HILL_CHANGE() * 2);
	}
#endif

	DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
}

//	--------------------------------------------------------------------------------
int CvCity::getStrengthValue(bool bForRangeStrike) const
{
	VALIDATE_OBJECT
	// Strike strikes are weaker
	if(bForRangeStrike)
	{
		int iValue = m_iStrengthValue;

		iValue -= m_pCityBuildings->GetBuildingDefense();

		CvAssertMsg(iValue > 0, "City strength should always be greater than zero. Please show Jon this and send your last 5 autosaves.");

		iValue *= /*40*/ GC.getCITY_RANGED_ATTACK_STRENGTH_MULTIPLIER();
		iValue /= 100;

		if(HasGarrison())
		{
			iValue *= (100 + GET_PLAYER(m_eOwner).GetGarrisonedCityRangeStrikeModifier());
			iValue /= 100;
		}

		// Religion city strike mod
		int iReligionCityStrikeMod = 0;
		ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
		if(eMajority != NO_RELIGION)
		{
			const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
			if(pReligion)
			{
				iReligionCityStrikeMod = pReligion->m_Beliefs.GetCityRangeStrikeModifier(getOwner());
				BeliefTypes eSecondaryPantheon = GetCityReligions()->GetSecondaryReligionPantheonBelief();
				if (eSecondaryPantheon != NO_BELIEF)
				{
					iReligionCityStrikeMod += GC.GetGameBeliefs()->GetEntry(eSecondaryPantheon)->GetCityRangeStrikeModifier();
				}
				if(iReligionCityStrikeMod > 0)
				{
					iValue *= (100 + iReligionCityStrikeMod);
					iValue /= 100;
				}
			}
		}

		return iValue;
	}

	return m_iStrengthValue;
}

//	--------------------------------------------------------------------------------
int CvCity::GetPower() const
{
	VALIDATE_OBJECT
	return int(pow((double) getStrengthValue() / 100, 1.5));		// This is the same math used to calculate Unit Power in CvUnitEntry
}


//	--------------------------------------------------------------------------------
int CvCity::getDamage() const
{
	VALIDATE_OBJECT
	return m_iDamage;
}

//	--------------------------------------------------------------------------------
void CvCity::setDamage(int iValue, bool noMessage)
{
	float fDelay = 0.0f;

	VALIDATE_OBJECT

	if(iValue < 0)
		iValue = 0;
	else if(iValue > GetMaxHitPoints())
		iValue = GetMaxHitPoints();

	if(iValue != getDamage())
	{
		int iOldValue = getDamage();
		auto_ptr<ICvCity1> pDllCity(new CvDllCity(this));
		gDLL->GameplayCitySetDamage(pDllCity.get(), iValue, iOldValue);

		// send the popup text if the player can see this plot
		if(!noMessage && plot()->GetActiveFogOfWarMode() == FOGOFWARMODE_OFF)
		{
			char text[256];
			text[0] = NULL;
			int iNewValue = MIN(GetMaxHitPoints(),iValue);
			int iDiff = iOldValue - iNewValue;
			if(iNewValue < iOldValue)
			{
				sprintf_s(text, "[COLOR_GREEN]+%d[ENDCOLOR]", iDiff);
				fDelay = GC.getPOST_COMBAT_TEXT_DELAY() * 2;
			}
			else
			{
				sprintf_s(text, "[COLOR_RED]%d[ENDCOLOR]", iDiff);
			}

#if defined(SHOW_PLOT_POPUP)
			SHOW_PLOT_POPUP(plot(), NO_PLAYER, text, fDelay);
#else
			DLLUI->AddPopupText(m_iX, m_iY, text, fDelay);
#endif
		}
		m_iDamage = iValue;
	}
}

//	--------------------------------------------------------------------------------
void CvCity::changeDamage(int iChange)
{
	VALIDATE_OBJECT
	if(0 != iChange)
	{
		setDamage(getDamage() + iChange);
	}
}

//	--------------------------------------------------------------------------------
/// Can a specific plot be bought for the city
bool CvCity::CanBuyPlot(int iPlotX, int iPlotY, bool bIgnoreCost)
{
	VALIDATE_OBJECT
	CvPlot* pTargetPlot = NULL;

	if(GC.getBUY_PLOTS_DISABLED())
	{
		return false;
	}
#if defined(MOD_BALANCE_CORE)
	if(getOwner() == NO_PLAYER)
	{
		return false;
	}
#endif

	pTargetPlot = GC.getMap().plot(iPlotX, iPlotY);

	if(!pTargetPlot)
	{
		// no plot to buy
		return false;
	}

	// if this plot belongs to someone, bail!
	if(pTargetPlot->getOwner() != NO_PLAYER)
	{
#if defined(MOD_BALANCE_CORE)
		if(MOD_BALANCE_CORE && GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles())
		{
			if(pTargetPlot->getOwner() == getOwner() || pTargetPlot->isCity())
			{
				return false;
			}
		}
		else
		{
#endif
		return false;
#if defined(MOD_BALANCE_CORE)
		}
#endif
	}

	// Must be adjacent to a plot owned by this city
	CvPlot* pAdjacentPlot;
	bool bFoundAdjacent = false;
	for(int iI = 0; iI < NUM_DIRECTION_TYPES; iI++)
	{
		pAdjacentPlot = plotDirection(pTargetPlot->getX(), pTargetPlot->getY(), ((DirectionTypes)iI));

		if(pAdjacentPlot != NULL)
		{
			if(pAdjacentPlot->getOwner() == getOwner())
			{
				if(pAdjacentPlot->GetCityPurchaseID() == GetID())
				{
					bFoundAdjacent = true;
					break;
				}
			}
		}
	}

	if(!bFoundAdjacent)
		return false;

	// Max range of 3

	const int iMaxRange = getBuyPlotDistance();
	if(plotDistance(iPlotX, iPlotY, getX(), getY()) > iMaxRange)
		return false;

	// check money
	if(!bIgnoreCost)
	{
		if(GET_PLAYER(getOwner()).GetTreasury()->GetGold() < GetBuyPlotCost(pTargetPlot->getX(), pTargetPlot->getY()))
		{
			return false;
		}
	}

	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
		args->Push(iPlotX);
		args->Push(iPlotY);

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanBuyPlot", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
				return false;
			}
		}
	}

	return true;
}


//	--------------------------------------------------------------------------------
/// Can this city buy a plot, any plot?
bool CvCity::CanBuyAnyPlot(void)
{
	VALIDATE_OBJECT
	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if(pkScriptSystem)
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());

		// Attempt to execute the game events.
		// Will return false if there are no registered listeners.
		bool bResult = false;
		if(LuaSupport::CallTestAll(pkScriptSystem, "CityCanBuyAnyPlot", args.get(), bResult))
		{
			// Check the result.
			if(bResult == false)
			{
#if defined(MOD_BALANCE_CORE)
				if(GC.getLogging() && GC.getAILogging())
				{
					const CvPlayerAI& kOwner = GET_PLAYER(getOwner());
					CvString strPlayerName = kOwner.getCivilizationShortDescription();
					FILogFile* pLog = LOGFILEMGR.GetLog(kOwner.GetCitySpecializationAI()->GetLogFileName(strPlayerName), FILogFile::kDontTimeStamp);
					CvString strBaseString = CvString::format("%03d, %s, %s, CanBuyAnyPlot failed in lua hook", 
						GC.getGame().getElapsedGameTurns(), strPlayerName.c_str(), getName().GetCString() );
					pLog->Msg(strBaseString);
				}
#endif
				return false;
			}
		}
	}

	vector<int> plots;
	GetBuyablePlotList( plots, true );
	return !plots.empty();
}


//	--------------------------------------------------------------------------------
/// Which plot will we buy next
CvPlot* CvCity::GetNextBuyablePlot(bool bForPurchase)
{
	VALIDATE_OBJECT
	std::vector<int> aiPlotList;
	GetBuyablePlotList(aiPlotList, bForPurchase);

	if (aiPlotList.empty())
		return NULL;

	int iPickedIndex = GC.getGame().getSmallFakeRandNum( aiPlotList.size(), *plot());
	return GC.getMap().plotByIndex(aiPlotList[iPickedIndex]);
}

//	--------------------------------------------------------------------------------
void CvCity::GetBuyablePlotList(std::vector<int>& aiPlotList, bool bForPurchase, int nChoices)
{
	aiPlotList.clear();
	std::vector< pair<int,int> > resultList;

	CvPlot* pLoopPlot = NULL;
	CvPlot* pThisPlot = plot();
	const int iMaxRange = /*5*/ GC.getMAXIMUM_ACQUIRE_PLOT_DISTANCE();
	TeamTypes thisTeam = getTeam();

	int iPLOT_INFLUENCE_DISTANCE_MULTIPLIER =	/*100*/ GC.getPLOT_INFLUENCE_DISTANCE_MULTIPLIER();
	int iPLOT_INFLUENCE_RING_COST =				/*100*/ GC.getPLOT_INFLUENCE_RING_COST();
	int iPLOT_INFLUENCE_RESOURCE_COST =			/*-150*/ GC.getPLOT_INFLUENCE_RESOURCE_COST();
	int iPLOT_INFLUENCE_NW_COST =				/*-500*/ GC.getPLOT_INFLUENCE_NW_COST();
	int iPLOT_INFLUENCE_WATER_COST =			/* 20*/ GC.getPLOT_INFLUENCE_WATER_COST();
	int iPLOT_INFLUENCE_YIELD_POINT_COST =		/*-10*/	GC.getPLOT_INFLUENCE_YIELD_POINT_COST();
	int iPLOT_INFLUENCE_NO_ADJACENT_OWNED_COST = /*1000*/ GC.getPLOT_INFLUENCE_NO_ADJACENT_OWNED_COST();

	int iYieldLoop;
	int iDirectionLoop;
	bool bFoundAdjacentOwnedByCity;

	SPathFinderUserData data(NO_PLAYER, PT_CITY_INFLUENCE, iMaxRange);
	ReachablePlots influencePlots = GC.GetStepFinder().GetPlotsInReach( pThisPlot, data );

	int iWorkPlotDistance = getWorkPlotDistance();
	ImprovementTypes eBarbCamptype = (ImprovementTypes)GC.getBARBARIAN_CAMP_IMPROVEMENT();

	for (int iDX = -iMaxRange; iDX <= iMaxRange; iDX++)
	{
		for (int iDY = -iMaxRange; iDY <= iMaxRange; iDY++)
		{
			pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iMaxRange);
			if (pLoopPlot != NULL)
			{
				if (pLoopPlot->getOwner() != NO_PLAYER)
				{
#if defined(MOD_BALANCE_CORE)
					if(MOD_BALANCE_CORE && GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles() && bForPurchase)
					{
						if(pLoopPlot->getOwner() == getOwner() || pLoopPlot->isCity())
						{
							continue;
						}
					}
					else
					{
#endif
					continue;
#if defined(MOD_BALANCE_CORE)
					}
#endif
				}
#if defined(MOD_BALANCE_CORE)
				//Let's rule out getting plots for which we lack an adjacent owned plot.
				bool bNoNeighbor = true;
				bool bPromiseNeighbor = false;
				for (int iI = 0; iI < NUM_DIRECTION_TYPES; ++iI)
				{
					CvPlot* pAdjacentPlot = plotDirection(pLoopPlot->getX(), pLoopPlot->getY(), ((DirectionTypes)iI));

					if (pAdjacentPlot != NULL)
					{
						if(pAdjacentPlot->getOwner() == getOwner() && pAdjacentPlot->getWorkingCityID()==GetID())
						{
							bNoNeighbor = false;
							break;
						}
						if(bForPurchase && pAdjacentPlot->getOwner() != NO_PLAYER && !GET_PLAYER(pAdjacentPlot->getOwner()).isMinorCiv())
						{
							if(GET_PLAYER(pAdjacentPlot->getOwner()).GetDiplomacyAI()->GetPlayerMadeBorderPromise(getOwner()))
							{
								bPromiseNeighbor = true;
								break;
							}
						}
					}
				}
				if(bNoNeighbor)
				{
					continue;
				}
				if(bPromiseNeighbor)
				{
					continue;
				}
#endif
#if defined(MOD_EVENTS_CITY_BORDERS)
				// This can be used to implement a 12-mile limit
				if (MOD_EVENTS_CITY_BORDERS) {
					if (GAMEEVENTINVOKE_TESTALL(GAMEEVENT_CityCanAcquirePlot, getOwner(), GetID(), pLoopPlot->getX(), pLoopPlot->getY()) == GAMEEVENTRETURN_FALSE) {
						continue;
					}
				} else {
#endif				
				ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
				if (pkScriptSystem) 
				{
					CvLuaArgsHandle args;
					args->Push(getOwner());
					args->Push(GetID());
					args->Push(pLoopPlot->getX());
					args->Push(pLoopPlot->getY());

					bool bResult = false;
					if (LuaSupport::CallTestAll(pkScriptSystem, "CityCanAcquirePlot", args.get(), bResult))
					{
						if (bResult == false) {
							continue;
						}
					}
				}
#if defined(MOD_EVENTS_CITY_BORDERS)
				}
#endif			

				ReachablePlots::iterator it = influencePlots.find( pLoopPlot->GetPlotIndex() );
				int iInfluenceCost = ( it != influencePlots.end() ) ? it->iNormalizedDistance : -1;

				if (iInfluenceCost >= 0)
				{
					iInfluenceCost *= iPLOT_INFLUENCE_DISTANCE_MULTIPLIER;

					// Resource Plots claimed first
					ResourceTypes eResource = pLoopPlot->getResourceType(thisTeam);
					if (eResource != NO_RESOURCE)
					{
#if defined(MOD_BALANCE_CORE)
						CvResourceInfo *pkResource = GC.getResourceInfo(eResource);
						if (pkResource)
						{
							if (pkResource->getResourceUsage() == RESOURCEUSAGE_LUXURY || pkResource->getResourceUsage() == RESOURCEUSAGE_STRATEGIC)
								iInfluenceCost += iPLOT_INFLUENCE_RESOURCE_COST;
							else if (plotDistance(pLoopPlot->getX(),pLoopPlot->getY(),getX(),getY()) <= iWorkPlotDistance)
								//bonus resources are meh, even if they are in range
								iInfluenceCost += iPLOT_INFLUENCE_RESOURCE_COST/2;
						}
#else
						iInfluenceCost += iPLOT_INFLUENCE_RESOURCE_COST;
						bool bBonusResource = GC.getResourceInfo(eResource)->getResourceUsage() == RESOURCEUSAGE_BONUS;
						if (bBonusResource)
						{
							if (plotDistance(pLoopPlot->getX(),pLoopPlot->getY(),getX(),getY()) > iWorkPlotDistance)
							{
								// undo the bonus - we can't work this tile from this city
								iInfluenceCost -= iPLOT_INFLUENCE_RESOURCE_COST;
							}
							else
							{
								// very slightly decrease value of bonus resources
								++iInfluenceCost;
							}
						}
#endif
					}
					else 
					{

						// Water Plots claimed later
#if defined(MOD_BALANCE_CORE)
						if (pLoopPlot->isWater() && !pLoopPlot->isLake())
#else
						if (pLoopPlot->isWater())
#endif
						{
							iInfluenceCost += iPLOT_INFLUENCE_WATER_COST;
						}

						// if we can't work this tile in this city make it much less likely to be picked
						if (plotDistance(pLoopPlot->getX(),pLoopPlot->getY(),getX(),getY()) > iWorkPlotDistance)
						{
							iInfluenceCost += iPLOT_INFLUENCE_RING_COST;
						}

					}

					// avoid barbarian camps
					ImprovementTypes thisImprovement = pLoopPlot->getImprovementType();
					if (thisImprovement == eBarbCamptype)
					{
						iInfluenceCost += iPLOT_INFLUENCE_RING_COST;
					}

					// while we're at it grab Natural Wonders quickly also
#if defined(MOD_PSEUDO_NATURAL_WONDER)
					if (pLoopPlot->IsNaturalWonder(true))
#else
					if (pLoopPlot->IsNaturalWonder())
#endif
					{
						iInfluenceCost += iPLOT_INFLUENCE_NW_COST;
					}

					// More Yield == more desirable
					for (iYieldLoop = 0; iYieldLoop < NUM_YIELD_TYPES; iYieldLoop++)
					{
						iInfluenceCost += (iPLOT_INFLUENCE_YIELD_POINT_COST * pLoopPlot->getYield((YieldTypes) iYieldLoop));
					}

					// all other things being equal move towards unclaimed resources
					bool bUnownedNaturalWonderAdjacentCount = false;
					for (int iI = 0; iI < NUM_DIRECTION_TYPES; ++iI)
					{
						CvPlot* pAdjacentPlot = plotDirection(pLoopPlot->getX(), pLoopPlot->getY(), ((DirectionTypes)iI));

						if (pAdjacentPlot != NULL)
						{
							if (pAdjacentPlot->getOwner() == NO_PLAYER)
							{
								int iPlotDistance = plotDistance(getX(), getY(), pAdjacentPlot->getX(), pAdjacentPlot->getY());
								ResourceTypes eAdjacentResource = pAdjacentPlot->getResourceType(thisTeam);
								if (eAdjacentResource != NO_RESOURCE)
								{
									// if we are close enough to work, or this is not a bonus resource
									if (iPlotDistance <= iWorkPlotDistance || GC.getResourceInfo(eAdjacentResource)->getResourceUsage() != RESOURCEUSAGE_BONUS)
									{
										--iInfluenceCost;
									}
								}
#if defined(MOD_PSEUDO_NATURAL_WONDER)
								if (pAdjacentPlot->IsNaturalWonder(true))
#else
								if (pAdjacentPlot->IsNaturalWonder())
#endif
								{
									if (iPlotDistance <= iWorkPlotDistance) // grab for this city
									{
										bUnownedNaturalWonderAdjacentCount = true;
									}
									--iInfluenceCost; // but we will slightly grow towards it for style in any case
								}
							}
						}
					}

					// move towards unclaimed NW
					iInfluenceCost += bUnownedNaturalWonderAdjacentCount ? -1 : 0;

					// Plots not adjacent to another Plot acquired by this City are pretty much impossible to get
					bFoundAdjacentOwnedByCity = false;
					for (iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; iDirectionLoop++)
					{
						CvPlot* pAdjacentPlot = plotDirection(pLoopPlot->getX(), pLoopPlot->getY(), (DirectionTypes) iDirectionLoop);

						if (pAdjacentPlot != NULL)
						{
							// Have to check plot ownership first because the City IDs match between different players!!!
							if (pAdjacentPlot->getOwner() == getOwner() && pAdjacentPlot->GetCityPurchaseID() == GetID())
							{
								bFoundAdjacentOwnedByCity = true;
								break;
							}
						}
					}

					if (!bFoundAdjacentOwnedByCity)
					{
						iInfluenceCost += iPLOT_INFLUENCE_NO_ADJACENT_OWNED_COST;
					}

					resultList.push_back( std::make_pair(iInfluenceCost,pLoopPlot->GetPlotIndex()) );
				}
			}
		}
	}

	//we want only the best
	std::sort( resultList.begin(), resultList.end() );
	if (resultList.size()>(size_t)nChoices)
		resultList.erase( resultList.begin()+nChoices, resultList.end() );

	//throw away the cost, return the plot index only
	for (size_t i=0; i<resultList.size(); i++)
		//if there's a clear favorite, don't bother with the rest
		if (resultList[i].first - resultList[0].first <= 100) 
			aiPlotList.push_back( resultList[i].second );
}

//	--------------------------------------------------------------------------------
/// How much will purchasing this plot cost -- (-1,-1) will return the generic price
int CvCity::GetBuyPlotCost(int iPlotX, int iPlotY) const
{
	VALIDATE_OBJECT
	if(iPlotX == -1 && iPlotY == -1)
	{
		return GET_PLAYER(getOwner()).GetBuyPlotCost();
	}

	CvPlot* pPlot = GC.getMap().plot(iPlotX, iPlotY);
	if(!pPlot)
	{
		return -1;
	}

	// Base cost
	int iCost = GET_PLAYER(getOwner()).GetBuyPlotCost();

	// Influence cost factor (e.g. Hills are more expensive than flat land)
	CvMap& thisMap = GC.getMap();
	CvPlot* pThisPlot = plot();

	const int iMaxRange = getBuyPlotDistance();
	if(plotDistance(iPlotX, iPlotY, getX(), getY()) > iMaxRange)
		return 9999; // Critical hit!

	int iPLOT_INFLUENCE_BASE_MULTIPLIER = /*100*/ GC.getPLOT_INFLUENCE_BASE_MULTIPLIER();
	int iPLOT_INFLUENCE_DISTANCE_MULTIPLIER = /*100*/ GC.getPLOT_INFLUENCE_DISTANCE_MULTIPLIER();
	int iPLOT_INFLUENCE_DISTANCE_DIVISOR = /*3*/ GC.getPLOT_INFLUENCE_DISTANCE_DIVISOR();
	int iPLOT_BUY_RESOURCE_COST = /*-100*/ GC.getPLOT_BUY_RESOURCE_COST();

	int iDistance = thisMap.calculateInfluenceDistance(pThisPlot, pPlot, iMaxRange);
	int iRefDistance = GetCheapestPlotInfluenceDistance();
	if (iRefDistance==INT_MAX)
		iRefDistance = 0;

	int iInfluenceCostFactor = iPLOT_INFLUENCE_BASE_MULTIPLIER;
	iInfluenceCostFactor += (iDistance-iRefDistance) * iPLOT_INFLUENCE_DISTANCE_MULTIPLIER / iPLOT_INFLUENCE_DISTANCE_DIVISOR;
	if(pPlot->getResourceType(getTeam()) != NO_RESOURCE)
		iInfluenceCostFactor += iPLOT_BUY_RESOURCE_COST;

	if(iInfluenceCostFactor > 100)
	{
		iCost *= iInfluenceCostFactor;
		iCost /= 100;
	}

#if defined(MOD_UI_CITY_EXPANSION)
	if (MOD_UI_CITY_EXPANSION && GET_PLAYER(getOwner()).isHuman()) {
		// If we have a culture surplus, we get a discount on the tile
		if (GetJONSCultureStored() >= GetJONSCultureThreshold()) {
			iCost -= GET_PLAYER(getOwner()).GetBuyPlotCost();
		}
	}
#endif

#if defined(MOD_BALANCE_CORE)
	//Owned by someone? Much more expensive!
	if(MOD_BALANCE_CORE && GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles())
	{
		if((pPlot->getOwner() != NO_PLAYER) && (pPlot->getOwner() != getOwner()))
		{
			iCost *= 2;
		}
	}
#endif

	// Discount for adjacent plots owned by us
	int iAdjacentOwnedCount = 0;
	CvPlot** aNeighbors = GC.getMap().getNeighborsUnchecked(pPlot);
	for (int i = 0; i < 6; i++)
	{
		CvPlot* pNeighbor = aNeighbors[i];
		if (pNeighbor && pNeighbor->getOwner()==getOwner())
			iAdjacentOwnedCount++;
	}
	iCost = iCost * (105 - iAdjacentOwnedCount*5); //we know that one is always owned
	iCost /= 100;

	// Game Speed Mod
	iCost *= GC.getGame().getGameSpeedInfo().getGoldPercent();
	iCost /= 100;

	iCost *= (100 + getPlotBuyCostModifier());
	iCost /= 100;

	// Now round so the number looks neat
	int iDivisor = /*5*/ max(1,GC.getPLOT_COST_APPEARANCE_DIVISOR());
	iCost = (iCost+iDivisor/2) / iDivisor;
	iCost *= iDivisor;

	return iCost;
}

//	--------------------------------------------------------------------------------
/// Buy the plot and set it's owner to us (executed by the network code)
void CvCity::BuyPlot(int iPlotX, int iPlotY)
{
	VALIDATE_OBJECT
	CvPlot* pPlot = GC.getMap().plot(iPlotX, iPlotY);
	if(!pPlot)
	{
		return;
	}
#if defined(MOD_BALANCE_CORE)
	if(getOwner() == NO_PLAYER)
	{
		return;
	}
#endif
	int iCost = GetBuyPlotCost(iPlotX, iPlotY);
	CvPlayer& thisPlayer = GET_PLAYER(getOwner());
	thisPlayer.GetTreasury()->LogExpenditure("", iCost, 1);
	thisPlayer.GetTreasury()->ChangeGold(-iCost);
#if defined(MOD_UI_CITY_EXPANSION)
	bool bWithGold = true;
	if (MOD_UI_CITY_EXPANSION && GET_PLAYER(getOwner()).isHuman()) {
		// If we have a culture surplus, we got a discount on the tile, so remove the surplus
		int iOverflow = GetJONSCultureStored() - GetJONSCultureThreshold();
		if (iOverflow >= 0) {
			SetJONSCultureStored(iOverflow);
			ChangeJONSCultureLevel(1);
			bWithGold = false;
		}
	}
#endif
#if defined(MOD_BALANCE_CORE)
	if (iCost > 0) 
	{
		//Did we buy this plot from someone? Oh no!
		if(pPlot != NULL && pPlot->getOwner() != NO_PLAYER && pPlot->getOwner() != getOwner())
		{
			if(!GET_PLAYER(pPlot->getOwner()).isHuman())
			{
				if(GET_PLAYER(pPlot->getOwner()).isMajorCiv())
				{
					GET_PLAYER(pPlot->getOwner()).GetDiplomacyAI()->ChangeNumTimesCultureBombed(getOwner(), 1);
				}
				else if(GET_PLAYER(pPlot->getOwner()).isMinorCiv())
				{
					GET_PLAYER(pPlot->getOwner()).GetMinorCivAI()->ChangeFriendshipWithMajor(getOwner(), -20);
				}
			}
		}
	}
#endif
#if defined(MOD_UI_CITY_EXPANSION)
	if (iCost > 0) {
		// Only do this if we actually paid for the plot (as opposed to getting it for free via city growth)
#endif
		thisPlayer.ChangeNumPlotsBought(1);

		// See if there's anyone else nearby that could get upset by this action
		CvCity* pNearbyCity;
#if defined(MOD_BALANCE_CORE)
		//Let's look at max range for plot purchases for this City.
		//Buying plots further and further from your city will make this more likely to trigger bad diplo.
		CvPlot* pLoopPlot = NULL;

		for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
		{
			pLoopPlot = iterateRingPlots(iPlotX, iPlotY, iI);

			if(pLoopPlot != NULL)
			{
				pNearbyCity = pLoopPlot->getPlotCity();

				if(pNearbyCity)
				{
					PlayerTypes ePlayer = pNearbyCity->getOwner();
					//We found another player? Good.
					if(ePlayer != NO_PLAYER && !GET_PLAYER(ePlayer).isMinorCiv() && ePlayer != getOwner())
					{
						//Resource? Grr!
						if(pPlot->getResourceType(GET_PLAYER(ePlayer).getTeam()) != NO_RESOURCE)
						{
							pNearbyCity->AI_ChangeNumPlotsAcquiredByOtherPlayer(getOwner(), 1);
							break;
						}
						//Natural Wonder? Grr!!!!
#if defined(MOD_PSEUDO_NATURAL_WONDER)
						if(pPlot->IsNaturalWonder(true))
#else
						if(pPlot->IsNaturalWonder())
#endif
						{
							pNearbyCity->AI_ChangeNumPlotsAcquiredByOtherPlayer(getOwner(), 3);
							break;
						}
						//Neighbors? Grr!
						int iUsOwned = 0;
						int iThemOwned = 0;
						CvPlot* pAdjacentPlot;
						for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; iDirectionLoop++)
						{
							pAdjacentPlot = plotDirection(pPlot->getX(), pPlot->getY(), ((DirectionTypes)iDirectionLoop));

							if(pAdjacentPlot != NULL)
							{
								if(pAdjacentPlot->getOwner() == ePlayer)
								{
									iThemOwned++;
								}
								if(pAdjacentPlot->getOwner() == getOwner())
								{
									iUsOwned;
								}
							}
						}
						//We're buying land near their claimed tiles? Grr!
						if(iThemOwned > iUsOwned)
						{
							pNearbyCity->AI_ChangeNumPlotsAcquiredByOtherPlayer(getOwner(), 2);
							break;
						}
						//We're competing? Grr!
						else if(iThemOwned >= iUsOwned)
						{
							pNearbyCity->AI_ChangeNumPlotsAcquiredByOtherPlayer(getOwner(), 1);
							break;
						}
					}
				}
			}
		}
#else
		for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
		{
			pPlot = iterateRingPlots(iPlotX, iPlotY, iI);

			if(pPlot != NULL)
			{
				pNearbyCity = pPlot->getPlotCity();

				if(pNearbyCity)
				{
					if(pNearbyCity->getOwner() != getOwner())
					{

						pNearbyCity->AI_ChangeNumPlotsAcquiredByOtherPlayer(getOwner(), 1);
					}
				}
			}
		}
#endif
#if defined(MOD_UI_CITY_EXPANSION)
	}
#endif
#if defined(MOD_BALANCE_CORE)
	GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_TILE_PURCHASE, true, NO_GREATPERSON, NO_BUILDING, 0, true, NO_PLAYER, NULL, false, this);
#endif
	if(GC.getLogging() && GC.getAILogging())
	{
		CvPlayerAI& kOwner = GET_PLAYER(getOwner());
		CvString playerName;
		FILogFile* pLog;
		CvString strBaseString;
		CvString strOutBuf;
		playerName = kOwner.getCivilizationShortDescription();
		pLog = LOGFILEMGR.GetLog(kOwner.GetCitySpecializationAI()->GetLogFileName(playerName), FILogFile::kDontTimeStamp);
		strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
		strBaseString += playerName + ", ";
		strOutBuf.Format("%s, City Plot Purchased, X: %d, Y: %d", getName().GetCString(), iPlotX, iPlotY);
		strBaseString += strOutBuf;
		pLog->Msg(strBaseString);
	}
	DoAcquirePlot(iPlotX, iPlotY);
		
#if defined(MOD_EVENTS_CITY)
	if (MOD_EVENTS_CITY) {
#if defined(MOD_UI_CITY_EXPANSION)
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityBoughtPlot, getOwner(), GetID(), iPlotX, iPlotY, bWithGold, !bWithGold);
#else
		GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityBoughtPlot, getOwner(), GetID(), iPlotX, iPlotY, true, false);
#endif
	} else {
#endif
	ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
	if (pkScriptSystem) 
	{
		CvLuaArgsHandle args;
		args->Push(getOwner());
		args->Push(GetID());
#if defined(MOD_BUGFIX_MINOR)
		args->Push(iPlotX);
		args->Push(iPlotY);
#else
		args->Push(plot()->getX());
		args->Push(plot()->getY());
#endif
		args->Push(true); // bGold
		args->Push(false); // bFaith/bCulture

		bool bResult;
		LuaSupport::CallHook(pkScriptSystem, "CityBoughtPlot", args.get(), bResult);
	}
#if defined(MOD_EVENTS_CITY)
	}
#endif

#if !defined(NO_ACHIEVEMENTS)
	//Achievement test for purchasing 1000 tiles
	if(thisPlayer.isHuman() && !GC.getGame().isGameMultiPlayer())
	{
		gDLL->IncrementSteamStatAndUnlock(ESTEAMSTAT_TILESPURCHASED, 1000, ACHIEVEMENT_PURCHASE_1000TILES);
	}
#endif
}

//	--------------------------------------------------------------------------------
/// Acquire the plot and set it's owner to us
void CvCity::DoAcquirePlot(int iPlotX, int iPlotY)
{
	VALIDATE_OBJECT
	CvPlot* pPlot = GC.getMap().plot(iPlotX, iPlotY);
	if(!pPlot)
	{
		return;
	}
	GET_PLAYER(getOwner()).AddAPlot(pPlot);

#if defined(MOD_BALANCE_CORE)
	if(pPlot->getOwner() != getOwner() && pPlot->getOwner() != NO_PLAYER && GET_PLAYER(pPlot->getOwner()).isHuman())
	{
		CvNotifications* pNotifications = GET_PLAYER(pPlot->getOwner()).GetNotifications();
		if(pNotifications)
		{
			CvString strBuffer = GetLocalizedText("TXT_KEY_NOTIFICATION_UA_STOLE_PLOT", GET_PLAYER(getOwner()).getNameKey());
			CvString strSummary = GetLocalizedText("TXT_KEY_NOTIFICATION_SUMMARY_UA_STOLE_PLOT", GET_PLAYER(getOwner()).getNameKey());
			pNotifications->Add(NOTIFICATION_GENERIC, strBuffer, strSummary, pPlot->getX(), pPlot->getY(), -1);
		}
	}
#endif
	pPlot->setOwner(getOwner(), GetID(), /*bCheckUnits*/ true, /*bUpdateResources*/ true);

	DoUpdateCheapestPlotInfluenceDistance();
}

//	--------------------------------------------------------------------------------
/// Compute how valuable buying a plot is to this city
int CvCity::GetBuyPlotScore(int& iBestX, int& iBestY)
{
	VALIDATE_OBJECT
	CvPlot* pLoopPlot = NULL;

	const int iMaxRange = getBuyPlotDistance();
	int iBestScore = -1;
	int iTempScore;

	int iDX, iDY;

	for(iDX = -iMaxRange; iDX <= iMaxRange; iDX++)
	{
		for(iDY = -iMaxRange; iDY <= iMaxRange; iDY++)
		{
			pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iMaxRange);
			if(pLoopPlot != NULL)
			{
				// Can we actually buy this plot?
				if(CanBuyPlot(pLoopPlot->getX(), pLoopPlot->getY(), true))
				{
					iTempScore = GetIndividualPlotScore(pLoopPlot);

					if(iTempScore > iBestScore)
					{
						iBestScore = iTempScore;
						iBestX = pLoopPlot->getX();
						iBestY = pLoopPlot->getY();
					}
				}
			}
		}
	}

	return iBestScore;
}

//	--------------------------------------------------------------------------------
/// Compute value of a plot we might buy
int CvCity::GetIndividualPlotScore(const CvPlot* pPlot) const
{
	VALIDATE_OBJECT
	int iRtnValue = 0;
	ResourceTypes eResource;
	int iYield;
	int iI;
	YieldTypes eSpecializationYield = NO_YIELD;
	CitySpecializationTypes eSpecialization;
	CvCity* pCity;

	eSpecialization = GetCityStrategyAI()->GetSpecialization();

	if(eSpecialization != NO_CITY_SPECIALIZATION)
	{
		eSpecializationYield = GC.getCitySpecializationInfo(eSpecialization)->GetYieldType();
	}

	// Does it have a resource?
	eResource = pPlot->getResourceType(getTeam());
	if(eResource != NO_RESOURCE)
	{
		CvResourceInfo *pkResource = GC.getResourceInfo(eResource);
		if (pkResource)
		{
			if(GET_TEAM(getTeam()).GetTeamTechs()->HasTech((TechTypes)pkResource->getTechReveal()))
			{
				int iRevealPolicy = pkResource->getPolicyReveal();
				if (iRevealPolicy == NO_POLICY || GET_PLAYER(getOwner()).GetPlayerPolicies()->HasPolicy((PolicyTypes)iRevealPolicy))
				{
					ResourceUsageTypes eResourceUsage = GC.getResourceInfo(eResource)->getResourceUsage();
					if(eResourceUsage == RESOURCEUSAGE_STRATEGIC)
					{
						iRtnValue += /* 50 */ GC.getAI_PLOT_VALUE_STRATEGIC_RESOURCE();
					}

					// Luxury resource?
					else if(eResourceUsage == RESOURCEUSAGE_LUXURY)
					{
						int iLuxuryValue = /* 40 */ GC.getAI_PLOT_VALUE_LUXURY_RESOURCE();

						// Luxury we don't have yet?
						if(GET_PLAYER(getOwner()).getNumResourceTotal(eResource) == 0)
							iLuxuryValue *= 2;

						iRtnValue += iLuxuryValue;
					}
				}
			}
		}
	}

	int iYieldValue = 0;
	int iTempValue;

	YieldTypes eYield;

	CvCityStrategyAI* pCityStrategyAI = GetCityStrategyAI();

	// Valuate the yields from this plot
	for(iI = 0; iI < NUM_YIELD_TYPES; iI++)
	{
		eYield = (YieldTypes) iI;

		iYield = pPlot->calculateNatureYield(eYield, getOwner());

		iTempValue = 0;

		if(eYield == eSpecializationYield)
			iTempValue += iYield* /*20*/ GC.getAI_PLOT_VALUE_SPECIALIZATION_MULTIPLIER();

		else
			iTempValue += iYield* /*10*/ GC.getAI_PLOT_VALUE_YIELD_MULTIPLIER();

		// Deficient? If so, give it a boost
		if(pCityStrategyAI->GetMostDeficientYield() == eYield)
			iTempValue *= /*5*/ GC.getAI_PLOT_VALUE_DEFICIENT_YIELD_MULTIPLIER();

		iYieldValue += iTempValue;
	}

	iRtnValue += iYieldValue;

	// For each player not on our team, check how close their nearest city is to this plot
	CvPlayer& owningPlayer = GET_PLAYER(m_eOwner);
	CvDiplomacyAI* owningPlayerDiploAI = owningPlayer.GetDiplomacyAI();
	for(iI = 0; iI < MAX_MAJOR_CIVS; iI++)
	{
		CvPlayer& loopPlayer = GET_PLAYER((PlayerTypes)iI);
		if(loopPlayer.isAlive())
		{
			if(loopPlayer.getTeam() != getTeam())
			{
				DisputeLevelTypes eLandDisputeLevel = owningPlayerDiploAI->GetLandDisputeLevel((PlayerTypes)iI);

				if(eLandDisputeLevel != NO_DISPUTE_LEVEL && eLandDisputeLevel != DISPUTE_LEVEL_NONE)
				{
					pCity = GC.getMap().findCity(pPlot->getX(), pPlot->getY(), (PlayerTypes)iI, NO_TEAM, true /*bSameArea */);

					if(pCity)
					{
						int iDistance = plotDistance(pPlot->getX(), pPlot->getY(), pCity->getX(), pCity->getY());

						// Only want to account for civs with a city within 10 tiles
						if(iDistance < 10)
						{
							switch(eLandDisputeLevel)
							{
							case DISPUTE_LEVEL_FIERCE:
								iRtnValue += (10 - iDistance) * /* 6 */ GC.getAI_PLOT_VALUE_FIERCE_DISPUTE();
#if defined(MOD_BALANCE_CORE)
								if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles() && pPlot->getOwner() == loopPlayer.GetID())
								{
									iRtnValue *= 100;
								}
#endif
								break;
							case DISPUTE_LEVEL_STRONG:
								iRtnValue += (10 - iDistance) * /* 4 */GC.getAI_PLOT_VALUE_STRONG_DISPUTE();
#if defined(MOD_BALANCE_CORE)
								if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles() && pPlot->getOwner() == loopPlayer.GetID())
								{
									iRtnValue *= 50;
								}
#endif
								break;
							case DISPUTE_LEVEL_WEAK:
								iRtnValue += (10 - iDistance) * /* 2 */ GC.getAI_PLOT_VALUE_WEAK_DISPUTE();
#if defined(MOD_BALANCE_CORE)
								if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsBuyOwnedTiles() && pPlot->getOwner() == loopPlayer.GetID())
								{
									iRtnValue *= 10;
								}
#endif
								break;
							}
						}
					}
				}
			}
		}
	}
	// Modify value based on cost - the higher it is compared to the "base" cost the less the value
	int iCost = GetBuyPlotCost(pPlot->getX(), pPlot->getY());

	iRtnValue *= GET_PLAYER(getOwner()).GetBuyPlotCost();

	// Protect against div by 0.
	CvAssertMsg(iCost != 0, "Plot cost is 0");
	if(iCost != 0)
		iRtnValue /= iCost;
	else
		iRtnValue = 0;

	return iRtnValue;
}

//	--------------------------------------------------------------------------------
/// What is the cheapest plot we can get
int CvCity::GetCheapestPlotInfluenceDistance() const
{
	return m_iCheapestPlotInfluenceDistance;
}

//	--------------------------------------------------------------------------------
/// What is the cheapest plot we can get
void CvCity::SetCheapestPlotInfluenceDistance(int iValue)
{
	if(m_iCheapestPlotInfluenceDistance != iValue)
		m_iCheapestPlotInfluenceDistance = iValue;

	CvAssertMsg(m_iCheapestPlotInfluenceDistance > 0, "Cheapest plot influence should never be 0 or less.");
}

//	--------------------------------------------------------------------------------
/// What is the cheapest plot we can get
void CvCity::DoUpdateCheapestPlotInfluenceDistance()
{
	vector<int> plots;
	GetBuyablePlotList( plots, false );

	if (!plots.empty())
	{
		int iRefDistance = GC.getMap().calculateInfluenceDistance( 
			plot(), GC.getMap().plotByIndex(plots.front()), getBuyPlotDistance() );

		SetCheapestPlotInfluenceDistance( iRefDistance);
	}
	else
		SetCheapestPlotInfluenceDistance(INT_MAX);
}

//	--------------------------------------------------------------------------------
/// Setting the danger value threat amount
void CvCity::SetThreatValue(int iThreatValue)
{
	VALIDATE_OBJECT
	m_iThreatValue = iThreatValue;
}

//	--------------------------------------------------------------------------------
/// Getting the danger value threat amount
int CvCity::getThreatValue(void)
{
	VALIDATE_OBJECT
	return m_iThreatValue;
}

//	--------------------------------------------------------------------------------
void CvCity::clearOrderQueue()
{
	VALIDATE_OBJECT
	while(headOrderQueueNode() != NULL)
	{
		popOrder(0);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::pushOrder(OrderTypes eOrder, int iData1, int iData2, bool bSave, bool bPop, bool bAppend, bool bRush)
{
	VALIDATE_OBJECT
	OrderData order;
	bool bValid;

	if(bPop)
	{
		clearOrderQueue();
	}

	bValid = false;

	switch(eOrder)
	{
	case ORDER_TRAIN:
		if(canTrain((UnitTypes)iData1))
		{
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo((UnitTypes)iData1);
			if(pkUnitInfo)
			{
				if(iData2 == -1)
				{
					iData2 = pkUnitInfo->GetDefaultUnitAIType();
				}

				GET_PLAYER(getOwner()).changeUnitClassMaking(((UnitClassTypes)(pkUnitInfo->GetUnitClassType())), 1);

				bValid = true;
			}
		}
		break;

	case ORDER_CONSTRUCT:
		if(canConstruct((BuildingTypes)iData1))
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo((BuildingTypes)iData1);
			if(pkBuildingInfo)
			{
				GET_PLAYER(getOwner()).changeBuildingClassMaking(((BuildingClassTypes)(pkBuildingInfo->GetBuildingClassType())), 1);
#if defined(MOD_BALANCE_CORE)
				//If other civs can see this city, and that this city is building this, that should reduce our desire to start it.
				if (::isWorldWonderClass(pkBuildingInfo->GetBuildingClassInfo()))
				{
					for (int iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
					{
						PlayerTypes eLoopPlayer = (PlayerTypes)iPlayerLoop;
						if (GET_PLAYER(eLoopPlayer).isAlive() && GET_TEAM(GET_PLAYER(eLoopPlayer).getTeam()).isHasMet(getTeam()))
						{
							if (plot()->isVisible(GET_PLAYER(eLoopPlayer).getTeam()))
							{
								GET_PLAYER(eLoopPlayer).ChangeNumCivsConstructingWonder((BuildingTypes)iData1, 1);
							}
						}
					}
				}
#endif
				bValid = true;
			}
		}
		break;

	case ORDER_CREATE:
		if(canCreate((ProjectTypes)iData1))
		{
			GET_TEAM(getTeam()).changeProjectMaking(((ProjectTypes)iData1), 1);
			GET_PLAYER(getOwner()).changeProjectMaking(((ProjectTypes)iData1), 1);

			bValid = true;
		}
		break;

	case ORDER_PREPARE:
		if(canPrepare((SpecialistTypes)iData1))
		{
			bValid = true;
		}
		break;

	case ORDER_MAINTAIN:
		if(canMaintain((ProcessTypes)iData1))
		{
			bValid = true;
		}
		break;

	default:
		CvAssertMsg(false, "iOrder did not match a valid option");
		break;
	}

	if(!bValid)
	{
		return;
	}

	order.eOrderType = eOrder;
	order.iData1 = iData1;
	order.iData2 = iData2;
	order.bSave = bSave;
	order.bRush = bRush;

	if(bAppend)
	{
		m_orderQueue.insertAtEnd(&order);
	}
	else
	{
		stopHeadOrder();
		m_orderQueue.insertAtBeginning(&order);
	}

	if(!bAppend || (getOrderQueueLength() == 1))
	{
		startHeadOrder();
	}

	if((getTeam() == GC.getGame().getActiveTeam()) || GC.getGame().isDebugMode())
	{
		if(isCitySelected())
		{
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			DLLUI->setDirty(PlotListButtons_DIRTY_BIT, true);
		}
	}

	DLLUI->setDirty(CityInfo_DIRTY_BIT, true);

	auto_ptr<ICvCity1> pCity = GC.WrapCityPointer(this);
	DLLUI->SetSpecificCityInfoDirty(pCity.get(), CITY_UPDATE_TYPE_PRODUCTION);
}


//	--------------------------------------------------------------------------------
void CvCity::popOrder(int iNum, bool bFinish, bool bChoose)
{
	VALIDATE_OBJECT

	CvPlayerAI& kOwner = GET_PLAYER(getOwner());	//Used often later on

	OrderData* pOrderNode;
	SpecialistTypes eSpecialist;
	ProjectTypes eCreateProject;
	BuildingTypes eConstructBuilding;
	UnitTypes eTrainUnit;
	UnitAITypes eTrainAIUnit;
	bool bWasFoodProduction;
	bool bStart;
	bool bMessage;
	int iCount;
	int iProductionNeeded;

	bWasFoodProduction = isFoodProduction();

	if(iNum == -1)
	{
		iNum = (getOrderQueueLength() - 1);
	}

	iCount = 0;

	pOrderNode = headOrderQueueNode();

	while(pOrderNode != NULL)
	{
		if(iCount == iNum)
		{
			break;
		}

		iCount++;

		pOrderNode = nextOrderQueueNode(pOrderNode);
	}

	if(pOrderNode == NULL)
	{
		return;
	}

	if(bFinish)
	{
		m_iThingsProduced++;
	}

	if(bFinish && pOrderNode->bSave)
	{
		pushOrder(pOrderNode->eOrderType, pOrderNode->iData1, pOrderNode->iData2, true, false, true);
	}

	eTrainUnit = NO_UNIT;
	eConstructBuilding = NO_BUILDING;
	eCreateProject = NO_PROJECT;
	eSpecialist = NO_SPECIALIST;

	switch(pOrderNode->eOrderType)
	{
	case ORDER_TRAIN:
		eTrainUnit = ((UnitTypes)(pOrderNode->iData1));
		eTrainAIUnit = ((UnitAITypes)(pOrderNode->iData2));
		CvAssertMsg(eTrainUnit != NO_UNIT, "eTrainUnit is expected to be assigned a valid unit type");
		CvAssertMsg(eTrainAIUnit != NO_UNITAI, "eTrainAIUnit is expected to be assigned a valid unit AI type");

		kOwner.changeUnitClassMaking(((UnitClassTypes)(GC.getUnitInfo(eTrainUnit)->GetUnitClassType())), -1);

		if(bFinish)
		{
			int iResult = CreateUnit(eTrainUnit, eTrainAIUnit);
			if(iResult != -1)
			{
#if defined(MOD_BALANCE_CORE)
				CvUnit* pUnit = GET_PLAYER(getOwner()).getUnit(iResult);
				if(pUnit && pUnit->isFreeUpgrade() || GET_PLAYER(getOwner()).GetPlayerTraits()->IsFreeUpgrade())
				{
					UnitTypes eUpgradeUnit = pUnit->GetUpgradeUnitType();
					if(eUpgradeUnit != NO_UNIT && this->canTrain(eUpgradeUnit, false, false, true))
					{
						pUnit->DoUpgrade(true);
					}
				}
				if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsConquestOfTheWorld())
				{
					if(pUnit && (pUnit->isFound() || pUnit->IsFoundMid()))
					{
						UnitTypes eBestLandUnit = NO_UNIT;
						int iStrengthBestLandCombat = 0;
						for(int iI = 0; iI < GC.getNumUnitClassInfos(); iI++)
						{
							const UnitClassTypes eUnitClass = static_cast<UnitClassTypes>(iI);
							CvUnitClassInfo* pkUnitClassInfo = GC.getUnitClassInfo(eUnitClass);
							if(pkUnitClassInfo)
							{
								const UnitTypes eUnit = (UnitTypes) getCivilizationInfo().getCivilizationUnits(eUnitClass);
								CvUnitEntry* pUnitEntry = GC.getUnitInfo(eUnit);
								if(pUnitEntry)
								{
									if(!canTrain(eUnit))
									{
										continue;
									}
									if(pUnitEntry->GetRangedCombat() > 0)
									{
										continue;
									}
									if(pUnitEntry->GetDomainType() == DOMAIN_LAND)
									{
										bool bBad = false;
										ResourceTypes eResource;
										for(int iResourceLoop = 0; iResourceLoop < GC.getNumResourceInfos(); iResourceLoop++)
										{
											eResource = (ResourceTypes) iResourceLoop;
											int iNumResource = pUnitEntry->GetResourceQuantityRequirement(eResource);
											if (iNumResource > 0)
											{
												if(GET_PLAYER(getOwner()).getNumResourceAvailable(eResource, true) < iNumResource)
												{
													bBad = true;
													break;
												}
											}
										}
										if(bBad)
										{
											continue;
										}
										int iCombatLandStrength = (std::max(1, pUnitEntry->GetCombat()));
										if(iCombatLandStrength > iStrengthBestLandCombat)
										{
											iStrengthBestLandCombat = iCombatLandStrength;
											eBestLandUnit = eUnit;
										}
									}
								}
							}
						}
						if(eBestLandUnit != NO_UNIT)
						{
							CvUnitEntry* pkbUnitEntry = GC.getUnitInfo(eBestLandUnit);
							if(pkbUnitEntry)
							{
								UnitAITypes eUnitAI = pkbUnitEntry->GetDefaultUnitAIType();
								int iResult = CreateUnit(eBestLandUnit, eUnitAI);
								CvAssertMsg(iResult != -1, "Unable to create unit");
								if (iResult != -1)
								{
									CvUnit* pUnit2 = GET_PLAYER(getOwner()).getUnit(iResult);
									if (!pUnit2->jumpToNearestValidPlot())
									{
										pUnit2->kill(false);	// Could not find a valid spot!
									}
									CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
									if (pNotifications)
									{
										Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CONQUEST_OF_WORLD_UNIT");
										strText << pUnit2->getNameKey() << getNameKey();
										Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CONQUEST_OF_WORLD_UNIT");
										strSummary << getNameKey();
										pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pUnit2->getX(), pUnit2->getY(), -1);
									}
								}
							}
						}
						else
						{
							UnitTypes eWarrior = (UnitTypes)GC.getInfoTypeForString("UNIT_WARRIOR");
							CvUnitEntry* pkbUnitEntry = GC.getUnitInfo(eWarrior);
							if(pkbUnitEntry)
							{
								UnitAITypes eUnitAI = pkbUnitEntry->GetDefaultUnitAIType();
								int iResult = CreateUnit(eWarrior, eUnitAI);
								CvAssertMsg(iResult != -1, "Unable to create unit");
								if (iResult != -1)
								{
									CvUnit* pUnit2 = GET_PLAYER(getOwner()).getUnit(iResult);
									if (!pUnit2->jumpToNearestValidPlot())
									{
										pUnit2->kill(false);	// Could not find a valid spot!
									}
									CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
									if (pNotifications)
									{
										Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CONQUEST_OF_WORLD_UNIT");
										strText << pUnit2->getNameKey() << getNameKey();
										Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CONQUEST_OF_WORLD_UNIT");
										strSummary << getNameKey();
										pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), pUnit2->getX(), pUnit2->getY(), -1);
									}
								}
							}
						}
					}
				}
				if(kOwner.GetPlayerTraits()->IsFreeZuluPikemanToImpi())
				{
					UnitClassTypes ePikemanClass = (UnitClassTypes)GC.getInfoTypeForString("UNITCLASS_PIKEMAN");
					UnitTypes eZuluImpi = (UnitTypes)GC.getInfoTypeForString("UNIT_ZULU_IMPI");
					if(pUnit != NULL && pUnit->getUnitClassType() == ePikemanClass && this->canTrain(eZuluImpi, false, false, true))
					{
						CvUnitEntry* pkcUnitEntry = GC.getUnitInfo(eZuluImpi);
						if(pkcUnitEntry)
						{
							UnitAITypes eZuluImpiAI = pkcUnitEntry->GetDefaultUnitAIType();
							CvUnit* pZuluImpi = kOwner.initUnit(eZuluImpi, pUnit->getX(), pUnit->getY(), eZuluImpiAI);
							pZuluImpi->convert(pUnit, true);
						}
					}
				}
#endif
#if defined(MOD_EVENTS_CITY)
				if (MOD_EVENTS_CITY)
				{
					GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityTrained, getOwner(), GetID(), iResult, false, false);
				}
				else
				{
#endif
				ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
				if (pkScriptSystem) 
				{
					CvLuaArgsHandle args;
					args->Push(getOwner());
					args->Push(GetID());
					args->Push(GET_PLAYER(getOwner()).getUnit(iResult)->GetID()); // This is probably just iResult
					args->Push(false); // bGold
					args->Push(false); // bFaith/bCulture

					bool bResult;
					LuaSupport::CallHook(pkScriptSystem, "CityTrained", args.get(), bResult);
				}
#if defined(MOD_EVENTS_CITY)
				}
#endif

				iProductionNeeded = getProductionNeeded(eTrainUnit) * 100;
#if defined(MOD_BALANCE_CORE)
				if (!GET_PLAYER(getOwner()).getUnit(iResult)->IsCivilianUnit())
				{
					GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_U_PROD, true, NO_GREATPERSON, NO_BUILDING, (iProductionNeeded / 100), false, NO_PLAYER, NULL, false, this);
				}
#endif
				// max overflow is the value of the item produced (to eliminate prebuild exploits)
				int iOverflow = getUnitProductionTimes100(eTrainUnit) - iProductionNeeded;
				int iMaxOverflow = std::max(iProductionNeeded, getCurrentProductionDifferenceTimes100(false, false));
				int iLostProduction = std::max(0, iOverflow - iMaxOverflow);
				iOverflow = std::min(iMaxOverflow, iOverflow);
				if(iOverflow > 0)
				{
					changeOverflowProductionTimes100(iOverflow);
				}
				setUnitProduction(eTrainUnit, 0);

				int iProductionGold = ((iLostProduction * GC.getMAXED_UNIT_GOLD_PERCENT()) / 100);
				if(iProductionGold > 0)
				{
					kOwner.GetTreasury()->ChangeGoldTimes100(iProductionGold);
				}
			}
			else
			{
				// create notification
				setUnitProduction(eTrainUnit, getProductionNeeded(eTrainUnit) - 1);

				CvNotifications* pNotifications = kOwner.GetNotifications();
				if(pNotifications)
				{
					Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_REMOVED_UNIT");
					strText << getNameKey();
					strText << GC.getUnitInfo(eTrainUnit)->GetDescription();
					Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_REMOVED_UNIT");
					pNotifications->Add(NOTIFICATION_GENERIC, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), -1);
				}
			}
		}
		break;

	case ORDER_CONSTRUCT:
	{
		eConstructBuilding = ((BuildingTypes)(pOrderNode->iData1));

		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eConstructBuilding);

		if(pkBuildingInfo)
		{
			kOwner.changeBuildingClassMaking(((BuildingClassTypes)(pkBuildingInfo->GetBuildingClassType())), -1);

			if(bFinish)
			{
				bool bResult = CreateBuilding(eConstructBuilding);
				DEBUG_VARIABLE(bResult);
				CvAssertMsg(bResult, "CreateBuilding failed");

#if defined(MOD_EVENTS_CITY)
				if (MOD_EVENTS_CITY) {
					GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityConstructed, getOwner(), GetID(), eConstructBuilding, false, false);
				} else {
#endif
				ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
				if (pkScriptSystem) 
				{
					CvLuaArgsHandle args;
					args->Push(getOwner());
					args->Push(GetID());
					args->Push(eConstructBuilding);
					args->Push(false); // bGold
					args->Push(false); // bFaith/bCulture

					bool bScriptResult;
					LuaSupport::CallHook(pkScriptSystem, "CityConstructed", args.get(), bScriptResult);
				}
#if defined(MOD_EVENTS_CITY)
				}
#endif

				iProductionNeeded = getProductionNeeded(eConstructBuilding) * 100;
				// max overflow is the value of the item produced (to eliminate prebuild exploits)
				int iOverflow = m_pCityBuildings->GetBuildingProductionTimes100(eConstructBuilding) - iProductionNeeded;
				int iMaxOverflow = std::max(iProductionNeeded, getCurrentProductionDifferenceTimes100(false, false));
				int iLostProduction = std::max(0, iOverflow - iMaxOverflow);
				iOverflow = std::min(iMaxOverflow, iOverflow);
				if(iOverflow > 0)
				{
					changeOverflowProductionTimes100(iOverflow);
				}
				m_pCityBuildings->SetBuildingProduction(eConstructBuilding, 0);

				int iProductionGold = ((iLostProduction * GC.getMAXED_BUILDING_GOLD_PERCENT()) / 100);
				if(iProductionGold > 0)
				{
					kOwner.GetTreasury()->ChangeGoldTimes100(iProductionGold);
				}
				
				if(GC.getLogging() && GC.getAILogging())
				{
					CvBuildingEntry* pkConstructBuildingInfo = GC.getBuildingInfo(eConstructBuilding);
					if(pkConstructBuildingInfo)
					{
						if(kOwner.GetWonderProductionAI()->IsWonder(*pkConstructBuildingInfo))
						{
							CvString playerName;
							FILogFile* pLog;
							CvString strBaseString;
							CvString strOutBuf;
							playerName = kOwner.getCivilizationShortDescription();
							pLog = LOGFILEMGR.GetLog(kOwner.GetCitySpecializationAI()->GetLogFileName(playerName), FILogFile::kDontTimeStamp);
							strBaseString.Format("%03d, ", GC.getGame().getElapsedGameTurns());
							strBaseString += playerName + ", ";
							strOutBuf.Format("%s, WONDER - Finished %s", getName().GetCString(), pkConstructBuildingInfo->GetDescription());
							strBaseString += strOutBuf;
							pLog->Msg(strBaseString);
						}
					}

				}
			}
		}
		break;
	}

	case ORDER_CREATE:
		eCreateProject = ((ProjectTypes)(pOrderNode->iData1));

		GET_TEAM(getTeam()).changeProjectMaking(eCreateProject, -1);
		kOwner.changeProjectMaking(eCreateProject, -1);

		if(bFinish)
		{
			bool bResult = CreateProject(eCreateProject);
			DEBUG_VARIABLE(bResult);
			CvAssertMsg(bResult, "Failed to create project");

#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityCreated, getOwner(), GetID(), eCreateProject, false, false);
			} else {
#endif
			ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
			if (pkScriptSystem) 
			{
				CvLuaArgsHandle args;
				args->Push(getOwner());
				args->Push(GetID());
				args->Push(eCreateProject);
				args->Push(false); // bGold
				args->Push(false); // bFaith/bCulture

				bool bScriptResult;
				LuaSupport::CallHook(pkScriptSystem, "CityCreated", args.get(), bScriptResult);
			}
#if defined(MOD_EVENTS_CITY)
			}
#endif

			iProductionNeeded = getProductionNeeded(eCreateProject) * 100;
			// max overflow is the value of the item produced (to eliminate prebuild exploits)
			int iOverflow = getProjectProductionTimes100(eCreateProject) - iProductionNeeded;
			int iMaxOverflow = std::max(iProductionNeeded, getCurrentProductionDifferenceTimes100(false, false));
			int iLostProduction = std::max(0, iOverflow - iMaxOverflow);
			iOverflow = std::min(iMaxOverflow, iOverflow);
			if(iOverflow > 0)
			{
				changeOverflowProductionTimes100(iOverflow);
			}
			setProjectProduction(eCreateProject, 0);

			int iProductionGold = ((iLostProduction * GC.getMAXED_PROJECT_GOLD_PERCENT()) / 100);
			if(iProductionGold > 0)
			{
				kOwner.GetTreasury()->ChangeGoldTimes100(iProductionGold);
			}
		}
		break;

	case ORDER_PREPARE:

		if(bFinish)
		{
			eSpecialist = ((SpecialistTypes)(pOrderNode->iData1));

#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityPrepared, getOwner(), GetID(), eSpecialist, false, false);
			}
#endif

			iProductionNeeded = getProductionNeeded(eSpecialist) * 100;

			// max overflow is the value of the item produced (to eliminate prebuild exploits)
			int iOverflow = getSpecialistProductionTimes100(eSpecialist) - iProductionNeeded;
			int iMaxOverflow = std::max(iProductionNeeded, getCurrentProductionDifferenceTimes100(false, false));
			iOverflow = std::min(iMaxOverflow, iOverflow);
			if(iOverflow > 0)
			{
				changeOverflowProductionTimes100(iOverflow);
			}

			setSpecialistProduction(eSpecialist, 0);
		}

		break;

	case ORDER_MAINTAIN:
		break;

	default:
		CvAssertMsg(false, "pOrderNode->eOrderType is not a valid option");
		break;
	}

	if(m_unitBeingBuiltForOperation.IsValid())
	{
		kOwner.CityUncommitToBuildUnitForOperationSlot(m_unitBeingBuiltForOperation);
		m_unitBeingBuiltForOperation.Invalidate();
	}

	if(pOrderNode == headOrderQueueNode())
	{
		bStart = true;
		stopHeadOrder();
	}
	else
	{
		bStart = false;
	}

	m_orderQueue.deleteNode(pOrderNode);
	pOrderNode = NULL;
	if(bFinish)
	{
		CleanUpQueue(); // cleans out items from the queue that may be invalidated by the recent construction
	}

	if(bStart)
	{
		startHeadOrder();
	}

	if((getTeam() == GC.getGame().getActiveTeam()) || GC.getGame().isDebugMode())
	{
		if(isCitySelected())
		{
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
		}
	}

	bMessage = false;

	if(bChoose)
	{
		if(getOrderQueueLength() == 0)
		{
			if(!isHuman() || isProductionAutomated())
			{
				AI_chooseProduction(false /*bInterruptWonders*/, false);
			}
			else
			{
				chooseProduction(eTrainUnit, eConstructBuilding, eCreateProject, bFinish);

				bMessage = true;
			}
		}
	}

	if(bFinish && !bMessage)
	{
		if(getOwner() == GC.getGame().getActivePlayer())
		{
			Localization::String localizedText;
			if(eTrainUnit != NO_UNIT)
			{
				CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eTrainUnit);
				if(pkUnitInfo)
				{
					localizedText = Localization::Lookup(((isLimitedUnitClass((UnitClassTypes)(pkUnitInfo->GetUnitClassType()))) ? "TXT_KEY_MISC_TRAINED_UNIT_IN_LIMITED" : "TXT_KEY_MISC_TRAINED_UNIT_IN"));
					localizedText << pkUnitInfo->GetTextKey() << getNameKey();
				}
			}
			else if(eConstructBuilding != NO_BUILDING)
			{
				CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eConstructBuilding);
				if(pkBuildingInfo)
				{
					localizedText = Localization::Lookup(((isLimitedWonderClass(pkBuildingInfo->GetBuildingClassInfo())) ? "TXT_KEY_MISC_CONSTRUCTED_BUILD_IN_LIMITED" : "TXT_KEY_MISC_CONSTRUCTED_BUILD_IN"));
					localizedText << pkBuildingInfo->GetTextKey() << getNameKey();
				}
			}
			else if(eCreateProject != NO_PROJECT)
			{
				localizedText = Localization::Lookup(((isLimitedProject(eCreateProject)) ? "TXT_KEY_MISC_CREATED_PROJECT_IN_LIMITED" : "TXT_KEY_MISC_CREATED_PROJECT_IN"));
				localizedText << GC.getProjectInfo(eCreateProject)->GetTextKey() << getNameKey();
			}
			if(isProduction())
			{
				localizedText = Localization::Lookup(((isProductionLimited()) ? "TXT_KEY_MISC_WORK_HAS_BEGUN_LIMITED" : "TXT_KEY_MISC_WORK_HAS_BEGUN"));
				localizedText << getProductionNameKey();
			}
			DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8()/*, szSound, MESSAGE_TYPE_MINOR_EVENT, szIcon, (ColorTypes)GC.getInfoTypeForString("COLOR_WHITE"), getX(), getY(), true, true*/);
		}
	}

	if((getTeam() == GC.getGame().getActiveTeam()) || GC.getGame().isDebugMode())
	{
		if(isCitySelected())
		{
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			DLLUI->setDirty(PlotListButtons_DIRTY_BIT, true);
		}
		DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
	}
}

//	--------------------------------------------------------------------------------
void CvCity::swapOrder(int iNum)
{
	// okay, this only swaps the order with the next one up in the queue
	VALIDATE_OBJECT

	if(iNum == 0)
	{
		stopHeadOrder();
	}

	m_orderQueue.swapUp(iNum);

	if(iNum == 0)
	{
		startHeadOrder();
	}

	if((getTeam() == GC.getGame().getActiveTeam()) || GC.getGame().isDebugMode())
	{
		if(isCitySelected())
		{
			//DLLUI->setDirty(InfoPane_DIRTY_BIT, true );
			DLLUI->setDirty(SelectionButtons_DIRTY_BIT, true);
			DLLUI->setDirty(CityScreen_DIRTY_BIT, true);
			DLLUI->setDirty(PlotListButtons_DIRTY_BIT, true);
		}
		DLLUI->setDirty(CityInfo_DIRTY_BIT, true);
	}
}


//	--------------------------------------------------------------------------------
void CvCity::startHeadOrder()
{
	VALIDATE_OBJECT
	OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_MAINTAIN)
		{
			processProcess(((ProcessTypes)(pOrderNode->iData1)), 1);
		}
	}
}


//	--------------------------------------------------------------------------------
void CvCity::stopHeadOrder()
{
	VALIDATE_OBJECT
	OrderData* pOrderNode = headOrderQueueNode();

	if(pOrderNode != NULL)
	{
		if(pOrderNode->eOrderType == ORDER_MAINTAIN)
		{
			processProcess(((ProcessTypes)(pOrderNode->iData1)), -1);
		}
	}
}


//	--------------------------------------------------------------------------------
int CvCity::getOrderQueueLength()
{
	VALIDATE_OBJECT
	return m_orderQueue.getLength();
}


//	--------------------------------------------------------------------------------
OrderData* CvCity::getOrderFromQueue(int iIndex)
{
	VALIDATE_OBJECT
	OrderData* pOrderNode;

	pOrderNode = m_orderQueue.getAt(iIndex);

	if(pOrderNode != NULL)
	{
		return pOrderNode;
	}
	else
	{
		return NULL;
	}
}


//	--------------------------------------------------------------------------------
OrderData* CvCity::nextOrderQueueNode(OrderData* pNode)
{
	VALIDATE_OBJECT
	return m_orderQueue.next(pNode);
}

//	--------------------------------------------------------------------------------
const OrderData* CvCity::nextOrderQueueNode(const OrderData* pNode) const
{
	VALIDATE_OBJECT
	return m_orderQueue.next(pNode);
}


//	--------------------------------------------------------------------------------
const OrderData* CvCity::headOrderQueueNode() const
{
	VALIDATE_OBJECT
	return m_orderQueue.head();
}

//	--------------------------------------------------------------------------------
OrderData* CvCity::headOrderQueueNode()
{
	VALIDATE_OBJECT
	return m_orderQueue.head();
}


//	--------------------------------------------------------------------------------
const OrderData* CvCity::tailOrderQueueNode() const
{
	VALIDATE_OBJECT
	return m_orderQueue.tail();
}

//	--------------------------------------------------------------------------------
/// remove items in the queue that are no longer valid
bool CvCity::CleanUpQueue(void)
{
	VALIDATE_OBJECT
	bool bOK = true;

	for(int iI = (getOrderQueueLength() - 1); iI >= 0; iI--)
	{
		OrderData* pOrder = getOrderFromQueue(iI);

		if(pOrder != NULL)
		{
			if(!canContinueProduction(*pOrder))
			{
				popOrder(iI, false, true);
				bOK = false;
			}
		}
	}

	return bOK;
}

//	--------------------------------------------------------------------------------
int CvCity::CreateUnit(UnitTypes eUnitType, UnitAITypes eAIType, bool bUseToSatisfyOperation)
{
	VALIDATE_OBJECT
	CvPlayer& thisPlayer = GET_PLAYER(getOwner());
	CvUnit* pUnit = thisPlayer.initUnit(eUnitType, getX(), getY(), eAIType);
	CvAssertMsg(pUnit, "");
	if(!pUnit)
	{
		CvAssertMsg(false, "CreateUnit failed");
		return -1;
	}

	if(pUnit->IsHasNoValidMove())
	{
		pUnit->kill(false);
		return -1;
	}

	addProductionExperience(pUnit);

#if defined(MOD_BALANCE_CORE)
	if(pUnit)
	{
		pUnit->setMoves(pUnit->maxMoves());
	}
#endif

#if defined(MOD_BALANCE_CORE)
	if(pUnit && pUnit->isTrade())
	{
		if(GC.getLogging() && GC.getAILogging())
		{
			CvString strCiv = GET_PLAYER(getOwner()).getCivilizationAdjective();
			CvString strLogString;
			strLogString.Format("TRADE UNIT MADE: %s %s at %d,d", strCiv.c_str(), pUnit->getName().c_str(), pUnit->getX(), pUnit->getY() );
			GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
		}
	}
#endif

	CvPlot* pRallyPlot = getRallyPlot();
	if(pRallyPlot != NULL)
	{
		pUnit->PushMission(CvTypes::getMISSION_MOVE_TO(), pRallyPlot->getX(), pRallyPlot->getY());
	}

	if(bUseToSatisfyOperation && m_unitBeingBuiltForOperation.IsValid())
	{
		thisPlayer.CityFinishedBuildingUnitForOperationSlot(m_unitBeingBuiltForOperation, pUnit);
		m_unitBeingBuiltForOperation.Invalidate();
	}
#if defined(MOD_BALANCE_CORE)
	// Check existing armies this unit could fit into if it wasn't automatically added to one.
	if(pUnit && pUnit->getArmyID() == -1)
	{
		for (CvAIOperation* pOp = GET_PLAYER(getOwner()).getFirstAIOperation(); pOp; pOp = GET_PLAYER(getOwner()).getNextAIOperation())
		{
			if (pOp->GetOperationState()==AI_OPERATION_STATE_RECRUITING_UNITS)
			{
				if (pOp->RecruitUnit(pUnit))
					break;
			}
		}
	}
#endif

	// Any AI unit with explore AI as a secondary unit AI (e.g. warriors) are assigned that unit AI if this AI player needs to explore more
#if defined(MOD_BUGFIX_MINOR_CIV_STRATEGIES)
	else if (!pUnit->isHuman() && EconomicAIHelpers::CannotMinorCiv(&thisPlayer, (EconomicAIStrategyTypes)GC.getInfoTypeForString("ECONOMICAISTRATEGY_NEED_RECON")))
#else
	else if(!pUnit->isHuman() && !thisPlayer.isMinorCiv())
#endif
	{
		EconomicAIStrategyTypes eStrategy = (EconomicAIStrategyTypes) GC.getInfoTypeForString("ECONOMICAISTRATEGY_NEED_RECON");
		if(thisPlayer.GetEconomicAI()->IsUsingStrategy(eStrategy))
		{
			if(pUnit->getUnitInfo().GetUnitAIType(UNITAI_EXPLORE) && pUnit->AI_getUnitAIType() != UNITAI_EXPLORE)
			{

				// Now make sure there isn't a critical military threat
				CvMilitaryAI* thisPlayerMilAI = thisPlayer.GetMilitaryAI();
				int iThreat = thisPlayerMilAI->GetThreatTotal();
				iThreat += thisPlayerMilAI->GetBarbarianThreatTotal();
				if(iThreat < thisPlayerMilAI->GetThreatWeight(THREAT_CRITICAL))
				{
					pUnit->AI_setUnitAIType(UNITAI_EXPLORE);
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Assigning explore unit AI to %s, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						thisPlayer.GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
				else
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strLogString;
						strLogString.Format("Not assigning explore AI to %s due to threats, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
						thisPlayer.GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
			}
		}
		eStrategy = (EconomicAIStrategyTypes) GC.getInfoTypeForString("ECONOMICAISTRATEGY_NEED_RECON_SEA");
		EconomicAIStrategyTypes eOtherStrategy = (EconomicAIStrategyTypes) GC.getInfoTypeForString("ECONOMICAISTRATEGY_REALLY_NEED_RECON_SEA");
		if(thisPlayer.GetEconomicAI()->IsUsingStrategy(eStrategy) || thisPlayer.GetEconomicAI()->IsUsingStrategy(eOtherStrategy))
		{
			if(pUnit->getUnitInfo().GetUnitAIType(UNITAI_EXPLORE_SEA))
			{
				pUnit->AI_setUnitAIType(UNITAI_EXPLORE_SEA);
				if(GC.getLogging() && GC.getAILogging())
				{
					CvString strLogString;
					strLogString.Format("Assigning explore sea unit AI to %s, X: %d, Y: %d", pUnit->getName().GetCString(), pUnit->getX(), pUnit->getY());
					thisPlayer.GetHomelandAI()->LogHomelandMessage(strLogString);
				}
			}
		}
	}
	//Increment for stat tracking and achievements
	if(pUnit->isHuman())
	{
		IncrementUnitStatCount(pUnit);
	}

	return pUnit->GetID();
}

//	--------------------------------------------------------------------------------
bool CvCity::CreateBuilding(BuildingTypes eBuildingType)
{
	VALIDATE_OBJECT

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuildingType);
	if(pkBuildingInfo == NULL)
		return false;

	const BuildingClassTypes eBuildingClass = (BuildingClassTypes)pkBuildingInfo->GetBuildingClassType();

	CvPlayerAI& kPlayer = GET_PLAYER(getOwner());

	if(kPlayer.isBuildingClassMaxedOut(eBuildingClass, 0))
	{
		kPlayer.removeBuildingClass(eBuildingClass);
	}

	m_pCityBuildings->SetNumRealBuilding(eBuildingType, m_pCityBuildings->GetNumRealBuilding(eBuildingType) + 1);

#if !defined(NO_ACHIEVEMENTS)
	//Achievements
	if(kPlayer.isHuman() && !GC.getGame().isGameMultiPlayer())
	{
		CvBuildingClassInfo* pBuildingClass = GC.getBuildingClassInfo(eBuildingClass);
		if(pBuildingClass && ::isWorldWonderClass(*pBuildingClass))
		{
			int iCount = 0;
			CvGameTrade* pGameTrade = GC.getGame().GetGameTrade();
			for (uint ui = 0; ui < pGameTrade->GetNumTradeConnections(); ui++)
			{
				if (pGameTrade->IsTradeRouteIndexEmpty(ui))
				{
					continue;
				}

				if (pGameTrade->GetTradeConnection(ui).m_eConnectionType == TRADE_CONNECTION_PRODUCTION)
				{
					CvCity* pDestCity = CvGameTrade::GetDestCity(pGameTrade->GetTradeConnection(ui));
					if (pDestCity->getX() == getX() && pDestCity->getY() == getY())
					{
						iCount++;
					}
				}
			}

			if (iCount >= 3) 
			{
				gDLL->UnlockAchievement(ACHIEVEMENT_XP2_31);
			}
		}

		CheckForAchievementBuilding(eBuildingType);
	}
#endif

	return true;
}


//	--------------------------------------------------------------------------------
bool CvCity::CreateProject(ProjectTypes eProjectType)
{
	VALIDATE_OBJECT

	CvPlayer& thisPlayer = GET_PLAYER(getOwner());
	CvTeam& thisTeam = GET_TEAM(getTeam());
	thisTeam.changeProjectCount(eProjectType, 1);

	ProjectTypes ApolloProgram = (ProjectTypes) GC.getSPACE_RACE_TRIGGER_PROJECT();
	ProjectTypes capsuleID = (ProjectTypes) GC.getSPACESHIP_CAPSULE();
	ProjectTypes boosterID = (ProjectTypes) GC.getSPACESHIP_BOOSTER();
	ProjectTypes stasisID = (ProjectTypes) GC.getSPACESHIP_STASIS();
	ProjectTypes engineID = (ProjectTypes) GC.getSPACESHIP_ENGINE();

	enum eSpaceshipState
	{
	    eUnderConstruction	= 0x0000,
	    eFrame				= 0x0001,
	    eCapsule			= 0x0002,
	    eStasis_Chamber		= 0x0004,
	    eEngine				= 0x0008,
	    eBooster1			= 0x0010,
	    eBooster2			= 0x0020,
	    eBooster3			= 0x0040,
	    eConstructed		= 0x0080,
	};

	if(eProjectType == ApolloProgram)
	{
		CvCity* theCapital = thisPlayer.getCapitalCity();
		if(theCapital)
		{
			auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(theCapital->plot()));
			gDLL->GameplaySpaceshipRemoved(pDllPlot.get());
			gDLL->GameplaySpaceshipCreated(pDllPlot.get(), eUnderConstruction + eFrame);
		}
	}
	else if(GC.getProjectInfo(eProjectType)->IsSpaceship())
	{
		VictoryTypes eVictory = (VictoryTypes)GC.getProjectInfo(eProjectType)->GetVictoryPrereq();

		if(NO_VICTORY != eVictory && GET_TEAM(getTeam()).canLaunch(eVictory))
		{
			auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
			gDLL->GameplaySpaceshipEdited(pDllPlot.get(), eConstructed);
			gDLL->sendLaunch(getOwner(), eVictory);
		}
		else
		{
			//show the spaceship progress

			// this section is kind of hard-coded but it is completely hard-coded on the engine-side so I have to give it the numbers it expects
			int spaceshipState = eFrame;

			if((thisTeam.getProjectCount((ProjectTypes)capsuleID)) == 1)
			{
				spaceshipState += eCapsule;
			}

			if((thisTeam.getProjectCount((ProjectTypes)stasisID)) == 1)
			{
				spaceshipState += eStasis_Chamber;
			}

			if((thisTeam.getProjectCount((ProjectTypes)engineID)) == 1)
			{
				spaceshipState += eEngine;
			}

			if((thisTeam.getProjectCount((ProjectTypes)boosterID)) >= 1)
			{
				spaceshipState += eBooster1;
			}

			if((thisTeam.getProjectCount((ProjectTypes)boosterID)) >= 2)
			{
				spaceshipState += eBooster2;
			}

			if((thisTeam.getProjectCount((ProjectTypes)boosterID)) == 3)
			{
				spaceshipState += eBooster3;
			}

			auto_ptr<ICvPlot1> pDllPlot(new CvDllPlot(plot()));
			gDLL->GameplaySpaceshipEdited(pDllPlot.get(), spaceshipState);
		}
	}

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::CanPlaceUnitHere(UnitTypes eUnitType)
{
	VALIDATE_OBJECT
	bool bCombat = false;

	CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnitType);
	if(pkUnitInfo == NULL)
		return false;

	// slewis - modifying 1upt
	if (pkUnitInfo->IsTrade())
	{
		return true;
	}

	if(pkUnitInfo->GetCombat() > 0 || pkUnitInfo->GetRange() > 0)
	{
		bCombat = true;
	}

	CvPlot* pPlot = plot();

	const IDInfo* pUnitNode;
	const CvUnit* pLoopUnit;

	pUnitNode = pPlot->headUnitNode();

	while(pUnitNode != NULL)
	{
		pLoopUnit = ::getUnit(*pUnitNode);
		pUnitNode = pPlot->nextUnitNode(pUnitNode);

		if(pLoopUnit != NULL)
		{
			// if a trade unit is here, ignore
			if (pLoopUnit->isTrade())
			{
				continue;
			}

			// Units of the same type OR Units belonging to different civs
			if(CvGameQueries::AreUnitsSameType(eUnitType, pLoopUnit->getUnitType()))
			{
				return false;
			}
		}
	}
	return true;
}

//	--------------------------------------------------------------------------------
// Is this city allowed to purchase something right now?
bool CvCity::IsCanPurchase(bool bTestPurchaseCost, bool bTestTrainable, UnitTypes eUnitType, BuildingTypes eBuildingType, ProjectTypes eProjectType, YieldTypes ePurchaseYield)
{
	CvAssertMsg(eUnitType >= 0 || eBuildingType >= 0 || eProjectType >= 0, "No valid passed in");
	CvAssertMsg(!(eUnitType >= 0 && eBuildingType >= 0) && !(eUnitType >= 0 && eProjectType >= 0) && !(eBuildingType >= 0 && eProjectType >= 0), "Only one being passed");

	// Can't purchase anything in a puppeted city
	// slewis - The Venetian Exception
	bool bIsPuppet = IsPuppet();
	bool bVenetianException = false;
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
	bool bPuppetExceptionUnit = false;
	bool bPuppetExceptionBuilding = false;
	bool bAllowsPuppetPurchase = false;
	if(MOD_BALANCE_CORE_PUPPET_PURCHASE && bIsPuppet)
	{
		if(eUnitType >= 0)
		{
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnitType);
			if(pkUnitInfo)
			{
				if(pkUnitInfo->IsPuppetPurchaseOverride())
				{
					bPuppetExceptionUnit = true;
				}
			}
		}
		else if(eBuildingType >= 0)
		{
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuildingType);
			if(pkBuildingInfo)
			{
				if(pkBuildingInfo->IsPuppetPurchaseOverride())
				{
					bPuppetExceptionBuilding = true;
				}
			}
		}
		const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
		for(int iI = 0; iI < GC.getNumBuildingClassInfos(); iI++)
		{
			CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
			if(!pkBuildingClassInfo)
			{
				continue;
			}
			BuildingTypes eLoopBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

			if(eLoopBuilding != NO_BUILDING && m_pCityBuildings->GetNumBuilding(eLoopBuilding) > 0)
			{
				CvBuildingEntry* pkBuildingInfo = GC.GetGameBuildings()->GetEntry(eLoopBuilding);
				if(pkBuildingInfo && pkBuildingInfo->IsAllowsPuppetPurchase())
				{
					bAllowsPuppetPurchase = true;
					break;
				}
			}
		}
	}
#endif
	if (GET_PLAYER(m_eOwner).GetPlayerTraits()->IsNoAnnexing() && bIsPuppet)
	{
		bVenetianException = true;
	}
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
	if (bIsPuppet && !bVenetianException && !bPuppetExceptionBuilding && !bPuppetExceptionUnit && !bAllowsPuppetPurchase)
#else
	if (bIsPuppet && !bVenetianException)
#endif
	{
		return false;
	}

	// Check situational reasons we can't purchase now (similar to not having enough gold or faith)
	if(bTestPurchaseCost)
	{
		// Can't purchase things if the city is in resistance or is being razed
		if(IsResistance() || IsRazing())
			return false;

		// if we're purchasing a unit
		if(eUnitType >= 0)
		{
			// if we can't add this unit to this tile, then don't!
			if(!CanPlaceUnitHere(eUnitType))
				return false;
		}
	}

	// What are we buying this with?
	switch(ePurchaseYield)
	{
	case YIELD_GOLD:
	{
		int iGoldCost = -1;

		// Unit
		if(eUnitType != NO_UNIT)
		{
			if(!canTrain(eUnitType, false, !bTestTrainable, false /*bIgnoreCost*/, true /*bWillPurchase*/))
				return false;

			iGoldCost = GetPurchaseCost(eUnitType);
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
			if(MOD_BALANCE_CORE_PUPPET_PURCHASE && bIsPuppet && !bPuppetExceptionUnit && !bAllowsPuppetPurchase && !bVenetianException)
			{
				return false;
			}
#endif
		}
		// Building
		else if(eBuildingType != NO_BUILDING)
		{
#if defined(MOD_API_EXTENSIONS)
			if(!canConstruct(eBuildingType, false, !bTestTrainable, false /*bIgnoreCost*/, true /*bWillPurchase*/))
#else
			if(!canConstruct(eBuildingType, false, !bTestTrainable))
#endif
			{
				bool bAlreadyUnderConstruction = canConstruct(eBuildingType, true, !bTestTrainable) && getFirstBuildingOrder(eBuildingType) != -1;
				if(!bAlreadyUnderConstruction)
				{
					return false;
				}
			}

			iGoldCost = GetPurchaseCost(eBuildingType);
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
			if(MOD_BALANCE_CORE_PUPPET_PURCHASE && bIsPuppet && !bPuppetExceptionBuilding && !bAllowsPuppetPurchase && !bVenetianException)
			{
				return false;
			}
#endif
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
			if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS && (NO_BUILDING != eBuildingType))
			{
				//Have we already invested here?
				CvBuildingEntry* pGameBuilding = GC.getBuildingInfo(eBuildingType);
				const BuildingClassTypes eBuildingClass = (BuildingClassTypes)(pGameBuilding->GetBuildingClassType());
				if(IsBuildingInvestment(eBuildingClass))
				{
					return false;
				}
			}
#endif
#if defined(MOD_BALANCE_CORE_UNIT_INVESTMENTS)
			if(MOD_BALANCE_CORE_UNIT_INVESTMENTS && (NO_UNIT != eUnitType))
			{
				//Have we already invested here?
				CvUnitEntry* pGameUnit = GC.getUnitInfo(eUnitType);
				const UnitClassTypes eUnitClass = (UnitClassTypes)(pGameUnit->GetUnitClassType());
				if(IsUnitInvestment(eUnitClass))
				{
					return false;
				}
			}
#endif		
		}
		// Project
		else if(eProjectType != NO_PROJECT)
		{
			if(/*1*/ GC.getPROJECT_PURCHASING_DISABLED() == 1)
				return false;

			if(!canCreate(eProjectType, false, !bTestTrainable))
				return false;

			iGoldCost = GetPurchaseCost(eProjectType);
		}

		if(iGoldCost == -1)
		{
			return false;
		}
		else
		{
			if(bTestPurchaseCost)
			{
				// Trying to buy something when you don't have enough money!!
				if(iGoldCost > GET_PLAYER(getOwner()).GetTreasury()->GetGold())
					return false;
#if defined(MOD_BALANCE_CORE)
				if(eUnitType != NO_UNIT && MOD_BALANCE_CORE && GetUnitPurchaseCooldown() > 0)
				{
					return false;
				}
				if(NO_BUILDING != eBuildingType && MOD_BALANCE_CORE && GetBuildingPurchaseCooldown() > 0)
				{
					return false;
				}
				if(eUnitType != NO_UNIT && !bVenetianException)
				{
					CvUnitEntry* thisUnitInfo = GC.getUnitInfo(eUnitType);
					// See if there are any BuildingClass requirements
					const int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
					const CvCivilizationInfo& thisCivilization = getCivilizationInfo();
					for(int iBuildingClassLoop = 0; iBuildingClassLoop < iNumBuildingClassInfos; iBuildingClassLoop++)
					{
						const BuildingClassTypes eBuildingClass = (BuildingClassTypes) iBuildingClassLoop;
						CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo(eBuildingClass);
						if(!pkBuildingClassInfo)
						{
							continue;
						}

						// Requires Building
						if(thisUnitInfo->GetBuildingClassPurchaseRequireds(eBuildingClass))
						{
							const BuildingTypes ePrereqBuilding = (BuildingTypes)(thisCivilization.getCivilizationBuildings(eBuildingClass));

							if(GetCityBuildings()->GetNumBuilding(ePrereqBuilding) == 0)
							{
								return false;
							}
						}
					}
				}
#endif	
			}
		}
	}
	break;

	case YIELD_FAITH:
	{
		int iFaithCost = -1;

		// Does this city have a majority religion?
		ReligionTypes eReligion = GetCityReligions()->GetReligiousMajority();
#if defined(MOD_BUGFIX_MINOR)
		// Permit faith purchases from pantheon beliefs
		if(eReligion < RELIGION_PANTHEON)
#else
		if(eReligion <= RELIGION_PANTHEON)
#endif
		{
			return false;
		}

		// Unit
		if(eUnitType != NO_UNIT)
		{
			iFaithCost = GetFaithPurchaseCost(eUnitType, true);
			if(iFaithCost < 1)
			{
				return false;
			}
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
			if(MOD_BALANCE_CORE_PUPPET_PURCHASE && bIsPuppet && !bPuppetExceptionUnit && !bAllowsPuppetPurchase && !bVenetianException)
			{
				return false;
			}
#endif
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnitType);

			if(pkUnitInfo)
			{
#if defined(MOD_BUGFIX_MINOR)
				if (pkUnitInfo->IsRequiresEnhancedReligion() && !(GC.getGame().GetGameReligions()->GetReligion(eReligion, m_eOwner)->m_bEnhanced))
#else
				if (pkUnitInfo->IsRequiresEnhancedReligion() && !(GC.getGame().GetGameReligions()->GetReligion(eReligion, NO_PLAYER)->m_bEnhanced))
#endif
				{
					return false;
				}
#if defined(MOD_BALANCE_CORE)
				const CvReligion *pReligion2 = GC.getGame().GetGameReligions()->GetReligion(eReligion, m_eOwner);
				if (pReligion2->m_Beliefs.IsSpecificFaithBuyingEnabled(eUnitType, getOwner()))
				{
					if(canTrain(eUnitType, false, !bTestTrainable, false /*bIgnoreCost*/, true /*bWillPurchase*/))
					{
						if(iFaithCost <= GET_PLAYER(getOwner()).GetFaith())
						{
							return true;
						}
					}
				}
#endif				
				if (pkUnitInfo->IsRequiresFaithPurchaseEnabled())
				{
#if defined(MOD_BALANCE_CORE)
					if(pkUnitInfo->GetBeliefUnlock() != NULL && pkUnitInfo->GetBeliefUnlock() != NO_BELIEF)
					{
						if(!HasBelief((BeliefTypes)pkUnitInfo->GetBeliefUnlock()))
						{
							return false;
						}
					}
#endif
					TechTypes ePrereqTech = (TechTypes)pkUnitInfo->GetPrereqAndTech();
					if (ePrereqTech == -1)
					{
						const CvReligion *pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligion, m_eOwner);
						if (!pReligion->m_Beliefs.IsFaithBuyingEnabled((EraTypes)0, getOwner())) // Ed?
						{
							return false;
						}
						if(!canTrain(eUnitType, false, !bTestTrainable, false /*bIgnoreCost*/, true /*bWillPurchase*/))
						{
							return false;
						}
					}
					else
					{
						CvTechEntry *pkTechInfo = GC.GetGameTechs()->GetEntry(ePrereqTech);
						if (!pkTechInfo)
						{
							return false;
						}
						else
						{
							const CvReligion *pReligion = GC.getGame().GetGameReligions()->GetReligion(eReligion, m_eOwner);
							if (!pReligion->m_Beliefs.IsFaithBuyingEnabled((EraTypes)pkTechInfo->GetEra(), getOwner()))
							{
								return false;
							}
							if(!canTrain(eUnitType, false, !bTestTrainable, false /*bIgnoreCost*/, true /*bWillPurchase*/))
							{
								return false;
							}
						}
					}
				}
#if defined(MOD_BUGFIX_MINOR)
				else
				{
#if defined(MOD_BALANCE_CORE)
					if(pkUnitInfo->GetBeliefUnlock() != NULL && pkUnitInfo->GetBeliefUnlock() != NO_BELIEF)
					{
						if(!HasBelief((BeliefTypes)pkUnitInfo->GetBeliefUnlock()))
						{
							return false;
						}
					}
#endif
					// Missionaries, Inquisitors and Prophets
					// We need a full religion and not just a pantheon,
					// and also to test that the player can build the unit, specifically the check for a civ specific version of the unit
					if(eReligion <= RELIGION_PANTHEON || !canTrain(eUnitType, false, !bTestTrainable, true /*bIgnoreCost*/, true /*bWillPurchase*/))
					{
						return false;
					}
				}
#endif
			}
		}
		// Building
		else if(eBuildingType != NO_BUILDING)
		{
			CvBuildingEntry* pkBuildingInfo = GC.GetGameBuildings()->GetEntry(eBuildingType);
 
			// Religion-enabled building
			if(pkBuildingInfo && pkBuildingInfo->IsUnlockedByBelief())
			{
				ReligionTypes eMajority = GetCityReligions()->GetReligiousMajority();
				if(eMajority <= RELIGION_PANTHEON)
				{
					return false;
				}
				const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(eMajority, getOwner());
				if(pReligion == NULL || !pReligion->m_Beliefs.IsBuildingClassEnabled((BuildingClassTypes)pkBuildingInfo->GetBuildingClassType(), getOwner()))
				{
					return false;
				}
#if defined(MOD_BUGFIX_MINOR)
			}
#endif

#if defined(MOD_API_EXTENSIONS)
				if(!canConstruct(eBuildingType, false, !bTestTrainable, true /*bIgnoreCost*/, true /*bWillPurchase*/))
#else
				if(!canConstruct(eBuildingType, false, !bTestTrainable, true /*bIgnoreCost*/))
#endif
				{
					return false;
				}

				if(GetCityBuildings()->GetNumBuilding(eBuildingType) > 0)
				{
					return false;
				}

				TechTypes ePrereqTech = (TechTypes)pkBuildingInfo->GetPrereqAndTech();
				if(ePrereqTech != NO_TECH)
				{
					CvTechEntry *pkTechInfo = GC.GetGameTechs()->GetEntry(ePrereqTech);
					if (pkTechInfo && !GET_TEAM(GET_PLAYER(getOwner()).getTeam()).GetTeamTechs()->HasTech(ePrereqTech))
					{
						return false;
					}
				}

				// Does this city have prereq buildings?
				int iNumBuildingClassInfos = GC.getNumBuildingClassInfos();
				BuildingTypes ePrereqBuilding;
				for(int iI = 0; iI < iNumBuildingClassInfos; iI++)
				{
					CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
					if(!pkBuildingClassInfo)
					{
						continue;
					}

					if(pkBuildingInfo->IsBuildingClassNeededInCity(iI))
					{
						const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
						ePrereqBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

						if(ePrereqBuilding != NO_BUILDING)
						{
							if(0 == m_pCityBuildings->GetNumBuilding(ePrereqBuilding))
							{
								return false;
							}
						}
					}
				}
#if !defined(MOD_BUGFIX_MINOR)
			}
#endif
#if defined(MOD_BALANCE_CORE)
			// Does this city have prereq buildings?
			if(MOD_BALANCE_CORE)
			{
				int iNumBuildings = 0;
				for(int iI = 0; iI < iNumBuildingClassInfos; iI++)
				{
					CvBuildingClassInfo* pkBuildingClassInfo = GC.getBuildingClassInfo((BuildingClassTypes)iI);
					if(!pkBuildingClassInfo)
					{
						continue;
					}

					if(pkBuildingInfo->IsBuildingClassNeededAnywhere(iI))
					{
						const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
						ePrereqBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

						if(ePrereqBuilding != NO_BUILDING)
						{
							CvCity* pLoopCity;
							int iLoop;
							for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
							{
								if(pLoopCity->GetCityBuildings()->GetNumBuilding(ePrereqBuilding) > 0)
								{
									iNumBuildings++;
								}
							}
							if(iNumBuildings == 0)
							{
								return false;
							}
						}
					}
					if(pkBuildingInfo->IsBuildingClassNeededNowhere(iI))
					{
						const CvCivilizationInfo& thisCivInfo = getCivilizationInfo();
						ePrereqBuilding = ((BuildingTypes)(thisCivInfo.getCivilizationBuildings(iI)));

						if(ePrereqBuilding != NO_BUILDING)
						{
							CvCity* pLoopCity;
							int iLoop;
							for(pLoopCity = GET_PLAYER(getOwner()).firstCity(&iLoop); pLoopCity != NULL; pLoopCity = GET_PLAYER(getOwner()).nextCity(&iLoop))
							{
								if(pLoopCity->GetCityBuildings()->GetNumBuilding(ePrereqBuilding) > 0)
								{
									return false;
								}
							}
						}
					}
				}
			}
#endif
			iFaithCost = GetFaithPurchaseCost(eBuildingType);
			if(iFaithCost < 1) return false;
#if defined(MOD_BALANCE_CORE_PUPPET_PURCHASE)
			if(MOD_BALANCE_CORE_PUPPET_PURCHASE && bIsPuppet && !bPuppetExceptionBuilding && !bAllowsPuppetPurchase && !bVenetianException)
			{
				return false;
			}
#endif
		}

		if(iFaithCost > 0)
		{
			if(bTestPurchaseCost)
			{
#if defined(MOD_BALANCE_CORE)
				if(eUnitType != NO_UNIT)
				{
					CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnitType);
					if(pkUnitInfo && GET_PLAYER(getOwner()).GetFaithPurchaseCooldown() > 0 && pkUnitInfo->GetGlobalFaithCooldown() > 0)
					{
						return false;
					}
				}
#endif
				// Trying to buy something when you don't have enough faith!!
				if(iFaithCost > GET_PLAYER(getOwner()).GetFaith())
				{
					return false;
				}
			}
		}
	}
	break;
	}

	return true;
}

//	--------------------------------------------------------------------------------
// purchase something at the city
void CvCity::Purchase(UnitTypes eUnitType, BuildingTypes eBuildingType, ProjectTypes eProjectType, YieldTypes ePurchaseYield)
{
	VALIDATE_OBJECT

	CvPlayer& kPlayer = GET_PLAYER(getOwner());

	switch(ePurchaseYield)
	{
	case YIELD_GOLD:
	{
		// Can we actually buy this thing?
		if(!IsCanPurchase(/*bTestPurchaseCost*/ true, /*bTestTrainable*/ true, eUnitType, eBuildingType, eProjectType, YIELD_GOLD))
			return;

		int iGoldCost = 0;
		
		kPlayer.GetTreasury();

		// Unit
		if(eUnitType != NO_UNIT){
			iGoldCost = GetPurchaseCost(eUnitType);
			CvUnitEntry* pGameUnit = GC.getUnitInfo(eUnitType);
			if(pGameUnit != NULL)
			{
				kPlayer.GetTreasury()->LogExpenditure((CvString)pGameUnit->GetText(), iGoldCost, 2);
			}
#if defined(MOD_BALANCE_CORE)
			SetUnitPurchaseCooldown(pGameUnit->GetCooldown());
#endif
		// Building
		}else if(eBuildingType != NO_BUILDING){
			iGoldCost = GetPurchaseCost(eBuildingType);
			CvBuildingEntry* pGameBuilding = GC.getBuildingInfo(eBuildingType);
			if(pGameBuilding != NULL)
			{
				kPlayer.GetTreasury()->LogExpenditure((CvString)pGameBuilding->GetText(), iGoldCost, 2);
			}
#if defined(MOD_BALANCE_CORE)
			SetBuildingPurchaseCooldown(pGameBuilding->GetCooldown());
#endif
		// Project
		} else if(eProjectType != NO_PROJECT){
			iGoldCost = GetPurchaseCost(eProjectType);
			kPlayer.GetTreasury()->LogExpenditure((CvString)GC.getProjectInfo(eProjectType)->GetText(), iGoldCost, 2);
		}

		GET_PLAYER(getOwner()).GetTreasury()->ChangeGold(-iGoldCost);
#if defined(MOD_BALANCE_CORE)
		if(iGoldCost > 0)
		{
			GET_PLAYER(getOwner()).doInstantYield(INSTANT_YIELD_TYPE_PURCHASE, false, NO_GREATPERSON, NO_BUILDING, iGoldCost, false, NO_PLAYER, NULL, false, this);
		}
#endif

		bool bResult = false;
		if(eUnitType >= 0)
		{
#if defined(MOD_BALANCE_CORE_UNIT_INVESTMENTS)
			if(MOD_BALANCE_CORE_UNIT_INVESTMENTS)
			{
				CvUnitEntry* pGameUnit = GC.getUnitInfo(eUnitType);
				if(pGameUnit)
				{
					const UnitClassTypes eUnitClass = (UnitClassTypes)(pGameUnit->GetUnitClassType());
					if(eUnitClass != NO_UNITCLASS)
					{
						SetUnitInvestment(eUnitClass, true);
						if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoAnnexing() && IsPuppet())
						{
							pushOrder(ORDER_TRAIN, eUnitType, -1, false, false, true, false);
						}
						else if(!GET_PLAYER(getOwner()).isHuman() && !IsPuppet())
						{
							pushOrder(ORDER_TRAIN, eUnitType, -1, false, false, true, false);
						}
					}
				}
			}
			else
			{
#endif
			int iResult = CreateUnit(eUnitType);
			CvAssertMsg(iResult != -1, "Unable to create unit");
			if (iResult != -1)
			{
				CvUnit* pUnit = kPlayer.getUnit(iResult);
				if (!pUnit->getUnitInfo().CanMoveAfterPurchase())
				{
					pUnit->setMoves(0);
				}
#if defined(MOD_BALANCE_CORE)
				if(pUnit && pUnit->isFreeUpgrade() || GET_PLAYER(getOwner()).GetPlayerTraits()->IsFreeUpgrade())
				{
					UnitTypes eUpgradeUnit = pUnit->GetUpgradeUnitType();
					if(eUpgradeUnit != NO_UNIT && this->canTrain(eUpgradeUnit, false, false, true))
					{
						pUnit->DoUpgrade(true);
					}
				}
				if(kPlayer.GetPlayerTraits()->IsFreeZuluPikemanToImpi())
				{
					UnitClassTypes ePikemanClass = (UnitClassTypes)GC.getInfoTypeForString("UNITCLASS_PIKEMAN");
					UnitTypes eZuluImpi = (UnitTypes)GC.getInfoTypeForString("UNIT_ZULU_IMPI");
					if(pUnit != NULL && pUnit->getUnitClassType() == ePikemanClass && this->canTrain(eZuluImpi, false, false, true))
					{
						CvUnitEntry* pkcUnitEntry = GC.getUnitInfo(eZuluImpi);
						if(pkcUnitEntry)
						{
							UnitAITypes eZuluImpiAI = pkcUnitEntry->GetDefaultUnitAIType();
							CvUnit* pZuluImpi = kPlayer.initUnit(eZuluImpi, pUnit->getX(), pUnit->getY(), eZuluImpiAI);
							pZuluImpi->convert(pUnit, true);
						}
					}
				}
				if(pUnit && pUnit->isTrade())
				{
					if(GC.getLogging() && GC.getAILogging())
					{
						CvString strCiv = GET_PLAYER(getOwner()).getCivilizationAdjective();
						CvString strLogString;
						strLogString.Format("TRADE UNIT BOUGHT: %s %s at %d,d", strCiv.c_str(), pUnit->getName().c_str(), pUnit->getX(), pUnit->getY() );
						GET_PLAYER(getOwner()).GetHomelandAI()->LogHomelandMessage(strLogString);
					}
				}
#endif

#if defined(MOD_EVENTS_CITY)
				if (MOD_EVENTS_CITY) {
					GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityTrained, getOwner(), GetID(), pUnit->GetID(), true, false);
				} else {
#endif
				ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
				if (pkScriptSystem) 
				{
					CvLuaArgsHandle args;
					args->Push(getOwner());
					args->Push(GetID());
					args->Push(pUnit->GetID());
					args->Push(true); // bGold
					args->Push(false); // bFaith/bCulture

					bool bScriptResult;
					LuaSupport::CallHook(pkScriptSystem, "CityTrained", args.get(), bScriptResult);
				}
#if defined(MOD_EVENTS_CITY)
			}
#endif
			}
#if defined(MOD_BALANCE_CORE_UNIT_INVESTMENTS)
			}
#endif
		}
		else if(eBuildingType >= 0)
		{
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
			if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
			{
				CvBuildingEntry* pGameBuilding = GC.getBuildingInfo(eBuildingType);
				if(pGameBuilding)
				{
					const BuildingClassTypes eBuildingClass = (BuildingClassTypes)(pGameBuilding->GetBuildingClassType());
					if(eBuildingClass != NO_BUILDINGCLASS)
					{
						SetBuildingInvestment(eBuildingClass, true);
						if(GET_PLAYER(getOwner()).GetPlayerTraits()->IsNoAnnexing() && IsPuppet())
						{
							pushOrder(ORDER_CONSTRUCT, eBuildingType, -1, false, false, true, false);
						}
						else if(!GET_PLAYER(getOwner()).isHuman() && !IsPuppet())
						{
							pushOrder(ORDER_CONSTRUCT, eBuildingType, -1, false, false, true, false);
						}
					}
				}
			}
			else
			{
#endif
			bResult = CreateBuilding(eBuildingType);
#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityConstructed, getOwner(), GetID(), eBuildingType, true, false);
			} else {
#endif
#if defined(MOD_BALANCE_CORE)
			CvBuildingEntry* pkPurchasedBuildingInfo = GC.getBuildingInfo(eBuildingType);
			if(pkPurchasedBuildingInfo)
			{
				const BuildingClassTypes ePurchasedClass = (BuildingClassTypes)pkPurchasedBuildingInfo->GetBuildingClassType();
				if(ePurchasedClass != NO_BUILDINGCLASS)
				{
					SetPurchased(ePurchasedClass, true);
				}
			}
#endif
			ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
			if (pkScriptSystem) 
			{
				CvLuaArgsHandle args;
				args->Push(getOwner());
				args->Push(GetID());
				args->Push(eBuildingType);
				args->Push(true); // bGold
				args->Push(false); // bFaith/bCulture

				bool bScriptResult;
				LuaSupport::CallHook(pkScriptSystem, "CityConstructed", args.get(), bScriptResult);
			}
#if defined(MOD_EVENTS_CITY)
			}
#endif

			CleanUpQueue(); // cleans out items from the queue that may be invalidated by the recent construction
			CvAssertMsg(bResult, "Unable to create building");
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
			}
#endif
		}
		else if(eProjectType >= 0)
		{
			bResult = CreateProject(eProjectType);
			CvAssertMsg(bResult, "Unable to create project");

#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityCreated, getOwner(), GetID(), eProjectType, true, false);
			} else {
#endif
			ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
			if (pkScriptSystem) 
			{
				CvLuaArgsHandle args;
				args->Push(getOwner());
				args->Push(GetID());
				args->Push(eProjectType);
				args->Push(true); // bGold
				args->Push(false); // bFaith/bCulture

				bool bScriptResult;
				LuaSupport::CallHook(pkScriptSystem, "CityCreated", args.get(), bScriptResult);
			}
#if defined(MOD_EVENTS_CITY)
			}
#endif
		}
	}
	break;
	case YIELD_FAITH:
	{
		int iFaithCost = 0;

		// Can we actually buy this thing?
		if(!IsCanPurchase(/*bTestPurchaseCost*/ true, /*bTestTrainable*/ true, eUnitType, eBuildingType, eProjectType, YIELD_FAITH))
			return;

		// Unit
		if(eUnitType != NO_UNIT)
			iFaithCost = GetFaithPurchaseCost(eUnitType, true  /*bIncludeBeliefDiscounts*/);
		// Building
		else if(eBuildingType != NO_BUILDING)
			iFaithCost = GetFaithPurchaseCost(eBuildingType);

		if(eUnitType >= 0)
		{
			int iResult = CreateUnit(eUnitType);
			CvAssertMsg(iResult != -1, "Unable to create unit");
			if (iResult == -1)
				return;	// Can't create the unit, most likely we have no place for it.  We have not deducted the cost yet so just exit.

			CvUnit* pUnit = kPlayer.getUnit(iResult);
#if defined(MOD_BALANCE_CORE)
			if(pUnit && pUnit->getUnitInfo().GetGlobalFaithCooldown() > 0)
			{
				int iCooldown = pUnit->getUnitInfo().GetGlobalFaithCooldown();
				iCooldown *= GC.getGame().getGameSpeedInfo().getTrainPercent();
				iCooldown /= 100;
				kPlayer.ChangeFaithPurchaseCooldown(iCooldown);
			}
#endif
#if defined(MOD_BUGFIX_MOVE_AFTER_PURCHASE)
			if (!pUnit->getUnitInfo().CanMoveAfterPurchase())
			{
#endif
				pUnit->setMoves(0);
#if defined(MOD_BUGFIX_MOVE_AFTER_PURCHASE)
			}
#endif
#if defined(MOD_BALANCE_CORE)
			if(pUnit && pUnit->isFreeUpgrade() || GET_PLAYER(getOwner()).GetPlayerTraits()->IsFreeUpgrade())
			{
				UnitTypes eUpgradeUnit = pUnit->GetUpgradeUnitType();
				if(eUpgradeUnit != NO_UNIT && this->canTrain(eUpgradeUnit, false, false, true))
				{
					pUnit->DoUpgrade(true);
				}
			}
			if(kPlayer.GetPlayerTraits()->IsFreeZuluPikemanToImpi())
			{
				UnitClassTypes ePikemanClass = (UnitClassTypes)GC.getInfoTypeForString("UNITCLASS_PIKEMAN");
				UnitTypes eZuluImpi = (UnitTypes)GC.getInfoTypeForString("UNIT_ZULU_IMPI");
				if(pUnit != NULL && pUnit->getUnitClassType() == ePikemanClass && this->canTrain(eZuluImpi, false, false, true))
				{
					CvUnitEntry* pkcUnitEntry = GC.getUnitInfo(eZuluImpi);
					if(pkcUnitEntry)
					{
						UnitAITypes eZuluImpiAI = pkcUnitEntry->GetDefaultUnitAIType();
						CvUnit* pZuluImpi = kPlayer.initUnit(eZuluImpi, pUnit->getX(), pUnit->getY(), eZuluImpiAI);
						pZuluImpi->convert(pUnit, true);
					}
				}
			}
#endif
#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityTrained, getOwner(), GetID(), pUnit->GetID(), false, true);
			} else {
#endif
			ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
			if (pkScriptSystem) 
			{
				CvLuaArgsHandle args;
				args->Push(getOwner());
				args->Push(GetID());
				args->Push(pUnit->GetID());
				args->Push(false); // bGold
				args->Push(true); // bFaith/bCulture

				bool bResult;
				LuaSupport::CallHook(pkScriptSystem, "CityTrained", args.get(), bResult);
			}
#if defined(MOD_EVENTS_CITY)
			}
#endif

			// Prophets are always of the religion the player founded
			ReligionTypes eReligion;
			if(pUnit->getUnitInfo().IsFoundReligion())
			{
				eReligion = kPlayer.GetReligions()->GetReligionCreatedByPlayer();
			}
			else
			{
				eReligion = GetCityReligions()->GetReligiousMajority();
			}
			pUnit->GetReligionData()->SetReligion(eReligion);

			int iReligionSpreads = pUnit->getUnitInfo().GetReligionSpreads();
			int iReligiousStrength = pUnit->getUnitInfo().GetReligiousStrength();

			// Missionary strength
			if(iReligionSpreads > 0 && eReligion > RELIGION_PANTHEON)
			{
				pUnit->GetReligionData()->SetSpreadsLeft(iReligionSpreads + GetCityBuildings()->GetMissionaryExtraSpreads());
				pUnit->GetReligionData()->SetReligiousStrength(iReligiousStrength);
			}

			if (pUnit->getUnitInfo().GetOneShotTourism() > 0)
			{
				pUnit->SetTourismBlastStrength(kPlayer.GetCulture()->GetTourismBlastStrength(pUnit->getUnitInfo().GetOneShotTourism()));
			}
#if defined(MOD_BALANCE_CORE)
			if (pUnit->getUnitInfo().GetBaseBeakersTurnsToCount() > 0)
			{
				pUnit->SetScienceBlastStrength(pUnit->getDiscoverAmount());
			}
			if (pUnit->getUnitInfo().GetBaseCultureTurnsToCount() > 0)
			{
				pUnit->SetCultureBlastStrength(pUnit->getGivePoliciesCulture());
			}
#endif

			kPlayer.ChangeFaith(-iFaithCost);

			UnitClassTypes eUnitClass = pUnit->getUnitClassType();
			if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_WRITER"))
			{
				kPlayer.incrementWritersFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_ARTIST"))
			{
				kPlayer.incrementArtistsFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_MUSICIAN"))
			{
				kPlayer.incrementMusiciansFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_SCIENTIST"))
			{
				kPlayer.incrementScientistsFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_MERCHANT"))
			{
				kPlayer.incrementMerchantsFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_ENGINEER"))
			{
				kPlayer.incrementEngineersFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_GENERAL"))
			{
				kPlayer.incrementGeneralsFromFaith();
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_ADMIRAL"))
			{
				kPlayer.incrementAdmiralsFromFaith();
				CvPlot *pSpawnPlot = kPlayer.GetGreatAdmiralSpawnPlot(pUnit);
				if (pUnit->plot() != pSpawnPlot)
				{
					pUnit->setXY(pSpawnPlot->getX(), pSpawnPlot->getY());
				}
			}
			else if (eUnitClass == GC.getInfoTypeForString("UNITCLASS_PROPHET"))
			{
#if defined(MOD_GLOBAL_TRULY_FREE_GP)
				kPlayer.GetReligions()->ChangeNumProphetsSpawned(1, false);
#else
				kPlayer.GetReligions()->ChangeNumProphetsSpawned(1);
#endif
			}
#if defined(MOD_DIPLOMACY_CITYSTATES)
			else if (MOD_DIPLOMACY_CITYSTATES && eUnitClass == GC.getInfoTypeForString("UNITCLASS_GREAT_DIPLOMAT"))
			{
				kPlayer.incrementDiplomatsFromFaith();
			}
#endif
#if defined(MOD_BALANCE_CORE)
			else if (pUnit->getUnitInfo().IsGPExtra() == 1)
			{
				kPlayer.incrementGPExtra1FromFaith();
			}
			else if (pUnit->getUnitInfo().IsGPExtra() == 2)
			{
				kPlayer.incrementGPExtra2FromFaith();
			}
			else if (pUnit->getUnitInfo().IsGPExtra() == 3)
			{
				kPlayer.incrementGPExtra3FromFaith();
			}
			else if (pUnit->getUnitInfo().IsGPExtra() == 4)
			{
				kPlayer.incrementGPExtra4FromFaith();
			}
			else if (pUnit->getUnitInfo().IsGPExtra() == 5)
			{
				kPlayer.incrementGPExtra5FromFaith();
			}
#endif

			if(GC.getLogging())
			{
				CvString strLogMsg;
				CvString temp;
				strLogMsg = kPlayer.getCivilizationShortDescription();
				strLogMsg += ", FAITH UNIT PURCHASE, ";
				strLogMsg += pUnit->getName();
				strLogMsg += ", ";
				strLogMsg += getName();
				strLogMsg += ", Faith Cost: ";
				temp.Format("%d", iFaithCost);
				strLogMsg += temp;
				strLogMsg += ", Faith Left: ";
				temp.Format("%d", kPlayer.GetFaith());
				strLogMsg += temp;
				GC.getGame().GetGameReligions()->LogReligionMessage(strLogMsg);
			}
		}

		else if(eBuildingType >= 0)
		{
			bool bResult = false;
			bResult = CreateBuilding(eBuildingType);
			CleanUpQueue(); // cleans out items from the queue that may be invalidated by the recent construction
			CvAssertMsg(bResult, "Unable to create building");

#if defined(MOD_EVENTS_CITY)
			if (MOD_EVENTS_CITY) {
				GAMEEVENTINVOKE_HOOK(GAMEEVENT_CityConstructed, getOwner(), GetID(), eBuildingType, false, true);
			} else {
#endif
#if defined(MOD_BALANCE_CORE)
			CvBuildingEntry* pkPurchasedBuildingInfo = GC.getBuildingInfo(eBuildingType);
			if(pkPurchasedBuildingInfo)
			{
				const BuildingClassTypes ePurchasedClass = (BuildingClassTypes)pkPurchasedBuildingInfo->GetBuildingClassType();
				if(ePurchasedClass != NO_BUILDINGCLASS)
				{
					SetPurchased(ePurchasedClass, true);
				}
			}
#endif
			ICvEngineScriptSystem1* pkScriptSystem = gDLL->GetScriptSystem();
			if (pkScriptSystem)
			{
				CvLuaArgsHandle args;
				args->Push(getOwner());
				args->Push(GetID());
				args->Push(eBuildingType);
				args->Push(false); // bGold
				args->Push(true); // bFaith/bCulture

				bool bScriptResult;
				LuaSupport::CallHook(pkScriptSystem, "CityConstructed", args.get(), bScriptResult);
			}
#if defined(MOD_EVENTS_CITY)
			}
#endif

			kPlayer.ChangeFaith(-iFaithCost);

			if(GC.getLogging())
			{
				CvString strLogMsg;
				CvString temp;
				strLogMsg = kPlayer.getCivilizationShortDescription();
				strLogMsg += ", FAITH BUILDING PURCHASE, ";

				CvBuildingXMLEntries* pGameBuildings = GC.GetGameBuildings();
				if(pGameBuildings != NULL)
				{
					CvBuildingEntry* pBuildingEntry = pGameBuildings->GetEntry(eBuildingType);
					if(pBuildingEntry != NULL)
					{
						strLogMsg += pBuildingEntry->GetDescription();
						strLogMsg += ", ";
					}
				}
				strLogMsg += getName();
				strLogMsg += ", Faith Cost: ";
				temp.Format("%d", iFaithCost);
				strLogMsg += temp;
				strLogMsg += ", Faith Left: ";
				temp.Format("%d", kPlayer.GetFaith());
				strLogMsg += temp;
				GC.getGame().GetGameReligions()->LogReligionMessage(strLogMsg);
			}
		}
	}
	break;
	}
}


// Protected Functions...

//	--------------------------------------------------------------------------------
void CvCity::doGrowth()
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doGrowth, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	// here would be a good place to override this in Lua

	// No growth or starvation if being razed
	if(IsRazing())
	{
		return;
	}
#if defined(MOD_BALANCE_CORE)
	//No growth or starvation if in resistance
	if(IsResistance())
	{
		return;
	}
#endif
	int iDiff = foodDifferenceTimes100();

	if(iDiff < 0)
	{
		CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
		if(pNotifications)
		{
			Localization::String text = Localization::Lookup("TXT_KEY_NTFN_CITY_STARVING");
			text << getNameKey();
			Localization::String summary = Localization::Lookup("TXT_KEY_NTFN_CITY_STARVING_S");
			summary << getNameKey();

			pNotifications->Add(NOTIFICATION_STARVING, text.toUTF8(), summary.toUTF8(), getX(), getY(), -1);
		}
	}

	changeFoodTimes100(iDiff);
	changeFoodKept(iDiff/100);

	setFoodKept(range(getFoodKept(), 0, ((growthThreshold() * getMaxFoodKeptPercent()) / 100)));

	if(getFood() >= growthThreshold())
	{
		if(GetCityCitizens()->IsForcedAvoidGrowth())  // don't grow a city if we are at avoid growth
		{
			setFood(growthThreshold());
		}
		else
		{
			changeFood(-(std::max(0, (growthThreshold() - getFoodKept()))));
			changePopulation(1);

			// Only show notification if the city is small
			if(getPopulation() <= 5)
			{
				CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
				if(pNotifications)
				{
					Localization::String localizedText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_GROWTH");
					localizedText << getNameKey() << getPopulation();
					Localization::String localizedSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_GROWTH");
					localizedSummary << getNameKey();
					pNotifications->Add(NOTIFICATION_CITY_GROWTH, localizedText.toUTF8(), localizedSummary.toUTF8(), getX(), getY(), GetID());
				}
			}
		}
	}
	else if(getFood() < 0)
	{
		changeFood(-(getFood()));

		if(getPopulation() > 1)
		{
			changePopulation(-1);
		}
	}
}

//	--------------------------------------------------------------------------------
bool CvCity::doCheckProduction()
{
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doCheckProduction, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	VALIDATE_OBJECT
	OrderData* pOrderNode;
	UnitTypes eUpgradeUnit;
	int iUpgradeProduction;
	int iProductionGold;
	int iI;
	bool bOK = true;

	int iMaxedUnitGoldPercent = GC.getMAXED_UNIT_GOLD_PERCENT();
	int iMaxedBuildingGoldPercent = GC.getMAXED_BUILDING_GOLD_PERCENT();
	int iMaxedProjectGoldPercent = GC.getMAXED_PROJECT_GOLD_PERCENT();

	CvPlayerAI& thisPlayer = GET_PLAYER(getOwner());

	int iNumUnitInfos = GC.getNumUnitInfos();
	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_Unit, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
		for(iI = 0; iI < iNumUnitInfos; iI++)
		{
			const UnitTypes eUnit = static_cast<UnitTypes>(iI);
			CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
			if(pkUnitInfo)
			{
				int iUnitProduction = getUnitProduction(eUnit);
				if(iUnitProduction > 0)
				{
					if(thisPlayer.isProductionMaxedUnitClass((UnitClassTypes)(pkUnitInfo)->GetUnitClassType()))
					{
						iProductionGold = ((iUnitProduction * iMaxedUnitGoldPercent) / 100);

						if(iProductionGold > 0)
						{
							thisPlayer.GetTreasury()->ChangeGold(iProductionGold);

							if(getOwner() == GC.getGame().getActivePlayer())
							{
								Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_LOST_WONDER_PROD_CONVERTED");
								localizedText << getNameKey() << GC.getUnitInfo((UnitTypes)iI)->GetTextKey() << iProductionGold;
								DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8());
							}
						}

						setUnitProduction(((UnitTypes)iI), 0);
					}
				}
			}
		}
	}

	int iNumBuildingInfos = GC.getNumBuildingInfos();
	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_Building, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );

		int iPlayerLoop;
		PlayerTypes eLoopPlayer;

		for(iI = 0; iI < iNumBuildingInfos; iI++)
		{
			const BuildingTypes eExpiredBuilding = static_cast<BuildingTypes>(iI);
			CvBuildingEntry* pkExpiredBuildingInfo = GC.getBuildingInfo(eExpiredBuilding);

			//skip if null
			if(pkExpiredBuildingInfo == NULL)
				continue;

			int iBuildingProduction = m_pCityBuildings->GetBuildingProduction(eExpiredBuilding);
			if(iBuildingProduction > 0)
			{
				const BuildingClassTypes eExpiredBuildingClass = (BuildingClassTypes)(pkExpiredBuildingInfo->GetBuildingClassType());

				if(thisPlayer.isProductionMaxedBuildingClass(eExpiredBuildingClass))
				{
					// Beaten to a world wonder by someone?
					if(isWorldWonderClass(pkExpiredBuildingInfo->GetBuildingClassInfo()))
					{
						for(iPlayerLoop = 0; iPlayerLoop < MAX_MAJOR_CIVS; iPlayerLoop++)
						{
							eLoopPlayer = (PlayerTypes) iPlayerLoop;

							// Found the culprit
							if(GET_PLAYER(eLoopPlayer).getBuildingClassCount(eExpiredBuildingClass) > 0)
							{
								GET_PLAYER(getOwner()).GetDiplomacyAI()->ChangeNumWondersBeatenTo(eLoopPlayer, 1);
								break;
							}
						}

						auto_ptr<ICvCity1> pDllCity(new CvDllCity(this));
						DLLUI->AddDeferredWonderCommand(WONDER_REMOVED, pDllCity.get(), (BuildingTypes) eExpiredBuilding, 0);
#if !defined(NO_ACHIEVEMENTS)
						//Add "achievement" for sucking it up
						gDLL->IncrementSteamStatAndUnlock(ESTEAMSTAT_BEATWONDERS, 10, ACHIEVEMENT_SUCK_AT_WONDERS);
#endif
					}

					iProductionGold = ((iBuildingProduction * iMaxedBuildingGoldPercent) / 100);
#if defined(MOD_BALANCE_CORE_BUILDING_INVESTMENTS)
					const BuildingClassTypes eWonderClass = (BuildingClassTypes)pkExpiredBuildingInfo->GetBuildingClassType();
					if(MOD_BALANCE_CORE_BUILDING_INVESTMENTS && IsBuildingInvestment(eWonderClass) && isWorldWonderClass(pkExpiredBuildingInfo->GetBuildingClassInfo()))
					{
						iProductionGold += ((25 * iMaxedBuildingGoldPercent) / 100);
					}
#endif
					if(iProductionGold > 0 && isWorldWonderClass(pkExpiredBuildingInfo->GetBuildingClassInfo()))
					{
#if defined(MOD_BALANCE_CORE_WONDERS_VARIABLE_REWARD)
						if(MOD_BALANCE_CORE_WONDERS_VARIABLE_REWARD && GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() != 0)
						{					
							int iEra = thisPlayer.GetCurrentEra();
							if(iEra <= 0)
							{
								iEra = 1;
							}
							if(GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() == 1)
							{
								//Wonders converted into Gold (default).
								thisPlayer.GetTreasury()->ChangeGold(iProductionGold * iEra);
							}
							if(GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() == 2)
							{
								//Wonders converted into Culture Points.
								iProductionGold = (iProductionGold * GC.getBALANCE_CULTURE_PERCENTAGE_VALUE()) / 100;
								thisPlayer.changeJONSCulture(iProductionGold * iEra);
								ChangeJONSCultureStored(iProductionGold * iEra);
							}
							if(GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() == 3)
							{
								//Wonders Converted into Golden Age Points.
								iProductionGold = (iProductionGold * GC.getBALANCE_GA_PERCENTAGE_VALUE()) / 100;
								thisPlayer.ChangeGoldenAgeProgressMeter(iProductionGold * iEra);
							}
							if(GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() == 4)
							{
								//Wonders Converted into Science Points
								iProductionGold = (iProductionGold * GC.getBALANCE_SCIENCE_PERCENTAGE_VALUE()) / 100;
								int iBeakersBonus = thisPlayer.GetScienceYieldFromPreviousTurns(GC.getGame().getGameTurn(), iProductionGold);
								if(iBeakersBonus > 0)
								{
									TechTypes eCurrentTech = thisPlayer.GetPlayerTechs()->GetCurrentResearch();
									if(eCurrentTech == NO_TECH)
									{
										thisPlayer.changeOverflowResearch(iBeakersBonus * iEra);
									}
									else
									{
										GET_TEAM(thisPlayer.getTeam()).GetTeamTechs()->ChangeResearchProgress(eCurrentTech, (iBeakersBonus * iEra), thisPlayer.GetID());
									}
								}
							}
							if(GC.getBALANCE_WONDER_BEATEN_CONSOLATION_PRIZE() == 5)
							{
								//Wonders Converted into Faith Points
								iProductionGold = (iProductionGold * GC.getBALANCE_FAITH_PERCENTAGE_VALUE()) / 100;
								thisPlayer.ChangeFaith(iProductionGold * iEra);
							}
						}
						else
						{
#endif
						thisPlayer.GetTreasury()->ChangeGold(iProductionGold);
#if defined(MOD_BALANCE_CORE_WONDERS_VARIABLE_REWARD)
						}
#endif

						if(getOwner() == GC.getGame().getActivePlayer())
						{
							Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_LOST_WONDER_PROD_CONVERTED");
							localizedText << getNameKey() << pkExpiredBuildingInfo->GetTextKey() << iProductionGold;
							DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8());
						}
					}

					m_pCityBuildings->SetBuildingProduction(eExpiredBuilding, 0);
				}
			}
		}
	}

	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_Project, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
		int iNumProjectInfos = GC.getNumProjectInfos();
		for(iI = 0; iI < iNumProjectInfos; iI++)
		{
			int iProjectProduction = getProjectProduction((ProjectTypes)iI);
			if(iProjectProduction > 0)
			{
				if(thisPlayer.isProductionMaxedProject((ProjectTypes)iI))
				{
					iProductionGold = ((iProjectProduction * iMaxedProjectGoldPercent) / 100);

					if(iProductionGold > 0)
					{
						thisPlayer.GetTreasury()->ChangeGold(iProductionGold);

						if(getOwner() == GC.getGame().getActivePlayer())
						{
							Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_LOST_WONDER_PROD_CONVERTED");
							localizedText << getNameKey() << GC.getProjectInfo((ProjectTypes)iI)->GetTextKey() << iProductionGold;
							DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8());
						}
					}

					setProjectProduction(((ProjectTypes)iI), 0);
				}
			}
		}
	}

	if(!isProduction() && isHuman() && !isProductionAutomated() && !IsIgnoreCityForHappiness())
	{
		chooseProduction();
		return bOK;
	}

	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_UpgradeUnit, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
		// Can now construct an Upgraded version of this Unit
		for(iI = 0; iI < iNumUnitInfos; iI++)
		{
			if(getFirstUnitOrder((UnitTypes)iI) != -1)
			{
				// If we can still actually train this Unit type then don't auto-upgrade it yet
				if(canTrain((UnitTypes)iI, true))
				{
					continue;
				}

				eUpgradeUnit = allUpgradesAvailable((UnitTypes)iI);

				if(eUpgradeUnit != NO_UNIT)
				{
					CvAssertMsg(eUpgradeUnit != iI, "Trying to upgrade a Unit to itself");
					iUpgradeProduction = getUnitProduction((UnitTypes)iI);
					setUnitProduction(((UnitTypes)iI), 0);
					setUnitProduction(eUpgradeUnit, iUpgradeProduction);

					pOrderNode = headOrderQueueNode();

					while(pOrderNode != NULL)
					{
						if(pOrderNode->eOrderType == ORDER_TRAIN)
						{
							if(pOrderNode->iData1 == iI)
							{
								thisPlayer.changeUnitClassMaking(((UnitClassTypes)(GC.getUnitInfo((UnitTypes)(pOrderNode->iData1))->GetUnitClassType())), -1);
								pOrderNode->iData1 = eUpgradeUnit;
								thisPlayer.changeUnitClassMaking(((UnitClassTypes)(GC.getUnitInfo((UnitTypes)(pOrderNode->iData1))->GetUnitClassType())), 1);
							}
						}

						pOrderNode = nextOrderQueueNode(pOrderNode);
					}
				}
			}
		}
	}

	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_UpgradeBuilding, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
		// Can now construct an Upgraded version of this Building
		for(iI = 0; iI < iNumBuildingInfos; iI++)
		{
			const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
			if(pkBuildingInfo)
			{
				if(getFirstBuildingOrder(eBuilding) != -1)
				{
					BuildingClassTypes eBuildingClass = (BuildingClassTypes) pkBuildingInfo->GetReplacementBuildingClass();

					if(eBuildingClass != NO_BUILDINGCLASS)
					{
						BuildingTypes eUpgradeBuilding = ((BuildingTypes)(thisPlayer.getCivilizationInfo().getCivilizationBuildings(eBuildingClass)));

						if(canConstruct(eUpgradeBuilding))
						{
							CvAssertMsg(eUpgradeBuilding != iI, "Trying to upgrade a Building to itself");
							iUpgradeProduction = m_pCityBuildings->GetBuildingProduction(eBuilding);
							m_pCityBuildings->SetBuildingProduction((eBuilding), 0);
							m_pCityBuildings->SetBuildingProduction(eUpgradeBuilding, iUpgradeProduction);

							pOrderNode = headOrderQueueNode();

							while(pOrderNode != NULL)
							{
								if(pOrderNode->eOrderType == ORDER_CONSTRUCT)
								{
									if(pOrderNode->iData1 == iI)
									{
										CvBuildingEntry* pkOrderBuildingInfo = GC.getBuildingInfo((BuildingTypes)pOrderNode->iData1);
										CvBuildingEntry* pkUpgradeBuildingInfo = GC.getBuildingInfo(eUpgradeBuilding);

										if(NULL != pkOrderBuildingInfo && NULL != pkUpgradeBuildingInfo)
										{
											const BuildingClassTypes eOrderBuildingClass = (BuildingClassTypes)pkOrderBuildingInfo->GetBuildingClassType();
											const BuildingClassTypes eUpgradeBuildingClass = (BuildingClassTypes)pkUpgradeBuildingInfo->GetBuildingClassType();

											thisPlayer.changeBuildingClassMaking(eOrderBuildingClass, -1);
											pOrderNode->iData1 = eUpgradeBuilding;
											thisPlayer.changeBuildingClassMaking(eUpgradeBuildingClass, 1);

										}
									}
								}

								pOrderNode = nextOrderQueueNode(pOrderNode);
							}
						}
					}
				}
			}
		}
	}

	{
		AI_PERF_FORMAT_NESTED("City-AI-perf.csv", ("CvCity::doCheckProduction_CleanupQueue, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
		bOK = CleanUpQueue();
	}

	return bOK;
}


//	--------------------------------------------------------------------------------
void CvCity::doProduction(bool bAllowNoProduction)
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doProduction, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );

	if(!isHuman() || isProductionAutomated())
	{
		if(!isProduction() || isProductionProcess() || AI_isChooseProductionDirty())
		{
			AI_chooseProduction(false /*bInterruptWonders*/, false);
		}
	}

	if(!bAllowNoProduction && !isProduction())
	{
		return;
	}

	if(isProduction())
	{

		if(isProductionBuilding())
		{
			const OrderData* pOrderNode = headOrderQueueNode();
			int iData1 = -1;
			if(pOrderNode != NULL)
			{
				iData1 = pOrderNode->iData1;
			}

			const BuildingTypes eBuilding = static_cast<BuildingTypes>(iData1);
			CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
			if(pkBuildingInfo)
			{
				if(isWorldWonderClass(pkBuildingInfo->GetBuildingClassInfo()))
				{
					if(m_pCityBuildings->GetBuildingProduction(eBuilding) == 0)  // otherwise we are probably already showing this
					{
						auto_ptr<ICvCity1> pDllCity(new CvDllCity(this));
						DLLUI->AddDeferredWonderCommand(WONDER_CREATED, pDllCity.get(), eBuilding, 0);
					}
				}
			}
		}

		changeProductionTimes100(getCurrentProductionDifferenceTimes100(false, true));

#if defined(MOD_PROCESS_STOCKPILE)
		if (!(MOD_PROCESS_STOCKPILE && isProductionProcess()))
#endif
		setOverflowProduction(0);
		setFeatureProduction(0);

#if defined(MOD_PROCESS_STOCKPILE)
		if(getProduction() >= getProductionNeeded())
#else
		if(getProduction() >= getProductionNeeded() && !isProductionProcess())
#endif
		{
#if defined(MOD_PROCESS_STOCKPILE)
			popOrder(0, !isProductionProcess(), true);
#else
			popOrder(0, true, true);
#endif
		}
	}
	else
	{
		changeOverflowProductionTimes100(getCurrentProductionDifferenceTimes100(false, false));
	}
}


//	--------------------------------------------------------------------------------
void CvCity::doProcess()
{
	ProcessTypes eProcess = getProductionProcess();
	CvAssertMsg(eProcess != NO_PROCESS, "Invalid Process for city production. Please send Anton your save file and version.");
	if (eProcess == NO_PROCESS) return;

	// Contribute production to a League project
	for(int iI = 0; iI < GC.getNumLeagueProjectInfos(); iI++)
	{
		LeagueProjectTypes eLeagueProject = (LeagueProjectTypes) iI;
		CvLeagueProjectEntry* pInfo = GC.getLeagueProjectInfo(eLeagueProject);
		if (pInfo)
		{
			if (pInfo->GetProcess() == eProcess)
			{
				GC.getGame().GetGameLeagues()->DoLeagueProjectContribution(getOwner(), eLeagueProject, getCurrentProductionDifferenceTimes100(false, true));
			}
		}
	}
	
#if defined(MOD_PROCESS_STOCKPILE)
	if (MOD_PROCESS_STOCKPILE && eProcess == GC.getInfoTypeForString("PROCESS_STOCKPILE"))
	{
		int iPile = getCurrentProductionDifferenceTimes100(false, false);
		// Can't use changeOverflowProductionTimes100() here as it asserts above 250 production
		setOverflowProductionTimes100(getOverflowProductionTimes100() + iPile);
		CUSTOMLOG("Adding %i production to the stockpile of %s (for a total of %i)", iPile/100, getName().c_str(), getOverflowProduction());
	}
#endif
}


//	--------------------------------------------------------------------------------
void CvCity::doDecay()
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doDecay, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	int iI;

	int iBuildingProductionDecayTime = GC.getBUILDING_PRODUCTION_DECAY_TIME();
	int iBuildingProductionDecayPercent = GC.getBUILDING_PRODUCTION_DECAY_PERCENT();

	int iNumBuildingInfos = GC.getNumBuildingInfos();
	for(iI = 0; iI < iNumBuildingInfos; iI++)
	{
		if(getProductionBuilding() != ((BuildingTypes)iI))
		{
			if(m_pCityBuildings->GetBuildingProduction((BuildingTypes)iI) > 0)
			{
				m_pCityBuildings->ChangeBuildingProductionTime(((BuildingTypes)iI), 1);

				if(isHuman())
				{
					if(m_pCityBuildings->GetBuildingProductionTime((BuildingTypes)iI) > iBuildingProductionDecayTime)
					{
						m_pCityBuildings->SetBuildingProduction(((BuildingTypes)iI), ((m_pCityBuildings->GetBuildingProduction((BuildingTypes)iI) * iBuildingProductionDecayPercent) / 100));
					}
				}
			}
			else
			{
				m_pCityBuildings->SetBuildingProductionTime(((BuildingTypes)iI), 0);
			}
		}
	}

	int iUnitProductionDecayTime = GC.getUNIT_PRODUCTION_DECAY_TIME();
	int iUnitProductionDecayPercent = GC.getUNIT_PRODUCTION_DECAY_PERCENT();

	int iNumUnitInfos = GC.getNumUnitInfos();
	for(iI = 0; iI < iNumUnitInfos; iI++)
	{
		const UnitTypes eUnit = static_cast<UnitTypes>(iI);
		CvUnitEntry* pkUnitInfo = GC.getUnitInfo(eUnit);
		if(pkUnitInfo)
		{
			if(getProductionUnit() != eUnit)
			{
				if(getUnitProduction(eUnit) > 0)
				{
					changeUnitProductionTime(eUnit, 1);

					if(isHuman())
					{
						if(getUnitProductionTime(eUnit) > iUnitProductionDecayTime)
						{
							setUnitProduction(eUnit, ((getUnitProduction(eUnit) * iUnitProductionDecayPercent) / 100));
						}
					}
				}
				else
				{
					setUnitProductionTime(eUnit, 0);
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
void CvCity::doMeltdown()
{
	VALIDATE_OBJECT
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::doMeltdown, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );

	int iNumBuildingInfos = GC.getNumBuildingInfos();
	for(int iI = 0; iI < iNumBuildingInfos; iI++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iI);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
		if(NULL != pkBuildingInfo && m_pCityBuildings->GetNumBuilding((BuildingTypes)iI) > 0)
		{
			if(pkBuildingInfo->GetNukeExplosionRand() != 0)
			{
				if(GC.getGame().getJonRandNum(pkBuildingInfo->GetNukeExplosionRand(), "Meltdown!!!") == 0)
				{
					if(m_pCityBuildings->GetNumRealBuilding((BuildingTypes)iI) > 0)
					{
						m_pCityBuildings->SetNumRealBuilding(((BuildingTypes)iI), 0);
					}

					CvUnitCombat::ApplyNuclearExplosionDamage(plot(), 1);

					if(getOwner() == GC.getGame().getActivePlayer())
					{
						Localization::String localizedText = Localization::Lookup("TXT_KEY_MISC_MELTDOWN_CITY");
						localizedText << getNameKey();

						DLLUI->AddCityMessage(0, GetIDInfo(), getOwner(), false, GC.getEVENT_MESSAGE_TIME(), localizedText.toUTF8()/*, "AS2D_MELTDOWN", MESSAGE_TYPE_MINOR_EVENT, NULL, (ColorTypes)GC.getInfoTypeForString("COLOR_RED"), getX(), getY(), true, true*/);
					}

					break;
				}
			}
		}
	}
}

//	--------------------------------------------------------------------------------
CvCityStrategyAI* CvCity::GetCityStrategyAI() const
{
	VALIDATE_OBJECT
	return m_pCityStrategyAI;
}

//	--------------------------------------------------------------------------------
CvCityCitizens* CvCity::GetCityCitizens() const
{
	VALIDATE_OBJECT
	return m_pCityCitizens;
}

//	--------------------------------------------------------------------------------
CvCityReligions* CvCity::GetCityReligions() const
{
	VALIDATE_OBJECT
	return m_pCityReligions;
}

//	--------------------------------------------------------------------------------
CvCityEmphases* CvCity::GetCityEmphases() const
{
	VALIDATE_OBJECT
	return m_pEmphases;
}

//	--------------------------------------------------------------------------------
CvCityEspionage* CvCity::GetCityEspionage() const
{
	VALIDATE_OBJECT
	return m_pCityEspionage;
}

//	--------------------------------------------------------------------------------
CvCityCulture* CvCity::GetCityCulture() const
{
	VALIDATE_OBJECT
		return m_pCityCulture;
}

// Private Functions...

//	--------------------------------------------------------------------------------
void CvCity::read(FDataStream& kStream)
{
	VALIDATE_OBJECT
	// Init data before load
	reset();

	// Version number to maintain backwards compatibility
	uint uiVersion;
	kStream >> uiVersion;
	MOD_SERIALIZE_INIT_READ(kStream);

#if defined(MOD_BALANCE_CORE)
	kStream >> m_syncArchive;
	//Values below deleted, as they're already in the sync archive!
#endif

	m_pCityBuildings->Read(kStream);

	UINT uLength;
	kStream >> uLength;
	for(UINT uIdx = 0; uIdx < uLength; ++uIdx)
	{
		OrderData  Data;

		kStream >> Data.eOrderType;
		if (uiVersion >= 5)
		{
			// Translate the data
			switch (Data.eOrderType)
			{
			case ORDER_TRAIN:
				Data.iData1 = CvInfosSerializationHelper::ReadHashed(kStream);
				kStream >> Data.iData2;		// This is a UnitAIType, but no code respects the ordering in GC.getUnitAIInfo
				break;

			case ORDER_CONSTRUCT:
				Data.iData1 = CvInfosSerializationHelper::ReadHashed(kStream);
				kStream >> Data.iData2;
				break;

			case ORDER_CREATE:
				Data.iData1 = CvInfosSerializationHelper::ReadHashed(kStream);
				kStream >> Data.iData2;
				break;

			case ORDER_PREPARE:
				Data.iData1 = CvInfosSerializationHelper::ReadHashed(kStream);
				kStream >> Data.iData2;
				break;

			case ORDER_MAINTAIN:
				Data.iData1 = CvInfosSerializationHelper::ReadHashed(kStream);
				kStream >> Data.iData2;
				break;

			default:
				CvAssertMsg(false, "pData->eOrderType failed to match a valid option");
				kStream >> Data.iData1;
				kStream >> Data.iData2;
				break;
			}
		}
		else
		{
			kStream >> Data.iData1;
			kStream >> Data.iData2;
		}

		kStream >> Data.bSave;
		kStream >> Data.bRush;

		bool bIsValid = false;
		switch (Data.eOrderType)
		{
			case ORDER_TRAIN:
				bIsValid = GC.getUnitInfo( (UnitTypes)Data.iData1 ) != NULL;
				CvAssertMsg(bIsValid, "Unit in build queue is invalid");
				break;

			case ORDER_CONSTRUCT:
				bIsValid = GC.getBuildingInfo( (BuildingTypes)Data.iData1 ) != NULL;
				CvAssertMsg(bIsValid, "Building in build queue is invalid");
				break;

			case ORDER_CREATE:
				bIsValid = GC.getProjectInfo( (ProjectTypes)Data.iData1 ) != NULL;
				CvAssertMsg(bIsValid, "Project in build queue is invalid");
				break;

			case ORDER_PREPARE:
				bIsValid = GC.getSpecialistInfo( (SpecialistTypes)Data.iData1 ) != NULL;
				CvAssertMsg(bIsValid, "Specialize in build queue is invalid");
				break;

			case ORDER_MAINTAIN:
				bIsValid = GC.getProcessInfo( (ProcessTypes)Data.iData1 ) != NULL;
				CvAssertMsg(bIsValid, "Process in build queue is invalid");
				break;
		}

		if (bIsValid)
			m_orderQueue.insertAtEnd(&Data);
	}

	m_pCityStrategyAI->Read(kStream);
	if(m_eOwner != NO_PLAYER)
	{
		GET_PLAYER(getOwner()).GetFlavorManager()->AddFlavorRecipient(m_pCityStrategyAI, false /* bPropogateFlavorValues */);
	}
	m_pCityCitizens->Read(kStream);
	kStream >> *m_pCityReligions;
	m_pEmphases->Read(kStream);

	kStream >> *m_pCityEspionage;

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiResourceYieldChange, NUM_YIELD_TYPES, GC.getNumResourceInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiFeatureYieldChange, NUM_YIELD_TYPES, GC.getNumFeatureInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiTerrainYieldChange, NUM_YIELD_TYPES, GC.getNumTerrainInfos());

#if defined(MOD_BALANCE_CORE)
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiPlotYieldChange, NUM_YIELD_TYPES, GC.getNumPlotInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiYieldPerXTerrainFromBuildings, NUM_YIELD_TYPES, GC.getNumTerrainInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiYieldPerXTerrainFromReligion, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiYieldPerXTerrain, NUM_YIELD_TYPES, GC.getNumTerrainInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiYieldPerXFeature, NUM_YIELD_TYPES, GC.getNumFeatureInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiYieldPerXUnimprovedFeature, NUM_YIELD_TYPES, GC.getNumFeatureInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiReligionBuildingYieldRateModifier, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiLocalBuildingClassYield, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());
	
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiImprovementYieldChange, NUM_YIELD_TYPES, GC.getNumImprovementInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventBuildingClassYield, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventBuildingClassYieldModifier, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());

	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventImprovementYield, NUM_YIELD_TYPES, GC.getNumImprovementInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventResourceYield, NUM_YIELD_TYPES, GC.getNumResourceInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventTerrainYield, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventFeatureYield, NUM_YIELD_TYPES, GC.getNumFeatureInfos());
	CvInfosSerializationHelper::ReadHashedDataArray(kStream, m_ppaiEventSpecialistYield, NUM_YIELD_TYPES, GC.getNumSpecialistInfos());
#endif

	CvCityManager::OnCityCreated(this);
}

//	--------------------------------------------------------------------------------
void CvCity::write(FDataStream& kStream) const
{
VALIDATE_OBJECT

	// Current version number
	uint uiVersion = 6;
	kStream << uiVersion;
	MOD_SERIALIZE_INIT_WRITE(kStream);
#if defined(MOD_BALANCE_CORE)
	kStream << m_syncArchive;
	//Values below deleted, as they're already in the sync archive!
#endif

	m_pCityBuildings->Write(kStream);

	//  Write m_orderQueue
	UINT uLength = (UINT)m_orderQueue.getLength();
	kStream << uLength;
	for(UINT uIdx = 0; uIdx < uLength; ++uIdx)
	{
		OrderData* pData = m_orderQueue.getAt(uIdx);

		kStream << pData->eOrderType;
		// Now we have to translate the data because most of them contain indices into Infos tables and it is very bad to save an index since the table order can change
		switch (pData->eOrderType)
		{
			case ORDER_TRAIN:
				CvInfosSerializationHelper::WriteHashed(kStream, (UnitTypes)(pData->iData1));
				kStream << pData->iData2;	// This is a UnitAIType, but no code respects the ordering in GC.getUnitAIInfo so just write out the index
				break;

			case ORDER_CONSTRUCT:
				CvInfosSerializationHelper::WriteHashed(kStream, (BuildingTypes)pData->iData1);
				kStream << pData->iData2;
				break;

			case ORDER_CREATE:
				CvInfosSerializationHelper::WriteHashed(kStream, (ProjectTypes)pData->iData1);
				kStream << pData->iData2;
				break;

			case ORDER_PREPARE:
				CvInfosSerializationHelper::WriteHashed(kStream, (SpecialistTypes)pData->iData1);
				kStream << pData->iData2;
				break;

			case ORDER_MAINTAIN:
				CvInfosSerializationHelper::WriteHashed(kStream, (ProcessTypes)pData->iData1);
				kStream << pData->iData2;
				break;

			default:
				CvAssertMsg(false, "pData->eOrderType failed to match a valid option");
				kStream << pData->iData1;
				kStream << pData->iData2;
				break;
		}
		kStream << pData->bSave;
		kStream << pData->bRush;
	}

	m_pCityStrategyAI->Write(kStream);
	m_pCityCitizens->Write(kStream);
	kStream << *m_pCityReligions;
	m_pEmphases->Write(kStream);
	kStream << *m_pCityEspionage;

	CvInfosSerializationHelper::WriteHashedDataArray<ResourceTypes>(kStream, m_ppaiResourceYieldChange, NUM_YIELD_TYPES, GC.getNumResourceInfos());

	CvInfosSerializationHelper::WriteHashedDataArray<FeatureTypes>(kStream, m_ppaiFeatureYieldChange, NUM_YIELD_TYPES, GC.getNumFeatureInfos());

	CvInfosSerializationHelper::WriteHashedDataArray<TerrainTypes>(kStream, m_ppaiTerrainYieldChange, NUM_YIELD_TYPES, GC.getNumTerrainInfos());

#if defined(MOD_BALANCE_CORE)
	CvInfosSerializationHelper::WriteHashedDataArray<PlotTypes>(kStream, m_ppaiPlotYieldChange, NUM_YIELD_TYPES, GC.getNumPlotInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<TerrainTypes>(kStream, m_ppaiYieldPerXTerrainFromBuildings, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<TerrainTypes>(kStream, m_ppaiYieldPerXTerrainFromReligion, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<TerrainTypes>(kStream, m_ppaiYieldPerXTerrain, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<FeatureTypes>(kStream, m_ppaiYieldPerXFeature, NUM_YIELD_TYPES, GC.getNumFeatureInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<FeatureTypes>(kStream, m_ppaiYieldPerXUnimprovedFeature, NUM_YIELD_TYPES, GC.getNumFeatureInfos());
	
	CvInfosSerializationHelper::WriteHashedDataArray<BuildingClassTypes>(kStream, m_ppaiReligionBuildingYieldRateModifier, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<BuildingClassTypes>(kStream, m_ppaiLocalBuildingClassYield, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());

	CvInfosSerializationHelper::WriteHashedDataArray<ImprovementTypes>(kStream, m_ppaiImprovementYieldChange, NUM_YIELD_TYPES, GC.getNumImprovementInfos());

	CvInfosSerializationHelper::WriteHashedDataArray<BuildingClassTypes>(kStream, m_ppaiEventBuildingClassYield, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<BuildingClassTypes>(kStream, m_ppaiEventBuildingClassYieldModifier, NUM_YIELD_TYPES, GC.getNumBuildingClassInfos());

	CvInfosSerializationHelper::WriteHashedDataArray<ImprovementTypes>(kStream, m_ppaiEventImprovementYield, NUM_YIELD_TYPES, GC.getNumImprovementInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<ResourceTypes>(kStream, m_ppaiEventResourceYield, NUM_YIELD_TYPES, GC.getNumResourceInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<TerrainTypes>(kStream, m_ppaiEventTerrainYield, NUM_YIELD_TYPES, GC.getNumTerrainInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<FeatureTypes>(kStream, m_ppaiEventFeatureYield, NUM_YIELD_TYPES, GC.getNumFeatureInfos());
	CvInfosSerializationHelper::WriteHashedDataArray<FeatureTypes>(kStream, m_ppaiEventSpecialistYield, NUM_YIELD_TYPES, GC.getNumSpecialistInfos());
#endif
}


//	--------------------------------------------------------------------------------
bool CvCity::isValidBuildingLocation(BuildingTypes eBuilding) const
{
	VALIDATE_OBJECT

	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return false;

	// Requires coast
	if(pkBuildingInfo->IsWater())
	{
		if(!isCoastal(pkBuildingInfo->GetMinAreaSize()))
			return false;
	}

	// Requires River
	if(pkBuildingInfo->IsRiver())
	{
		if(!(plot()->isRiver()))
			return false;
	}

	// Requires Fresh Water
	if(pkBuildingInfo->IsFreshWater())
	{
		if(!plot()->isFreshWater_cached())
			return false;
	}
#if defined(MOD_BALANCE_CORE)
	//Requires no fresh water
	if(MOD_BALANCE_CORE)
	{
		if(pkBuildingInfo->IsNoWater())
		{
			if(plot()->isFreshWater_cached())
			return false;
		}
		if(pkBuildingInfo->IsNoRiver())
		{
			if(plot()->isRiver())
			return false;
		}
		//Capital Only
		if(pkBuildingInfo->IsCapitalOnly())
		{
			if(!isCapital())
			{
				return false;
			}
		}
	}
#endif
	// Requires adjacent Mountain
	if(pkBuildingInfo->IsMountain())
	{
		bool bFoundMountain = false;

		CvPlot* pAdjacentPlot;
		for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; iDirectionLoop++)
		{
			pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));

			if(pAdjacentPlot != NULL)
			{
				if(pAdjacentPlot->isMountain())
				{
					bFoundMountain = true;
					break;
				}
			}
		}

		if(!bFoundMountain)
			return false;
	}

	// Requires nearby Mountain (within 2 tiles)
	if(pkBuildingInfo->IsNearbyMountainRequired())
	{
		bool bFoundMountain = false;

		const int iMountainRange = 2;
		CvPlot* pLoopPlot;

		for(int iDX = -iMountainRange; iDX <= iMountainRange; iDX++)
		{
			for(int iDY = -iMountainRange; iDY <= iMountainRange; iDY++)
			{
				pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iMountainRange);
				if(pLoopPlot)
				{
#if defined(MOD_PSEUDO_NATURAL_WONDER)
					if(pLoopPlot->isMountain() && !pLoopPlot->IsNaturalWonder(true) && pLoopPlot->getOwner() == getOwner())
#else
					if(pLoopPlot->isMountain() && !pLoopPlot->IsNaturalWonder() && pLoopPlot->getOwner() == getOwner())
#endif
					{
						bFoundMountain = true;
						break;
					}
				}
			}

			if(bFoundMountain == true)
				break;
		}

		if(!bFoundMountain)
			return false;
	}

	// Requires Hills
	if(pkBuildingInfo->IsHill())
	{
		if(!plot()->isHills())
			return false;
	}

	// Requires Flat
	if(pkBuildingInfo->IsFlat())
	{
		if(plot()->isHills())
			return false;
	}

	// Requires city not built on certain terrain?
	TerrainTypes eTerrainProhibited = (TerrainTypes) pkBuildingInfo->GetProhibitedCityTerrain();
	if(eTerrainProhibited != NO_TERRAIN)
	{
		if(plot()->getTerrainType() == eTerrainProhibited)
		{
			return false;
		}
	}

	// Requires city to be on or next to a particular terrain type?
	TerrainTypes eTerrainRequired = (TerrainTypes) pkBuildingInfo->GetNearbyTerrainRequired();
	if(eTerrainRequired != NO_TERRAIN)
	{
		bool bFoundTerrain = false;

		// City on the right terrain?
		if(plot()->getTerrainType() == eTerrainRequired)
			bFoundTerrain = true;

		// Check adjacent plots
		if(!bFoundTerrain)
		{
			CvPlot* pAdjacentPlot;
			for(int iDirectionLoop = 0; iDirectionLoop < NUM_DIRECTION_TYPES; iDirectionLoop++)
			{
				pAdjacentPlot = plotDirection(getX(), getY(), ((DirectionTypes)iDirectionLoop));

				if(pAdjacentPlot != NULL)
				{
					// City adjacent to the right terrain?
					if(pAdjacentPlot->getTerrainType() == eTerrainRequired)
					{
						bFoundTerrain = true;
						break;
					}
				}
			}
		}

		// Didn't find nearby required terrain
		if(!bFoundTerrain)
			return false;
	}
#if defined(MOD_BALANCE_CORE)
	if(pkBuildingInfo->IsAnyBodyOfWater())
	{
		if(plot()->isFreshWater_cached() || isCoastal(pkBuildingInfo->GetMinAreaSize()))
		{
			return true;
		}
		else
		{
			return false;
		}
	}
#endif
	return true;
}


// CACHE: cache frequently used values
///////////////////////////////////////

//	--------------------------------------------------------------------------------
void CvCity::invalidatePopulationRankCache()
{
	VALIDATE_OBJECT
	m_bPopulationRankValid = false;
}

//	--------------------------------------------------------------------------------
void CvCity::invalidateYieldRankCache(YieldTypes eYield)
{
	VALIDATE_OBJECT
	CvAssertMsg(eYield >= NO_YIELD && eYield < NUM_YIELD_TYPES, "invalidateYieldRankCache passed bogus yield index");

	if(eYield == NO_YIELD)
	{
		for(int iI = 0; iI < NUM_YIELD_TYPES; iI++)
		{
			m_abBaseYieldRankValid.setAt(iI, false);
			m_abYieldRankValid.setAt(iI, false);
		}
	}
	else
	{
		m_abBaseYieldRankValid.setAt(eYield, false);
		m_abYieldRankValid.setAt(eYield, false);
	}
}

//	--------------------------------------------------------------------------------
bool CvCity::isMadeAttack() const
{
	VALIDATE_OBJECT
	return m_bMadeAttack;
}

//	--------------------------------------------------------------------------------
void CvCity::setMadeAttack(bool bNewValue)
{
	VALIDATE_OBJECT
	m_bMadeAttack = bNewValue;
}

#if defined(MOD_EVENTS_CITY_BOMBARD)
//	--------------------------------------------------------------------------------
int CvCity::getBombardRange() const
{
	bool bIndirectFireAllowed;
	return getBombardRange(bIndirectFireAllowed);
}

//	--------------------------------------------------------------------------------
int CvCity::getBombardRange(bool& bIndirectFireAllowed) const
{
	VALIDATE_OBJECT
	
	if (MOD_EVENTS_CITY_BOMBARD) {
		int iValue = 0;
		if (GAMEEVENTINVOKE_VALUE(iValue, GAMEEVENT_GetBombardRange, getOwner(), GetID()) == GAMEEVENTRETURN_VALUE) {
			// Defend against modder stupidity!
			if (iValue != 0 && ::abs(iValue) <= GC.getMAX_CITY_ATTACK_RANGE()) {
				bIndirectFireAllowed = (iValue < 0);
				return ::abs(iValue);
			}
		}
	}
	
	bIndirectFireAllowed = GC.getCAN_CITY_USE_INDIRECT_FIRE();
	return GC.getCITY_ATTACK_RANGE();
}
#endif

//	--------------------------------------------------------------------------------
bool CvCity::canRangeStrike() const
{
	VALIDATE_OBJECT

#if !defined(MOD_BALANCE_CORE_MILITARY)
	// Can't shoot more than once per turn
	if(isMadeAttack())
		return false;
#endif
#if defined(MOD_BALANCE_CORE)
	// Can't shoot if it's not our turn
	if(!GET_PLAYER(getOwner()).isTurnActive())
	{
		return false;
	}
#endif

	// Can't shoot when in resistance
	if(IsResistance() || IsRazing())
		return false;

	// Can't shoot if we have no HP left (shouldn't really ever happen)
	if(getDamage() == GetMaxHitPoints())
		return false;

#if !defined(MOD_BALANCE_CORE_MILITARY)
	// Apparently it's possible for someone to fire during another player's turn
	if(!GET_PLAYER(getOwner()).isTurnActive())
		return false;
#endif

	return true;
}

//	--------------------------------------------------------------------------------
bool CvCity::CanRangeStrikeNow() const
{
	if(!canRangeStrike())
	{
		return false;
	}

#if defined(MOD_BALANCE_CORE_MILITARY)
	if (isMadeAttack())
		return false;
#endif

#if defined(MOD_EVENTS_CITY_BOMBARD)
	bool bIndirectFireAllowed; // By reference, yuck!!!
	int iRange = getBombardRange(bIndirectFireAllowed);
#else
	int iRange = GC.getCITY_ATTACK_RANGE();
	bool bIndirectFireAllowed = GC.getCAN_CITY_USE_INDIRECT_FIRE();
#endif
	CvPlot* pPlot = plot();
	int iX = getX();
	int iY = getY();
	TeamTypes eTeam = getTeam();
	PlayerTypes eActivePlayer = GC.getGame().getActivePlayer();
	int iGameTurn = GC.getGame().getGameTurn();

	for(int iDX = -iRange; iDX <= iRange; iDX++)
	{
		for(int iDY = -iRange; iDY <= iRange; iDY++)
		{
			CvPlot* pTargetPlot = plotXYWithRangeCheck(iX, iY, iDX, iDY, iRange);
			if(!pTargetPlot)
			{
				continue;
			}

			bool bCanRangeStrike = true;
			if(!bIndirectFireAllowed)
			{
				if(!pPlot->canSeePlot(pTargetPlot, eTeam, iRange, NO_DIRECTION))
				{
					bCanRangeStrike = false;
				}
			}

			if(bCanRangeStrike)
			{
				if(pTargetPlot->isVisible(eTeam))
				{
					int iTargetPlotX = pTargetPlot->getX();
					int iTargetPlotY = pTargetPlot->getY();
					int iPlotDistance = plotDistance(iX, iY, iTargetPlotX, iTargetPlotY);
					if(iPlotDistance <= iRange)
					{
#if defined(MOD_BALANCE_CORE_MILITARY)
						if(canRangeStrikeAt(iTargetPlotX, iTargetPlotY) && canRangedStrikeTarget(*pTargetPlot)!=NULL)
#else
						if(canRangeStrikeAt(iTargetPlotX, iTargetPlotY))
#endif
						{
							if(m_eOwner == eActivePlayer)
							{
								if(iGameTurn != m_bombardCheckTurn)
								{
									m_bombardCheckTurn = iGameTurn;
								}
							}

							return true;
						}
					}
				}
			}
		}
	}

	return false;
}

//	--------------------------------------------------------------------------------
/// Does this City have a Building that allows it to Range Strike?
bool CvCity::IsHasBuildingThatAllowsRangeStrike() const
{
	VALIDATE_OBJECT
	bool bHasBuildingThatAllowsRangeStrike = false;

	for(int iBuildingLoop = 0; iBuildingLoop < GC.getNumBuildingInfos(); iBuildingLoop++)
	{
		const BuildingTypes eBuilding = static_cast<BuildingTypes>(iBuildingLoop);
		CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);

		if(pkBuildingInfo)
		{
			// Has this Building
			if(GetCityBuildings()->GetNumBuilding(eBuilding) > 0)
			{
				// Does it grant Range Strike ability?
				if(pkBuildingInfo->IsAllowsRangeStrike())
				{
					bHasBuildingThatAllowsRangeStrike = true;
					break;
				}
			}
		}
	}

	return bHasBuildingThatAllowsRangeStrike;
}

//	--------------------------------------------------------------------------------
bool CvCity::canRangeStrikeAt(int iX, int iY) const
{
	VALIDATE_OBJECT
	if(!canRangeStrike())
	{
		return false;
	}

	const CvPlot* pTargetPlot = GC.getMap().plot(iX, iY);

	if(NULL == pTargetPlot)
	{
		return false;
	}

	if(!pTargetPlot->isVisible(getTeam()))
	{
		return false;
	}

#if defined(MOD_EVENTS_CITY_BOMBARD)
	bool bIndirectFireAllowed; // By reference, yuck!!!
	int iAttackRange = getBombardRange(bIndirectFireAllowed);
#else
	int iAttackRange = GC.getCITY_ATTACK_RANGE();
#endif

	if(plotDistance(plot()->getX(), plot()->getY(), pTargetPlot->getX(), pTargetPlot->getY()) > iAttackRange)
	{
		return false;
	}

#if defined(MOD_EVENTS_CITY_BOMBARD)
	if(!bIndirectFireAllowed)
#else
	if(!GC.getCAN_CITY_USE_INDIRECT_FIRE())
#endif
	{
		if(!plot()->canSeePlot(pTargetPlot, getTeam(), iAttackRange, NO_DIRECTION))
		{
			return false;
		}
	}

	// If it's NOT a city, see if there are any units to aim for
	if(!pTargetPlot->isCity())
	{
		if(!canRangedStrikeTarget(*pTargetPlot))
		{
			return false;
		}
	}
	else // I don't want cities attacking each other directly
	{
		return false;
	}

	return true;
}

//	----------------------------------------------------------------------------
CityTaskResult CvCity::rangeStrike(int iX, int iY)
{
	VALIDATE_OBJECT
	CvUnit* pDefender;

	CityTaskResult eResult = TASK_ABORTED;

	CvPlot* pPlot = GC.getMap().plot(iX, iY);
	if(NULL == pPlot)
	{
		return eResult;
	}

#if defined(MOD_BALANCE_CORE_MILITARY)
	if(!canRangeStrikeAt(iX, iY) || rangedStrikeTarget(pPlot)==NULL || isMadeAttack())
#else
	if(!canRangeStrikeAt(iX, iY))
#endif
	{
		return eResult;
	}

	setMadeAttack(true);

	// No City
	if(!pPlot->isCity())
	{
		pDefender = rangedStrikeTarget(pPlot);

		CvAssert(pDefender != NULL);
		if(!pDefender) return TASK_ABORTED;

		CvCombatInfo kCombatInfo;
		CvUnitCombat::GenerateRangedCombatInfo(*this, pDefender, *pPlot, &kCombatInfo);

		uint uiParentEventID = 0;
		if(!CvPreGame::quickCombat())
		{
			// Center camera here!
			bool isTargetVisibleToActivePlayer = pPlot->isActiveVisible(false);
			if(isTargetVisibleToActivePlayer)
			{
				auto_ptr<ICvPlot1> pDllPlot = GC.WrapPlotPointer(pPlot);
				DLLUI->lookAt(pDllPlot.get(), CAMERALOOKAT_NORMAL);
			}

			kCombatInfo.setVisualizeCombat(pPlot->isActiveVisible(false));

			auto_ptr<ICvCombatInfo1> pDllCombatInfo(new CvDllCombatInfo(&kCombatInfo));
			uiParentEventID = gDLL->GameplayCityCombat(pDllCombatInfo.get());

			// Set the combat units so that other missions do not continue until combat is over.
			pDefender->setCombatCity(this);
			setCombatUnit(pDefender);
			eResult = TASK_QUEUED;
		}
		else
		{
			eResult = TASK_COMPLETED;
		}

		CvUnitCombat::ResolveCombat(kCombatInfo, uiParentEventID);
	}

	return eResult;
}

//	--------------------------------------------------------------------------------
bool CvCity::canRangedStrikeTarget(const CvPlot& targetPlot) const
{
	VALIDATE_OBJECT
	return (rangedStrikeTarget(&targetPlot) != 0);
}

//	--------------------------------------------------------------------------------
CvUnit* CvCity::rangedStrikeTarget(const CvPlot* pPlot) const
{
	VALIDATE_OBJECT
	CvUnit* pDefender = pPlot->getBestDefender(NO_PLAYER, getOwner(), NULL, true, false, false, /*bNoncombatAllowed*/ true);

	if(pDefender)
	{
		if(!pDefender->IsDead())
		{
#if defined(MOD_GLOBAL_SUBS_UNDER_ICE_IMMUNITY)
			// If the defender is a sub and the plot is ice, return NULL
			if (pDefender->getInvisibleType() == 0 && pPlot->getFeatureType() == FEATURE_ICE) {
				return NULL;
			}
#endif

			return pDefender;
		}
	}

	return NULL;
}

//	--------------------------------------------------------------------------------
int CvCity::rangeCombatUnitDefense(const CvUnit* pDefender, const CvPlot* pInPlot) const
{
	if (pInPlot == NULL)
		pInPlot = pDefender->plot();

	int iDefenderStrength = 0;

	if (pDefender->CanEverEmbark() && pInPlot->needsEmbarkation(pDefender))
	{
		iDefenderStrength = pDefender->GetEmbarkedUnitDefense();
	}
	// Use Ranged combat value for defender, UNLESS it's a boat or an Impi (ranged support)
#if defined(MOD_BALANCE_CORE)
	//Correction - make this apply to all ranged units, naval too.
	else if (!pDefender->isRangedSupportFire() && pDefender->isRanged())
#else
	else if (!pDefender->isRangedSupportFire() && !pDefender->getDomainType() == DOMAIN_SEA)
#endif
	{
		//have to consider embarkation explicitly
		if ( (!pInPlot && pDefender->isEmbarked()) || (pInPlot && pInPlot->needsEmbarkation(pDefender) && pDefender->CanEverEmbark()) )
			iDefenderStrength = pDefender->GetEmbarkedUnitDefense();
		else
			iDefenderStrength = pDefender->GetMaxRangedCombatStrength(NULL, NULL, false, false, pInPlot, plot());
	}
	else
	{
		iDefenderStrength = pDefender->GetMaxDefenseStrength(pInPlot, NULL, /*bFromRangedAttack*/ true);
	}

	return iDefenderStrength;
}

//	--------------------------------------------------------------------------------
int CvCity::rangeCombatDamage(const CvUnit* pDefender, CvCity* pCity, bool bIncludeRand, const CvPlot* pInPlot) const
{
	VALIDATE_OBJECT
	
	if (pInPlot == NULL)
	{
		if (pDefender != NULL)
		{
			pInPlot = pDefender->plot();
		}
		else if (pCity != NULL)
		{
			pInPlot = pCity->plot();
		}
	}

	int iAttackerStrength = getStrengthValue(true);

#if defined(MOD_BALANCE_CORE)
	//Cities should deal less raw damage to boats - helps naval siege units greatly.
	if(pDefender != NULL)
	{
		if(pDefender->getDomainType() == DOMAIN_SEA)
		{
			iAttackerStrength *= /* 75 */ GC.getBALANCE_NAVAL_DEFENSE_CITY_STRIKE_MODIFIER();
			iAttackerStrength /= 100;
		}
	}
#endif

	int iDefenderStrength = 1;
	if (pCity != NULL)
	{
		iDefenderStrength = pCity->getStrengthValue();
	}
	else if (pDefender != NULL)
	{
		// If this is a defenseless unit, do a fixed amount of damage
		if (!pDefender->IsCanDefend(pInPlot))
		{
			return GC.getNONCOMBAT_UNIT_RANGED_DAMAGE();
		}

		iDefenderStrength = rangeCombatUnitDefense(pDefender, pInPlot);
	}

	// The roll will vary damage between 30 and 40 (out of 100) for two units of identical strength

	int iAttackerDamage = /*250*/ GC.getRANGE_ATTACK_SAME_STRENGTH_MIN_DAMAGE();

	int iAttackerRoll = 0;
	if(bIncludeRand)
	{
		if (isBarbarian())
		{
			iAttackerRoll = GC.getGame().getSmallFakeRandNum(10, GetID()) * 120;
		}
		else
		{
			iAttackerRoll = GC.getGame().getJonRandNum(/*300*/ GC.getRANGE_ATTACK_SAME_STRENGTH_POSSIBLE_EXTRA_DAMAGE(), "City Ranged Attack Damage");
		}
	}
	else
	{
		iAttackerRoll = /*300*/ GC.getRANGE_ATTACK_SAME_STRENGTH_POSSIBLE_EXTRA_DAMAGE();
		iAttackerRoll -= 1;	// Subtract 1 here, because this is the amount normally "lost" when doing a rand roll
		iAttackerRoll /= 2;	// The divide by 2 is to provide the average damage
	}
	iAttackerDamage += iAttackerRoll;


	double fStrengthRatio = (double(iAttackerStrength) / iDefenderStrength);

	// In case our strength is less than the other guy's, we'll do things in reverse then make the ratio 1 over the result
	if(iDefenderStrength > iAttackerStrength)
	{
		fStrengthRatio = (double(iDefenderStrength) / iAttackerStrength);
	}

	fStrengthRatio = (fStrengthRatio + 3) / 4;
	fStrengthRatio = pow(fStrengthRatio, 4.0);
	fStrengthRatio = (fStrengthRatio + 1) / 2;

	if(iDefenderStrength > iAttackerStrength)
	{
		fStrengthRatio = 1 / fStrengthRatio;
	}

	iAttackerDamage = int(iAttackerDamage * fStrengthRatio);

	// Bring it back out of hundreds
	iAttackerDamage /= 100;

	// Always do at least 1 damage
	int iMinDamage = /*1*/ GC.getMIN_CITY_STRIKE_DAMAGE();
	if(iAttackerDamage < iMinDamage)
		iAttackerDamage = iMinDamage;

	return iAttackerDamage;
}

//	--------------------------------------------------------------------------------
int CvCity::GetAirStrikeDefenseDamage(const CvUnit* pAttacker, bool bIncludeRand) const
{
	int iAttackerStrength = pAttacker->GetMaxRangedCombatStrength(NULL, /*pCity*/ NULL, true, false);
	int iDefenderStrength = getStrengthValue(false);

	// The roll will vary damage between 2 and 3 (out of 10) for two units of identical strength

	int iDefenderDamageRatio = GetMaxHitPoints() - getDamage();
	int iDefenderDamage = /*200*/ GC.getAIR_STRIKE_SAME_STRENGTH_MIN_DEFENSE_DAMAGE() * iDefenderDamageRatio / GetMaxHitPoints();

	int iDefenderRoll = 0;
	if(bIncludeRand)
	{
		iDefenderRoll = /*200*/ GC.getGame().getJonRandNum(GC.getAIR_STRIKE_SAME_STRENGTH_POSSIBLE_EXTRA_DEFENSE_DAMAGE(), "Unit Air Strike Combat Damage");
		iDefenderRoll *= iDefenderDamageRatio;
		iDefenderRoll /= GetMaxHitPoints();
	}
	else
	{
		iDefenderRoll = /*200*/ GC.getAIR_STRIKE_SAME_STRENGTH_POSSIBLE_EXTRA_DEFENSE_DAMAGE();
		iDefenderRoll -= 1;	// Subtract 1 here, because this is the amount normally "lost" when doing a rand roll
		iDefenderRoll *= iDefenderDamageRatio;
		iDefenderRoll /= GetMaxHitPoints();
		iDefenderRoll /= 2;	// The divide by 2 is to provide the average damage
	}
	iDefenderDamage += iDefenderRoll;

	double fStrengthRatio = (double(iDefenderStrength) / iAttackerStrength);

	// In case our strength is less than the other guy's, we'll do things in reverse then make the ratio 1 over the result
	if (iAttackerStrength > iDefenderStrength)
	{
		fStrengthRatio = (double(iAttackerStrength) / iDefenderStrength);
	}

	fStrengthRatio = (fStrengthRatio + 3) / 4;
	fStrengthRatio = pow(fStrengthRatio, 4.0);
	fStrengthRatio = (fStrengthRatio + 1) / 2;

	if (iAttackerStrength > iDefenderStrength)
	{
		fStrengthRatio = 1 / fStrengthRatio;
	}

	iDefenderDamage = int(iDefenderDamage * fStrengthRatio);

	// Bring it back out of hundreds
	iDefenderDamage /= 100;

	// Always do at least 1 damage
	int iMinDamage = /*1*/ GC.getMIN_CITY_STRIKE_DAMAGE();
	if(iDefenderDamage < iMinDamage)
		iDefenderDamage = iMinDamage;

	return iDefenderDamage;
}

//	--------------------------------------------------------------------------------
void CvCity::DoNearbyEnemy()
{
	AI_PERF_FORMAT("City-AI-perf.csv", ("CvCity::DoNearbyEnemy, Turn %03d, %s, %s", GC.getGame().getElapsedGameTurns(), GetPlayer()->getCivilizationShortDescription(), getName().c_str()) );
	// Can't actually range strike
	if(!canRangeStrike())
		return;

#if defined(MOD_BALANCE_CORE_MILITARY)
	if (isMadeAttack())
		return;
#endif

#if defined(MOD_EVENTS_CITY_BOMBARD)
	int iSearchRange = getBombardRange();
#else
	int iSearchRange = GC.getCITY_ATTACK_RANGE();
#endif
	CvPlot* pBestPlot = NULL;

	bool bFoundEnemy = false;

	for(int iDX = -(iSearchRange); iDX <= iSearchRange && !pBestPlot; iDX++)
	{
		for(int iDY = -(iSearchRange); iDY <= iSearchRange && !pBestPlot; iDY++)
		{
			CvPlot* pLoopPlot = plotXYWithRangeCheck(getX(), getY(), iDX, iDY, iSearchRange);

			if(pLoopPlot != NULL)
			{
				if(pLoopPlot->isVisibleEnemyUnit(getOwner()))
				{
#if defined(MOD_BALANCE_CORE_MILITARY)
					if(canRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY()) && rangedStrikeTarget(pLoopPlot)!=NULL)
#else
					if(canRangeStrikeAt(pLoopPlot->getX(), pLoopPlot->getY()))
#endif
					{
						bFoundEnemy = true;

						// Notification
						CvNotifications* pNotifications = GET_PLAYER(getOwner()).GetNotifications();
						if(pNotifications)
						{
							Localization::String strText = Localization::Lookup("TXT_KEY_NOTIFICATION_CITY_CAN_SHOOT");
							strText << getNameKey();
							Localization::String strSummary = Localization::Lookup("TXT_KEY_NOTIFICATION_SUMMARY_CITY_CAN_SHOOT");
							strSummary << getNameKey();
							pNotifications->Add(NOTIFICATION_CITY_RANGE_ATTACK, strText.toUTF8(), strSummary.toUTF8(), getX(), getY(), GetID());
						}

						break;
					}
				}
			}
		}

		if(bFoundEnemy)
			break;
	}
}

#if defined(MOD_BALANCE_CORE_DEALS)
bool CvCity::IsInDanger(PlayerTypes eEnemy) const
{
	int iRange = 4;
	int iFriendlyPower = GetPower()*2;
	int iEnemyPower = 0;

	CvPlayer &kEnemy = GET_PLAYER(eEnemy);

	int iX = this->plot()->getX();
	int iY = this->plot()->getY();
	bool bFriendlyGeneralInTheVicinity = false;
	bool bEnemyGeneralInTheVicinity = false;

	int iUnitLoop;
	for (const CvUnit* pLoopUnit = GetPlayer()->firstUnit(&iUnitLoop); pLoopUnit != NULL; pLoopUnit = GetPlayer()->nextUnit(&iUnitLoop))
	{
		if (pLoopUnit->IsCombatUnit())
		{
			int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), iX, iY);
			if (iDistance <= iRange)
			{
				iFriendlyPower += pLoopUnit->GetPower();
			}
		}
		if (!bFriendlyGeneralInTheVicinity && pLoopUnit->IsGreatGeneral())
		{
			int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), iX, iY);
			if (iDistance <= iRange)
			{
				bFriendlyGeneralInTheVicinity = true;
			}
		}
	}
	if (bFriendlyGeneralInTheVicinity)
	{
		iFriendlyPower *= 11;
		iFriendlyPower /= 10;
	}

	for (const CvUnit* pLoopUnit = kEnemy.firstUnit(&iUnitLoop); pLoopUnit != NULL; pLoopUnit = kEnemy.nextUnit(&iUnitLoop))
	{
		if (pLoopUnit->IsCombatUnit())
		{
			int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), iX, iY);
			if (iDistance <= iRange)
			{
				iEnemyPower += pLoopUnit->GetPower();
			}
		}
		if (!bEnemyGeneralInTheVicinity && pLoopUnit->IsGreatGeneral())
		{
			int iDistance = plotDistance(pLoopUnit->getX(), pLoopUnit->getY(), iX, iY);
			if (iDistance <= iRange)
			{
				bEnemyGeneralInTheVicinity = true;
			}
		}
	}
	if (bEnemyGeneralInTheVicinity)
	{
		iEnemyPower *= 11;
		iEnemyPower /= 10;
	}

	return (iEnemyPower>iFriendlyPower);
}
#endif

//	--------------------------------------------------------------------------------
void CvCity::CheckForAchievementBuilding(BuildingTypes eBuilding)
{
	CvBuildingEntry* pkBuildingInfo = GC.getBuildingInfo(eBuilding);
	if(pkBuildingInfo == NULL)
		return;

#if !defined(NO_ACHIEVEMENTS)
	const char* szBuildingTypeChar = pkBuildingInfo->GetType();
	CvString szBuilding = szBuildingTypeChar;

	if(szBuilding == "BUILDING_LONGHOUSE")
	{
		CvPlot* pLoopPlot;
		int nForests = 0;

		for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
		{
			pLoopPlot = iterateRingPlots(getX(), getY(), iI);

			if(pLoopPlot != NULL)
			{
				if(pLoopPlot->getOwner() == getOwner())
				{
					if(pLoopPlot->getFeatureType() == FEATURE_FOREST)
					{
						nForests++;
					}
				}
			}
		}
		if(nForests >=4)
		{
			gDLL->UnlockAchievement(ACHIEVEMENT_SPECIAL_LONGHOUSE);
		}
	}
	if(szBuilding == "BUILDING_FLOATING_GARDENS")
	{
		int iCityX = getX();
		int iCityY = getY();
		PlayerTypes eCityOwner = getOwner();

		for(int iI = 0; iI < GetNumWorkablePlots(); iI++)
		{
			CvPlot* pLoopPlot = iterateRingPlots(iCityX, iCityY, iI);

			if(pLoopPlot != NULL && pLoopPlot->getOwner() == eCityOwner && pLoopPlot->isLake())
			{
				gDLL->UnlockAchievement(ACHIEVEMENT_SPECIAL_GARDENS);
				break;
			}
		}
	}
	//DLC_06 achievement: Build Statue of Zeus and Temple of Artemis in same city
	if(szBuilding == "BUILDING_STATUE_ZEUS" || szBuilding == "BUILDING_TEMPLE_ARTEMIS")
	{
		CvString szOtherWonder = "";
		if(szBuilding == "BUILDING_STATUE_ZEUS")
		{
			szOtherWonder = "BUILDING_TEMPLE_ARTEMIS";
		}
		else
		{
			szOtherWonder = "BUILDING_STATUE_ZEUS";
		}
		BuildingTypes eOtherWonder = (BuildingTypes)GC.getInfoTypeForString(szOtherWonder, true);
		if(eOtherWonder != NO_BUILDING)
		{
			PlayerTypes eCityOwner = getOwner();
			if(GetCityBuildings()->GetNumBuilding(eOtherWonder) > 0)
			{
				if(GetCityBuildings()->GetBuildingOriginalOwner(eOtherWonder) == eCityOwner)
				{
					gDLL->UnlockAchievement(ACHIEVEMENT_SPECIAL_ZEUS_AND_ARTEMIS);
				}
			}
		}
	}
#endif
}

//	--------------------------------------------------------------------------------
void CvCity::IncrementUnitStatCount(CvUnit* pUnit)
{
	CvString szUnitType = pUnit->getUnitInfo().GetType();

#if !defined(NO_ACHIEVEMENTS)
	if(szUnitType == "UNIT_WARRIOR")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_WARRIOR);
	}
	else if(szUnitType == "UNIT_SETTLER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SETTLER);
	}
	else if(szUnitType == "UNIT_WORKER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_WORKER);
	}
	else if(szUnitType == "UNIT_WORKBOAT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_WORKBOAT);
	}
	else if(szUnitType == "UNIT_GREAT_GENERAL")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_GREATGENERALS);
	}
	else if(szUnitType == "UNIT_SS_STASIS_CHAMBER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SSSTASISCHAMBER);
	}
	else if(szUnitType == "UNIT_SS_ENGINE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SSENGINE);
	}
	else if(szUnitType == "UNIT_SS_COCKPIT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SSCOCKPIT);
	}
	else if(szUnitType == "UNIT_SS_BOOSTER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SSBOOSTER);
	}
	else if(szUnitType == "UNIT_MISSILE_CRUISER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MISSILECRUISER);
	}
	else if(szUnitType == "UNIT_NUCLEAR_SUBMARINE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_NUCLEARSUBMARINE);
	}
	else if(szUnitType == "UNIT_CARRIER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CARRIER);
	}
	else if(szUnitType == "UNIT_BATTLESHIP")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_BATTLESHIP);
	}
	else if(szUnitType == "UNIT_SUBMARINE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SUBMARINE);
	}
	else if(szUnitType == "UNIT_DESTROYER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_DESTROYER);
	}
	else if(szUnitType == "UNIT_IRONCLAD")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_IRONCLAD);
	}
	else if(szUnitType == "UNIT_FRIGATE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_FRIGATE);
	}
	else if(szUnitType == "UNIT_ENGLISH_SHIPOFTHELINE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SHIPOFTHELINE);
	}
	else if(szUnitType == "UNIT_CARAVEL")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CARAVEL);
	}
	else if(szUnitType == "UNIT_TRIREME")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_TRIREME);
	}
	else if(szUnitType == "UNIT_MECH")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_GIANTDEATHROBOT);
	}
	else if(szUnitType == "UNIT_NUCLEAR_MISSILE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_NUCLEARMISSILE);
	}
	else if(szUnitType == "UNIT_STEALTH_BOMBER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_STEALTHBOMBER);
	}
	else if(szUnitType == "UNIT_JET_FIGHTER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_JETFIGHTER);
	}
	else if(szUnitType == "UNIT_GUIDED_MISSILE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_GUIDEDMISSILE);
	}
	else if(szUnitType == "UNIT_MODERN_ARMOR")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MODERNARMOR);
	}
	else if(szUnitType == "UNIT_HELICOPTER_GUNSHIP")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_HELICOPTERGUNSHIP);
	}
	else if(szUnitType == "UNIT_MOBILE_SAM")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MOBILESAM);
	}
	else if(szUnitType == "UNIT_ROCKET_ARTILLERY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ROCKETARTILLERY);
	}
	else if(szUnitType == "UNIT_MECHANIZED_INFANTRY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MECHANIZEDINFANTRY);
	}
	else if(szUnitType == "UNIT_ATOMIC_BOMB")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ATOMICBOMB);
	}
	else if(szUnitType == "UNIT_BOMBER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_BOMBER);
	}
	else if(szUnitType == "UNIT_AMERICAN_B17")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_B17);
	}
	else if(szUnitType == "UNIT_FIGHTER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_FIGHTER);
	}
	else if(szUnitType == "UNIT_JAPANESE_ZERO")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ZERO);
	}
	else if(szUnitType == "UNIT_PARATROOPER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_PARATROOPER);
	}
	else if(szUnitType == "UNIT_TANK")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_TANK);
	}
	else if(szUnitType == "UNIT_GERMAN_PANZER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_PANZER);
	}
	else if(szUnitType == "UNIT_ARTILLERY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ARTILLERY);
	}
	else if(szUnitType == "UNIT_ANTI_AIRCRAFT_GUN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ANTIAIRCRAFTGUN);
	}
	else if(szUnitType == "UNIT_ANTI_TANK_GUN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ANTITANKGUN);
	}
	else if(szUnitType == "UNIT_INFANTRY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_INFANTRY);
	}
	else if(szUnitType == "UNIT_FRENCH_FOREIGNLEGION")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_FOREIGNLEGION);
	}
	else if(szUnitType == "UNIT_CAVALRY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CAVALRY);
	}
	else if(szUnitType == "UNIT_RUSSIAN_COSSACK")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_COSSACK);
	}
	else if(szUnitType == "UNIT_RIFLEMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_RIFLEMAN);
	}
	else if(szUnitType == "UNIT_LANCER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_LANCER);
	}
	else if(szUnitType == "UNIT_OTTOMAN_SIPAHI")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SIPAHI);
	}
	else if(szUnitType == "UNIT_CANNON")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CANNON);
	}
	else if(szUnitType == "UNIT_MUSKETMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MUSKETMAN);
	}
	else if(szUnitType == "UNIT_AMERICAN_MINUTEMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MINUTEMAN);
	}
	else if(szUnitType == "UNIT_FRENCH_MUSKETEER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MUSKETEER);
	}
	else if(szUnitType == "UNIT_OTTOMAN_JANISSARY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_JANISSARY);
	}
	else if(szUnitType == "UNIT_LONGSWORDSMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_LONGSWORDSMAN);
	}
	else if(szUnitType == "UNIT_JAPANESE_SAMURAI")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SAMURAI);
	}
	else if(szUnitType == "UNIT_TREBUCHET")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_TREBUCHET);
	}
	else if(szUnitType == "UNIT_KNIGHT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_KNIGHT);
	}
	else if(szUnitType == "UNIT_SIAMESE_WARELEPHANT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_NARESUANSELEPHANT);
	}
	else if(szUnitType == "UNIT_SONGHAI_MUSLIMCAVALRY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MANDEKALUCAVALRY);
	}
	else if(szUnitType == "UNIT_CROSSBOWMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CROSSBOWMAN);
	}
	else if(szUnitType == "UNIT_CHINESE_CHUKONU")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CHUKONU);
	}
	else if(szUnitType == "UNIT_ARABIAN_CAMELARCHER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CAMELARCHER);
	}
	else if(szUnitType == "UNIT_ENGLISH_LONGBOWMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_LONGBOWMAN);
	}
	else if(szUnitType == "UNIT_PIKEMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_PIKEMAN);
	}
	else if(szUnitType == "UNIT_GERMAN_LANDSKNECHT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_LANDSKNECHT);
	}
	else if(szUnitType == "UNIT_CATAPULT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CATAPULT);
	}
	else if(szUnitType == "UNIT_ROMAN_BALLISTA")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_BALLISTA);
	}
	else if(szUnitType == "UNIT_HORSEMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_HORSEMAN);
	}
	else if(szUnitType == "UNIT_GREEK_COMPANIONCAVALRY")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_COMPANIONCAVALRY);
	}
	else if(szUnitType == "UNIT_SWORDSMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SWORDSMAN);
	}
	else if(szUnitType == "UNIT_IROQUOIAN_MOHAWKWARRIOR")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_MOHAWKWARRIOR);
	}
	else if(szUnitType == "UNIT_ROMAN_LEGION")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_LEGION);
	}
	else if(szUnitType == "UNIT_CHARIOT_ARCHER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_CHARIOTARCHER);
	}
	else if(szUnitType == "UNIT_EGYPTIAN_WARCHARIOT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_WARCHARIOT);
	}
	else if(szUnitType == "UNIT_INDIAN_WARELEPHANT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_WARELEPHANT);
	}
	else if(szUnitType == "UNIT_SPEARMAN")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SPEARMAN);
	}
	else if(szUnitType == "UNIT_GREEK_HOPLITE")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_HOPLITE);
	}
	else if(szUnitType == "UNIT_PERSIAN_IMMORTAL")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_IMMORTAL);
	}
	else if(szUnitType == "UNIT_ARCHER")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_ARCHER);
	}
	else if(szUnitType == "UNIT_SCOUT")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_SCOUT);
	}
	else if(szUnitType == "UNIT_AZTEC_JAGUAR")
	{
		gDLL->IncrementSteamStat(ESTEAMSTAT_JAGUAR);
	}
	else
	{
		OutputDebugString("\nNo stat for selected unit type.\n");
	}

	bool bAllUnitsUnlocked;

	bAllUnitsUnlocked = AreAllUnitsBuilt();
	if(bAllUnitsUnlocked)
	{
		gDLL->UnlockAchievement(ACHIEVEMENT_ALL_UNITS);
	}
#endif
}

//	--------------------------------------------------------------------------------
// Check to see if all the units have been built
bool CvCity::AreAllUnitsBuilt()
{
	int iI;
	int iUnitStatStart = 1;   //As they're defined on the backend
	int iUnitStatEnd = 79;
	int32 nStat = 0;

	for(iI = iUnitStatStart; iI < iUnitStatEnd; iI++)
	{
		if(gDLL->GetSteamStat((ESteamStat)iI, &nStat))
		{
			if(nStat <= 0)
			{
				return false;
			}
		}
	}
	//Whoops, one is out of order
	if(gDLL->GetSteamStat(ESTEAMSTAT_CAVALRY, &nStat))
	{
		if(nStat <=0)
		{
			return false;
		}
	}
	return true;

}

//	--------------------------------------------------------------------------------
/// Which unit would we build if we are building one for an operation?
UnitTypes CvCity::GetUnitForOperation()
{
	VALIDATE_OBJECT
	UnitTypes eBestUnit;
	UnitAITypes eUnitAI;

	CvPlayerAI& kPlayer = GET_PLAYER(getOwner());

	bool bSameCity = false;
	OperationSlot thisOperationSlot = kPlayer.PeekAtNextUnitToBuildForOperationSlot(this, bSameCity);
	if (thisOperationSlot.IsValid() && bSameCity)
	{
		CvArmyAI* pThisArmy = kPlayer.getArmyAI(thisOperationSlot.m_iArmyID);

		if(pThisArmy)
		{
#if defined(MOD_BALANCE_CORE)
			if(pThisArmy->GetDomainType() == DOMAIN_SEA && pThisArmy->GetArea() != NULL)
			{
				if( !isAdjacentToArea( pThisArmy->GetArea() ) )
				{
					return NO_UNIT;
				}
			}
			else if (pThisArmy->GetDomainType() == DOMAIN_LAND && pThisArmy->GetArea() != getArea())
			{
				return NO_UNIT;
			}
#endif
			// figure out the primary and secondary unit type to potentially build
			int iFormationIndex = pThisArmy->GetFormationIndex();
			CvMultiUnitFormationInfo* thisFormation = GC.getMultiUnitFormationInfo(iFormationIndex);
			if(thisFormation)
			{
				const CvFormationSlotEntry& slotEntry = thisFormation->getFormationSlotEntry(thisOperationSlot.m_iSlotID);

				eUnitAI = slotEntry.m_primaryUnitType;
#if defined(MOD_BALANCE_CORE)
				eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI, true);
#else
				eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI);
#endif
				if(eBestUnit == NO_UNIT)
				{
					eUnitAI = slotEntry.m_secondaryUnitType;
#if defined(MOD_BALANCE_CORE)
					eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI, true);
#else
					eBestUnit = m_pCityStrategyAI->GetUnitProductionAI()->RecommendUnit(eUnitAI);
#endif
				}

				if(eBestUnit != NO_UNIT)
				{
					return eBestUnit;
				}
			}
		}
	}
	return NO_UNIT;
}

//	--------------------------------------------------------------------------------
/// What does a City shoot when attacking a Unit?
const char* CvCity::GetCityBombardEffectTag() const
{
	EraTypes eCityEra = GET_TEAM(getTeam()).GetCurrentEra();

	return GC.getEraInfo(eCityEra)->GetCityBombardEffectTag();
}

//	--------------------------------------------------------------------------------
uint CvCity::GetCityBombardEffectTagHash() const
{
	EraTypes eCityEra = GET_TEAM(getTeam()).GetCurrentEra();

	return GC.getEraInfo(eCityEra)->GetCityBombardEffectTagHash();
}

//	---------------------------------------------------------------------------
int CvCity::GetMaxHitPoints() const
{
	return GC.getMAX_CITY_HIT_POINTS() + m_iExtraHitPoints;
}

//	--------------------------------------------------------------------------------
int CvCity::GetExtraHitPoints() const
{
	return m_iExtraHitPoints;
}

//	--------------------------------------------------------------------------------
void CvCity::ChangeExtraHitPoints(int iValue)
{
	if (iValue != 0)
	{
		m_iExtraHitPoints += iValue;
		FAssertMsg(m_iExtraHitPoints >= 0, "Trying to set ExtraHitPoints to a negative value");
		if (m_iExtraHitPoints < 0)
			m_iExtraHitPoints = 0;

		int iCurrentDamage = getDamage();
		if (iCurrentDamage > GetMaxHitPoints())
			setDamage(iCurrentDamage);		// Call setDamage, it will clamp the value.
	}
}

//	--------------------------------------------------------------------------------
const FAutoArchive& CvCity::getSyncArchive() const
{
	return m_syncArchive;
}

//	--------------------------------------------------------------------------------
FAutoArchive& CvCity::getSyncArchive()
{
	return m_syncArchive;
}

//	--------------------------------------------------------------------------------
std::string CvCity::debugDump(const FAutoVariableBase& /*var*/) const
{
	std::string result = "Game Turn : ";
	char gameTurnBuffer[8] = {0};
	int gameTurn = GC.getGame().getGameTurn();
	sprintf_s(gameTurnBuffer, "%d\0", gameTurn);
	result += gameTurnBuffer;
	return result;
}

//	--------------------------------------------------------------------------------
std::string CvCity::stackTraceRemark(const FAutoVariableBase& var) const
{
	std::string result = debugDump(var);
	if(&var == &m_aiBaseYieldRateFromTerrain)
	{
		result += std::string("\nlast yield used to update from terrain = ") + FSerialization::toString(s_lastYieldUsedToUpdateRateFromTerrain) + std::string("\n");
		result += std::string("change value used for update = ") + FSerialization::toString(s_changeYieldFromTerreain) + std::string("\n");
	}
	return result;
}

//	---------------------------------------------------------------------------
bool CvCity::IsBusy() const
{
	return getCombatUnit() != NULL;
}

//	---------------------------------------------------------------------------
const CvUnit* CvCity::getCombatUnit() const
{
	return ::getUnit(m_combatUnit);
}

//	---------------------------------------------------------------------------
CvUnit* CvCity::getCombatUnit()
{
	return ::getUnit(m_combatUnit);
}

//	---------------------------------------------------------------------------
void CvCity::setCombatUnit(CvUnit* pCombatUnit, bool /*bAttacking*/)
{
	if(pCombatUnit != NULL)
	{
		CvAssertMsg(getCombatUnit() == NULL , "Combat Unit is not expected to be assigned");
		CvAssertMsg(!(plot()->isCityFighting()), "(plot()->isCityFighting()) did not return false as expected");
		m_combatUnit = pCombatUnit->GetIDInfo();
	}
	else
	{
		clearCombat();
	}
}

//	----------------------------------------------------------------------------
void CvCity::clearCombat()
{
	if(getCombatUnit() != NULL)
	{
		CvAssertMsg(plot()->isCityFighting(), "plot()->isCityFighting is expected to be true");
		m_combatUnit.reset();
	}
}

//	----------------------------------------------------------------------------
//	Return true if the city is fighting with someone.	Equivalent to the CvUnit call.
bool CvCity::isFighting() const
{
	return getCombatUnit() != NULL;
}

#if defined(MOD_API_EXTENSIONS)
//	----------------------------------------------------------------------------
bool CvCity::HasBelief(BeliefTypes iBeliefType) const
{
	const ReligionTypes iReligion = GetCityReligions()->GetReligiousMajority();
	const CvReligion* pReligion = GC.getGame().GetGameReligions()->GetReligion(iReligion, getOwner());

	return (pReligion && pReligion->m_Beliefs.HasBelief(iBeliefType));
}

bool CvCity::HasBuilding(BuildingTypes iBuildingType) const
{
	return (GetCityBuildings()->GetNumBuilding(iBuildingType) > 0);
}

bool CvCity::HasBuildingClass(BuildingClassTypes iBuildingClassType) const
{
	return HasBuilding((BuildingTypes) getCivilizationInfo().getCivilizationBuildings(iBuildingClassType));
}

bool CvCity::HasAnyWonder() const
{
	return (getNumWorldWonders() > 0);
}

bool CvCity::HasWonder(BuildingTypes iBuildingType) const
{
	return HasBuilding(iBuildingType);
}

bool CvCity::IsCivilization(CivilizationTypes iCivilizationType) const
{
	return (GET_PLAYER(getOwner()).getCivilizationType() == iCivilizationType);
}

bool CvCity::HasFeature(FeatureTypes iFeatureType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		if (pLoopPlot->HasFeature(iFeatureType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasWorkedFeature(FeatureTypes iFeatureType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasFeature(iFeatureType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasAnyNaturalWonder() const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}
#if defined(MOD_PSEUDO_NATURAL_WONDER)
		if (pLoopPlot->IsNaturalWonder(true)) {
#else
		if (pLoopPlot->IsNaturalWonder()) {
#endif
			return true;
		}
	}

	return false;
}

bool CvCity::HasNaturalWonder(FeatureTypes iFeatureType) const
{
	return HasFeature(iFeatureType);
}

bool CvCity::HasImprovement(ImprovementTypes iImprovementType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		if (pLoopPlot->HasImprovement(iImprovementType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasWorkedImprovement(ImprovementTypes iImprovementType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasImprovement(iImprovementType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasPlotType(PlotTypes iPlotType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		if (pLoopPlot->HasPlotType(iPlotType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasWorkedPlotType(PlotTypes iPlotType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasPlotType(iPlotType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasAnyReligion() const
{
	return (GetCityReligions()->IsReligionInCity());
}

bool CvCity::HasReligion(ReligionTypes iReligionType) const
{
	return (HasAnyReligion() && GetCityReligions()->GetNumFollowers(iReligionType) > 0);
}

bool CvCity::HasResource(ResourceTypes iResourceType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Team can't see the resource here
		if (pLoopPlot->getResourceType(getTeam()) != iResourceType) {
			continue;
		}

		// Resource not linked to this city
		// if (pLoopPlot->GetResourceLinkedCity() != this) {
		// 	continue;
		// }

		if (pLoopPlot->HasResource(iResourceType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasWorkedResource(ResourceTypes iResourceType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Team can't see the resource here
		if (pLoopPlot->getResourceType(getTeam()) != iResourceType) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasResource(iResourceType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::IsConnectedTo(CvCity* pCity) const
{
	return GET_PLAYER(getOwner()).IsCityConnectedToCity((CvCity*) this, pCity);
}

bool CvCity::HasSpecialistSlot(SpecialistTypes iSpecialistType) const
{
	for (int iBuildingType = 0; iBuildingType < GC.getNumBuildingInfos(); iBuildingType++) {
		if (HasBuilding((BuildingTypes)iBuildingType)) {
			CvBuildingEntry* pkBuilding = GC.getBuildingInfo((BuildingTypes)iBuildingType);
			if (pkBuilding && pkBuilding->GetSpecialistType() == iSpecialistType && pkBuilding->GetSpecialistCount() > 0) {
				return true;
			}
		}
	}

	return false;
}

bool CvCity::HasSpecialist(SpecialistTypes iSpecialistType) const
{
	return (GetCityCitizens()->GetSpecialistCount(iSpecialistType) > 0);
}

bool CvCity::HasTerrain(TerrainTypes iTerrainType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		if (pLoopPlot->HasTerrain(iTerrainType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasWorkedTerrain(TerrainTypes iTerrainType) const
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();


	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasTerrain(iTerrainType)) {
			return true;
		}
	}

	return false;
}

bool CvCity::HasAnyDomesticTradeRoute(bool bOutgoing) const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if ( bOutgoing && GetID()==pConnection->m_iOriginID && getOwner()==pConnection->m_eDestOwner )
			return true;
		if ( !bOutgoing && GetID()==pConnection->m_iDestID && getOwner()==pConnection->m_eOriginOwner )
			return true;
	}

	return false;
}

bool CvCity::HasAnyInternationalTradeRoute(bool bOutgoing) const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if ( bOutgoing && GetID()==pConnection->m_iOriginID && getOwner()!=pConnection->m_eDestOwner )
			return true;
		if ( !bOutgoing && GetID()==pConnection->m_iDestID && getOwner()!=pConnection->m_eOriginOwner )
			return true;
	}

	return false;
}

bool CvCity::HasTradeRouteToAnyCity() const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if ( GetID()==pConnection->m_iOriginID )
			return true;
	}

	return false;
}

bool CvCity::HasTradeRouteTo(CvCity* pCity) const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if (GetID()==pConnection->m_iOriginID && pCity->GetID()==pConnection->m_iDestID)
			return true;
	}

	return false;
}

bool CvCity::HasTradeRouteFromAnyCity() const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if (GetID()==pConnection->m_iDestID)
			return true;
	}

	return false;
}

bool CvCity::HasTradeRouteFrom(CvCity* pCity) const
{
	CvGameTrade* pTrade = GC.getGame().GetGameTrade();
	for (uint iTradeRoute = 0; iTradeRoute < pTrade->GetNumTradeConnections(); iTradeRoute++) {
		if (pTrade->IsTradeRouteIndexEmpty(iTradeRoute))
			continue;

		const TradeConnection* pConnection = &(pTrade->GetTradeConnection(iTradeRoute));
		if (GetID()==pConnection->m_iDestID && pCity->GetID()==pConnection->m_iOriginID)
			return true;
	}

	return false;
}

bool CvCity::IsOnFeature(FeatureTypes iFeatureType) const
{
	return plot()->HasFeature(iFeatureType);
}

bool CvCity::IsAdjacentToFeature(FeatureTypes iFeatureType) const
{
	return plot()->IsAdjacentToFeature(iFeatureType);
}

bool CvCity::IsWithinDistanceOfFeature(FeatureTypes iFeatureType, int iDistance) const
{
	return plot()->IsWithinDistanceOfFeature(iFeatureType, iDistance);
}
#if defined(MOD_BALANCE_CORE)
bool CvCity::IsWithinDistanceOfUnit(UnitTypes eOtherUnit, int iDistance, bool bIsFriendly, bool bIsEnemy) const
{
	return plot()->IsWithinDistanceOfUnit(getOwner(), eOtherUnit, iDistance, bIsFriendly, bIsEnemy);
}
bool CvCity::IsWithinDistanceOfUnitClass(UnitClassTypes eUnitClass, int iDistance, bool bIsFriendly, bool bIsEnemy) const
{
	return plot()->IsWithinDistanceOfUnitClass(getOwner(), eUnitClass, iDistance, bIsFriendly, bIsEnemy);
}
bool CvCity::IsWithinDistanceOfUnitCombatType(UnitCombatTypes eUnitCombat, int iDistance, bool bIsFriendly, bool bIsEnemy) const
{
	return plot()->IsWithinDistanceOfUnitCombatType(getOwner(), eUnitCombat, iDistance, bIsFriendly, bIsEnemy);
}
bool CvCity::IsWithinDistanceOfUnitPromotion(PromotionTypes eUnitPromotion, int iDistance, bool bIsFriendly, bool bIsEnemy) const
{
	return plot()->IsWithinDistanceOfUnitPromotion(getOwner(), eUnitPromotion, iDistance, bIsFriendly, bIsEnemy);
}
#endif
bool CvCity::IsOnImprovement(ImprovementTypes iImprovementType) const
{
	return plot()->HasImprovement(iImprovementType);
}

bool CvCity::IsAdjacentToImprovement(ImprovementTypes iImprovementType) const
{
	return plot()->IsAdjacentToImprovement(iImprovementType);
}

bool CvCity::IsWithinDistanceOfImprovement(ImprovementTypes iImprovementType, int iDistance) const
{
	return plot()->IsWithinDistanceOfImprovement(iImprovementType, iDistance);
}

bool CvCity::IsOnPlotType(PlotTypes iPlotType) const
{
	return plot()->HasPlotType(iPlotType);
}

bool CvCity::IsAdjacentToPlotType(PlotTypes iPlotType) const
{
	return plot()->IsAdjacentToPlotType(iPlotType);
}

bool CvCity::IsWithinDistanceOfPlotType(PlotTypes iPlotType, int iDistance) const
{
	return plot()->IsWithinDistanceOfPlotType(iPlotType, iDistance);
}

bool CvCity::IsOnResource(ResourceTypes iResourceType) const
{
	return plot()->HasResource(iResourceType);
}

bool CvCity::IsAdjacentToResource(ResourceTypes iResourceType) const
{
	return plot()->IsAdjacentToResource(iResourceType);
}

bool CvCity::IsWithinDistanceOfResource(ResourceTypes iResourceType, int iDistance) const
{
	return plot()->IsWithinDistanceOfResource(iResourceType, iDistance);
}

bool CvCity::IsOnTerrain(TerrainTypes iTerrainType) const
{
	return plot()->HasTerrain(iTerrainType);
}

bool CvCity::IsAdjacentToTerrain(TerrainTypes iTerrainType) const
{
	return plot()->IsAdjacentToTerrain(iTerrainType);
}

bool CvCity::IsWithinDistanceOfTerrain(TerrainTypes iTerrainType, int iDistance) const
{
	return plot()->IsWithinDistanceOfTerrain(iTerrainType, iDistance);
}
int CvCity::CountNumWorkedFeature(FeatureTypes iFeatureType)
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();
	int iNum = 0;

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
		{
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) 
		{
			continue;
		}

		if (pLoopPlot->HasFeature(iFeatureType)) 
		{
			iNum++;
		}
	}

	return iNum;
}
int CvCity::CountNumWorkedImprovement(ImprovementTypes eImprovement)
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();
	int iNum = 0;

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
		{
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) 
		{
			continue;
		}

		if (pLoopPlot->getImprovementType() == eImprovement) 
		{
			iNum++;
		}
	}

	return iNum;
}
int CvCity::CountNumWorkedResource(ResourceTypes eResource)
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();
	int iNum = 0;

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
		{
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) 
		{
			continue;
		}

		if (pLoopPlot->getResourceType() == eResource) 
		{
			iNum++;
		}
	}

	return iNum;
}
int CvCity::CountNumImprovement(ImprovementTypes eImprovement)
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();
	int iNum = 0;

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
		{
			continue;
		}

		// Does not belong to this city
		if (pLoopPlot->getWorkingCity() != this) 
		{
			continue;
		}

		if (pLoopPlot->getImprovementType() == eImprovement) 
		{
			iNum++;
		}
	}

	return iNum;
}
int CvCity::CountNumWorkedRiverTiles(TerrainTypes eTerrain)
{
	int iX = getX(); int iY = getY(); int iOwner = getOwner();
	int iNum = 0;

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) 
		{
			continue;
		}

		if(!pLoopPlot->isRiver())
		{
			continue;
		}

		// Does not belong to this city
		if (pLoopPlot->getWorkingCity() != this) 
		{
			continue;
		}

		if(!GetCityCitizens()->IsWorkingPlot(pLoopPlot))
		{
			continue;
		}

		if(eTerrain == NO_TERRAIN)
		{
			iNum++;
		}
		else if(eTerrain == pLoopPlot->getTerrainType())
		{
			iNum++;
		}
	}

	return iNum;
}
#endif

//	--------------------------------------------------------------------------------
#if defined(MOD_CORE_PER_TURN_DAMAGE)
int CvCity::addDamageReceivedThisTurn(int iDamage)
{
	m_iDamageTakenThisTurn+=iDamage;
	return m_iDamageTakenThisTurn;
}

void CvCity::flipDamageReceivedPerTurn()
{
	m_iDamageTakenLastTurn = m_iDamageTakenThisTurn;
	m_iDamageTakenThisTurn = 0;
}

bool CvCity::isInDangerOfFalling() const
{
	int iHitpoints = GetMaxHitPoints() - getDamage();
	//be conservative here ...
	return (m_iDamageTakenLastTurn*1.5 > iHitpoints);
}

bool CvCity::isUnderSiege() const
{
	return (m_iDamageTakenLastTurn>0);
}
#endif

#if defined(MOD_BALANCE_CORE)
//the closest friendly cities - up to 4 entries 
const std::vector<int>& CvCity::GetClosestFriendlyNeighboringCities() const
{
	return m_vClosestNeighbors;
}

void CvCity::UpdateClosestFriendlyNeighbors()
{
	struct SCityWithScore
	{
		CvCity* city;
		int score;
		SCityWithScore(CvCity* ptr, int i) : city(ptr), score(i) {}
		bool operator<(const SCityWithScore& rhs) const { return score<rhs.score; }
	};

	std::vector<SCityWithScore> allNeighbors;

	CvPlayer& kOwner = GET_PLAYER(getOwner());
	int iLoop = 0;
	for(CvCity* pCity = kOwner.firstCity(&iLoop); pCity != NULL; pCity = kOwner.nextCity(&iLoop))
	{
		//we are not our own neighbor
		if (pCity==this)
			continue;

		int iDistance = plotDistance( this->getX(), this->getY(), pCity->getX(), pCity->getY() );
		allNeighbors.push_back( SCityWithScore(pCity,iDistance) );
	}

	std::stable_sort(allNeighbors.begin(), allNeighbors.end());

	m_vClosestNeighbors.clear();
	for (size_t i=0; i<min<size_t>(6,allNeighbors.size()); i++)
		m_vClosestNeighbors.push_back(allNeighbors[i].city->GetID());
}

void CvCity::AttachUnit(CvUnit* pUnit)
{
	if (pUnit)
		m_vAttachedUnits.push_back( pUnit->GetID() );
}

void CvCity::ClearAttachedUnits()
{
	m_vAttachedUnits.clear();
}

const std::vector<int>& CvCity::GetAttachedUnits() const
{
	return m_vAttachedUnits;
}

int CvCity::CountFeature(FeatureTypes iFeatureType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not owned by this city
		if (pLoopPlot->getWorkingCity() != this) {
			continue;
		}

		if (pLoopPlot->HasFeature(iFeatureType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountWorkedFeature(FeatureTypes iFeatureType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasFeature(iFeatureType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountImprovement(ImprovementTypes iImprovementType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not owned by this city
		if (pLoopPlot->getWorkingCity() != this) {
			continue;
		}

		if (pLoopPlot->HasImprovement(iImprovementType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountWorkedImprovement(ImprovementTypes iImprovementType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasImprovement(iImprovementType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountPlotType(PlotTypes iPlotType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not owned by this city
		if (pLoopPlot->getWorkingCity() != this) {
			continue;
		}

		if (pLoopPlot->HasPlotType(iPlotType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountWorkedPlotType(PlotTypes iPlotType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasPlotType(iPlotType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountResource(ResourceTypes iResourceType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not owned by this city
		if (pLoopPlot->getWorkingCity() != this) {
			continue;
		}

		if (pLoopPlot->HasResource(iResourceType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountWorkedResource(ResourceTypes iResourceType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasResource(iResourceType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountTerrain(TerrainTypes iTerrainType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not owned by this city
		if (pLoopPlot->getWorkingCity() != this) {
			continue;
		}

		if (pLoopPlot->HasTerrain(iTerrainType)) {
			++iCount;
		}
	}
	
	return iCount;
}

int CvCity::CountWorkedTerrain(TerrainTypes iTerrainType) const
{
	int iCount = 0;
	int iX = getX(); int iY = getY(); int iOwner = getOwner();

	for (int iCityPlotLoop = 0; iCityPlotLoop < GetNumWorkablePlots(); iCityPlotLoop++)
	{
		CvPlot* pLoopPlot = iterateRingPlots(iX, iY, iCityPlotLoop);

		// Invalid plot or not owned by this player
		if (pLoopPlot == NULL || pLoopPlot->getOwner() != iOwner) {
			continue;
		}

		// Not being worked by this city
		if (pLoopPlot->getWorkingCity() != this || !pLoopPlot->isBeingWorked()) {
			continue;
		}

		if (pLoopPlot->HasTerrain(iTerrainType)) {
			++iCount;
		}
	}

	return iCount;
}
#endif
