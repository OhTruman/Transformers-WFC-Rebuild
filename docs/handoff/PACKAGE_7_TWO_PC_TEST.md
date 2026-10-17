# Package 7: the two-PC network test (about 15 minutes)

**What this is.** Package 7 carries online phase 1: the network layer that keeps two PCs' matches identical. This test runs a
5-minute bot match on Streets **in the background on both PCs at once, over your home network**. Every 1/60 s each PC checks that
its match is identical to the other's, and your microphone's voice is sent between the PCs. You see no picture: it is a test of the
network and sync, run with the bots playing both seats.

**What is not in package 7:** hosting or joining from the game's menus, and an online match you see and play yourself. Those need a
real online session behind the Host / Join menus and the online match drawn on screen with your own controls; they are the next
online step (phase 2). Playing package 7 normally (Play.bat) is unchanged and offline.

## 1. Set up the second PC (PC B)

1. Copy the **whole package folder** (the one with Play.bat, about 29 GB) to PC B, to a short path such as `D:\WFC` (deep folders
   break some file paths).
2. Both PCs on the **same home network** (wired or Wi-Fi), with the network set to **Private** in Windows.
3. PC B needs no graphics card for this test.

## 2. Run it

1. **PC B:** open a Command Prompt, run `ipconfig`, note the **IPv4 Address** (e.g. 192.168.1.23).
2. **PC A** (this PC): double-click **Play_NetTest_Host.bat** in the package folder. It shows PC A's IPv4 address - note it - and asks
   for PC B's address: type it and press Enter.
3. **PC B**, within 3 minutes: double-click **Play_NetTest_Join.bat**, type **PC A's** address, press Enter.
4. If Windows asks whether to allow **wfc_rebuild.exe** on the network, choose **Allow** (Private networks) on both PCs.
5. Both windows say "Running ... (about 6 minutes)". The microphone icon appears while it runs: speak, play music near the mic, or
   stay quiet - all fine. Wait until each window says **Finished: exit code N**.

## 3. What to look for

- **Exit code 0 on both PCs** = the two matches stayed identical for all 18,000 ticks and ended together. That is the pass.
- Other codes: **3** = the matches differed (a sync bug: the files below pin it to the tick), **4** = the other PC was not found
  (wrong address, the firewall, or started more than 3 minutes apart: run both again), **5** = PC A stopped early, **6** = the two
  PCs are not running the same package (copy it again), **7** = the network port could not be opened (another program on UDP port
  14000).

## 4. Send back from **each** PC

The folder **NetTest_results** inside the package folder (three files):

| file | what it tells us |
|---|---|
| `wfc.log` | `NET all 2 seats here`, the `NET input delay` lines (how the delay adapted to your network, with the round trip time), `NET done ... status 0` or a `NET DESYNC at tick ...` line, and the voice lines (`voice capture: started`, `voice: sent ... received ... played ...`) |
| `net.csv` | one row per tick: the state hash, the command hash, any stall and the bytes in / out - compared side by side |
| `record.bin` | the recording of every tick's commands: replays the match here exactly, to find any difference |

Label them (e.g. `PC-A`, `PC-B`). Nothing in them is audio: the voice is only counted, never saved.
