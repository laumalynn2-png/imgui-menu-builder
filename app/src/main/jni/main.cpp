#include <jni.h>
#include <errno.h>

#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>
#include <sstream>

#include <android/log.h>

#include <EGL/egl.h>
#include <GLES3/gl3.h>

#include "ImGui/imgui.h"
#include "ImGui/imgui_internal.h"
#include "ImGui/backends/imgui_impl_android.h"
#include "ImGui/backends/imgui_impl_opengl3.h"

//====================================
#include "ImGui/FONTS/DEFAULT.h"
//=====================================

#define LOG_TAG "ImGuiBuilder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
int screenWidth = 0;
int screenHeight = 0;
bool g_Initialized = false;

static JavaVM* g_jvm = NULL;

static std::string g_savePath;
static std::string g_code;
static std::string g_save;
static bool g_dirty = true;

static char g_title[64] = "ImGui Menu";
static ImVec4 g_bg(0.06f, 0.06f, 0.09f, 1.0f);
static float g_scale = 2.2f;
static bool g_scaleApplied = false;

// ---------------------------------------------------------------------------
// Widget model
// ---------------------------------------------------------------------------
enum WidgetType {
    W_TEXT = 0,
    W_BUTTON,
    W_CHECKBOX,
    W_SLIDER_FLOAT,
    W_SLIDER_INT,
    W_COMBO,
    W_SEPARATOR,
    W_COLOR_EDIT4,
    W_INPUT_TEXT,
    W_TYPE_COUNT
};

struct Widget {
    int   type;
    char  label[96];
    float f1, f2, f3, f4;
    int   i1, i2;
    float c[4];
    char  text[256];
    char  items[256];

    Widget() {
        type = W_TEXT;
        strncpy(label, "Widget", sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';
        f1 = 0.0f; f2 = 1.0f; f3 = 0.0f; f4 = 1.0f;
        i1 = 0; i2 = 0;
        c[0] = 1.0f; c[1] = 0.35f; c[2] = 0.35f; c[3] = 1.0f;
        strncpy(text, "Text", sizeof(text) - 1);
        text[sizeof(text) - 1] = '\0';
        strncpy(items, "Option A|Option B", sizeof(items) - 1);
        items[sizeof(items) - 1] = '\0';
    }
};

static std::vector<Widget> g_widgets;
static int g_sel = -1;   // currently selected widget index

// ---------------------------------------------------------------------------
// Small helpers
// ---------------------------------------------------------------------------
static void copyStr(char* dst, int dstSize, const char* src) {
    if (dstSize <= 0) return;
    if (!src) src = "";
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

static std::string fmtFloat(float f) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%g", (double)f);
    return std::string(buf);
}

// Escape a string so it can be embedded inside a C++ "..." literal.
static std::string escapeCpp(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char ch : s) {
        if (ch == '\\' || ch == '"') out += '\\';
        if (ch == '\n' || ch == '\r' || ch == '\t') continue;
        out += ch;
    }
    return out;
}

// Sanitize a widget label into a valid C++ identifier.
// Non [A-Za-z0-9_] chars become '_'; prefix 'w' if it starts with a digit;
// ensure uniqueness by appending _<index>.
static std::string sanitizeVar(const char* label, int index, std::vector<std::string>& used) {
    std::string v;
    for (const char* p = label; *p; ++p) {
        char ch = *p;
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
            (ch >= '0' && ch <= '9') || ch == '_')
            v += ch;
        else
            v += '_';
    }
    if (v.empty()) v = "widget";
    if (v[0] >= '0' && v[0] <= '9') v = "w" + v;

    std::string base = v;
    int n = index;
    for (;;) {
        bool dup = false;
        for (size_t k = 0; k < used.size(); ++k) {
            if (used[k] == v) { dup = true; break; }
        }
        if (!dup) break;
        char suf[32];
        snprintf(suf, sizeof(suf), "_%d", n++);
        v = base + suf;
    }
    used.push_back(v);
    return v;
}

// Convert "A|B|C" into "A\0B\0C\0\0" for the zero-separated Combo overload.
// Returns the number of items.
static int splitItemsToComboBuffer(const char* items, char* buf, int bufSize) {
    if (bufSize <= 0) return 0;
    int count = 0;
    int i = 0;
    bool inItem = false;
    if (items) {
        for (const char* p = items; *p && i < bufSize - 2; ++p) {
            if (*p == '|') {
                if (inItem) { count++; inItem = false; }
                buf[i++] = '\0';
            } else {
                inItem = true;
                buf[i++] = *p;
            }
        }
    }
    if (inItem) count++;
    buf[i++] = '\0';                       // terminate last item
    if (i < bufSize) buf[i++] = '\0';      // empty string marks end of list
    else buf[bufSize - 1] = '\0';
    return count;
}

static void addWidget(int type) {
    Widget w;
    w.type = type;
    switch (type) {
        case W_TEXT:         copyStr(w.label, 96, "Text");      copyStr(w.text, 256, "Hello ImGui"); break;
        case W_BUTTON:       copyStr(w.label, 96, "Button");    break;
        case W_CHECKBOX:     copyStr(w.label, 96, "Checkbox");  break;
        case W_SLIDER_FLOAT: copyStr(w.label, 96, "Slider");    w.f1 = 0.0f; w.f2 = 100.0f; w.f3 = 50.0f; break;
        case W_SLIDER_INT:   copyStr(w.label, 96, "SliderInt"); w.f1 = 0.0f; w.f2 = 100.0f; w.f3 = 50.0f; break;
        case W_COMBO:        copyStr(w.label, 96, "Combo");     w.i1 = 0; break;
        case W_SEPARATOR:    copyStr(w.label, 96, "Separator"); break;
        case W_COLOR_EDIT4:  copyStr(w.label, 96, "Color");     break;
        case W_INPUT_TEXT:   copyStr(w.label, 96, "Input");     copyStr(w.text, 256, "Text"); break;
        default: break;
    }
    g_widgets.push_back(w);
    g_sel = (int)g_widgets.size() - 1;
    g_dirty = true;
}

// ---------------------------------------------------------------------------
// Preview rendering of a widget
// ---------------------------------------------------------------------------
static void renderWidget(Widget& w) {
    switch (w.type) {
        case W_TEXT:
            ImGui::TextUnformatted(w.text, w.text + strlen(w.text));
            break;
        case W_BUTTON:
            ImGui::Button(w.label);
            break;
        case W_CHECKBOX: {
            bool v = (w.i1 != 0);
            if (ImGui::Checkbox(w.label, &v)) w.i1 = v ? 1 : 0;
            break;
        }
        case W_SLIDER_FLOAT:
            ImGui::SliderFloat(w.label, &w.f3, w.f1, w.f2);
            break;
        case W_SLIDER_INT: {
            int v = (int)w.f3;
            if (ImGui::SliderInt(w.label, &v, (int)w.f1, (int)w.f2)) w.f3 = (float)v;
            break;
        }
        case W_COMBO: {
            char buf[512];
            int count = splitItemsToComboBuffer(w.items, buf, (int)sizeof(buf));
            if (w.i1 >= count) w.i1 = (count > 0) ? count - 1 : 0;
            if (w.i1 < 0) w.i1 = 0;
            if (count > 0)
                ImGui::Combo(w.label, &w.i1, buf);
            else
                ImGui::Text("%s: (no items)", w.label);
            break;
        }
        case W_SEPARATOR:
            ImGui::Separator();
            break;
        case W_COLOR_EDIT4:
            ImGui::ColorEdit4(w.label, w.c);
            break;
        case W_INPUT_TEXT:
            ImGui::InputText(w.label, w.text, sizeof(w.text));
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Code + save generation
// ---------------------------------------------------------------------------
static std::string buildCode() {
    std::string out;
    out.reserve(4096);
    out += "// Auto-generated by ImGui Builder\n";
    out += "// Requires Dear ImGui + imgui_impl_android + imgui_impl_opengl3 backends\n";
    out += "#include \"imgui.h\"\n";
    out += "#include \"imgui_impl_android.h\"\n";
    out += "#include \"imgui_impl_opengl3.h\"\n";
    out += "// --- one-time init ---\n";
    out += "// ImGui::CreateContext();\n";
    out += "// ImGui::StyleColorsDark();\n";
    out += "// ImGui_ImplAndroid_Init();\n";
    out += "// ImGui_ImplOpenGL3_Init(\"#version 300 es\");\n";
    out += "// --- call every frame ---\n";
    out += "void DrawMenu() {\n";
    out += "    ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);\n";
    out += "    if (ImGui::Begin(\"" + escapeCpp(std::string(g_title)) + "\")) {\n";

    std::vector<std::string> used;
    for (size_t idx = 0; idx < g_widgets.size(); ++idx) {
        const Widget& w = g_widgets[idx];
        std::string v = sanitizeVar(w.label, (int)idx, used);
        char line[1024];

        switch (w.type) {
            case W_TEXT:
                out += "        ImGui::TextUnformatted(\"" + escapeCpp(std::string(w.text)) + "\");\n";
                break;
            case W_BUTTON:
                out += "        if (ImGui::Button(\"" + escapeCpp(std::string(w.label)) + "\")) { /* TODO */ }\n";
                break;
            case W_CHECKBOX:
                out += "        static bool " + v + "_state = false;\n";
                out += "        ImGui::Checkbox(\"" + escapeCpp(std::string(w.label)) + "\", &" + v + "_state);\n";
                break;
            case W_SLIDER_FLOAT:
                out += "        static float " + v + "_v = " + fmtFloat(w.f3) + "f;\n";
                out += "        ImGui::SliderFloat(\"" + escapeCpp(std::string(w.label)) + "\", &" + v + "_v, " +
                       fmtFloat(w.f1) + "f, " + fmtFloat(w.f2) + "f);\n";
                break;
            case W_SLIDER_INT:
                snprintf(line, sizeof(line), "        static int %s_v = %d;\n", v.c_str(), (int)w.f3);
                out += line;
                snprintf(line, sizeof(line), "        ImGui::SliderInt(\"%s\", &%s_v, %d, %d);\n",
                         escapeCpp(std::string(w.label)).c_str(), v.c_str(), (int)w.f1, (int)w.f2);
                out += line;
                break;
            case W_COMBO: {
                std::string itemsCode;
                std::istringstream ss(w.items);
                std::string item;
                bool first = true;
                while (std::getline(ss, item, '|')) {
                    if (!first) itemsCode += ", ";
                    itemsCode += "\"" + escapeCpp(item) + "\"";
                    first = false;
                }
                if (first) itemsCode = "\"Option A\"";
                out += "        static int " + v + "_i = 0;\n";
                out += "        const char* " + v + "_items[] = { " + itemsCode + " };\n";
                out += "        ImGui::Combo(\"" + escapeCpp(std::string(w.label)) + "\", &" + v + "_i, " +
                       v + "_items, IM_ARRAYSIZE(" + v + "_items));\n";
                break;
            }
            case W_SEPARATOR:
                out += "        ImGui::Separator();\n";
                break;
            case W_COLOR_EDIT4:
                out += "        static float " + v + "_c[4] = { " + fmtFloat(w.c[0]) + "f, " +
                       fmtFloat(w.c[1]) + "f, " + fmtFloat(w.c[2]) + "f, " + fmtFloat(w.c[3]) + "f };\n";
                out += "        ImGui::ColorEdit4(\"" + escapeCpp(std::string(w.label)) + "\", " + v + "_c);\n";
                break;
            case W_INPUT_TEXT:
                out += "        static char " + v + "_buf[128] = \"\";\n";
                out += "        ImGui::InputText(\"" + escapeCpp(std::string(w.label)) + "\", " +
                       v + "_buf, sizeof(" + v + "_buf));\n";
                break;
            default:
                break;
        }
    }

    out += "    }\n";
    out += "    ImGui::End();\n";
    out += "}\n";
    return out;
}

static std::string buildSave() {
    std::string out;
    out.reserve(2048);
    out += "TITLE ";
    out += g_title;
    out += "\n";
    char buf[64];
    snprintf(buf, sizeof(buf), "SCALE %g\n", (double)g_scale);
    out += buf;
    for (size_t i = 0; i < g_widgets.size(); ++i) {
        const Widget& w = g_widgets[i];
        char line[1024];
        snprintf(line, sizeof(line),
                 "W %d|%s|%g|%g|%g|%g|%d|%d|%g|%g|%g|%g|%s|%s\n",
                 w.type, w.label,
                 (double)w.f1, (double)w.f2, (double)w.f3, (double)w.f4,
                 w.i1, w.i2,
                 (double)w.c[0], (double)w.c[1], (double)w.c[2], (double)w.c[3],
                 w.text, w.items);
        out += line;
    }
    return out;
}

// Rebuild both the generated code and the save content.
static void rebuildAll() {
    g_code = buildCode();
    g_save = buildSave();
    g_dirty = false;
}

// ---------------------------------------------------------------------------
// Save-format parser
// ---------------------------------------------------------------------------
static void parseSave(const std::string& content) {
    g_widgets.clear();
    g_sel = -1;
    std::istringstream stream(content);
    std::string line;
    while (std::getline(stream, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
        if (line.empty()) continue;

        if (line.compare(0, 6, "TITLE ") == 0) {
            copyStr(g_title, (int)sizeof(g_title), line.c_str() + 6);
            continue;
        }
        if (line.compare(0, 6, "SCALE ") == 0) {
            float s = (float)atof(line.c_str() + 6);
            if (s < 1.0f) s = 1.0f;
            if (s > 3.5f) s = 3.5f;
            g_scale = s;
            continue;
        }
        if (line.compare(0, 2, "W ") != 0) continue;

        // W <type>|<label>|<f1>|<f2>|<f3>|<f4>|<i1>|<i2>|<c0>|<c1>|<c2>|<c3>|<text>|<items>
        std::vector<std::string> f;
        std::istringstream ls(line.c_str() + 2);
        std::string field;
        while (std::getline(ls, field, '|')) f.push_back(field);
        if (f.size() < 14) continue;

        Widget w;
        w.type = atoi(f[0].c_str());
        if (w.type < 0 || w.type >= W_TYPE_COUNT) continue;
        copyStr(w.label, (int)sizeof(w.label), f[1].c_str());
        w.f1 = (float)atof(f[2].c_str());
        w.f2 = (float)atof(f[3].c_str());
        w.f3 = (float)atof(f[4].c_str());
        w.f4 = (float)atof(f[5].c_str());
        w.i1 = atoi(f[6].c_str());
        w.i2 = atoi(f[7].c_str());
        w.c[0] = (float)atof(f[8].c_str());
        w.c[1] = (float)atof(f[9].c_str());
        w.c[2] = (float)atof(f[10].c_str());
        w.c[3] = (float)atof(f[11].c_str());
        copyStr(w.text,  (int)sizeof(w.text),  f[12].c_str());
        copyStr(w.items, (int)sizeof(w.items), f[13].c_str());
        g_widgets.push_back(w);
    }
    g_dirty = true;
}

// ---------------------------------------------------------------------------
// Java callback helpers (MainActivity static methods)
// ---------------------------------------------------------------------------
static JNIEnv* getJniEnv() {
    JNIEnv* env = NULL;
    if (g_jvm) g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    return env;
}

// public static void copyToClipboard(String text)
static void jCopy(const std::string& s) {
    JNIEnv* env = getJniEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/qoder/imguibuilder/MainActivity");
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "copyToClipboard", "(Ljava/lang/String;)V");
    if (!mid) return;
    jstring js = env->NewStringUTF(s.c_str());
    env->CallStaticVoidMethod(cls, mid, js);
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(cls);
}

// public static void saveContent(String content)
static void jSave(const std::string& s) {
    JNIEnv* env = getJniEnv();
    if (!env) return;
    jclass cls = env->FindClass("com/qoder/imguibuilder/MainActivity");
    if (!cls) return;
    jmethodID mid = env->GetStaticMethodID(cls, "saveContent", "(Ljava/lang/String;)V");
    if (!mid) return;
    jstring js = env->NewStringUTF(s.c_str());
    env->CallStaticVoidMethod(cls, mid, js);
    env->DeleteLocalRef(js);
    env->DeleteLocalRef(cls);
}

// public static String loadContent()  (returns "" if none)
static std::string jLoad() {
    JNIEnv* env = getJniEnv();
    if (!env) return std::string();
    jclass cls = env->FindClass("com/qoder/imguibuilder/MainActivity");
    if (!cls) return std::string();
    jmethodID mid = env->GetStaticMethodID(cls, "loadContent", "()Ljava/lang/String;");
    if (!mid) return std::string();
    jstring js = (jstring)env->CallStaticObjectMethod(cls, mid);
    std::string out;
    if (js) {
        const char* utf = env->GetStringUTFChars(js, NULL);
        if (utf) {
            out = utf;
            env->ReleaseStringUTFChars(js, utf);
        }
        env->DeleteLocalRef(js);
    }
    env->DeleteLocalRef(cls);
    return out;
}

// ---------------------------------------------------------------------------
// Builder UI
// ---------------------------------------------------------------------------
static void widgetsTab() {
    // --- palette: 3 buttons per row ---
    struct PaletteEntry { const char* name; int type; };
    static const PaletteEntry palette[] = {
        { "Text",         W_TEXT },
        { "Button",       W_BUTTON },
        { "Checkbox",     W_CHECKBOX },
        { "Slider Float", W_SLIDER_FLOAT },
        { "Slider Int",   W_SLIDER_INT },
        { "Combo",        W_COMBO },
        { "Separator",    W_SEPARATOR },
        { "Color Edit",   W_COLOR_EDIT4 },
        { "Input Text",   W_INPUT_TEXT },
    };
    for (int i = 0; i < (int)(sizeof(palette) / sizeof(palette[0])); ++i) {
        if (i > 0 && (i % 3) != 0) ImGui::SameLine();
        if (ImGui::Button(palette[i].name, ImVec2(ImGui::GetContentRegionAvail().x, 0)))
            addWidget(palette[i].type);
    }

    ImGui::Separator();

    // --- Clear All with two-click confirmation ---
    static bool confirming = false;
    if (!confirming) {
        if (ImGui::Button("Clear All")) confirming = true;
    } else {
        if (ImGui::Button("Confirm Clear")) {
            g_widgets.clear();
            g_sel = -1;
            g_dirty = true;
            confirming = false;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) confirming = false;
    }
    ImGui::SameLine();
    ImGui::Text("(%d widgets)", (int)g_widgets.size());

    // --- widget list ---
    float listHeight = -8.0f * ImGui::GetTextLineHeightWithSpacing();
    if (ImGui::BeginChild("##widgetList", ImVec2(0, listHeight), ImGuiChildFlags_None, ImGuiWindowFlags_None)) {
        float labelWidth = ImGui::GetWindowWidth() * 0.42f;
        for (int i = 0; i < (int)g_widgets.size(); ++i) {
            Widget& w = g_widgets[i];
            ImGui::PushID(i);

            ImGui::AlignTextToFramePadding();
            ImGui::BulletText("%d", i + 1);
            ImGui::SameLine();

            ImGui::PushItemWidth(labelWidth);
            if (ImGui::InputText("##label", w.label, sizeof(w.label)))
                g_dirty = true;
            ImGui::PopItemWidth();
            if (ImGui::IsItemActivated()) g_sel = i;
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) g_sel = i;

            ImGui::SameLine();
            if (ImGui::Button("Up")) {
                if (i > 0) {
                    std::swap(g_widgets[i], g_widgets[i - 1]);
                    if (g_sel == i) g_sel = i - 1;
                    else if (g_sel == i - 1) g_sel = i;
                    g_dirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Dn")) {
                if (i + 1 < (int)g_widgets.size()) {
                    std::swap(g_widgets[i], g_widgets[i + 1]);
                    if (g_sel == i) g_sel = i + 1;
                    else if (g_sel == i + 1) g_sel = i;
                    g_dirty = true;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Del")) {
                g_widgets.erase(g_widgets.begin() + i);
                if (g_sel == i) g_sel = -1;
                else if (g_sel > i) g_sel--;
                g_dirty = true;
                ImGui::PopID();
                break;  // indices shifted, stop this frame
            }

            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    // --- property panel for selected widget ---
    ImGui::Separator();
    if (g_sel >= 0 && g_sel < (int)g_widgets.size()) {
        Widget& w = g_widgets[g_sel];
        ImGui::Text("Properties (#%d: %s)", g_sel + 1, w.label);
        switch (w.type) {
            case W_SLIDER_FLOAT:
                if (ImGui::InputFloat("Min", &w.f1)) g_dirty = true;
                if (ImGui::InputFloat("Max", &w.f2)) g_dirty = true;
                if (ImGui::InputFloat("Default", &w.f3)) g_dirty = true;
                break;
            case W_SLIDER_INT: {
                int mn = (int)w.f1, mx = (int)w.f2, def = (int)w.f3;
                if (ImGui::InputInt("Min", &mn))     { w.f1 = (float)mn;  g_dirty = true; }
                if (ImGui::InputInt("Max", &mx))     { w.f2 = (float)mx;  g_dirty = true; }
                if (ImGui::InputInt("Default", &def)){ w.f3 = (float)def; g_dirty = true; }
                break;
            }
            case W_COLOR_EDIT4:
                if (ImGui::ColorEdit4("Color", w.c)) g_dirty = true;
                break;
            case W_TEXT:
            case W_INPUT_TEXT:
                if (ImGui::InputText("Text", w.text, sizeof(w.text))) g_dirty = true;
                break;
            case W_COMBO:
                if (ImGui::InputText("Items (| separated)", w.items, sizeof(w.items))) g_dirty = true;
                break;
            default:
                ImGui::TextDisabled("No extra properties for this widget.");
                break;
        }
    } else {
        ImGui::TextDisabled("Select a widget to edit its properties.");
    }
}

static void codeTab() {
    if (g_dirty) rebuildAll();

    if (ImGui::Button("Copy Code")) {
        if (g_dirty) rebuildAll();
        jCopy(g_code);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save")) {
        if (g_dirty) rebuildAll();
        jSave(g_save);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load")) {
        std::string c = jLoad();
        if (!c.empty()) parseSave(c);
    }

    if (ImGui::BeginChild("code", ImVec2(0, 0), ImGuiChildFlags_Borders, ImGuiWindowFlags_None)) {
        ImGui::TextUnformatted(g_code.c_str(), g_code.c_str() + g_code.size());
    }
    ImGui::EndChild();
}

static void settingsTab() {
    static float lastScale = 0.0f;
    if (lastScale <= 0.0f) lastScale = g_scale;  // scale was applied once in step()

    if (ImGui::InputText("Window Title", g_title, sizeof(g_title)))
        g_dirty = true;

    if (ImGui::ColorEdit4("Clear Color", &g_bg.x))
        g_dirty = true;

    if (ImGui::SliderFloat("UI Scale", &g_scale, 1.0f, 3.5f)) {
        if (g_scale != lastScale) {
            if (lastScale > 0.0f)
                ImGui::GetStyle().ScaleAllSizes(g_scale / lastScale);
            lastScale = g_scale;
        }
        g_dirty = true;
    }

    if (!g_savePath.empty()) {
        ImGui::Separator();
        ImGui::TextDisabled("Save path: %s", g_savePath.c_str());
    }
}

static void UI() {
    // (A) Builder window
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2((float)screenWidth - 20, 330 * g_scale), ImGuiCond_Once);
    if (ImGui::Begin("ImGui Builder", NULL, ImGuiWindowFlags_NoCollapse)) {
        if (ImGui::BeginTabBar("BuilderTabs", ImGuiTabBarFlags_None)) {
            if (ImGui::BeginTabItem("Widgets")) {
                widgetsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Code")) {
                codeTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Settings")) {
                settingsTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();

    // (B) Preview window - live rendering of the designed menu
    ImGui::SetNextWindowPos(ImVec2(10, 360 * g_scale), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2((float)screenWidth - 20, (float)screenHeight - 360 * g_scale - 10), ImGuiCond_Once);
    char previewTitle[96];
    snprintf(previewTitle, sizeof(previewTitle), "%s###Preview", g_title);
    if (ImGui::Begin(previewTitle, NULL, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings)) {
        if (g_widgets.empty())
            ImGui::TextDisabled("Add widgets from the palette above.");
        for (size_t i = 0; i < g_widgets.size(); ++i)
            renderWidget(g_widgets[i]);
    }
    ImGui::End();
}

// ---------------------------------------------------------------------------
// JNI declarations
// ---------------------------------------------------------------------------
extern "C" {
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_init(JNIEnv* env, jclass cls);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_resize(JNIEnv* env, jobject obj, jint width, jint height);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_step(JNIEnv* env, jobject obj);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_imgui_1Shutdown(JNIEnv* env, jobject obj);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_MotionEventClick(JNIEnv* env, jobject obj, jboolean down, jfloat x, jfloat y);
    JNIEXPORT jstring JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_getCode(JNIEnv* env, jobject obj);
    JNIEXPORT jstring JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_getSaveContent(JNIEnv* env, jobject obj);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_loadContent(JNIEnv* env, jobject obj, jstring content);
    JNIEXPORT void JNICALL Java_com_qoder_imguibuilder_GLES3JNIView_nativeSetSavePath(JNIEnv* env, jobject obj, jstring path);
};

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_init(JNIEnv* env, jclass cls) {
    if (g_Initialized) return;

    env->GetJavaVM(&g_jvm);

    // Setup ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;

    ImGui::StyleColorsDark();

    // Setup Platform/Renderer backends
    ImGui_ImplAndroid_Init(NULL);
    ImGui_ImplOpenGL3_Init("#version 300 es");

    // Embedded font (Custom3 array from FONTS/DEFAULT.h)
    ImFontConfig fontConfig;
    fontConfig.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF(const_cast<std::uint8_t*>(Custom3), sizeof(Custom3), 28.0f, &fontConfig);

    g_Initialized = true;
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_resize(JNIEnv* env, jobject obj, jint width, jint height) {
    screenWidth = (int)width;
    screenHeight = (int)height;
    glViewport(0, 0, width, height);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = NULL;
    io.DisplaySize = ImVec2((float)width, (float)height);
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_step(JNIEnv* env, jobject obj) {
    // Start the Dear ImGui frame
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplAndroid_NewFrame(screenWidth, screenHeight);
    ImGui::NewFrame();

    if (!g_scaleApplied) {
        ImGui::GetStyle().ScaleAllSizes(g_scale);
        g_scaleApplied = true;
    }

    UI();

    // Render
    ImGui::Render();
    glClearColor(g_bg.x, g_bg.y, g_bg.z, g_bg.w);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_imgui_1Shutdown(JNIEnv* env, jobject obj) {
    if (!g_Initialized) return;
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplAndroid_Shutdown();
    ImGui::DestroyContext();
    g_Initialized = false;
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_MotionEventClick(JNIEnv* env, jobject obj, jboolean down, jfloat x, jfloat y) {
    ImGuiIO& io = ImGui::GetIO();
    io.MouseDown[0] = down;
    io.MousePos = ImVec2(x, y);
}

JNIEXPORT jstring JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_getCode(JNIEnv* env, jobject obj) {
    if (g_dirty) rebuildAll();
    return env->NewStringUTF(g_code.c_str());
}

JNIEXPORT jstring JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_getSaveContent(JNIEnv* env, jobject obj) {
    if (g_dirty) rebuildAll();
    return env->NewStringUTF(g_save.c_str());
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_loadContent(JNIEnv* env, jobject obj, jstring content) {
    if (!content) return;
    const char* utf = env->GetStringUTFChars(content, NULL);
    std::string s = utf ? utf : "";
    if (utf) env->ReleaseStringUTFChars(content, utf);
    if (!s.empty()) parseSave(s);
    g_dirty = true;
}

JNIEXPORT void JNICALL
Java_com_qoder_imguibuilder_GLES3JNIView_nativeSetSavePath(JNIEnv* env, jobject obj, jstring path) {
    if (!path) return;
    const char* utf = env->GetStringUTFChars(path, NULL);
    if (utf) {
        g_savePath = utf;
        env->ReleaseStringUTFChars(path, utf);
    }
}
