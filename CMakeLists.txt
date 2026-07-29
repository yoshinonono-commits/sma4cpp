# Sma4Py の C++/Qt 再実装 (Windows 向け、フェーズ1: コアのみ)。
#
# Python版は requirements.txt + pip でライブラリを揃えたが、
# C++版はビルド時に静的にリンクする。ここが最初の大きな違い:
# 「実行時に何を読み込むか」ではなく「ビルド時に何を組み込むか」で
# 依存関係を考える。
cmake_minimum_required(VERSION 3.16)
project(sma4cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Eigen3 REQUIRED NO_MODULE)

# --- コアライブラリ (GUI非依存。Windows/Linux/macOSどれでもビルドできる) -----
add_library(sma4core STATIC
    src/expr_eval.cpp
    src/fitting.cpp
)
target_include_directories(sma4core PUBLIC src)
target_link_libraries(sma4core PUBLIC Eigen3::Eigen)

if (MSVC)
    target_compile_options(sma4core PUBLIC /W4)
else()
    target_compile_options(sma4core PUBLIC -Wall -Wextra)
endif()

# --- コアのテスト (GUI無し。python版の tests/test_features.py に相当) -------
enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE sma4core)
add_test(NAME core COMMAND test_core)

# --- GUI (フェーズ2: 未実装。Qt6 が見つかったときだけビルド対象にする) -------
find_package(Qt6 QUIET COMPONENTS Widgets)
if (Qt6_FOUND)
    message(STATUS "Qt6 が見つかりました。GUI は今後 src/gui/ に追加します (フェーズ2)。")
else()
    message(STATUS "Qt6 が見つからないため GUI はスキップします (コアのみビルド)。"
                    " Windows開発機で Qt6 を入れれば GUI 部分をビルドできるようにする予定です。")
endif()
