---@diagnostic disable: undefined-global, undefined-field

workspace("datasurf")
    configurations({"debug", "asan", "release"})
    platforms({"linux", "windows"})
    location("build")
    architecture("x86_64")

project("datasurf")
    language("C")
    cdialect("C99")
    warnings("Extra")
    targetname("datasurf")
    kind("StaticLib")
    toolset("clang")
    buildoptions({"-Wextra", "-Wall", "-Wpedantic", "-Wconversion", "-Wshadow",
                  "-Wsign-compare", "-Wtype-limits", "-Wunused"})
    includedirs({"./include/", "./vendor/puddle/include/"})

    filter("configurations:asan")
        defines{"ASAN"}

    filter("configurations:debug")
        defines{"DEBUG"}

    filter("configurations:debug or asan")
        runtime("debug")
        symbols("On")
        optimize("Off")
        buildoptions({"-g", "-O0"})
        linkoptions({"-g", "-O0"})

    filter("configurations:release")
        defines{"NDEBUG"}
        staticruntime("off")
        runtime("release")
        symbols("Off")
        optimize("Speed")

    filter("platforms:linux")
        system("linux")
        defines("BUILD_LINUX")
        targetdir("bin/%{cfg.buildcfg}")
        objdir("obj/datasurf")
        files({ "./src/linux_datasurf*",
                "./include/linux_datasurf*",
                "./src/datasurf_*",
                "./include/datasurf_*",
                "./vendor/puddle/src/string_view.c",
                "./vendor/puddle/src/linux_pd_path.c" })
        includedirs("/usr/include/")
        linkoptions("-fuse-ld=mold")

    filter("platforms:windows")
        system("windows")
        defines("BUILD_WINDOWS")
        targetdir("bin/%{cfg.buildcfg}")
        objdir("obj/")
        files({ "./src/win32_datasurf*",
                "./include/win32_datasurf*",
                "./src/datasurf_*",
                "./include/datasurf_*",
                "./vendor/puddle/src/string_view.c",
                "./vendor/puddle/src/win32_pd_path.c" })

    filter({"platforms:linux", "configurations:asan"})
        buildoptions({"-fsanitize=address,leak,undefined", "-fno-omit-frame-pointer",
                      "-static-libasan"})
        linkoptions({"-fsanitize=address,leak,undefined", "-fno-omit-frame-pointer",
                     "-static-libasan"})

    filter({"platforms:linux", "configurations:debug or asan"})
        buildoptions("-gfull");
        linkoptions("-gfull");

    filter({"platforms:windows", "configurations:debug or asan"})
        buildoptions("-gcodeview");
        linkoptions("-gcodeview");

    filter({"platforms:windows", "configurations:asan"})
        toolset("clang-cl")
        buildoptions({"/fsanitize=address", "/Zi", "/INCREMENTAL:NO"})
        linkoptions{"/link clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"}
        editandcontinue("Off")

project("datasurftest")
    language("C")
    cdialect("C99")
    warnings("Extra")
    targetname("datasurftest")
    kind("ConsoleApp")
    toolset("clang")
    buildoptions({"-Wextra", "-Wall", "-Wpedantic", "-Wconversion", "-Wshadow",
                  "-Wsign-compare", "-Wtype-limits", "-Wunused"})
    links("datasurf:static")

    filter("configurations:asan")
        defines{"ASAN"}

    filter("configurations:debug")
        defines{"DEBUG"}

    filter("configurations:debug or asan")
        staticruntime("off")
        runtime("debug")
        symbols("On")
        buildoptions({"-g", "-O0"})
        linkoptions({"-g", "-O0"})

    filter("configurations:release")
        defines{"NDEBUG"}
        staticruntime("off")
        runtime("release")
        symbols("Off")
        optimize("Speed")

    filter("platforms:linux")
        system("linux")
        defines("BUILD_LINUX")
        targetdir("bin/%{cfg.buildcfg}")
        objdir("obj/datasurftest/")
        files({ "./src/linux_datasurftest*",
                "./include/linux_datasurftest*",
                "./src/datasurftest*",
                "./include/datasurftest*" })
        includedirs({ "./include/", "/usr/include/", "./vendor/puddle/include/"})
        libdirs("./bin/%{cfg.buildcfg}/")
        links("datasurf:static")
        linkoptions({"-fuse-ld=mold", "-lm"})
        toolset("clang")

    filter("platforms:windows")
        system("windows")
        defines("BUILD_WINDOWS")
        targetdir("bin/%{cfg.buildcfg}")
        objdir("obj/")
        files({ "./src/win32_datasurf*",
                "./include/win32_datasurf*",
                "./src/datasurf*",
                "./include/datasurf*" })
        includedirs({"./include/", "./vendor/puddle/include/"})
        libdirs("./bin/%{cfg.buildcfg}/")

    filter({"platforms:linux", "configurations:asan"})
        buildoptions({"-fsanitize=address,leak,undefined", "-fno-omit-frame-pointer",
                      "-static-libasan"})
        linkoptions({"-fsanitize=address,leak,undefined", "-fno-omit-frame-pointer",
                     "-static-libasan"})

    filter({"platforms:windows", "configurations:debug or asan"})
        kind("ConsoleApp")

    filter({"platforms:linux", "configurations:debug or asan"})
        buildoptions("-gfull");
        linkoptions("-gfull");

    filter({"platforms:windows", "configurations:debug or asan"})
        buildoptions("-gcodeview");
        linkoptions("-gcodeview");

    filter({"platforms:windows", "configurations:asan"})
        toolset("clang-cl")
        buildoptions({"/fsanitize=address", "/Zi", "/INCREMENTAL:NO"})
        linkoptions{"/link clang_rt.asan_dynamic-x86_64.lib clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"}
        editandcontinue("Off")
