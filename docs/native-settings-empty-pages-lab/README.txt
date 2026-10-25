Native Settings UI empty-page investigation
===========================================

Date: 2026-10-25. Game: 2.50 / 3.0.88.46235.
Installed Native Settings UI: 1.97. Installed CET port: 1.38.0.

Finding
-------
A local CET regression rejects valid derived-to-base native struct arguments.
The exact-type check added to ClassConverter::ToRED in
sources/cyber-engine-tweaks/src/reverse/Converter.h:207 rejects an inkTextRef
where inkWidgetRef is required. Native Settings UI calls
this.descriptionText:SetVisible(false) immediately after clearing the options
list. The conversion fails before it schedules creation of subcategory
headings or controls. Mod tabs are populated by a separate callback and remain
visible. This reproduces the user's precise symptom: all three tabs exist,
with everything below them blank.

The change is ours, in patches/cyber-engine-tweaks/002-runtime-stability.patch.
The previous converter accepted any ClassReference; the new exact-type guard
attempted to reject wrong native structures but also rejected legal ancestry.
The appropriate correction must allow same-class or derived-to-base conversion
while continuing to reject unrelated types and base-to-derived conversion.
RawConverter's exact-type check is a separate contract and should not simply
be removed along with this one.

Scope of conclusion
-------------------
The installed DLL matches the workspace Release DLL. The relevant installed
library, current port source and actual 2.5 script declarations form the
failure chain. An isolated execution of the real library callback with the
extracted production conversion method reproduces the blank page; changing
only the conversion policy makes it populate again.

No live engine exception was captured, and this investigation did not launch
or instrument the game. A corrected production DLL still needs validation in
the game. This is a confirmed port defect and isolated reproduction of the
reported symptom, not a claim that all other dependencies or mod features work.

Evidence and identities
-----------------------
Local snapshots and results: build/native-settings-empty-pages/
  strict.log, ancestry.log, converter.inc, identities.json
  nativeSettings/{init.lua,Cron.lua}
  evidence/ contains captured source, relevant dependency logs, mod scripts,
  the original CET patch and an additional hash manifest.

Installed CyberEngineTweaks.dll (9654272 bytes), identical to the Release DLL:
  705094f42cdc230ce4d9ece761b101ff56dd335b405f5efcd8cf03dc44d59a86
Original CET runtime-stability patch:
  b64c62e4b3bc874de35a0256d2e050602290c8de26fe4f010086d435d2107931
Converter.h:
  9581c52a78338d7b4fa2467c4a143ac6af84405789b1a9f0dad3df54ee20c25d
Extracted ClassConverter::ToRED method:
  63d664398e1cd12000551dc88ba7c45158e4a473145428ab8fc927a9968442a0
Installed nativeSettings/init.lua:
  c3db971bf2c3117f81ebe5e6586589e0495bea77632deb2685c55e4011d9d86c
Installed Cron.lua:
  26f9685c60a86cc5624eabd0a2aec9796c4bba020156e05fad7e4648bd1938d6

Source-level chain
------------------
1. Each installed mod uses GetMod("nativeSettings") and registers its tab and
   controls through that library. These are CET/Lua Native Settings UI pages.
   They do not use Mod Settings.dll's script-property discovery mechanism.
   Mod Settings's log of zero registered classes does not diagnose these pages.

   Mod paths relative to their own CET mod directories:
     DeceptiousQuestCore/init.lua:74, nativeSetup/nativeLoad
     General Vehicle Fixes/init.lua:182, onInit
     BetterFlashlight/modules/settingsUI.lua:20, setup

   Quest Core additionally creates its controls when a loaded-game menu opens.
   General Vehicle Fixes and Better Flashlight independently register controls
   at initialization. Quest Core's lifecycle alone cannot explain all three
   empty tabs. Current CET scripting logs show all three mods and nativeSettings
   loaded at 03:49:06 UTC+02:00; the library initializes at 03:49:18.

2. nativeSettings/init.lua:260 builds tab metadata in PopulateSettingsData.
   Its PopulateCategorySettingsOptions override starts at line 296. It calls:
     line 308: this.settingsOptionsList:RemoveAllChildren()
     line 309: this.descriptionText:SetVisible(false)
   Only after this, at line 327, does it schedule the callback that creates
   headings and actual options. Therefore a failure at 309 leaves an empty
   list even when all options are registered correctly.

3. The copied, decompiled vanilla 2.5 cache in
   build/mod-settings-menu-research/vanilla.reds declares:
     SettingsMainGameController.m_descriptionText: inkTextRef
     inkTextRef extends inkLeafRef
     inkLeafRef extends inkWidgetRef
     inkWidgetRef.SetVisible(self: inkWidgetRef, visible: Bool)
   Relevant declaration lines: 329754, 164657, 164408, 164234.
   SDK generated TextWidgetReference/LeafWidgetReference/WidgetReference
   declarations corroborate this inheritance. The base is the native struct
   inkWidgetReference; inkTextRef is its script alias hierarchy.

4. CET resolves the inherited SetVisible function. ResolveHandle preserves
   the implicit self argument for this static native method, and ExecuteFunction
   converts that argument to the parameter's declared inkWidgetReference type.
   ClassConverter::ToRED compares the source and destination type pointers for
   equality, so the actual inkTextWidgetReference fails. RTTIHelper reports:
     Function 'SetVisible' parameter 1 must be inkWidgetReference.
   This text is reproduced in the isolated harness, not claimed as an existing
   message in the live game log.

5. There is a second local defect in error handling. WrapNextOverride now
   propagates a failed callback as a Lua error. ExecuteChain obtains the
   protected luaResult at FunctionOverride.cpp:461, but handles only the valid
   branch at line 463. It neither logs nor returns failure for an invalid
   result. Previously the continuation logged errors itself. That explains
   why the failed settings callback can stop rendering without a useful error
   being written to the mod's log. Before/after observers retain their own
   logging; this gap concerns the override path.

Isolated reproduction
----------------------
run.py copies the installed init.lua and Cron.lua into the workspace and
extracts ClassConverter::ToRED unchanged from the local CET source. It compiles
that method with the repository's pinned sol2 headers and the host's LuaJIT,
then executes the actual installed page-population callback and Cron scheduler.

Engine objects, class descriptors and drawing operations are stand-ins. Three
representative tabs are registered using the library's real public API, each
with one subcategory, a toggle and a float slider. The three individual mods'
gameplay/startup scripts and the production DLL are not executed by this test.
The candidate ancestry walk models the SDK's CClass::IsA behavior; it is not a
production source edit or a full native engine ABI test.

Results for every tab:
  baseline exact-type conversion: 3 tabs, page cleared, 0 headings, 0 controls;
    SetVisible's first argument is rejected.
  candidate ancestry conversion: 3 tabs, page cleared, 1 heading, 2 controls;
    all callbacks complete.

Conversion checks additionally prove that the candidate accepts exact types
and inkText/inkCompound references as inkWidget references, while rejecting
unrelated structs and base-to-derived arguments.

Reproduce from the repository root (requires clang++, pkg-config and LuaJIT
development files, plus the initialized pinned sol2 dependency):
  python3 docs/native-settings-empty-pages-lab/run.py \
    --native-settings /path/to/nativeSettings

After changing production source, preserve the negative control with:
  python3 docs/native-settings-empty-pages-lab/run.py \
    --native-settings build/native-settings-empty-pages/nativeSettings \
    --converter-header build/native-settings-empty-pages/evidence/Converter.h \
    --baseline

Checks of other dependencies
----------------------------
TweakXL: the latest log loads General Vehicle Fixes tweak files and reports
one distinct ambiguity, repeated on reload:
  Vehicle.VehicleWheelDrivingPreset_Hozuki_Sport_Front::
  Ambiguous definition. The value type cannot be determined.
The installed Feature/suspension.yaml:271 literally contains a double colon:
  Vehicle.VehicleWheelDrivingPreset_Hozuki_Sport_Front::
Parsing the captured YAML confirms one colon remains in the mapping key;
TweakXL then searches the wrong
record name and cannot infer a flat value from the mapping. This is a malformed
mod-data key, separate from the CET UI defect. It affects that vehicle tweak,
not the shared settings renderer. No installed YAML was edited.

Codeware: initializes successfully and loads its metadata. Its required input
helpers are not involved in the failing SetVisible conversion. This does not
establish that every Codeware feature works.

ArchiveXL: loads the Mod Settings archive and Deceptious Quest Core's XL file.
It still reports the known disabled journal hook and missing optional New Game
Plus quest phases. Those deserve their existing runtime limitations; they do
not account for the reproduced CET argument rejection in the settings renderer.

CET has additional preexisting metadata/configuration errors in the logs.
They were preserved separately. The blank-page reproduction requires neither
TweakDB metadata nor the configuration writer, so those errors should not be
substituted for the demonstrated cause.

Required correction and validation
----------------------------------
1. Make ClassConverter validate class ancestry, with proper class-kind/null
   checks; retain rejection of unrelated/downcast values. Do not revert to
   accepting every ClassReference.
2. Restore diagnostics and explicit failure handling for invalid protected
   override results. Do not silently turn a failed override into success.
3. Add production regression coverage for valid struct upcasts, unrelated
   types, forbidden downcasts, and the complete override error-handling path.
   Earlier continuation tests did not cover the outer ExecuteChain failure
   branch or this inheritance use of native struct arguments.
4. Export/rebuild CET and validate the Native Settings pages in the game,
   including controls, keybindings and navigation. This investigation leaves
   source implementation to a subsequent fix; the candidate test is not a
   packaged or installed correction.

Upstream reference, consulted 2026-10-25:
  https://github.com/justarandomguyintheinternet/CP77_nativeSettings
  https://raw.githubusercontent.com/justarandomguyintheinternet/CP77_nativeSettings/main/nativeSettings/init.lua
The upstream library also declares version 1.97 and uses this same common
rendering sequence. The conclusion is based on the user's installed copy and
our port source, not on assuming a public compatibility report applies.

No game/MO2 files, production source, Git index or commits were changed by this
investigation. Concurrent Mod Settings menu changes were preserved.

Production correction follow-up
-------------------------------
The subsequent user-requested fix is saved separately as
patches/cyber-engine-tweaks/003-native-settings-conversion.patch. The first two
patch layers and the source/dependency/SDK pins remain unchanged.

ClassConverter now checks both RTTI kinds before using CClass::IsA. Valid base
prefixes are borrowed from the original object; unrelated types, downcasts,
sibling types, non-class/null descriptors and empty storage are rejected.
RawConverter retains its exact-type rule.

ExecuteChain now logs an unhandled override's function name and full Lua error
in the entry override's mod logger, with scripting/default logger fallbacks for
internal calls. It returns false and skips return/out conversion on failure.
It preserves before/after observer behavior, argument cleanup and GC. It never
retries the original function: a callback may already have called next() before
failing. Lua errors handled by the mod itself remain successful invocations.

tests/native/cet/conversion.cpp compiles the real converter and SDK ancestry
method, with real Windows sol2/LuaJIT and fake engine descriptors. The old code
exits 42 on inkTextRef -> inkWidgetRef; the fixed code passes all conversion
and rejection cases. The extended cet-lua fixture compiles ExecuteChain and
WrapNextOverride unchanged. The old dispatcher fails the assertion that an
unhandled callback returns failure; the fixed dispatcher passes logging,
nested/handled errors, output preservation, cleanup and no-retry checks.
Negative logs are retained at build/cet-runtime/{conversion,dispatch}-before.log.

All five Windows executables passed under an isolated Wine prefix. The Python
suite collected 90 tests: 74 passed and 16 optional checks were skipped. The installed Native Settings UI
script also passed the three-page isolated reproduction using the extracted
fixed production converter, recorded in build/native-settings-empty-pages/
production.log. The default lab command now tests this production implementation;
--baseline explicitly selects the preserved old-code/candidate comparison.
These checks still do not constitute a game startup or settings interaction test.

The CET Release build, all eight patch reconstructions, official source boundary
audit and independent package verifier passed. All three archive checksums passed.
The rebuilt MO2 package is:
  dist/cet-native-settings-fixed/CyberEngineTweaks-1.38.0-game2.5-mo2-rootbuilder.zip
  SHA256: 2e2eb90921b1b88c9af9b9483aa00b8358ca23aeef4b9e9d8e690ef24bb37e63
The packaged Release DLL has SHA256:
  1b716aabf02f9037af8fbda827465ce1cdf1de554eabfa20b125933877288e1e
Standard game-root and symbols archives, manifest.json and SHA256SUMS are in the
same distribution directory. No game/MO2 files were modified by the correction;
installation and in-game controls, keybindings and navigation remain untested.
