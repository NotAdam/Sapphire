add_executable( luau_selftest main.cpp )
target_link_libraries( luau_selftest PRIVATE world Luau.VM Luau.Compiler Luau.Common )
