#include "LuauScheduler.h"
#include "LuauContext.h"
#include "LuauRuntime.h"

#include <lua.h>
#include <lualib.h>

#include <algorithm>
#include <chrono>

namespace Sapphire::Scripting
{
  const char* exitReasonName( ExitReason reason )
  {
    switch( reason )
    {
      case ExitReason::Finished:
        return "finished";
      case ExitReason::Error:
        return "error";
      case ExitReason::Cancelled:
        return "cancelled";
    }
    return "unknown";
  }

  const char* waitKindName( WaitKind kind )
  {
    switch( kind )
    {
      case WaitKind::None:
        return "None";
      case WaitKind::Timer:
        return "Timer";
      case WaitKind::Park:
        return "Park";
      case WaitKind::Change:
        return "Change";
      case WaitKind::Scene:
        return "Scene";
      case WaitKind::Action:
        return "Action";
    }
    return "Unknown";
  }

  namespace
  {
    std::string errorMessage( lua_State* co )
    {
      std::string msg;
      if( const char* s = lua_tostring( co, -1 ) )
        msg = s;
      else
        msg = std::string( "(error object is a " ) + luaL_typename( co, -1 ) + " value)";
      if( const char* trace = lua_debugtrace( co ) )
      {
        msg += "\n";
        msg += trace;
      }
      while( !msg.empty() && msg.back() == '\n' )
        msg.pop_back();
      return msg;
    }
  }

  LuauScheduler::LuauScheduler( LuauRuntime& runtime, ISchedulerHost& host ) :
    m_runtime( runtime ),
    m_host( host ),
    m_sharedClock( std::make_unique< ScriptClock >() )
  {
    m_runtime.setScheduler( this );
  }

  LuauScheduler::~LuauScheduler()
  {
    if( m_runtime.scheduler() == this )
      m_runtime.setScheduler( nullptr );
  }

  ScriptThread* LuauScheduler::create( LuauContext& ctx, uint32_t playerId )
  {
    lua_State* env = ctx.envThread();
    lua_State* co = lua_newthread( env );
    int ref = lua_ref( env, -1 );
    lua_pop( env, 1 );

    auto thread = std::make_unique< ScriptThread >();
    thread->id = m_nextId++;
    if( m_nextId == 0 )
      m_nextId = 1;
    thread->co = co;
    thread->ref = ref;
    thread->ctx = &ctx;
    thread->playerId = playerId;
    lua_setthreaddata( co, reinterpret_cast< void* >( static_cast< uintptr_t >( thread->id ) ) );

    auto* raw = thread.get();
    m_threads.emplace( raw->id, std::move( thread ) );
    indexThread( *raw );
    return raw;
  }

  ScriptThread* LuauScheduler::syncThread( LuauContext& ctx, uint32_t playerId )
  {
    auto* pooled = find( ctx.m_syncThread );
    if( pooled && !pooled->running )
    {
      pooled->playerId = playerId;
      return pooled;
    }
    // nested sync call on the same context: the pooled thread is busy, use a throwaway one
    if( pooled )
      return create( ctx, playerId );
    // created without a player: a pooled thread serves any player and cancelPlayer must not find it
    auto* t = create( ctx, 0 );
    t->pooled = true;
    t->playerId = playerId;
    ctx.m_syncThread = t->id;
    return t;
  }

  void LuauScheduler::indexThread( ScriptThread& thread )
  {
    if( thread.ctx )
      thread.ctx->m_threadIds.insert( thread.id );
    if( thread.playerId != 0 && !thread.pooled )
      m_byPlayer[ thread.playerId ].insert( thread.id );
  }

  void LuauScheduler::unindexThread( ScriptThread& thread )
  {
    if( thread.ctx )
      thread.ctx->m_threadIds.erase( thread.id );
    if( thread.playerId == 0 || thread.pooled )
      return;
    auto it = m_byPlayer.find( thread.playerId );
    if( it == m_byPlayer.end() )
      return;
    it->second.erase( thread.id );
    if( it->second.empty() )
      m_byPlayer.erase( it );
  }

  void LuauScheduler::indexWait( ScriptThread& thread )
  {
    if( !thread.ctx || ( thread.wait != WaitKind::Timer && thread.wait != WaitKind::Change ) )
      return;
    if( thread.wakeAt != UINT64_MAX )
    {
      auto& clock = thread.ctx->clock();
      clock.timers.emplace( thread.wakeAt, thread.id );
      thread.timerClock = &clock;
    }
    if( thread.wait == WaitKind::Change )
    {
      thread.ctx->m_changeWaiters.insert( thread.id );
      thread.inChangeSet = true;
    }
  }

  void LuauScheduler::clearWait( ScriptThread& thread )
  {
    if( thread.timerClock )
    {
      thread.timerClock->timers.erase( { thread.wakeAt, thread.id } );
      thread.timerClock = nullptr;
    }
    if( thread.inChangeSet )
    {
      if( thread.ctx )
        thread.ctx->m_changeWaiters.erase( thread.id );
      thread.inChangeSet = false;
    }
  }

  bool LuauScheduler::idle() const
  {
    return m_depth == 0;
  }

  ScriptThread* LuauScheduler::current() const
  {
    return m_current;
  }

  ScriptThread* LuauScheduler::find( uint32_t id )
  {
    auto it = m_threads.find( id );
    return it == m_threads.end() ? nullptr : it->second.get();
  }

  bool LuauScheduler::isTaskThread( lua_State* co ) const
  {
    auto id = reinterpret_cast< uintptr_t >( lua_getthreaddata( co ) );
    if( id == 0 )
      return false;
    auto it = m_threads.find( static_cast< uint32_t >( id ) );
    if( it != m_threads.end() && it->second->co == co )
      return true;
    return std::any_of( m_exiting.begin(), m_exiting.end(), [ co ]( const ScriptThread* t ) { return t->co == co; } );
  }

  size_t LuauScheduler::threadCount( const LuauContext* ctx ) const
  {
    return ctx ? ctx->m_threadIds.size() : m_threads.size();
  }

  void LuauScheduler::forEach( const std::function< void( ScriptThread& ) >& fn )
  {
    std::vector< uint32_t > ids;
    for( auto& [ id, t ] : m_threads )
      ids.push_back( id );
    for( auto id : ids )
      if( auto* t = find( id ) )
        fn( *t );
  }

  uint64_t LuauScheduler::addExit( ScriptThread& thread, lua_State* L, int idx )
  {
    uint64_t serial = m_nextExitSerial++;
    thread.exitRefs.push_back( ExitRef{ serial, lua_ref( L, idx ) } );
    return serial;
  }

  void LuauScheduler::removeExit( uint32_t threadId, uint64_t serial )
  {
    auto* t = find( threadId );
    if( !t )
    {
      auto it = std::find_if( m_exiting.begin(), m_exiting.end(),
                              [ threadId ]( const ScriptThread* e ) { return e->id == threadId; } );
      if( it == m_exiting.end() )
        return;
      t = *it;
    }
    auto it = std::find_if( t->exitRefs.begin(), t->exitRefs.end(),
                            [ serial ]( const ExitRef& r ) { return r.serial == serial; } );
    if( it == t->exitRefs.end() )
      return;
    lua_unref( m_runtime.state(), it->ref );
    t->exitRefs.erase( it );
  }

  void LuauScheduler::enqueue( ScriptThread& thread, int nargs, PushFn push )
  {
    clearWait( thread );
    thread.queued = true;
    m_ready.push_back( Pending{ thread.id, nargs, std::move( push ) } );
  }

  void LuauScheduler::start( ScriptThread& thread, int nargs )
  {
    if( !idle() )
    {
      enqueue( thread, nargs, {} );
      return;
    }
    runSegment( thread, nargs, nullptr );
    drain();
  }

  void LuauScheduler::resume( ScriptThread& thread, PushFn args, World::Quest* bound )
  {
    if( !idle() || thread.running || thread.queued )
    {
      enqueue( thread, 0, std::move( args ) );
      return;
    }
    int nargs = args ? args( thread.co ) : 0;
    runSegment( thread, nargs, bound );
    drain();
  }

  void LuauScheduler::wake( uint32_t threadId )
  {
    auto* t = find( threadId );
    if( !t || t->wait != WaitKind::Park || t->queued )
      return;
    enqueue( *t, 0, {} );
  }

  void LuauScheduler::cancel( uint32_t threadId )
  {
    auto* t = find( threadId );
    if( !t )
      return;
    // task threads are resumed only by the scheduler (LuauRuntime rejects coroutine.resume/close on them)
    if( t->running )
    {
      t->cancelRequested = true;
      return;
    }
    ++m_depth;
    exitThread( *t, ExitReason::Cancelled );
    --m_depth;
  }

  void LuauScheduler::cancelAll( LuauContext& ctx )
  {
    // threads inside exitThread are no longer indexed but still point at the context
    for( auto* t : m_exiting )
      if( t->ctx == &ctx )
        t->ctx = nullptr;
    ctx.m_syncThread = 0;
    // exit callbacks may spawn further threads into the context: repeat until none are left
    while( !ctx.m_threadIds.empty() )
    {
      std::vector< uint32_t > ids( ctx.m_threadIds.begin(), ctx.m_threadIds.end() );
      for( auto id : ids )
      {
        auto* t = find( id );
        if( !t || t->ctx != &ctx )
        {
          ctx.m_threadIds.erase( id );
          continue;
        }
        if( t->running )
        {
          // the context is going away underneath a running thread: let it unwind without touching the context
          t->cancelRequested = true;
          clearWait( *t );
          ctx.m_threadIds.erase( id );
          t->ctx = nullptr;
          continue;
        }
        cancel( id );
      }
    }
  }

  void LuauScheduler::cancelPlayer( uint32_t playerId )
  {
    auto it = m_byPlayer.find( playerId );
    if( it == m_byPlayer.end() )
      return;
    std::vector< uint32_t > ids( it->second.begin(), it->second.end() );
    for( auto id : ids )
      cancel( id );
  }

  void LuauScheduler::notifyChange( LuauContext& ctx )
  {
    if( ctx.m_changeWaiters.empty() )
      return;
    auto ids = std::move( ctx.m_changeWaiters );
    ctx.m_changeWaiters.clear();
    for( auto id : ids )
    {
      auto* t = find( id );
      if( !t )
        continue;
      t->inChangeSet = false;
      if( t->wait == WaitKind::Change && !t->queued && !t->running )
        enqueue( *t, 0, {} );
    }
  }

  ScriptClock& LuauScheduler::sharedClock()
  {
    return *m_sharedClock;
  }

  void LuauScheduler::useSharedClock( LuauContext& ctx, uint64_t nowMs )
  {
    auto& shared = *m_sharedClock;
    if( nowMs > shared.nowMs )
      shared.nowMs = nowMs;
    if( ctx.m_clock == &shared )
      return;

    auto& own = *ctx.m_clock;
    for( auto& [ wakeAt, id ] : own.timers )
    {
      auto* t = find( id );
      if( !t )
        continue;
      uint64_t remaining = wakeAt > own.nowMs ? wakeAt - own.nowMs : 0;
      t->wakeAt = remaining >= UINT64_MAX - shared.nowMs ? UINT64_MAX : shared.nowMs + remaining;
      t->timerClock = nullptr;
      if( t->wakeAt != UINT64_MAX )
      {
        shared.timers.emplace( t->wakeAt, id );
        t->timerClock = &shared;
      }
    }
    own.timers.clear();
    ctx.m_clock = &shared;
  }

  void LuauScheduler::updateTimers( ScriptClock& clock, uint64_t nowMs )
  {
    clock.nowMs = nowMs;
    // (wakeAt, id) order: earlier deadlines first, ties in creation order
    while( !clock.timers.empty() )
    {
      auto it = clock.timers.begin();
      if( it->first > nowMs )
        break;
      uint32_t id = it->second;
      clock.timers.erase( it );
      auto* t = find( id );
      if( !t )
        continue;
      t->timerClock = nullptr;
      enqueue( *t, 0, {} );
    }
  }

  void LuauScheduler::segmentEnded( ScriptThread& thread, int status )
  {
    if( !thread.ctx )
      return;
    // a waiter that only re-checked its predicate changed nothing; notifying here would wake waiters in a loop
    if( status == LUA_YIELD && thread.wait == WaitKind::Change && thread.recheck && !thread.cancelRequested )
      return;
    // the segment may have written state no host notification covers (director.data, script globals)
    notifyChange( *thread.ctx );
  }

  LuauScheduler::SegmentEnd LuauScheduler::runSegment( ScriptThread& thread, int nargs, World::Quest* bound )
  {
    ++m_depth;
    ScriptThread* prev = m_current;
    bool outermost = prev == nullptr;
    if( outermost )
      m_runtime.armDeadline();

    clearWait( thread );
    thread.wait = WaitKind::None;
    thread.recheck = false;
    thread.queued = false;
    thread.boundQuest = bound;
    thread.running = true;
    m_current = &thread;
    int status = lua_resume( thread.co, prev ? prev->co : nullptr, nargs );
    m_current = prev;
    thread.running = false;
    if( outermost )
      m_runtime.disarmDeadline();

    // before indexWait: the thread must not wake itself
    segmentEnded( thread, status );

    SegmentEnd end = SegmentEnd::Exited;
    if( status == LUA_YIELD && !thread.cancelRequested )
    {
      if( thread.wait == WaitKind::None && !thread.queued )
      {
        m_host.reportError( thread, "task yielded outside a host wait" );
        exitThread( thread, ExitReason::Error );
      }
      else
      {
        indexWait( thread );
        m_host.onYield( thread );
        thread.boundQuest = nullptr;
        end = SegmentEnd::Yielded;
      }
    }
    else if( status == LUA_YIELD )
      exitThread( thread, ExitReason::Cancelled );
    else if( status == LUA_OK )
      exitThread( thread, ExitReason::Finished );
    else
    {
      m_host.reportError( thread, errorMessage( thread.co ) );
      exitThread( thread, ExitReason::Error );
    }

    --m_depth;
    return end;
  }

  bool LuauScheduler::runSync( ScriptThread& thread, int nargs, const std::function< void( lua_State* ) >& onResults )
  {
    ++m_depth;
    ScriptThread* prev = m_current;
    bool outermost = prev == nullptr;
    if( outermost )
      m_runtime.armDeadline();

    thread.sync = true;
    thread.running = true;
    m_current = &thread;
    int status = lua_resume( thread.co, prev ? prev->co : nullptr, nargs );
    m_current = prev;
    thread.running = false;
    if( outermost )
      m_runtime.disarmDeadline();

    segmentEnded( thread, status );

    bool ok = false;
    bool recycle = thread.pooled;
    if( status == LUA_OK )
    {
      if( onResults )
        onResults( thread.co );
      exitThread( thread, ExitReason::Finished, recycle );
      ok = true;
    }
    else if( status == LUA_YIELD )
      exitThread( thread, ExitReason::Cancelled, recycle );
    else
    {
      m_host.reportError( thread, errorMessage( thread.co ) );
      exitThread( thread, ExitReason::Error, recycle );
    }

    --m_depth;
    if( m_depth == 0 )
      drain();
    return ok;
  }

  void LuauScheduler::drain()
  {
    if( !idle() || m_draining )
      return;
    m_draining = true;
    uint32_t budgetMs = m_runtime.timeBudgetMs();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds( budgetMs );
    while( !m_ready.empty() )
    {
      // remaining ready threads run on the next drain
      if( budgetMs != 0 && std::chrono::steady_clock::now() >= deadline )
        break;
      Pending p = std::move( m_ready.front() );
      m_ready.pop_front();
      auto* t = find( p.id );
      if( !t || !t->queued )
        continue;
      int nargs = p.push ? p.push( t->co ) : p.nargs;
      runSegment( *t, nargs, nullptr );
    }
    m_draining = false;
  }

  bool LuauScheduler::recyclable( const ScriptThread& thread ) const
  {
    return thread.ctx && thread.ctx->isOpen() && !thread.ctx->isClosing() && thread.ctx->m_syncThread == thread.id;
  }

  void LuauScheduler::resetPooled( ScriptThread& thread )
  {
    lua_resetthread( thread.co );
    thread.playerId = 0;
    thread.wait = WaitKind::None;
    thread.wakeAt = 0;
    thread.recheck = false;
    thread.cancelRequested = false;
    thread.boundQuest = nullptr;
    thread.usedScenes = false;
    thread.events.clear();
    thread.quests.clear();
  }

  void LuauScheduler::releaseThread( ScriptThread& thread, ExitReason reason )
  {
    if( reason == ExitReason::Cancelled )
      lua_resetthread( thread.co );
    if( thread.ctx && thread.ctx->m_syncThread == thread.id )
      thread.ctx->m_syncThread = 0;
    lua_unref( m_runtime.state(), thread.ref );
  }

  void LuauScheduler::exitThread( ScriptThread& thread, ExitReason reason, bool recycle )
  {
    clearWait( thread );
    thread.queued = false;

    // the common end of a sync hook: no exit callbacks, so no script code can run while the thread exits and it can
    // stay in the thread table. `running` keeps re-entrant sync calls and cancels off it while the host finishes it
    if( recycle && thread.exitRefs.empty() && recyclable( thread ) )
    {
      thread.running = true;
      m_host.onExit( thread, reason );
      thread.running = false;
      if( recyclable( thread ) )
      {
        resetPooled( thread );
        return;
      }
      // the host closed the context underneath
      unindexThread( thread );
      auto owned = m_threads.extract( thread.id );
      releaseThread( thread, reason );
      return;
    }

    lua_State* L = m_runtime.state();
    // detach first: a re-entrant cancel of this thread (from an exit callback) must not find it again
    unindexThread( thread );
    auto owned = m_threads.extract( thread.id );
    m_exiting.push_back( &thread );

    // an exit callback can register exit callbacks of its own; bound the recursion
    bool tooDeep = m_exiting.size() > kMaxExitNesting;
    if( tooDeep && !thread.exitRefs.empty() )
      m_host.reportError( thread, "task.onExit callbacks are nested too deeply; skipped" );
    // pop one at a time: an exit callback can still removeExit a later one of this thread
    while( !thread.exitRefs.empty() )
    {
      int ref = thread.exitRefs.back().ref;
      thread.exitRefs.pop_back();
      if( !tooDeep && thread.ctx && thread.ctx->isOpen() )
      {
        auto* et = create( *thread.ctx, thread.playerId );
        lua_getref( et->co, ref );
        lua_pushstring( et->co, exitReasonName( reason ) );
        runSync( *et, 1 );
      }
      lua_unref( L, ref );
    }

    m_host.onExit( thread, reason );
    m_exiting.pop_back();

    // a pooled sync thread is kept for the next sync call of its context, unless a nested sync call replaced it
    // while it was exiting
    if( recycle && owned && recyclable( thread ) )
    {
      resetPooled( thread );
      m_threads.insert( std::move( owned ) );
      indexThread( thread );
      return;
    }

    releaseThread( thread, reason );
    // owned (the extracted node) frees the thread on return
  }
}
