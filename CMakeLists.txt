cmake_minimum_required(VERSION 3.10)
project(Notepad)

set(CMAKE_CXX_STANDARD 20)

# Garante que o compilador encontre o picosha2.h e o icon.ico na pasta do projeto
include_directories(${CMAKE_CURRENT_SOURCE_DIR})

# Adiciona o main.cpp e o resource.rc (que contém o ícone)
add_executable(Notepad WIN32 main.cpp)

# Linka as bibliotecas do Windows para interface e som
target_link_libraries(Notepad comctl32 winmm)

# --- CONFIGURAÇÃO PARA RELEASE (ESTÁTICO) ---
# Isso evita os erros de "libgcc_s_seh-1.dll" e "libstdc++-6.dll" no PC dos outros
if(MINGW)
    target_link_options(Notepad PRIVATE "-static-libgcc" "-static-libstdc++" "-static")
endif()

# Caso você use MSVC (Visual Studio) no futuro, isso embuti o runtime
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()