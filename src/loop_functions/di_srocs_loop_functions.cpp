#include "di_srocs_loop_functions.h"

#include <argos3/plugins/simulator/entities/block_entity.h>
#include <argos3/plugins/robots/builderbot/simulator/builderbot_entity.h>

namespace argos {

   /****************************************/
   /****************************************/

   CDISRoCSLoopFunctions::CDISRoCSLoopFunctions() {}

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::Init(TConfigurationNode& t_tree) {
      /* parse loop function configuration */
      TConfigurationNodeIterator itCondition("condition");
      for(itCondition = itCondition.begin(&t_tree);
          itCondition != itCondition.end();
          ++itCondition) {
         /* parse the actions */
         std::vector<std::shared_ptr<SAction> > vecActions;
         TConfigurationNodeIterator itAction("action");
         for(itAction = itAction.begin(&(*itCondition));
             itAction != itAction.end();
             ++itAction) {
            std::string strActionType;
            UInt32 unDelay = 0;
            GetNodeAttribute(*itAction, "type", strActionType);
            GetNodeAttributeOrDefault(*itAction, "delay", unDelay, unDelay);
            if(strActionType == "add_timer") {
               std::string strId;
               GetNodeAttribute(*itAction, "id", strId);
               std::shared_ptr<SAddTimerAction> ptrTimerAction =
                  std::make_shared<SAddTimerAction>(*this, unDelay, std::move(strId));
               vecActions.emplace_back(std::move(ptrTimerAction));
            }
            else if(strActionType == "add_entity") {
               TConfigurationNodeIterator itEntity;
               for(itEntity = itEntity.begin(&(*itAction));
                   itEntity != itEntity.end();
                   ++itEntity) {
                  std::shared_ptr<SAddEntityAction> ptrEntityAction =
                     std::make_shared<SAddEntityAction>(*this, unDelay, *itEntity);
                  vecActions.emplace_back(std::move(ptrEntityAction));
               }
            }
            else if(strActionType == "terminate") {
               std::shared_ptr<STerminateAction> ptrTerminateAction =
                  std::make_shared<STerminateAction>(*this, unDelay);
               vecActions.emplace_back(std::move(ptrTerminateAction));
            }
            else {
               THROW_ARGOSEXCEPTION("Loop function action type \"" << strActionType << "\" not implemented.");
            }
         }
         std::string strConditionType;
         bool bOnce = false;
         GetNodeAttribute(*itCondition, "type", strConditionType);
         GetNodeAttributeOrDefault(*itCondition, "once", bOnce, bOnce);
         if(strConditionType == "entity") {
            std::string strTarget;
            std::string strId;
            std::string strType;
            CVector3 cPosition;
            Real fThreshold;
            GetNodeAttribute(*itCondition, "target", strTarget);
            std::string::size_type nSeperator = strTarget.find(':');
            if(nSeperator != std::string::npos) {
               strType = std::move(strTarget.substr(0, nSeperator));
               strId = std::move(strTarget.substr(nSeperator + 1));
            }
            else {
               strId = std::move(strTarget);
            }
            GetNodeAttribute(*itCondition, "position", cPosition);
            GetNodeAttribute(*itCondition, "threshold", fThreshold);
            std::unique_ptr<SEntityCondition> ptrEntityCondition =
               std::make_unique<SEntityCondition>(*this, bOnce, std::move(vecActions), std::move(strId), std::move(strType), cPosition, fThreshold);
            m_vecConditions.emplace_back(std::move(ptrEntityCondition));
         }
         else if(strConditionType == "timer") {
            std::string strId;
            UInt32 unValue;
            GetNodeAttribute(*itCondition, "id", strId);
            GetNodeAttribute(*itCondition, "value", unValue);
            std::unique_ptr<STimerCondition> ptrTimerCondition =
               std::make_unique<STimerCondition>(*this, bOnce, std::move(vecActions), std::move(strId), unValue);
            m_vecConditions.emplace_back(std::move(ptrTimerCondition));
         }
         else {
            THROW_ARGOSEXCEPTION("Loop function condition type \"" << strConditionType << "\" not implemented.");
         }
      }
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::Reset() {
      /* remove all dynamically added entities */
      for(CEntity* pc_entity : m_vecAddedEntities) {
         CallEntityOperation<CSpaceOperationRemoveEntity, CSpace, void>(GetSpace(), *pc_entity);
      }
      m_vecAddedEntities.clear();
      /* clear is experiment finished */
      m_bTerminate = false;
      /* clear map of timers */
      m_mapTimers.clear();
      /* clear output streams */
      m_mapOutputStreams.clear();
      /* reenable all conditions */
      for(std::unique_ptr<SCondition>& ptr_condition : m_vecConditions) {
         ptr_condition->Enabled = true;
      }
      
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::PreStep() {
      UInt32 unClock = GetSpace().GetSimulationClock();
      /* increment all timers */
      for(std::pair<const std::string, UInt32>& c_timer : m_mapTimers) {
         std::get<UInt32>(c_timer)++;
      }
      /* check conditions */
      for(std::unique_ptr<SCondition>& ptr_condition : m_vecConditions) {
         if(ptr_condition->Enabled && ptr_condition->IsTrue()) {
            /* schedule the associated actions */
            for(const std::shared_ptr<SAction>& ptr_action : ptr_condition->Actions) {
               m_mapPendingActions.emplace(unClock + ptr_action->Delay, ptr_action);
            }
            if(ptr_condition->Once) {
               ptr_condition->Enabled = false;
            }
         }
      }
      /* execute actions for the current timestep */
      std::pair<std::multimap<UInt32, std::shared_ptr<SAction> >::iterator,
                std::multimap<UInt32, std::shared_ptr<SAction> >::iterator> cActionRange =
         m_mapPendingActions.equal_range(unClock);
      for(std::multimap<UInt32, std::shared_ptr<SAction> >::iterator itAction = cActionRange.first;
          itAction != cActionRange.second;
          ++itAction) {
         itAction->second->Execute();
      }
      m_mapPendingActions.erase(unClock);
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::PostStep() {
      using TValueType = std::pair<const std::string, CAny>;
      try {
         for(TValueType& t_robot : GetSpace().GetEntitiesByType("builderbot")) {
            CBuilderBotEntity* pcBuilderBot =
               any_cast<CBuilderBotEntity*>(t_robot.second);
            LogEmbodiedEntityToFile(t_robot.first, pcBuilderBot->GetEmbodiedEntity());
         }
      }
      catch(CARGoSException &ex) {}
      try {
         for(TValueType& t_block : GetSpace().GetEntitiesByType("block")) {
            CBlockEntity* pcBlock =
               any_cast<CBlockEntity*>(t_block.second);
            LogEmbodiedEntityToFile(t_block.first, pcBlock->GetEmbodiedEntity());
         }
      }
      catch(CARGoSException &ex) {}
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::LogEmbodiedEntityToFile(const std::string& str_entity_id,
                                                       const CEmbodiedEntity& c_embodied_entity) {
      UInt32 unClock = GetSpace().GetSimulationClock();
      std::map<std::string, std::ofstream>::iterator itOutputStream =
         m_mapOutputStreams.find(str_entity_id);
      if(itOutputStream == std::end(m_mapOutputStreams)) {
         std::pair<std::map<std::string, std::ofstream>::iterator,bool> cResult =
            m_mapOutputStreams.emplace(std::piecewise_construct,
                                       std::forward_as_tuple(str_entity_id),
                                       std::forward_as_tuple(str_entity_id + ".csv",
                                                             std::ios_base::out |
                                                             std::ios_base::trunc));
         if(cResult.second) {
            itOutputStream = cResult.first;
         }
         else {
            THROW_ARGOSEXCEPTION("Could not insert output stream into map");
         }
      }
      itOutputStream->second 
         << unClock
         << ","
         << c_embodied_entity.GetOriginAnchor().Position
         << std::endl;
   }

   /****************************************/
   /****************************************/

   bool CDISRoCSLoopFunctions::IsExperimentFinished() {
      return m_bTerminate;
   }

   /****************************************/
   /****************************************/

   bool CDISRoCSLoopFunctions::SEntityCondition::IsTrue() {
      try {
         std::vector<CEntity*> vecCandidateEntities;
         if(!EntityType.empty()) {
            for(CEntity* pc_entity : Parent.GetSpace().GetRootEntityVector()) {
               if(pc_entity->GetTypeDescription() == EntityType) {
                  if(!EntityId.empty() && (pc_entity->GetId() != EntityId)) {
                     continue;
                  }
                  vecCandidateEntities.push_back(pc_entity);
               }
            }
         }
         else {
            vecCandidateEntities.push_back(&Parent.GetSpace().GetEntity(EntityId));
         }
         for(CEntity* pc_entity : vecCandidateEntities) {
            CComposableEntity* pcComposableEntity =
               dynamic_cast<CComposableEntity*>(pc_entity);
            if(pcComposableEntity != nullptr && pcComposableEntity->HasComponent("body")) {
               CEmbodiedEntity& cEmbodiedEntity =
                  pcComposableEntity->GetComponent<CEmbodiedEntity>("body");
               Real fDistance =
                  Distance(Position, cEmbodiedEntity.GetOriginAnchor().Position);
               if(fDistance < Threshold) {
                  return true;
               }
            }
         }
      }
      catch(CARGoSException& ex) {}
      /* if we reach this point the condition is false */
      return false;
   }

   /****************************************/
   /****************************************/

   bool CDISRoCSLoopFunctions::STimerCondition::IsTrue() {
      std::map<std::string, UInt32>::iterator itTimer =
         Parent.m_mapTimers.find(TimerId);
      if(itTimer != std::end(Parent.m_mapTimers)) {
         return (itTimer->second == Value);
      }
      return false;
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::SAddEntityAction::Execute() {
      CEntity* pcEntity = CFactory<CEntity>::New(Configuration.Value());
      pcEntity->Init(Configuration);
      CallEntityOperation<CSpaceOperationAddEntity, CSpace, void>(Parent.GetSpace(), *pcEntity);
      /* attempt to do a collision check */
      CComposableEntity* pcComposableEntity =
         dynamic_cast<CComposableEntity*>(pcEntity);
      if((pcComposableEntity != nullptr) && pcComposableEntity->HasComponent("body")) {
         CEmbodiedEntity& cEmbodiedEntity =
            pcComposableEntity->GetComponent<CEmbodiedEntity>("body");
         if(cEmbodiedEntity.IsCollidingWithSomething()) {
            LOGERR << "[WARNING] Failed to add entity \"" 
                   << pcEntity->GetId()
                   << "\" since it would have collided with something"
                   << std::endl;
            CallEntityOperation<CSpaceOperationRemoveEntity, CSpace, void>(Parent.GetSpace(), *pcEntity);
         }
         else {
            /* entity added successfully */
            Parent.m_vecAddedEntities.push_back(pcEntity);
            return;
         }
      }
      LOGERR << "[WARNING] Could not perform collision test for entity \""
             << pcEntity->GetId()
             << std::endl;
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::SAddTimerAction::Execute() {
      if(Parent.m_mapTimers.count(TimerId) != 0) {
         LOGERR << "[WARNING] Timer \""
                << TimerId
                << "\" already exists and has been reset to zero"
                << std::endl;
      }
      Parent.m_mapTimers[TimerId] = 0;
   }

   /****************************************/
   /****************************************/

   void CDISRoCSLoopFunctions::STerminateAction::Execute() {
      Parent.m_bTerminate = true;
   }

   /****************************************/
   /****************************************/

   REGISTER_LOOP_FUNCTIONS(CDISRoCSLoopFunctions, "di_srocs_loop_functions");

}
