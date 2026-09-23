# ElaWidgetTools source provenance

Source: <https://github.com/Liniyous/ElaWidgetTools>

## Pinloom 0.4.8: current integration

Apply patches 01–11 below, then `12-pinloom-native-interactions.patch` and
`13-pinloom-combo-popup-padding.patch`.
Patch 12 is based on Pinloom `369e1b472701d7facf91a01710babde58ffe170c` and
records only this library's incremental source changes. It supersedes the
immediate drawer, popup and input policy described in the historical sections.

The component sources were selectively ported from committed ZeroSlack baseline
`3f1c4afab0d0423c04c3c3af4bb3c3d9aabe5cde`, then the three menu files from
`75180fad5e5f5142684cf092649deffe5720994d` (shared patch 27). Shared patch 28's
ElaListView style lifetime fix was also ported: application-owned style,
QObject-bound deferred deletion after widget teardown. Its supplied reference is
`28-xips-list-style-lifetime.patch`; this is recorded inside Pinloom patch 12,
not applied verbatim over Pinloom's distinct item-view contracts. Public
interfaces remain at the shared p27 level; no ABI equality with other products'
DLLs is assumed. Pinloom distributes its own matched DLL and executable.

Shared behavior includes owned and interruptible combo/menu transitions,
content-only bounded drawer snapshots, smooth ordinary wheel input, text and
dialog lifecycle fixes, and toolbar geometry. These are extensions to the
pinned Ela implementations, not claims that all extensions exist upstream.
Pinloom retains its standard item roles, accessibility, localized text actions,
semantic colors, action-only navigation, and non-activating capture window.
Additional changes reuse the line-edit focus animation, preserve inherited
text palettes, keep Qt Ctrl/Shift wheel semantics and expose tree input settlement.
The app adapter preserves the navigation view's mapped overlay scrollbar instead
of deleting its source, and leaves text-editor pixel-to-line conversion to Qt.

Shared patch 29 (`29-wave-overlay-origin-lifetime.patch`, SHA256
`c292256d9d23cc391b2a185b88d7335f79410ef08e491f727916829c627a88f8`)
was incrementally applied to the three scrollbar files and included in patch 12.
Mapped origin/area references are QPointer guarded; source destruction detaches
the event filter, stops the animation and hides the overlay. Pinloom retains an
origin-replacement/resize/grab/teardown regression that failed before this fix.
The public API remains p27.

Shared patch 30 (`30-regmap-combo-popup-padding.patch`, SHA256
`e69b815ba035831e2a84484acb0c46f957c83f346fff230a8c2b3034d4a44046`)
is recorded as local patch 13, based on Pinloom
`51ec1d1429b2f26ca65b848a3176dde9dfb370b8`. Its only library source change is
ElaComboBox.cpp: account for vertical layout padding once per fresh popup,
constrain the final geometry to the available screen, and settle repeated
visible-show requests without growing the popup. Existing lifecycle and input
branches are retained. Source compatibility patch level is now 30; public
API/ABI remains at p27. Version 0.4.8 is retained without creating a release tag.
The six row-count/screen-edge regressions fail before this fix and are retained.

Qt 6.10.2 EXACT is required, including private headers used by tree animation
settlement. MIT, OFL and font bytes are unchanged. Reconstruction compares the
result to source after Git text-line-ending normalization (BOM preserved).
See `docs/ELA_NATIVE_0.4.8.md` in the Pinloom repository for tests and boundaries.

## Historical import and patches 01–11

Upstream revision: `454cac2d57a47d3cc28577dc817793aec1881ca7`

The `ElaWidgetTools/` library subtree was vendored without its example
application. Its MIT license and upstream README are preserved alongside
the source. The following ZeroSlack-specific changes are applied:

- Convert icon enum values explicitly when constructing `QChar`, as required
  by Qt 6.10.2.
- Omit four example images from the resource collection. ZeroSlack does not
  use Ela's sample user card, acrylic card, or Mica image features.
- Replace the unverified Fontello-generated `ElaAwesome.ttf` with the
  unmodified Font Awesome Free Solid 6.7.2 font (SIL OFL 1.1) and remap
  internally referenced icon values. The exact mapping is preserved in
  `patches/03-icon-font-swap.patch`; eleven unavailable icons use free alternatives.

Reconstruction: extract the library subtree at the pinned revision, then apply
`patches/01` through `06` in filename order. These patches reproduce all 417
files of the audited comparison source, including its historical formatting.
Apply `patches/07-zeroslack-control-contracts.patch` last for the migration:
QIcon/mnemonic/keyboard/checked/focus/default-button painting, selected tool-button
icons, theme repaint, ownership of a removed combo-box layout item, and safe style
detachment before tool/combo/spin-box destruction. The latter includes the shared
popup/view and embedded line-edit styles; visible top-level teardown is tested.

The product adapter in `src/ui/uicontrols.cpp` releases fixed dimensions, restores
ZeroSlack typography, updates per-button theme colors, supplies focus outlines,
and uses Qt's immediate combo popup lifecycle with Ela's style. This avoids
upstream's non-interruptible popup animation. No recursive application event
filter or protected-surface traversal is installed for Ela.

This integration is pinned to Qt 6.10.2 because ElaTabBar includes Qt private
headers. Rebuild both DLLs and rerun validation before changing the Qt version.
Upstream CMake declares version 2.0.0 while its public header declares 2.0.3;
the commit identifier above is the authoritative source version.

Both `LICENSE` (ElaWidgetTools) and `Font/FontAwesome-LICENSE.txt` must
accompany redistributed binaries.

## Pinloom integration

Imported from the committed vendor subtree at ZeroSlack commit
`261c90e6d96d9738f7eed2eab27499a25768778d` (no working-tree/build dependency).
Pinloom additionally records patch 08: detach the local style before destroying
line edits, checkboxes, and menus, retaining the owned style pointer instead of
deleting a possible Qt stylesheet proxy. This protects visible popup/parent
teardown. The adapter `src/widgets/PinloomUiControls.cpp` is in Pinloom; it
preserves application fonts, immediate popup dismissal, Qt editing focus and
localized context actions, and scopes QSS away from Ela controls. Its menu and
input transitions deliberately have no decorative motion, including when the
system requests reduced motion.

Patch 09 extends safe, explicitly owned style detachment to plain-text editors,
scrollbars, menu bars and status bars. The scrollbar's persistent animation is
parent-owned and stopped before teardown. It also guards the menu-bar overflow
button lookup, sizes menu rows from font metrics instead of the current widget
height, and retains Qt mnemonic/escaped-ampersand painting.

Pinloom's follow-up adapter uses these controls for text previews, settings and
inspector scrolling, and the resident main-window bars. It preserves Qt wheel
and keyboard scrolling without decorative motion. Scrollbar context menus stay
Qt-localized. Text context actions are Qt-generated in an immediate Ela popup;
the action-container lifetime is paired without inheriting the popup's style.
Native Qt file dialogs, destructive confirmations and specialized PDF surfaces
are not replaced.

Patch 10 covers the remaining UI families. Item-view styles delegate standard
item roles, checkbox painting and multiline sizing to Qt for Pinloom's explicit
item contract; the views still own Ela frames/headers/scrollbars. List/table/tree
and navigation styles detach safely before deletion. Navigation animations have
owned lifetimes, model display/accessibility roles expose destination names and
stable route keys, and action-only navigation bypasses the global page-history
commander. Window buttons have accessible names. The app adapter composes
ElaAppBar with QMainWindow/QDialog and routes close through Qt, never Ela's
default force-close path. Its offscreen chrome skips HWND operations.

Pinloom's opt-in instant message-bar path uses parent font metrics, bounded
three-line text, accessible full text, parent-relative geometry, and a
context-owned expiry timer. It does not register in Ela's animated global
message map. Replacing or destroying a notice therefore has no global entry
or animation to outlive the window. Existing upstream animation behavior is
unchanged for callers that do not opt in. Patches 08–10 touch disjoint source
files and can each be checked against the committed imported reference.

Patch 11 adds an opt-in immediate drawer transition for Pinloom's settings and
advanced locator forms. It toggles the real content and arrow state without
creating a screenshot animation, preserving keyboard focus and rapid toggles.
The default upstream behavior is unchanged. Its two files are disjoint from
patches 08–10 and can be checked against the same imported reference.

PinloomUiSurfaces composes ElaToolTip without the upstream hover event filter:
Qt help events, explicit ToolTipRole data, bounded literal text, owner lifetime,
expiry, and focus preservation remain under the application's control. Native
file/safety dialogs are excluded. ElaScrollPageArea supplies settings card
painting, ElaDrawerArea supplies the folding surface, and shared tag popup
frames use Ela theme/shadow painting with Qt popup dismissal.
