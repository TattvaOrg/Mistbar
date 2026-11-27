# Mistbar Apple HIG Review

*Review based on the apple-design skill (v1.0) and Apple's Human Interface Guidelines*

## Summary

**Overall Rating: Needs work**

Mistbar successfully captures the *aesthetic* of a macOS top bar with its Liquid Glass effects, but several areas need adjustment to truly match Apple's Human Interface Guidelines for a native macOS experience. The design shows good intent (Purpose principle) but needs refinement in accessibility, platform conventions, and craft.

### Critical Issues (Must Fix)

#### 1. Liquid Glass Layer Discipline
**File:** `src/themes/theme.cpp`, `src/bar/bar_app.cpp`

Mistbar applies Liquid Glass effects to the bar and popovers, which is correct for the *functional layer*. However, the guidelines require:

- **Liquid Glass belongs only on the functional layer** (toolbars, menubars, tab bars, sidebars, sheets, popovers, alerts)
- **Never on content layers** (app backgrounds, cards, list rows, content containers)

**Finding:** While the bar itself is a functional element, ensure that any underlying content or wallpaper peeks through correctly without the glass effect interfering with content legibility.

**Fix:** Follow the two-layer model:
1. **Content layer:** Text, images, lists, media - use standard materials or opaque surfaces, never Liquid Glass
2. **Functional layer:** The menubar/tab bar - Liquid Glass is appropriate here

> `liquid-glass.md` § "The two layers": "Content layer... When this layer needs depth it uses standard materials, never Liquid Glass."

**Citation:** `liquid-glass.md › The two layers`: "Content layer. Text, images, lists, media, and app backgrounds. When this layer needs depth it uses standard materials, never Liquid Glass."

#### 2. Color Budget and Accent Usage
**File:** `src/themes/theme.cpp`

The current implementation uses `accent_color` (#0A84FF blue) extensively. Apple's guidelines are strict:

- **One, at most two, tinted primary actions per view** - never a row of them
- **Labels stay monochrome over colorful content** - no colored text labels on glass
- **Brand color stays off controls other than primary actions and status indicators**

**Finding:** The accent color is applied broadly across the bar elements. While the theming code does support accent color configuration, the default application may violate the color budget rule.

**Fix:** 
- Reserve accent color (`#0A84FF`) only for: primary actions (like "Done" equivalent), unread badges, selected tab icons
- Keep toolbar/menubar labels in monochrome (current `text_primary`/{0.96, 0.96, 0.97} is acceptable)
- If the app has colorful content scrolling beneath, ensure the resting state keeps controls legible

> `liquid-glass.md` § "Color on glass": "Apply color sparingly to the Liquid Glass material, and to symbols or text on the material. Reserve it for elements that truly benefit from emphasis..."

**Citation:** `liquid-glass.md › Color on glass`: "Apply color sparingly to the Liquid Glass material... Reserve it for elements that truly benefit from emphasis, such as status indicators or primary actions."

### High Issues (Should Fix)

#### 3. Menubar Layout and Conventions
**File:** `src/bar/bar_app.cpp`, `src/window/`

Mistbar's menu bar layout needs to follow macOS conventions:

- **Standard macOS menu order:** Apple menu () → File → Edit → View → Go → Window → Help
- **The "M" menu** should be the Apple/ menu, not a generic app menu
- **Each command should be reachable from the menu bar** (including toolbar items)

**Finding:** The code references "M menu" which should be the standard Apple menu. The layout of File/Edit/View/Go/Window/Help follows reasonably close, but verification is needed.

**Fix:** 
- Ensure the first menu is the standard Apple menu with About, Preferences, Services, etc.
- Commands like "About Mistbar", "Preferences..." should follow macOS conventions
- Every toolbar command should also exist in the menu bar

> `designing-for-macos.md` § "Best practices": "Use the menu bar to give people easy access to all the commands they need to do things in your app."

> `the-menu-bar.md`: Details on macOS menu bar conventions.

#### 4. Control Sizes and Hit Regions
**File:** `src/bar/bar_app.cpp` (various glyph drawing functions)

Apple specifies minimum control sizes:

- **macOS default:** 28x28 pt, minimum 20x20 pt
- **Current implementation:** Some glyph/icons may be smaller than 28pt

**Finding:** The control center icons (speaker, wifi, bluetooth, etc.) are drawn at various sizes. Need to verify they meet the 28pt default minimum, especially for touch/click targets.

**Fix:** 
- Ensure interactive areas around icons are at least 28x28 pt
- The `draw_cc_*` functions should include padding to meet minimum touch targets
- Use the system's preferred control size when possible

> `accessibility.md` § "Mobility": "macOS | 28x28 pt | 20x20 pt"

> `accessibility.md` § "Mobility": "Consider spacing between controls as important as size. Include enough padding between elements to reduce the chance that someone taps the wrong control."

**Citation:** `accessibility.md › Mobility`: "macOS | 28x28 pt | 20x20 pt"

#### 5. Contrast and Readability
**File:** `src/themes/theme.cpp`

Color contrast ratios must meet minimums:

- **Text 18pt+, all weights:** 3:1 minimum
- **Text up to 17pt:** 4.5:1 minimum  
- **Bold text any size:** 3:1 minimum

**Finding:** The theming code defines `text_primary` = {0.96, 0.96, 0.97} on dark surfaces, which is nearly white. Need to verify contrast ratios against the background colors defined.

**Fix:** 
- Calculate contrast ratios for all text/background combinations
- Ensure `text_primary` against `bar_bg` and `panel_bg` meets 3:1 minimum for 18pt+ text
- For smaller text, ensure 4.5:1 contrast
- Test both light and dark appearances

> `accessibility.md` § "Vision": "Text size | Text weight | Minimum contrast ratio | Up to 17 pts | All | 4.5:1 | 18 pts | All | 3:1 | All | Bold | 3:1"

**Citation:** `accessibility.md › Vision`: "Two popular standards of measure for color contrast are the Web Content Accessibility Guidelines (WCAG) and the Accessible Perceptual Contrast Algorithm (APCA)."

### Medium Issues (Improve Polish)

#### 6. Typography and Type Scaling
**File:** `src/themes/theme.cpp`

- **macOS default:** 13 pt, minimum 10 pt
- **Avoid light/thin weights for small text**

**Finding:** The theming uses system-defined colors but doesn't explicitly set type sizes. Need to ensure text sizes respect Dynamic Type and platform defaults.

**Fix:** 
- Set default text sizes matching macOS: 13pt default, 10pt minimum
- Use system fonts (SF Pro) with appropriate weights
- Avoid thin weights for text smaller than 18pt

> `typography.md`: Details on type scales and sizes per platform.

#### 7. Keyboard Navigation and Focus
**File:** `src/bar/bar_app.cpp`, `src/settings/`

- **Every command reachable from menu bar**
- **Standard shortcuts respected** (Cmd+, for preferences, Cmd+Q to quit, etc.)
- **Keyboard-only use works on desktop**

**Finding:** The code handles some keyboard events but needs verification for full keyboard access compliance.

**Fix:** 
- Ensure Cmd+, opens preferences, Cmd+Q quits, Cmd+Tab switches apps
- Full Keyboard Access should navigate all controls
- Focus should be visible and logical

> `keyboards.md`: Details on keyboard support.

#### 8. System Item Styles in Toolbar
**File:** `src/bar/bar_app.cpp` (Control Center glyph drawing)

- **Prefer system-provided symbols without borders** (SF Symbols)
- **`.prominent` style for key actions** like Done/Submit

**Finding:** Custom glyphs are drawn (`draw_cc_speaker_glyph`, etc.) rather than using SF Symbols. While this works for a custom app, using system symbols would provide better integration.

**Fix:** 
- Where possible, use SF Symbols for standard controls (speaker, wifi, battery, etc.)
- Use `.prominent` style for primary actions
- Ensure custom icons are vector-based and labeled for screen readers

> `icons.md`: Guidance on SF Symbols usage.

#### 9. Status Bar and Window Conventions
**File:** `src/window/`

- **Windows resize fluidly**, use system window controls
- **Never keep critical information in a bottom bar**

**Finding:** Need to verify the window management follows macOS patterns.

**Fix:** 
- Use system title bar/controls (close, minimize, zoom)
- Ensure no critical info in bottom areas
- Windows should resize fluidly

> `windows.md`: Details on macOS window conventions.

### Low Polish Suggestions

#### 10. Point of View and Identity
**Assessment:** Mistbar has a clear point of view - it's a "Liquid Glass top bar for Linux Wayland". This is distinctive without being generic template-ui.

**Suggestion:** The black-only theme (`theme: "dark"` with `glassPreset: "regular"`) is distinctive. Consider whether the accent color (`#0A84FF`) could be used more intentionally as a "signature element" per the craft lens, rather than being broadly applied.

> `SKILL.md` § "Craft notes": "Does the typography carry personality, or is it a neutral delivery vehicle? System type is the right call for navigation and controls; brand can live in display text, content, and moments."

#### 11. Empty States and Help
**Suggestion:** Consider adding accessible empty states and help/documentation that follows Apple's patterns.

> `writing.md`: "Empty screens invite the next action."

#### 12. Dynamic Type Support
**Suggestion:** Add support for Dynamic Type text size scaling. While this is a C++/Wayland app, the theming should allow text sizes to scale with system preferences.

### Recommendations Summary

| Priority | Area | Action |
|----------|------|--------|
| **Critical** | Liquid Glass layer discipline | Ensure glass only on functional layer, never content layer |
| **Critical** | Color budget | Reserve accent for primary actions only; keep labels monochrome |
| **High** | Menubar layout | Standard Apple menu order; all commands in menu bar |
| **High** | Control sizes | Minimum 28x28pt hit regions on desktop |
| **High** | Contrast ratios | Verify 3:1/4.5:1 minimums for all text/background combos |
| **Medium** | Typography | Set 13pt default, use system fonts, avoid thin weights |
| **Medium** | Keyboard navigation | Cmd+,/Q, Cmd+Tab, Full Keyboard Access support |
| **Low** | Point of view | Leverage the distinctive Liquid Glass identity |

### Implementation Checklist

Based on the apple-design skill's review format, here's what to address:

```
### Critical
- **Layer discipline:** Liquid Glass only on functional layer (bar, not content)
- **Color budget:** Accent color only for primary actions/status, not broad application

### Improvements
- **Menubar:** Standard macOS menu order (Apple, File, Edit, View, Go, Window, Help)
- **Control sizes:** Minimum 28pt hit regions for all interactive elements
- **Contrast:** Verify ratios per accessibility.md guidelines
- **Typography:** 13pt default, system fonts, appropriate weights

### Craft notes
- The black-only dark theme with Liquid Glass is distinctive and well-executed
- Accent color (#0A84FF) should be a "signature element" saved for primary actions
- Consider using SF Symbols for standard controls to improve platform integration

### What works
- Liquid Glass aesthetic successfully captures the macOS feel
- Black-only theme is consistent and intentional
- Configurable presets (regular, frosted, dark, clear, solid) provide flexibility
- The menu bar layout follows macOS conventions broadly