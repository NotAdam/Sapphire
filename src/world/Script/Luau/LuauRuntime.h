#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <unordered_map>

struct lua_State;

namespace Sapphire::Scripting
{
  class LuauScheduler;

  struct CompiledChunk
  {
    std::string bytecode;
    std::string error;
    std::filesystem::file_time_type mtime;
  };

  // The one Luau VM of the world server. Used only on the main server thread.
  class LuauRuntime
  {
  public:
    using RegisterFn = std::function< void( lua_State* ) >;

    LuauRuntime() = default;
    ~LuauRuntime();
    LuauRuntime( const LuauRuntime& ) = delete;
    LuauRuntime& operator=( const LuauRuntime& ) = delete;

    // registerBindings runs before the prelude and installs every binding (host primitives go into __host)
    bool start( const std::filesystem::path& preludeFile, const std::filesystem::path& moduleRoot,
                RegisterFn registerBindings );
    void stop();
    bool running() const;
    lua_State* state() const;
    // canonical when the directory exists
    const std::filesystem::path& root() const;
    // canonicalFile lies inside root()
    bool inRoot( const std::filesystem::path& canonicalFile ) const;

    // cached by path, recompiled when the file's mtime differs
    const CompiledChunk& compile( const std::filesystem::path& file );

    // "@<path relative to root>" (generic separators)
    std::string chunkName( const std::filesystem::path& file ) const;
    std::string relativePath( const std::filesystem::path& file ) const;

    static std::filesystem::path defaultScriptRoot();

    void setTimeBudgetMs( uint32_t ms );
    uint32_t timeBudgetMs() const;
    void armDeadline();
    void disarmDeadline();

    void setScheduler( LuauScheduler* scheduler );
    LuauScheduler* scheduler() const;

    static LuauRuntime* from( lua_State* L );

  private:
    static void onInterrupt( lua_State* L, int gc );

    lua_State* m_state{ nullptr };
    std::filesystem::path m_root;
    std::unordered_map< std::string, CompiledChunk > m_chunks;
    uint32_t m_budgetMs{ 1000 };
    int64_t m_deadline{ 0 }; // steady clock ms; 0 = not armed
    uint32_t m_interrupts{ 0 }; // interrupts since the deadline was armed
    LuauScheduler* m_scheduler{ nullptr };
  };
}
