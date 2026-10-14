# Presentation gate - agents/frontend a96f841 watchdog, pass-4 routing Debug a96f841bd6eaaa4ad8fdf255fabcd230b436ad7b

Verdict: **VISUAL DEFECTS**

| | |
|---|---|
| exe | F:\Transformers Rebuild\Rebuild-Experimental\work\ab\fe_fix\build\bin\wfc_rebuild.exe |
| render data | product default (<exe>\..\..\work\render) |
| result | pass 11 / fail 2 / known 0 / info 0 / skip 0 / human 0 / unknown 0 / waiting 0 / partial 1 |

## FAIL

- **present.watchdog.customization.softlock** [Frontend/AssetTools]: customization: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: none; after 3 Back press(es) the frame matches the main menu False (scene similarity 0.12, still-inside similarity 0.073); focus present in the screen's dumps True
- **present.watchdog.customization.classification** [Frontend/AssetTools]: customization: DISPLAY CORRECT (automated part). character model loaded for the preview: False; 'LOADING' still shown: False

## Other

- PASS **present.watchdog.extras_movies.softlock**: extras_movies: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.622, still-inside similarity 0.219); focus present in the screen's dumps True
- PASS **present.watchdog.extras_movies.classification**: extras_movies: FULLY FUNCTIONAL. a movie started: True
- PASS **present.watchdog.extras_credits.softlock**: extras_credits: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 2 Back press(es) the frame matches the main menu True (scene similarity 0.611, still-inside similarity 0.145); focus present in the screen's dumps True
- PARTIAL **present.watchdog.extras_credits.classification**: extras_credits: SCREEN PRESENT. after Accept on Credits the screen differs from the Extras list: True (similarity 0.046)
- PASS **present.watchdog.extras_concept.softlock**: extras_concept: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 2 Back press(es) the frame matches the main menu True (scene similarity 0.587, still-inside similarity 0.542); focus present in the screen's dumps True
- PASS **present.watchdog.extras_concept.classification**: extras_concept: FULLY FUNCTIONAL. 
- PASS **present.watchdog.accounts_create.softlock**: accounts_create: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.615, still-inside similarity 0.247); focus present in the screen's dumps True
- PASS **present.watchdog.accounts_create.classification**: accounts_create: FULLY FUNCTIONAL. typed 'WFCQA' with ordinary key events: text in a dump True; frame changed after typing True
- PASS **present.watchdog.settings_controls.softlock**: settings_controls: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 3 Back press(es) the frame matches the main menu True (scene similarity 0.622, still-inside similarity 0.468); focus present in the screen's dumps True
- PASS **present.watchdog.settings_controls.classification**: settings_controls: FULLY FUNCTIONAL. Mouse / Keyboard Layout card (read-only by design, CONFIRMED ORIGINAL per Frontend decompile): 42 binding description texts visible (need >= 6; e.g. MOVE / W,A,S,D OR ARROW KEYS / TURN / LOOK AROUND / MOUSE)
- PASS **present.watchdog.back_forward.softlock**: back_forward: script finished; alternative exit (Cancel / Start / Esc) reaches the main menu: not needed; after 0 Back press(es) the frame matches the main menu True (scene similarity 0.632, still-inside similarity 0.995); focus present in the screen's dumps True
- PASS **present.watchdog.back_forward.classification**: back_forward: FULLY FUNCTIONAL. 
