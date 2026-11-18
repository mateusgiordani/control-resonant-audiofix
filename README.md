# Control Resonant AudioFix

A tiny, free, open-source fix for the **audio stutter / desync / music cutting out** in *Control Resonant* on PC.
No PrimoCache, no driver, no background program: one `version.dll` dropped into the game folder.

🇧🇷 [Leia em português](README.pt-BR.md)

## TL;DR

1. Download `ControlResonant-AudioFix.zip` from [Releases](../../releases).
2. Extract it anywhere and run `install.ps1` (right-click → *Run with PowerShell*), **or** copy
   `version.dll` and `audiofix.ini` into the game folder (next to `CONTROLResonant.exe`).
3. Play. To remove, run `uninstall.ps1` or delete `version.dll` from the game folder.

## Root cause

The game streams its data from 63 `.rmdblob` packages (~106 GB). It opens them with
[`FILE_FLAG_NO_BUFFERING`](https://learn.microsoft.com/en-us/windows/win32/fileio/file-buffering),
which tells Windows **not to use its RAM file cache**. That would be fine if every byte were read once,
but the game re-reads the same assets over and over:

| Measured on one PC (32 GB RAM, NVMe) | |
|---|---|
| Data requested by the game in the first **11 minutes** | **160 GB** |
| Sustained request rate while moving around | **165–310 MB/s** |
| Package files on disk | 106 GB |

Because the cache is bypassed, every one of those requests goes to the SSD. After ~20 minutes the
drive queue saturates (100% active time, high latency, low MB/s), and the audio streamer, which
needs its data on time, loses the race. You hear it as stutter, the music dropping out and lip
desync. The same pattern was reported for *Alan Wake 2* (same Northlight engine).

This is also why [PrimoCache](https://www.romexsoftware.com/en-us/primo-cache/) works: it adds a RAM block
cache *below* the flag. This fix instead removes the flag, so Windows' own cache does the job.

You can confirm it yourself: the log written by this fix shows the original open flags,
`0x60000080` = `FILE_FLAG_NO_BUFFERING | FILE_FLAG_OVERLAPPED | FILE_ATTRIBUTE_NORMAL`.

## How the fix works

`CONTROLResonant.exe` imports `VERSION.dll`, which is not a Windows *KnownDLL*, so a copy in the
game folder is loaded instead of the system one. This proxy:

1. **Forwards** all 17 `version.dll` exports to `C:\Windows\System32\version.dll`, so nothing changes there.
2. **Hooks** `CreateFileW` / `CreateFileA` in the game executable's import table (IAT). It never
   patches game code in memory and never touches the files on disk.
3. Only for **read-only** opens of files ending in `.rmdblob`, it:
   - removes `FILE_FLAG_NO_BUFFERING` (and `FILE_FLAG_WRITE_THROUGH`);
   - adds `FILE_FLAG_RANDOM_ACCESS`, which tells the cache manager to keep the data mapped
     (configurable).
4. Writes `audiofix.log` with the first 200 opens and, every 60 s, how much data the game requested.

Saves, settings and anything opened for writing are left alone.

Result on the test PC: the game kept requesting 150–300 MB/s while the actual disk reads dropped to
**0–10 MB/s** (disk 0–9% busy, queue 0), and the audio stayed in sync.

## Side effects and trade-offs

Please read these, they are honest:

- **First visit to an area still hits the disk.** A cache only helps with data that was already
  read once. Expect a brief hiccup at most when a new zone streams in for the first time.
- **Slightly more CPU.** Buffered I/O copies data from the cache into the game's buffer (one
  `memcpy`). On modern CPUs this is tiny compared to a saturated SSD, but it is not zero.
- **Windows will use free RAM as file cache.** Task Manager will show less "free" memory and more
  "cached"/standby. This is normal: standby memory is handed back instantly when a program needs it.
  Unlike PrimoCache there is no fixed reservation, and nothing competes with the game for RAM.
  With 16 GB of RAM the cache is smaller, so the improvement may be smaller.
- **Overlapped reads may complete immediately.** With the cache, some async reads finish
  synchronously. That is a documented, valid behavior and the game handled it fine in testing,
  but it is a change in timing.
- **Conflicts with other `version.dll` mods.** Only one `version.dll` can live in the folder. The
  installer refuses to overwrite one that is not ours.
- **Game updates.** Steam "Verify integrity" and updates don't remove extra files, so the fix
  survives patches. If Remedy changes how files are opened, the fix just stops matching and does
  nothing, or remove it. If Remedy fixes the bug, delete `version.dll`.
- **No data-loss risk.** Unlike a write-back RAM cache (PrimoCache *Defer-Write*), nothing is
  written through this fix, so a crash or power cut can't corrupt anything.
- **Online/anti-cheat.** The game has no anti-cheat. This is a standard DLL proxy, like many mods
  (ReShade, SpecialK, etc.). Use at your own risk, as with any mod.

## Configuration (`audiofix.ini`)

```ini
[AudioFix]
Enabled=1        ; 0 = don't change anything, just log (useful for A/B comparison)
RandomAccess=1   ; add FILE_FLAG_RANDOM_ACCESS
Log=1            ; write audiofix.log
StatsSeconds=60  ; stats interval, 0 = off
Extensions=.rmdblob
```

## Verifying it works

Open `audiofix.log` in the game folder after playing:

```
AudioFix carregado em ...\CONTROLResonant.exe
IAT: CreateFileW=1 CreateFileA=1 ReadFile=1 entradas interceptadas
open \\?\...\data_pack2\pc\base-generic-000.rmdblob flags=0x60000080 -> 0x50000080  (NO_BUFFERING removido)
stats: aberturas=61 (modificadas=61) leituras=1094977 total=150.52 GB  ultimo intervalo=305.9 MB/s  RAM livre=15.4 GB
```

Compare `ultimo intervalo` (what the game asks for) with the disk read speed in Task Manager
(what actually hits the SSD). *(Log messages are in Portuguese.)*

## Building from source

Requires Visual Studio 2022 Build Tools (C/C++ workload).

```
build.bat          # -> build\version.dll
build.bat test     # also builds build\test\test.exe
```

`test.exe` simulates the game: it opens a `.rmdblob` with `NO_BUFFERING` and does an unaligned read,
which fails with error 87 without the DLL and succeeds with it.

## Reporting the bug to Remedy

This is a workaround, not a real fix. Please report it so it gets patched:
<https://remedy.helpshift.com/hc/en/5-control-resonant/> → chat bubble → *Bug Reports*.

## License

MIT. Not affiliated with Remedy Entertainment.
