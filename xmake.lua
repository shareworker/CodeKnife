-- Set common options for MinGW/MSVC compatibility
set_languages("cxx17")  -- Use C++17 for better MinGW compatibility
set_warnings("none")    -- Avoid MSVC D9025 override by not mixing /Wn levels

-- External dependencies
-- Declare glib on platforms that need it (Linux)
if is_plat("linux") then
    add_requires("glib")
end

-- Platform detection and flags
if is_plat("windows") then
    -- Windows (MSVC/MinGW). Keep warnings controlled globally via set_warnings("none")
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
add_includedirs("include", "include/cobject", "include/util")

-- CodeKnife dynamic library - unified library containing all components
target("codeknife")
    set_kind("shared")
    -- Source files
    add_files("src/util/*.cpp")
    add_files("src/cobject/*.cpp")
    -- Exclude non-target platform dispatcher
    if is_plat("windows") then
        remove_files("src/cobject/event_dispatcher_linux.cpp")
    else
        remove_files("src/cobject/event_dispatcher_win.cpp")
    end
    add_headerfiles("include/**.hpp")  -- Include all header files recursively
    add_includedirs("include", "include/cobject", "include/util", {public = true})

    -- Platform-specific dependencies
    if is_plat("windows") then
        add_syslinks("ws2_32", "advapi32", "kernel32", "user32")
    else
        add_links("pthread", "rt", "stdc++fs")
        add_packages("glib")
    end

    -- Export symbols for dynamic library
    if is_plat("windows") then
        add_cxxflags("-DCODEKNIFE_EXPORTS")
    else
        add_cxxflags("-fPIC")
    end

-- CodeKnife static library - for tests to avoid DLL dependency issues
target("codeknife_static")
    set_kind("static")
    -- Source files
    add_files("src/util/*.cpp")
    add_files("src/cobject/*.cpp")
    -- Exclude non-target platform dispatcher
    if is_plat("windows") then
        remove_files("src/cobject/event_dispatcher_linux.cpp")
    else
        remove_files("src/cobject/event_dispatcher_win.cpp")
    end
    add_headerfiles("include/**.hpp")  -- Include all header files recursively
    add_includedirs("include", "include/cobject", "include/util", {public = true})

    -- Platform-specific dependencies
    if is_plat("windows") then
        add_syslinks("ws2_32", "advapi32", "kernel32", "user32")
        -- Static linking flags for Windows (including pthread)
        add_cxxflags("-static-libgcc", "-static-libstdc++", "-static")
        add_ldflags("-static-libgcc", "-static-libstdc++", "-static")
    else
        add_links("pthread", "rt", "stdc++fs")
        add_packages("glib")
    end

-- Utility tests
target("test_util")
    set_kind("binary")
    add_deps("codeknife_static")  -- Use static library to avoid DLL issues
    add_files("test/test_main.cpp")
    add_tests("default")
    if is_plat("windows") then
        add_syslinks("ws2_32")
        -- Add static linking flags to fix runtime library issues (including pthread)
        add_cxxflags("-static-libgcc", "-static-libstdc++", "-static")
        add_ldflags("-static-libgcc", "-static-libstdc++", "-static")
    else
        add_links("pthread", "stdc++fs")
        add_packages("glib")
        -- Set LSan suppressions for known static container leaks in debug mode
        if is_mode("debug") then
            add_runenvs("LSAN_OPTIONS", "suppressions=$(projectdir)/.lsan_suppressions")
        end
    end
    set_rundir("$(projectdir)")
