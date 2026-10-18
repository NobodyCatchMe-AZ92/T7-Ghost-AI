# Native end-of-match menu - feasibility study (2026-10-19)

Question: can the Ghost Battle options (Rematch, Rematch on a new stage, 3 next opponents, Character
Select, Main Menu) live in the game's own result menu instead of the mod's ImGui overlay?

## What the game's menus are

- Tekken 7's UI is **Scaleform GFx** (Flash movies, ActionScript 3) through Unreal's ScaleformUI plugin
  (`Engine\Source\Runtime\ScaleformUI\...` paths and `Scaleform::GFx` symbols in the exe; 17 Scaleform
  exports, none of them the Movie API).
- The movies live in the `.pak` files (`Content\Paks`). The game loads `*_P.pak` patch paks from
  `Content\Paks\~Mods`, and the community already ships costume/stage mods that way, so overriding a menu movie with a pak is an established route.
- Menus talk back to C++ through Flash's `fscommand` / `ExternalInterface.call`. In the exe the AS
  `getURL("FSCommand:...")` path is at `0x1F59742` / `0x1F59D44`: it calls the movie's FSCommand
  handler (`[movie + 0x238]`, virtual slot 1: `(handler, movie, command, args)`); the
  ExternalInterface path is next to the string at `0x2EDA030` (used at `0x1F1C723`).
- The result menu's state is visible from C++: `[exe+0x34D6C04]` != 0 while it is up (already used).
  Its cursor is not in the exe's data section (it lives in the movie).

## Options

| | How | Look | Effort / risk |
|---|---|---|---|
| A. Overlay (current) | ImGui menu over the native one, XInput hook, scripted presses | Tekken-like, not native | done, works |
| B. Runtime Scaleform | hook the FSCommand / ExternalInterface handlers to learn the result menu's protocol, then add entries by calling the movie's own AS functions (`GFx::Movie::Invoke` - must be found by RE, it is not exported) and catch the new entries' callbacks | native | high: find Invoke / the menu's AS functions; one wrong call can crash the UI |
| C. Pak mod | extract the result-menu movie (.gfx/.swf) from the paks, add 5 entries in a Flash editor (JPEXS FFDec), the new entries send new FSCommands that the DLL catches (then it sends the native "Next match" / "Main menu" command itself) | native | high: needs the pak tools and the movie, a second file to ship (`~Mods\GhostBattle_P.pak`), and must be redone if the game ever updates |

## Recommendation

Keep A as the shipping menu (it works and needs no extra files). If a native look is wanted, the next
step for both B and C is the same cheap recon: hook the FSCommand / ExternalInterface handlers and log
every command the result menu sends while navigating it. That tells which commands "Next match",
"Start Over" and "Return to Main Menu" are and whether the menu is built from C++ data (then B, adding
entries at runtime) or fixed in the movie (then C, a pak).

The rename ("GHOST BATTLE") can also go either way: runtime memory patch (`src/gbtext.hpp`, being
tested) or a pak overriding the localization / the movie's text.
