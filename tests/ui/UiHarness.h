#pragma once

// UI test harness: the real editor in a real desktop window, driven like a user.
//
// - Bot injects mouse / wheel / key events through the window's ComponentPeer,
//   i.e. the same entry point the operating system uses: JUCE does the hit
//   testing (through the 75-200 % scale transform), drag tracking, click
//   counting and modal blocking exactly as with a real mouse. Every click also
//   checks that the point lands on the intended control.
// - Harness owns a processor (temp preset and capture folders: tests never
//   touch the user's Documents), opens its editor on the desktop and feeds it
//   audio at real-time pace so the UI timers see what a DAW session shows.
// - Screenshots of each scenario go to NOVAPITCH_UI_SHOTS (env) or the build's
//   ui-shots folder; CI uploads them.

#include <catch2/catch_test_macros.hpp>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "UiTestAccess.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"
#include "ui/PluginEditor.h"
#include "unit/TestHelpers.h"

#include <cstdlib>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

#ifndef NOVAPITCH_UI_SHOTS_DIR
 #define NOVAPITCH_UI_SHOTS_DIR "ui-shots"
#endif

namespace novapitch::uitest
{

inline juce::String u8 (const char* text) { return juce::String::fromUTF8 (text); }

/** Runs the message loop (UI timers, async updates, repaints) for `ms`. */
inline void pump (int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

/** Runs the message loop until `condition` holds (or `timeoutMs` passes) and returns it.
    Use it for everything a UI timer updates: CI machines can be slow to fire timers. */
inline bool waitFor (const std::function<bool()>& condition, int timeoutMs = 2000)
{
    const auto end = juce::Time::getMillisecondCounter() + static_cast<juce::uint32> (timeoutMs);
    while (! condition())
    {
        if (juce::Time::getMillisecondCounter() > end)
            return false;
        pump (15);
    }
    return true;
}

inline bool hasDisplay()
{
   #if JUCE_LINUX || JUCE_BSD
    return std::getenv ("DISPLAY") != nullptr && *std::getenv ("DISPLAY") != 0;
   #else
    return true;
   #endif
}

#define NOVAPITCH_REQUIRE_DISPLAY()                                                    \
    do {                                                                               \
        if (! novapitch::uitest::hasDisplay())                                         \
            SKIP ("UI tests need a display: run them under xvfb-run -a on Linux");     \
    } while (false)

inline juce::File shotsDir()
{
    if (auto* env = std::getenv ("NOVAPITCH_UI_SHOTS"); env != nullptr && *env != 0)
        return juce::File (juce::String::fromUTF8 (env));
    return juce::File (juce::String::fromUTF8 (NOVAPITCH_UI_SHOTS_DIR));
}

/** Writes `c` to <shots>/<name>-100.png and -200.png (HiDPI rendering). */
inline bool shot (juce::Component& c, const juce::String& name)
{
    auto dir = shotsDir();
    dir.createDirectory();
    bool ok = true;
    for (const float scale : { 1.0f, 2.0f })
    {
        const auto file = dir.getChildFile (name + "-" + juce::String (juce::roundToInt (scale * 100.0f)) + ".png");
        file.deleteFile();
        juce::FileOutputStream out (file);
        juce::PNGImageFormat png;
        ok = ok && out.openedOk() && png.writeImageToStream (c.createComponentSnapshot (c.getLocalBounds(), true, scale), out);
    }
    return ok;
}

//==============================================================================
template <typename T = juce::Component>
T* find (juce::Component& root, const juce::String& id)
{
    if (root.getComponentID() == id)
        if (auto* t = dynamic_cast<T*> (&root))
            return t;
    for (auto* child : root.getChildren())
        if (auto* found = find<T> (*child, id))
            return found;
    return nullptr;
}

template <typename T = juce::Component>
T& get (juce::Component& root, const juce::String& id)
{
    auto* c = find<T> (root, id);
    INFO ("component id: " << id);
    REQUIRE (c != nullptr);
    return *c;
}

inline void forEach (juce::Component& root, const std::function<void (juce::Component&)>& fn)
{
    for (auto* child : root.getChildren())
    {
        fn (*child);
        forEach (*child, fn);
    }
}

//==============================================================================
struct MenuEntry
{
    int id = 0;
    juce::String text;
    bool enabled = true, ticked = false, subMenu = false, header = false;
};

inline std::vector<MenuEntry> entries (const juce::PopupMenu& menu)
{
    std::vector<MenuEntry> out;
    for (juce::PopupMenu::MenuItemIterator it (menu, true); it.next();)
    {
        const auto& item = it.getItem();
        if (item.isSeparator)
            continue;
        out.push_back ({ item.itemID, item.text, item.isEnabled, item.isTicked, item.subMenu != nullptr, item.isSectionHeader });
    }
    return out;
}

/** Id of the item with exactly this text, else of the first one starting with it; -1 if none. */
inline int itemId (const juce::PopupMenu& menu, const juce::String& text, bool* enabled = nullptr)
{
    const auto all = entries (menu);
    for (const bool exact : { true, false })
        for (const auto& e : all)
            if (! e.subMenu && ! e.header && (exact ? e.text == text : e.text.startsWith (text)))
            {
                if (enabled != nullptr)
                    *enabled = e.enabled;
                return e.id;
            }
    return -1;
}

/** The button of a dialog with this text (look it up before clicking: the click closes
    and deletes the dialog). */
inline juce::Button& dialogButton (juce::AlertWindow& dialog, const juce::String& text)
{
    juce::Button* found = nullptr;
    for (int i = 0; i < dialog.getNumButtons(); ++i)
        if (dialog.getButton (i)->getButtonText() == text)
            found = dialog.getButton (i);
    INFO ("dialog button " << text);
    REQUIRE (found != nullptr);
    return *found;
}

/** True if a pop-up menu window is open (and closes it). */
inline bool closeOpenMenus()
{
    pump (80);
    const bool any = juce::PopupMenu::dismissAllActiveMenus();
    pump (80);
    return any;
}

//==============================================================================
/** Mouse and keyboard through the window's ComponentPeer (the OS entry point). */
class Bot
{
public:
    static juce::ModifierKeys shift() { return juce::ModifierKeys (juce::ModifierKeys::shiftModifier); }
    static juce::ModifierKeys alt() { return juce::ModifierKeys (juce::ModifierKeys::altModifier); }
    static juce::ModifierKeys command() { return juce::ModifierKeys (juce::ModifierKeys::commandModifier); }

    /** Component that a click at `local` (in `c` coordinates) reaches. */
    static juce::Component* hit (juce::Component& c, juce::Point<float> local)
    {
        auto& top = *c.getTopLevelComponent();
        return top.getComponentAt (top.getLocalPoint (&c, local).toInt());
    }

    void move (juce::Component& c, juce::Point<float> local, juce::ModifierKeys keys = {})
    {
        send (c, local, keys, {});
    }

    void click (juce::Component& c, std::optional<juce::Point<float>> local = {}, juce::ModifierKeys keys = {},
                bool right = false, bool requireHit = true)
    {
        const auto p = local.value_or (c.getLocalBounds().toFloat().getCentre());
        if (requireHit)
            checkHit (c, p);
        const auto button = juce::ModifierKeys (right ? juce::ModifierKeys::rightButtonModifier : juce::ModifierKeys::leftButtonModifier);
        now = juce::jmax (now + 700, juce::Time::currentTimeMillis()); // never a double click by accident
        send (c, p, keys, {});
        send (c, p, keys, button);
        now += 60;
        send (c, p, keys, {});
        reset();
        pump (40);
    }

    void doubleClick (juce::Component& c, std::optional<juce::Point<float>> local = {})
    {
        const auto p = local.value_or (c.getLocalBounds().toFloat().getCentre());
        checkHit (c, p);
        now = juce::jmax (now + 700, juce::Time::currentTimeMillis());
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        for (int i = 0; i < 2; ++i)
        {
            send (c, p, {}, left);
            now += 40;
            send (c, p, {}, {});
            now += 60;
        }
        reset();
        pump (40);
    }

    void rightClick (juce::Component& c, std::optional<juce::Point<float>> local = {}, juce::ModifierKeys keys = {})
    {
        click (c, local, keys, true);
    }

    /** Press at `from`, move in `steps` to `to`, release (`c` coordinates at the moment
        of the press: like a real mouse, the path is fixed on screen even if `c` moves
        or resizes during the drag, e.g. a resize corner). */
    void drag (juce::Component& c, juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys keys = {}, int steps = 12)
    {
        checkHit (c, from);
        auto& top = *c.getTopLevelComponent();
        const auto p0 = top.getLocalPoint (&c, from), p1 = top.getLocalPoint (&c, to);
        const auto left = juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier);
        now = juce::jmax (now + 700, juce::Time::currentTimeMillis());
        sendAt (top, p0, keys, {});
        sendAt (top, p0, keys, left);
        for (int i = 1; i <= steps; ++i)
        {
            now += 16;
            sendAt (top, p0 + (p1 - p0) * (static_cast<float> (i) / static_cast<float> (steps)), keys, left);
        }
        now += 16;
        sendAt (top, p1, keys, {});
        reset();
        pump (40);
    }

    void wheel (juce::Component& c, juce::Point<float> local, float deltaY, juce::ModifierKeys keys = {})
    {
        raise (c);
        move (c, local, keys);
        juce::ModifierKeys::currentModifiers = keys;
        juce::MouseWheelDetails w;
        w.deltaX = 0.0f;
        w.deltaY = deltaY;
        w.isReversed = false;
        w.isSmooth = false;
        w.isInertial = false;
        now += 50;
        auto& top = *c.getTopLevelComponent();
        top.getPeer()->handleMouseWheel (juce::MouseInputSource::InputSourceType::mouse, top.getLocalPoint (&c, local), now, w);
        reset();
        pump (30);
    }

    /** Key press routed like ComponentPeer does: to the focused component when the
        window has OS focus, otherwise from `target` up its parents (CI desktops do
        not always give keyboard focus to a test window). */
    bool key (juce::Component& target, const juce::KeyPress& k)
    {
        bool used = false;
        juce::ModifierKeys::currentModifiers = k.getModifiers();
        if (auto* peer = target.getPeer(); peer != nullptr && target.hasKeyboardFocus (true))
            used = peer->handleKeyPress (k);
        else
            for (auto* c = &target; c != nullptr && ! used; c = c->getParentComponent())
                used = c->keyPressed (k);
        reset();
        pump (30);
        return used;
    }

private:
    juce::int64 now = juce::Time::currentTimeMillis();

    static void reset() { juce::ModifierKeys::currentModifiers = {}; }

    /** A user clicks the window they see: bring it in front of the other test windows.
        A moving mouse also dismisses any tooltip on its way. */
    static void raise (juce::Component& c)
    {
        auto& top = *c.getTopLevelComponent();
        forEach (top, [] (juce::Component& child) {
            if (auto* tip = dynamic_cast<juce::TooltipWindow*> (&child); tip != nullptr && tip->isVisible())
                tip->hideTip();
        });
        if (juce::Desktop::getInstance().getComponent (juce::Desktop::getInstance().getNumComponents() - 1) != &top)
        {
            top.toFront (false);
            pump (20);
        }
    }

    static void checkHit (juce::Component& c, juce::Point<float> p)
    {
        raise (c);
        auto& top = *c.getTopLevelComponent();
        // The window system must also see this window at that point (not covered by another window).
        CHECK (top.contains (top.getLocalPoint (&c, p)));
        auto* under = hit (c, p);
        INFO ("click target: " << c.getComponentID() << " " << typeid (c).name() << " at " << p.toString()
                               << ", reached: " << (under != nullptr ? under->getComponentID() + " " + typeid (*under).name() : juce::String ("nothing")));
        CHECK (c.isShowing());
        CHECK ((under == &c || c.isParentOf (under)));
    }

    void send (juce::Component& c, juce::Point<float> local, juce::ModifierKeys keys, juce::ModifierKeys buttons)
    {
        auto& top = *c.getTopLevelComponent();
        sendAt (top, top.getLocalPoint (&c, local), keys, buttons);
    }

    /** One mouse event at a position of the window (peer coordinates). */
    void sendAt (juce::Component& top, juce::Point<float> positionInWindow, juce::ModifierKeys keys, juce::ModifierKeys buttons)
    {
        auto* peer = top.getPeer();
        REQUIRE (peer != nullptr);
        juce::ModifierKeys::currentModifiers = keys.withFlags (buttons.getRawFlags());
        peer->handleMouseEvent (juce::MouseInputSource::InputSourceType::mouse, positionInWindow,
                                keys.withFlags (buttons.getRawFlags()), juce::MouseInputSource::defaultPressure,
                                juce::MouseInputSource::defaultOrientation, now);
        now += 8;
    }
};

//==============================================================================
/** Temporary folder removed at the end of the test. */
struct TempFolder
{
    juce::File dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("NovaPitchUiTest", "");
    TempFolder() { dir.createDirectory(); }
    ~TempFolder() { dir.deleteRecursively(); }
};

/** A processor with its editor open in a desktop window. */
class Harness
{
public:
    static constexpr double kSampleRate = 48000.0;
    static constexpr int kBlock = 256;
    static constexpr int kWindowX = 20, kWindowY = 20;

    explicit Harness (int width = PluginEditor::kBaseWidth, bool open = true)
    {
        proc = std::make_unique<NovaPitchProcessor>();
        proc->getPresetManager().setUserFolder (temp.dir.getChildFile ("Presets"));
        proc->setCaptureRoot (temp.dir.getChildFile ("Captures"));
        proc->prepareToPlay (kSampleRate, kBlock);
        if (open)
            openEditor (width);
    }

    ~Harness()
    {
        closeEditor();
        proc->setPlayHead (nullptr);
        proc->releaseResources();
        proc.reset();
        pump (50);
    }

    void openEditor (int width = PluginEditor::kBaseWidth)
    {
        closeEditor();
        window.reset (proc->createEditorAndMakeActive());
        REQUIRE (window != nullptr);
        window->setTopLeftPosition (kWindowX, kWindowY);
        // No native frame: like a plugin view embedded in a host window. (A native
        // title bar makes Windows / macOS apply size limits to the frame and clamp
        // titled windows to the screen.)
        window->addToDesktop (juce::ComponentPeer::windowAppearsOnTaskbar);
        window->setVisible (true);
        setWidth (width);
        pump (120);
    }

    void closeEditor()
    {
        if (window != nullptr)
        {
            proc->editorBeingDeleted (window.get()); // what every plugin wrapper does first
            window->removeFromDesktop();
            window.reset();
            pump (60);
        }
    }

    /** Largest width <= `wanted` (760:640, at least 75 %) whose window fits on the
        primary display, for tests that click with the real mouse path (CI desktops
        can be as small as 1024 x 768). */
    static int fitWidth (int wanted)
    {
        int fit = wanted;
        if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
        {
            const auto area = display->userBounds.toNearestInt();
            const int maxW = area.getRight() - kWindowX - 8;
            const int maxH = area.getBottom() - kWindowY - 8;
            fit = juce::jmin (wanted, maxW, maxH * PluginEditor::kBaseWidth / PluginEditor::kBaseHeight);
        }
        return juce::jmax (PluginEditor::kBaseWidth * 3 / 4, fit);
    }

    /** Resizes the window the way a host does (through the editor's constrainer). */
    void setWidth (int width)
    {
        const int height = juce::roundToInt (width * static_cast<double> (PluginEditor::kBaseHeight) / PluginEditor::kBaseWidth);
        window->setSize (width, height);
        pump (40);
    }

    PluginEditor& ed() { return UiTestAccess::editor (*window); }
    juce::Component& content() { return UiTestAccess::content (ed()); }
    NovaPitchProcessor& p() { return *proc; }

    template <typename T = juce::Component>
    T& get (const juce::String& id) { return uitest::get<T> (*window, id); }

    float param (const juce::String& id) { return proc->getValueTreeState().getRawParameterValue (id)->load(); }
    void setParam (const juce::String& id, float value) { test::setParam (*proc, id, value); }

    /** Processes a mono signal (copied to both channels) at about real-time pace,
        letting the UI timers run between blocks, as in a DAW. */
    void feed (const std::vector<float>& mono, test::TestPlayHead* head = nullptr, juce::MidiBuffer* midiIn = nullptr,
               bool realtime = true, bool settle = true)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        juce::MidiBuffer midi;
        const int blocksPerSlice = juce::jmax (1, static_cast<int> (0.03 * kSampleRate / kBlock));
        int block = 0;
        for (size_t pos = 0; pos + kBlock <= mono.size(); pos += kBlock, ++block)
        {
            if (head != nullptr)
            {
                head->timeInSamples = static_cast<int64_t> (pos);
                proc->setPlayHead (head);
            }
            buffer.copyFrom (0, 0, mono.data() + pos, kBlock);
            buffer.copyFrom (1, 0, mono.data() + pos, kBlock);
            midi.clear();
            if (midiIn != nullptr && pos == 0)
                midi.addEvents (*midiIn, 0, kBlock, 0);
            proc->processBlock (buffer, midi);
            lastOutput.insert (lastOutput.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + kBlock);
            if (realtime && (block + 1) % blocksPerSlice == 0)
                pump (30);
        }
        if (realtime && settle)
            pump (60);
    }

    /** One block with only MIDI (CC, notes). */
    void sendMidi (const juce::MidiBuffer& events)
    {
        juce::AudioBuffer<float> buffer (2, kBlock);
        buffer.clear();
        juce::MidiBuffer midi (events);
        proc->processBlock (buffer, midi);
        pump (80);
    }

    bool shot (const juce::String& name) { return uitest::shot (*window, name); }

    /** Clicks `c` and tells whether that opened a pop-up menu (counted when the menu
        window is created, so it does not depend on the test window having OS focus). */
    bool clickOpensMenu (Bot& bot, juce::Component& c, std::optional<juce::Point<float>> at = {}, bool right = false,
                         const juce::String& shotName = {})
    {
        const int before = UiTestAccess::menusOpened (ed());
        bot.click (c, at, {}, right);
        pump (80);
        const bool opened = UiTestAccess::menusOpened (ed()) > before;
        if (opened && shotName.isNotEmpty()) // the menu window: the newest desktop component
            for (int i = juce::Desktop::getInstance().getNumComponents(); --i >= 0;)
                if (auto* top = juce::Desktop::getInstance().getComponent (i); top != window.get() && top->isShowing())
                {
                    uitest::shot (*top, shotName);
                    break;
                }
        closeOpenMenus();
        return opened;
    }

    std::vector<float> lastOutput;
    std::unique_ptr<juce::AudioProcessorEditor> window;

private:
    TempFolder temp;
    std::unique_ptr<NovaPitchProcessor> proc;
};

/** Counts the gestures and value changes the processor reports to the host. */
struct HostListener final : juce::AudioProcessorListener
{
    explicit HostListener (juce::AudioProcessor& p) : processor (p) { processor.addListener (this); }
    ~HostListener() override { processor.removeListener (this); }

    void audioProcessorParameterChanged (juce::AudioProcessor*, int index, float) override { changed.push_back (index); }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int index) override { begins.push_back (index); }
    void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int index) override { ends.push_back (index); }

    bool sawGesture (int index) const
    {
        return std::find (begins.begin(), begins.end(), index) != begins.end() && std::find (ends.begin(), ends.end(), index) != ends.end();
    }
    void clear() { changed.clear(); begins.clear(); ends.clear(); }

    juce::AudioProcessor& processor;
    std::vector<int> changed, begins, ends;
};

inline int paramIndex (NovaPitchProcessor& p, const juce::String& id)
{
    return p.getValueTreeState().getParameter (id)->getParameterIndex();
}

} // namespace novapitch::uitest
