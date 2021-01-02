include "libs/PrLib"

workspace "HogeProject"
    location "build"
    configurations { "Debug", "Release" }
    startproject "main"

architecture "x86_64"

externalproject "prlib"
	location "libs/PrLib/build" 
    kind "StaticLib"
    language "C++"

project "main"
    kind "ConsoleApp"
    language "C++"
    targetdir "bin/"
    systemversion "latest"
    flags { "MultiProcessorCompile", "NoPCH" }

    -- Src
    files { "main_simple.cpp" }

    -- Helper
    files { "libs/d3dx12/*.h" }
    includedirs { "libs/d3dx12/" }

    links { "dxgi" }
    links { "d3d12" }

    -- HLSL compiler
    links { "libs/dxc_2020_10-22/lib/x64/dxcompiler" }
    postbuildcommands { 
        "{COPY} ../libs/dxc_2020_10-22/bin/x64/dxcompiler.dll ../bin",
        "{COPY} ../libs/dxc_2020_10-22/bin/x64/dxil.dll ../bin",
        "mt.exe -manifest ../utf8.manifest -outputresource:$(TargetDir)$(TargetName).exe -nologo"
    }

    -- prlib
    -- setup command
    -- git submodule add https://github.com/Ushio/prlib libs/prlib
    -- premake5 vs2017
    dependson { "prlib" }
    includedirs { "libs/prlib/src" }
    libdirs { "libs/prlib/bin" }
    filter {"Debug"}
        links { "prlib_d" }
    filter {"Release"}
        links { "prlib" }
    filter{}

    symbols "On"

    filter {"Debug"}
        runtime "Debug"
        targetname ("Main_Debug")
        optimize "Off"
    filter {"Release"}
        runtime "Release"
        targetname ("Main")
        optimize "Full"
    filter{}
