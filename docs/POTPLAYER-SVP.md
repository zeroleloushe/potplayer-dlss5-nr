# PotPlayer + SVP portable — куда вставлять

Типичный портативный стек:

```
PotPlayerMini64.exe
AviSynth+ / AviSynth Filter (CrendKing)     ← SVP remote control пишет скрипт сюда
SVP 4 Manager
Renderer: Built-in D3D11  или  madVR
```

Граф, который должен получиться:

```
Built-in / LAV decoder  (DXVA2 copy-back, D3D11)
        ↓
AviSynth Filter  =  AvsFilterSource + SVSuper/SVAnalyse/SVSmoothFps
        ↓
DLSS5NR()          ← две строки в конце скрипта
        ↓
D3D11 / madVR
```

## Что не трогать

- `main.setup.potplayer.native` должен остаться **false**. Native AviSynth PotPlayer’а ломает смену размера кадра у SVP.
- Copy-back обязателен: NR живёт на CPU-staged BGRA, native DXVA поверхность ему не отдать.
- «D3D11 GPU Super Resolution» в PotPlayer — это VSR, выключить.

## Как проверить, что фильтр в графе

Во время воспроизведения: `Ctrl+F1` (Playback / System Info) или Tab. В списке фильтров должен быть AviSynth Filter. После патча скрипта в логе SVP / Overlay не должно быть `Script error: there is no function named DLSS5NR` — это значит DLL не в `plugins64`.

## Порядок относительно SVP

**После** интерполяции (рекомендация v0.1): NR обрабатывает уже 48/60 fps. Дороже по GPU, зато картинка та, что идёт на экран.

**До** интерполяции: меньше кадров через NR, но SVP анализирует уже «нейро» картинку — MV могут поехать. Не первый эксперимент.

## Если картинка не меняется

1. `nvngx_dlssnr.dll` лежит в `%LOCALAPPDATA%\potplayer-dlss5-nr\runtime\`.
2. Рядом с `DLSS5NR.dll` есть `nvngx.dll_pot.dll` (имя нельзя менять).
3. Статус `0xBAD00002` — шим не тот или не загрузился, return address не из `*nvngx.dll*`.
4. Статус `0xBAD00001` — этот билд NR runtime не для вашей карты.
5. Подпись DLL: HashMismatch на `nvngx_dlssnr.dll` часто даёт вечный FAIL. Нужен подписанный 310.8.
