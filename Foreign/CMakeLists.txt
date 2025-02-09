if(NOT CLIENT_HAS_LUA)
    file(GLOB LuaSrc "Lua/src/*.c" "Lua/src/*.h")
endif()
file(GLOB HashSrc "Hash/*.cpp" "Hash/*.h")
file(GLOB AntlrSrc "Antlr-3.4/libantlr3c-3.4/src/*.c")
file(GLOB FmtSrc "fmt/*.cc")
if (WIN32)
	file(GLOB StackWalkerSrc "StackWalker/*.cpp")
endif()
file(GLOB PlustacheSrc "plustache/*.cpp")
file(GLOB ZLibSrc "zlib/*.c" "zlib/*.h")
set(StamSimpleFluidSrc StamSimpleFluid/solver.c)
set(CParseSource cparse/containers.cpp cparse/functions.cpp cparse/packToken.cpp cparse/shunting-yard.cpp)

BasicLibrary(ForeignMisc "${LuaSrc};${HashSrc};${FmtSrc};${StackWalkerSrc};${PlustacheSrc};${StamSimpleFluidSrc};${ZLibSrc};${AntlrSrc};${CParseSource}")

# Ensure we export the necessary include folders from this target
# Most other targets list this target as a dependency, so they should inherit these include folders
target_include_directories(ForeignMisc PUBLIC Antlr-3.4/libantlr3c-3.4 Antlr-3.4/libantlr3c-3.4/include)
target_include_directories(ForeignMisc PUBLIC ${CMAKE_CURRENT_LIST_DIR})        # required to be public because of the way plustache includes it's headers
target_include_directories(ForeignMisc INTERFACE half-1.9.2/include eigen)
target_include_directories(ForeignMisc INTERFACE cml-2_1_2)
target_include_directories(ForeignMisc INTERFACE LuaBridge/Source/LuaBridge Lua/src)
target_include_directories(ForeignMisc INTERFACE json/include)
target_include_directories(ForeignMisc INTERFACE future/include)

if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(ForeignMisc PRIVATE -Wno-parentheses-equality -Wno-incompatible-pointer-types-discards-qualifiers)
elseif ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    target_compile_options(ForeignMisc PRIVATE /wd4100 /wd4244 /wd4996 /wd4706 /wd4127 /wd4267 /wd4018 /wd4702 /wd4324 /wd4310 /wd4701 /wd4703 /wd4099 /wd4068 /wd4101 /wd4180)
endif()
