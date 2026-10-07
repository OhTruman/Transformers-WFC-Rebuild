#pragma once
// Clean-room reconstruction - button prompts in text (TnInputCommandToBindingMapper) and the last-used input device.
//
// TnInputCommandToBindingMapper.TranslateCommandsInString [CONFIRMED script, RE 0d66bc0]: a localized string carries
// ^COMMAND tokens (ending at a space, comma or period); each goes InputCommandToKeyBindings (Xe-TransInput.ini:
// command -> Key / KeyAlt) -> KeyToActionScriptBindings[Key].ActionScript (the localized [TnInputCommandToBindingMapper]
// section) and is replaced in the string. On the Xbox 360 that gives "{a}", "{x}", ... (the movies' setImageSubstitutions
// draw Gamepad* glyphs); the PC SKU's TransGame_PC.int maps keys to orange HTML text ("<font color='#FF9333'>G</font>").
// A token without a binding stays as it is. Callers: contextual commands, tutorial messages, kill-streak descriptions.
//
// The PC SKU's own command -> key table (its input ini) is not in the dump: the PC table here follows the rebuild's
// keyboard bindings onto the PC mapper's key names [PC ADAPTATION]. The original PC always showed key text (no device
// switching); showing pad glyphs after pad input is a PC ADAPTATION (user / Integration decision).

#include <map>
#include <string>
#include <vector>

namespace frontend {

class InputPrompts {
public:
    // Loads Xe-TransInput.ini InputCommandToKeyBindings, TransGame.int and TransGame_PC.int mapper sections.
    bool load(const std::string& extractedRoot);
    // Replaces every bound ^COMMAND in text: pad = the console glyph tokens, else the PC key text.
    std::string translate(const std::string& text, bool pad) const;
    // The PC key text for a key name of the PC mapper ("G", "Space", "LeftMouseButton", ...), "" if none.
    std::string pcKeyText(const std::string& key) const;
    // Test / tooling: install tables directly.
    void setTables(std::map<std::string, std::string> padKeys, std::map<std::string, std::string> padText,
                   std::map<std::string, std::string> pcText);

    static const std::map<std::string, std::string>& pcCommandKeys();   // ^COMMAND -> PC mapper key [PC ADAPTATION]

private:
    std::map<std::string, std::string> padKeys_;   // ^COMMAND -> XboxTypeS_* key
    std::map<std::string, std::string> padText_;   // XboxTypeS_* -> "{a}"
    std::map<std::string, std::string> pcText_;    // PC mapper key -> "<font color='#FF9333'>G</font>"
};

// The last-used input device with hysteresis [PC ADAPTATION]: a pad button / stick / trigger selects the pad at once;
// a key or mouse button selects keyboard / mouse at once; mouse movement selects it only after kMouseTravel pixels
// within kMouseWindow seconds, so a nudge of the mouse while playing on the pad does not flip the prompts.
class InputDevice {
public:
    static constexpr float kMouseTravel = 40.0f;
    static constexpr float kMouseWindow = 0.25f;
    // One frame: pad activity, keyboard / mouse-button activity, mouse movement (pixels), frame time.
    void update(bool padActive, bool keyActive, float mouseDx, float mouseDy, float dt);
    bool pad() const { return pad_; }
    void setPad(bool pad) { pad_ = pad; }

private:
    bool pad_ = false;      // PC default: keyboard / mouse (the original PC showed key text)
    float travel_ = 0.0f, window_ = 0.0f;
};

}  // namespace frontend
