
file( GLOB_RECURSE SOURCES
        *.cpp
  *.h
)
add_library( world ${SOURCES} )
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SOURCES})

set_target_properties( world PROPERTIES
                           ENABLE_EXPORTS ON
  WINDOWS_EXPORT_ALL_SYMBOLS ON
)

target_link_libraries( world PUBLIC common )
target_include_directories( world PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" )
target_link_libraries( world PRIVATE Luau.VM Luau.Compiler Luau.Common )
target_compile_definitions( world PRIVATE SAPPHIRE_LUAU_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/lua_scripts/" )
