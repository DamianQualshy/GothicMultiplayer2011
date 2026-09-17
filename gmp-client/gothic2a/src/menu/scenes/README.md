# Native main-menu scenes

Players select **GMP Options > Extended menu scenes**. **No** uses the original
camera/sword scenes; **Yes** uses the training and walking scenes. The choice is
saved in the existing client configuration and takes effect immediately. Basic
scenes are the default. **Tab** in the main menu cycles the selected group.

## Author cameras and actors

Each extended scene keeps all its settings at the top of its own `.cpp` file:

- [extended/credits_walk_scene.cpp](extended/credits_walk_scene.cpp): world,
  camera position/rotation, actor names, spawn/destination waypoints, visuals,
  walking speed and reset timing.
- [extended/training_scene.cpp](extended/training_scene.cpp): world, camera
  position/rotation, actor spawn/facing, visuals, armor, weapon state and individual animations.

These are private C++ constants alongside the scene's behavior. Each scene owns
its settings independently; adding another scene does not require a shared setup
file. The scene headers contain only their interfaces and runtime state.

| Setting | What to edit |
| --- | --- |
| `kTrainingCamera`, `kCreditsCamera` | Exact world `camera_position` and `camera_pitch`, `camera_yaw`, `camera_roll` in degrees. Y is up; no automatic camera height adjustment is applied. |
| `kTrainees` | One `MenuNpcDefinition` per actor, including spawn, appearance, equipment, initial animation and whether to draw the sword. The array size is inferred. |
| `kTrainingYaw` | Training NPC facing in degrees, overriding each waypoint's direction. |
| `kCreditsWalkers` | One `MenuNpcDefinition` per walking actor. The array size is inferred. |
| `kCreditsWalkSpeed` | Walking speed in world units/second; turning rate scales with it automatically. |
| `kCreditsResetDelay` | Pause in seconds after all actors reach their destinations and vanish, before the whole group reappears at its spawn points. |
| `kWorld` in each scene | Required loaded world for that scene. Changing this does not load another world; adjust its camera and waypoint names to match. |

### NPC definitions

Both scenes pass the same `MenuNpcDefinition` directly to `MenuNpc::Create`.
Edit an entry in `kTrainees` or `kCreditsWalkers`; no separate appearance, equipment
or label setup is required. Entries can omit fields to use their defaults. C++20
named initializers use a leading dot and must follow the declaration order below;
fields can be skipped.

| Field | Default and behavior |
| --- | --- |
| `name` | Empty label text. |
| `name_color` | `{255, 255, 255}`; red, green and blue values from 0 to 255. |
| `name_font` | `CP1250_FONT_DEFAULT.TGA`; an explicit empty string also selects this default. |
| `name_show` | `false`; set `true` to draw a nonempty name above the head. Works in both scenes. |
| `name_fade_start` | `1000.0f`; camera-to-NPC distance in world units where the name begins fading out. Must be finite and nonnegative. |
| `name_fade_end` | `2000.0f`; the name is completely hidden at or beyond this distance. Must be finite and greater than `name_fade_start`. |
| `instance` | `PC_HERO`; an explicit empty string also selects this default. Other values name an existing `C_NPC` instance in the loaded scripts. |
| `visual` | Body model `HUM_BODY_NAKED0`, body texture `0`, head model `HUM_HEAD_PONY`, head texture `0`, in that order. Applied only to `PC_HERO`, through native `SetAdditionalVisuals`; skin color, teeth texture and the armor argument are all `0`. Ignored for every other NPC instance. |
| `overlay` | Empty; adds no overlay. A nonempty model overlay such as `HUMANS_MILITIA.MDS` is applied only to `PC_HERO`. Ignored for every other NPC instance. |
| `equip_melee` | Empty; no equipment override. Otherwise an item instance for a one- or two-handed melee weapon. |
| `equip_ranged` | Empty; no equipment override. Otherwise a bow or crossbow item instance. |
| `equip_armor` | Empty; no equipment override. Otherwise a torso armor item instance. Armor determines the dressed body mesh. |
| `spawn_wp` | Empty; must be set to a valid waypoint in the loaded ZEN. |
| `end_wp` | Empty; used only when scene behavior explicitly calls `WalkToEndWaypoint`. Setting it alone does not start walking. |
| `freeze_on_wp` | `true`; standalone animations stay at the actor's current placement. Set `false` to apply the animation's authored root movement and yaw. Explicit walking still follows its route in either mode. |
| `animation` | Empty; leaves animation playback to scene code. Otherwise `MenuNpc::Create` starts this clip after placement and equipment setup. Each actor can choose a different clip. |
| `animation_loop` | `true`; restart the initial clip when it finishes. `false` disables these restarts; the animation's native loops and follow-ups still apply. Use a non-looping clip for a one-off action. |
| `draw_melee` | `false`; leave melee weapons sheathed. Set `true` to draw the equipped sword/axe before starting the initial animation. Requires an equipped melee weapon. |

Instance names are resolved case-insensitively. The selected native instance
supplies its original appearance, overlays and inventory. `visual` and `overlay`
overrides are skipped entirely for non-`PC_HERO` instances, even if those fields
contain invalid asset names. Empty equipment fields keep any inherited equipment.
TrainingScene has two sword practitioners using `T_1HSFREE` with `draw_melee = true`
and a third actor using the arms-crossed `S_LGUARD` pose with `draw_melee = false`.
The observer keeps its sword sheathed and stays anchored with `freeze_on_wp = true`.
Its nameplate is hidden by default; enable `name_show` and edit `name` to label it.
Add more definitions to `kTrainees`; its size is inferred, and scene startup,
updates, labels and cleanup handle the whole group. All these initial-animation
fields belong to the shared NPC definition and are available in other scenes too.

The two sword practitioners set `freeze_on_wp = false`. Their animation's local root
displacement is applied in the direction each NPC faces, including any authored
vertical movement. Root rotation uses yaw, matching the engine's normal behavior.
Set the field to `true` for any trainee that should stay anchored. Animation loops
keep accumulated movement; `ResetToSpawn` explicitly returns an actor to its WP.
Scene actors remain under manual control with physics and normal AI disabled.

Names retain their RGB color and fade linearly between `name_fade_start` and
`name_fade_end`. Distance is measured from the scene camera, not the hidden player.
The resulting opacity is also multiplied by the NPC's native render alpha, which
accounts for the world's object draw distance, model fade and visual transparency.
Zero-alpha labels are not drawn. An invalid fade interval hides only the label.
Set these fields in each scene's NPC definitions to tune the distances.

`ResetToSpawn` places an actor at its definition's `spawn_wp`;
`WalkToEndWaypoint` starts a waynet route to its `end_wp`. The credits scene calls
both for each actor on reset. Training actors use their individual `animation`,
`animation_loop` and `draw_melee` settings during creation. For behavior that changes
over time, scene code can explicitly call `PlayAnimation`, `DrawMeleeWeapon` and
`WalkToWaypoint` on each actor. In `Update`, use `HasAnimationFinished` or
`HasReachedDestination` to advance that actor to its next action once. Use
`animation_loop = false` with a non-looping clip if waiting for it to finish.
`ResetToSpawn` only places the actor; it does not replay the initial animation.

The supplied positions and waypoint names target Gothic II's
`NEWWORLD\NEWWORLD.ZEN`. Use connected, dry walking paths. Each endpoint can be
anywhere along a supported route. Actors spawn at the named waypoint, face its
direction, and are placed with their feet on the ground. Missing spawn names never silently
fall back to a nearby point or a position relative to the camera.

`WalkToWaypoint` uses `zCWayNet::FindRoute` from the last placed/reached waypoint,
then follows the resulting sequence of connected waypoints. Routes are copied
to owned positions and the temporary native `zCRoute` is destroyed immediately.
Facing turns gradually through the shortest angle, with the rotation limit derived
from walking speed across the entire frame. At 120 world units/second, the limit
is 180 degrees/second; halving walking speed halves the turning rate as well.
Position continues along the connected route; smoothing does
not cut corners across geometry. Walking clips do not add their root movement
on top of the route's movement, even with `freeze_on_wp = false`.
Movement samples ground and checks static geometry between points. It is intended
for ordinary footpaths, not ladders, swimming, jumping, doors or NPC routines.
Unsupported or obstructed routes fail; there is no straight-line fallback.
Use `PlaceAtWaypoint` before starting another route if movement was interrupted
or a standalone animation moved the actor away from its waypoint.
`WalkTo(position)` remains available for an explicitly authored direct segment.

Each credits actor follows its own route to `end_wp`. On arrival, its model,
equipped visuals and nameplate vanish immediately while the other actors keep
walking. Arrived actors remain allocated and healthy but stop receiving scene
updates. Once everyone has arrived, `kCreditsResetDelay` elapses and the group
returns to its spawn points. All routes are restored before the actors are shown
together again. This loop reuses the existing NPCs and is independent of camera
position or facing; actors can have different destinations.

Creation applies additional visuals and overlays before equipment. Armor and
weapons are local inventory items owned by the actor. Newly created equipment
overrides have their equip/unequip script callbacks and item effects disabled;
stock item definitions provide their meshes and equipment data.
Weapon visuals are loaded with `CreateVisual` after inventory insertion, before
being attached to the actor. A fresh `CreateItem` can legitimately have no loaded
visual yet; an actual mesh load failure still stops the scene with a diagnostic.
The native NPC initializer runs to obtain the selected instance's authored data;
use instances whose constructors are suitable for temporary menu actors. Any
registered daily routine and initial AI state are removed immediately afterward.
Parser instance bindings and the global AI switch are restored after creation.
Actors have no multiplayer identity, active script AI state, daily routine,
perception, Lua registration or synchronization. Sleeping actors have their model
animations advanced explicitly. Animation movement is applied only for standalone
clips with `freeze_on_wp = false`; route movement is controlled separately.

## Scene code and lifetime

`basic/` contains the original camera/sword scene implementation. `extended/`
contains the actor scenes, with each scene's settings in its own `.cpp` file.
Both groups are registered in [scene_registry.cpp](scene_registry.cpp); the
original camera presets live in `basic/newworld_scenes.h`. `RegisterBasicMenuScenes` and
`RegisterExtendedMenuScenes` keep the pools separate for the GMP Options switch;
extended mode includes the default basic scene only as a fallback.

Add new behavior in a scene's `Start`, `Update`, `Render` and `Stop` methods, then
register a factory in the appropriate group in `scene_registry.cpp`.
Use the shared helpers in `../scene/` for NPCs,
camera access and persistent nameplates. `SceneManager::ResetActiveScene` recreates
the current scene; the credits scene also performs its own internal resets.

`EnterMenuState` only bootstraps the menu and immediately transitions to its UI
states. The manager therefore belongs to `MenuContext`, lasting across those UI
states. Scene updates run before world rendering; nameplates draw after world
rendering and before the menu UI. Names belong to the actors, and the renderer
stores no borrowed NPC pointers or persistent screen entries.
Labels use the same immediate pixel-coordinate drawing path:
world-to-camera transform, native projection, then centered `screen->PrintChars`.
The screen font, color and renderer blend mode are restored afterward. No detached `VIEW_ITEM` or
queued text is used.

The manager owns one scene. Switching modes/scenes, starting a connection,
disconnecting, re-entering the menu and leaving for gameplay release actors and
nameplates before content/world replacement. NPC wrappers retain their world,
remove actors through the virtual world API and release ZenGin references.
The camera helper restores the engine camera's transform, AI and sleeping state.
Partially initialized or unhealthy scenes are cleaned up and skipped for the rest
of that configuration; extended mode retains the original basic scene as a fallback.

## Verification

Unhandled client crashes are configured to save a local BugTrap report in the
game's `Multiplayer/crash-dump/` directory. The report includes a minidump, native
crash details and a copy of `GMPLog.log`, preserving the scene lifecycle log before
the next launch replaces it. Keep the report and the matching client binary/PDB
when investigating an intermittent crash. Report generation still needs runtime
verification; a successful scene run alone does not establish that a crash is fixed.

Source checks cover the native APIs, authoring defaults, waypoint graph,
localization JSON and patch whitespace. Runtime rendering and lifecycle
validation are still pending. In-game checks should cover:

- Toggle basic/extended mode, restart the client, and cycle scenes with Tab.
- Confirm each actor's spawn, facing, body/head and armor. The first two practice
  with drawn swords; the third stands with crossed arms and a sheathed sword.
  With `freeze_on_wp = false`, check authored movement and yaw
  across multiple loops. With `true`, check that the actor remains anchored.
- Check walking turns at corners, including headings across the -180/180-degree
  boundary and multiple waypoint crossings in a frame. Change walking speed and
  confirm turning scales with it while actors still reach their configured endpoints.
- Watch several credits loops with staggered arrivals: each NPC and its label
  disappear only at its own `end_wp`; remaining NPCs keep walking. After the last
  arrival and reset delay, everyone reappears at their configured spawn points.
  Try destinations in front of the camera and different endpoints per NPC.
- Change screen resolution; confirm labels stay centered and clipped correctly.
- Set different label colors/fonts and enable the trainee's label. Check that
  omitted or empty `instance` and `name_font` use their documented defaults.
- Watch labels appear gradually as NPCs approach: full distance opacity below
  `name_fade_start`, fading between the endpoints, and no label at or beyond
  `name_fade_end`. Lower object draw distance and verify labels also follow the
  NPCs' native fade without changing the menu UI's opacity.
- Use a non-`PC_HERO` instance with invalid `visual`/`overlay` settings; confirm
  its original appearance is preserved and its routine does not take control.
- Try an already applied overlay, inherited equipment, and each equipment
  override, including two-handed melee weapons and bows/crossbows.
- Repeatedly switch scenes, connect/fail/cancel, enter gameplay and return to the
  menu. Check for duplicate NPCs, retained equipment, labels or disabled controls,
  and verify camera/music restoration and stable NPC/vob counts.
- Temporarily use an invalid waypoint, item or animation in the C++ setup;
  confirm one useful log entry, cleanup of partial actors and a working fallback.
