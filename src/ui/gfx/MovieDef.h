// Clean-room reconstruction — Scaleform GFx movie definition (parsed .gfx = SWF 8 + GFx extension tags).
// Parsed once per file and shared by every instance. Supported tags: see MovieDef.cpp parseTags().
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ui/gfx/GfxTypes.h"

namespace gfx {

// ---- shapes ----
struct GradStop { uint8_t ratio = 0; RGBA color; };
struct FillStyle {
    enum Type : uint8_t { Solid = 0x00, Linear = 0x10, Radial = 0x12, Focal = 0x13,
                          BitmapRepeat = 0x40, BitmapClip = 0x41, BitmapRepeatHard = 0x42, BitmapClipHard = 0x43 };
    uint8_t type = Solid;
    RGBA color;
    Matrix m;                      // gradient / bitmap space -> shape space (twips)
    std::vector<GradStop> grad;
    uint8_t spread = 0, interp = 0;
    float focal = 0.0f;
    uint16_t bitmapId = 0xFFFF;
    bool isBitmap() const { return type >= 0x40 && type <= 0x43; }
    bool isGradient() const { return type == Linear || type == Radial || type == Focal; }
};
struct LineStyle {
    float width = 20.0f;           // twips
    RGBA color;
    bool hasFill = false;
    FillStyle fill;
    uint8_t startCap = 0, endCap = 0, join = 0;
    bool noHScale = false, noVScale = false, pixelHinting = false, noClose = false;
};
// One run of edges sharing a style state; points flattened (curves subdivided), pts[0] = move-to.
struct ShapePath {
    int styleSet = 0;              // index into ShapeDef::fillSets / lineSets (DefineShape2+ "new styles")
    int fill0 = 0, fill1 = 0, line = 0;   // 1-based, 0 = none
    std::vector<Point> pts;
};
struct ShapeDef {
    Rect bounds;
    std::vector<std::vector<FillStyle>> fillSets;
    std::vector<std::vector<LineStyle>> lineSets;
    std::vector<ShapePath> paths;
    bool dynamic = false;   // owned by a display object (drawing API, text box, caret), not by a (never freed) MovieDef
};

// ---- fonts / text ----
struct FontDef {
    std::string name;
    bool bold = false, italic = false, hasLayout = false, wideCodes = false;
    bool font3 = false;                         // DefineFont3: EM 1024*20; DefineFont2: EM 1024
    std::vector<ShapeDef> glyphs;               // glyph outlines (font units)
    std::vector<uint16_t> codes;                // glyph index -> UCS-2 code
    std::map<uint16_t, int> codeToGlyph;
    std::vector<float> advances;                // font units
    float ascent = 0, descent = 0, leading = 0;
    std::map<uint32_t, float> kerning;          // (code1 << 16 | code2) -> adjustment
    float emSize() const { return font3 ? 1024.0f * 20.0f : 1024.0f; }
};

struct EditTextDef {
    Rect bounds;
    bool hasText = false, wordWrap = false, multiline = false, password = false, readOnly = false, hasColor = false,
         hasMaxLength = false, hasFont = false, hasFontClass = false, autoSize = false, hasLayout = false,
         noSelect = false, border = false, wasStatic = false, html = false, useOutlines = false;
    uint16_t fontId = 0;
    std::string fontClass;
    float height = 240;                          // twips
    RGBA color;
    uint16_t maxLength = 0;
    uint8_t align = 0;                           // 0 left 1 right 2 center 3 justify
    float leftMargin = 0, rightMargin = 0, indent = 0, leading = 0;
    std::string variable, initialText;
};

struct TextRecordGlyph { int glyph = 0; float advance = 0; };
struct TextRecord {
    uint16_t fontId = 0; bool hasFont = false;
    RGBA color; bool hasColor = false;
    float x = 0, y = 0; bool hasX = false, hasY = false;
    float height = 0;
    std::vector<TextRecordGlyph> glyphs;
};
struct StaticTextDef { Rect bounds; Matrix m; std::vector<TextRecord> records; };

// ---- bitmaps ----
struct BitmapDef {
    std::string exportName;                       // GFx DefineExternalImage export name (resource)
    std::string fileName;                         // e.g. "LoadScreen_GFX_I20.tga" (extracted as .png)
    int targetWidth = 0, targetHeight = 0;        // stage size of the image (pixels)
    std::string resolvedPath;                     // PNG on disk (after ExternalTextures remap)
};

// ---- timelines ----
struct ActionBlock { std::shared_ptr<std::vector<uint8_t>> code; };

struct ClipAction { uint32_t events = 0; uint8_t keyCode = 0; ActionBlock code; };

struct PlaceCmd {
    uint16_t depth = 0;
    bool move = false, hasChar = false, hasMatrix = false, hasCx = false, hasRatio = false, hasName = false,
         hasClipDepth = false, hasClipActions = false, hasBlend = false, hasClassName = false;
    uint16_t charId = 0;
    Matrix m;
    CXForm cx;
    uint16_t ratio = 0;
    std::string name;
    uint16_t clipDepth = 0;
    uint8_t blend = 0;
    std::string className;
    std::vector<ClipAction> clipActions;
};

struct ControlTag {
    enum Kind { Place, Remove } kind = Place;
    PlaceCmd place;                               // Place
    uint16_t depth = 0;                           // Remove
};

struct Frame {
    std::vector<ControlTag> tags;
    std::vector<ActionBlock> actions;             // DoAction, in tag order
    std::vector<std::pair<uint16_t, ActionBlock>> initActions;   // DoInitAction (sprite id, code)
};

struct SpriteDef {
    int frameCount = 0;
    std::vector<Frame> frames;
    std::map<std::string, int> labels;            // label -> 0-based frame
    std::string label(int frame) const;
};

// DefineMorphShape: start / end edges and styles; instances draw the interpolation at their PlaceObject ratio.
struct MorphRec {
    enum Kind : uint8_t { Move, Style, Line, Curve } kind = Line;
    float x = 0, y = 0, cx = 0, cy = 0;     // anchor / control (absolute)
    int fill0 = 0, fill1 = 0, line = 0;     // Style (-1 = unchanged)
};
struct MorphDef {
    Rect startBounds, endBounds;
    std::vector<FillStyle> startFills, endFills;
    std::vector<LineStyle> startLines, endLines;
    std::vector<MorphRec> start, end;       // end: Move / Line / Curve only
    mutable std::map<uint16_t, std::unique_ptr<ShapeDef>> cache;   // ratio -> interpolated shape
    const ShapeDef* at(uint16_t ratio) const;
};

enum class CharType { Shape, Sprite, EditText, StaticText, Font, Bitmap, Imported, Morph, Unknown };
struct CharDef {
    CharType type = CharType::Unknown;
    int index = -1;                               // into the per-type vector
    std::string importUrl, importName;            // Imported
};

struct ImportDef { std::string url; uint16_t id = 0; std::string name; };

class MovieDef {
public:
    bool load(const std::string& path);
    const std::string& path() const { return path_; }
    const std::string& baseName() const { return base_; }        // "FrontEnd_GFX"
    std::string dir() const;                                    // directory of the .gfx

    Rect stage;                                                 // twips
    float frameRate = 30.0f;
    int version = 8;
    RGBA background;

    SpriteDef root;
    std::map<uint16_t, CharDef> chars;
    std::vector<ShapeDef> shapes;
    std::vector<SpriteDef> sprites;
    std::vector<EditTextDef> editTexts;
    std::vector<StaticTextDef> staticTexts;
    std::vector<FontDef> fonts;
    std::vector<BitmapDef> bitmaps;
    std::vector<std::unique_ptr<MorphDef>> morphs;
    std::map<std::string, uint16_t> exports;                    // linkage name -> id
    std::map<uint16_t, std::string> exportNames;
    std::vector<ImportDef> imports;
    std::map<uint16_t, std::pair<std::string, std::string>> unsupported;   // tag code -> note (diagnostics)

    const CharDef* character(uint16_t id) const {
        auto it = chars.find(id); return it == chars.end() ? nullptr : &it->second;
    }
    const SpriteDef* sprite(uint16_t id) const;
    int findFont(const std::string& name, bool bold, bool italic) const;   // index or -1

private:
    std::string path_, base_;
};

// Font-relevant shape parse helper (glyphs) exposed for tests.
bool parseShapeRecords(const uint8_t* data, size_t size, size_t& pos, int shapeVersion, ShapeDef& out, bool glyph);

} // namespace gfx
