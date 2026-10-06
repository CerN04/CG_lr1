# CG_lr1
lab 1 CG MAI

## Запуск
Необходимы установленные C++, Vulcan SDK и CMake.

Из главной папки проекта выполнить одну из команд ниже для сборки зависимостей и настройки проекта:
```bash
cmake --preset debug       # for GNU/Linux (GCC/Clang)
cmake --preset msvc-debug  # for Windows (Visual Studio 2019)
cmake --preset mingw-debug # for Windows (MinGW)
```

Чтобы собрать проект, выполните:
```bash
cmake --build build-debug --parallel # for debug
```

Для сборок `msvc-debug` выходные файлы находятся в папке `Debug`.
Для других конфигураций выходные файлы находятся в `vulkan-starter-app`.

**Убедитесь, что ваш рабочий каталог указывает на корень проекта!**

