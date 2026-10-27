/* The level viewer's own parts (live_viewer.h is its interface to the rest of bc.exe): the state and the
 * functions its files share.
 *   live_viewer.cpp     the core: the game's state, the records, the capture, the replay hooks, the frozen loop,
 *                       the interface (live_viewer.h)
 *   viewer_edit.cpp     the level editor's record edits, the gizmo, mod files
 *   viewer_terrain.cpp  the editor's Terrain mode: tools, preview, texture slots, its overlay and panel
 *   viewer_panels.cpp   the overlays (markers, labels, collision), the Inspector, the panels, the status line
 *   viewer_input.cpp    keyboard, mouse and controllers; the free camera's step
 *   viewer_script.cpp   the test script (--viewer-test) and its virtual controller
 * Everything here is in live::vw. */
#ifndef VIEWER_INTERNAL_H
#define VIEWER_INTERNAL_H

#include <chrono>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "imgui/imgui.h"

#include "level_edit.h"
#include "live_viewer.h"
#include "orbit_math.h"
#include "viewer_records.h"

namespace live {
namespace vw {

using viewer::FlyCam;
using viewer::Record;
using viewer::Vec;

/* ---- the game's state (fixed N64 addresses, host order; tools_port/names.txt) ---- */
template <typename T> T rd(uint32_t a) {
    return *reinterpret_cast<volatile T *>(uintptr_t(a));
}
constexpr uint32_t kGameMode = 0x80364A90u, kLevelNo = 0x802E8BDCu, kLevelData = 0x80358074u,
                   kHeapPtr = 0x80358070u, kFrontEnd = 0x80370C50u, kFramesInMode = 0x80358060u,
                   kLevelInfo = 0x802E8F94u, kGlobeLevels = 0x8020D810u, kLevelDl = 0x80358030u,
                   kLevelFileDl = 0x803BE6E0u, kGObjects = 0x803F4030u, kGObjectsEnd = 0x803F7654u,
                   kGVehicles = 0x80364460u, kGVehiclesEnd = 0x803649D0u, kGroundCells = 0x803BDB10u,
                   kGroundCellW = 0x803BE718u, kLevelCols = 0x803BE714u, kCarrierX = 0x803EF6DCu,
                   kDriverPos = 0x803ED808u, kDigger = 0x803643D4u;
constexpr uint32_t kDyn = 0x803156F8u, kDynSize = 0x21498u, kDynDl = 0x48B0u, kDynDlEnd = 0xA3A0u, kDynView = 0x140u,
                   kDynProj = 0x80u, kDynLevelCalls = 0xA4E0u;
constexpr uint64_t MODE_LEVEL = 4, MODE_LEVEL_START = 0x2000, MODE_MAP_VIEW = 0x100, MODE_CARRIER_END = 0x200;

inline bool inRam(uint32_t a, uint32_t n = 1) {
    return a >= 0x80000000u && a + n <= 0x80800000u && a + n >= a;
}

extern ViewerEnv g_env;

/* ---- what is drawn: categories -------------------------------------------------- */
enum Cat { CAT_TERRAIN, CAT_BUILDINGS, CAT_VEHICLES, CAT_EFFECTS, CAT_OTHER, CAT_SKY, CAT_HUD, CAT_COUNT };
const char *const kCatNames[CAT_COUNT] = {"terrain", "buildings", "vehicles", "effects", "other", "sky", "hud"};
const char *const kCatLabels[CAT_COUNT] = {"Terrain (the level's geometry)", "Buildings and objects",
                                           "Vehicles (and their shadows)", "Effects (smoke, particles, people)",
                                           "Other world items (RDUs, TNT, crates, trails, water)", "Sky",
                                           "HUD and text"};
/* the tag kinds (include/game/port.h PORT_ITAG_*) */
enum {
    K_NONE = 0, K_HUD = 1, K_OBJ, K_SHADOW, K_SHADOWFE, K_BUILDING, K_BPART, K_FALL, K_RISE, K_BILLBOARD,
    K_PARTICLE, K_PEOPLE, K_GSHADOW, K_MARKER, K_RADAR, K_TERRAIN = 100
};

/* ---- options (the panels, the test script) -------------------------------------- */
struct Options {
    bool show[CAT_COUNT] = {true, true, true, true, true, true, false};
    bool farOut = true;           /* the far plane out at kFar (else the game's 10000) */
    bool plain = true;            /* a plain background instead of the sky (the game's sky is a flat picture
                                     for its own camera's heading and pitch: wrong for any other view) */
    bool allCells = true;         /* capture with every cell that fits (else the game's own visible cells) */
    bool markers = true, labels = true, collision = false, zones = false;
    bool kinds[viewer::REC_KINDS] = {true, true, true, true, true, true, true, true, true, false, false, false,
                                     false, false};
    double speed = 600;           /* drawing units a second */
    double labelDist = 1200;      /* labels within this distance */
    bool ui = true;               /* the panels */
};
extern Options g_opt;
constexpr double kFar = 120000.0;
constexpr float kPanelScale = 0.8f;   /* the viewer's panels: smaller than the settings menu's text */

/* ---- the state (live_viewer.cpp) ------------------------------------------------ */
extern bool g_loadedLevel;        /* the viewer loaded a level: leaving restarts bc.exe */
extern unsigned g_shots;
extern unsigned g_capturedFrame;  /* plat_frames() of the frozen frame */
extern bool g_recapture;          /* a recapture asked for (the frozen loop returns) */
extern bool g_leave, g_quitAfter;
extern bool g_resetCam;           /* the next freeze starts from the game's camera (a new level, a new entry) */

/* the frozen frame */
struct Frozen {
    bool level = false;            /* a level frame (its DynamicBuf known): the free camera works */
    unsigned dyn = 0;
    double view[4][4] = {}, proj[4][4] = {};
    unsigned long long mode = 0;
    int levelNo = -1;
    uint32_t origin = 0;           /* the VI origin of the frozen picture */
    uint32_t lvl = 0;              /* gLevelData */
    uint32_t terrain[12] = {};     /* the level lists (both buffers) and fixed lists: terrain */
    uint32_t levelCalls = 0;
    unsigned cellsDrawn = 0, cells = 0;
};
extern Frozen g_fz;

/* ---- the records ---------------------------------------------------------------- */
extern std::vector<Record> g_recs;   /* the level file's, then gObjects', then gVehicles' */
extern std::vector<std::string> g_notes;
extern int g_sel;                    /* the selected record */
struct Tri {
    float v[9];
    uint8_t src;                  /* 0 grid A, 1 grid B, 2 building */
};
extern std::vector<Tri> g_tris;

/* ---- the camera ----------------------------------------------------------------- */
extern FlyCam g_cam;
struct ModelView {
    bool on = false;
    uint32_t kind = 0, index = 0; /* the owner shown (K_OBJ: a gVehicles index, K_BUILDING: a gObjects one) */
    int rec = -1;                 /* the record it was opened from */
    double yaw = 30, pitch = 20, dist = 0;
    Vec centre;
    double radius = 0;
    bool measured = false;        /* centre and radius from its drawing (the vertices it drew) */
    double lo[3], hi[3];
    unsigned vtx = 0;
    std::string title;
};
extern ModelView g_mv;
extern bool g_measure;            /* this replay measures the model view's model */

/* the view the render thread gives RT64 (viewHook) */
struct ViewShare {
    bool on = false;
    float game[4][4] = {};        /* the frozen frame's view: projections with it get ours */
    float view[4][4] = {};
    bool farOut = false;
    bool haveProj = false;
    double proj[4][4] = {};       /* the projection RT64 drew with (for the overlays) */
    unsigned hooked = 0;
};
extern ViewShare g_vs;
extern std::mutex g_vsMutex;

void currentView(double m[4][4]);
double groundAt(double x, double z, double fallback);
uint32_t levelData();
size_t levelSpan(uint32_t lvl);
std::string levelLabel(int i);
void readFileRecords();
int findRecord(int kind, int index);
int vehicleOf(const Record &spawn);
void jumpToStart();
void cameraFromGame();
void cameraTop();
extern bool g_previewNoCull;      /* the terrain preview is on: the lists' G_CULLDL boxes are the old ones */
void replay();
void requestLevel(int level);
void leave();

/* ---- the level editor (viewer_edit.cpp) ------------------------------------------ */
extern edit::Editor g_ed;
extern bool g_editMode;           /* the editor's panels, gizmo and keys are on */
struct Drag {
    int mode = 0;                 /* 0 none, 1 on the ground plane, 2 along x, 3 along y, 4 along z, 5 heading */
    int kind = -1, index = -1;
    Vec start, cur;
    double startHeading = 0, heading = 0;
    double grabX = 0, grabZ = 0, t0 = 0, a0 = 0;
    bool moved = false;
};
extern Drag g_drag;
struct EditOpts {
    bool snapGround = true;       /* a record moved on the ground plane sits on the ground (its y = the floor's) */
    int grid = 0;                 /* x / z snapped to this step (map units; 0: none) */
};
extern EditOpts g_eopt;
extern std::string g_editMsg;     /* the last edit's result (the Changes panel) */
extern std::string g_savedSha;    /* the edited file last saved to a mod file */
extern std::string g_savedPath;
extern char g_modName[64];
extern bool g_modEnable;
extern bool g_playAfterLoad;      /* "Play from here": leave the viewer into play once the level is up */
extern std::chrono::steady_clock::time_point g_applyStart;   /* Apply: the reload's time, to the frozen picture */
extern bool g_applyPending;
extern double g_applyMs;
/* the overlays' projection of this tick (the gizmo's picking) */
struct OverlayView {
    bool ok = false;
    double view[4][4] = {}, proj[4][4] = {};
    double rx = 0, ry = 0, rw = 0, rh = 0;
};
extern OverlayView g_ov;
/* edits asked for by the panels: run after the UI frame is built (they replace the records the panels read) */
extern std::vector<std::function<void()>> g_after;

bool isEditable(int kind);
bool kindHasY(int kind);
bool kindHasHeading(int kind);
bool editing();
void reselect(int kind, int index);
void setEditMode(bool on);
bool pushEdit(const LmChange &c, int selKind, int selIndex);
bool pushEditBurst(int kind, int index, int field, long value);
std::string carrierWarning();
void editUndo();
void editRedo();
bool selEditable(int &kind, int &index);
void duplicateSel();
void deleteSel();
void rotateSel(double deg);
void groundSel();
std::string modsDir();
void applyEdits();
void playEdits();
bool saveMod(const std::string &name, bool enable);
void autosave();

/* the gizmo */
struct Gizmo {
    bool ok = false;
    ImVec2 c;                     /* the centre on screen */
    ImVec2 tip[3];                /* the x, y, z arrows' tips */
    bool axis[3] = {};
    std::vector<ImVec2> ring;     /* the heading ring (empty: none) */
    double len = 0;               /* the arrows' world length */
    Vec p;
};
bool projectPoint(const Vec &p, ImVec2 &s, double *depth = nullptr);
Gizmo gizmo();
int gizmoHit(float x, float y);
bool screenRay(float sx, float sy, edit::V3 &o, edit::V3 &d);
bool beginDrag(int mode, float sx, float sy);
void updateDrag(float sx, float sy);
void endDrag();
void drawGizmo(ImDrawList *dl);

/* ---- the terrain tools (viewer_terrain.cpp) --------------------------------------- */
enum TTool { TT_RAISE, TT_LOWER, TT_SMOOTH, TT_FLATTEN, TT_RAMP, TT_SELECT, TT_PAINT, TT_COUNT };
const char *const kToolNames[TT_COUNT] = {"Raise", "Lower", "Smooth", "Flatten", "Ramp", "Select", "Paint"};
struct TerrainView {
    bool on = false;              /* the editor's Terrain mode (else the records) */
    int tool = TT_RAISE;
    terrain::Brush brush;         /* (strength: raise / lower map units a second; amount for the others) */
    double rate = 80, amount = 0.35;
    double flatY = 0;
    bool flatPicked = false;
    /* a stroke */
    bool stroking = false;
    std::unordered_map<int, double> cur;
    std::unordered_map<int, terrain::P3> shown;   /* the stroke's corners with the welds (what the preview shows) */
    unsigned shownApps = ~0u;                     /* (the application they were resolved for) */
    int skipped = 0;
    double tickAcc = 0;
    unsigned applications = 0;
    /* where the tool points */
    bool hoverOk = false;
    double hover[3] = {};
    int hoverTri = -1;
    float mx = -1, my = -1;
    bool padAim = false;
    /* the ramp */
    int rampN = 0;
    double rampA[3] = {}, rampB[3] = {};
    double rampRise = 40;
    /* the selection */
    bool selVerts = false;
    std::vector<int> selTri, selPos;
    int moveBy[3] = {0, 8, 0};
    /* painting */
    int paintTex = -1;
    bool planar = true;
    /* a texture slot from a PNG */
    char importPath[512] = "";
    int importSlot = -1;
    std::string importMsg, importPng64;
    std::vector<uint8_t> importShow;   /* what the slot will hold, as RGBA8 */
    uint32_t importW = 0, importH = 0;
    size_t importPacked = 0;
    uint32_t importRoom = 0;
    edit::TexSlot importTex;
    /* the preview */
    std::vector<terrain::P3> applied;   /* the corners' values in the file the game runs */
    uint32_t mapLvl = 0;
    unsigned mapFrame = ~0u;
    size_t mapCorners = 0;
    std::vector<std::pair<uint32_t, int>> map;   /* RDRAM vertex -> corner */
    std::vector<std::pair<uint32_t, uint64_t>> saved;
    bool previewOn = false;
    unsigned previewed = 0;
    double previewMs = 0;           /* the last replay's writes and their putting back */
    std::string msg;
    std::vector<double> strokeMs;   /* the timings of the last strokes' steps (rebuild included) */
    bool applyEach = false;         /* Apply after every step (the game's collision follows at once) */
    size_t seams = 0;               /* corners on another triangle's edge (an old mod's splits): Close seams */
};
extern TerrainView g_tv;
/* the level's textures (the drawn triangles' states): id -> format, size, a run to join near (x, z) */
struct PalEntry {
    int tex = -1, fmt = 0, siz = 0, w = 0, h = 0, count = 0;
};
/* a texture slot the selection draws */
struct SlotUse {
    int tex, fmt, siz, w, h, mips;
};

bool terrainMode();
void terrainOpened();
void terrainApplied();
void terrainChanged();
void previewWrite();
void previewRestore();
void terrainHover(float sx, float sy);
void applyBrushOnce(double perApp);
void makeRamp();
void moveSelection(int dx, int dy, int dz);
std::string weldNote();
void splitSelection();
void sealSeams();
std::vector<PalEntry> palette();
std::vector<int> texturesHere();
void paintSelection(int tex);
void retexRun(int tex);
void followRecords();
std::vector<SlotUse> selectionSlots();
bool importLoad(const std::string &path, const SlotUse &u, bool asNew = false);
/* the on-screen keyboard popup for a text field (a controller alone; viewer_panels.cpp): open it with
 * ImGui::OpenPopup(popup), call this every frame; path: the keys a file path needs too */
void nameKeyboard(const char *popup, char *buf, size_t n, bool path);
void importAdd();
void terrainMouseDown(float x, float y, bool shift, bool ctrl);
void terrainMouseUp();
void terrainTick(double dt);
bool terrainKey(SDL_Scancode sc, bool ctrl, bool shift);
void terrainPad(unsigned short buttons, unsigned short pressed);
void drawTerrainOverlay(ImDrawList *dl);
void terrainPanel();

/* ---- the overlays and the panels (viewer_panels.cpp) -------------------------------- */
extern std::string g_status;
extern bool g_cleanShot;          /* this tick's picture is a clean screenshot: no panels, no overlays */
extern bool g_overlayShot;        /* ... with the overlays, no panels */
extern bool g_padNav;             /* the controller works the panels (Start) */
extern bool g_padNavFocus;        /* (focus a panel on the next frame) */
extern std::string g_padNavWin;   /* (the panel the controller was on when the frozen loop ended: Apply) */
void selectAt(float x, float y);
bool modelFor(int rec);
std::string shotPath(const std::string &name);
std::string autoShotName(bool overlays);
void buildUi();

/* ---- input (viewer_input.cpp) -------------------------------------------------------- */
extern bool g_looking;            /* right mouse button held: mouse look */
void viewerEvent(SDL_Event &e);
void stepCamera(double dt);

/* ---- the test script (viewer_script.cpp) ---------------------------------------------- */
extern std::vector<std::string> g_test;
void doShot(const std::string &name, bool overlays, bool panels = false);
void vpadAfterFrame();
void runTestOp();

}  // namespace vw
}  // namespace live

#endif
