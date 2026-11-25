#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <memory>
#include <string>
#include <vector>

#include <ForwardsZone.h>
#include "Quest/Quest.h"

struct lua_State;

namespace Sapphire::Scripting
{
  class LuauRuntime;
  class LuauContext;
  struct ScriptClock;

  enum class ExitReason : uint8_t
  {
    Finished,
    Error,
    Cancelled
  };

  enum class WaitKind : uint8_t
  {
    None,
    Timer,
    Park,
    Change,
    Scene,
    Action
  };

  const char* exitReasonName( ExitReason reason ); // "finished" | "error" | "cancelled"
  const char* waitKindName( WaitKind kind );

  struct OwnedEvent
  {
    std::weak_ptr< Entity::Player > player;
    uint32_t eventId;
    Event::EventHandlerPtr handler;
  };

  struct QuestAccess
  {
    std::weak_ptr< Entity::Player > player;
    uint16_t questId;
    World::Quest working;
    World::Quest snapshot;
    World::Quest* bound = nullptr;
  };

  struct ExitRef
  {
    uint64_t serial;                  // unique per registration (registry refs and pooled thread ids are reused)
    int ref;
  };

  struct ScriptThread
  {
    uint32_t id;
    lua_State* co;
    int ref;
    LuauContext* ctx;
    uint32_t playerId = 0;            // owning player's entity id, 0 = none (a pooled thread: its current call's)
    bool sync = false;                // true: yielding host functions raise syncError
    const char* syncError = "cannot yield here";
    WaitKind wait = WaitKind::None;
    uint64_t wakeAt = 0;              // Timer, and Change with a timeout: on ctx->clock(); UINT64_MAX = never
    bool recheck = false;             // Change: the segment only re-checked a wait.till predicate (no notifyChange)
    bool queued = false;              // in the ready queue
    bool running = false;             // inside lua_resume (possibly with nested resumes above it)
    bool cancelRequested = false;     // cancel() while running: exits as Cancelled when its segment ends
    bool pooled = false;              // a context's reusable sync thread (syncThread); never indexed by player
    World::Quest* boundQuest = nullptr; // the engine's quest object for the current segment (quest scene callbacks)
    std::vector< OwnedEvent > events; // bottom = opened by the engine before dispatch, above it = player:nest
    std::vector< QuestAccess > quests;
    std::vector< ExitRef > exitRefs;  // task.onExit callbacks, run last-registered first
    ScriptClock* timerClock = nullptr; // the clock whose timers hold (wakeAt, id)
    bool inChangeSet = false;         // in ctx->m_changeWaiters
    bool usedScenes = false;          // the host has (or had) pending scenes of this thread
  };

  struct ISchedulerHost
  {
    virtual ~ISchedulerHost() = default;
    virtual void onYield( ScriptThread& thread ) = 0;
    virtual void onExit( ScriptThread& thread, ExitReason reason ) = 0;
    virtual void reportError( ScriptThread& thread, const std::string& message ) = 0;
  };

  class LuauScheduler
  {
  public:
    using PushFn = std::function< int( lua_State* ) >; // pushes resume values onto the thread, returns their count

    LuauScheduler( LuauRuntime& runtime, ISchedulerHost& host );
    ~LuauScheduler();

    // lua_newthread on the context env; the thread is referenced until it exits
    ScriptThread* create( LuauContext& ctx, uint32_t playerId );
    // a thread for one runSync call: the context's reusable sync thread when it is idle, else a new one
    ScriptThread* syncThread( LuauContext& ctx, uint32_t playerId );
    // function + args already on thread.co. Runs now when idle, else queues
    void start( ScriptThread& thread, int nargs );
    // engine callbacks; queues (bound ignored) when not idle
    void resume( ScriptThread& thread, PushFn args, World::Quest* bound = nullptr );
    // Park -> ready queue; no-op for any other state or unknown id
    void wake( uint32_t threadId );
    void cancel( uint32_t threadId );
    void cancelAll( LuauContext& ctx );
    void cancelPlayer( uint32_t playerId );
    // every Change waiter of the context -> ready queue. Also runs after every script segment of the context that
    // did more than re-check a wait.till predicate
    void notifyChange( LuauContext& ctx );
    // the clock of singleton contexts
    ScriptClock& sharedClock();
    // switches ctx to the shared clock (pending timers keep their remaining time); the shared clock never goes back
    void useSharedClock( LuauContext& ctx, uint64_t nowMs );
    // sets clock.nowMs; due timers of every context on it -> ready queue
    void updateTimers( ScriptClock& clock, uint64_t nowMs );
    // sync = true; a yield cancels the thread and returns false. onResults sees the returned values on thread.co
    bool runSync( ScriptThread& thread, int nargs, const std::function< void( lua_State* ) >& onResults = {} );
    // run the ready queue until empty; no-op when not idle
    void drain();
    bool idle() const;
    // the task whose segment is running (innermost when nested): the caller of every host function, also of those
    // called from a coroutine the task resumed. nullptr outside script code
    ScriptThread* current() const;
    ScriptThread* find( uint32_t id );
    // co is the thread of a live (or exiting) task, not a coroutine created by a script
    bool isTaskThread( lua_State* co ) const;
    size_t threadCount( const LuauContext* ctx = nullptr ) const;
    void forEach( const std::function< void( ScriptThread& ) >& fn );
    // task.onExit: registers the function at idx of L on thread; returns the registration's serial
    uint64_t addExit( ScriptThread& thread, lua_State* L, int idx );
    // drops a registration that has not run yet, of a live or exiting thread; no-op when it is not registered there
    void removeExit( uint32_t threadId, uint64_t serial );

  private:
    struct Pending
    {
      uint32_t id;
      int nargs;
      PushFn push;
    };

    enum class SegmentEnd
    {
      Yielded,
      Exited
    };

    SegmentEnd runSegment( ScriptThread& thread, int nargs, World::Quest* bound );
    // recycle: a pooled thread is reset and kept instead of freed (when its context is still open)
    void exitThread( ScriptThread& thread, ExitReason reason, bool recycle = false );
    // recycle is possible: the thread is still its open context's pooled sync thread
    bool recyclable( const ScriptThread& thread ) const;
    // back to the state of a fresh pooled thread
    void resetPooled( ScriptThread& thread );
    // drops the Luau thread of a thread already removed from m_threads
    void releaseThread( ScriptThread& thread, ExitReason reason );
    void enqueue( ScriptThread& thread, int nargs, PushFn push );
    // after a segment: notifies the context unless the segment only re-checked a predicate
    void segmentEnded( ScriptThread& thread, int status );
    // Timer / Change bookkeeping of a thread that just yielded
    void indexWait( ScriptThread& thread );
    void clearWait( ScriptThread& thread );
    void indexThread( ScriptThread& thread );
    void unindexThread( ScriptThread& thread );

    LuauRuntime& m_runtime;
    ISchedulerHost& m_host;
    std::map< uint32_t, std::unique_ptr< ScriptThread > > m_threads;
    std::unordered_map< uint32_t, std::set< uint32_t > > m_byPlayer; // playerId -> thread ids
    std::unique_ptr< ScriptClock > m_sharedClock;
    std::deque< Pending > m_ready;
    ScriptThread* m_current{ nullptr }; // thread currently inside lua_resume
    int m_depth{ 0 };                   // > 0 while a segment (including its host callbacks) is being processed
    uint32_t m_nextId{ 1 };
    uint64_t m_nextExitSerial{ 1 };
    bool m_draining{ false };
    static constexpr size_t kMaxExitNesting = 32;
    std::vector< ScriptThread* > m_exiting; // threads inside exitThread, innermost last
  };
}
