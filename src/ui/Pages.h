#pragma once
#include "Visuals.h"
#include "../gen/Generator.h"
#include <thread>

namespace gx::ui
{
// Container that forwards frames to every Animated descendant that is showing.
class Page : public juce::Component, public Animated
{
public:
    explicit Page (Context& c) : ctx (c) {}
    void tick (const Frame& f) override { forward (this, f); }
    virtual void refresh() {}          // after preset load / undo
    virtual void pageShown() {}
    static void forward (juce::Component* c, const Frame& f);
protected:
    Context& ctx;
};

class Logo : public juce::Component, public juce::SettableTooltipClient
{
public:
    Logo() { setMouseCursor (juce::MouseCursor::PointingHandCursor); setTooltip ("dskmusic.com"); }
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override { juce::URL ("https://dskmusic.com").launchInDefaultBrowser(); }
    static void drawMark (juce::Graphics&, juce::Rectangle<float>, float glow, float phase = 0.0f);
    float glow = 0, phase = 0;
};

// Round trigger button: plays the effect (or stops it while it is playing).
class PlayButton : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    explicit PlayButton (ApexProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void tick (const Frame&) override;
private:
    ApexProcessor& proc;
    bool on = false, host = false;
    float level = 0, pulse = 0;
};

class Header : public juce::Component, public Animated
{
public:
    Header (Context& c, std::function<void (int)> onTab);
    void paint (juce::Graphics&) override;
    void resized() override;
    void tick (const Frame&) override;
    void setTab (int t);
    void setPresetName (const juce::String& n) { presetName.setText (n, juce::dontSendNotification); }
    std::function<void (int delta)> onPresetStep;
    std::function<void()> onAbout, onAB, onUndo, onRedo;
    juce::TextButton abButton { "A" };
    juce::TextButton undoButton { L ("Deshacer", "Undo") }, redoButton { L ("Rehacer", "Redo") };
private:
    Context& ctx;
    Logo logo;
    PlayButton play;
    juce::OwnedArray<juce::TextButton> tabs;
    juce::TextButton prev { "<" }, next { ">" }, about { "i" };
    juce::Label presetName;
    std::function<void (int)> tabCb;
};

class AboutOverlay : public juce::Component
{
public:
    AboutOverlay();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override { setVisible (false); }
private:
    juce::HyperlinkButton link { "dskmusic.com", juce::URL ("https://dskmusic.com") };
};

// The big GENERATE button (Orbitron) with an animated glow while the designer works.
class GenerateButton : public juce::Component, public juce::SettableTooltipClient, public Animated
{
public:
    GenerateButton();
    void paint (juce::Graphics&) override;
    void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
    void mouseEnter (const juce::MouseEvent&) override { over = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { over = false; repaint(); }
    void tick (const Frame& f) override { t = f.time; if (busy || over) repaint(); }
    std::function<void()> onClick;
    bool busy = false;
    juce::String text = "GENERATE";
private:
    bool over = false; double t = 0;
};

// "Drag me into your DAW or video editor": drags the current effect out as a WAV file.
class DragHandle : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit DragHandle (Context& c) : ctx (c) { setMouseCursor (juce::MouseCursor::DraggingHandCursor); }
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent& e) override { if (! started && e.getDistanceFromDragStart() > 6) { started = true; if (ctx.startDrag) ctx.startDrag(); } }
    void mouseUp (const juce::MouseEvent&) override { started = false; }
private:
    Context& ctx;
    bool started = false;
};

// Main page: type, style, timeline, GENERATE, time controls and the six macros.
class FxPage : public Page
{
public:
    explicit FxPage (Context& c);
    ~FxPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    void tick (const Frame&) override;
    void refresh() override;
    bool isBusy() const { return busy.load(); }
    bool handleKey (const juce::KeyPress&);
    void presetLoaded() {}
    std::function<void()> onPresetChanged;
    struct UiState { bool lockStyle = false; };
    UiState uiState() const { return { lockStyle.getToggleState() }; }
    void setUiState (const UiState& s) { lockStyle.setToggleState (s.lockStyle, juce::dontSendNotification); }
    void generate();                 // new effect of the current type
private:
    void runDesigner (std::function<Preset()> job, const juce::String& what);
    void applyDesigned (const Preset& p);
    void updateDurationControls();
    void updateStyleButtons();
    TimelineView timeline;
    CinemaView cinema;
    GenerateButton genBtn;
    DragHandle dragHandle;
    juce::OwnedArray<juce::TextButton> typeBtns, styleBtns;
    juce::TextButton newSeed { L ("Nueva semilla", "New seed") }, mutate { L ("Mutar", "Mutate") }, mix { L ("Mezclar", "Mix") },
                     seedPrev { "<" }, seedNext { ">" }, playBtn { L ("Reproducir", "Play") }, stopBtn { "Stop" };
    juce::ToggleButton lockStyle { L ("Fijar estilo", "Lock style") };
    juce::Label seedLabel, status, hint;
    Box timePanel, macroPanel;
    ParamKnob *kSeconds = nullptr, *kBeats = nullptr, *kFrames = nullptr;
    ParamCombo *cFps = nullptr;
    std::thread worker;
    std::atomic<bool> busy { false }, ready { false }, cancel { false };
    Preset result;
    juce::SpinLock resultLock;
    double busySince = 0, msgAt = 0; int msgIdx = 0;
    juce::String pendingWhat;
    uint32_t rngCounter = 1;
    int lastTypeKey = -1;
};

// Layer strips + master + trigger / host sync.
class LayersPage : public Page
{
public:
    explicit LayersPage (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    struct Strip : public Box { Strip (Context& c, int layer); int layer; };
    juce::OwnedArray<Strip> strips;
    Box master, trigger;
};

class PresetsPage : public Page, private juce::ListBoxModel, private juce::TextEditor::Listener
{
public:
    explicit PresetsPage (Context& c);
    ~PresetsPage() override { ctx.library.onChange = nullptr; }
    void resized() override;
    void paint (juce::Graphics&) override;
    void refresh() override;
    void pageShown() override { refresh(); }
    bool keyPressed (const juce::KeyPress&) override;
    void step (int delta);                         // header < > buttons
    int rowOfPreset (const juce::String& name) const;  // row in the current filtered list, -1 if hidden
    std::function<void (const Preset&)> onLoaded;
private:
    int getNumRows() override { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void selectedRowsChanged (int row) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void textEditorTextChanged (juce::TextEditor&) override { requery(); }
    void requery();
    void loadRow (int row);
    void showRowMenu (int row);
    void editMeta (int libIndex);
    void rebuildCategoryList();

    juce::TextEditor search;
    juce::ListBox list { "presets", this };
    juce::ListBox catList, subList;
    struct StringModel : juce::ListBoxModel
    {
        juce::StringArray items; int selected = 0; std::function<void (int)> onSelect;
        int getNumRows() override { return items.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool sel) override;
        void selectedRowsChanged (int row) override { selected = row; if (onSelect) onSelect (row); }
    } catModel, subModel;
    juce::OwnedArray<juce::TextButton> tagButtons;
    juce::ComboBox typeBox, sortBox, authorBox, folderBox;
    juce::ToggleButton favOnly { L ("Favoritos", "Favourites") };
    juce::TextButton saveBtn { L ("Guardar", "Save") }, saveAsBtn { L ("Guardar como", "Save as") }, importBtn { L ("Importar", "Import") }, exportBtn { L ("Exportar banco", "Export bank") },
                     prevBtn { L ("Anterior", "Previous") }, nextBtn { L ("Siguiente", "Next") }, folderBtn { L ("Nueva carpeta", "New folder") }, rescanBtn { L ("Reescanear", "Rescan") };
    juce::Label info;
    std::vector<int> shown;
    bool syncingSelection = false;
    std::unique_ptr<juce::FileChooser> chooser;
};

// Export: single effect (WAV / FLAC / OGG, head and tail), numbered packs with metadata, preset files.
class ExportPage : public Page, private juce::Timer
{
public:
    explicit ExportPage (Context& c);
    ~ExportPage() override;
    void resized() override;
    void paint (juce::Graphics&) override;
    bool isBusy() const { return busy.load(); }
private:
    void timerCallback() override;
    void exportSingle();
    void exportPackNow();
    void startJob (std::function<juce::String()> job, const juce::String& what);
    juce::ComboBox format, rate, packCount;
    juce::Slider head, tailPad;
    juce::TextButton exportOne { L ("Exportar efecto…", "Export effect…") }, exportPackBtn { L ("Exportar pack…", "Export pack…") }, cancelBtn { L ("Cancelar", "Cancel") },
                     exportPreset { L ("Exportar preset .apex", "Export .apex preset") }, importPreset { L ("Importar preset o banco", "Import preset or bank") },
                     openFolder { L ("Abrir carpeta de arrastre", "Open drag folder") };
    std::unique_ptr<juce::FileChooser> chooser;
    std::thread worker;
    std::atomic<bool> busy { false }, cancelFlag { false }, done { false };
    std::atomic<float> progress { 0 };
    juce::String result, status;
    juce::SpinLock resLock;
};

class SettingsPage : public Page
{
public:
    SettingsPage (Context& c, std::function<void (float)> setScale, std::function<void()> showAbout);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    juce::ComboBox scaleBox, animBox, langBox;
    juce::ToggleButton gpuToggle { L ("Visual de cine en alta calidad (tarjeta gráfica)", "High-quality cinematic visual (graphics card)") };
    juce::ToggleButton aiMsgs { L ("Mensajes del diseñador al generar", "Designer messages while generating") }, preview { L ("Sonar al elegir un preset o generar", "Play when a preset is selected or generated") };
    juce::Slider historyLimit;
    juce::TextButton openFolder { L ("Abrir carpeta de presets", "Open presets folder") }, clearLearn { L ("Borrar asignaciones MIDI Learn", "Clear MIDI Learn assignments") }, aboutBtn { L ("Acerca de Apex", "About Apex") };
};

class HelpPage : public Page
{
public:
    explicit HelpPage (Context& c);
    void resized() override;
    void paint (juce::Graphics&) override;
private:
    struct Body : public juce::Component { void paint (juce::Graphics&) override; int layoutHeight (int width); };
    juce::Viewport view;
    Body body;
};
}
