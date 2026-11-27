# Mistbar - Apple HIG Review Summary

Based on the installed `apple-design` skill from https://github.com/dickwu/apple-design-skill

## Quick Assessment
**Rating: Needs work** - The app captures the visual aesthetic of macOS Liquid Glass but needs several adjustments to match Apple's Human Interface Guidelines for a native experience.

## Critical Changes Needed

### 1. Liquid Glass Layer Discipline (Most Important)
- **Rule:** Liquid Glass belongs ONLY on the functional layer (toolbars, menubars, tab bars, sidebars, sheets, popovers, alerts)
- **Never** on content layers (app backgrounds, cards, list rows, content containers)
- **Fix:** Ensure the bar's glass effect is on the functional layer only; underlying content should use standard materials or opaque surfaces

### 2. Color Budget
- **Rule:** One, at most two, tinted primary actions per view; labels stay monochrome over colorful content; brand color off controls other than primary actions/status indicators
- **Fix:** Reserve `#0A84FF` accent color only for primary actions and status indicators, not broadly applied

### 3. Menubar Layout
- **Rule:** Standard order: Apple menu () → File → Edit → View → Go → Window → Help
- **Fix:** Ensure the first menu is the standard Apple menu with About, Preferences, Services

## High Priority Changes

### 4. Control Sizes
- **Rule:** macOS minimum 28x28 pt default, 20x20 pt minimum
- **Fix:** Ensure all interactive hit regions meet minimum size

### 5. Contrast Ratios
- **Rule:** Text up to 17pt: 4.5:1; text 18pt+: 3:1; bold text any size: 3:1
- **Fix:** Calculate and verify contrast for all text/background combinations

### 6. Keyboard Navigation
- **Rule:** Cmd+, opens preferences, Cmd+Q quits, Cmd+Tab switches apps; Full Keyboard Access works
- **Fix:** Add standard keyboard shortcuts and ensure keyboard navigation

## Medium Priority Changes

### 7. Typography
- **Rule:** macOS default 13pt, minimum 10pt; avoid light/thin weights for small text
- **Fix:** Set appropriate type sizes and weights

### 8. System Icons
- **Rule:** Prefer SF Symbols over custom drawings for standard controls
- **Fix:** Use SF Symbols for speaker, wifi, battery, etc. where possible

## What Already Works Well

✅ **Black-only dark theme** - Consistent and intentional  
✅ **Liquid Glass aesthetic** - Successfully captures the macOS feel  
✅ **Configurable presets** - Regular, frosted, dark, clear, solid  
✅ **Menu bar layout** - Generally follows macOS conventions  
✅ **Purpose-driven design** - Clear "Liquid Glass top bar for Linux Wayland" identity  

## How to Apply These Changes

1. **Review the apple-design-review.md** for detailed findings with citations
2. **Start with Critical fixes:** Layer discipline and color budget have the biggest visual impact
3. **Use the skill commands:** `apple-design` can review specific aspects
4. **Test with:** 
   - Increase Contrast system setting
   - Reduced Motion setting
   - Largest text size accessibility setting
   - Dark/Light appearance toggles

## Running an Actual Review with the Skill

You can now run design reviews in OpenCode using the skill:

```bash
# The skill is installed and available as /apple-design
# To review specific aspects of Mistbar:
/apple-design review this-code-base
```

Or ask design questions:
```
Review this menubar layout against Apple's HIG
Audit this Liquid Glass implementation for accessibility
Does this control size meet macOS conventions?
```

The skill is now installed in `~/.agents/skills/apple-design` and works with OpenCode, Antigravity, Cline, Codex, Cursor, and other supported agents.