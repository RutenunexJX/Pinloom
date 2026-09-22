# ElaWidgetTools source provenance

Source: <https://github.com/Liniyous/ElaWidgetTools>

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
