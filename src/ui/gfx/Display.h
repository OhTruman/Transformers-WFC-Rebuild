// Clean-room reconstruction — GFx display list (MovieClip / Shape / TextField / StaticText / Bitmap instances),
// timelines and the Player that owns one movie's display tree, its AVM1 VM and its loaded libraries.
#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "ui/gfx/Avm1.h"
#include "ui/gfx/GfxTypes.h"
#include "ui/gfx/MovieDef.h"

namespace gfx {

class Player;
class MovieClip;

// AS2 depth <-> SWF depth: timeline depths are 1..; script depths start at 0 = SWF depth 16384.
constexpr int kDepthOffset = 16384;

class DisplayObject {
public:
    enum class Kind { Clip, Shape, Text, StaticText, Bitmap };
    DisplayObject(Kind k, Player* p) : kind(k), player(p) {}
    virtual ~DisplayObject() = default;

    Kind kind;
    Player* player;
    std::shared_ptr<const MovieDef> def;      // definition library this instance resolves characters in
    uint16_t charId = 0;
    int depth = 0;                            // SWF depth
    std::string name;
    MovieClip* parent = nullptr;
    Matrix matrix;
    CXForm cx;
    bool visible = true;
    int clipDepth = 0;                        // mask layer: masks depths (depth, clipDepth]
    uint16_t ratio = 0;
    uint8_t blend = 0;
    bool scripted = false;                    // created by attachMovie / createEmptyMovieClip / createTextField / ...
    int placeFrame = -1;                      // timeline frame of the PlaceObject that created it
    bool removed = false;
    avm1::Object* script = nullptr;           // AS object (MovieClip / TextField)
    DisplayObject* maskedBy = nullptr;        // setMask()
    bool usedAsMask = false;                  // the mask of another object (not drawn itself)
    // AS-visible transform components (kept separately so _xscale etc. round-trip like Flash).
    float rotationDeg = 0, xscale = 100, yscale = 100;
    bool componentsValid = false;
    void syncComponents();
    void applyComponents();

    virtual Rect localBounds() const { return {}; }
    Rect boundsIn(const Matrix& m) const;
    Matrix worldMatrix() const;
    CXForm worldCx() const;
    bool worldVisible() const;
    std::string targetPath() const;           // "_level0.menuAnchor_mc.menuMain_mc"
    std::string slashPath() const;            // "/menuAnchor_mc/menuMain_mc"
    MovieClip* rootClip();
};

class ShapeInstance : public DisplayObject {
public:
    explicit ShapeInstance(Player* p) : DisplayObject(Kind::Shape, p) {}
    const ShapeDef* shape = nullptr;
    const MorphDef* morph = nullptr;          // morph shape: drawn at the instance ratio
    const ShapeDef* current() const { return morph ? morph->at(ratio) : shape; }
    Rect localBounds() const override { const ShapeDef* s = current(); return s ? s->bounds : Rect{}; }
};

class BitmapInstance : public DisplayObject {
public:
    explicit BitmapInstance(Player* p) : DisplayObject(Kind::Bitmap, p) {}
    const BitmapDef* bitmap = nullptr;
    std::string path;                         // resolved PNG
    int width = 0, height = 0;                // stage pixels
    bool smoothing = true;
    Rect localBounds() const override { return {0, 0, width * 20.0f, height * 20.0f}; }
};

class StaticTextInstance : public DisplayObject {
public:
    explicit StaticTextInstance(Player* p) : DisplayObject(Kind::StaticText, p) {}
    const StaticTextDef* text = nullptr;
    Rect localBounds() const override { return text ? text->bounds : Rect{}; }
};

// One laid-out glyph of a text field (field space, twips).
struct GlyphRun {
    const FontDef* font = nullptr;
    int glyph = -1;
    float x = 0, y = 0;                       // baseline origin
    float size = 0;                           // em size in twips
    RGBA color;
    // Inline image (GFx setImageSubstitutions): drawn instead of a glyph.
    std::string image;
    float imgW = 0, imgH = 0;
};

struct TextFormatSpan {
    std::string font;
    float size = 12;                          // points (pixels)
    RGBA color;
    bool bold = false, italic = false, underline = false;
    int align = 0;                            // 0 left 1 right 2 center 3 justify
    float letterSpacing = 0, leading = 0, leftMargin = 0, rightMargin = 0, indent = 0;
    bool kerning = false;
    std::string url;
};

class TextField : public DisplayObject {
public:
    explicit TextField(Player* p) : DisplayObject(Kind::Text, p) {}
    const EditTextDef* edit = nullptr;
    Rect bounds;                              // field rectangle (twips, local)
    bool html = false, multiline = false, wordWrap = false, selectable = true, border = false, background = false;
    bool condenseWhite = false, embedFonts = true, password = false;
    std::string autoSize = "none";            // none / left / right / center
    std::string verticalAlign = "none";       // GFx extension
    std::string textAutoSize = "none";        // GFx extension (shrink / fit)
    RGBA borderColor{0, 0, 0, 255}, backgroundColor{255, 255, 255, 255};
    std::string variable;
    std::string variableShown;                // last value displayed from the variable binding
    int maxChars = 0;
    // Content: plain characters with a per-character format.
    std::u16string chars;
    std::vector<int> charFormat;              // index into formats
    std::vector<TextFormatSpan> formats;
    TextFormatSpan newFormat;                 // getNewTextFormat / format for text assignment
    std::string htmlSource;                   // last htmlText assignment (round trip)
    // Layout result.
    std::vector<GlyphRun> glyphs;
    float textWidth = 0, textHeight = 0;
    bool layoutDirty = true;
    // GFx shadow extension (accepted, drawn as PARTIAL drop shadow).
    float shadowAlpha = 0, shadowDistance = 0, shadowAngle = 45, shadowBlurX = 0, shadowBlurY = 0, shadowStrength = 1;
    uint32_t shadowColor = 0;
    std::shared_ptr<ShapeDef> boxShape;       // border / background (built on demand)
    // Input fields: DefineEditText without ReadOnly, or type = "input". Caret positions per character index (left
    // edge, line top, line height; index chars.size() = after the last character), filled by layout().
    bool inputType = false;
    bool editable() const { return inputType; }
    struct CaretPos { float x = 0, y = 0, h = 0; };
    std::vector<CaretPos> caretPos;
    struct ImageSub { std::u16string key; std::string path; float w = 0, h = 0, baseLineY = 0, natH = 0; };
    std::vector<ImageSub> imageSubs;          // GFx TextField.setImageSubstitutions

    void setPlainText(const std::string& utf8);
    void setHtmlText(const std::string& html);
    std::string plainText() const;
    std::string htmlText() const;
    void layout();
    // Pending text is laid out first: an autoSize field reports the size of its current text (Flash measures on
    // read, e.g. the lobby ticker spaces its messages by message_txt._width right after setting htmlText).
    Rect localBounds() const override { if (layoutDirty) const_cast<TextField*>(this)->layout(); return bounds; }
};

class MovieClip : public DisplayObject {
public:
    explicit MovieClip(Player* p) : DisplayObject(Kind::Clip, p) {}
    const SpriteDef* sprite = nullptr;        // timeline (empty clips: nullptr)
    int frame = -1;                           // 0-based current frame (-1 before the first)
    bool playing = true;
    bool isRootOfMovie = false;               // root timeline of a loaded movie (loadMovie / level)
    bool constructed = false;
    bool unloadPending = false;
    std::map<int, std::unique_ptr<DisplayObject>> children;   // by SWF depth
    std::set<uint16_t> initActionsRun;        // DoInitAction sprites already executed (per movie def)
    // Drawing API (moveTo / lineTo / beginFill ...).
    std::shared_ptr<ShapeDef> drawing;
    int drawFill = 0, drawLine = 0;
    Point drawPen;
    bool enabled = true, focusEnabled = false, tabEnabled = false;
    std::string lockroot;
    std::vector<ClipAction> clipActions;      // onClipEvent handlers from PlaceObject2/3
    uint32_t clipEventMask = 0;

    int totalFrames() const { return sprite ? std::max(1, sprite->frameCount) : 1; }
    DisplayObject* childAtDepth(int d) const;
    DisplayObject* childByName(const std::string& n) const;
    int nextHighestDepth() const;             // AS depth
    Rect localBounds() const override;
};

struct ClipEventQueue;

// One movie instance tree ("level") with its VM.
class Player {
public:
    using MovieResolver = std::function<std::string(const std::string& url, const std::string& fromDir)>;

    Player();
    ~Player();

    // Library: loads (and caches) a movie definition by path.
    std::shared_ptr<const MovieDef> loadDef(const std::string& path);
    bool loadRoot(const std::string& path);
    MovieClip* root() const { return root_; }
    avm1::VM& vm() { return *vm_; }
    const MovieDef* rootDef() const { return rootDef_.get(); }

    // Per tick at the movie frame rate (dt seconds); runs timelines, enterFrame, intervals, loaders.
    void advance(float dt);
    // Key events (Flash key codes) to Key listeners / Key.isDown.
    void keyEvent(int keyCode, bool down);
    // Text entry into the focused input field (Selection focus): typed characters, and the editing keys Backspace (8),
    // Delete (46), Left (37), Right (39), Home (36), End (35). Return true when a focused input field took it.
    bool textInput(char32_t c);
    bool textEditKey(int keyCode);
    void setTextFocus(TextField* tf);          // nullptr clears; mirrors Selection.getFocus
    TextField* textFocus() const { return focusText_ && !focusText_->removed ? focusText_ : nullptr; }
    size_t caretIndex() const { return caret_; }
    void setCaretIndex(size_t i);
    // Mouse (stage pixels). Flash 8 button semantics: the topmost visible, enabled clip with a button handler
    // (onPress / onRelease / onReleaseOutside / onRollOver / onRollOut / onDragOver / onDragOut, own or inherited)
    // whose geometry (or hitArea) contains the pointer receives the button events; Mouse listeners and clip
    // onMouseMove / onMouseDown / onMouseUp handlers receive every event.
    void mouseMove(float x, float y);
    void mouseButton(bool down);
    void mouseWheel(int delta);
    void mouseLeave();                        // pointer left the stage (no hover)
    float mouseX = 0, mouseY = 0;             // stage pixels; AS _xmouse / _ymouse are derived per clip
    MovieClip* hoverButton() const { return hover_; }
    bool hitTestPoint(const DisplayObject* d, float stageX, float stageY, bool shapeFlag) const;   // stage pixels
    double timeMs() const { return timeMs_; }

    // Character instantiation.
    DisplayObject* instantiate(MovieClip* parent, const std::shared_ptr<const MovieDef>& def, uint16_t charId, int depth,
                               const std::string& name, bool scripted);
    MovieClip* attachMovie(MovieClip* parent, const std::string& linkage, const std::string& name, int asDepth,
                           avm1::Object* initObj);
    MovieClip* createEmptyMovieClip(MovieClip* parent, const std::string& name, int asDepth);
    TextField* createTextField(MovieClip* parent, const std::string& name, int asDepth, float x, float y, float w, float h);
    BitmapInstance* attachBitmap(MovieClip* parent, avm1::Object* bitmapData, int asDepth, bool smoothing);
    MovieClip* duplicate(DisplayObject* src, const std::string& name, int asDepth, avm1::Object* initObj);
    void removeObject(DisplayObject* d);
    bool swapDepths(DisplayObject* d, int newSwfDepth);
    void loadMovieInto(MovieClip* target, const std::string& url, avm1::Object* loader);

    // Timeline.
    void gotoFrame(MovieClip* mc, int frame, bool play);
    int resolveFrame(MovieClip* mc, const avm1::Value& f);   // label / number -> 0-based, -1 if none
    void queueFrameActions(MovieClip* mc);
    void runInitActions(MovieClip* mc, int frame);

    // Script <-> display.
    avm1::Object* scriptObject(DisplayObject* d);
    DisplayObject* resolveTarget(const std::string& path, DisplayObject* base);   // "/a/b", "_root.a", "a.b", "../x"
    // Events.
    void dispatchClipEvent(MovieClip* mc, const char* method, uint32_t clipEventFlag);
    void queueAction(std::function<void()> fn) { actionQueue_.push_back(std::move(fn)); }
    void drainActions();

    // Intervals (setInterval / setTimeout).
    int addInterval(avm1::Value fnOrObj, const std::string& method, double ms, avm1::Args args, bool once);
    void clearInterval(int id);

    // Linkage resolution: exported name in `def` or (imported) in another movie.
    bool resolveExport(const std::shared_ptr<const MovieDef>& def, const std::string& name,
                       std::shared_ptr<const MovieDef>& outDef, uint16_t& outId);
    bool resolveCharacter(const std::shared_ptr<const MovieDef>& def, uint16_t id, std::shared_ptr<const MovieDef>& outDef,
                          uint16_t& outId, int depthGuard = 0);
    // flash.display.BitmapData.loadBitmap: an exported bitmap in any loaded library (and the libraries they import).
    bool findExportedBitmap(const std::string& linkage, std::shared_ptr<const MovieDef>& def, uint16_t& id);
    const FontDef* resolveFont(const std::string& name, bool bold, bool italic, const MovieDef* def);
    std::string translate(const std::string& text) const;   // GFx translator ($File.Section.Key)

    // Host configuration.
    MovieResolver resolveMovieUrl;            // "../_Shared/SharedComponents.swf" -> .gfx path
    std::function<std::string(const std::string& key)> translator;
    std::function<std::string(const std::string& resource)> externalTexture;   // resource name -> PNG path
    std::string fontLibPath;                  // gfxfontlib (Fonts_EFIGS.gfx)
    std::map<std::string, std::string> fontMap;   // "$TitleFont" -> "Distilla Cyrillic Regular"
    float stageWidth = 1120, stageHeight = 720;   // Stage.width / height (stage units)
    std::vector<avm1::Object*> keyListeners, stageListeners, mouseListeners;
    std::set<int> keysDown;
    int lastKeyCode = 0, lastAscii = 0;
    std::vector<std::unique_ptr<DisplayObject>> graveyard;   // removed instances (script objects may still refer)
    std::string movieName;                    // diagnostics

    // Rendering traversal.
    struct RenderItem {
        enum Type { Shape, Glyph, Bitmap, Image, MaskBegin, MaskEnd, MaskPop } type = Shape;
        std::string imagePath;                // Image: inline text image (stage pixels imgW x imgH)
        float imgW = 0, imgH = 0;
        const ShapeDef* shape = nullptr;      // Shape / Glyph (glyph outline) / mask shapes
        const BitmapInstance* bitmap = nullptr;
        Matrix m;
        CXForm cx;
        RGBA glyphColor;
        const DisplayObject* owner = nullptr;
    };
    void buildRenderList(const Matrix& base, std::vector<RenderItem>& out);

private:
    void advanceClip(MovieClip* mc);
    void applyFrameTags(MovieClip* mc, int frame, bool runActions);
    void placeObject(MovieClip* mc, const PlaceCmd& pc, int frame);
    void constructClip(MovieClip* mc, avm1::Object* initObj);
    void collectEnterFrame(MovieClip* mc, std::vector<MovieClip*>& out);
    void renderObject(const DisplayObject* d, const Matrix& m, const CXForm& cx, std::vector<RenderItem>& out);
    void tickIntervals();
    void processLoads();
    void unloadClip(DisplayObject* d);

    bool hitGeometry(const DisplayObject* d, const Point& world, bool ignoreVisible) const;   // world = stage twips
    MovieClip* findButton(MovieClip* mc, const Point& world);
    bool isButtonClip(MovieClip* mc);
    void callHandler(MovieClip* mc, const char* name);
    void broadcastMouse(const char* method, uint32_t clipEventFlag, const avm1::Args& args);
    void updateHover();
    void syncVariableText(MovieClip* mc);
    MovieClip* hover_ = nullptr;              // button under the pointer (not pressed)
    MovieClip* pressed_ = nullptr;            // button that received onPress
    bool pressedOver_ = false;
    bool mouseDown_ = false, mouseInside_ = false;

    std::unique_ptr<avm1::VM> vm_;
    std::map<std::string, std::shared_ptr<MovieDef>> defs_;
    std::shared_ptr<const MovieDef> rootDef_;
    MovieClip* root_ = nullptr;
    std::unique_ptr<MovieClip> rootOwner_;
    std::deque<std::function<void()>> actionQueue_;
    struct Interval { int id; avm1::Value target; std::string method; double ms; double next; avm1::Args args; bool once; bool dead = false; };
    std::vector<Interval> intervals_;
    int nextInterval_ = 1;
    struct PendingLoad { MovieClip* target; std::string url; avm1::Object* loader; };
    std::vector<PendingLoad> loads_;
    double timeMs_ = 0;
    TextField* focusText_ = nullptr;
    size_t caret_ = 0;
    std::shared_ptr<ShapeDef> caretShape_;    // unit rectangle, scaled to the caret
    TextField* inputFieldAt(MovieClip* mc, const Point& world, const Matrix& parent);
    void inputChanged(TextField* tf);
    std::set<const MovieDef*> initDone_;      // per definition: init actions executed (by sprite id)
    std::map<const MovieDef*, std::set<uint16_t>> initRun_;
    std::map<std::string, std::shared_ptr<MovieDef>> fontLib_;
    Matrix renderBase_;
    int instanceCounter_ = 0;
    bool renderingMask_ = false;

    friend class avm1::VM;
};

} // namespace gfx
