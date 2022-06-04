set_project("ibase")
set_languages("c99", "c++17")
add_rules("mode.debug", "mode.release")

add_requires("asio")
add_requires("spdlog")
add_requires("fmt", {configs = {header_only=true}})


if is_plat("linux", "android", "ios", "macos") then
    add_syslinks("pthread", "dl", "m", "c")
else    
    add_defines("_WIN32_WINNT=0x0501")
end

target("ibase")
    set_kind("static")
    add_files("ibase/*.cpp")
    add_headerfiles("ibase/*.hpp")
    add_includedirs("ibase", {public = true})
    add_packages("asio", "fmt")

target("exam")
    set_kind("binary")
    add_files("examples/*.cpp")
    add_deps("ibase")
    add_packages("asio", "fmt", "spdlog")