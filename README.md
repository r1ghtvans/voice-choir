# Voice Choir

VST3 / AU / Standalone плагин: превращает один голос в хор (ансамбль из 1–16 «певцов»).

## Как это работает
Каждый певец — отдельная копия вашего голоса со своим «характером»:
- сдвиг высоты (детюн) + индивидуальное вибрато + медленный случайный дрейф,
- своя задержка (разброс по времени),
- своя тембровая окраска (lowpass/highpass) и позиция в стерео.
Сверху — общий ревер.

## Параметры
Voices, Detune, Timing Spread, Vibrato Depth/Rate, Drift, Tone Variation,
Stereo Width, Reverb Size/Mix, Dry/Wet, Output.

## Сборка через GitHub
1. Загрузите репозиторий на GitHub, ветка `main`.
2. Вкладка **Actions** → сборка запустится сама (Windows, macOS, Linux).
3. Готовые плагины — в **Artifacts** внизу страницы запуска.
4. Для релиза: `git tag v0.1.0 && git push --tags` — архивы появятся в **Releases**.

## Локальная сборка
    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

## Установка
- Windows: положите `Voice Choir.vst3` в `C:\Program Files\Common Files\VST3`
- macOS: `~/Library/Audio/Plug-Ins/VST3` (или `Components` для AU)
- Linux: `~/.vst3`

## Лицензия
Используется JUCE (AGPLv3 / коммерческая лицензия). Для коммерческого распространения
закрытого плагина нужна лицензия JUCE.

## Планы
- Гармонический режим (MIDI-аккорд → хор поёт ноты)
- Формантный сдвиг, собственный GUI
