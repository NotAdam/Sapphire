#ifndef _ACTION_H_
#define _ACTION_H_

#include <Common.h>
#include "../Forwards.h"

namespace Core {
namespace Action {

class Action
{

public:
  Action();

  virtual ~Action();

  uint16_t getId() const;

  Common::HandleActionType getHandleActionType() const;

  Entity::CharaPtr getTargetChara() const;

  bool isInterrupted() const;

  void setInterrupted();

  uint64_t getStartTime() const;

  void setStartTime( uint64_t startTime );

  uint32_t getCastTime() const;

  void setCastTime( uint32_t castTime );

  Entity::CharaPtr getActionSource() const;

  virtual void onStart()
  {
  };

  virtual void onFinish()
  {
  };

  virtual void onInterrupt()
  {
  };

  // update action, if returns true, action is done and has to be removed from the actor
  virtual bool update();

protected:
  uint16_t m_id;
  Common::HandleActionType m_handleActionType;

  uint64_t m_startTime;
  uint32_t m_castTime;

  Entity::CharaPtr m_pSource;
  Entity::CharaPtr m_pTarget;

  bool m_bInterrupt;

};

}
}

#endif
