-- Set common options for MinGW/MSVC compatibility
set_languages("cxx17")  -- Use C++17 for better MinGW compatibility
set_warnings("all")     -- Don't treat warnings as errors

-- External dependencies
add_requires("gtest")  -- GoogleTest for unit testing

-- Platform detection and flags
if is_plat("windows") then
    -- MinGW/Windows specific flags
    add_cxxflags("-Wall", "-Wextra")
    add_defines("WIN32_LEAN_AND_MEAN", "_WIN32_WINNT=0x0601")
    add_syslinks("ws2_32", "kernel32", "user32")
    -- MinGW doesn't need filesystem linking explicitly
else
    -- Linux/Unix flags
    add_cxxflags("-Wall", "-Wextra", "-Werror")
    add_cxxflags("-fstack-protector-strong", "-D_FORTIFY_SOURCE=2", "-Wformat-security")
    
    -- Debug flags (only on Linux due to sanitizer support)
    if is_mode("debug") then
        add_cxxflags("-fsanitize=address,undefined")
        add_ldflags("-fsanitize=address,undefined")
    end
end

-- Release flags  
add_cxxflags("-O2", "-DNDEBUG", {mode = "release"})

-- Include directories
add_includedirs("include")

-- CodeKnife dynamic library - unified library containing all components
target("codeknife")
    set_kind("shared")
    add_files("src/*.cpp")  -- Include all source files
    add_headerfiles("include/*.hpp")  -- Include all header files
    add_includedirs("include", {public = true})
    
    -- Platform-specific dependencies
    if is_plat("windows") then
        add_syslinks("ws2_32", "advapi32", "kernel32", "user32")
    else
        add_links("pthread", "rt", "stdc++fs")
    end
    
    -- Export symbols for dynamic library
    if is_plat("windows") then
        add_cxxflags("-DCODEKNIFE_EXPORTS")
    else
        add_cxxflags("-fPIC")
    end

-- Utility tests
target("test_util")
    set_kind("binary")
    add_deps("codeknife")
    add_files("test/test_main.cpp")
    add_packages("gtest")
    add_tests("default")
    if is_plat("windows") then
        add_syslinks("ws2_32")
    else
        add_links("pthread", "stdc++fs")
    end
    set_rundir("$(projectdir)")
